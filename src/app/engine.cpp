#include "app/engine.hpp"

#include "organism/mushroom.hpp"
#include "scene/tree_generated.hpp"

#include "seq/layer_sink.hpp"
#include "seq/section_actions.hpp"

#include "core/phase_profiler.hpp"
#include "core/interaction_latency.hpp"
#include "core/phase2_probe.hpp" // TEMPORARY: ui-responsiveness phase 2
#include <optional>

#include <cstdlib>
#include <string_view>
#include <functional>
#include <map>
#include <set>

#include "assets/image.hpp"
#include "core/hash.hpp"
#include "core/log.hpp"
#include "params/serialization.hpp"
#include "world/atmospheric_params.hpp"

#include <nlohmann/json.hpp>

#include <fstream>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <chrono>

namespace avgen::app {

const char* tempoSourceName(TempoSource source) {
    return source == TempoSource::MidiClock ? "midi" : "analysis";
}

std::optional<TempoSource> tempoSourceFromName(std::string_view name) {
    if (name == "analysis") {
        return TempoSource::Analysis;
    }
    if (name == "midi" || name == "midiClock" || name == "midi-clock") {
        return TempoSource::MidiClock;
    }
    return std::nullopt;
}

Engine::Engine(EngineMode mode) : mode_(mode), shaderLayers_(params_) {
    // Generators that scenes can name (ADR-175). Registering here rather than from a static
    // initialiser keeps the order explicit and keeps `scene` free of any knowledge of what a
    // mushroom is; re-registering a name is defined to replace it, so calling this per Engine is
    // harmless.
    organism::registerMushroomGenerator();
    scene::registerTreeGenerator();
    ensureControlSource();
    audioSignals_ = signals::AudioSignals::declare(bus_);
    timeSignals_.seconds = bus_.declare("time.seconds", 0.0f, 3600.0f);
    timeSignals_.progress = bus_.declare("time.progress");
    timeSignals_.playing = bus_.declare("time.playing");
    timeSignals_.beatPhase = bus_.declare("beat.phase");
    timeSignals_.beatPulse = bus_.declare("beat.pulse", 0.0f, 1.0f, true);
    timeSignals_.beatCount = bus_.declare("beat.count", 0.0f, 100000.0f);
    timeSignals_.bpm = bus_.declare("beat.bpm", 0.0f, 300.0f);
    timeSignals_.barPhase = bus_.declare("beat.bar");
    timeSignals_.phrasePhase = bus_.declare("beat.phrase");
    timeSignals_.phraseCount = bus_.declare("beat.phraseCount", 0.0f, 100000.0f);
    timeSignals_.phrasePulse = bus_.declare("beat.phrasePulse", 0.0f, 1.0f, true);
    timeSignals_.sectionPhase = bus_.declare("beat.section");
    timeSignals_.sectionCount = bus_.declare("beat.sectionCount", 0.0f, 100000.0f);
    music_.declare(bus_); // music.beat ... music.impact (ADR-073)
    stateProgressSignal_ = bus_.declare("state.progress");
    stateIndexSignal_ = bus_.declare("state.index", 0.0f, 64.0f);
    sources_.attach(bus_, params_);
    postParams_ = scene::registerPostParameters(params_, post_);
    cameraParams_ = scene::registerCameraParameters(params_, lens_, exposure_, focus_);
    installController(std::make_unique<scene::OrbScene>(params_, modulator_));
    if (mode_ == EngineMode::Live) {
        player_ = std::make_unique<audio::AudioPlayer>();
    }
}

void Engine::ensureControlSource() {
    auto* existing = sources_.find("control", "control");
    if (existing == nullptr) {
        existing = &sources_.add(std::make_unique<signals::ControlSource>("control"));
    }
    auto* control = dynamic_cast<signals::ControlSource*>(existing);
    for (const auto& channel : controlHub_.map().channels()) {
        control->addChannel(channel.name, channel.event);
    }
}

signals::ControlSource& Engine::controlSource() {
    ensureControlSource();
    return *dynamic_cast<signals::ControlSource*>(sources_.find("control", "control"));
}

void Engine::setTempoSource(TempoSource source) {
    if (source == tempoSource_) {
        return;
    }
    tempoSource_ = source;
    // Re-sync the extrapolated beat clock from whichever source is now in charge.
    beatClockPhase_ = 0.0;
    lastAnalysisBeatCount_ = 0;
    log::info("tempo source: {}", tempoSourceName(source));
}

void Engine::installController(std::unique_ptr<scene::SceneController> controller) {
    controller_ = std::move(controller);
    // Live only. An expensive procedural regeneration is allowed to wait for the slider driving it
    // to stop moving, rather than taking the frame away from the editor on every frame of a drag
    // (see Composition::setInteractiveRebuildBudget). Offline never sets it, because the deferral
    // reads a wall clock and a wall clock must not decide what a deterministic render contains.
    //
    // The budget is the cost above which an object is treated as expensive. Two milliseconds:
    // comfortably below a 60 Hz frame's share, comfortably above the cost of a procedural small
    // enough that deferring it would only add latency.
    if (mode_ == EngineMode::Live) {
        if (auto* comp = composition()) {
            comp->setInteractiveRebuildBudget(2.0);
        }
        // The same decision for the playhead, for the same reason and with the same restriction to
        // Live. `AVGEN_NO_SEEK_DEFERRAL=1` turns it off, so the before and after stay runnable out
        // of one binary rather than out of two builds that no longer both exist -- the same reason
        // AVGEN_LEGACY_PROCGEN, AVGEN_SCATTER_WORKERS and AVGEN_NO_TERRAIN_CACHE exist. Without
        // that switch an A/B of this change would have to compare two process runs, which §3 of
        // docs/application-performance.md forbids on a machine whose load average moves from 3 to 44
        // inside one session.
        static const bool noSeekDeferral = std::getenv("AVGEN_NO_SEEK_DEFERRAL") != nullptr;
        setInteractiveSeekBudget(noSeekDeferral ? 0.0 : 2.0);
        // And the ceiling on what one seek's re-simulation may cost, in the unit it is paid in:
        // steps x bodies (ADR-273). Live only, for the same reason -- a deterministic render wants
        // the whole ninety seconds whatever it costs, because nobody is sitting waiting for it.
        //
        // **It is a cast-size ceiling, not a latency dial, and the measurement is why.** Shortening
        // Glowmere's window does not cost a character a little accumulated history; it puts the
        // cast somewhere else entirely. Measured, at t = 90 s on the authored scene: a 45 s window
        // moves a body 195.4 m from where the full replay puts it, and a 22 s window 240.7 m. A
        // faster seek that draws a different frame is not a faster seek (ADR-182), so this may not
        // be set low enough to bite on a scene that is currently getting a correct answer.
        //
        // 180,000 is the whole ninety seconds for any scene with up to thirty-three bodies that
        // need it -- Glowmere's twenty-two deep bodies cost 118,801, so it keeps its exact frame --
        // and it shrinks from there. What it is actually for is the case ADR-267 called the
        // blocker: two hundred and fifty autonomous characters, where one timeline click took
        // 402 s measured here and 594 s in ADR-267, and where the alternative to a shorter window
        // is not a better frame but an editor nobody can use.
        //
        // AVGEN_SEEK_BODY_STEPS overrides it (0 = no ceiling) so the before and after stay runnable
        // out of one binary.
        if (const char* budget = std::getenv("AVGEN_SEEK_BODY_STEPS")) {
            seekBodyStepBudget_ = std::strtoull(budget, nullptr, 10);
        } else {
            seekBodyStepBudget_ = 180000;
        }
    }
    // AVGEN_LEGACY_PROCGEN=1 restores the pre-ADR-233 double generation, in *any* mode, for one
    // purpose: so that a headless capture can be taken both ways out of the same binary and the
    // two compared byte for byte. A switch that only the live editor could reach would leave the
    // determinism claim resting on two builds that no longer both exist -- the same reason
    // AVGEN_SCATTER_WORKERS and AVGEN_NO_TERRAIN_CACHE exist. Unset, which is every ordinary run,
    // nothing here happens.
    if (const char* legacy = std::getenv("AVGEN_LEGACY_PROCGEN");
        legacy != nullptr && std::string_view(legacy) == "1") {
        if (auto* comp = composition()) {
            comp->setLegacyProceduralGeneration(true);
            log::warn("AVGEN_LEGACY_PROCGEN=1: procedurals generate twice per frame (the pre-ADR-233 path)");
        }
    }
    // Scene swaps clear the parameter set, so sources, post settings and shader layers must
    // re-register. Post parameters keep their current base values (post_ holds them).
    ensureControlSource();
    sources_.attach(bus_, params_);
    if (params_.find("audio/inputGain") == nullptr) {
        inputGain_ = &params_.add(params::ParamDesc<float>{.path = "audio/inputGain",
                                                            .defaultValue = 1.0f,
                                                            .hardMin = 0.0f,
                                                            .hardMax = 8.0f,
                                                            .softMin = 0.0f,
                                                            .softMax = 4.0f});
    }
    if (params_.find("post/bloom/intensity") == nullptr) {
        scene::PostSettings keep = post_;
        postParams_ = scene::registerPostParameters(params_, keep);
    }
    if (params_.find("camera/lens/focalLength") == nullptr) {
        const scene::LensSettings keepLens = lens_;
        const scene::ExposureSettings keepExposure = exposure_;
        const scene::FocusSettings keepFocus = focus_;
        cameraParams_ = scene::registerCameraParameters(params_, keepLens, keepExposure, keepFocus);
    }
    // ADR-207: the scene's world effects, and the `worldfx/<name>/...` parameters that make every
    // number on them automatable, keyable and modulatable. The authored set is copied off the
    // composition rather than read through it per frame, because the live set is what modulation
    // writes to and a composition's authored values must survive being modulated.
    //
    // Unregistered first and unconditionally: a scene swap replaces the cast of effects, and a
    // registrar that only ever adds leaves the previous scene's paths behind for a route to bind to.
    timeline_.unbind();
    world::unregisterWorldEffectParameters(params_, worldEffectParams_);
    world::unregisterAtmosphericParameters(params_, atmosphericParams_);
    worldEffects_.clear();
    atmosphericEffects_.clear();
    if (const auto* comp = composition()) {
        worldEffects_ = comp->worldEffects();
        atmosphericEffects_ = comp->atmosphericEffects();
    }
    if (!worldEffects_.empty()) {
        worldEffectParams_ = world::registerWorldEffectParameters(params_, worldEffects_);
    }
    if (!atmosphericEffects_.empty()) {
        atmosphericParams_ = world::registerAtmosphericParameters(params_, atmosphericEffects_);
    }
    resetCameraState();
    shaderLayers_.reattach();
    // The composition's layer parameters (ADR-083), with everything else that has to survive a
    // scene swap -- and before rebind(), because a timeline track naming a parameter that does not
    // exist yet binds to nothing and then does nothing, quietly (ADR-075, ADR-080).
    layers_.detach();
    layers_.attach(params_);
    addDefaultPostRoutes();
    rebind();
    modulator_.resetState();
    // A scene swap cleared the parameter set, so every track the sequence baked is now bound to
    // nothing -- including the ones naming nodes the new scene does have. Re-installing is the only
    // thing that fixes that, and it is what makes "open a project, then swap its scene" behave.
    if (hasSequence()) {
        if (auto r = installSequence(); !r) {
            log::warn("sequence: {}", r.error().message);
            noteBindingProblem(r.error().message);
        }
    }
}

// ---- the cinematic sequence (ADR-089) --------------------------------------------------------

Result<seq::InstallReport> Engine::setSequence(seq::Sequence sequence) {
    sequence_ = std::move(sequence);
    return installSequence();
}

Result<seq::InstallReport> Engine::installSequence() {
    // The sink is built fresh each time and owns nothing between calls: what identifies a
    // sequencer layer is its name, which survives in the stack, not a handle held here.
    seq::CompositionLayerSink sink(layers_, &params_);

    // What a camera behaviour's `clearance` measures against. Supplied here because this is the
    // layer that knows there is a world: `seq/` asks how high the ground is and does not learn what
    // terrain is (see `BakeOptions::groundHeightAt`).
    //
    // `surfaceAt` rather than `heightAt`, so a chase following someone along a shoreline does not
    // dive through the lake on its way -- a shot from under water is a decision, not a side effect.
    // Null when the scene has no terrain, which the bake reports rather than silently ignoring.
    seq::BakeOptions options;
    if (scene::Composition* comp = composition()) {
        if (const world::TerrainQuery ground = comp->terrainQuery(); ground.valid()) {
            options.groundHeightAt = [ground](float x, float z) {
                return ground.surfaceAt(glm::vec2(x, z));
            };
        }
    }
    auto report = seq::install(sequence_, timeline_, params_, sink, sequenceTargets_, options);
    if (!report) {
        // The install left the timeline consistent (old tracks gone) even when the bake failed, so
        // forget the targets: there is nothing left for the next install to erase.
        sequenceTargets_.clear();
        sequenceReport_ = seq::InstallReport{};
        sequenceEvents_.clear();
        firedEvents_.clear();
        return report;
    }
    sequenceTargets_ = report->targets;
    sequenceReport_ = *report;
    // The dispatcher copies the events, so an editor may keep editing `sequence().events` between
    // installs without the running frame reading a reallocated vector.
    sequenceEvents_.setEvents(sequence_.events, sequenceReport_.events);
    sequenceEvents_.reset(timelineClock_.seconds);
    firedEvents_.clear();
    refreshTransport(); // a bake can lengthen or shorten the piece
    for (const std::string& warning : report->warnings) {
        noteBindingProblem(warning);
    }
    log::info("sequence '{}': {} shot(s), {} actor(s), {} overlay cue(s) -> {} track(s), {} key(s), "
              "{} layer(s){}",
              sequence_.name, sequence_.shots.size(), sequence_.actors.size(),
              sequence_.overlays.size(), report->trackCount, report->keyCount,
              report->layersRealised,
              report->unresolved.empty()
                  ? std::string{}
                  : fmt::format(", {} unresolved target(s)", report->unresolved.size()));
    return report;
}

void Engine::clearSequence() {
    seq::CompositionLayerSink sink(layers_, &params_);
    seq::uninstall(timeline_, params_, sink, sequenceTargets_);
    sequenceTargets_.clear();
    sequenceReport_ = seq::InstallReport{};
    sequenceEvents_.clear();
    firedEvents_.clear();
    sequence_ = seq::Sequence{};
    refreshTransport();
}

void Engine::removeLayerParameters() {
    timeline_.unbind(); // the tracks hold pointers into the set these are about to leave
    for (const auto& layer : layers_.layers()) {
        layers_.removeParameters(params_, *layer);
    }
    layers_.detach();
}

void Engine::refreshLayerParameters() {
    layers_.attach(params_);
    rebind();
}

scene::ActiveCameraState Engine::activeCamera() const {
    if (const auto* comp = dynamic_cast<const scene::Composition*>(controller_.get()); comp != nullptr) {
        return comp->activeCamera();
    }
    // No composition: the orb scene and a glTF scene each have exactly one camera and no director,
    // so the honest answer is the main camera, unclaimed. Reported rather than left empty, because a
    // consumer asking "which camera" must never have to distinguish "none" from "not applicable".
    return scene::ActiveCameraState{};
}

Result<void> Engine::setCameraDirection(scene::CameraDirection direction) {
    auto* comp = composition();
    if (comp == nullptr) {
        return fail("this scene has no composition, so it has no camera collection");
    }
    // The slugs that are going away, worked out before the swap. A camera that is deleted must take
    // its parameters and its automation with it: parameters left behind would be saved into the
    // project for ever and tracks left behind would sit unbound, which is precisely the "a target
    // nobody reads" failure ADR-242 is about.
    std::vector<std::string> departing;
    for (const scene::CameraRig& was : comp->cameraDirection().cameras) {
        if (was.id == scene::kMainCamera || was.slug.empty()) {
            continue;
        }
        const bool kept = std::ranges::any_of(direction.cameras, [&](const scene::CameraRig& now) {
            return now.slug == was.slug;
        });
        if (!kept) {
            departing.push_back("cameras/" + was.slug + "/");
        }
    }
    // Unbind first: a track holding a raw `IParameter*` into a parameter that is about to be removed
    // is a dangling pointer the moment it is. The same order `removeLayer` uses.
    timeline_.unbind();
    if (auto ok = comp->setCameraDirection(std::move(direction)); !ok) {
        (void)timeline_.bind(params_);
        return ok;
    }
    if (!departing.empty()) {
        auto& tracks = timeline_.tracks();
        std::erase_if(tracks, [&](const params::Track& track) {
            return std::ranges::any_of(departing, [&](const std::string& prefix) {
                return track.target.starts_with(prefix);
            });
        });
        std::vector<std::string> doomed;
        for (const params::IParameter* param : params_.ordered()) {
            if (std::ranges::any_of(departing,
                                    [&](const std::string& prefix) { return param->path().starts_with(prefix); })) {
                doomed.push_back(param->path());
            }
        }
        for (const std::string& path : doomed) {
            params_.remove(path);
        }
    }
    rebind();
    return {};
}

comp::TextLayer& Engine::addTextLayer(std::string text, double startSeconds, double endSeconds) {
    comp::TextLayer& layer = layers_.addText(std::move(text), startSeconds, endSeconds);
    layer.attach(params_);
    rebind();
    return layer;
}

comp::ShapeLayer& Engine::addShapeLayer(comp::ShapeKind shape) {
    comp::ShapeLayer& layer = layers_.addShape(shape);
    layer.attach(params_);
    rebind();
    return layer;
}

bool Engine::removeLayer(std::uint32_t id) {
    const comp::Layer* layer = layers_.find(id);
    if (layer == nullptr) {
        return false;
    }
    timeline_.unbind();
    layers_.removeParameters(params_, *layer);
    // Tracks that were driving the layer that just left would sit unbound for ever. Dropping them
    // with the layer is the honest thing: the alternative is a saved project full of tracks aimed
    // at nothing, which is exactly the failure this system was built to stop having.
    auto& tracks = timeline_.tracks();
    const std::string prefix = fmt::format("layers/{}/", id);
    std::erase_if(tracks, [&](const params::Track& t) { return t.target.rfind(prefix, 0) == 0; });
    const bool removed = layers_.remove(id);
    rebind();
    return removed;
}

comp::Layer* Engine::duplicateLayer(std::uint32_t id) {
    comp::Layer* copy = layers_.duplicate(id);
    if (copy != nullptr) {
        copy->attach(params_);
        rebind();
    }
    return copy;
}

void Engine::resetCameraState() {
    focusState_.reset();
    cameraStateReset_ = true;
}

std::size_t Engine::addDefaultAtmosphericRoutes(std::string_view effectName) {
    // The kind decides the routes, so an effect that is not there has none to add.
    const auto it = std::find_if(atmosphericEffects_.begin(), atmosphericEffects_.end(),
                                 [&](const world::AtmosphericEffect& e) { return e.name == effectName; });
    if (it == atmosphericEffects_.end()) {
        return 0;
    }
    const std::string prefix = world::atmosphericParameterPrefix(effectName);
    for (const params::ModRoute& r : modulator_.routes()) {
        if (r.target.starts_with(prefix)) {
            return 0; // already automated; leave whatever somebody set up alone
        }
    }
    std::size_t added = 0;
    for (params::ModRoute& r : world::defaultAtmosphericRoutes(effectName, it->kind)) {
        if (params_.find(r.target) == nullptr) {
            log::warn("default route for '{}' targets '{}', which is not a parameter; skipped",
                      effectName, r.target);
            continue;
        }
        modulator_.addRoute(std::move(r));
        ++added;
    }
    if (added > 0) {
        rebind(); // the routes hold pointers into the parameter set, and bind is what fills them
        log::debug("attached {} default audio route(s) to atmospheric effect '{}'", added, effectName);
    }
    return added;
}

void Engine::addDefaultPostRoutes() {
    auto has = [&](const char* target) {
        for (const auto& r : modulator_.routes()) {
            if (r.target == target) {
                return true;
            }
        }
        return false;
    };
    if (!has("post/bloom/intensity")) {
        params::ModRoute r{.source = "audio.rms", .target = "post/bloom/intensity", .amount = 0.6f};
        r.chain.attackMs = 30.0f;
        r.chain.decayMs = 400.0f;
        modulator_.addRoute(r);
    }
    if (!has("post/lens/chromaticAberration")) {
        params::ModRoute r{.source = "audio.onset", .target = "post/lens/chromaticAberration", .amount = 0.35f};
        r.chain.envelope = params::EnvelopeMode::PeakHold;
        r.chain.envelopeHoldMs = 20.0f;
        r.chain.envelopeFallPerSecond = 6.0f;
        modulator_.addRoute(r);
    }
}

void Engine::rebind() {
    if (controlSource().needsAttach()) {
        sources_.attach(bus_, params_); // new control channels must exist on the bus first
    }
    if (auto r = modulator_.bind(bus_, params_); !r) {
        log::warn("modulation bind: {}", r.error().message);
        noteBindingProblem(fmt::format("modulation: {}", r.error().message));
    }
    if (auto r = timeline_.bind(params_); !r) {
        log::warn("{}", r.error().message);
        noteBindingProblem(r.error().message);
    }
    // An entity whose reaction resolved to nothing is the same class of failure and belongs in the
    // same list: a binding that does nothing must be visible somewhere a person looks, not only in
    // a log line that scrolled past at start-up.
    if (const auto* comp = composition()) {
        for (const std::string& problem : comp->entityProblems()) {
            noteBindingProblem(problem);
        }
    }
}

void Engine::noteBindingProblem(std::string message) {
    if (std::find(projectWarnings_.begin(), projectWarnings_.end(), message) != projectWarnings_.end()) {
        return;
    }
    projectWarnings_.push_back(std::move(message));
}

Result<void> Engine::setWorldEffects(std::vector<world::WorldEffect> effects) {
    // Validated before anything is touched, so a refusal leaves the engine exactly as it was.
    if (auto ok = world::validateWorldEffects(effects); !ok) {
        return ok;
    }
    // The composition owns the authored set (it is what a save writes); the engine owns the live
    // one. Writing both here is what stops the panel and the file drifting apart.
    if (auto* comp = composition()) {
        if (auto ok = comp->setWorldEffects(effects); !ok) {
            return ok;
        }
    }
    // The parameter set changes shape -- effects appear and disappear -- so the timeline has to let
    // go of its pointers before the old paths are removed, and rebind afterwards.
    timeline_.unbind();
    world::unregisterWorldEffectParameters(params_, worldEffectParams_);
    worldEffects_ = std::move(effects);
    if (!worldEffects_.empty()) {
        worldEffectParams_ = world::registerWorldEffectParameters(params_, worldEffects_);
    }
    rebind();
    return {};
}

// ADR-230, on exactly the terms `setWorldEffects` states above.
Result<void> Engine::setAtmosphericEffects(std::vector<world::AtmosphericEffect> effects) {
    if (auto ok = world::validateAtmosphericEffects(effects); !ok) {
        return ok;
    }
    if (auto* comp = composition()) {
        if (auto ok = comp->setAtmosphericEffects(effects); !ok) {
            return ok;
        }
    }
    timeline_.unbind();
    world::unregisterAtmosphericParameters(params_, atmosphericParams_);
    atmosphericEffects_ = std::move(effects);
    if (!atmosphericEffects_.empty()) {
        atmosphericParams_ = world::registerAtmosphericParameters(params_, atmosphericEffects_);
    }
    rebind();
    return {};
}

void Engine::detachSceneParameters() {
    if (auto* comp = composition()) {
        comp->detach();
    }
    shaderLayers_.detach();
    layers_.detach();
    timeline_.unbind();
    // ADR-207. After `timeline_.unbind()`, because a track aimed at a `worldfx/...` path holds a
    // pointer into the parameter that is about to go.
    world::unregisterWorldEffectParameters(params_, worldEffectParams_);
    world::unregisterAtmosphericParameters(params_, atmosphericParams_);
}

params::Track* Engine::recordKey(const std::string& path, int component, params::KeyInterp interp,
                                 params::TimeBase base) {
    auto* track = timeline_.recordKey(params_, path, component, timelineClock_.at(base), interp, base);
    if (track == nullptr) {
        log::warn("timeline: cannot key unknown parameter '{}'", path);
    }
    return track;
}

signals::Source& Engine::addSource(const std::string& kind, const std::string& baseName) {
    std::string name = baseName;
    for (int i = 2; sources_.find(kind, name) != nullptr; ++i) {
        name = baseName + std::to_string(i);
    }
    auto source = signals::SourceRack::create(kind, name);
    if (!source) {
        log::error("unknown source kind '{}'", kind);
        source = signals::SourceRack::create("lfo", name);
    }
    auto& ref = sources_.add(std::move(source));
    rebind();
    return ref;
}

void Engine::removeSource(const std::string& kind, const std::string& name) {
    if (sources_.remove(kind, name)) {
        rebind();
    }
}

params::Preset& Engine::storePreset(const std::string& name) {
    return presets_.add(params::capturePreset(params_, name));
}

bool Engine::recallPreset(const std::string& name) {
    const auto* preset = presets_.find(name);
    if (preset == nullptr) {
        return false;
    }
    params::applyPreset(params_, *preset);
    return true;
}

void Engine::morphPresets(const std::string& a, const std::string& b, float t) {
    const auto* pa = presets_.find(a);
    const auto* pb = presets_.find(b);
    if (pa == nullptr || pb == nullptr) {
        return;
    }
    params::applyPresetBlend(params_, *pa, *pb, t);
}

bool Engine::goToState(const std::string& name, bool instant) {
    return states_.go(name, params_, presets_, instant);
}

void Engine::ensureMacroKnob(const std::string& knob, float defaultValue) {
    auto* source = sources_.find("macro", "macros");
    if (source == nullptr) {
        source = &sources_.add(std::make_unique<signals::MacroSource>("macros"));
    }
    auto* macros = dynamic_cast<signals::MacroSource*>(source);
    if (macros == nullptr) {
        return;
    }
    const auto& knobs = macros->knobs();
    if (std::find(knobs.begin(), knobs.end(), knob) == knobs.end()) {
        macros->addKnob(knob, defaultValue);
        sources_.attach(bus_, params_); // new knob parameter and signal
    }
}

void Engine::applyWorldMacros() {
    // A director's knobs are world macros; installing them keeps a loaded project's director live.
    for (WorldMacro& m : director_.macros()) {
        bool known = false;
        for (const WorldMacro& existing : worldMacros_) {
            known = known || existing.name == m.name;
        }
        if (!known) {
            worldMacros_.push_back(std::move(m));
        }
    }
    for (const WorldMacro& m : worldMacros_) {
        ensureMacroKnob(m.name, m.defaultValue);
        applyWorldMacro(m, modulator_);
    }
    rebind();
}

void Engine::setWorldMacro(WorldMacro macro) {
    bool replaced = false;
    for (WorldMacro& m : worldMacros_) {
        if (m.name == macro.name) {
            m = macro;
            replaced = true;
        }
    }
    if (!replaced) {
        worldMacros_.push_back(macro);
    }
    ensureMacroKnob(macro.name, macro.defaultValue);
    applyWorldMacro(macro, modulator_);
    rebind();
}

void Engine::setDirector(WorldDirector director) {
    clearDirector();
    director_ = std::move(director);
    for (WorldMacro& macro : director_.macros()) {
        setWorldMacro(std::move(macro));
    }
}

void Engine::clearDirector() {
    for (const DirectorMapping& mapping : director_.mappings) {
        removeWorldMacro(directorKnobName(mapping.knob));
    }
    director_ = WorldDirector{};
}

LookApplyResult Engine::applyLookByName(const std::string& name) {
    for (const LookPreset& look : looks_) {
        if (look.name == name) {
            return applyLook(params_, look);
        }
    }
    return LookApplyResult{};
}

bool Engine::removeWorldMacro(const std::string& name) {
    const auto it = std::remove_if(worldMacros_.begin(), worldMacros_.end(),
                                   [&](const WorldMacro& m) { return m.name == name; });
    if (it == worldMacros_.end()) {
        return false;
    }
    worldMacros_.erase(it, worldMacros_.end());
    removeWorldMacroRoutes(name, modulator_);
    // The knob itself is a parameter on the macro source: remove it so a replaced director does
    // not leave stale knobs behind.
    if (auto* source = sources_.find("macro", "macros")) {
        if (auto* macros = dynamic_cast<signals::MacroSource*>(source)) {
            macros->removeKnob(name, params_);
        }
    }
    rebind();
    return true;
}

Result<std::uint32_t> Engine::addShaderLayer(const std::filesystem::path& path, shaders::LayerStage stage) {
    auto id = shaderLayers_.add(path, stage);
    if (id) {
        rebind();
    }
    return id;
}

void Engine::removeShaderLayer(std::uint32_t id) {
    if (shaderLayers_.remove(id)) {
        rebind();
    }
}

namespace {

// Project-relative path policy: relative when on the same root (".." allowed so a project can sit
// beside its assets), absolute otherwise.
std::string relativeTo(const std::filesystem::path& file, const std::filesystem::path& baseDir) {
    std::error_code ec;
    auto abs = std::filesystem::weakly_canonical(file, ec);
    if (ec) {
        abs = std::filesystem::absolute(file).lexically_normal();
    }
    auto base = std::filesystem::weakly_canonical(baseDir, ec);
    if (ec) {
        base = std::filesystem::absolute(baseDir).lexically_normal();
    }
    if (abs.root_name() != base.root_name()) {
        return abs.generic_string();
    }
    const auto rel = abs.lexically_relative(base);
    return rel.empty() ? abs.generic_string() : rel.generic_string();
}

std::filesystem::path resolveFrom(const std::string& stored, const std::filesystem::path& baseDir) {
    std::filesystem::path p(stored);
    if (p.is_absolute()) {
        return p.lexically_normal();
    }
    return (baseDir / p).lexically_normal();
}

// Asset references in the project's "assets" block and shader entries: an object
// { "path", "size", "sha256" } (the old bare string form is still read). Size and hash identify
// the file by content so a moved asset can be relinked.
struct AssetRef {
    std::string path;
    std::uintmax_t size = 0;
    bool hasSize = false;
    std::string sha256;
};

std::optional<AssetRef> readAssetRef(const nlohmann::json& j) {
    AssetRef ref;
    if (j.is_string()) {
        ref.path = j.get<std::string>();
        return ref;
    }
    if (!j.is_object() || !j.contains("path") || !j["path"].is_string()) {
        return std::nullopt;
    }
    ref.path = j["path"].get<std::string>();
    if (j.contains("size") && j["size"].is_number_unsigned()) {
        ref.size = j["size"].get<std::uintmax_t>();
        ref.hasSize = true;
    }
    if (j.contains("sha256") && j["sha256"].is_string()) {
        ref.sha256 = j["sha256"].get<std::string>();
    }
    return ref;
}

// Writes { "path": relative, "size", "sha256" } for a file (size/hash omitted when unreadable).
nlohmann::json assetRefJson(const std::filesystem::path& file, const std::filesystem::path& baseDir) {
    nlohmann::json j = nlohmann::json::object();
    j["path"] = relativeTo(file, baseDir);
    std::error_code ec;
    const auto size = std::filesystem::file_size(file, ec);
    if (!ec) {
        j["size"] = size;
        if (auto hash = sha256File(file)) {
            j["sha256"] = *hash;
        }
    }
    return j;
}

std::string assetRefPath(const nlohmann::json& j) {
    const auto ref = readAssetRef(j);
    return ref ? ref->path : std::string();
}

void setAssetRefPath(nlohmann::json& j, const std::string& path) {
    if (j.is_object()) {
        j["path"] = path;
    } else {
        j = path;
    }
}

// Looks for a moved asset under `root` (depth <= 6): the same file name, preferring the same
// size; with a stored hash the content must match. Returns the first acceptable candidate.
constexpr int kRelinkMaxDepth = 6;

std::optional<std::filesystem::path> findRelinkCandidate(const AssetRef& ref, const std::filesystem::path& missing,
                                                         const std::filesystem::path& root) {
    std::error_code ec;
    const auto wanted = missing.filename();
    if (wanted.empty() || !std::filesystem::is_directory(root, ec)) {
        return std::nullopt;
    }
    std::vector<std::pair<bool, std::filesystem::path>> candidates; // (size matches, path)
    auto it = std::filesystem::recursive_directory_iterator(
        root, std::filesystem::directory_options::skip_permission_denied, ec);
    for (; !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (it.depth() >= kRelinkMaxDepth) {
            it.disable_recursion_pending();
        }
        if (!it->is_regular_file(ec) || it->path().filename() != wanted) {
            continue;
        }
        const auto candidate = it->path().lexically_normal();
        if (candidate == missing) {
            continue;
        }
        const auto size = it->file_size(ec);
        const bool sizeMatches = !ec && (!ref.hasSize || size == ref.size);
        candidates.emplace_back(sizeMatches, candidate);
    }
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const auto& a, const auto& b) { return a.first && !b.first; });
    for (const auto& [sizeMatches, candidate] : candidates) {
        if (!ref.sha256.empty()) {
            auto hash = sha256File(candidate);
            if (!hash || *hash != ref.sha256) {
                continue; // same name, different content
            }
            return candidate;
        }
        if (ref.hasSize && !sizeMatches) {
            continue;
        }
        return candidate;
    }
    return std::nullopt;
}

// Files a glTF may reference next to itself (external buffers and images); copied with bundles.
bool isGltfSidecar(const std::filesystem::path& p) {
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".bin" || ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".ktx2" || ext == ".webp";
}

// Walks a scene file's node assets (recursively through nested scene files) and its environment
// map, calling `visit(absolutePath, isSceneFile)` for each.
void visitSceneFileAssets(const std::filesystem::path& sceneFile,
                          const std::function<void(const std::filesystem::path&, bool)>& visit, int depth = 0) {
    if (depth > scene::Composition::kMaxNestingDepth) {
        return;
    }
    std::ifstream in(sceneFile);
    nlohmann::json doc = nlohmann::json::parse(in, nullptr, false);
    if (!doc.is_object()) {
        return;
    }
    const auto dir = sceneFile.parent_path();
    if (doc.contains("environment") && doc["environment"].is_object() && doc["environment"].contains("map") &&
        doc["environment"]["map"].is_string()) {
        visit(resolveFrom(doc["environment"]["map"].get<std::string>(), dir), false);
    }
    if (doc.contains("materialPrograms") && doc["materialPrograms"].is_array()) {
        for (const auto& entry : doc["materialPrograms"]) {
            if (entry.is_string()) { // a library material referenced by path rather than inlined
                visit(resolveFrom(entry.get<std::string>(), dir), false);
            }
        }
    }
    if (doc.contains("nodes") && doc["nodes"].is_array()) {
        for (const auto& node : doc["nodes"]) {
            if (!node.is_object() || !node.contains("asset") || !node["asset"].is_string()) {
                continue;
            }
            const auto asset = resolveFrom(node["asset"].get<std::string>(), dir);
            const bool nested = node.value("kind", std::string()) == "scene";
            visit(asset, nested);
            if (nested && asset != sceneFile) {
                visitSceneFileAssets(asset, visit, depth + 1);
            }
        }
    }
}

} // namespace

Result<void> Engine::saveProject(const std::filesystem::path& path) {
    nlohmann::json doc = params::saveProject(params_, modulator_, &sources_, &presets_);
    const auto dir = std::filesystem::absolute(path).parent_path();
    doc["app"] = {{"name", "avgen"}, {"version", kAppVersion}};
    // Shader layer paths relative to the project, with size and content hash for relinking.
    nlohmann::json shaders = shaderLayers_.toJson();
    for (auto& entry : shaders) {
        if (entry.is_object() && entry.contains("path") && entry["path"].is_string()) {
            const nlohmann::json ref = assetRefJson(entry["path"].get<std::string>(), dir);
            for (const auto& [key, value] : ref.items()) {
                entry[key] = value;
            }
        }
    }
    doc["shaders"] = std::move(shaders);
    if (!timeline_.empty()) {
        nlohmann::json timeline = timeline_.toJson();
        // Tracks the sequence baked are derived from it, exactly as its layers are, and the
        // sequence is saved below. Saving both means the next load reads them *and* re-bakes them,
        // and two tracks writing camera/position is not a blend -- it is whichever one the timeline
        // happens to apply second. So the file holds what the author wrote by hand; the bake is
        // recreated from the shots it came from.
        if (!sequenceTargets_.empty() && timeline.contains("tracks") && timeline["tracks"].is_array()) {
            nlohmann::json authored = nlohmann::json::array();
            for (const auto& track : timeline["tracks"]) {
                const std::string target = track.value("target", std::string{});
                if (std::find(sequenceTargets_.begin(), sequenceTargets_.end(), target) ==
                    sequenceTargets_.end()) {
                    authored.push_back(track);
                }
            }
            timeline["tracks"] = std::move(authored);
        }
        if (!timeline.value("tracks", nlohmann::json::array()).empty() ||
            !timeline.value("cues", nlohmann::json::array()).empty()) {
            doc["timeline"] = std::move(timeline);
        }
    }
    // ADR-225: the Auto-director's controls, beside the cut they produced.
    //
    // Written only when the author has moved something, so a project nobody directed keeps the file
    // it had -- the same rule `cameraAimFollow` and `cameraShotSpans` below follow, and the reason a
    // round trip on an untouched project is byte-stable.
    if (!(autoDirector_ == AutoDirectorSettings{})) {
        // `autoDirector`, not `director`: `director` is already the World Director's knob mappings
        // (ADR-088), written a few lines below. Two blocks under one key is one block, and the
        // second one written wins -- which is how this was caught: the load refused a document
        // whose `director` block was the other subsystem's.
        doc["autoDirector"] = autoDirector_.toJson();
    }
    // ADR-249: the song plan Song Mode directs to, beside the settings that say how freely.
    //
    // Its own key rather than a field of `autoDirector`, because the two are different kinds of
    // thing: the settings are a person's preferences and the plan is the piece's content. Written
    // only when there is one, so nothing changes for a project that has never used Song Mode.
    if (!songPlan_.empty()) {
        doc["songPlan"] = songPlan_.toJson();
    }
    // ADR-158: which hero each directed shot was cut for, beside the tracks it accompanies.
    //
    // A sibling of `timeline` rather than part of the scene, because that is what it belongs to: the
    // shots are camera automation, and a scene shared between two projects must not carry one
    // project's cut. Written only when there is a cut, so a project that was never directed keeps
    // the file it had.
    if (const auto* comp = composition(); comp != nullptr && !comp->aimFollow().empty()) {
        nlohmann::json shots = nlohmann::json::array();
        for (const scene::AimFollow& shot : comp->aimFollow()) {
            shots.push_back(nlohmann::json{{"start", shot.startSeconds},
                                           {"end", shot.endSeconds},
                                           {"hero", shot.hero},
                                           {"heroAtCut", {shot.heroAtCut.x, shot.heroAtCut.y, shot.heroAtCut.z}}});
        }
        doc["cameraAimFollow"] = std::move(shots);
    }
    // ADR-207: the other half of the same bake -- when the camera travels and when it holds, which
    // is what a world effect time-gates on. A sibling of `cameraAimFollow` for the identical reason,
    // and saved for a reason the aim-follow table taught the hard way: an offline render loads a
    // *project document*, so anything the director left only in memory is a cut the render does not
    // have. Without this the beam and the pulse were correct in the window and absent from every
    // frame anybody exported.
    if (!shotSpans_.empty()) {
        nlohmann::json spans = nlohmann::json::array();
        for (const world::ShotSpan& span : shotSpans_) {
            nlohmann::json entry{{"start", span.start},
                                 {"end", span.end},
                                 {"travel", span.travel},
                                 {"spotlight", span.spotlight},
                                 {"emphasis", span.emphasis},
                                 {"subject", span.subject},
                                 {"subjectPosition",
                                  {span.subjectPosition.x, span.subjectPosition.y, span.subjectPosition.z}},
                                 {"subjectRadius", span.subjectRadius}};
            if (!span.handoff.empty()) {
                entry["handoff"] = span.handoff;
                entry["handoffPosition"] = {span.handoffPosition.x, span.handoffPosition.y,
                                            span.handoffPosition.z};
            }
            spans.push_back(std::move(entry));
        }
        doc["cameraShotSpans"] = std::move(spans);
    }
    // ADR-207 and ADR-230, on the argument `cameraShotSpans` above makes, for the two families it
    // did not cover -- and this time it is not a derived cut but the effects themselves.
    //
    // The world effects and the atmospheric effects belong to the `Composition`, and a project whose
    // scene came from a file saves that scene **by reference**: `assets.scene.path` plus a hash of
    // the bytes already on disk. Nothing writes the scene file. So an aurora added through the World
    // Effects panel lived in the composition the window was drawing and in no document any render
    // reads -- and a render builds its own `Engine` and loads the project (`startRenderFromUi` saves
    // the project for you and then renders *that file*). That is the whole of "the sky effects are in
    // the app and not in the video".
    //
    // Measured, on the owner's own project: `glowmere-valley-2-multicam.json` carries **94**
    // `atmos/Aurora/...` and `atmos/Bioluminescent Comet/...` parameter values, 47 each, and its
    // scene file authors **no** `atmosphericEffects` at all. Loading it warns 94 times about a
    // parameter nobody owns. The save kept every number of the aurora and lost the aurora.
    //
    // Written only when the session's list is not the one the scene file holds, so a project that
    // opened a scene and rendered it keeps the file it had -- the rule `autoDirector` and
    // `cameraShotSpans` are written under. An *emptied* list is a difference like any other and is
    // written as an empty array: a deletion that only survives while the process does is the same
    // defect pointing the other way. Both sides are compared after a round trip through the same
    // `fromJson`/`toJson`, because a hand-typed `0.0055` and the float the engine ran it as are one
    // authored value and must not read as an edit.
    //
    // Nothing is written for an inlined composition: `assets.scene.inline` is `Composition::toJson`,
    // which already carries both arrays. A second copy beside it would be a second answer.
    if (const scene::Composition* comp = composition(); comp != nullptr && !compositionPath_.empty()) {
        nlohmann::json sceneDoc;
        if (std::ifstream sceneIn(compositionPath_); sceneIn) {
            sceneDoc = nlohmann::json::parse(sceneIn, nullptr, false);
        }
        // The file's own list, put through the parse the engine puts it through. An entry the parser
        // refuses yields a null, which compares unequal to any array and so records the session's
        // list -- the safe direction: a scene file this build cannot read is not evidence that the
        // render already has what the window has.
        const auto onDisk = [&](const char* key, auto parse) {
            nlohmann::json out = nlohmann::json::array();
            if (!sceneDoc.is_object() || !sceneDoc.contains(key) || !sceneDoc[key].is_array()) {
                return out;
            }
            for (const auto& entry : sceneDoc[key]) {
                auto one = parse(entry);
                if (!one) {
                    return nlohmann::json{};
                }
                out.push_back(one->toJson());
            }
            return out;
        };
        nlohmann::json liveWorld = nlohmann::json::array();
        for (const world::WorldEffect& effect : comp->worldEffects()) {
            liveWorld.push_back(effect.toJson());
        }
        if (liveWorld != onDisk("worldEffects", [](const nlohmann::json& j) {
                return world::WorldEffect::fromJson(j);
            })) {
            doc["worldEffects"] = std::move(liveWorld);
        }
        nlohmann::json liveAtmos = nlohmann::json::array();
        for (const world::AtmosphericEffect& effect : comp->atmosphericEffects()) {
            liveAtmos.push_back(effect.toJson());
        }
        if (liveAtmos != onDisk("atmosphericEffects", [](const nlohmann::json& j) {
                return world::AtmosphericEffect::fromJson(j);
            })) {
            doc["atmosphericEffects"] = std::move(liveAtmos);
        }
        // ADR-276, and it is the same defect a third time. Starring an object in the world editor
        // calls `Composition::setHeroes`; the composition is saved **by reference**; so the star
        // lived in the window and in no document any render reads. Measured on the owner's own
        // session: one hero starred, one hero in the composition, **zero** after a save and a
        // reload -- and an offline render builds its own `Engine` and loads the project, so a
        // starred hero never reached a deliverable at all.
        //
        // It is written here rather than into the scene file for the reason ADR-271 gives and
        // ADR-207/230 gave before it: nothing but a "Save Scene As..." dialog writes a scene file,
        // so a re-authored hero would be discarded by the next Cmd-S; and a scene is shared between
        // projects -- `glowmere-stylized.scene.json` backs two -- so an editor that wrote scenes
        // would change a project the user did not open and re-fingerprint it on every star.
        //
        // The same rule as its two siblings above: written only when the session's list is not the
        // one the scene file holds, compared after a round trip through the same `fromJson`/
        // `toJson` so a hand-typed number and the float the engine ran it as are one authored value
        // rather than an edit; and an *emptied* list is a difference like any other, written as an
        // empty array, because an unstar that only survives while the process does is the identical
        // defect pointing the other way.
        //
        // `cameraAimFollow` above names heroes by name, and this is what makes that table mean
        // something after a reload: a cut whose heroes did not survive the save is a cut aimed at
        // nothing (`Composition::applyDirectedAim` skips an entry whose hero is not in `heroes()`).
        nlohmann::json liveHeroes = nlohmann::json::array();
        for (const world::HeroPoint& hero : comp->heroes()) {
            liveHeroes.push_back(hero.toJson());
        }
        if (liveHeroes != onDisk("heroes", [](const nlohmann::json& j) {
                return world::HeroPoint::fromJson(j);
            })) {
            doc["heroes"] = std::move(liveHeroes);
        }
        // ADR-330, and it is the same defect a fourth time -- the one the owner reported first and
        // the one that survived ADR-276. Deleting an object in the world editor calls
        // `Composition::detachNode`; the composition is saved by reference; so the deletion lived in
        // the window and in no document any render reads. Measured on the owner's own project
        // (`glowmere-valley-2-multicam.json`): **80 nodes, 79 after the delete, 80 again after a
        // save and a reload**. The object came back, so it was in every frame of every export.
        //
        // Not the three keys above's shape, and the difference is the whole of why this one is
        // harder. `worldEffects`, `atmosphericEffects` and `heroes` are small lists the project can
        // simply hold a copy of. The nodes are the scene: a copy would be 80 objects, would make the
        // shared scene file dead for this project the moment anybody corrected it, and would be a
        // second answer to every question the `parameters` block already answers about where a node
        // is. So what is written is the **difference, by name** -- and `removed` is a negative fact
        // with nothing left to carry it, which is why it needed a record at all.
        //
        // `scene::nodeEditsAgainst` is the whole of the decision and is pure, so a test can assert
        // what a given pair of documents produces without an engine (ADR-278's argument for
        // `unknownKeys`, applied again). It returns null when the session's node set is the file's,
        // which is what keeps a save of an untouched project byte-stable -- the control that makes
        // the positive arms mean anything.
        //
        // `comp->toJson()` rather than a bespoke walk of `nodes()`: an added node then reaches the
        // project in exactly the serialisation a scene file would have given it, and graph-installed
        // nodes are left out by the one piece of code that already knows which those are.
        if (nlohmann::json edits = scene::nodeEditsAgainst(comp->toJson().value("nodes", nlohmann::json()), sceneDoc);
            !edits.is_null()) {
            doc["sceneNodes"] = std::move(edits);
        }
        // The authored lights, and this is the same defect a **fifth** time -- ADR-207's world
        // effects, ADR-230's atmospherics, ADR-276's heroes and ADR-330's nodes were the first
        // four. Adding a light in the Lights panel calls `Composition::setAuthoredLights`; the
        // composition is saved **by reference** (`assets.scene.path` plus a hash of the bytes
        // already on disk); so without this the light lived in the window and in no document any
        // render reads -- and an offline render builds its own `Engine` and loads the project, so
        // it would never have reached a deliverable at all. `check_project_integrity.py` passes on
        // that result, because the file is still perfectly valid; it is just missing a light.
        //
        // The whole list rather than ADR-330's difference-by-name, and the reason is the shape of
        // the data rather than a preference: a node's numbers are already in `parameters`, so the
        // project only owes the *set*. Most of a light's twenty-five fields -- `type`, `role`,
        // `node`, `up`, the area extents -- are not parameters and never will be, so a by-name
        // difference would record which lights exist and lose what they are. That puts lights in
        // `worldEffects`/`heroes`' family, which is small lists the project can simply hold.
        //
        // `authoredLightsAgainst` canonicalises the file's own list out and back through the same
        // parser before comparing, so a scene that spells out `"intensity": 1.0` does not read as
        // an edit and an untouched project stays byte-stable.
        // `authoredLightsRestJson()` rather than `toJson()["lights"]`: the latter carries the
        // per-frame parameter writeback, so a project that merely dims a light would record its
        // whole lighting rig -- a second answer to a question `parameters` already answers.
        if (nlohmann::json lights = scene::authoredLightsAgainst(comp->authoredLightsRestJson(), sceneDoc);
            !lights.is_null()) {
            doc["lights"] = std::move(lights);
        }
    }
    if (!states_.empty()) {
        doc["states"] = states_.toJson();
    }
    if (!director_.mappings.empty()) {
        doc["director"] = director_.toJson();
    }
    if (!worldMacros_.empty()) {
        nlohmann::json macros = nlohmann::json::array();
        for (const WorldMacro& m : worldMacros_) {
            macros.push_back(m.toJson());
        }
        doc["worldMacros"] = std::move(macros);
    }
    // The 2D composition (ADR-083). Pulled back from the parameters first: the inspector and the
    // timeline write through the parameter set, and a project saved from the authored fields alone
    // would lose every edit made with a slider.
    layers_.pullAuthored();
    if (!layers_.empty()) {
        nlohmann::json composition = layers_.toJson();
        // Layers the sequence made are derived from its overlay cues, and the cues are saved just
        // below. Writing both would save the same lyric twice and, worse, restore it with a layer
        // id the next install will not reuse -- so the saved parameter values would attach to a
        // layer that no longer exists. The sequence owns them; "composition" holds what the author
        // made by hand.
        if (composition.contains("layers") && composition["layers"].is_array()) {
            nlohmann::json authored = nlohmann::json::array();
            for (const auto& layer : composition["layers"]) {
                const std::string name = layer.value("name", std::string{});
                if (!seq::CompositionLayerSink::ownedName(name)) {
                    authored.push_back(layer);
                }
            }
            composition["layers"] = std::move(authored);
        }
        if (!composition.value("layers", nlohmann::json::array()).empty()) {
            doc["composition"] = std::move(composition);
        }
        // Their parameters are derived too. Left in, a reload would try to apply "layers/7/anchor"
        // before the install has made layer 7, and warn about a parameter the author never wrote.
        if (doc.contains("parameters") && doc["parameters"].is_object()) {
            std::vector<std::string> doomed;
            for (const auto& layer : layers_.layers()) {
                if (!seq::CompositionLayerSink::ownedName(layer->name)) {
                    continue;
                }
                for (const std::string& path : layer->parameterPaths()) {
                    doomed.push_back(path);
                }
            }
            for (const std::string& path : doomed) {
                doc["parameters"].erase(path);
            }
        }
    }
    // The same argument, for the bodies a staging scenario moves (ADR-264).
    //
    // A scenario hides an animal once it has abducted it and shows the beam while it fires, so the
    // `visible` and the transform such a node has at the instant somebody presses save are a
    // photograph of a run. A project's `parameters` are applied *over* its scene at load, so saving
    // them means the next load opens with an invisible goat and a beam that never switches off --
    // and the beam's saved scale once ran it at 3.07 m against the 7.8 m the scene authors, which is
    // narrower than the animals it lifts.
    //
    // Four consecutive saves of Glowmere re-introduced exactly this while the fix for the last one
    // was being written, which is what moved it from a cleanup script to the save itself. A script
    // that has to be run after every save is a script somebody will forget to run.
    //
    // The scene keeps saying where these bodies start and whether they are visible; that is
    // authorship and it is not touched. What goes is only the project's copy of where the run left
    // them.
    if (doc.contains("parameters") && doc["parameters"].is_object()) {
        if (const scene::Composition* comp = composition(); comp != nullptr && !comp->staging().empty()) {
            const std::vector<entity::EntityDesc>& descs = comp->entities();
            std::vector<std::string> names;
            names.reserve(descs.size());
            for (const entity::EntityDesc& d : descs) {
                names.push_back(d.name);
            }
            const auto nodeOf = [&](const std::string& name) {
                for (const entity::EntityDesc& d : descs) {
                    if (d.name == name) {
                        return d.node.empty() ? d.name : d.node;
                    }
                }
                return name;
            };
            const auto tagsOf = [&](const std::string& name) {
                for (const entity::EntityDesc& d : descs) {
                    if (d.name == name) {
                        return d.tags;
                    }
                }
                return std::vector<std::string>{};
            };
            const std::set<std::string> owned =
                stage::scenarioOwnedNodes(comp->staging(), nodeOf, tagsOf, names);
            std::vector<std::string> doomed;
            for (const auto& [path, value] : doc["parameters"].items()) {
                const std::size_t first = path.find('/');
                const std::size_t second = path.find('/', first + 1);
                if (first == std::string::npos || second == std::string::npos) {
                    continue;
                }
                if (path.compare(0, first, "nodes") != 0) {
                    continue;
                }
                const std::string body = path.substr(first + 1, second - first - 1);
                const std::string field = path.substr(second + 1);
                if (owned.count(body) == 0) {
                    continue;
                }
                if (field == "visible" || field == "position" || field == "rotation" || field == "scale") {
                    doomed.push_back(path);
                }
            }
            for (const std::string& path : doomed) {
                doc["parameters"].erase(path);
            }
        }
    }
    if (hasSequence()) {
        doc["sequence"] = sequence_.toJson();
    }
    doc["render"] = render_.toJson();
    // ADR-366. Written unconditionally, beside "render" and for the same reason: a block
    // that is only emitted when it differs from the default is a writer gated on its own
    // subject, which is the half of ADR-350's defect that survived the first fix of it.
    doc["pathtrace"] = pathTrace_.toJson();
    // The transport's persistent half (ADR-102). Additively, and only when there is something to
    // say: a project that never set a loop gains no key, so files written before this round-trip
    // unchanged. The playing state, the position and the playback rate are deliberately *not* here
    // -- they are how you are working, not what the piece is.
    if (const TransportLoop& loop = transport_.loop();
        loop.enabled || loop.endSeconds > loop.startSeconds) {
        doc["transport"] = {{"loop",
                             {{"enabled", loop.enabled},
                              {"start", loop.startSeconds},
                              {"end", loop.endSeconds}}}};
    }
    doc["control"] = controlHub_.map().toJson();
    if (outputs_.is_array() && !outputs_.empty()) {
        doc["outputs"] = outputs_;
    }
    doc["control"]["tempoSource"] = tempoSourceName(tempoSource_);
    doc["control"]["phraseBars"] = phraseBars_;
    doc["control"]["sectionPhrases"] = sectionPhrases_;
    nlohmann::json assets = nlohmann::json::object();
    // One plain clip is written as it always was -- `assets.audio`, a single reference -- so every
    // project made before arrangements existed round-trips byte for byte. Anything richer is a clip
    // list instead, and the two are never both present: two places naming the audio is two places to
    // disagree about it.
    const bool plainSingle =
        audioClips_.size() == 1 && audioClips_.front() == audio::AudioClip{audioClips_.front().file};
    if (plainSingle && !audioPath_.empty()) {
        assets["audio"] = assetRefJson(audioPath_, dir);
    } else if (!audioClips_.empty()) {
        nlohmann::json list = nlohmann::json::array();
        for (const audio::AudioClip& clip : audioClips_) {
            nlohmann::json item = nlohmann::json::object();
            item["file"] = assetRefJson(clip.file, dir);
            // Only what differs from the default, so a clip that was merely dropped on the timeline
            // reads as one line rather than as eight fields of zero.
            if (clip.startSeconds != 0.0) item["start"] = clip.startSeconds;
            if (clip.inSeconds != 0.0) item["in"] = clip.inSeconds;
            if (clip.durationSeconds != 0.0) item["duration"] = clip.durationSeconds;
            if (clip.gain != 1.0f) item["gain"] = clip.gain;
            if (clip.fadeInSeconds != 0.0) item["fadeIn"] = clip.fadeInSeconds;
            if (clip.fadeOutSeconds != 0.0) item["fadeOut"] = clip.fadeOutSeconds;
            if (!clip.enabled) item["enabled"] = false;
            if (!clip.name.empty()) item["name"] = clip.name;
            list.push_back(std::move(item));
        }
        assets["audioClips"] = std::move(list);
    }
    if (!environmentPath_.empty()) {
        assets["environment"] = assetRefJson(environmentPath_, dir);
    }
    nlohmann::json sceneRef = nlohmann::json::object();
    if (auto* comp = composition()) {
        if (compositionPath_.empty()) {
            // An unsaved composition: keep it inline so the project stays self-contained.
            sceneRef["kind"] = "composition";
            sceneRef["inline"] = comp->toJson();
        } else {
            sceneRef["kind"] = "composition";
            sceneRef["path"] = assetRefJson(compositionPath_, dir);
        }
    } else if (auto* gltf = gltfScene()) {
        sceneRef["kind"] = "gltf";
        sceneRef["path"] = assetRefJson(gltf->path(), dir);
    } else {
        sceneRef["kind"] = "orb";
    }
    assets["scene"] = std::move(sceneRef);
    doc["assets"] = std::move(assets);

    std::ofstream out(path);
    if (!out) {
        return fail("cannot write '{}'", path.string());
    }
    out << doc.dump(2) << '\n';
    projectPath_ = path;
    return {};
}

Result<void> Engine::loadProject(const std::filesystem::path& path) {
    // ---- the stages, and what they cost -----------------------------------------------------
    //
    // The list is fixed and known before the first byte is read, which is what makes "stage 3 of 9"
    // a countable fact rather than a guess (ADR-064: a fabricated bar is indistinguishable from a
    // real one). Each stage's wall-clock is kept for `lastLoadTimings()`, because "opening a
    // project freezes the application" is not a number and the first job was to make it one.
    static constexpr std::array<std::string_view, 9> kStages{
        "Reading the project",  "Clearing the last project", "Loading audio",
        "Building the scene",   "Loading the environment",   "Layers and parameters",
        "Shaders and control",  "Timeline and automation",   "Finishing"};
    loadTimings_.clear();
    loadTimings_.reserve(kStages.size());
    auto stageStarted = std::chrono::steady_clock::now();
    int stageIndex = -1;
    const auto stage = [&](int index) {
        if (stageIndex >= 0 && stageIndex < static_cast<int>(kStages.size())) {
            const auto now = std::chrono::steady_clock::now();
            loadTimings_.emplace_back(
                std::string(kStages[static_cast<std::size_t>(stageIndex)]),
                std::chrono::duration<double, std::milli>(now - stageStarted).count());
            stageStarted = now;
        }
        stageIndex = index;
        if (loadReporter_) {
            loadReporter_(LoadStage{.name = kStages[static_cast<std::size_t>(index)],
                                    .index = index,
                                    .count = static_cast<int>(kStages.size())});
        }
    };
    // Closes the final stage's timing and prints the breakdown. Called on every exit that reached
    // a stage, including the failures -- a load that failed slowly is the one you most want the
    // numbers for.
    const auto finishTimings = [&] {
        stage(static_cast<int>(kStages.size()) - 1);
        stageIndex = -1;
        double total = 0.0;
        std::string line;
        for (const auto& [name, ms] : loadTimings_) {
            total += ms;
            if (ms >= 1.0) {
                line += fmt::format("{} {:.0f} ms; ", name, ms);
            }
        }
        log::info("project load: {:.0f} ms total -- {}", total, line.empty() ? "all stages under 1 ms" : line);
    };

    stage(0);
    std::ifstream in(path);
    if (!in) {
        return fail("cannot open '{}'", path.string());
    }
    nlohmann::json doc = nlohmann::json::parse(in, nullptr, false);
    if (doc.is_discarded()) {
        return fail("'{}' is not valid JSON", path.string());
    }
    if (!doc.is_object() || doc.value("format", std::string()) != params::kProjectFormatName) {
        return fail("'{}' is not an avgen project", path.string());
    }
    // Migrate once here so the two parameter passes below see a current document.
    auto migrated = params::migrateProject(doc);
    if (!migrated) {
        return std::unexpected(migrated.error());
    }
    for (const auto& step : migrated->steps) {
        log::info("project '{}' migrated: {}", path.filename().string(), step);
    }
    stage(1);
    const auto dir = std::filesystem::absolute(path).parent_path();
    projectWarnings_.clear();
    // Back to factory before anything of this project's is applied.
    //
    // Opening a project is a *replacement*, not a merge: a parameter the document does not mention
    // is the default, not whatever the last project left behind. Sources, routes and presets were
    // always replaced; parameters were not, and the ones that leaked are the ones the engine owns
    // rather than the scene -- all of `post/*`, the camera's lens, exposure and focus, the input
    // gain -- because a scene swap is what clears the parameter set and those are re-registered
    // immediately afterwards.
    //
    // It has to happen *here*, before the scene loads, and not in the parameter pass further down:
    // a composition's own `post` block is authored state that lands between the two, and a reset
    // after it would erase it.
    //
    // Resetting the structs as well as the parameters matters for the same ordering reason:
    // `installController` re-registers post and camera parameters from them (`keep = post_`),
    // using the current values as the new *defaults*, so a stale struct here would come back as a
    // stale default that no later reset could tell from an authored one.
    for (params::IParameter* param : params_.ordered()) {
        if (param != nullptr && param->flags().serialized) {
            param->resetToDefault();
        }
    }
    post_ = scene::PostSettings{};
    lens_ = scene::LensSettings{};
    exposure_ = scene::ExposureSettings{};
    focus_ = scene::FocusSettings{};
    auto warn = [&](std::string message) {
        log::warn("project: {}", message);
        projectWarnings_.push_back(std::move(message));
    };
    // Resolves an asset reference (string or object form); a missing file is searched for under
    // the project folder by name, size and content hash and relinked with a warning.
    auto resolveAsset = [&](const nlohmann::json& j, const char* label) -> std::optional<std::filesystem::path> {
        const auto ref = readAssetRef(j);
        if (!ref) {
            return std::nullopt;
        }
        auto path = resolveFrom(ref->path, dir);
        std::error_code ec;
        if (std::filesystem::exists(path, ec)) {
            return path;
        }
        if (auto found = findRelinkCandidate(*ref, path, dir)) {
            warn(fmt::format("relinked {}: {} -> {}", label, ref->path, relativeTo(*found, dir)));
            return *found;
        }
        return path; // still missing: the loader reports it
    };

    stage(2);
    // The audio is the project's too. A document that names none means silence, not whatever was
    // playing before: opening a project with no `assets.audio` used to leave the previous piece
    // loaded, under a scene it was never written for, with a transport whose duration came from it.
    //
    // A live *input* is a device choice rather than project state and is left alone -- it already
    // holds no file, so there is nothing here for this to clear.
    {
        const auto assets = doc.find("assets");
        const bool statesAudio = assets != doc.end() && assets->is_object() &&
                                 (assets->contains("audioClips") || assets->contains("audio"));
        if (!statesAudio && input_ == nullptr && (hasAudio() || !audioClips_.empty())) {
            if (auto r = setAudioClips({}); !r) {
                warn("audio: " + r.error().message);
            }
        }
    }

    // ---- assets first: they define the parameter surface the rest of the document targets ----
    if (const auto assets = doc.find("assets"); assets != doc.end() && assets->is_object()) {
        std::filesystem::path sceneEnvironment;
        if (assets->contains("audioClips") && (*assets)["audioClips"].is_array()) {
            // An arrangement wins over a single reference; they are never both written.
            std::vector<audio::AudioClip> clips;
            for (const auto& item : (*assets)["audioClips"]) {
                if (!item.is_object() || !item.contains("file")) {
                    continue;
                }
                audio::AudioClip clip;
                clip.file = resolveAsset(item["file"], "audio clip").value_or(std::filesystem::path());
                clip.startSeconds = item.value("start", 0.0);
                clip.inSeconds = item.value("in", 0.0);
                clip.durationSeconds = item.value("duration", 0.0);
                clip.gain = item.value("gain", 1.0f);
                clip.fadeInSeconds = item.value("fadeIn", 0.0);
                clip.fadeOutSeconds = item.value("fadeOut", 0.0);
                clip.enabled = item.value("enabled", true);
                clip.name = item.value("name", std::string{});
                clips.push_back(std::move(clip));
            }
            if (auto r = setAudioClips(std::move(clips)); !r) {
                warn("audio: " + r.error().message);
                // The arrangement this project names could not be built. Silence, not the last
                // project's piece standing in for it.
                static_cast<void>(setAudioClips({}));
            }
        } else if (assets->contains("audio")) {
            if (const auto audio = resolveAsset((*assets)["audio"], "audio"); audio && *audio != audioPath_) {
                if (auto r = loadAudio(*audio); !r) {
                    warn("audio: " + r.error().message);
                    static_cast<void>(setAudioClips({})); // as above: silence, not the last piece
                }
            }
        }
        stage(3);
        if (assets->contains("scene") && (*assets)["scene"].is_object()) {
            const auto& sceneRef = (*assets)["scene"];
            const std::string kind = sceneRef.value("kind", std::string("orb"));
            if (kind == "orb") {
                if (orbScene() == nullptr) {
                    loadOrbScene();
                }
            } else if (kind == "gltf" && sceneRef.contains("path")) {
                const auto scenePath = resolveAsset(sceneRef["path"], "scene").value_or(std::filesystem::path());
                if (gltfScene() == nullptr || gltfScene()->path() != scenePath) {
                    if (auto r = loadScene(scenePath); !r) {
                        warn("scene: " + r.error().message);
                    }
                }
            } else if (kind == "composition" && sceneRef.contains("path")) {
                const auto scenePath = resolveAsset(sceneRef["path"], "scene").value_or(std::filesystem::path());
                const auto previousEnvironment = environmentPath_;
                environmentPath_.clear();
                // ADR-330: the objects this session added to, and removed from, the scene it saves
                // by reference. Passed *into* the load rather than applied after it, so the
                // composition the engine ends up with is the one the parser would have built from a
                // scene file with those edits made -- and so it is in place before anything reads
                // the node list. That ordering is not taste: the `parameters` block further down
                // carries `nodes/<name>/...` values, a hero names a node, and the world effects name
                // a hero. Nodes are the most structural thing a project can say, so they go first.
                if (auto r = loadComposition(scenePath, doc.value("sceneNodes", nlohmann::json())); !r) {
                    environmentPath_ = previousEnvironment;
                    warn("scene: " + r.error().message);
                } else {
                    sceneEnvironment = environmentPath_;
                }
            } else if (kind == "composition" && sceneRef.contains("inline")) {
                // No node edits here, deliberately: `assets.scene.inline` *is* `Composition::toJson`
                // and already carries the live node list. A difference beside it would be a second
                // answer -- the same reason `heroes` and the two effect lists are not written for an
                // inlined composition either.
                registry_.setBaseDirectory(dir);
                auto comp = scene::Composition::fromJson(sceneRef["inline"], registry_);
                if (!comp) {
                    warn("scene: " + comp.error().message);
                } else {
                    const float masterGain = modulator_.masterGain;
                    detachSceneParameters();
                    params_.clear();
                    modulator_.clearRoutes();
                    modulator_.masterGain = masterGain;
                    (*comp)->attach(params_, modulator_);
                    if (!(*comp)->environmentMap().empty()) {
                        sceneEnvironment = registry_.resolve((*comp)->environmentMap());
                    }
                    environmentPath_ = sceneEnvironment;
                    compositionPath_.clear();
                    const nlohmann::json inlinePost = (*comp)->postJson();
                    installController(std::move(*comp));
                    if (auto r = scene::applyPostJson(inlinePost, postParams_); !r) {
                        warn("scene: " + r.error().message);
                    }
                }
            } else {
                warn("scene: unknown kind '" + kind + "'");
            }
        }
        stage(4);
        std::optional<std::filesystem::path> env;
        if (assets->contains("environment")) {
            env = resolveAsset((*assets)["environment"], "environment");
        }
        if (env) {
            if (*env != environmentPath_) {
                if (auto r = loadEnvironment(*env); !r) {
                    warn("environment: " + r.error().message);
                }
            }
        } else if (sceneEnvironment.empty() && !environmentPath_.empty()) {
            environmentPath_.clear();
            controller_->scene().environment.environmentMap = scene::kInvalidTexture;
            if (auto* comp = composition()) {
                comp->setEnvironmentMap({});
            }
        }
    }
    // Shader layer paths are project-relative on disk (and relinked like other assets).
    if (doc.contains("shaders") && doc["shaders"].is_array()) {
        for (auto& entry : doc["shaders"]) {
            if (entry.is_object() && entry.contains("path") && entry["path"].is_string()) {
                if (const auto shaderPath = resolveAsset(entry, "shader")) {
                    entry["path"] = shaderPath->string();
                }
            }
        }
    }

    stage(5);
    // ---- the session's world and atmospheric effects, over the ones its scene authors ----
    //
    // Here, and for the reason the 2D composition below is here: the parameter block a few lines
    // down carries `worldfx/<name>/...` and `atmos/<name>/...` values, and `setWorldEffects` /
    // `setAtmosphericEffects` are what *register* those paths. Applied after the parameters, an
    // effect would arrive with every number back at its default and the project's own values would
    // already have been refused as unknown -- which is exactly what the 94 warnings this fix was
    // found by say.
    //
    // Over rather than instead of: the scene file is still the state that runs first (ADR-264), and
    // an absent key changes nothing. A present one is the session's answer, including an empty array,
    // which is how a deleted effect stays deleted.
    // ADR-276: the session's heroes, over the ones its scene authors. **Before** the world effects,
    // and not by taste: a `WorldEffect` may name a hero as its source, and `Composition::setHeroes`
    // is what decides whether that name is real. The scene file's own reader orders them the same
    // way and says so (`composition.cpp`, "read after the heroes").
    //
    // Over rather than instead of (ADR-264): the scene file is still the state that runs first, an
    // absent key changes nothing, and a present one is the session's answer -- including an empty
    // array, which is how an unstarred object stays unstarred.
    if (const auto entry = doc.find("heroes"); entry != doc.end() && entry->is_array()) {
        std::vector<world::HeroPoint> heroes;
        heroes.reserve(entry->size());
        bool readable = true;
        for (std::size_t i = 0; i < entry->size(); ++i) {
            auto one = world::HeroPoint::fromJson((*entry)[i]);
            if (!one) {
                // Named, and the whole block refused rather than the member skipped: a hero that
                // quietly failed to load looks exactly like a hero nobody declared, which is
                // ADR-067 and ADR-070, twice bitten.
                warn(fmt::format("heroes[{}]: {}", i, one.error().message));
                readable = false;
                break;
            }
            heroes.push_back(std::move(*one));
        }
        if (readable) {
            if (auto* comp = composition(); comp != nullptr) {
                if (auto ok = comp->setHeroes(std::move(heroes)); !ok) {
                    warn("heroes: " + ok.error().message);
                }
            }
        }
    }
    // The authored lights the session ended with (the fifth of ADR-207's family; see the save).
    //
    // **Before `params::loadProject` below, and that ordering is the whole of whether this works.**
    // `setAuthoredLights` is what registers `lights/<id>/...`, and `params::loadProject` drops any
    // path it cannot find with a warning rather than keeping it -- so a lights block applied after
    // the parameters would leave every light's intensity, colour and position in the document and
    // in no parameter. That is ADR-358's shader-layer defect exactly, where `shader/<layer>/<input>`
    // was written by every save and met with "references unknown parameter; ignored" by every load.
    //
    // Over rather than instead of (ADR-264): an absent key changes nothing and the scene still runs
    // first; a present one is the session's answer, including an empty array, which is how a
    // deleted light stays deleted.
    if (const auto entry = doc.find("lights"); entry != doc.end() && entry->is_array()) {
        auto lights = scene::authoredLightsFromJson(*entry, "project");
        if (!lights) {
            warn(lights.error().message);
        } else if (auto* comp = composition(); comp != nullptr) {
            if (auto ok = comp->setAuthoredLights(std::move(*lights)); !ok) {
                warn("lights: " + ok.error().message);
            }
        }
    }
    if (const auto entry = doc.find("worldEffects"); entry != doc.end() && entry->is_array()) {
        std::vector<world::WorldEffect> effects;
        effects.reserve(entry->size());
        bool readable = true;
        for (std::size_t i = 0; i < entry->size(); ++i) {
            auto one = world::WorldEffect::fromJson((*entry)[i]);
            if (!one) {
                warn(fmt::format("worldEffects[{}]: {}", i, one.error().message));
                readable = false;
                break;
            }
            effects.push_back(std::move(*one));
        }
        if (readable) {
            if (auto ok = setWorldEffects(std::move(effects)); !ok) {
                warn("worldEffects: " + ok.error().message);
            }
        }
    }
    if (const auto entry = doc.find("atmosphericEffects"); entry != doc.end() && entry->is_array()) {
        std::vector<world::AtmosphericEffect> effects;
        effects.reserve(entry->size());
        bool readable = true;
        for (std::size_t i = 0; i < entry->size(); ++i) {
            auto one = world::AtmosphericEffect::fromJson((*entry)[i]);
            if (!one) {
                warn(fmt::format("atmosphericEffects[{}]: {}", i, one.error().message));
                readable = false;
                break;
            }
            effects.push_back(std::move(*one));
        }
        if (readable) {
            // ADR-387 §19, the second half of the migration. A project's `atmosphericEffects` block
            // is a COPY of the scene's list and REPLACES it (ADR-264), so a project saved before the
            // vortex was an effect carries a list that cannot contain one -- and the vortex the
            // scene's own migration just produced would be thrown away by a project that is exactly
            // as legacy as the scene it names. Measured: the intermediate state where only the scene
            // had been migrated rendered 97.96% of pixels different, with the funnel gone.
            //
            // Carried over by KIND rather than by name, because the thing being migrated is a format
            // that predates the kind. A project that authors its own vortex is not legacy and is left
            // alone, which is the control: this can only ever add the one the file could not express.
            const bool projectHasVortex =
                std::any_of(effects.begin(), effects.end(), [](const world::AtmosphericEffect& e) {
                    return e.kind == world::AtmosphereKind::Vortex;
                });
            if (!projectHasVortex) {
                for (const world::AtmosphericEffect& e : atmosphericEffects_) {
                    if (e.kind == world::AtmosphereKind::Vortex) {
                        effects.push_back(e);
                        break;
                    }
                }
            }
            if (auto ok = setAtmosphericEffects(std::move(effects)); !ok) {
                warn("atmosphericEffects: " + ok.error().message);
            }
        }
    }
    // ADR-387 §19, the third and last half of the migration: the PATHS. Moving the vortex from
    // `scene/vortex/*` to `atmos/<name>/*` orphans every route, key, preset member and macro that
    // named the old one -- silently, because a route whose target does not resolve is dropped with
    // a warning nobody reads and the picture simply stops answering the music.
    //
    // This was not theory. The shipped Tree of Life project carries five of them (bass -> density
    // and breath, mid -> turbulence, treble -> filaments, progress -> emission), and without this
    // the migrated scene rendered 95.35% of its pixels differently from the scene it replaced --
    // an unmodulated funnel, dimmest exactly where the vortex is brightest. Three renders and a
    // uniform probe said the values reaching the GPU were correct to the last decimal before the
    // routes turned out to be what had moved.
    //
    // Rewritten over the WHOLE document rather than over `routes`, because a path is a string in
    // six places (a route target, a timeline track, a cue, a preset member, a macro target, a
    // control binding) and a migration that knows about one of them is a migration that fails
    // quietly in the other five.
    if (!atmosphericEffects_.empty()) {
        std::string vortexName;
        for (const world::AtmosphericEffect& e : atmosphericEffects_) {
            if (e.kind == world::AtmosphereKind::Vortex) {
                vortexName = e.name;
                break;
            }
        }
        if (!vortexName.empty()) {
            const std::string from = "scene/vortex/";
            const std::string to = world::atmosphericParameterPrefix(vortexName);
            std::size_t moved = 0;
            const auto rewrite = [&](auto&& self, nlohmann::json& node) -> void {
                if (node.is_string()) {
                    const std::string& v = node.get_ref<const std::string&>();
                    if (v.compare(0, from.size(), from) == 0) {
                        node = to + v.substr(from.size());
                        ++moved;
                    }
                    return;
                }
                if (node.is_array()) {
                    for (nlohmann::json& child : node) { self(self, child); }
                    return;
                }
                if (!node.is_object()) { return; }
                nlohmann::json rebuilt = nlohmann::json::object();
                for (auto& [key, value] : node.items()) {
                    self(self, value);
                    if (key.size() > from.size() && key.compare(0, from.size(), from) == 0) {
                        rebuilt[to + key.substr(from.size())] = std::move(value);
                        ++moved;
                    } else {
                        rebuilt[key] = std::move(value);
                    }
                }
                node = std::move(rebuilt);
            };
            rewrite(rewrite, doc);
            if (moved > 0) {
                log::info("project: migrated {} reference(s) from 'scene/vortex/' to '{}' (ADR-387)",
                          moved, to);
            }
        }
    }
    // ---- the 2D composition (ADR-083) ----
    // Here, and not later: the parameter block below carries "layers/<id>/..." values, and the
    // timeline below carries tracks aimed at them. Both need the parameters to exist first, and a
    // project written before this feature existed simply has no "composition" key.
    removeLayerParameters();
    if (const auto composition = doc.find("composition"); composition != doc.end()) {
        if (auto r = layers_.fromJson(*composition); !r) {
            return r;
        }
    } else {
        layers_.clear();
    }
    layers_.attach(params_);

    if (auto r = params::loadProject(doc, params_, modulator_, &sources_, &presets_); !r) {
        return r;
    }
    if (doc.contains("control")) {
        auto map = control::ControlMap::fromJson(doc["control"]);
        if (!map) {
            return std::unexpected(map.error());
        }
        controlHub_.setMap(std::move(*map));
        const std::string tempo = doc["control"].value("tempoSource", std::string("analysis"));
        if (const auto source = tempoSourceFromName(tempo)) {
            setTempoSource(*source);
        } else {
            return fail("control.tempoSource '{}' unknown (analysis|midi)", tempo);
        }
        setPhraseBars(doc["control"].value("phraseBars", 4));
        setSectionPhrases(doc["control"].value("sectionPhrases", 4));
    } else {
        controlHub_.setMap(control::ControlMap{});
        setTempoSource(TempoSource::Analysis);
    }
    stage(6);
    outputs_ = doc.contains("outputs") && doc["outputs"].is_array() ? doc["outputs"] : nlohmann::json::array();
    ensureControlSource();
    sources_.attach(bus_, params_);
    if (doc.contains("shaders")) {
        if (auto r = shaderLayers_.fromJson(doc["shaders"]); !r) {
            return r;
        }
        // ADR-358. A shader layer's ISF inputs ARE parameters -- `ShaderLayerSet::registerInputs`
        // adds one per input under "shader/<layer>/<input>" -- but they do not exist until the
        // line above has run, and `params::loadProject` ran twenty-five lines up. So every
        // `"shader/..."` a project wrote was met with "references unknown parameter; ignored" and
        // the layer drew its file defaults. Not a hypothetical: the Tree of Life's cosmos layer
        // has eleven inputs and no project in the repository has ever been able to set one, which
        // is why the defect went unseen -- nobody could author the thing that would have shown it.
        //
        // A second, narrow pass rather than moving the load: `loadProject` also installs sources,
        // routes and presets, and reordering the whole of it to serve one prefix is a change whose
        // blast radius is every project. This one touches exactly the paths that could not
        // previously be set, so no existing picture moves -- there is no project in the repository
        // with a `shader/` parameter in it, because until now writing one did nothing.
        if (const auto parameters = doc.find("parameters");
            parameters != doc.end() && parameters->is_object()) {
            std::size_t applied = 0;
            for (const auto& [path, value] : parameters->items()) {
                if (!path.starts_with("shader/")) {
                    continue;
                }
                params::IParameter* param = params_.find(path);
                if (param == nullptr) {
                    log::warn("project references unknown shader parameter '{}'; ignored", path);
                    continue;
                }
                if (auto r = params::parameterFromJson(*param, value); !r) {
                    log::warn("project parameter '{}': {}", path, r.error().message);
                    continue;
                }
                ++applied;
            }
            // Said out loud, because the pass twenty lines up has already warned that these paths
            // are unknown -- it ran before the layers existed -- and a reader watching the log
            // would otherwise have only the warning and no sign that they were set after all.
            if (applied > 0) {
                log::info("project: {} shader-layer parameter(s) applied after the layers loaded", applied);
            }
        }
    } else {
        shaderLayers_.clear();
    }
    stage(7);
    if (doc.contains("timeline")) {
        if (auto r = timeline_.fromJson(doc["timeline"]); !r) {
            return r;
        }
    } else {
        timeline_.clear();
    }
    // ADR-225. Reset when absent rather than inherited, like everything else on this path: a
    // project written before this block existed, or one nobody directed, opens with the defaults
    // and no message -- its absence is the ordinary state, not a fault.
    autoDirector_ = AutoDirectorSettings{};
    if (const auto director = doc.find("autoDirector"); director != doc.end()) {
        auto parsed = AutoDirectorSettings::fromJson(*director);
        if (!parsed) {
            return std::unexpected(parsed.error());
        }
        autoDirector_ = *parsed;
    }
    // ADR-249. Cleared when absent for the same reason the settings are reset: a project with no
    // plan must not inherit the last one's, or Song Mode would direct this piece to another one's
    // sections.
    songPlan_ = SongPlan{};
    if (const auto plan = doc.find("songPlan"); plan != doc.end()) {
        auto parsed = songPlanFromJson(*plan);
        if (!parsed) {
            return std::unexpected(parsed.error());
        }
        songPlan_ = std::move(*parsed);
    }
    // ADR-158. Cleared when absent, like the timeline above: a project without a cut must not
    // inherit the last one's, or the camera would chase a hero this scene has never heard of.
    if (auto* comp = composition()) {
        std::vector<scene::AimFollow> shots;
        if (doc.contains("cameraAimFollow") && doc["cameraAimFollow"].is_array()) {
            for (const auto& entry : doc["cameraAimFollow"]) {
                if (!entry.is_object() || !entry.contains("heroAtCut") || !entry["heroAtCut"].is_array() ||
                    entry["heroAtCut"].size() != 3) {
                    warn("cameraAimFollow: a shot without a 'heroAtCut' position was skipped");
                    continue;
                }
                scene::AimFollow shot;
                shot.startSeconds = entry.value("start", 0.0);
                shot.endSeconds = entry.value("end", 0.0);
                shot.hero = entry.value("hero", std::string{});
                const auto& at = entry["heroAtCut"];
                shot.heroAtCut = glm::vec3(at[0].get<float>(), at[1].get<float>(), at[2].get<float>());
                if (shot.hero.empty() || !(shot.endSeconds > shot.startSeconds)) {
                    warn("cameraAimFollow: a shot with no hero or no duration was skipped");
                    continue;
                }
                shots.push_back(std::move(shot));
            }
        }
        comp->setAimFollow(std::move(shots));
        // ADR-217, and for exactly the reason the table above round-trips: an offline render reloads
        // the project before drawing it, so a hold that only existed in memory would make a rendered
        // file differ from the window that asked for it. It is derived rather than stored -- the
        // settings are already in `autoDirector`, and storing it twice would let the two disagree.
        // A project with no cut gets no hold, because `setAimHold` on an empty scenario is off.
    }
    // ADR-207, and cleared when absent for the same reason the aim-follow table is: a project with
    // no cut must not inherit the last one's, or a world effect would fire against a schedule for a
    // film this scene has never been in.
    {
        std::vector<world::ShotSpan> spans;
        if (doc.contains("cameraShotSpans") && doc["cameraShotSpans"].is_array()) {
            for (const auto& entry : doc["cameraShotSpans"]) {
                if (!entry.is_object()) {
                    continue;
                }
                world::ShotSpan span;
                span.start = entry.value("start", 0.0);
                span.end = entry.value("end", 0.0);
                span.travel = entry.value("travel", false);
                span.spotlight = entry.value("spotlight", false);
                span.emphasis = entry.value("emphasis", 0.0f);
                span.subject = entry.value("subject", std::string{});
                const auto readVec = [&entry](const char* key, glm::vec3& out) {
                    if (entry.contains(key) && entry[key].is_array() && entry[key].size() == 3) {
                        out = glm::vec3(entry[key][0].get<float>(), entry[key][1].get<float>(),
                                        entry[key][2].get<float>());
                    }
                };
                readVec("subjectPosition", span.subjectPosition);
                span.subjectRadius = entry.value("subjectRadius", 1.0f);
                span.handoff = entry.value("handoff", std::string{});
                readVec("handoffPosition", span.handoffPosition);
                if (!(span.end > span.start)) {
                    warn("cameraShotSpans: a span with no duration was skipped");
                    continue;
                }
                spans.push_back(std::move(span));
            }
        }
        shotSpans_ = std::move(spans);
    }
    cueState_ = {};
    cueApplied_ = false;
    states_ = StateMachine{};
    if (doc.contains("states")) {
        if (auto r = states_.fromJson(doc["states"]); !r) {
            return r;
        }
        if (auto r = states_.validate(presets_); !r) {
            log::warn("project states: {}", r.error().message);
        }
    }
    worldMacros_.clear();
    if (doc.contains("worldMacros")) {
        if (!doc["worldMacros"].is_array()) {
            return fail("'worldMacros' must be an array");
        }
        for (const auto& mj : doc["worldMacros"]) {
            auto m = WorldMacro::fromJson(mj);
            if (!m) {
                return std::unexpected(m.error());
            }
            worldMacros_.push_back(std::move(*m));
        }
    }
    if (doc.contains("director")) {
        auto director = WorldDirector::fromJson(doc["director"]);
        if (!director) {
            return std::unexpected(director.error());
        }
        director_ = std::move(*director);
    } else {
        director_ = WorldDirector{};
    }
    applyWorldMacros();
    states_.reset(params_, presets_);
    if (doc.contains("render")) {
        auto r = RenderSettings::fromJson(doc["render"]);
        if (!r) {
            return std::unexpected(r.error());
        }
        render_ = *r;
    } else {
        render_ = RenderSettings{};
    }
    // A project with no "pathtrace" block gets the defaults rather than inheriting the trace
    // settings of whatever was open before -- the same rule the transport's loop follows, and for
    // the same reason: 512 samples set for one piece must not silently govern the next.
    if (doc.contains("pathtrace")) {
        auto r = PathTraceSettings::fromJson(doc["pathtrace"]);
        if (!r) {
            return std::unexpected(r.error());
        }
        pathTrace_ = *r;
    } else {
        pathTrace_ = PathTraceSettings{};
    }
    // The transport's persistent half. A project with no "transport" block clears the loop rather
    // than inheriting the one from whatever was open before: loading a project must not leave a
    // range from another piece quietly governing this one.
    transport_.clearLoop();
    if (doc.contains("transport") && doc["transport"].is_object()) {
        const auto& block = doc["transport"];
        if (block.contains("loop") && block["loop"].is_object()) {
            const auto& loop = block["loop"];
            TransportLoop parsed;
            parsed.enabled = loop.value("enabled", false);
            parsed.startSeconds = loop.value("start", 0.0);
            parsed.endSeconds = loop.value("end", 0.0);
            transport_.setLoop(parsed);
        }
    }
    // Parameter values for sources and shader inputs arrive in the same document; apply them
    // again now that those parameters exist (unknown-at-first-pass paths were skipped).
    if (auto r = params::loadProject(doc, params_, modulator_, nullptr, nullptr); !r) {
        return r;
    }
    // And re-anchor the entity layer, because the document that just landed can *move nodes*.
    //
    // A project serialises every node's position, rotation and scale -- Glowmere Valley 2's carries
    // 5,489 parameters -- and those values are applied *over* whatever the scene registered. The
    // entity layer was bound before that happened, so every entity driving a node the project moved
    // went on believing the node was where the scene file put it while the renderer drew it where
    // the project said. Nothing reported the disagreement and nothing closed it: measured in
    // Glowmere, the saucer's entity and the saucer's node were **28.661 m apart for the whole run**,
    // which is why an animal lifted correctly onto the beam's drawn axis was dragged twenty-eight
    // metres sideways on the way up (ADR-264).
    //
    // `installEntities` is idempotent and re-derives each anchor from the node's world transform,
    // now read from the parameter *bases* -- so this is the whole of the fix and it is general: it
    // is about projects and nodes, and knows nothing about saucers.
    if (scene::Composition* comp = composition(); comp != nullptr) {
        comp->installEntities();
    }
    rebind();
    modulator_.resetState();
    // The sequence goes on last, after every parameter a bake could possibly name exists: the
    // scene's nodes, the camera, the post chain, the sources and the shader inputs. Installing it
    // earlier would bind its tracks to a parameter set that was still being built, which is how a
    // feature ends up correct in every respect except that it does nothing (ADR-075).
    sequence_ = seq::Sequence{};
    sequenceTargets_.clear();
    sequenceReport_ = seq::InstallReport{};
    if (doc.contains("sequence")) {
        auto parsed = seq::Sequence::fromJson(doc["sequence"]);
        if (!parsed) {
            return fail("project sequence: {}", parsed.error().message);
        }
        sequence_ = std::move(*parsed);
        if (auto r = installSequence(); !r) {
            // A sequence that will not install is a load warning, not a load failure: the rest of
            // the project is perfectly good and the author needs to see the piece to fix it.
            log::warn("project sequence: {}", r.error().message);
            noteBindingProblem(fmt::format("sequence: {}", r.error().message));
        }
    }
    finishTimings();
    projectPath_ = path;
    // A loaded project is a different piece. The transport stops and parks at its start rather than
    // carrying the previous project's playhead into it -- opening a project while another is playing
    // used to leave the new one running from wherever the old one had got to.
    refreshTransport();
    transport_.stop();
    seekSeconds(transport_.positionSeconds());
    reportCuePresetOverrides();
    log::info("project '{}' loaded: {} parameters, {} routes, {} sources, {} presets, {} timeline tracks, {} cues, {} warning(s)",
              path.filename().string(), params_.size(), modulator_.routes().size(), sources_.sources().size(),
              presets_.presets().size(), timeline_.tracks().size(), timeline_.cues().size(), projectWarnings_.size());
    return {};
}

void Engine::newProject() {
    sequence_ = seq::Sequence{};
    sequenceTargets_.clear();
    sequenceReport_ = seq::InstallReport{};
    removeLayerParameters();
    layers_.clear();
    timeline_.clear();
    cueState_ = {};
    cueApplied_ = false;
    presets_.clear();
    sources_.clear();
    shaderLayers_.clear();
    environmentPath_.clear();
    loadOrbScene(); // clears the parameter set and routes, re-registers sources/post/shaders
    // Every parameter the engine owns, not only post. `camera/lens`, `camera/exposure` and
    // `camera/focus` sit in the same never-cleared set and used to survive File > New, which made
    // "new project" mean something different from "open project".
    for (auto* p : params_.ordered()) {
        const std::string_view path = p->path();
        if (path.starts_with("post/") || path.starts_with("camera/lens/") ||
            path.starts_with("camera/exposure/") || path.starts_with("camera/focus/") ||
            path == "audio/inputGain") {
            p->resetToDefault();
        }
    }
    post_ = scene::PostSettings{};
    lens_ = scene::LensSettings{};
    exposure_ = scene::ExposureSettings{};
    focus_ = scene::FocusSettings{};
    render_ = RenderSettings{};
    controlHub_.setMap(control::ControlMap{});
    outputs_ = nlohmann::json::array();
    setTempoSource(TempoSource::Analysis);
    ensureControlSource();
    sources_.attach(bus_, params_);
    modulator_.masterGain = 1.0f;
    // File > New is a project with no audio, like opening one that names none: the clip list was
    // already cleared here but the *installed* buffer was not, so the last piece kept playing.
    // A live input is a device choice and is left running (see `loadProject`).
    audioClips_.clear();
    clipSources_.clear();
    audioMix_ = audio::MixReport{};
    if (input_ == nullptr) {
        static_cast<void>(installAudio(nullptr));
    }
    projectPath_.clear();
    projectWarnings_.clear();
    transport_.clearLoop();
    refreshTransport();
    transport_.stop();
}

std::vector<std::filesystem::path> Engine::referencedFiles() const {
    std::vector<std::filesystem::path> files;
    auto add = [&](const std::filesystem::path& p) {
        if (p.empty()) {
            return;
        }
        const auto abs = std::filesystem::absolute(p).lexically_normal();
        if (std::find(files.begin(), files.end(), abs) == files.end()) {
            files.push_back(abs);
        }
    };
    add(audioPath_);
    for (const audio::AudioClip& clip : audioClips_) {
        add(clip.file); // every file the arrangement plays, not only the one `audioPath_` names
    }
    add(environmentPath_);
    if (const auto* gltf = dynamic_cast<const scene::GltfScene*>(controller_.get())) {
        add(gltf->path());
    }
    if (!compositionPath_.empty()) {
        add(compositionPath_);
        visitSceneFileAssets(std::filesystem::absolute(compositionPath_), [&](const auto& p, bool) { add(p); });
    } else if (const auto* comp = dynamic_cast<const scene::Composition*>(controller_.get())) {
        for (const auto& node : comp->nodes()) {
            if (!node->asset.empty()) {
                const auto asset = registry_.resolve(node->asset);
                add(asset);
                if (node->kind == scene::NodeKind::Scene) {
                    visitSceneFileAssets(asset, [&](const auto& p, bool) { add(p); });
                }
            }
        }
    }
    for (const auto& layer : shaderLayers_.layers()) {
        add(layer->path);
    }
    return files;
}

Result<void> Engine::exportBundle(const std::filesystem::path& dir) {
    std::error_code ec;
    const auto assetsDir = dir / "assets";
    std::filesystem::create_directories(assetsDir, ec);
    if (ec) {
        return fail("cannot create '{}': {}", assetsDir.string(), ec.message());
    }
    // Unique destination names: keep the file name, suffix on collision between different sources.
    std::map<std::filesystem::path, std::filesystem::path> placed; // source -> destination
    std::set<std::string> usedNames;
    auto place = [&](const std::filesystem::path& source) -> std::filesystem::path {
        if (const auto it = placed.find(source); it != placed.end()) {
            return it->second;
        }
        std::string name = source.filename().string();
        const std::string stem = source.stem().string();
        const std::string ext = source.extension().string();
        for (int i = 2; usedNames.count(name) != 0; ++i) {
            name = stem + "_" + std::to_string(i) + ext;
        }
        usedNames.insert(name);
        const auto dest = assetsDir / name;
        placed[source] = dest;
        return dest;
    };
    auto copyFile = [&](const std::filesystem::path& source) -> Result<std::filesystem::path> {
        const auto dest = place(source);
        if (!std::filesystem::exists(source)) {
            return fail("missing file '{}'", source.string());
        }
        std::filesystem::copy_file(source, dest, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            return fail("cannot copy '{}' to '{}': {}", source.string(), dest.string(), ec.message());
        }
        std::string ext = source.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext == ".gltf") {
            // External buffers and images live next to the file; bring the plausible ones along.
            for (const auto& entry : std::filesystem::directory_iterator(source.parent_path(), ec)) {
                if (entry.is_regular_file() && isGltfSidecar(entry.path())) {
                    std::filesystem::copy_file(entry.path(), assetsDir / entry.path().filename(),
                                               std::filesystem::copy_options::overwrite_existing, ec);
                }
            }
        }
        return dest;
    };
    // Scene files are rewritten so their references point into the bundle.
    std::function<Result<std::filesystem::path>(const std::filesystem::path&, int)> bundleScene;
    bundleScene = [&](const std::filesystem::path& sceneFile, int depth) -> Result<std::filesystem::path> {
        if (depth > scene::Composition::kMaxNestingDepth) {
            return fail("scene files nest too deeply at '{}'", sceneFile.string());
        }
        if (const auto it = placed.find(sceneFile); it != placed.end()) {
            return it->second;
        }
        std::ifstream in(sceneFile);
        nlohmann::json doc = nlohmann::json::parse(in, nullptr, false);
        if (!doc.is_object()) {
            return fail("'{}' is not a valid scene file", sceneFile.string());
        }
        const auto dest = place(sceneFile);
        const auto srcDir = sceneFile.parent_path();
        if (doc.contains("environment") && doc["environment"].is_object() && doc["environment"].contains("map") &&
            doc["environment"]["map"].is_string()) {
            auto copied = copyFile(resolveFrom(doc["environment"]["map"].get<std::string>(), srcDir));
            if (!copied) {
                return std::unexpected(copied.error());
            }
            doc["environment"]["map"] = copied->filename().generic_string();
        }
        if (doc.contains("materialPrograms") && doc["materialPrograms"].is_array()) {
            for (auto& entry : doc["materialPrograms"]) {
                if (!entry.is_string()) {
                    continue;
                }
                auto copied = copyFile(resolveFrom(entry.get<std::string>(), srcDir));
                if (!copied) {
                    return std::unexpected(copied.error());
                }
                entry = copied->filename().generic_string();
            }
        }
        if (doc.contains("nodes") && doc["nodes"].is_array()) {
            for (auto& node : doc["nodes"]) {
                if (!node.is_object() || !node.contains("asset") || !node["asset"].is_string()) {
                    continue;
                }
                const auto asset = resolveFrom(node["asset"].get<std::string>(), srcDir);
                Result<std::filesystem::path> copied =
                    node.value("kind", std::string()) == "scene" ? bundleScene(asset, depth + 1) : copyFile(asset);
                if (!copied) {
                    return std::unexpected(copied.error());
                }
                node["asset"] = copied->filename().generic_string();
            }
        }
        std::ofstream out(dest);
        if (!out) {
            return fail("cannot write '{}'", dest.string());
        }
        out << doc.dump(2) << '\n';
        return dest;
    };

    // Save the project first (captures the current state), then rewrite its references.
    const auto projectFile = dir / "project.json";
    if (auto r = saveProject(projectFile); !r) {
        return r;
    }
    std::ifstream in(projectFile);
    nlohmann::json doc = nlohmann::json::parse(in, nullptr, false);
    in.close();
    auto& assets = doc["assets"];
    if (assets.contains("audio")) {
        auto copied = copyFile(resolveFrom(assetRefPath(assets["audio"]), dir));
        if (!copied) {
            return std::unexpected(copied.error());
        }
        setAssetRefPath(assets["audio"], relativeTo(*copied, dir));
    }
    if (assets.contains("environment")) {
        auto copied = copyFile(resolveFrom(assetRefPath(assets["environment"]), dir));
        if (!copied) {
            return std::unexpected(copied.error());
        }
        setAssetRefPath(assets["environment"], relativeTo(*copied, dir));
    }
    if (assets.contains("scene") && assets["scene"].contains("path")) {
        const auto scenePath = resolveFrom(assetRefPath(assets["scene"]["path"]), dir);
        auto copied = assets["scene"]["kind"] == "composition" ? bundleScene(scenePath, 0) : copyFile(scenePath);
        if (!copied) {
            return std::unexpected(copied.error());
        }
        // A rewritten scene file has new content: refresh its identity.
        assets["scene"]["path"] = assetRefJson(*copied, dir);
    } else if (assets.contains("scene") && assets["scene"].contains("inline")) {
        // Inline compositions: write them out as a scene file in the bundle so nodes' assets can
        // be rewritten like any other scene file.
        const auto tmp = assetsDir / "composition.json";
        {
            std::ofstream out(tmp);
            // Node assets are relative to the composition's registry base; make them absolute.
            nlohmann::json inlineScene = assets["scene"]["inline"];
            for (auto& node : inlineScene["nodes"]) {
                if (node.contains("asset") && node["asset"].is_string()) {
                    node["asset"] = registry_.resolve(node["asset"].get<std::string>()).string();
                }
            }
            if (inlineScene.contains("environment") && inlineScene["environment"].contains("map")) {
                inlineScene["environment"]["map"] =
                    registry_.resolve(inlineScene["environment"]["map"].get<std::string>()).string();
            }
            out << inlineScene.dump(2) << '\n';
        }
        placed.erase(tmp);
        usedNames.erase("composition.json");
        auto bundled = bundleScene(tmp, 0);
        if (!bundled) {
            return std::unexpected(bundled.error());
        }
        assets["scene"] = {{"kind", "composition"}, {"path", relativeTo(*bundled, dir)}};
    }
    if (doc.contains("shaders")) {
        for (auto& entry : doc["shaders"]) {
            if (entry.is_object() && entry.contains("path") && entry["path"].is_string()) {
                auto copied = copyFile(resolveFrom(entry["path"].get<std::string>(), dir));
                if (!copied) {
                    return std::unexpected(copied.error());
                }
                entry["path"] = relativeTo(*copied, dir);
            }
        }
    }
    std::ofstream out(projectFile);
    if (!out) {
        return fail("cannot write '{}'", projectFile.string());
    }
    out << doc.dump(2) << '\n';
    log::info("bundle exported to '{}': {} file(s)", dir.string(), placed.size());
    return {};
}

Result<void> Engine::loadScene(const std::filesystem::path& path) {
    // Import first so a failed load leaves the current scene, parameters and routes untouched.
    auto ctrl = scene::GltfScene::load(path);
    if (!ctrl) {
        return std::unexpected(ctrl.error());
    }
    const float masterGain = modulator_.masterGain;
    detachSceneParameters();
    params_.clear();
    modulator_.clearRoutes();
    modulator_.masterGain = masterGain;
    (*ctrl)->attach(params_, modulator_);
    installController(std::move(*ctrl));
    return reapplyEnvironment();
}

void Engine::newComposition() {
    const float masterGain = modulator_.masterGain;
    detachSceneParameters();
    params_.clear();
    modulator_.clearRoutes();
    modulator_.masterGain = masterGain;
    auto comp = std::make_unique<scene::Composition>(registry_, "composition");
    comp->attach(params_, modulator_);
    compositionPath_.clear();
    installController(std::move(comp));
    if (auto r = reapplyEnvironment(); !r) {
        log::warn("environment: {}", r.error().message);
    }
}

Result<void> Engine::loadComposition(const std::filesystem::path& rawPath) {
    return loadComposition(rawPath, nlohmann::json());
}

Result<void> Engine::loadComposition(const std::filesystem::path& rawPath, const nlohmann::json& nodeEdits) {
    const auto path = std::filesystem::absolute(rawPath).lexically_normal();
    registry_.setBaseDirectory(path.parent_path());
    auto comp = scene::Composition::loadFile(path, registry_, nodeEdits);
    if (!comp) {
        return std::unexpected(comp.error());
    }
    compositionPath_ = path;
    return installComposition(std::move(*comp));
}

Result<void> Engine::setCompositionJson(const nlohmann::json& document) {
    // The registry keeps whatever base directory the current composition was loaded against, so a
    // document restored from memory resolves its assets exactly as the one it replaces did.
    auto comp = scene::Composition::fromJson(document, registry_);
    if (!comp) {
        return std::unexpected(comp.error());
    }
    return installComposition(std::move(*comp));
}

Result<void> Engine::installComposition(std::unique_ptr<scene::Composition> composition) {
    auto comp = std::move(composition);
    const float masterGain = modulator_.masterGain;
    detachSceneParameters();
    params_.clear();
    modulator_.clearRoutes();
    modulator_.masterGain = masterGain;
    comp->attach(params_, modulator_);
    // ADR-059: the composition's own `post` block, kept until the parameters it names exist again.
    // `params_.clear()` above destroyed the previous set, and installController below is what
    // re-registers the post parameters -- reading postParams_ before that point is a dangling
    // pointer, which is exactly the bug the first version of this had.
    const nlohmann::json postJson = comp->postJson();
    // A scene file may carry its own environment map.
    if (!comp->environmentMap().empty()) {
        environmentPath_ = registry_.resolve(comp->environmentMap());
    }
    installController(std::move(comp));
    // Now the parameters exist. The project's own `parameters` block is applied at the end of the
    // project load and still overrides anything set here.
    if (auto r = scene::applyPostJson(postJson, postParams_); !r) {
        return r;
    }
    return reapplyEnvironment();
}

Result<void> Engine::saveComposition(const std::filesystem::path& path) {
    auto* comp = composition();
    if (comp == nullptr) {
        return fail("the current scene is not a composition (use New Composition or add a node first)");
    }
    // Rebase every asset path on the scene file's folder so the file can move with its assets:
    // absolute first (against the current base), then relative to the new base.
    for (auto& node : comp->nodes()) {
        if (!node->asset.empty()) {
            node->asset = registry_.resolve(node->asset);
        }
    }
    registry_.setBaseDirectory(path.parent_path());
    for (auto& node : comp->nodes()) {
        if (!node->asset.empty()) {
            const std::string id = registry_.assetId(node->asset);
            node->asset = id.empty() ? registry_.relativise(node->asset) : std::filesystem::path(id);
        }
    }
    comp->setEnvironmentMap(environmentPath_.empty() ? std::filesystem::path()
                                                     : registry_.relativise(registry_.resolve(environmentPath_)));
    if (auto r = comp->saveFile(path); !r) {
        return r;
    }
    if (!environmentPath_.empty()) {
        comp->setEnvironmentMap(environmentPath_);
    }
    compositionPath_ = path;
    return {};
}

Result<scene::CompositionNode*> Engine::addNode(scene::CompositionNode node) {
    if (composition() == nullptr) {
        newComposition();
    }
    auto added = composition()->addNode(std::move(node));
    if (added) {
        rebind();
    }
    return added;
}

void Engine::removeNode(const std::string& name) {
    if (auto* comp = composition()) {
        if (comp->removeNode(name)) {
            rebind();
        }
    }
}

void Engine::loadOrbScene() {
    detachSceneParameters();
    params_.clear();
    modulator_.clearRoutes();
    installController(std::make_unique<scene::OrbScene>(params_, modulator_));
    if (auto r = reapplyEnvironment(); !r) {
        log::warn("environment: {}", r.error().message);
    }
}

Result<void> Engine::reapplyEnvironment() {
    if (environmentPath_.empty()) {
        return {};
    }
    if (auto* comp = composition()) {
        // A composition rebuilds its texture list, so it owns its environment map (loaded
        // through the registry on rebuild).
        comp->setEnvironmentMap(environmentPath_);
        return {};
    }
    auto image = assets::loadImage(environmentPath_, false);
    if (!image) {
        return std::unexpected(image.error());
    }
    if (!image->isHdr()) {
        return fail("'{}' is not an HDR image", environmentPath_.string());
    }
    auto& sc = controller_->scene();
    sc.environment.environmentMap = sc.addTexture(std::move(*image));
    return {};
}

Result<void> Engine::loadEnvironment(const std::filesystem::path& path) {
    auto image = assets::loadImage(path, false);
    if (!image) {
        return std::unexpected(image.error());
    }
    if (!image->isHdr()) {
        return fail("'{}' is not an HDR (Radiance .hdr) image", path.string());
    }
    if (auto* comp = composition()) {
        comp->setEnvironmentMap(path);
    } else {
        auto& sc = controller_->scene();
        sc.environment.environmentMap = sc.addTexture(std::move(*image));
    }
    environmentPath_ = path;
    log::info("environment map '{}' installed", path.filename().string());
    return {};
}

Result<void> Engine::loadFile(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == ".gltf" || ext == ".glb") {
        return loadScene(path);
    }
    if (ext == ".hdr") {
        return loadEnvironment(path);
    }
    if (ext == ".json") {
        std::ifstream in(path);
        nlohmann::json doc = nlohmann::json::parse(in, nullptr, false);
        if (doc.is_object() && doc.value("format", std::string()) == scene::Composition::kFormatName) {
            return loadComposition(path);
        }
        return loadProject(path);
    }
    if (ext == ".wgsl" || ext == ".isf") {
        auto id = addShaderLayer(path, shaders::LayerStage::Background);
        if (!id) {
            return std::unexpected(id.error());
        }
        return {};
    }
    auto duration = loadAudio(path);
    if (!duration) {
        return std::unexpected(duration.error());
    }
    return {};
}

Engine::~Engine() {
    runner_.reset();
    player_.reset();
}

Result<void> Engine::installAudio(std::shared_ptr<const audio::AudioFile> file) {
    if (!file || file->frameCount() == 0) {
        // An empty arrangement is "no audio", which is an ordinary state -- not a device with
        // nothing in it. Opening one for a buffer of no samples would hold the sound card for a
        // project that has none.
        if (mode_ == EngineMode::Live && player_) {
            runner_.reset();
            static_cast<void>(player_->setSource(nullptr));
        }
        track_.reset();
        audioFile_.reset();
        audioPath_.clear();
        offlineFrameCursor_ = 0;
        hasFrame_ = false;
        ++audioRevision_;
        refreshTransport();
        return {};
    }
    analyzerConfig_.sampleRate = file->sampleRate();

    if (mode_ == EngineMode::Live) {
        runner_.reset();
        if (auto r = player_->setSource(file); !r) {
            return std::unexpected(r.error());
        }
        runner_ = std::make_unique<analysis::AnalysisRunner>(analyzerConfig_, player_->analysisStream());
        runner_->start();
    }
    // The whole-track analysis, in *both* modes.
    //
    // Live playback reads the runner's per-frame stream and never touches this, so it was only
    // built offline -- which meant `track()` was null in the windowed application and anything
    // needing the whole piece could not exist there. The camera director is exactly that: a musical
    // structure is a fold over a complete track, so "Direct to Music" could never enable no matter
    // how much audio was loaded. A loaded *file* is a finite, known signal and can be analyzed up
    // front whatever mode is playing it; a live input genuinely has no track and correctly gets
    // none.
    //
    // It costs one pass over the file at load -- about 130 ms for ninety seconds -- and both of the
    // places that read `track_` are already behind a mode or player check, so this is inert for
    // live rendering.
    track_ = std::make_shared<analysis::AnalysisTrack>(
        analysis::AnalysisTrack::analyze(*file, analyzerConfig_));
    offlineFrameCursor_ = 0;
    log::info("analyzed {:.2f} s of audio: {} frames", file->durationSeconds(), track_->frames().size());
    audioFile_ = std::move(file);
    ++audioRevision_;
    modulator_.resetState();
    music_.reset();
    hasFrame_ = false;
    // The piece just got a length, or a different one. Refreshed here rather than left to the next
    // frame so that everything which asks the engine how long the project is between loading and
    // rendering -- the AI tools, a script, a render job built before the first tick -- gets the
    // answer the file just gave.
    refreshTransport();
    return {};
}

Result<double> Engine::loadAudio(const std::filesystem::path& path) {
    // One file is an arrangement of one clip, and goes through exactly the same mixer as ten. That
    // is only safe because a one-clip mix is bit-identical to the file (ADR-103, and the test that
    // says so); it is worth it because there is then one audio path rather than two that agree most
    // of the time.
    if (auto r = setAudioClips({audio::AudioClip{.file = path}}); !r) {
        return std::unexpected(r.error());
    }
    if (!audioFile_) {
        return fail("'{}' decoded to no audio", path.filename().string());
    }
    audioPath_ = path;
    return audioFile_->durationSeconds();
}

Result<void> Engine::setAudioClips(std::vector<audio::AudioClip> clips) {
    audioClips_ = std::move(clips);
    return rebuildAudio();
}

Result<void> Engine::rebuildAudio() {
    const std::vector<std::string> failures = clipSources_.sync(audioClips_);
    if (audioClips_.size() == 1 && !failures.empty()) {
        // One file, and it did not load. That is exactly the old `loadAudio` failure, so it is
        // *returned* rather than noted: the project loader turns it into one warning in its own
        // words, and noting it here as well would report the same missing file twice.
        return fail("{}", failures.front());
    }
    for (const std::string& problem : failures) {
        noteBindingProblem(problem);
        log::warn("{}", problem);
    }
    auto mixed = audio::mixArrangement(audioClips_, clipSources_, &audioMix_);
    if (!mixed) {
        return std::unexpected(mixed.error());
    }
    for (const std::string& warning : audioMix_.warnings) {
        noteBindingProblem(warning);
        log::warn("{}", warning);
    }
    if (audioMix_.clipsMixed > 1) {
        log::info("audio: {} clip(s) mixed to {:.2f} s at {} Hz, {} ch in {:.1f} ms",
                  audioMix_.clipsMixed, audioMix_.durationSeconds, audioMix_.sampleRate,
                  audioMix_.channels, audioMix_.millis);
    }
    auto shared = std::make_shared<const audio::AudioFile>(std::move(*mixed));
    if (auto r = installAudio(shared); !r) {
        return r;
    }
    // The path a project writes as its audio asset, and the one the UI shows. Only meaningful when
    // the arrangement is a single untouched file; anything richer is written as a clip list instead.
    audioPath_.clear();
    if (audioClips_.size() == 1 && audioClips_.front().enabled && audioFile_) {
        audioPath_ = audioClips_.front().file;
    }
    return {};
}

Result<void> Engine::play() {
    refreshTransport();
    // Play from the end starts again, which the transport decides; the position it lands on is what
    // the audio device has to be told about, so the device is synchronised after the state change
    // rather than before it.
    if (!transport_.play()) {
        return {}; // already playing: not a failure, and not a reason to restart the device
    }
    return syncAudioToTransport(true);
}

void Engine::pause() {
    transport_.pause();
    audioFollowing_ = false;
    if (player_) {
        player_->pause();
    }
}

void Engine::togglePlay() {
    if (isPlaying()) {
        pause();
    } else if (auto r = play(); !r) {
        log::warn("play: {}", r.error().message);
    }
}

void Engine::stop() {
    // Stop parks at the start of the play range -- the loop start with a loop on, else zero -- which
    // is the conventional stop and is what this did before by way of AudioPlayer::stop (pause, then
    // seek to 0). The seek carries the rest: modulation, sources, the music classifier, the beat
    // clock, the entities and the event scheduler.
    transport_.stopInPlace();
    audioFollowing_ = false;
    if (player_) {
        player_->pause();
    }
    seekSeconds(transport_.playStartSeconds());
}

void Engine::seekSeconds(double seconds) {
    // ADR-182's control, and it is here rather than in the instrument on purpose: this is inside the
    // work a seek performs, so a calibration run slows the *pipeline* by a known amount and the
    // latency harness either sees it or is not a latency harness. Zero unless --latency-inject asked.
    core::applyInjectedDelayForOpenInteraction();
    // TEMPORARY (ui-responsiveness phase 2): every seek, wherever it came from.
    ++probe2::frame().seeks;
    const probe2::Add probeSeek(probe2::frame().seekMs);
    // Clamped by the transport first, and everything below resynchronises to the position it
    // actually took. Passing the *requested* second on to the entity world and the event scheduler
    // while the playhead sat somewhere else is how a seek past the end used to leave the two
    // disagreeing about where the piece was.
    const double target = transport_.seek(seconds);
    if (player_) {
        player_->seekSeconds(target);
    }
    if (track_) {
        // The offline analysis cursor walks forward through the frames, so a backwards seek has to
        // rewind it or every frame between here and where it had got to is skipped. Rewound rather
        // than reset to zero: a forward seek keeps its place.
        offlineFrameCursor_ = 0;
    }
    seconds = target;
    modulator_.resetState();
    sources_.reset();
    // A seek discontinuity in the energy history reads as a drop; the detector must not carry
    // the old piece across it.
    music_.reset();
    beatClockPhase_ = 0.0;
    lastAnalysisBeatCount_ = 0;
    cueState_ = {};   // cues re-sync from the new position on the next frame
    cueApplied_ = false;
    // Entities, too (ADR-093). Until this existed a seek left every character exactly where the
    // playhead had walked it to, so scrubbing back to the same second twice gave two different
    // frames -- `EntityWorld::reset` was written for this and nothing ever called it. `seek` is
    // the version that belongs here: `reset` alone would put every character back at its t = 0
    // pose, which is a different frame from the one the seeked second actually has.
    if (scene::Composition* composition = this->composition()) {
        // The director first (ADR-209), because it is the tier above: it drops every claim, releases
        // every body it was holding and puts back every parameter it wrote -- so the re-simulation
        // below starts from the scene the file describes rather than from a beam left lit and a cow
        // left invisible twenty metres in the air. A scenario that autostarts picks up again on the
        // next frame, which is what makes the seeked second a function of the second rather than of
        // how the playhead got there.
        {
            const probe2::Add probeDirector(probe2::frame().directorResetMs); // TEMPORARY: phase 2
            composition->director().reset(&composition->entityWorld(), &params_);
        }
        // ADR-217: and the camera's hold on it, for the same reason. The hold is derived from the
        // scenario's state, and the scenario has just been put back to the top -- a hold left armed
        // across the seek would keep the camera on a shot the new second is nowhere near.
        composition->clearAimFollowState();
        // ADR-245: and the camera director's view of what has happened, for exactly the same
        // reason. An event span observed before the jump describes a run of a scenario that the
        // seek has just abolished; carrying it over would cut to an event camera for an event that
        // is no longer happening.
        composition->clearCameraEventState();
        // No camera position and no distance-detail flag: a seek that culled by distance was a
        // function of where the camera happened to be, and ADR-267 measured that at 50.263 m over
        // eight explorers at thirty seconds. What used to be saved by skipping distant bodies is
        // bounded here instead, in the unit the cost is actually paid in (ADR-273).
        composition->entityWorld().seek(seconds, &params_, nullptr, 1.0 / 60.0,
                                        entity::SeekBudget{.maxSeconds = 90.0,
                                                           .maxBodySteps = seekBodyStepBudget_});
        // Skinning has its own "a frame ago", and a seek makes that sentence false: the joints were
        // not anywhere a frame ago. Left alone, the first frame after every scrub carries joint
        // motion vectors for a jump nobody made and the character smears. Told here rather than
        // collapsed here, because the rigs have not been re-posed at this point -- see
        // `SkinnedRig::reseedPrevious`.
        for (scene::SkinnedRig& rig : composition->scene().rigs) {
            rig.reseedAfterDiscontinuity();
        }
    }
    // A live event belongs to the moment it happened and the moment is gone; the scheduled tier is
    // rebased rather than cleared, so the next frame restores the standing intents at the new
    // playhead instead of replaying everything between here and there (ADR-098).
    sequenceEvents_.reset(seconds);
    firedEvents_.clear();
    // The timeline clock, now rather than on the next update. Everything above has just been told
    // the new second; leaving the clock a frame behind means anything that reads it between a seek
    // and the next frame -- a panel drawing the playhead, a script asserting where it landed, a tool
    // reporting the position -- sees the second the playhead has left.
    timelineClock_.seconds = seconds;
    // Any seek by any route settles the interactive request too. Without this a `stop()` or a
    // frame step would leave `seekEvaluatedSeconds_` naming a second the world is no longer at, and
    // the next drag's first frame would compare against a lie.
    seekRequestSeconds_ = seconds;
    seekEvaluatedSeconds_ = seconds;
    seekPending_ = false;
    // T3 for the interaction log: the authoritative state has changed. Everything above this line
    // is what a scrub costs, and the log's `input->ack` minus this is where it went.
    core::interactions().markModel();
}

// ---- the interactive seek -------------------------------------------------------------------

void Engine::requestSeek(double seconds, bool gestureHeld) {
    // The cheap half, now. This is what draws the playhead and what every readout in the
    // application means by "where are we": the transport's own arithmetic plus the timeline clock.
    // Both were already set inside `seekSeconds`; taking them out of it and doing them here is the
    // whole of the immediate response, and it costs nothing measurable.
    const double target = transport_.seek(seconds);
    if (player_) {
        player_->seekSeconds(target);
    }
    timelineClock_.seconds = target;
    seekRequestSeconds_ = target;
    seekGestureHeld_ = gestureHeld;
    seekPending_ = true;
    if (interactiveSeekBudgetMs_ <= 0.0) {
        // The budget is zero everywhere except the live editor, so every other caller -- an offline
        // render, a test, the control hub -- gets exactly what it got before: the whole seek, on
        // this frame, in this call. A deferral that could reach a deterministic render would make
        // what the render contains a function of a wall clock.
        seekSeconds(target);
        seekEvaluatedSeconds_ = target;
        seekPending_ = false;
    }
}

bool Engine::serviceSeekRequest(double elapsedMs) {
    if (!seekPending_) {
        return false;
    }
    if (!scene::advanceSeekDeferral(seekDeferral_, seekRequestSeconds_, seekEvaluatedSeconds_,
                                    seekGestureHeld_, elapsedMs)) {
        return false;
    }
    const auto start = std::chrono::steady_clock::now();
    seekSeconds(seekRequestSeconds_);
    seekDeferral_.lastMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    seekEvaluatedSeconds_ = seekRequestSeconds_;
    seekPending_ = false;
    return true;
}

bool Engine::isPlaying() const { return transport_.isPlaying(); }

double Engine::positionSeconds() const {
    if (input_) {
        // Live capture has no timeline to be positioned on: "now" is how much has been captured, and
        // that is what every meter and readout in the application means by it.
        return input_->sampleRate() > 0 ? static_cast<double>(input_->framesCaptured()) / input_->sampleRate() : 0.0;
    }
    return transport_.positionSeconds();
}

double Engine::audioDurationSeconds() const { return audioFile_ ? audioFile_->durationSeconds() : 0.0; }

double Engine::durationSeconds() const { return transport_.durationSeconds(); }

void Engine::refreshTransport() {
    // The project's length is the longest thing in it. A sequence that runs past its audio is a
    // sequence that should play to its end, and a project with no audio at all still has a length.
    double duration = audioDurationSeconds();
    duration = std::max(duration, sequence_.duration());
    duration = std::max(duration, timeline_.durationSeconds());
    transport_.setDuration(duration);
    // The tempo is for the bars/beats readout and for beat stepping with no analyzed grid. It comes
    // from wherever the beat clock came from this frame, so the display cannot disagree with the
    // signals.
    const double bpm = midiClockActive_ ? controlHub_.midiClock().bpm()
                                        : (hasFrame_ ? static_cast<double>(latest_.tempoBpm) : 0.0);
    transport_.setTempo(bpm, 4);
    // The project's frame rate *is* the render settings' frame rate. Not a second one: the frames a
    // person steps through have to be the frames the project exports, and two numbers that are
    // nearly always equal are two numbers that will one day not be.
    transport_.setFrameRate(FrameRate::fromFps(render_.fps));
}

Result<void> Engine::syncAudioToTransport(bool seekDevice) {
    if (!player_ || !player_->hasSource()) {
        audioFollowing_ = false;
        return {};
    }
    // Audio follows at unit rate and is silent otherwise. `AudioPlayer` has no rate control, and
    // ADR-102 refuses to fake one: a device left running at 1x under a 2x transport drifts a second
    // out every second, which is worse than silence and much harder to notice.
    const bool wants = transport_.isPlaying() && transport_.rate() == 1.0;
    if (seekDevice) {
        player_->seekSeconds(transport_.positionSeconds());
    }
    if (wants == audioFollowing_) {
        return {};
    }
    audioFollowing_ = wants;
    if (!wants) {
        player_->pause();
        return {};
    }
    if (auto r = player_->play(); !r) {
        // The transport keeps playing: the visuals are not hostage to a device that would not
        // start, and a piece running silently is a better answer than nothing happening at all.
        audioFollowing_ = false;
        return r;
    }
    return {};
}

void Engine::stepFrames(std::int64_t frames) {
    if (frames == 0) {
        return;
    }
    // Through the transport's own arithmetic for *where*, and through seekSeconds for *everything
    // else that has to move with it*.
    const double target =
        transport_.secondsOfFrame(transport_.frameOf(transport_.positionSeconds()) + frames);
    seekSeconds(target);
}

double Engine::beatBoundary(double fromSeconds, int direction) const {
    if (direction == 0) {
        return fromSeconds;
    }
    // The analyzed grid first: it is where the beats actually are, as opposed to where a constant
    // tempo says they ought to be, and a piece that breathes is exactly where that difference shows.
    if (track_ != nullptr) {
        const auto& beats = track_->beats().beatTimes;
        if (!beats.empty()) {
            constexpr double kNudge = 1e-3; // so "next" from exactly on a beat is the following one
            if (direction > 0) {
                for (const float t : beats) {
                    if (static_cast<double>(t) > fromSeconds + kNudge) {
                        return static_cast<double>(t);
                    }
                }
                return fromSeconds;
            }
            double best = fromSeconds;
            bool found = false;
            for (const float t : beats) {
                if (static_cast<double>(t) < fromSeconds - kNudge) {
                    best = static_cast<double>(t);
                    found = true;
                } else {
                    break;
                }
            }
            return found ? best : fromSeconds;
        }
    }
    const double bpm = transport_.tempoBpm();
    if (!(bpm > 0.0)) {
        return fromSeconds; // no grid and no tempo: a beat step has nothing to step to
    }
    const double beatSeconds = 60.0 / bpm;
    const double beat = fromSeconds / beatSeconds;
    const double next = direction > 0 ? std::floor(beat + 1e-6) + 1.0 : std::ceil(beat - 1e-6) - 1.0;
    return std::max(0.0, next * beatSeconds);
}

double Engine::markerBoundary(double fromSeconds, int direction, bool sectionsOnly) const {
    if (direction == 0) {
        return fromSeconds;
    }
    constexpr double kNudge = 1e-3;
    double best = fromSeconds;
    bool found = false;
    for (const seq::Marker& marker : sequence_.markers) {
        if (marker.kind == seq::MarkerKind::Beat) {
            continue;
        }
        if (sectionsOnly && marker.kind != seq::MarkerKind::Section) {
            continue;
        }
        if (direction > 0) {
            if (marker.timeSeconds > fromSeconds + kNudge && (!found || marker.timeSeconds < best)) {
                best = marker.timeSeconds;
                found = true;
            }
        } else if (marker.timeSeconds < fromSeconds - kNudge && (!found || marker.timeSeconds > best)) {
            best = marker.timeSeconds;
            found = true;
        }
    }
    return found ? best : fromSeconds;
}

void Engine::stepMarkers(int direction, bool sectionsOnly) {
    // A piece with no section markers gets every marker instead. Shift+arrow going nowhere at all
    // would read as a broken key rather than as "this song has no sections".
    const bool anySections =
        sectionsOnly && std::any_of(sequence_.markers.begin(), sequence_.markers.end(),
                                    [](const seq::Marker& m) {
                                        return m.kind == seq::MarkerKind::Section;
                                    });
    const double target =
        markerBoundary(transport_.positionSeconds(), direction, sectionsOnly && anySections);
    if (target != transport_.positionSeconds()) {
        seekSeconds(target);
    }
}

void Engine::stepBeats(int beats) {
    if (beats == 0) {
        return;
    }
    double position = transport_.positionSeconds();
    const int direction = beats > 0 ? 1 : -1;
    for (int i = 0; i < std::abs(beats); ++i) {
        const double next = beatBoundary(position, direction);
        if (next == position) {
            break; // ran out of grid
        }
        position = next;
    }
    seekSeconds(position);
}

void Engine::setVolume(float volume) {
    if (player_) {
        player_->setVolume(volume);
    }
}

float Engine::volume() const { return player_ ? player_->volume() : 1.0f; }

Result<void> Engine::useAudioInput(const std::string& deviceName) {
    if (mode_ != EngineMode::Live) {
        return fail("live audio input needs the live engine");
    }
    auto input = std::make_unique<audio::AudioInput>();
    if (auto r = input->open(deviceName); !r) {
        return r;
    }
    runner_.reset();
    if (player_) {
        player_->pause();
    }
    analyzerConfig_.sampleRate = input->sampleRate();
    runner_ = std::make_unique<analysis::AnalysisRunner>(analyzerConfig_, input->analysisStream());
    runner_->start();
    input_ = std::move(input);
    audioFile_.reset();
    audioPath_.clear();
    music_.reset();
    hasFrame_ = false;
    beatClockPhase_ = 0.0;
    beatClockCount_ = 0;
    lastAnalysisBeatCount_ = 0;
    log::info("live audio input '{}' at {} Hz", input_->deviceName(), input_->sampleRate());
    return {};
}

void Engine::stopAudioInput() {
    if (!input_) {
        return;
    }
    runner_.reset();
    input_.reset();
    music_.reset();
    hasFrame_ = false;
    audioSignals_.publishSilence(bus_);
}

FrameTime Engine::tick(FrameClock& clock) {
    FrameTime time = clock.tick();
    if (mode_ == EngineMode::Offline) {
        // Nothing to decide: the fixed-step clock is the authority offline. `update` is what records
        // the position on the transport, because an offline caller may build its own `FrameTime` and
        // call `update` without ever coming through here -- the render job does not, but several
        // tests and the headless benchmark do, and a timeline that only advanced for callers who
        // used the right entry point would be a trap.
        lastRenderTime_ = time.renderTime;
        return time;
    }
    transport_.setMode(TransportMode::Realtime);
    refreshTransport();
    if (transport_.isPlaying()) {
        // Audio is still the master clock while it is running (ADR-012): the transport reads the
        // play-head rather than integrating, so a device that jitters or stalls cannot make the
        // visuals drift away from the sound. With no audio -- or at any rate but 1x, where the
        // device is deliberately silent -- it integrates the elapsed wall time instead, which is the
        // case that did not exist before and is the whole point of this class.
        const bool follow = audioFollowing_ && player_ && player_->isPlaying();
        const TransportTick tick = follow ? transport_.follow(player_->positionSeconds())
                                          : transport_.advance(time.deltaTime);
        if (tick.looped) {
            // A wrap is a discontinuity like any seek, and everything a seek resynchronises has to
            // be resynchronised here too -- the entities, the event scheduler, the cue state, the
            // music classifier -- or the second lap is not the first lap.
            seekSeconds(tick.positionSeconds);
        } else if (tick.reachedEnd) {
            static_cast<void>(syncAudioToTransport(false)); // the piece is over; the device stops with it
        }
        time.renderTime = transport_.positionSeconds();
        clock.seek(time.renderTime);
    }
    lastRenderTime_ = time.renderTime;
    return time;
}

void Engine::publishFrame(const analysis::AnalysisFrame& frame) {
    latest_ = frame;
    hasFrame_ = true;
    audioSignals_.publish(bus_, frame);
    // Live, this is every analysis frame the render thread sees. Offline it is the last of the
    // batch update() already walked, which consume() recognises by frame index and ignores.
    music_.consume(frame, phraseBars_, sectionPhrases_);
}

void Engine::updateTimeSignals(const FrameTime& time, bool newAnalysisFrame) {
    const auto& midiClock = controlHub_.midiClock();
    midiClockActive_ = tempoSource_ == TempoSource::MidiClock && midiClock.running() && midiClock.hasTempo();
    double bpm = hasFrame_ ? static_cast<double>(latest_.tempoBpm) : 0.0;
    bool pulse = false;
    if (midiClockActive_) {
        // The MIDI clock owns the beat clock: phase and count come straight from the tracker
        // (already extrapolated to this frame by the hub).
        bpm = midiClock.bpm();
        beatClockPhase_ = midiClock.beatPhase();
        beatClockCount_ = midiClock.beatCount();
        pulse = midiClock.beatEvent();
    } else if (bpm > 0.0) {
        // Advance the per-frame beat clock; re-sync to the analyzer whenever it reports a beat.
        beatClockPhase_ += time.deltaTime * bpm / 60.0;
        if (newAnalysisFrame && latest_.beatCount != lastAnalysisBeatCount_) {
            beatClockPhase_ = static_cast<double>(latest_.beatPhase);
            beatClockCount_ = latest_.beatCount;
            lastAnalysisBeatCount_ = latest_.beatCount;
            pulse = true;
        } else if (beatClockPhase_ >= 1.0) {
            beatClockPhase_ -= 1.0;
            ++beatClockCount_;
            pulse = true;
        }
    } else {
        beatClockPhase_ = 0.0;
    }
    const double duration = durationSeconds();
    bus_.set(timeSignals_.seconds, static_cast<float>(time.renderTime));
    bus_.set(timeSignals_.progress, duration > 0.0 ? static_cast<float>(std::clamp(positionSeconds() / duration, 0.0, 1.0)) : 0.0f);
    bus_.set(timeSignals_.playing, isPlaying() ? 1.0f : 0.0f);
    bus_.set(timeSignals_.beatPhase, static_cast<float>(beatClockPhase_));
    bus_.setEvent(timeSignals_.beatPulse, pulse, 1.0f);
    bus_.set(timeSignals_.beatCount, static_cast<float>(beatClockCount_));
    bus_.set(timeSignals_.bpm, static_cast<float>(bpm));
    bus_.set(timeSignals_.barPhase, static_cast<float>((beatClockCount_ % 4 + beatClockPhase_) / 4.0));
    {
        // Phrases and sections from the beat clock: continuous phases plus an event at each phrase
        // boundary, so a state machine can escalate over musical structure rather than per beat.
        const double beatsPerBar = 4.0;
        const double beats = static_cast<double>(beatClockCount_) + beatClockPhase_;
        const double bars = beats / beatsPerBar;
        const double phrases = bars / static_cast<double>(phraseBars_);
        const double sections = phrases / static_cast<double>(sectionPhrases_);
        const auto phraseIndex = static_cast<std::uint32_t>(phrases < 0.0 ? 0.0 : phrases);
        bus_.set(timeSignals_.phrasePhase, static_cast<float>(phrases - std::floor(phrases)));
        bus_.set(timeSignals_.phraseCount, static_cast<float>(phraseIndex));
        bus_.setEvent(timeSignals_.phrasePulse, phraseIndex != lastPhraseIndex_, 1.0f);
        lastPhraseIndex_ = phraseIndex;
        bus_.set(timeSignals_.sectionPhase, static_cast<float>(sections - std::floor(sections)));
        bus_.set(timeSignals_.sectionCount, static_cast<float>(static_cast<std::uint32_t>(sections < 0.0 ? 0.0 : sections)));
    }

    sourceContext_.time = time;
    sourceContext_.audioPosition = positionSeconds();
    sourceContext_.audioDuration = duration;
    sourceContext_.playing = isPlaying();
    sourceContext_.beatPhase = static_cast<float>(beatClockPhase_);
    sourceContext_.beatCount = beatClockCount_;
    sourceContext_.tempoBpm = static_cast<float>(bpm);
    sourceContext_.beatEvent = pulse;
}

void Engine::reportCuePresetOverrides() {
    // A cue recalls its preset into the base values the moment the playhead reaches it, so a value
    // the scene file authored and a value a cue preset names are not in competition: the preset
    // wins, from that cue onward. That precedence is right -- a cue arc is a deliberate statement
    // about time and a scene file is the starting condition -- and it was silent, which is not.
    // Hyperspace's gate plates went through a dozen material edits that did exactly nothing
    // because a preset was pinning their emission (docs/shot-hyperspace.md).
    //
    // Measured once, here, against the values in effect at load, which is exactly the question an
    // author is asking when they edit a scene file and re-run. Deduplicated across cues and
    // reported as one line, because *every* cue preset overrides something -- that is what a cue
    // is -- and a notice per cue would be a notice nobody reads. This is not a load fault, so it
    // does not go in projectWarnings(); `cuePresetOverrides()` is where the inspector reads it.
    cuePresetOverrides_.clear();
    if (!timeline_.enabled || timeline_.cues().empty()) {
        return;
    }
    std::set<std::string> paths;
    std::size_t cues = 0;
    for (const auto& cue : timeline_.cues()) {
        if (cue.preset.empty()) {
            continue;
        }
        const auto* preset = presets_.find(cue.preset);
        if (preset == nullptr) {
            continue; // applyCues() warns about this when it gets there
        }
        const auto conflicts = params::presetConflicts(params_, *preset);
        if (conflicts.empty()) {
            continue;
        }
        ++cues;
        for (const auto& conflict : conflicts) {
            paths.insert(conflict.path);
        }
    }
    if (paths.empty()) {
        return;
    }
    cuePresetOverrides_.assign(paths.begin(), paths.end());
    constexpr std::size_t kListed = 10;
    std::string listed;
    for (std::size_t i = 0; i < cuePresetOverrides_.size() && i < kListed; ++i) {
        listed += (i == 0 ? "" : ", ") + cuePresetOverrides_[i];
    }
    if (cuePresetOverrides_.size() > kListed) {
        listed += fmt::format(", and {} more", cuePresetOverrides_.size() - kListed);
    }
    log::warn("{} cue preset(s) take over {} value(s) the scene set; editing these in the scene "
              "file will not survive the first cue that names them: {}",
              cues, cuePresetOverrides_.size(), listed);
}

void Engine::setViewport(std::uint32_t width, std::uint32_t height) {
    viewportWidth_ = width;
    viewportHeight_ = height;
}

void Engine::updateTimelineClock(const FrameTime& time) {
    // The transport, unconditionally. This line used to read
    //
    //     timelineClock_.seconds = audioFile_ ? positionSeconds() : time.renderTime;
    //
    // and that ternary was the defect (ADR-102): with no audio file the timeline followed the
    // free-running render clock, so a project without audio could not be paused or seeked -- any
    // seek was overwritten on the very next frame -- and one with audio was bounded by the length of
    // the wav however long the sequence was.
    static_cast<void>(time);
    timelineClock_.seconds = transport_.positionSeconds();
    timelineClock_.beats = static_cast<double>(beatClockCount_) + beatClockPhase_;
}

void Engine::applyCues() {
    if (!timeline_.enabled) {
        return;
    }
    const auto state = timeline_.cueAt(timelineClock_);
    if (state.index != cueState_.index) {
        // A new cue took effect (forward playback, a seek, or an edit): remember where the
        // morph starts from and apply the preset from scratch.
        cueFrom_ = params::capturePreset(params_, "cue-from");
        cueApplied_ = false;
    }
    cueState_ = state;
    if (state.index < 0 || cueApplied_) {
        return;
    }
    const auto& cues = timeline_.cues();
    if (static_cast<std::size_t>(state.index) >= cues.size()) {
        return;
    }
    const auto& cue = cues[static_cast<std::size_t>(state.index)];
    if (cue.preset.empty()) {
        cueApplied_ = true; // a marker only
        return;
    }
    const auto* preset = presets_.find(cue.preset);
    if (preset == nullptr) {
        log::warn("timeline cue '{}': preset '{}' not found", cue.name, cue.preset);
        cueApplied_ = true;
        return;
    }
    if (state.progress >= 1.0f) {
        params::applyPreset(params_, *preset);
        cueApplied_ = true;
    } else {
        params::applyPresetBlend(params_, cueFrom_, *preset, state.progress);
    }
}

namespace {

// Where the world's nodes are, for a world effect resolving a `node:` source (ADR-207). An interface
// rather than a lambda so resolution allocates nothing: the engine counts allocations per frame.
class CompositionEffectScene final : public world::WorldEffectScene {
public:
    explicit CompositionEffectScene(const scene::Composition* comp) : comp_(comp) {}

    [[nodiscard]] bool nodePosition(std::string_view name, glm::vec3& out) const override {
        if (comp_ == nullptr) {
            return false;
        }
        const scene::CompositionNode* node = comp_->findNode(std::string(name));
        if (node == nullptr) {
            return false;
        }
        out = comp_->nodeWorldTransform(*node).position;
        return true;
    }

    [[nodiscard]] bool nodeForward(std::string_view name, glm::vec3& out) const override {
        if (comp_ == nullptr) {
            return false;
        }
        const scene::CompositionNode* node = comp_->findNode(std::string(name));
        if (node == nullptr) {
            return false;
        }
        // The node's -Z axis, which is the convention the rest of the engine uses for "forward".
        out = comp_->nodeWorldTransform(*node).rotation * glm::vec3(0.0f, 0.0f, -1.0f);
        return true;
    }

private:
    const scene::Composition* comp_;
};

} // namespace

// The camera's velocity, in metres per timeline second.
//
// **A finite difference in timeline seconds, never in frame deltas.** A beam pointed where the
// camera is going has to point the same way in a 30 fps offline render and in a 144 Hz window, and a
// frame-to-frame difference does not: it is `speed * deltaTime`, and deltaTime is whatever the
// machine managed. The camera is a baked `camera/position` track (ADR-075), which is a pure function
// of time, so the honest answer is to evaluate it twice a fixed step apart.
//
// Zero when the camera is not automated, which is the truthful answer for a camera that is wherever
// somebody last dragged it: there is no trajectory to read.
glm::vec3 Engine::cameraVelocityOnTimeline() const {
    constexpr double kStep = 1.0 / 60.0; // one 60 Hz frame: short enough to be local, long enough to
                                         // survive the float precision of a key value
    const params::Track* track = timeline_.findTrack("camera/position", -1);
    if (track == nullptr || !track->enabled || track->keys.size() < 2) {
        return glm::vec3(0.0f);
    }
    const double now = timelineClock_.at(track->timeBase);
    // Beats per second when the track is keyed in beats, so the result is still metres per *second*.
    const double scale = track->timeBase == params::TimeBase::Beats
                             ? (sourceContext_.tempoBpm > 1.0f ? sourceContext_.tempoBpm / 60.0 : 2.0)
                             : 1.0;
    const params::KeyValue a = track->evaluate(now - kStep * scale);
    const params::KeyValue b = track->evaluate(now);
    const glm::vec3 delta(b[0] - a[0], b[1] - a[1], b[2] - a[2]);
    return delta / static_cast<float>(kStep);
}

// Resolves this frame's world effects into the scene (ADR-207).
//
// Called from `update()` after the camera has been placed and after the modulation routes have run,
// so the numbers it reads are this frame's finals and the camera it reads is this frame's camera.
// Everything it does is a pure function of the transport second, which is what keeps an offline
// render of second N identical to a playthrough of second N.
void Engine::updateWorldEffects() {
    scene::Scene& live = controller_->scene();
    if (worldEffects_.empty()) {
        live.worldEffects = world::WorldEffectFrame{};
        return;
    }
    world::applyWorldEffectParameters(worldEffectParams_, worldEffects_);

    const CompositionEffectScene sceneAdapter(composition());
    world::WorldEffectContext ctx;
    ctx.seconds = timelineClock_.seconds;
    ctx.cameraPosition = live.camera.position;
    ctx.cameraTarget = live.camera.target;
    const glm::vec3 aim = live.camera.target - live.camera.position;
    ctx.cameraForward = glm::length(aim) > 1e-5f ? glm::normalize(aim) : glm::vec3(0.0f, 0.0f, -1.0f);
    ctx.cameraVelocity = cameraVelocityOnTimeline();
    ctx.shots = shotSpans_;
    ctx.scene = &sceneAdapter;
    if (const auto* comp = composition()) {
        ctx.heroes = comp->heroes();
    }
    world::buildWorldEffectFrame(worldEffects_, ctx, live.worldEffects);
    // Logged on the edge rather than per frame: "why is my effect not firing" is a question about
    // when it started and stopped, and a line per frame would bury the answer.
    if (live.worldEffects.count != lastWorldEffectCount_) {
        lastWorldEffectCount_ = live.worldEffects.count;
        log::debug("world effects: {} live at {:.2f}s", live.worldEffects.count, ctx.seconds);
    }
}

// The aurora's spectrum, folded from the analysis frame into `kAuroraBands` log-spaced bins.
//
// **This is the one place an effect reads audio directly, and it is deliberate.** The rule
// `world/effect_params.hpp` states -- audio reaches an effect as a modulation route, never as a hook
// -- holds for every scalar an aurora has, and `defaultAtmosphericRoutes` is what implements it. It
// cannot hold for the curtain's *shape*, because that is sixteen numbers across the sky and a route
// carries one. So the vector rides in the resolution context instead, and it is read from
// `latestFrame()` -- the same frame the signal bus, the material inputs and the camera director all
// read, published by the same code in both modes.
//
// That is what keeps it deterministic. Offline, `latest_` is `frames[cursor - 1]` of a precomputed
// `AnalysisTrack`, a pure function of the render time; live, it is the runner's newest. Neither
// integrates state here, and nothing below reads a frame counter.
//
// Log-spaced because hearing is: sixteen linear slices of a 1025-bin spectrum would put thirteen of
// them above 6 kHz, where a curtain has nothing to show.
void Engine::updateAuroraSpectrum() {
    auroraSpectrum_.fill(0.5f); // the neutral value: an ordinary curtain when there is no music
    const analysis::AnalysisFrame& f = latest_;
    if (f.magnitude.empty()) {
        return;
    }
    const float nyquist = static_cast<float>(analyzerConfig_.sampleRate) * 0.5f;
    if (!(nyquist > 0.0f)) {
        return;
    }
    constexpr float kLowHz = 40.0f;
    constexpr float kHighHz = 12000.0f;
    const float bins = static_cast<float>(f.magnitude.size());
    const float logLow = std::log(kLowHz);
    const float logSpan = std::log(std::min(kHighHz, nyquist)) - logLow;
    for (std::size_t i = 0; i < world::kAuroraBands; ++i) {
        const float t0 = static_cast<float>(i) / static_cast<float>(world::kAuroraBands);
        const float t1 = static_cast<float>(i + 1) / static_cast<float>(world::kAuroraBands);
        const float hz0 = std::exp(logLow + logSpan * t0);
        const float hz1 = std::exp(logLow + logSpan * t1);
        const auto binOf = [&](float hz) {
            return std::clamp(static_cast<std::size_t>(hz / nyquist * bins), std::size_t{0},
                              f.magnitude.size() - 1);
        };
        const std::size_t b0 = binOf(hz0);
        const std::size_t b1 = std::max(binOf(hz1), b0 + 1);
        float sum = 0.0f;
        std::size_t n = 0;
        for (std::size_t b = b0; b < b1 && b < f.magnitude.size(); ++b) {
            sum += f.magnitude[b] * f.magnitude[b];
            ++n;
        }
        // Root-mean-square over the slice, then a fixed compression. A *fixed* curve rather than a
        // running normaliser on purpose: `AnalysisFrame::bands` is already auto-gained with a 4 s
        // decay, and an aurora whose sixteen bins each had their own AGC would breathe in sixteen
        // directions and read as noise rather than as a spectrum.
        const float rms = n > 0 ? std::sqrt(sum / static_cast<float>(n)) : 0.0f;
        auroraSpectrum_[i] = std::clamp(std::sqrt(rms) * 1.9f, 0.0f, 1.0f);
    }
}

// Resolves this frame's atmospheric effects into the scene (ADR-230).
//
// Called from `update()` beside `updateWorldEffects`, for the same reasons: after the camera has
// been placed and after the modulation routes have run, so the numbers it reads are this frame's
// finals. Everything it does is a pure function of the transport second.
void Engine::updateAtmosphericEffects() {
    scene::Scene& live = controller_->scene();
    if (atmosphericEffects_.empty()) {
        live.atmospherics = world::AtmosphericFrame{};
        return;
    }
    world::applyAtmosphericParameters(atmosphericParams_, atmosphericEffects_);
    updateAuroraSpectrum();

    world::AtmosphericContext ctx;
    ctx.seconds = timelineClock_.seconds;
    ctx.cameraPosition = live.camera.position;
    ctx.shots = shotSpans_;
    ctx.spectrum = auroraSpectrum_;
    world::buildAtmosphericFrame(atmosphericEffects_, ctx, live.atmospherics);
    // The §12 quality control. Offline renders get the full march; live playback takes two thirds of
    // it, which is a difference nobody sees on a moving comet and a third of the tail's cost.
    live.atmospherics.cometSteps = mode_ == EngineMode::Offline ? 28u : 18u;

    const std::uint32_t total = live.atmospherics.cometCount + live.atmospherics.auroraCount;
    if (total != lastAtmosphericCount_) {
        lastAtmosphericCount_ = total;
        log::debug("atmospheric effects: {} comet(s), {} aurora(s) live at {:.2f}s",
                   live.atmospherics.cometCount, live.atmospherics.auroraCount, ctx.seconds);
    }
}

// ADR-216's seam, connected at last.
//
// `firedEvents()` has existed since ADR-098 and, until now, **nothing in the shipping application
// read it**. Only a test did. That was not an oversight: the comment on `firedEvents()` refuses to
// let the engine invent a meaning for an EntityAction, because a second interpretation of the event
// stream is exactly the duplication the design was avoiding.
//
// The refusal was right and the consequence was that the whole path was inert -- a section could
// generate an event, the event could fire, and nothing moved. So the meaning lives in ONE place that
// is not here (`seq::actionFromEvent`), and this function does the only thing left: hand what that
// returns to the action system. The engine still does not decide what a verb is; it decides who to
// ask, which is its job.
//
// A firing whose verb does not resolve is reported once and dropped. Once, because these repeat
// every occurrence of a section name and a per-frame log would bury the run; reported, because a
// section that was supposed to make something happen and made nothing happen is the silent failure
// this repository keeps paying for.
void Engine::applySectionActions() {
    if (firedEvents_.empty()) {
        return;
    }
    scene::Composition* composition = this->composition();
    if (composition == nullptr) {
        return;
    }
    for (const seq::FiredEvent& fired : firedEvents_) {
        if (fired.eventIndex >= sequence_.events.size()) {
            continue;
        }
        const seq::SequenceEvent& event = sequence_.events[fired.eventIndex];
        if (event.what.kind != seq::EventActionKind::EntityAction) {
            continue;   // a Notify belongs to the host, exactly as before
        }
        auto directed = seq::actionFromEvent(event.what.target, event.what.value, event.what.argument);
        if (!directed) {
            // Not necessarily a mistake. `section_performance.hpp` says the verb vocabulary belongs to
            // the Director layer, and a host that reads `firedEvents()` itself may define verbs this
            // engine has no action for -- `test_sequence_project` does exactly that with `walkTo`.
            // So this says what was skipped and why without calling it broken, once per event id,
            // and a genuine typo still surfaces instead of vanishing.
            if (sectionActionProblems_.insert(event.id).second) {
                log::info("section event '{}' not applied here ({}); a host reading firedEvents() "
                          "may own this verb",
                          event.id, directed.error().message);
            }
            continue;
        }
        // ADR-093: a seek re-delivers a standing intent rather than replaying it as news. The action
        // system is told the same thing either way -- what differs is that a restored firing is the
        // character being put back where the piece says it already is, so it must not queue behind
        // whatever it was doing before the jump.
        if (!composition->entityWorld().direct(directed->entity, {directed->action},
                                               timelineClock_.seconds)) {
            if (sectionActionProblems_.insert(event.id).second) {
                log::warn("section event '{}': nothing here is called '{}'", event.id,
                          directed->entity);
            }
        }
    }
}

void Engine::update(const FrameTime& time) {
    const auto start = std::chrono::steady_clock::now();
    bool newFrame = false;

    // ADR-186: the frame's detail policy, written before anything reads it. Applied every frame
    // rather than once at the setter, because a controller may replace its scene (a project load,
    // a reimport) and the policy is the engine's, not that scene's -- the same reason the quality
    // tier is pushed to the renderer per frame.
    if (controller_) {
        controller_->scene().detailLimits = detailLimits_;
    }

    if (mode_ == EngineMode::Offline) {
        // The offline position, taken from whatever clock produced this frame. No clamp, no loop and
        // no end rule: a render of 0..120 s against 30 s of audio renders 120 seconds, and a loop
        // set for previewing must not silently become part of an export (ADR-102).
        transport_.setMode(TransportMode::Offline);
        transport_.setOfflinePosition(time.renderTime);
    }

    if (mode_ == EngineMode::Live) {
        if (runner_ && runner_->acquire()) {
            newFrame = true;
            publishFrame(runner_->latest());
            stats_.analysisHopMicros = runner_->averageHopMicros();
            stats_.analysisFrames = runner_->framesProduced();
        } else if (hasFrame_) {
            // No new analysis this render frame: keep continuous values, drop the event pulse.
            bus_.setEvent(audioSignals_.onset, false);
        } else {
            audioSignals_.publishSilence(bus_);
        }
    } else if (track_ && !track_->empty()) {
        // Consume every analysis frame whose centre lies at or before renderTime so onsets that
        // fall between two render frames are not lost at low frame rates.
        const auto& frames = track_->frames();
        bool onset = false;
        float onsetStrength = 0.0f;
        std::size_t cursor = offlineFrameCursor_;
        const std::size_t probeCursorStart = cursor; // TEMPORARY: phase 2
        std::optional<probe2::Add> probeCatchup(std::in_place, probe2::frame().analysisCatchupMs);
        while (cursor < frames.size() && frames[cursor].timeSeconds <= time.renderTime) {
            if (frames[cursor].onset) {
                onset = true;
                onsetStrength = std::max(onsetStrength, frames[cursor].onsetStrength);
            }
            // The classifier is fed here rather than from publishFrame() below, which only ever
            // sees the last frame of the batch: at 30 fps that is one analysis frame in three, and
            // a detector that samples the music at the frame rate is a detector whose answers
            // depend on the frame rate (ADR-073).
            music_.consume(frames[cursor], phraseBars_, sectionPhrases_);
            ++cursor;
        }
        probeCatchup.reset(); // TEMPORARY: phase 2 -- stop the clock before the publish below
        probe2::frame().analysisFramesConsumed += cursor - probeCursorStart;
        if (cursor > offlineFrameCursor_) {
            analysis::AnalysisFrame frame = frames[cursor - 1];
            frame.onset = onset;
            if (onset) {
                frame.onsetStrength = onsetStrength;
            }
            publishFrame(frame);
            newFrame = true;
            offlineFrameCursor_ = cursor;
            stats_.analysisFrames = cursor;
        } else if (hasFrame_) {
            bus_.setEvent(audioSignals_.onset, false);
        } else {
            audioSignals_.publishSilence(bus_);
        }
    } else {
        audioSignals_.publishSilence(bus_);
    }

    const auto allocsNow = [] { return static_cast<std::uint32_t>(core::allocCounters().allocations); };
    const std::uint32_t allocsAtStart = allocsNow();
    std::uint32_t allocMark = allocsNow();
    // TEMPORARY (ui-responsiveness phase 2): the same stage boundaries the alloc marks already use.
    auto probeMark = std::chrono::steady_clock::now();
    const auto probeStage = [&probeMark](double& sink) {
        const auto now = std::chrono::steady_clock::now();
        sink += std::chrono::duration<double, std::milli>(now - probeMark).count();
        probeMark = now;
    };

    // Live control first: MIDI clock messages feed this frame's beat clock, transport commands
    // move the position the time signals read, parameter writes precede modulation.
    controlHub_.update(*this, time);
    if (controlSource().needsAttach()) {
        sources_.attach(bus_, params_); // new control channels: declare and rebind routes
        rebind();
    }
    probeStage(probe2::frame().updControlMs); // TEMPORARY: phase 2
    stats_.allocsControl = allocsNow() - allocMark;
    allocMark = allocsNow();
    updateTimeSignals(time, newFrame);
    music_.publish(bus_); // unconditional: no audio consumed means every music.* signal is false
    updateTimelineClock(time);
    applyCues();
    {
        BeatInfo beat;
        beat.beatPulse = bus_.event(timeSignals_.beatPulse);
        beat.barPhase = bus_.value(timeSignals_.barPhase);
        beat.phrasePulse = bus_.event(timeSignals_.phrasePulse);
        beat.sectionPhase = bus_.value(timeSignals_.sectionPhase);
        beat.onset = bus_.event(audioSignals_.onset);
        beat.onsetStrength = bus_.value(audioSignals_.onsetStrength);
        const double bpm = sourceContext_.tempoBpm > 1.0f ? static_cast<double>(sourceContext_.tempoBpm) : 120.0;
        beat.beatSeconds = 60.0 / bpm;
        beat.barSeconds = beat.beatSeconds * 4.0;
        states_.update(time.renderTime, time.deltaTime, bus_, beat, params_, presets_);
        bus_.set(stateProgressSignal_, states_.progress());
        bus_.set(stateIndexSignal_, static_cast<float>(std::max(0, states_.currentIndex())));
    }
    probeStage(probe2::frame().updSignalsMs); // TEMPORARY: phase 2
    stats_.allocsSignals = allocsNow() - allocMark;
    allocMark = allocsNow();
    if (input_ && inputGain_ != nullptr) {
        input_->setGain(inputGain_->value());
    }
    sources_.update(bus_, sourceContext_);
    params_.resetFinals();
    timeline_.apply(timelineClock_); // automation: the first modulation layer (ADR-018)
    // Spatial reactivity, between automation and the routes (ADR-097). After timeline_.apply so a
    // field that follows a baked actor reads the position that actor has *at this instant* --
    // which is what makes such a field a pure function of time, and so scrub-safe and
    // offline-exact (ADR-091). Before applyRoutes because a field's entire output is a gain on a
    // route's depth: run it after and every reaction in the scene is one frame behind its field.
    controller_->updateFields(time, bus_, modulator_);
    modulator_.applyRoutes(bus_, params_, time.deltaTime);
    // Autonomous behaviour, after the routes and before the scene reads the finals (ADR-088): a
    // behaviour's own knobs have been modulated by now, and the offsets it writes land on top of
    // whatever the routes wrote, so a route and a behaviour compose on one property.
    controller_->updateBehaviour(time, bus_);
    if (viewportHeight_ > 0) {
        if (auto* comp = composition()) {
            comp->setViewport(viewportWidth_, viewportHeight_);
        }
    }
    // The one part of a sequence a track cannot carry: which clip each actor is in, and the second
    // its phase started from (ADR-089). After updateBehaviour, so a sequence that says what a
    // character is doing wins over a behaviour that guessed; before controller_->update(), which is
    // what poses the rigs. Pure in the clock, so a scrub lands the same pose as a play-through.
    if (auto* comp = composition(); comp != nullptr && !sequence_.actors.empty()) {
        seq::applyAnimation(sequence_, sequenceReport_.events.clips, *comp, timelineClock_.seconds);
    }
    // The two tiers of the event system a track cannot carry (ADR-098). `advanceTo` decides for
    // itself whether the playhead stepped or jumped; the drain is per frame so nothing accumulates
    // when no host is listening.
    sequenceEvents_.advanceTo(timelineClock_.seconds);
    firedEvents_ = sequenceEvents_.drain(timelineClock_.seconds);
    applySectionActions();
    probeStage(probe2::frame().updModulationMs); // TEMPORARY: phase 2
    stats_.allocsModulation = allocsNow() - allocMark;
    allocMark = allocsNow();
    controller_->update(time);
    probeStage(probe2::frame().updControllerMs); // TEMPORARY: phase 2
    stats_.allocsController = allocsNow() - allocMark;
    scene::applyPostParameters(postParams_, post_);
    // ---- physical camera (ADR-037) ---------------------------------------------------------
    // After controller_->update() has placed the camera: the lens, the focus tracker's new
    // distance and the exposure block go onto the camera and into the post chain, which applies
    // exposure before bloom (ADR-039). With the defaults the exposure scale is exactly 1 and the
    // lens does not touch the field of view, so scenes authored before this render unchanged.
    scene::applyCameraParameters(cameraParams_, lens_, exposure_, focus_);
    // ADR-245: a camera's optical identity. When the camera that has the frame states a focal
    // length, the frame is on that lens -- which is what makes "Valley Wide is a 24 mm camera" true
    // of the depth of field and the circle of confusion and not only of the field of view. A camera
    // with no opinion (the default, and every camera in every project written before this) leaves
    // the lens exactly as the `camera/lens/*` parameters set it.
    {
        const scene::ActiveCameraState active = activeCamera();
        if (active.focalLength > 0.0f) {
            lens_.focalLength = active.focalLength;
            lens_.useExplicitFov = false;
        }
        if (active.focusDistance > 0.0f) {
            lens_.focusDistance = active.focusDistance;
        }
    }
    {
        scene::Scene& live = controller_->scene();
        live.camera.lens = lens_;
        live.camera.exposure = exposure_;
        const float deltaSeconds = static_cast<float>(time.deltaTime);
        const float target =
            scene::focusTargetDistance(focus_, live.camera, live.composition, lens_.focusDistance);
        const float tracked = scene::updateFocus(focusState_, target, deltaSeconds, focus_.speed);
        live.camera.lens.focusDistance = tracked;
        post_.lens = live.camera.lens;
        if (post_.dofPhysical || focus_.mode != scene::FocusSettings::Mode::Fixed) {
            post_.focusDistance = tracked;
        }
        post_.exposure = exposure_;
        post_.exposureDeltaSeconds = deltaSeconds;
        post_.exposureReset = cameraStateReset_;
        cameraStateReset_ = false;
        // ADR-040: the shutter now scales the blur inside the post chain (blur length is the
        // screen motion times shutterAngle / 360), so `post_.lens` above is all it needs and
        // post/motionBlur/amount stays exactly as authored.
    }
    controller_->scene().post = post_;
    updateWorldEffects();
    updateAtmosphericEffects();
    {
        shaders::StdUniforms base;
        const auto& f = latest_;
        base.audio[0] = hasFrame_ ? f.rms : 0.0f;
        base.audio[1] = hasFrame_ ? f.bands[0] : 0.0f;
        base.audio[2] = hasFrame_ ? f.bands[2] : 0.0f;
        base.audio[3] = hasFrame_ ? f.bands[4] : 0.0f;
        base.audio2[0] = hasFrame_ ? f.bands[1] : 0.0f;
        base.audio2[1] = hasFrame_ ? f.bands[3] : 0.0f;
        base.audio2[2] = hasFrame_ ? std::min(1.0f, f.onsetStrength / 2.0f) : 0.0f;
        base.audio2[3] = static_cast<float>(beatClockPhase_);
        base.beat[0] = sourceContext_.tempoBpm;
        base.beat[1] = static_cast<float>(beatClockCount_);
        base.beat[2] = bus_.value(timeSignals_.barPhase);
        base.beat[3] = bus_.value(timeSignals_.progress);
        shaderLayers_.update(time, base);
    }
    bus_.clearEvents();

    probeStage(probe2::frame().updOtherMs); // TEMPORARY: phase 2
    stats_.allocsOther = (allocsNow() - allocsAtStart) - stats_.allocsControl - stats_.allocsSignals -
                         stats_.allocsModulation - stats_.allocsController;
    // T4 for the interaction log -- but not while a seek is still outstanding. The scene this
    // update derived is the scene at the *evaluated* second, and a deferred request has not reached
    // that second yet. Marking presentation here regardless would file every deferred drag frame as
    // a twenty-millisecond interaction, which is the single most flattering lie available to a
    // deferral and exactly what this instrument exists to refuse.
    // Note what this implies and do not paper over it: a UI edit happens inside `ui.build`, which
    // runs after this, so the evaluated consequence of an edit is always one frame later than the
    // edit. That one frame is real and belongs in `input->final visual`.
    if (!seekPending_) {
        core::interactions().markPresentation();
    }
    const auto end = std::chrono::steady_clock::now();
    const double micros = std::chrono::duration<double, std::micro>(end - start).count();
    stats_.modulationMicros = stats_.modulationMicros * 0.9 + micros * 0.1;
}

params::ModRoute* Engine::routeForTarget(const std::string& path) {
    for (auto& route : modulator_.routes()) {
        if (route.target == path) {
            return &route;
        }
    }
    return nullptr;
}

} // namespace avgen::app
