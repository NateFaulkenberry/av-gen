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
#include "core/vortex.hpp"
#include "core/wind.hpp"
#include "params/serialization.hpp"
#include "world/effects/distortion_frame.hpp"
#include "world/effects/effect_params.hpp"
#include "world/effects/effect_registry.hpp"
#include "world/effects/particle_emitter.hpp"
#include "world/effects/entity_fx.hpp"
#include "world/effects/history_bank.hpp"
#include "world/effects/ribbon_frame.hpp"
#include "world/effects/star_field.hpp"
#include "world/effects/transform_frame.hpp"

#include <fmt/ranges.h>

#include <nlohmann/json.hpp>

#include <fstream>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstring>
#include <chrono>

namespace avgen::app {

namespace {

// ADR-870: the deterministic signals, declared first on every bus that carries them and always in
// this order, so the engine's bus and a seek replay's bus give each the same id -- which is what
// lets the replay's `MusicRuntime` (it holds its ids) and its values be handed back to the engine.
void declareFrameSignals(signals::SignalBus& bus, signals::AudioSignals& audio, Engine::TimeSignals& time,
                         MusicRuntime& music) {
    audio = signals::AudioSignals::declare(bus);
    time.seconds = bus.declare("time.seconds", 0.0f, 3600.0f);
    time.progress = bus.declare("time.progress");
    time.playing = bus.declare("time.playing");
    time.beatPhase = bus.declare("beat.phase");
    time.beatPulse = bus.declare("beat.pulse", 0.0f, 1.0f, true);
    time.beatCount = bus.declare("beat.count", 0.0f, 100000.0f);
    time.bpm = bus.declare("beat.bpm", 0.0f, 300.0f);
    time.barPhase = bus.declare("beat.bar");
    time.phrasePhase = bus.declare("beat.phrase");
    time.phraseCount = bus.declare("beat.phraseCount", 0.0f, 100000.0f);
    time.phrasePulse = bus.declare("beat.phrasePulse", 0.0f, 1.0f, true);
    time.sectionPhase = bus.declare("beat.section");
    time.sectionCount = bus.declare("beat.sectionCount", 0.0f, 100000.0f);
    music.declare(bus); // music.beat ... music.impact (ADR-073)
}

} // namespace

// ---- ADR-870: the signal bus a seek replays -------------------------------------------------
//
// A play builds its bus once per frame in `Engine::update`: the offline analysis frames up to the
// frame's instant, the beat clock, the music classifier. Offline, with an analysed track, all of it
// is a pure function of the frame sequence, so a replay stepping the same instants on its own copy
// of the pipeline sees the bus the play saw. This is that copy: its own `SignalClock` and its own
// bus, driven by the engine's own `consumeAnalysis` and `advanceClock`.
//
// Not replayed, and not on this bus: control sources and OSC/MIDI (`sources_`, `controlHub_`), the
// scene states (`state.*`) and the entity-derived signals -- the first two are not functions of
// time, the last is the entity world's own output. The replay's bodies read them as absent, as
// every replay before this read everything.
class Engine::ReplaySignals final : public entity::ReplaySignalSource {
public:
    explicit ReplaySignals(const Engine& engine) : engine_(engine) {
        signals::AudioSignals audio;
        Engine::TimeSignals time;
        declareFrameSignals(zeroBus_, audio, time, zero_.music);
        bus_ = zeroBus_;
    }

    // What this seek's frames read that the pipeline does not carry, fixed for the seek.
    void begin(bool playing, double duration) {
        playing_ = playing;
        duration_ = duration;
    }

    [[nodiscard]] const signals::SignalBus& bus() const override { return bus_; }

    void reset() override {
        state_ = zero_;
        bus_ = zeroBus_;
        started_ = false;
        exact_ = true;
        lastBuilt_ = -1.0;
    }

    void build(double now, double dt) override {
        if (!started_) {
            // A replay that did not begin at zero (the window, `SeekMode::Window`) consumed the
            // whole track in its first frame: not the pipeline a play had.
            started_ = true;
            exact_ = now == 0.0;
        }
        bus_.clearEvents(); // the end of the previous frame, where `Engine::update` clears them
        const FrameTime time{now, dt, 0};
        const bool fresh = engine_.consumeAnalysis(state_, bus_, now);
        ClockInputs in;
        in.bpm = engine_.resolvedTempo(state_, false).second;
        in.position = now; // the offline transport sits at the frame's instant
        in.duration = duration_;
        in.playing = playing_;
        engine_.advanceClock(state_, bus_, time, fresh, in);
        lastBuilt_ = now;
    }

    struct Checkpoint final : entity::HostCheckpoint {
        SignalClock clock;
        std::vector<float> values; // the bus: continuous values persist between analysis frames
        std::size_t measured = 0;
        [[nodiscard]] std::size_t bytes() const override { return measured; }
    };

    [[nodiscard]] std::shared_ptr<const entity::HostCheckpoint> capture() const override {
        auto c = std::make_shared<Checkpoint>();
        c->clock = state_;
        c->values.resize(bus_.size());
        for (std::size_t i = 0; i < bus_.size(); ++i) {
            c->values[i] = bus_.value(static_cast<signals::SignalId>(i));
        }
        c->measured = sizeof(Checkpoint) + c->values.size() * sizeof(float) +
                      (c->clock.latest.magnitude.size() + c->clock.latest.spectrum.size()) * sizeof(float);
        return c;
    }

    void restore(const entity::HostCheckpoint& checkpoint) override {
        const auto& c = static_cast<const Checkpoint&>(checkpoint);
        state_ = c.clock;
        for (std::size_t i = 0; i < c.values.size() && i < bus_.size(); ++i) {
            bus_.set(static_cast<signals::SignalId>(i), c.values[i]);
        }
        started_ = true;
        exact_ = true; // only an exact replay records checkpoints
    }

    [[nodiscard]] std::uint64_t inputKey() const override { return engine_.replaySignalKey(playing_); }

    // Whether the pipeline now stands where a play from zero stands after its frame at `target`.
    [[nodiscard]] bool exactAt(double target) const {
        return started_ && exact_ && std::abs(lastBuilt_ - target) <= 1e-9;
    }

    // Without an entity replay to ride on (no composition, or a replay that was not exact): the
    // pipeline alone, on the grid `EntityWorld::seekExact` steps -- k / 60, dt = 0 at zero, and one
    // short step to a target between two instants.
    void runTo(double target) {
        reset();
        constexpr double kRate = 60.0;
        const double exactSteps = std::max(target, 0.0) * kRate;
        auto last = static_cast<std::uint64_t>(std::llround(exactSteps));
        const bool onGrid = std::abs(exactSteps - static_cast<double>(last)) <= 1e-6;
        if (!onGrid) {
            last = static_cast<std::uint64_t>(std::floor(exactSteps));
        }
        for (std::uint64_t k = 0; k <= last; ++k) {
            build(static_cast<double>(k) / kRate, k == 0 ? 0.0 : 1.0 / kRate);
        }
        if (!onGrid) {
            build(target, target - static_cast<double>(last) / kRate);
        }
        lastBuilt_ = target;
    }

    [[nodiscard]] const SignalClock& state() const { return state_; }

private:
    const Engine& engine_;
    SignalClock zero_;
    signals::SignalBus zeroBus_;
    SignalClock state_;
    signals::SignalBus bus_;
    bool playing_ = false;
    double duration_ = 0.0;
    bool started_ = false;
    bool exact_ = true;
    double lastBuilt_ = -1.0;
};

bool Engine::seekReplaysSignals() const {
    return mode_ == EngineMode::Offline && track_ && !track_->empty();
}

std::uint64_t Engine::replaySignalKey(bool playing) const {
    std::uint64_t h = 0x51a7c0ffee5eedull;
    const auto mix = [&h](std::uint64_t v) { h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2); };
    const auto bits = [](double d) {
        std::uint64_t u = 0;
        std::memcpy(&u, &d, sizeof u);
        return u;
    };
    mix(audioRevision_); // bumped by every install of audio, which is every new track
    mix(track_ ? track_->frames().size() : 0u);
    mix(tempoOverride_.available ? 1u : 0u);
    mix(bits(tempoOverride_.available ? tempoOverride_.bpm : 0.0));
    mix(embeddedTempo_.available ? 1u : 0u);
    mix(bits(embeddedTempo_.available ? embeddedTempo_.bpm : 0.0));
    mix(bits(durationSeconds()));
    mix(static_cast<std::uint64_t>(phraseBars_));
    mix(static_cast<std::uint64_t>(sectionPhrases_));
    mix(playing ? 1u : 0u);
    return h;
}

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
    // First on the bus, and in the one order a seek replay's bus also uses (ADR-870).
    declareFrameSignals(bus_, audioSignals_, timeSignals_, clock_.music);
    stateProgressSignal_ = bus_.declare("state.progress");
    stateIndexSignal_ = bus_.declare("state.index", 0.0f, 64.0f);
    sources_.attach(bus_, params_);
    postParams_ = scene::registerPostParameters(params_, post_);
    temporalParams_ = scene::registerTemporalParameters(params_, temporal_);
    cameraParams_ = scene::registerCameraParameters(params_, lens_, exposure_, focus_);
    installController(std::make_unique<scene::OrbScene>(params_, modulator_));
    if (mode_ == EngineMode::Live) {
        player_ = std::make_unique<audio::AudioPlayer>();
    }
}

// ---- control source, tempo and the scene controller -------------------------------------------

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
    clock_.beatPhase = 0.0;
    clock_.lastAnalysisBeatCount = 0;
    log::info("tempo source: {}", tempoSourceName(source));
}

void Engine::installController(std::unique_ptr<scene::SceneController> controller) {
    controller_ = std::move(controller);
    ++sceneGeneration_;
    // ADR-825: an offline engine waits for a baked motion database rather than rendering the frames
    // a load happened to take with the body on its clips. A live one does not wait.
    if (auto* comp = dynamic_cast<scene::Composition*>(controller_.get()); comp != nullptr) {
        comp->setBlockingMotionLoads(mode_ == EngineMode::Offline);
    }
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
        // ADR-700 retired the window: a seek now resumes from the nearest checkpoint, and this
        // ceiling bounds the replay *from* it -- in practice only a first scrub, before the
        // checkpoints exist, can reach it, and past it the seek falls back to the window and
        // reports itself inexact. The default rose to `SeekBudget::kEditorBodySteps` (400,000)
        // so that first scrub of the Glowmere multicam, 257,982 body-steps to its last frame, is
        // exact. The history of the number, for the record:
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
            // ADR-700: raised with the window's retirement; see `SeekBudget::kEditorBodySteps`.
            seekBodyStepBudget_ = entity::SeekBudget::kEditorBodySteps;
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
    if (params_.find("temporal/echo/enabled") == nullptr) {
        const scene::TemporalSettings keep = temporal_;
        temporalParams_ = scene::registerTemporalParameters(params_, keep);
    }
    if (params_.find("camera/lens/focalLength") == nullptr) {
        const scene::LensSettings keepLens = lens_;
        const scene::ExposureSettings keepExposure = exposure_;
        const scene::FocusSettings keepFocus = focus_;
        cameraParams_ = scene::registerCameraParameters(params_, keepLens, keepExposure, keepFocus);
    }
    // ADR-702: the scene's effects -- every owner's, one list -- and the `fx/<id>/...` parameters
    // that make every number on them automatable, keyable and modulatable. The authored set is
    // copied off the composition rather than read through it per frame, because the live set is
    // what modulation writes to and a composition's authored values must survive being modulated.
    //
    // Unregistered first and unconditionally: a scene swap replaces the cast of effects, and a
    // registrar that only ever adds leaves the previous scene's paths behind for a route to bind to.
    timeline_.unbind();
    world::unregisterEffectParameters(params_, effectParams_);
    effects_.clear();
    if (const auto* comp = composition()) {
        effects_ = comp->effects();
    }
    installEffects();
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
    // ADR-758: an actor on a node an entity drives is a scripted performance. It moves the ENTITY
    // (a director motion for its span) instead of baking tracks onto the node, where they would be
    // summed with the entity's own travel and pin the node for the whole film.
    // Which entity each performer actor holds, found before the bake (which must skip their tracks)
    // and turned into performers after it (ADR-820: the clip schedule the install resolves decides
    // where the sequencer owns the rig).
    std::vector<std::pair<const seq::Actor*, std::string>> performing;
    if (scene::Composition* comp = composition()) {
        for (const seq::Actor& actor : sequence_.actors) {
            const std::string node = actor.nodeName();
            const auto desc = std::find_if(comp->entities().begin(), comp->entities().end(), [&](const entity::EntityDesc& d) {
                return (d.node.empty() ? d.name : d.node) == node;
            });
            if (desc == comp->entities().end()) {
                continue;
            }
            options.performerNodes.push_back(node);
            performing.emplace_back(&actor, desc->name);
        }
    }
    auto report = seq::install(sequence_, timeline_, params_, sink, sequenceTargets_, options);
    if (scene::Composition* comp = composition()) {
        std::vector<scene::Composition::Performer> performers;
        if (report) {
            for (const auto& [actor, entityName] : performing) {
                if (auto performer = seq::performerFor(*actor, entityName, options.groundHeightAt,
                                                       report->events.clips,
                                                       seq::clipLookupFor(*comp, actor->nodeName()))) {
                    performers.push_back(std::move(*performer));
                }
            }
        }
        comp->setPerformers(std::move(performers));
    }
    if (!report) {
        // The install left the timeline consistent (old tracks gone) even when the bake failed, so
        // forget the targets: there is nothing left for the next install to erase.
        sequenceTargets_.clear();
        sequenceReport_ = seq::InstallReport{};
        sequenceEvents_.clear();
        firedEvents_.clear();
        directedEvents_.clear();
        if (scene::Composition* comp = composition()) {
            comp->setDirectives({}); // ADR-824: nothing installed, nothing scheduled
        }
        return report;
    }
    sequenceTargets_ = report->targets;
    sequenceReport_ = *report;
    installDirectives(); // ADR-824
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

// ADR-824: every scheduled section action whose verb this engine knows and whose subject is an entity
// here becomes a composition directive -- applied at its second on a play and in a seek's replay --
// and leaves `applySectionActions`, which would otherwise give the same order again. Verbs a host
// owns, and subjects nothing here is called, stay with `firedEvents()` exactly as before.
void Engine::installDirectives() {
    directedEvents_.clear();
    scene::Composition* comp = composition();
    if (comp == nullptr) {
        return;
    }
    std::vector<scene::Composition::Directive> directives;
    for (const seq::Firing& firing : sequenceReport_.events.dispatches) {
        if (firing.eventIndex >= sequence_.events.size()) {
            continue;
        }
        const seq::SequenceEvent& event = sequence_.events[firing.eventIndex];
        scene::Composition::Directive d;
        d.timeSeconds = firing.timeSeconds;
        if (event.what.kind == seq::EventActionKind::CharacterGoal) {
            // ADR-828 (F7): a goal for the character's `goal` considerer, from this second.
            if (event.what.target.empty() || comp->entityWorld().find(event.what.target) == nullptr) {
                continue;
            }
            d.entity = event.what.target;
            d.goal = true;
            d.goalSpec.subject = event.what.value;
            d.goalSpec.affordance = event.what.argument;
            d.goalSpec.until = event.what.seconds;
            d.goalSpec.intent = event.what.goal.intent;
            d.goalSpec.activity = event.what.goal.activity;
            d.goalSpec.approach = event.what.goal.approach;
            d.goalSpec.dwell = event.what.goal.dwell;
        } else if (event.what.kind == seq::EventActionKind::EntityAction) {
            auto directed = seq::actionFromEvent(event.what.target, event.what.value, event.what.argument);
            if (!directed || comp->entityWorld().find(directed->entity) == nullptr) {
                continue;
            }
            d.entity = directed->entity;
            d.release = directed->release;
            d.goal = directed->goal;
            d.goalSpec.subject = directed->goalSubject;
            d.goalSpec.affordance = directed->goalAffordance;
            if (!directed->release && !directed->goal) {
                d.actions.push_back(directed->action);
            }
        } else {
            continue;
        }
        std::uint64_t h = 1469598103934665603ULL;
        for (const char c : event.toJson().dump() + fmt::format("|{}|{}", firing.timeSeconds, firing.eventIndex)) {
            h ^= static_cast<unsigned char>(c);
            h *= 1099511628211ULL;
        }
        d.signature = h;
        directives.push_back(std::move(d));
        directedEvents_.insert(firing.eventIndex);
    }
    comp->setDirectives(std::move(directives));
}

// ADR-828 (Phase D §26): a character's named completions -- an action's `onComplete`, a goal's
// `goal.arrived` and `goal.done` -- posted to the sequence's live triggers as `ActionComplete`, the
// event's name as the trigger's name and the entity as its subject. So "when Rook arrives" is a
// sequence event `{when: actionComplete, name: "goal.arrived", subject: "rook"}`, and the Director's
// `rook.goal.arrived` is that pair. Nothing posted before this: `ActionComplete` had no producer.
//
// Posted from the step's own record (`EntityWorld::actionEvents`), at the simulation second the
// action completed. The step raises the same events in a seek's replay (as world events), so a
// recording that samples a play and a replay of the same film sees the same names at the same times;
// the dispatcher itself follows ADR-098 on a seek (a live event is not re-fired by a scrub).
void Engine::postCharacterEvents() {
    const scene::Composition* comp = composition();
    if (comp == nullptr) {
        return;
    }
    const entity::EntityWorld& world = comp->entityWorld();
    for (const entity::ActionEvent& e : world.actionEvents()) {
        if (e.result != entity::ActionResult::Completed) {
            continue;
        }
        if (!e.event.empty()) {
            sequenceEvents_.post(seq::TriggerSignal{seq::TriggerKind::ActionComplete, e.event, e.entity, e.time});
        }
        // ADR-832: an interaction says it finished by its own name, "prop.verb", with nothing
        // authored on the action -- "when Rook has sat on the stump" is `{interactionComplete,
        // "stump.sit", "rook"}`.
        if (!e.interaction.empty()) {
            sequenceEvents_.post(
                seq::TriggerSignal{seq::TriggerKind::InteractionComplete, e.interaction, e.entity, e.time});
        }
    }
    // ADR-832: the field pass's edges, by the field's name and the entity's. `TriggerEvent` was
    // recorded for "whoever wants edges" and nobody read it until now.
    const auto& fields = world.fields();
    const auto& entities = world.entities();
    for (const entity::TriggerEvent& t : world.triggerEvents()) {
        if (t.field >= fields.size() || t.entity >= entities.size()) {
            continue;
        }
        sequenceEvents_.post(seq::TriggerSignal{t.enter ? seq::TriggerKind::VolumeEnter : seq::TriggerKind::VolumeExit,
                                                fields[t.field].name, entities[t.entity]->name(), t.time});
    }
}

void Engine::clearSequence() {
    if (scene::Composition* comp = composition()) {
        comp->setPerformers({}); // ADR-758: no sequence, no performances
        comp->setDirectives({}); // ADR-824: and no scheduled orders
    }
    directedEvents_.clear();
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

// ---- cameras, layers and the routes a scene gets for free -------------------------------------

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

std::size_t Engine::addDefaultEffectRoutes(std::string_view effectId) {
    // The type decides the routes, so an effect that is not there has none to add.
    const std::size_t at = world::findEffect(effects_, effectId);
    if (at == effects_.size()) {
        return 0;
    }
    const std::string prefix = world::effectParameterPrefix(effectId);
    for (const params::ModRoute& r : modulator_.routes()) {
        if (r.target.starts_with(prefix)) {
            return 0; // already automated; leave whatever somebody set up alone
        }
    }
    // ADR-703: `owner.` sources resolved against the instance's owner. An owner that cannot give
    // one of them a meaning (a World-owned type whose route reads its owner's speed) refuses the
    // whole set by name rather than installing a route that binds to nothing.
    auto resolved = world::defaultEffectRoutes(effects_[at]);
    if (!resolved) {
        log::warn("{}", resolved.error().message);
        noteBindingProblem(resolved.error().message);
        return 0;
    }
    std::size_t added = 0;
    for (params::ModRoute& r : *resolved) {
        if (params_.find(r.target) == nullptr) {
            log::warn("default route for effect '{}' targets '{}', which is not a parameter; skipped",
                      effectId, r.target);
            continue;
        }
        modulator_.addRoute(std::move(r));
        ++added;
    }
    if (added > 0) {
        rebind(); // the routes hold pointers into the parameter set, and bind is what fills them
        log::debug("attached {} default audio route(s) to effect '{}'", added, effectId);
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

// ---- binding, effects and the parameter surface -----------------------------------------------

void Engine::rebind() {
    // ADR-703: before the bind, so an `entity.<name>.*` source a route names is a declared signal
    // by the time the modulator resolves it.
    refreshHistorySubscriptions();
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

// ADR-702. The one entry point that changes which effects exist.
Result<void> Engine::setEffects(std::vector<world::EffectInstance> effects) {
    // Stored in stack order whatever order the caller built it in, then validated before anything
    // is touched, so a refusal leaves the engine exactly as it was.
    world::normaliseEffectOrder(effects);
    if (auto ok = world::validateEffects(effects); !ok) {
        return ok;
    }
    // The composition owns the authored set (it is what a save writes); the engine owns the live
    // one. Writing both here is what stops the panel and the file drifting apart.
    if (auto* comp = composition()) {
        if (auto ok = comp->setEffects(effects); !ok) {
            return ok;
        }
    }
    // The parameter set changes shape -- effects appear and disappear -- so the timeline has to let
    // go of its pointers before the old paths are removed, and rebind afterwards.
    timeline_.unbind();
    world::unregisterEffectParameters(params_, effectParams_);
    effects_ = std::move(effects);
    installEffects();
    reportedDeadFields_.clear();
    rebind();
    return {};
}

void Engine::installEffects() {
    if (!effects_.empty()) {
        effectParams_ = world::registerEffectParameters(params_, effects_);
    }
    world::effectEvaluationOrder(effects_, effectOrder_);
    effectStatus_.assign(effects_.size(), world::EffectStatus::Dormant);
    effectStatusReason_.assign(effects_.size(), std::string());
}

std::vector<world::EffectInstance> Engine::capturedEffects() const {
    std::vector<world::EffectInstance> authored;
    if (const auto* comp = composition()) {
        authored = comp->effects();
    } else {
        authored = effects_;
    }
    world::captureEffectParameters(effectParams_, authored);
    return authored;
}

Result<void> Engine::editEffects(const std::function<Result<void>(std::vector<world::EffectInstance>&)>& edit) {
    std::vector<world::EffectInstance> next = capturedEffects();
    if (auto ok = edit(next); !ok) {
        return ok;
    }
    return setEffects(std::move(next));
}

std::string_view Engine::effectStatusReason(std::string_view id) const {
    const std::size_t at = world::findEffect(effects_, id);
    return at < effectStatusReason_.size() ? std::string_view(effectStatusReason_[at]) : std::string_view();
}

world::EffectStatus Engine::effectStatus(std::string_view id) const {
    const std::size_t at = world::findEffect(effects_, id);
    return at < effectStatus_.size() ? effectStatus_[at] : world::EffectStatus::Dormant;
}

bool Engine::effectOwnerExists(const world::EffectOwner& owner) const {
    switch (owner.kind) {
    case world::EffectTarget::World:
    case world::EffectTarget::Camera: return true;
    case world::EffectTarget::Entity: {
        const auto* comp = composition();
        if (comp == nullptr) {
            return false;
        }
        for (const world::HeroPoint& h : comp->heroes()) {
            if (h.name == owner.name) {
                return true;
            }
        }
        return comp->findNode(owner.name) != nullptr;
    }
    case world::EffectTarget::Light: {
        const auto* comp = composition();
        if (comp == nullptr) {
            return false;
        }
        // A light owner is named by the light's stable id (ADR-278), the same identity its
        // `lights/<id>/...` parameters use.
        for (const auto& light : comp->authoredLights()) {
            if (scene::Composition::authoredLightId(light) == owner.name) {
                return true;
            }
        }
        return false;
    }
    }
    return false;
}

Result<std::size_t> Engine::renameEffectOwner(const world::EffectOwner& from, const world::EffectOwner& to) {
    // The routes are edited on a copy and only kept if the effect edit is, so a refused rename
    // leaves both halves as they were.
    std::vector<params::ModRoute> routes = modulator_.routes();
    std::size_t moved = 0;
    auto ok = editEffects([&](std::vector<world::EffectInstance>& list) -> Result<void> {
        moved = world::renameEffectOwner(list, routes, from, to);
        return {};
    });
    if (!ok) {
        return std::unexpected(ok.error());
    }
    modulator_.routes() = std::move(routes);
    rebind(); // the copies' parameter pointers predate the re-registration; bind refills them
    return moved;
}

void Engine::detachSceneParameters() {
    if (auto* comp = composition()) {
        comp->detach();
    }
    shaderLayers_.detach();
    layers_.detach();
    timeline_.unbind();
    // ADR-702. After `timeline_.unbind()`, because a track aimed at an `fx/...` path holds a pointer
    // into the parameter that is about to go.
    world::unregisterEffectParameters(params_, effectParams_);
}

params::Track* Engine::recordKey(const std::string& path, int component, params::KeyInterp interp,
                                 params::TimeBase base) {
    auto* track = timeline_.recordKey(params_, path, component, timelineClock_.at(base), interp, base);
    if (track == nullptr) {
        log::warn("timeline: cannot key unknown parameter '{}'", path);
    }
    return track;
}

// ---- sources, presets, states and world macros ------------------------------------------------

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

// ADR-158's aim-follow entry, in the one spelling both `cameraAimFollow` and ADR-582's parked copy
// use. One writer and one reader for both keys, so a field added to one cannot be dropped by the
// other (docs/testing.md #38: a fact that lives in a copied region lives in N copies).
nlohmann::json aimFollowToJson(std::span<const scene::AimFollow> follow) {
    nlohmann::json shots = nlohmann::json::array();
    for (const scene::AimFollow& shot : follow) {
        shots.push_back(nlohmann::json{{"start", shot.startSeconds},
                                       {"end", shot.endSeconds},
                                       {"hero", shot.hero},
                                       {"heroAtCut", {shot.heroAtCut.x, shot.heroAtCut.y, shot.heroAtCut.z}}});
    }
    return shots;
}

std::vector<scene::AimFollow> aimFollowFromJson(const nlohmann::json& array, const char* key,
                                                const std::function<void(std::string)>& warn) {
    std::vector<scene::AimFollow> shots;
    if (!array.is_array()) {
        return shots;
    }
    for (const auto& entry : array) {
        if (!entry.is_object() || !entry.contains("heroAtCut") || !entry["heroAtCut"].is_array() ||
            entry["heroAtCut"].size() != 3) {
            warn(fmt::format("{}: a shot without a 'heroAtCut' position was skipped", key));
            continue;
        }
        scene::AimFollow shot;
        shot.startSeconds = entry.value("start", 0.0);
        shot.endSeconds = entry.value("end", 0.0);
        shot.hero = entry.value("hero", std::string{});
        const auto& at = entry["heroAtCut"];
        shot.heroAtCut = glm::vec3(at[0].get<float>(), at[1].get<float>(), at[2].get<float>());
        if (shot.hero.empty() || !(shot.endSeconds > shot.startSeconds)) {
            warn(fmt::format("{}: a shot with no hero or no duration was skipped", key));
            continue;
        }
        shots.push_back(std::move(shot));
    }
    return shots;
}

// ADR-582's `parkedDirector` block. The tracks go through the timeline's own writer and reader and
// the shots through `CameraShot`'s, so a parked track or shot is literally the same document a live
// one is and cannot drift from it in what it supports.
nlohmann::json parkedCutToJson(const ParkedDirectorsCut& cut) {
    nlohmann::json out = nlohmann::json::object();
    params::Timeline scratch;
    scratch.tracks() = cut.tracks;
    out["tracks"] = scratch.toJson()["tracks"];
    out["aimFollow"] = aimFollowToJson(cut.aimFollow);
    nlohmann::json shots = nlohmann::json::array();
    for (const scene::CameraShot& shot : cut.cameraShots) {
        shots.push_back(shot.toJson());
    }
    out["cameraShots"] = std::move(shots);
    return out;
}

Result<ParkedDirectorsCut> parkedCutFromJson(const nlohmann::json& doc,
                                             const std::function<void(std::string)>& warn) {
    if (!doc.is_object()) {
        return fail("'parkedDirector' must be a JSON object");
    }
    ParkedDirectorsCut cut;
    if (const auto tracks = doc.find("tracks"); tracks != doc.end()) {
        params::Timeline scratch;
        if (auto ok = scratch.fromJson(nlohmann::json{{"tracks", *tracks}}); !ok) {
            return fail("parkedDirector: {}", ok.error().message);
        }
        cut.tracks = std::move(scratch.tracks());
    }
    if (const auto follow = doc.find("aimFollow"); follow != doc.end()) {
        cut.aimFollow = aimFollowFromJson(*follow, "parkedDirector.aimFollow", warn);
    }
    if (const auto shots = doc.find("cameraShots"); shots != doc.end()) {
        if (!shots->is_array()) {
            return fail("parkedDirector.cameraShots must be an array");
        }
        for (const auto& entry : *shots) {
            auto shot = scene::CameraShot::fromJson(entry);
            if (!shot) {
                return fail("parkedDirector.cameraShots: {}", shot.error().message);
            }
            cut.cameraShots.push_back(std::move(*shot));
        }
    }
    return cut;
}

} // namespace

// ---- the project document -- what a save writes, and what it compares against -----------------
//
// `projectDocument` is the serialiser; `saveProject` below writes what it returns, and the
// unsaved-changes check at ADR-440 compares against it. One function, so a save and a dirty
// check cannot disagree about what the project is.

nlohmann::json Engine::projectDocument(const std::filesystem::path& path) {
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
    // ADR-755: the Director Plans this project's content came from, as its provenance. Written only
    // when there is one, so a project that never used the Director is byte-for-byte what it was.
    if (!directingPlans_.empty() || !unreadableDirectingPlans_.empty()) {
        nlohmann::json plans = nlohmann::json::array();
        for (const directing::Plan& plan : directingPlans_) {
            plans.push_back(plan.toJson());
        }
        for (const nlohmann::json& raw : unreadableDirectingPlans_) {
            plans.push_back(raw);
        }
        doc["directingPlans"] = std::move(plans);
    }
    // ADR-158: which hero each directed shot was cut for, beside the tracks it accompanies.
    //
    // A sibling of `timeline` rather than part of the scene, because that is what it belongs to: the
    // shots are camera automation, and a scene shared between two projects must not carry one
    // project's cut. Written only when there is a cut, so a project that was never directed keeps
    // the file it had.
    if (const auto* comp = composition(); comp != nullptr && !comp->aimFollow().empty()) {
        doc["cameraAimFollow"] = aimFollowToJson(comp->aimFollow());
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
    // ADR-582: a cut whose owner took the camera back is parked, not gone. The spans above are
    // written whether the director is steering or parked -- they are the film's focus schedule and
    // effects follow them either way -- and this block holds the half that steers the camera, so
    // "Resume director" in the next session puts back exactly what was there.
    //
    // Written only when something is parked, so a project that was never directed, or one whose
    // director is steering, keeps the file it had; its presence is the parked marker.
    if (!parkedCut_.empty()) {
        doc["parkedDirector"] = parkedCutToJson(parkedCut_);
    }
    // ADR-207 and ADR-230, on the argument `cameraShotSpans` above makes, for the two families it
    // did not cover -- and this time it is not a derived cut but the effects themselves.
    //
    // The effects belong to the `Composition`, and a project whose scene came from a file saves that
    // scene **by reference**: `assets.scene.path` plus a hash of the bytes already on disk. Nothing
    // writes the scene file. So an aurora added through the (since removed) World Effects panel lived in the composition the window was drawing and in no document any render
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
    // which already carries the `effects` array. A second copy beside it would be a second answer.
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
        // ADR-702: one list, every owner's, under one key.
        nlohmann::json liveEffects = nlohmann::json::array();
        for (const world::EffectInstance& effect : comp->effects()) {
            liveEffects.push_back(effect.toJson());
        }
        if (liveEffects != onDisk("effects", [](const nlohmann::json& j) {
                return world::EffectInstance::fromJson(j);
            })) {
            doc["effects"] = std::move(liveEffects);
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
        // `authoredHeroes()`, not `heroes()`. The live list follows each hero's node *final*, so a
        // hero on a moving body walks with it; writing that is what made every save record the
        // walk. The note further down recording `heroes (the key appears once the herd has
        // walked)` as expected churn was this defect being logged as noise rather than diagnosed.
        nlohmann::json liveHeroes = nlohmann::json::array();
        for (const world::HeroPoint& hero : comp->authoredHeroes()) {
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
        // harder. `effects` and `heroes` are small lists the project can
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
        // `effects`/`heroes`' family, which is small lists the project can simply hold.
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
        // The camera collection and the camera track, and this is the same defect a **sixth** time
        // (ADR-751). `Engine::setCameraDirection` -- the Cameras panel's "+ Camera", "Delete" and
        // "Cut to this camera", and every camera the Director compiles -- writes the composition;
        // the composition is saved by reference; so a camera or a cut lived in the window and in no
        // document a render reads. Measured before this: 2 cameras and 1 shot in the session, 1 and
        // 0 after a save and a reload (`test_director_persistence.cpp`).
        //
        // The whole collection, in `lights`' family rather than ADR-330's difference-by-name: a
        // rig's placement, aim node and lens are not parameters, so the project owes what the rigs
        // *are*, not only which exist. **Authored shots only.** `Directed` shots are Song Mode's
        // (ADR-249) and are regenerated at load from `autoDirector` and `songPlan`, which the
        // project already saves; writing them would photograph the run and make every directed
        // project look edited. Compared after a round trip through the same parser, so an untouched
        // project writes nothing and stays byte-stable.
        const auto authoredOnly = [](scene::CameraDirection direction) {
            std::erase_if(direction.shots, [](const scene::CameraShot& shot) {
                return shot.origin == scene::CameraShot::Origin::Directed;
            });
            return direction.toJson();
        };
        nlohmann::json fileCameras;
        if (!sceneDoc.is_object() || !sceneDoc.contains("cameraDirection")) {
            scene::CameraDirection none;
            none.ensureMainCamera();
            fileCameras = authoredOnly(std::move(none));
        } else if (auto parsed = scene::CameraDirection::fromJson(sceneDoc["cameraDirection"]); parsed) {
            parsed->ensureMainCamera();
            fileCameras = authoredOnly(std::move(*parsed));
        }
        if (nlohmann::json liveCameras = authoredOnly(comp->cameraDirection()); liveCameras != fileCameras) {
            doc["cameraDirection"] = std::move(liveCameras);
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
    // The artist's tempo, and only the artist's (ADR-394). A detected tempo is not written --
    // re-analysing produces it again, and persisting it would make a measurement look like a
    // decision. An embedded tempo is not written either: it lives in the audio file, and writing a
    // stale copy here would outlive the file being replaced. What must survive a save is the one
    // thing nothing can recompute: that a person chose this number.
    if (tempoOverride_.available) {
        doc["transport"]["tempo"] = {{"bpm", tempoOverride_.bpm},
                                     {"source", audio::tempoProvenanceToken(tempoOverride_.source)}};
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
    return doc;
}

Result<void> Engine::writeProjectCopy(const std::filesystem::path& path) {
    const nlohmann::json doc = projectDocument(path);
    std::ofstream out(path);
    if (!out) {
        return fail("cannot write '{}'", path.string());
    }
    out << doc.dump(2) << '\n';
    if (!out) {
        return fail("could not finish writing '{}'", path.string());
    }
    return {};
}

Result<void> Engine::saveProject(const std::filesystem::path& path) {
    nlohmann::json doc = projectDocument(path);
    std::ofstream out(path);
    if (!out) {
        return fail("cannot write '{}'", path.string());
    }
    out << doc.dump(2) << '\n';
    projectPath_ = path;
    // What is now on disk, in the shape the comparison uses. `doc` is the bytes that were just
    // written, so this cannot disagree with them (ADR-440).
    projectBaseline_ = std::move(doc);
    projectBaselineValid_ = true;
    projectDirty_ = false;
    return {};
}

// ---- unsaved changes (ADR-440) ---------------------------------------------------------------
//
// **Why there is no `bool dirty_` set by every mutation.** This codebase has a documented family of
// defects where a hand-maintained list drifts from the thing it describes -- a visitor that skipped
// seven struct members, an unregister table missing two fields, a test harness copying a uniform
// block field by field. A dirty flag is the same shape, and its failure is silent in the direction
// that loses work: every new mutation site is one more chance to forget it. The honest basis is
// that a project is dirty when serialising it now differs from what a save would have written the
// last time it agreed with the disk.
//
// **Why that is not a comparison against the file.** Measured, on 2026-09-20, by loading eight
// example projects, changing nothing, and diffing an immediate save against the file it came from:
//
//     glowmere-valley-2        347 differing JSON paths   610 KB -> 618 KB
//     glowmere-valley-2-multicam  279                     675 KB -> 689 KB
//     temple                   801                        6.5 KB -> 60 KB
//     hero                    1069                        1.3 KB -> 67 KB
//     chamber                 1576                        3.1 KB -> 113 KB
//     fungi                   1584                        0.8 KB -> 92 KB
//     lab                     1879                        3.4 KB -> 121 KB
//     night-shift            19798                        33 KB -> 1.6 MB
//
// Not one project round-trips. A hand-written file records the handful of parameters somebody
// changed; a save writes every parameter the live session has registered, which is why night-shift
// grows forty-nine times. A prompt built on "differs from the file" fires on every project, every
// time, and a prompt that always fires trains the reflex that dismisses it.
//
// **So the baseline is a serialisation, not the file.** It is taken at the end of `loadProject` and
// again in `saveProject`, from the same builder the save uses, so both sides of the comparison are
// the same function of the same engine and the load's own lossiness cancels out. What is being
// measured is "has anything changed since this project was opened or saved", which is the question
// the dialog asks.
//
// **The one thing that still moves on its own**, from the same measurement, playing 600 frames
// (20 s) with no input at all and saving again from the same engine:
//
//     glowmere-valley-2-multicam   18 paths: heroes[0,2,3,4,5,6]/position[0..2]
//                                   2 paths: parameters/particles/visitor-beam/{spawnRate,emissive}
//     glowmere-valley-2             1 path:  heroes  (the key appears once the herd has walked)
//     the other six                 0 paths
//
// That is ADR-386's per-frame parameter writeback and the entity simulation, and it is the whole of
// it -- twenty paths across eight projects. `sampleDirty` absorbs it **by measuring it rather than
// naming it**: on a sample where the host reports that nothing touched the application, every
// difference is by definition the engine's own, so the baseline simply moves. No list of noisy keys
// exists to drift out of date, which is the property the flag could not have.
//
// And it is monotone: once dirty, dirty until a save or a load. Otherwise a quiet second after an
// edit would absorb the edit, which is the flag's failure mode reintroduced by the back door.
void Engine::sampleProjectDirty(bool touchedSinceLastSample) {
    if (projectDirty_ || !projectBaselineValid_) {
        return;
    }
    nlohmann::json doc = projectDocument(projectPath_);
    if (doc == projectBaseline_) {
        return;
    }
    if (touchedSinceLastSample) {
        projectDirty_ = true;
    } else {
        projectBaseline_ = std::move(doc);
    }
}

bool Engine::projectDirty(bool touchedSinceLastSample) {
    sampleProjectDirty(touchedSinceLastSample);
    return projectDirty_;
}

void Engine::markProjectSaved() {
    projectBaseline_ = projectDocument(projectPath_);
    projectBaselineValid_ = true;
    projectDirty_ = false;
}

// ---- loading a project, and starting a new one ------------------------------------------------

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
    // ---- the session's effects, over the ones its scene authors ----
    //
    // Here, and for the reason the 2D composition below is here: the parameter block a few lines
    // down carries `fx/<id>/...` values, and `setEffects` is what *registers* those paths. Applied after the parameters, an
    // effect would arrive with every number back at its default and the project's own values would
    // already have been refused as unknown -- which is exactly what the 94 warnings this fix was
    // found by say.
    //
    // Over rather than instead of: the scene file is still the state that runs first (ADR-264), and
    // an absent key changes nothing. A present one is the session's answer, including an empty array,
    // which is how a deleted effect stays deleted.
    // ADR-276: the session's heroes, over the ones its scene authors. **Before** the effects, and
    // not by taste: an effect may be attached to a hero or name one as its source, and `Composition::setHeroes`
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
    // The session's cameras and authored camera track (ADR-751, the sixth of ADR-207's family; see
    // the save). **Before `params::loadProject`**, for the lights' reason: `setCameraDirection` is
    // what registers `cameras/<slug>/...`, and a rig's position and aim arrive in `parameters`.
    // Song Mode's `Directed` shots are not in the key -- they are regenerated -- so any the scene
    // file carries are kept rather than replaced.
    if (const auto entry = doc.find("cameraDirection"); entry != doc.end() && entry->is_object()) {
        auto direction = scene::CameraDirection::fromJson(*entry);
        if (!direction) {
            warn("cameraDirection: " + direction.error().message);
        } else if (auto* comp = composition(); comp != nullptr) {
            for (const scene::CameraShot& shot : comp->cameraDirection().shots) {
                if (shot.origin == scene::CameraShot::Origin::Directed) {
                    direction->shots.push_back(shot);
                }
            }
            if (auto ok = comp->setCameraDirection(std::move(*direction)); !ok) {
                warn("cameraDirection: " + ok.error().message);
            }
        }
    }
    // ADR-702: the session's effects -- every owner's, one list -- over the ones its scene authors.
    // After the heroes, because an effect can be attached to one. The pre-ADR-702 keys are named
    // rather than read (ADR-441: every tracked project was converted in the repository).
    for (const char* legacy : {"worldEffects", "atmosphericEffects"}) {
        if (doc.contains(legacy)) {
            warn(fmt::format("'{}' is the pre-ADR-702 effect format and was ignored; effects are one "
                             "'effects' array now (tools/migrate_effects.py converts a project)",
                             legacy));
        }
    }
    if (const auto entry = doc.find("effects"); entry != doc.end() && entry->is_array()) {
        std::vector<world::EffectInstance> effects;
        effects.reserve(entry->size());
        bool readable = true;
        for (std::size_t i = 0; i < entry->size(); ++i) {
            auto one = world::EffectInstance::fromJson((*entry)[i]);
            if (!one) {
                // Named, and the whole block refused rather than the member skipped: an effect that
                // quietly failed to load looks exactly like one nobody declared.
                warn(fmt::format("effects[{}]: {}", i, one.error().message));
                readable = false;
                break;
            }
            effects.push_back(std::move(*one));
        }
        if (readable) {
            if (auto ok = setEffects(std::move(effects)); !ok) {
                warn("effects: " + ok.error().message);
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
    // ADR-755. Cleared when absent, like the song plan: a project must not inherit another's
    // provenance. A plan this build cannot read is a warning and is kept verbatim, because dropping
    // it here would make the next save delete it -- the defect family this program exists to avoid.
    directingPlans_.clear();
    unreadableDirectingPlans_.clear();
    if (const auto plans = doc.find("directingPlans"); plans != doc.end() && plans->is_array()) {
        for (std::size_t i = 0; i < plans->size(); ++i) {
            directing::PlanParse parsed = directing::parsePlan((*plans)[i]);
            if (parsed.plan) {
                directingPlans_.push_back(std::move(*parsed.plan));
                continue;
            }
            unreadableDirectingPlans_.push_back((*plans)[i]);
            const auto firstError = std::find_if(parsed.issues.begin(), parsed.issues.end(), [](const directing::Issue& issue) {
                return issue.severity == directing::Severity::Error;
            });
            warn(fmt::format("directingPlans[{}]: kept but not read: {}", i,
                             firstError != parsed.issues.end() ? firstError->message : std::string("unreadable")));
        }
    }
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
        if (const auto follow = doc.find("cameraAimFollow"); follow != doc.end()) {
            shots = aimFollowFromJson(*follow, "cameraAimFollow", warn);
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
    // ADR-582, and cleared when absent for the same reason: a project with nothing parked must not
    // inherit the last one's parked cut, or "Resume director" would restore another film's camera.
    // A project saved before this block existed has none, and opens exactly as it always did.
    parkedCut_ = ParkedDirectorsCut{};
    if (const auto parked = doc.find("parkedDirector"); parked != doc.end()) {
        auto cut = parkedCutFromJson(*parked, warn);
        if (!cut) {
            return std::unexpected(cut.error());
        }
        parkedCut_ = std::move(*cut);
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
    // Same rule as the loop: a project with no tempo block gets none, rather than inheriting the
    // override from whatever was open before. A tempo from another piece quietly governing this
    // one is exactly the defect the reset exists to prevent.
    tempoOverride_ = {};
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
        if (block.contains("tempo") && block["tempo"].is_object()) {
            const auto& tempo = block["tempo"];
            const double bpm = tempo.value("bpm", 0.0);
            // Provenance round-trips with the value. A file that somehow records a non-user
            // provenance here is not honoured: only a decision is persisted, so anything else
            // would be a measurement masquerading as one.
            const auto source = audio::tempoProvenanceFromToken(tempo.value("source", "user"));
            if (source == audio::TempoProvenance::UserOverride && bpm >= audio::kMinPlausibleBpm &&
                bpm <= audio::kMaxPlausibleBpm) {
                tempoOverride_ = audio::AudioTempo{.available = true,
                                                   .bpm = bpm,
                                                   .source = audio::TempoProvenance::UserOverride,
                                                   .confidence = 1.0};
            } else if (bpm != 0.0) {
                log::warn("project tempo {:g} ({}) ignored", bpm, tempo.value("source", "user"));
            }
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
    // The baseline for "has anything changed since this was opened" (ADR-440). Taken here, at the
    // end of the load and from the same builder a save uses, rather than from `doc` -- the document
    // that was read is not the document this engine would write, by between 279 and 19,798 JSON
    // paths depending on the project, and a baseline taken from it would report every project dirty
    // the instant it opened. The comment on `sampleProjectDirty` has the measurement.
    markProjectSaved();
    log::info("project '{}' loaded: {} parameters, {} routes, {} sources, {} presets, {} timeline tracks, {} cues, {} warning(s)",
              path.filename().string(), params_.size(), modulator_.routes().size(), sources_.sources().size(),
              presets_.presets().size(), timeline_.tracks().size(), timeline_.cues().size(), projectWarnings_.size());
    return {};
}

void Engine::newProject() {
    directingPlans_.clear();
    unreadableDirectingPlans_.clear();
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
    // A new project is clean: there is nothing in it yet to lose (ADR-440). Last, so the baseline
    // photographs the engine this function has finished resetting rather than the one it started
    // with -- taken at the top it would make File > New produce an immediately-dirty project.
    markProjectSaved();
}

// ---- referenced files and bundle export -------------------------------------------------------

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
    return bundleInto(dir / "project.json", dir / "assets");
}

// The one copier. `exportBundle` writes a folder with a fixed layout; Save As writes a project file
// the person named, with its copies beside it. They differ only in where the two outputs land, and
// a second implementation of "copy every asset and rewrite every reference" is how one of them
// quietly stops covering a reference the other handles -- which is exactly the defect this is being
// written to fix, one level up.
Result<void> Engine::bundleInto(const std::filesystem::path& projectFile,
                                const std::filesystem::path& assetsDir) {
    std::error_code ec;
    const auto dir = projectFile.parent_path();
    std::filesystem::create_directories(dir, ec);
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
        // The light rig is an asset path in the environment block, and it was missed here exactly
        // as it was missed in `saveComposition`: two copiers, the same omission, found twice. A
        // scene bundled without it opens in the new location with no lighting.
        if (doc.contains("environment") && doc["environment"].is_object() &&
            doc["environment"].contains("lightRig") && doc["environment"]["lightRig"].is_string()) {
            auto copied = copyFile(resolveFrom(doc["environment"]["lightRig"].get<std::string>(), srcDir));
            if (!copied) {
                return std::unexpected(copied.error());
            }
            doc["environment"]["lightRig"] = copied->filename().generic_string();
        }
        // Entity profiles. `visitor` in the Tree of Life scenes names one, and a bundle without it
        // reports "cannot open entity profile" on open -- which is what caught this.
        if (doc.contains("entities") && doc["entities"].is_array()) {
            for (auto& entity : doc["entities"]) {
                if (!entity.is_object() || !entity.contains("profile") || !entity["profile"].is_string()) {
                    continue;
                }
                auto copied = copyFile(resolveFrom(entity["profile"].get<std::string>(), srcDir));
                if (!copied) {
                    return std::unexpected(copied.error());
                }
                entity["profile"] = copied->filename().generic_string();
            }
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
        // EVERY `asset` in the document, wherever it sits. Enumerating the places a mesh can be
        // named was wrong three times running: `node.asset` was handled, `procedural.source.asset`
        // was not (the saucer), and `scatter[].asset` was not either -- eighteen references, every
        // tree, rock and plant in the Tree of Life. Each gap was found only by fixing the one above
        // it, which is the signature of a list that will go stale again the next time somebody adds
        // a place to name a mesh. So this walks the document instead: any string under a key called
        // `asset` is an asset, and a new home for one is covered the day it is invented.
        std::function<Result<void>(nlohmann::json&)> copyEveryAsset;
        copyEveryAsset = [&](nlohmann::json& value) -> Result<void> {
            if (value.is_array()) {
                for (auto& item : value) {
                    if (auto r = copyEveryAsset(item); !r) {
                        return r;
                    }
                }
                return {};
            }
            if (!value.is_object()) {
                return {};
            }
            for (auto& [key, child] : value.items()) {
                if (key == "asset" && child.is_string()) {
                    const auto source = resolveFrom(child.get<std::string>(), srcDir);
                    // A nested scene is bundled as a scene, not copied as an opaque file.
                    std::string ext = source.extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    Result<std::filesystem::path> copied =
                        ext == ".json" ? bundleScene(source, depth + 1) : copyFile(source);
                    if (!copied) {
                        return std::unexpected(copied.error());
                    }
                    child = copied->filename().generic_string();
                    continue;
                }
                if (auto r = copyEveryAsset(child); !r) {
                    return r;
                }
            }
            return {};
        };
        if (doc.contains("nodes")) {
            if (auto r = copyEveryAsset(doc["nodes"]); !r) {
                return std::unexpected(r.error());
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
    log::info("bundle written to '{}': {} file(s) beside '{}'", assetsDir.string(), placed.size(),
              projectFile.filename().string());
    return {};
}

// Save As. The project and its own copies of everything it names, so opening it later cannot reach
// back into the folder it was saved from -- and, more to the point, so EDITING it later cannot write
// back over a scene another project is still using. That was the reported defect: a project saved to
// the Desktop kept pointing at `examples/world/...scene.json`, and saving the scene through it
// overwrote the shared original.
Result<void> Engine::saveProjectAsCopy(const std::filesystem::path& path) {
    auto file = path;
    if (file.extension().empty()) {
        file.replace_extension(".json");
    }
    // Per project rather than a shared `assets/`: two projects saved into one folder would otherwise
    // copy over each other's files by name, which is the same defect this exists to remove.
    const auto assetsDir = file.parent_path() / (file.stem().string() + "_assets");
    if (auto r = bundleInto(file, assetsDir); !r) {
        return r;
    }
    // The session now belongs to the copy. Without this the editor would still be holding the paths
    // it was opened with, and the next scene save would land on the original -- the copy would be a
    // snapshot rather than a move, and the reported defect would survive its own fix.
    return loadProject(file);
}

// ---- scenes, compositions and the environment -------------------------------------------------

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
    // ADR-582: the director's aim-follow table is camera automation, not scene content -- it is
    // saved beside `timeline`, not in the scene document -- so it survives a composition being
    // replaced exactly as the camera tracks and the shot spans it belongs with do. Without this an
    // assistant rollback (`setCompositionJson`) silently emptied it, the next save wrote the loss,
    // and the cut came back with its keys and without the follow. A project load still replaces it
    // from the document afterwards, cleared when absent.
    std::vector<scene::AimFollow> keepFollow;
    if (const scene::Composition* was = this->composition()) {
        keepFollow = was->aimFollow();
    }
    comp->setAimFollow(std::move(keepFollow));
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
    // The light rig is an asset path in the same environment block and was the one thing here that
    // did not move with the file. Saving `glowmere-valley-2-multicam` to the Desktop wrote
    // `../lightrigs/glowmere-valley.rig.json` unchanged -- still relative to `examples/world` --
    // so it resolved to `~/lightrigs/...` and the scene opened without its lighting.
    const auto rigPath = comp->lightRigPath();
    if (!rigPath.empty()) {
        comp->rebaseLightRigPath(registry_.relativise(registry_.resolve(rigPath)));
    }
    if (auto r = comp->saveFile(path); !r) {
        return r;
    }
    if (!environmentPath_.empty()) {
        comp->setEnvironmentMap(environmentPath_);
    }
    if (!rigPath.empty()) {
        comp->rebaseLightRigPath(rigPath); // the live session keeps the path it was loaded with
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

// ---- audio ------------------------------------------------------------------------------------

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
        // The tempo the audio brought with it goes when the audio does. Left standing it would be a
        // tempo attributed to a file that is no longer loaded -- and, worse, one that would keep
        // outranking the analyzer for whatever was loaded next. The override is NOT cleared here:
        // that belongs to the project, not to the audio.
        embeddedTempo_ = {};
        clock_.analysisCursor = 0;
        clock_.hasFrame = false;
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
    // The beat grid, seeded but not skipped (ADR-394).
    //
    // An embedded BPM is a number, not a grid: it has no beat phase, no downbeat and no bar line
    // in it. Skipping this pass because a tag supplied a number would leave the BeatTracker, the
    // Auto-director, the sequencer and every beat-driven route with nothing to consume -- and no
    // test in the feature would notice. So the pass always runs.
    //
    // What the tag buys is not time -- the cost here is the onset/STFT pass, which every other
    // audio-reactive signal needs anyway and which no tag can replace. It buys *accuracy*: the
    // tempogram's log-Gaussian prior is centred on the known BPM and narrowed, which removes the
    // half/double-tempo octave error the default prior at 120 exists to mitigate and cannot always
    // resolve. The search still runs; it is simply told where to look.
    analysis::BeatTrackerConfig beatConfig;
    if (embeddedTempo_.available) {
        beatConfig.preferredBpm = static_cast<float>(embeddedTempo_.bpm);
        beatConfig.priorWidthOctaves = audio::kSeededPriorWidthOctaves;
        log::info("beat tracking seeded from embedded tempo: {:g} bpm (prior width {:g} octaves)",
                  embeddedTempo_.bpm, audio::kSeededPriorWidthOctaves);
    }
    track_ = std::make_shared<analysis::AnalysisTrack>(
        analysis::AnalysisTrack::analyze(*file, analyzerConfig_, beatConfig));
    clock_.analysisCursor = 0;
    log::info("analyzed {:.2f} s of audio: {} frames", file->durationSeconds(), track_->frames().size());
    audioFile_ = std::move(file);
    ++audioRevision_;
    modulator_.resetState();
    clock_.music.reset();
    clock_.hasFrame = false;
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
    // Embedded Tempo of the arrangement, captured before the mix is installed -- the mixdown has
    // no container to read, so this is the last point it exists (ADR-394). Set before
    // `installAudio` because the whole-track analysis it runs is seeded from it.
    embeddedTempo_ = audio::arrangementEmbeddedTempo(audioClips_, clipSources_);
    if (embeddedTempo_.available && tempoOverride_.available) {
        // Precedence, stated where it would otherwise be violated: importing a file with a BPM in
        // it must not silently replace a tempo the artist typed.
        log::info("embedded tempo {:g} noted; tempo override {:g} kept", embeddedTempo_.bpm,
                  tempoOverride_.bpm);
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

// ---- transport --------------------------------------------------------------------------------

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
    // ADR-870: offline, with an analysed track, the signal pipeline is replayed to the target with
    // the entities and the live one continues from the replay's state -- the state a play from zero
    // has there. Otherwise (live, or no audio) it is reset, as it always was.
    const bool replaySignals = seekReplaysSignals();
    if (track_ && !replaySignals) {
        // The offline analysis cursor walks forward through the frames, so a backwards seek has to
        // rewind it or every frame between here and where it had got to is skipped. Rewound rather
        // than reset to zero: a forward seek keeps its place.
        clock_.analysisCursor = 0;
    }
    seconds = target;
    modulator_.resetState();
    sources_.reset();
    if (!replaySignals) {
        // A seek discontinuity in the energy history reads as a drop; the detector must not carry
        // the old piece across it.
        clock_.music.reset();
        clock_.beatPhase = 0.0;
        clock_.lastAnalysisBeatCount = 0;
    }
    ReplaySignals* signalReplay = nullptr;
    if (replaySignals) {
        if (!replaySignals_) {
            replaySignals_ = std::make_unique<ReplaySignals>(*this);
        }
        replaySignals_->begin(isPlaying(), durationSeconds());
        signalReplay = replaySignals_.get();
    }
    cueState_ = {};   // cues re-sync from the new position on the next frame
    cueApplied_ = false;
    // ADR-398. `PostSettings::exposureReset` has documented itself as "(scene change, timeline
    // seek)" since it was written, and until this line only the scene change ever set it:
    // `resetCameraState` had exactly one caller, the composition install. So a scrub reset the
    // renderer's temporal history and left the METER on its pre-seek reading, and the first frames
    // after a jump from a night interior to a noon exterior were exposed for the interior and
    // walked out of it over the meter's adaptation time. The focus tracker behind the same call is
    // the same story one lens along.
    //
    // A seek is `TimelineStep::Jump` (core/pre_roll.hpp) and this is what a jump costs: the state
    // that was a function of the frames you came from is not a function of the frame you arrived
    // at. Re-rendering the same frame is NOT this case and must not take it -- a repeat has to
    // reproduce, which is why this sits in `seekSeconds` and not in the per-frame update.
    resetCameraState();
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
        //
        // ADR-671 (the owner's ruling, 2026-09-21): and then *replays* it with the entities, so a
        // scrub -- and a render that starts mid-film -- lands the craft, the animals it lifts and
        // every character that perceives them exactly where a play from zero puts them. The
        // reset is inside `seekWithDirector`; what ADR-209 called "picks up again on the next
        // frame" is now "is where it would have been".
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
        {
            const probe2::Add probeDirector(probe2::frame().directorResetMs); // TEMPORARY: phase 2
            // ADR-700: from the nearest simulation checkpoint, exact at any second. The window
            // (`AVGEN_SEEK_MODE=window`) and the whole-history replay (`=full`) stay reachable as
            // A/B arms out of one binary.
            static const entity::SeekMode seekMode = entity::SeekBudget::modeFromEnvironment();
            // ADR-703: what the replay re-applies to HIST is part of what its checkpoints were
            // taken under, so an edited transform track drops them.
            if (historyAutomation_ != nullptr) {
                historyBank_.setAutomationKey(historyAutomation_->key());
            }
            composition->seekWithDirector(seconds, params_,
                                          entity::SeekBudget{.maxSeconds = 90.0,
                                                             .maxBodySteps = seekBodyStepBudget_,
                                                             .mode = seekMode},
                                          1.0 / 60.0, signalReplay);
        }
        // Skinning has its own "a frame ago", and a seek makes that sentence false: the joints were
        // not anywhere a frame ago. Left alone, the first frame after every scrub carries joint
        // motion vectors for a jump nobody made and the character smears. Told here rather than
        // collapsed here, because the rigs have not been re-posed at this point -- see
        // `SkinnedRig::reseedPrevious`.
        for (scene::SkinnedRig& rig : composition->scene().rigs) {
            rig.reseedAfterDiscontinuity();
        }
    }
    // ADR-870: the live pipeline continues from where a play from zero stands after its frame at
    // the target -- the replay's state when that replay was exact, the pipeline run alone when it
    // was not (no composition, or the window). The bus's continuous values come with it; its
    // events do not, since a play clears them at the end of every frame.
    if (signalReplay != nullptr) {
        if (!signalReplay->exactAt(seconds)) {
            signalReplay->runTo(seconds);
        }
        clock_ = signalReplay->state();
        const signals::SignalBus& replayed = signalReplay->bus();
        for (std::size_t i = 0; i < replayed.size() && i < bus_.size(); ++i) {
            const auto id = static_cast<signals::SignalId>(i);
            if (replayed.info(id).isEvent) {
                bus_.setEvent(id, false);
            } else {
                bus_.set(id, replayed.value(id));
            }
        }
        stats_.analysisFrames = clock_.analysisCursor;
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

std::pair<audio::TempoProvenance, double> Engine::resolvedTempo() const {
    return resolvedTempo(clock_, midiClockActive_);
}

std::pair<audio::TempoProvenance, double> Engine::resolvedTempo(const SignalClock& clock, bool midiActive) const {
    // The precedence chain, and the only copy of it. Highest first; each arm returns immediately,
    // so a lower source can never overwrite a higher one. That is the whole point -- an import
    // must not silently replace a tempo the artist typed, and making it structural beats making it
    // a rule somebody has to remember.
    if (tempoOverride_.available) {
        return {audio::TempoProvenance::UserOverride, tempoOverride_.bpm};
    }
    if (midiActive) {
        if (const double bpm = controlHub_.midiClock().bpm(); bpm > 0.0) {
            return {audio::TempoProvenance::ExternalClock, bpm};
        }
    }
    if (embeddedTempo_.available) {
        return {audio::TempoProvenance::EmbeddedMetadata, embeddedTempo_.bpm};
    }
    if (clock.hasFrame && clock.latest.tempoBpm > 0.0f) {
        return {audio::TempoProvenance::Detected, static_cast<double>(clock.latest.tempoBpm)};
    }
    return {audio::TempoProvenance::None, 0.0};
}

audio::AudioTempo Engine::tempo() const {
    // `resolvedTempo()` decides; this only dresses the answer with the diagnostics belonging to
    // whichever source won. Not a second precedence chain -- there is exactly one.
    const auto [source, bpm] = resolvedTempo();
    switch (source) {
    case audio::TempoProvenance::UserOverride:
        return tempoOverride_;
    case audio::TempoProvenance::EmbeddedMetadata:
        return embeddedTempo_;
    case audio::TempoProvenance::ExternalClock:
        return audio::AudioTempo{
            .available = true, .bpm = bpm, .source = source, .confidence = 1.0};
    case audio::TempoProvenance::Detected:
        return audio::AudioTempo{.available = true,
                                 .bpm = bpm,
                                 .source = source,
                                 .confidence = static_cast<double>(clock_.latest.tempoConfidence)};
    case audio::TempoProvenance::None:
        break;
    }
    return {};
}

void Engine::setTempoOverride(double bpm) {
    if (!(bpm >= audio::kMinPlausibleBpm && bpm <= audio::kMaxPlausibleBpm)) {
        log::warn("tempo override {:g} ignored: outside {:g}..{:g} bpm", bpm, audio::kMinPlausibleBpm,
                  audio::kMaxPlausibleBpm);
        return;
    }
    tempoOverride_ = audio::AudioTempo{.available = true,
                                       .bpm = bpm,
                                       .source = audio::TempoProvenance::UserOverride,
                                       .confidence = 1.0};
    log::info("tempo override set to {:g} bpm", bpm);
    refreshTransport();
}

void Engine::clearTempoOverride() {
    if (!tempoOverride_.available) {
        return;
    }
    tempoOverride_ = {};
    log::info("tempo override cleared; tempo returns to {}",
              audio::tempoProvenanceName(tempo().source));
    refreshTransport();
}

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
    transport_.setTempo(resolvedTempo().second, 4);
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
    clock_.music.reset();
    clock_.hasFrame = false;
    clock_.beatPhase = 0.0;
    clock_.beatCount = 0;
    clock_.lastAnalysisBeatCount = 0;
    log::info("live audio input '{}' at {} Hz", input_->deviceName(), input_->sampleRate());
    return {};
}

void Engine::stopAudioInput() {
    if (!input_) {
        return;
    }
    runner_.reset();
    input_.reset();
    clock_.music.reset();
    clock_.hasFrame = false;
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

void Engine::publishFrame(SignalClock& clock, signals::SignalBus& bus, const analysis::AnalysisFrame& frame) const {
    clock.latest = frame;
    clock.hasFrame = true;
    audioSignals_.publish(bus, frame);
    // Live, this is every analysis frame the render thread sees. Offline it is the last of the
    // batch update() already walked, which consume() recognises by frame index and ignores.
    clock.music.consume(frame, phraseBars_, sectionPhrases_);
}

bool Engine::consumeAnalysis(SignalClock& clock, signals::SignalBus& bus, double renderTime) const {
    if (!track_ || track_->empty()) {
        audioSignals_.publishSilence(bus);
        return false;
    }
    // Consume every analysis frame whose centre lies at or before renderTime so onsets that
    // fall between two render frames are not lost at low frame rates.
    const auto& frames = track_->frames();
    bool onset = false;
    float onsetStrength = 0.0f;
    std::size_t cursor = clock.analysisCursor;
    while (cursor < frames.size() && frames[cursor].timeSeconds <= renderTime) {
        if (frames[cursor].onset) {
            onset = true;
            onsetStrength = std::max(onsetStrength, frames[cursor].onsetStrength);
        }
        // The classifier is fed here rather than from publishFrame() below, which only ever
        // sees the last frame of the batch: at 30 fps that is one analysis frame in three, and
        // a detector that samples the music at the frame rate is a detector whose answers
        // depend on the frame rate (ADR-073).
        clock.music.consume(frames[cursor], phraseBars_, sectionPhrases_);
        ++cursor;
    }
    if (cursor > clock.analysisCursor) {
        // Assigned rather than copied into a temporary, so a replay's thousands of frames reuse the
        // spectrum's capacity instead of allocating it each time (ADR-870).
        clock.latest = frames[cursor - 1];
        clock.latest.onset = onset;
        if (onset) {
            clock.latest.onsetStrength = onsetStrength;
        }
        clock.hasFrame = true;
        audioSignals_.publish(bus, clock.latest);
        clock.music.consume(clock.latest, phraseBars_, sectionPhrases_); // a repeat: ignored by index
        clock.analysisCursor = cursor;
        return true;
    }
    if (clock.hasFrame) {
        // No new analysis this render frame: keep continuous values, drop the event pulse.
        bus.setEvent(audioSignals_.onset, false);
    } else {
        audioSignals_.publishSilence(bus);
    }
    return false;
}

bool Engine::advanceClock(SignalClock& clock, signals::SignalBus& bus, const FrameTime& time,
                          bool newAnalysisFrame, const ClockInputs& in) const {
    double bpm = in.bpm;
    bool pulse = false;
    if (in.midi) {
        // The MIDI clock owns the beat clock: phase and count come straight from the tracker
        // (already extrapolated to this frame by the hub).
        clock.beatPhase = in.midiPhase;
        clock.beatCount = in.midiCount;
        pulse = in.midiPulse;
    } else if (bpm > 0.0) {
        // Advance the per-frame beat clock; re-sync to the analyzer whenever it reports a beat.
        clock.beatPhase += time.deltaTime * bpm / 60.0;
        if (newAnalysisFrame && clock.latest.beatCount != clock.lastAnalysisBeatCount) {
            clock.beatPhase = static_cast<double>(clock.latest.beatPhase);
            clock.beatCount = clock.latest.beatCount;
            clock.lastAnalysisBeatCount = clock.latest.beatCount;
            pulse = true;
        } else if (clock.beatPhase >= 1.0) {
            clock.beatPhase -= 1.0;
            ++clock.beatCount;
            pulse = true;
        }
    } else {
        clock.beatPhase = 0.0;
    }
    const double duration = in.duration;
    bus.set(timeSignals_.seconds, static_cast<float>(time.renderTime));
    bus.set(timeSignals_.progress, duration > 0.0 ? static_cast<float>(std::clamp(in.position / duration, 0.0, 1.0)) : 0.0f);
    bus.set(timeSignals_.playing, in.playing ? 1.0f : 0.0f);
    bus.set(timeSignals_.beatPhase, static_cast<float>(clock.beatPhase));
    bus.setEvent(timeSignals_.beatPulse, pulse, 1.0f);
    bus.set(timeSignals_.beatCount, static_cast<float>(clock.beatCount));
    bus.set(timeSignals_.bpm, static_cast<float>(bpm));
    bus.set(timeSignals_.barPhase, static_cast<float>((clock.beatCount % 4 + clock.beatPhase) / 4.0));
    {
        // Phrases and sections from the beat clock: continuous phases plus an event at each phrase
        // boundary, so a state machine can escalate over musical structure rather than per beat.
        const double beatsPerBar = 4.0;
        const double beats = static_cast<double>(clock.beatCount) + clock.beatPhase;
        const double bars = beats / beatsPerBar;
        const double phrases = bars / static_cast<double>(phraseBars_);
        const double sections = phrases / static_cast<double>(sectionPhrases_);
        const auto phraseIndex = static_cast<std::uint32_t>(phrases < 0.0 ? 0.0 : phrases);
        bus.set(timeSignals_.phrasePhase, static_cast<float>(phrases - std::floor(phrases)));
        bus.set(timeSignals_.phraseCount, static_cast<float>(phraseIndex));
        bus.setEvent(timeSignals_.phrasePulse, phraseIndex != clock.lastPhraseIndex, 1.0f);
        clock.lastPhraseIndex = phraseIndex;
        bus.set(timeSignals_.sectionPhase, static_cast<float>(sections - std::floor(sections)));
        bus.set(timeSignals_.sectionCount, static_cast<float>(static_cast<std::uint32_t>(sections < 0.0 ? 0.0 : sections)));
    }
    // The classifier's events, after the clock as they always were. Unconditional: no audio
    // consumed means every music.* signal is false.
    clock.music.publish(bus);
    return pulse;
}

void Engine::updateTimeSignals(const FrameTime& time, bool newAnalysisFrame) {
    const auto& midiClock = controlHub_.midiClock();
    midiClockActive_ = tempoSource_ == TempoSource::MidiClock && midiClock.running() && midiClock.hasTempo();
    // The same resolution the transport readout uses, so the picture and the display cannot be
    // driven by different numbers (ADR-394). The *phase* still comes from the analyzer even
    // when the bpm came from a tag, because a BPM tag has no phase in it.
    ClockInputs in;
    in.bpm = resolvedTempo().second;
    if (midiClockActive_) {
        in.midi = true;
        in.bpm = midiClock.bpm();
        in.midiPhase = midiClock.beatPhase();
        in.midiCount = midiClock.beatCount();
        in.midiPulse = midiClock.beatEvent();
    }
    in.position = positionSeconds();
    in.duration = durationSeconds();
    in.playing = isPlaying();
    const bool pulse = advanceClock(clock_, bus_, time, newAnalysisFrame, in);

    sourceContext_.time = time;
    sourceContext_.audioPosition = in.position;
    sourceContext_.audioDuration = in.duration;
    sourceContext_.playing = in.playing;
    sourceContext_.beatPhase = static_cast<float>(clock_.beatPhase);
    sourceContext_.beatCount = clock_.beatCount;
    sourceContext_.tempoBpm = static_cast<float>(in.bpm);
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
    timelineClock_.beats = static_cast<double>(clock_.beatCount) + clock_.beatPhase;
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

// Where the world's nodes are, for an effect resolving a `node:` or `owner` source (ADR-207/702). An interface
// rather than a lambda so resolution allocates nothing: the engine counts allocations per frame.
class CompositionEffectScene final : public world::EffectSceneQuery {
public:
    CompositionEffectScene(const scene::Composition* comp, const world::HistoryBank* history,
                           std::span<const world::EffectInstance> effects = {},
                           std::span<const std::uint32_t> order = {})
        : comp_(comp), history_(history), effects_(effects), order_(order) {}

    // The frame's context, for re-evaluating XFORM offsets at past instants. Set once the context
    // (which points back at this adapter) exists.
    void setContext(const world::EffectContext* ctx) { ctx_ = ctx; }

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

    [[nodiscard]] bool nodeView(std::string_view name, world::NodeView& out) const override {
        return comp_ != nullptr && comp_->nodeView(name, out);
    }

    // ADR-703. From HIST: metres per second over the last simulation step (a backward difference of
    // the node's drawn position over 1/60 s, interpolated when a play's frames are not on that
    // grid). Deterministic under seek because the history is checkpointed and replayed (ADR-700).
    // False when the node is not subscribed -- the engine subscribes the entity owner of every
    // type in a Wave 1 bucket -- or has not been recorded yet.
    //
    // Wave 2: an owner an XFORM type offsets is differenced over its DRAWN path instead (the history
    // re-applied with the offset at each end), so an orbiting saucer's Space Warp stretches along the
    // orbit. Every other owner keeps HIST's own difference, bit for bit.
    [[nodiscard]] bool nodeVelocity(std::string_view name, glm::vec3& out) const override {
        if (history_ == nullptr) {
            return false;
        }
        if (ctx_ == nullptr || !world::hasTransformProducer(effects_, name)) {
            return history_->velocity(name, out);
        }
        const std::size_t ring = history_->find(name);
        if (ring >= history_->ringCount() || history_->sampleCount(ring) == 0) {
            return false;
        }
        const double t1 = history_->sample(ring, history_->sampleCount(ring) - 1).t;
        const double t0 = t1 - world::HistoryBank::kGridStep;
        glm::vec3 p0;
        glm::vec3 p1;
        if (!nodeDrawnPosition(name, t1, p1)) {
            return false;
        }
        if (!nodeDrawnPosition(name, t0, p0)) {
            out = glm::vec3(0.0f); // one sample: at rest, as HIST says
            return true;
        }
        out = (p1 - p0) / static_cast<float>(world::HistoryBank::kGridStep);
        return true;
    }

    [[nodiscard]] bool nodeDrawnPosition(std::string_view name, double t, glm::vec3& out) const override {
        world::HistorySample s;
        if (history_ == nullptr || !history_->sampleAt(name, t, s)) {
            return false;
        }
        out = s.position;
        if (ctx_ == nullptr || !world::hasTransformProducer(effects_, name)) {
            return true;
        }
        world::EffectContext at = *ctx_;
        at.seconds = t;
        at.scene = nullptr; // a Geometry type never reads the drawn scene (rendering-architecture §3)
        world::TransformOffset offset;
        if (world::transformOffsetAt(effects_, order_, at, name, offset)) {
            out = world::drawnOrigin(offset, s.position, s.rotation, s.scale);
        }
        return true;
    }

private:
    const scene::Composition* comp_;
    const world::HistoryBank* history_;
    std::span<const world::EffectInstance> effects_;
    std::span<const std::uint32_t> order_;
    const world::EffectContext* ctx_ = nullptr;
};

// ADR-703. The play's transform automation, for the seek replay to re-apply when it records HIST.
//
// A play applies the timeline to the parameter finals before the entities step and the scene
// flattens, so a node keyed across the valley is drawn where its track puts it. The replay applies
// no timeline (ADR-700 keys only what the simulation reads), so without this a scrubbed trail on a
// keyed node would be drawn from the node's authored spot. This hands the replay the track's value
// at each replayed instant, as a delta on the parameter's base -- for the recording only; nothing
// here reaches the simulation. Seconds-based tracks only: a beat-keyed track's position depends on
// the beat clock, which the replay does not have either.
class TimelineTransformAutomation final : public world::HistoryAutomation {
public:
    explicit TimelineTransformAutomation(const params::Timeline& timeline) : timeline_(timeline) {}

    [[nodiscard]] bool transformDelta(const params::IParameter& param, double seconds,
                                      glm::vec3& delta) const override {
        if (!timeline_.enabled) {
            return false;
        }
        bool any = false;
        delta = glm::vec3(0.0f);
        const std::size_t count = std::min<std::size_t>(param.componentCount(), 3);
        for (const params::Track& track : timeline_.tracks()) {
            if (!track.enabled || track.param != &param || track.timeBase != params::TimeBase::Seconds) {
                continue;
            }
            const params::KeyValue value = track.evaluate(seconds);
            const auto apply = [&](std::size_t c, float v) {
                const float base = param.baseComponent(c);
                const float current = base + delta[static_cast<int>(c)];
                float next = current;
                switch (track.mode) {
                case params::TrackMode::Replace: next = v; break;
                case params::TrackMode::Add: next = current + v; break;
                case params::TrackMode::Multiply: next = current * v; break;
                }
                delta[static_cast<int>(c)] = next - base;
            };
            if (track.component >= 0) {
                if (static_cast<std::size_t>(track.component) < count) {
                    apply(static_cast<std::size_t>(track.component), value[0]);
                }
            } else {
                for (std::size_t c = 0; c < count; ++c) {
                    apply(c, value[c]);
                }
            }
            any = true;
        }
        return any;
    }

    // Every seconds-based track that could move a node, hashed, so an edited key drops the
    // checkpoints the history was recorded under. Conservative: a track on any `.../position`,
    // `rotation` or `scale` path counts whether or not a subscribed node reads it.
    [[nodiscard]] std::uint64_t key() const override {
        std::uint64_t h = 0x7472616e73ull;
        const auto mix = [&h](std::uint64_t v) { h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2); };
        const auto bits = [](double d) {
            std::uint64_t u = 0;
            std::memcpy(&u, &d, sizeof u);
            return u;
        };
        if (!timeline_.enabled) {
            return h;
        }
        for (const params::Track& track : timeline_.tracks()) {
            const std::string_view target = track.target;
            if (!(target.ends_with("/position") || target.ends_with("/rotation") || target.ends_with("/scale"))) {
                continue;
            }
            for (const char c : target) {
                mix(static_cast<unsigned char>(c));
            }
            mix(static_cast<std::uint64_t>(track.enabled) | (static_cast<std::uint64_t>(track.mode) << 1) |
                (static_cast<std::uint64_t>(track.timeBase) << 4) |
                (static_cast<std::uint64_t>(track.component + 1) << 8));
            mix(bits(track.loopLength));
            for (const params::Key& key : track.keys) {
                mix(bits(key.time));
                mix(static_cast<std::uint64_t>(key.interp));
                for (int c = 0; c < 4; ++c) {
                    mix(bits(static_cast<double>(key.value[static_cast<std::size_t>(c)])));
                    mix(bits(static_cast<double>(key.tangentIn[static_cast<std::size_t>(c)])));
                    mix(bits(static_cast<double>(key.tangentOut[static_cast<std::size_t>(c)])));
                }
            }
        }
        return h;
    }

private:
    const params::Timeline& timeline_;
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

// The aurora's spectrum, folded from the analysis frame into `kAuroraBands` log-spaced bins.
//
// **This is the one place an effect reads audio directly, and it is deliberate.** The rule
// `world/effect_params.hpp` states -- audio reaches an effect as a modulation route, never as a hook
// -- holds for every scalar an aurora has, and `defaultEffectRoutes` is what implements it. It
// cannot hold for the curtain's *shape*, because that is sixteen numbers across the sky and a route
// carries one. So the vector rides in the resolution context instead, and it is read from
// `latestFrame()` -- the same frame the signal bus, the material inputs and the camera director all
// read, published by the same code in both modes.
//
// That is what keeps it deterministic. Offline, `clock_.latest` is `frames[cursor - 1]` of a precomputed
// `AnalysisTrack`, a pure function of the render time; live, it is the runner's newest. Neither
// integrates state here, and nothing below reads a frame counter.
//
// Log-spaced because hearing is: sixteen linear slices of a 1025-bin spectrum would put thirteen of
// them above 6 kHz, where a curtain has nothing to show.
void Engine::updateAuroraSpectrum() {
    auroraSpectrum_.fill(0.5f); // the neutral value: an ordinary curtain when there is no music
    const analysis::AnalysisFrame& f = clock_.latest;
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

// §68, one field many subscribers: everything this scene publishes, rebuilt each frame.
//
// This function is the whole of why the field bus is not another thing that exists and is never
// reached. `defaultEffectRoutes` was correct for a year with no caller (ADR-385/392), and a
// bus nobody published into would have been the same defect wearing a newer word. So it is called
// from `updateAtmosphericEffects`, on the shipping path, before the resolve that reads it -- and
// the test that proves an effect's subscription reaches its picture goes through this call rather
// than around it.
//
// **The publish is of AUTHORED fields, before the resolve, and that ordering is a decision.** A
// vortex's drawn centre may be leaned by its own subscription, which is not known until the resolve
// has run -- so publishing the drawn funnel would need two passes, and a vortex that subscribed to
// itself would need a third. Publishing the authored funnel makes the bus a function of the scene
// rather than of the frame's own output, which is the only version of this with no fixed point in
// it. The difference a subscriber sees is exactly the lean, which is zero unless that funnel is
// itself subscribed, and is bounded at a fifth of its radius when it is.
void Engine::publishFields() {
    scene::Scene& live = controller_->scene();
    fieldBus_.clear();

    // ADR-055's wind, whatever the environment says -- including when it is off. A disabled wind
    // packs to `dir.w == 0` and samples as a flat zero, so a subscriber to a calm world gets a
    // still field rather than a dead name, and switching the wind on in the panel makes every
    // subscriber move without anything being re-resolved.
    fieldBus_.publishWind(std::string(world::fields::kWindField), wind::packWind(live.environment.wind));

    // ADR-388's funnels. One per vortex EFFECT rather than one for "the vortex", because the name
    // is the effect's and a scene may author several even though only the first is marched --
    // subscribing to the second is then a thing an artist can express and a thing the report can
    // explain, rather than a silent mismatch between what the panel lists and what resolves.
    for (const world::EffectInstance& e : effects_) {
        if (e.kind != world::EffectKind::Vortex || !e.enabled || !e.vortex.active()) {
            continue;
        }
        // ADR-562, and this site was BROKEN before it. It hand-copied `world::Vortex` into a
        // `vortex::VortexField` member by member -- and copied **17 of the 24**, omitting
        // `eyeWallWidth`, `eyeWallGain`, `bandArms`, `bandPitchDegrees`, `bandDepth`,
        // `bandHarmonic` and `cloudNoise`: every one of Vortex 2.0's macro-structure controls.
        //
        // So the field this bus PUBLISHED was a different shape from the one the march drew -- no
        // eye wall, no spiral bands, the fBM stack at full weight -- and anything subscribing to a
        // vortex through `fieldBus_` has been following a funnel that does not exist on screen.
        // ADR-388's whole premise is one description of the medium that everything can ask; a
        // second hand-written copy of it is how that premise quietly stops being true, which is
        // ADR-401's finding for the third time in this family (the renderer's clamps, then the
        // renderer's conversion, now this).
        //
        // Composing the field removes the copy rather than correcting it. There is nothing left
        // here to fall behind.
        fieldBus_.publishVortex(world::fields::vortexFieldName(e.id), vortex::packVortex(e.vortex.field));
    }

    // The loud half. A subscription naming a field nobody publishes is this repository's signature
    // defect in a new place, so it is said out loud -- once per name, because a message repeated at
    // frame rate is a message nobody reads, which is the same failure from the other end.
    std::vector<std::string> names;
    std::vector<world::fields::Subscription> subs;
    names.reserve(effects_.size());
    subs.reserve(effects_.size());
    for (const world::EffectInstance& e : effects_) {
        names.push_back(e.name);
        subs.push_back(e.flow);
    }
    for (const world::fields::DeadSubscription& dead : fieldBus_.unresolved(names, subs)) {
        const std::string key = dead.subscriber + " -> " + dead.field;
        if (std::find(reportedDeadFields_.begin(), reportedDeadFields_.end(), key) !=
            reportedDeadFields_.end()) {
            continue;
        }
        reportedDeadFields_.push_back(key);
        log::warn("effect '{}' subscribes to field '{}', which this scene does not "
                  "publish; its flow influence does nothing. Published: {}",
                  dead.subscriber, dead.field, fmt::join(fieldBus_.names(), ", "));
    }
}

// ADR-702: the one evaluator. Resolves this frame's effects -- every owner's, every type's -- into
// the scene's render contributions.
//
// Called from `update()` after the camera has been placed and after the modulation routes have run,
// so the numbers it reads are this frame's finals and the camera it reads is this frame's camera.
// Everything it does is a pure function of the transport second, which is what keeps an offline
// render of second N identical to a playthrough of second N (ADR-091).
//
// The shape is ADR-702 §28's: apply the parameters, build ONE context, then ask each render stage's
// builder for its contribution. The builders walk `effectOrder_` (render stage, priority, stack
// position -- computed when the list changed, so nothing here allocates) and each writes the status
// of the instances it owns. Stages are not forced through one implementation: the surface waves
// are a per-fragment term in the lit pass, the sky is a far-plane draw, the media are marched --
// and all three coexist because each writes its own block of the frame and its own slots in it.
world::EffectContext Engine::effectContext(const world::EffectSceneQuery* scene) const {
    const scene::Scene& live = controller_->scene();
    world::EffectContext ctx;
    ctx.seconds = timelineClock_.seconds;
    ctx.cameraPosition = live.camera.position;
    ctx.cameraTarget = live.camera.target;
    const glm::vec3 aim = live.camera.target - live.camera.position;
    ctx.cameraForward = glm::length(aim) > 1e-5f ? glm::normalize(aim) : glm::vec3(0.0f, 0.0f, -1.0f);
    ctx.cameraVelocity = cameraVelocityOnTimeline();
    ctx.shots = shotSpans_;
    ctx.scene = scene;
    if (const auto* comp = composition()) {
        ctx.heroes = comp->heroes();
    }
    ctx.spectrum = auroraSpectrum_;
    ctx.fieldBus = &fieldBus_;
    // Wave 2 (TRIGGER): beats and onsets from the offline track, the sequence's markers, HIST.
    triggerClock_.bind(track_.get(), sequence_.markers, &historyBank_, ctx.seconds, phraseBars_, sectionPhrases_);
    ctx.triggers = &triggerClock_;
    return ctx;
}

void Engine::updateEffects(EffectPhase phase) {
    scene::Scene& live = controller_->scene();
    if (phase == EffectPhase::BeforeScene) {
        // ---- the Geometry stage, before the flatten (rendering-architecture §3) --------------------
        // The parameters are applied here, once for the frame: the routes have run, so these are this
        // frame's finals, and nothing between here and `AfterScene` writes an effect parameter.
        transformFrame_.count = 0;
        transformFrame_.dropped = 0;
        if (!effects_.empty()) {
            world::applyEffectParameters(effectParams_, effects_);
            if (effectStatus_.size() != effects_.size()) {
                effectStatus_.assign(effects_.size(), world::EffectStatus::Dormant);
            }
            if (effectStatusReason_.size() != effects_.size()) {
                effectStatusReason_.assign(effects_.size(), std::string());
            }
            // Reasons are rewritten every frame; clearing keeps the strings' storage.
            for (std::string& r : effectStatusReason_) {
                r.clear();
            }
            // No scene query: this frame's drawn transforms do not exist yet, and last frame's are
            // not an input a Geometry type may have (it would lag, and differ between play and scrub).
            const world::EffectContext ctx = effectContext(nullptr);
            // RenderStage::Geometry -- XFORM offsets (Orbit, Spiral, Float, Shake, Bounce).
            world::buildTransformFrame(effects_, ctx, transformFrame_, effectOrder_, effectStatus_,
                                       effectStatusReason_);
        }
        // Every frame, so a replaced composition is handed the frame too; an empty frame is the
        // flatten exactly as it was without XFORM.
        if (auto* comp = composition()) {
            comp->setEffectOffsets(&transformFrame_);
        }
        return;
    }
    if (effects_.empty()) {
        live.waves = world::WaveFrame{};
        live.atmospherics = world::AtmosphericFrame{};
        live.distortion.count = 0; // ADR-703: DF's gate -- no producer, nothing touched
        live.distortion.dropped = 0;
        // An emitter's system lives in the scene's particle list, so "no effects" has to take it
        // back out; the builder removes every `fx:` system whose instance is gone.
        world::buildParticleFrame({}, world::EffectContext{}, live.particles, {}, {}, {});
        live.entityFx.clear();
        live.ribbons.vertices.clear(); // capacity kept: no allocation when a trail comes back
        live.ribbons.strips.clear();
        live.ribbons.dropped = 0;
        live.stars = world::StarField{};
        return;
    }
    // The parameters were applied, and the status table sized and its reasons cleared, by the
    // `BeforeScene` phase this frame; the Geometry stage's statuses are already written.
    updateAuroraSpectrum();
    publishFields();

    CompositionEffectScene sceneAdapter(composition(), &historyBank_, effects_, effectOrder_);
    const world::EffectContext ctx = effectContext(&sceneAdapter);
    sceneAdapter.setContext(&ctx);
    if (effectStatus_.size() != effects_.size() || effectStatusReason_.size() != effects_.size()) {
        // Only if the list changed between the phases, which nothing in `update` does.
        effectStatus_.assign(effects_.size(), world::EffectStatus::Dormant);
        effectStatusReason_.assign(effects_.size(), std::string());
    }
    // RenderStage::Material -- the surface waves.
    world::buildWaveFrame(effects_, ctx, live.waves, effectOrder_, effectStatus_);
    // RenderStage::Sky and RenderStage::Volumetric -- comets, auroras and placed media.
    world::buildAtmosphericFrame(effects_, ctx, live.atmospherics, effectOrder_, effectStatus_);
    // RenderStage::ScreenSpace -- DF distortion proxies (Space Warp).
    world::buildDistortionFrame(effects_, ctx, live.distortion, effectOrder_, effectStatus_,
                                effectStatusReason_);
    // RenderStage::Particles -- effect-owned particle systems (EMIT, ADR-703).
    world::buildParticleFrame(effects_, ctx, live.particles, effectOrder_, effectStatus_, effectStatusReason_);
    // RenderStage::Material -- per-entity lanes (FXL), and the spill lights they request (LIGHTMOD).
    world::buildEntityFxFrame(effects_, ctx, live.entityFx, effectOrder_, effectStatus_, effectStatusReason_);
    // RenderStage::Particles -- ADR-703's camera-facing strips (Trail), RIBBON over HIST.
    world::buildRibbonFrame(effects_, ctx, historyBank_, live.ribbons, effectOrder_, effectStatus_,
                            effectStatusReason_);
    // RenderStage::Sky -- the star field (Stars, Wave 2), in place of the background's fixed stars.
    world::buildStarField(effects_, ctx, live.stars, effectOrder_, effectStatus_, effectStatusReason_);
    // An instance attached to an entity the scene does not have is reported as such, whatever its
    // builder said: "orphaned" is the actionable answer, "dormant" would send somebody looking at
    // its timing.
    std::uint32_t dropped = 0;
    for (std::size_t i = 0; i < effects_.size(); ++i) {
        if (effects_[i].owner.kind != world::EffectTarget::World && !effectOwnerExists(effects_[i].owner)) {
            effectStatus_[i] = world::EffectStatus::Orphaned;
            effectStatusReason_[i] = fmt::format("{} '{}' is not in this scene.",
                                                 world::effectTargetName(effects_[i].owner.kind),
                                                 effects_[i].owner.name);
        }
        if (effectStatus_[i] == world::EffectStatus::Dropped && effectStatusReason_[i].empty()) {
            // The generic sentence; a builder that knows more (which budget, how full) wrote its own.
            if (const world::EffectSchema* schema = world::effectSchema(effects_[i].kind)) {
                effectStatusReason_[i] = fmt::format("The {} stage's GPU capacity was full this frame.",
                                                     world::renderStageName(schema->stage));
            }
        }
        dropped += effectStatus_[i] == world::EffectStatus::Dropped ? 1u : 0u;
    }
    // ADR-562/702: say it out loud, once per changed count rather than per frame. Before ADR-562 a
    // fog bank and a vortex authored together rendered byte-identical to whichever came first, and
    // the one counter that knew was read by nothing.
    if (dropped != lastEffectDropped_) {
        lastEffectDropped_ = dropped;
        if (dropped > 0) {
            log::warn("{} effect(s) are active but not drawn: their render stage's GPU capacity is "
                      "full (surface waves {}, comets {}, auroras {}, placed media {}, distortion "
                      "proxies {}, ribbon vertices {}, effect particle systems {}). The Effects panel "
                      "marks which, and says why.",
                      dropped, world::kMaxGpuWaves, world::kMaxGpuComets, world::kMaxGpuAuroras,
                      world::kMaxMedia, world::kMaxDistortionProxies, world::kRibbonVertexBudget,
                      world::kMaxEffectParticleSystems);
        }
    }
    lastMediaDropped_ = live.atmospherics.mediaDropped;
    // The §12 quality control. Offline renders get the full march; live playback takes two thirds of
    // it, which is a difference nobody sees on a moving comet and a third of the tail's cost.
    live.atmospherics.cometSteps = mode_ == EngineMode::Offline ? 28u : 18u;

    // Logged on the edge rather than per frame: "why is my effect not firing" is a question about
    // when it started and stopped, and a line per frame would bury the answer.
    if (live.waves.count != lastWaveCount_) {
        lastWaveCount_ = live.waves.count;
        log::debug("surface waves: {} live at {:.2f}s", live.waves.count, ctx.seconds);
    }
    const std::uint32_t total = live.atmospherics.cometCount + live.atmospherics.auroraCount;
    if (total != lastAtmosphericCount_) {
        lastAtmosphericCount_ = total;
        log::debug("sky effects: {} comet(s), {} aurora(s) live at {:.2f}s",
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
        if (directedEvents_.contains(fired.eventIndex)) {
            continue;   // ADR-824: the composition gives this order itself, at its second, on both paths
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
        auto& world = composition->entityWorld();
        const double now = timelineClock_.seconds;
        const bool found = directed->release ? world.release(directed->entity, now)
                           : directed->goal  ? world.setGoal(directed->entity, directed->goalSubject,
                                                             directed->goalAffordance, now)
                                             : world.direct(directed->entity, {directed->action}, now);
        if (!found) {
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
            publishFrame(clock_, bus_, runner_->latest());
            stats_.analysisHopMicros = runner_->averageHopMicros();
            stats_.analysisFrames = runner_->framesProduced();
        } else if (clock_.hasFrame) {
            // No new analysis this render frame: keep continuous values, drop the event pulse.
            bus_.setEvent(audioSignals_.onset, false);
        } else {
            audioSignals_.publishSilence(bus_);
        }
    } else if (track_ && !track_->empty()) {
        // ADR-870: the same function a seek's replay runs, on the live clock and bus.
        const std::size_t probeCursorStart = clock_.analysisCursor; // TEMPORARY: phase 2
        {
            const probe2::Add probeCatchup(probe2::frame().analysisCatchupMs); // TEMPORARY: phase 2
            newFrame = consumeAnalysis(clock_, bus_, time.renderTime);
        }
        probe2::frame().analysisFramesConsumed += clock_.analysisCursor - probeCursorStart;
        if (newFrame) {
            stats_.analysisFrames = clock_.analysisCursor;
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
    updateTimeSignals(time, newFrame); // and music.*, unconditionally (ADR-073)
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
    // ADR-703 (rendering-architecture §3): the entity-derived signals, from the last completed step,
    // before the routes that read them -- one step of latency, the same in a play and a scrub.
    publishEntitySignals();
    modulator_.applyRoutes(bus_, params_, time.deltaTime);
    // Autonomous behaviour, after the routes and before the scene reads the finals (ADR-088): a
    // behaviour's own knobs have been modulated by now, and the offsets it writes land on top of
    // whatever the routes wrote, so a route and a behaviour compose on one property.
    controller_->updateBehaviour(time, bus_);
    postCharacterEvents(); // ADR-828: this step's named completions, before the dispatcher drains
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
    // RenderStage::Geometry (Wave 2, rendering-architecture §3): the XFORM offsets, which the
    // flatten below composes into their owners' transforms -- before it, so children, attachments,
    // effects reading the drawn view and `prevModel` all follow them.
    updateEffects(EffectPhase::BeforeScene);
    controller_->update(time);
    // ADR-703: this step's drawn transforms into HIST, after the flattening and before the effects
    // read them -- so a Trail's head and its newest sample are the same instant.
    recordHistory();
    probeStage(probe2::frame().updControllerMs); // TEMPORARY: phase 2
    stats_.allocsController = allocsNow() - allocMark;
    scene::applyPostParameters(postParams_, post_);
    scene::applyTemporalParameters(temporalParams_, temporal_);
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
    // ADR-834 (Phase D §36): where each character sits in the finished frame, published after the
    // camera and its lens are final. Read by routes and reactions from the next frame, like every
    // other bus signal; never read by the simulation's own step.
    if (auto* comp = composition()) {
        comp->publishCinematicSignals(bus_);
    }
    // Without this line every temporal parameter resolves, round-trips and reaches nothing --
    // ADR-039's selective bloom and ADR-035's identifier mask were both shipped missing exactly
    // this assignment, and both were invisible because the feature simply never ran.
    controller_->scene().temporal = temporal_;
    updateEffects(EffectPhase::AfterScene); // every stage after Geometry, against the flattened scene
    {
        shaders::StdUniforms base;
        const auto& f = clock_.latest;
        base.audio[0] = clock_.hasFrame ? f.rms : 0.0f;
        base.audio[1] = clock_.hasFrame ? f.bands[0] : 0.0f;
        base.audio[2] = clock_.hasFrame ? f.bands[2] : 0.0f;
        base.audio[3] = clock_.hasFrame ? f.bands[4] : 0.0f;
        base.audio2[0] = clock_.hasFrame ? f.bands[1] : 0.0f;
        base.audio2[1] = clock_.hasFrame ? f.bands[3] : 0.0f;
        base.audio2[2] = clock_.hasFrame ? std::min(1.0f, f.onsetStrength / 2.0f) : 0.0f;
        base.audio2[3] = static_cast<float>(clock_.beatPhase);
        base.beat[0] = sourceContext_.tempoBpm;
        base.beat[1] = static_cast<float>(clock_.beatCount);
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

// ---- ADR-703: HIST and the entity-derived signals -------------------------------------------------

void Engine::refreshHistorySubscriptions() {
    scene::Composition* comp = composition();
    if (comp != historyComposition_) {
        // Another scene: nothing recorded against the last one describes this one.
        historyBank_.clear();
        historyComposition_ = comp;
    }
    if (historyAutomation_ == nullptr) {
        historyAutomation_ = std::make_unique<TimelineTransformAutomation>(timeline_);
    }
    historyBank_.setAutomation(historyAutomation_.get());
    if (comp != nullptr) {
        comp->setHistoryBank(&historyBank_);
    }

    // Who is recorded: the entity owner of every type that reads its owner's motion -- a type in
    // one of the Wave 1 buckets (a Trail's path, a Space Warp's velocity, a Glow's distance) -- as
    // deep as the deepest of them reads; and every entity a route names a signal of. The sky, the
    // media and the surface waves read nothing of their owner's motion, so Glowmere's sixteen
    // hero pulses subscribe nobody.
    std::vector<world::HistorySubscription> wanted;
    for (const world::EffectInstance& e : effects_) {
        if (e.owner.kind != world::EffectTarget::Entity || e.owner.name.empty()) {
            continue;
        }
        const world::EffectSchema* schema = world::effectSchema(e.kind);
        // XFORM (Wave 2) reads nothing of its owner's motion either: it is a function of t alone.
        if (schema == nullptr || world::isAtmosphericBucket(schema->resolve.bucket) ||
            schema->resolve.bucket == world::EffectBucket::Surface ||
            schema->resolve.bucket == world::EffectBucket::Transform) {
            continue;
        }
        wanted.push_back(world::HistorySubscription{e.owner.name, world::ribbonHistorySeconds(e)});
    }
    constexpr std::string_view kEntity = "entity.";
    for (const params::ModRoute& r : modulator_.routes()) {
        const std::string_view src = r.source;
        if (!src.starts_with(kEntity)) {
            continue;
        }
        for (const std::string_view leaf : world::kEntitySignalLeaves) {
            if (src.size() > kEntity.size() + leaf.size() + 1 && src.ends_with(leaf) &&
                src[src.size() - leaf.size() - 1] == '.') {
                wanted.push_back(world::HistorySubscription{
                    std::string(src.substr(kEntity.size(), src.size() - kEntity.size() - leaf.size() - 1)), 0.0f});
                break;
            }
        }
    }
    world::appendEffectHistoryNeeds(effects_, wanted); // Wave 2: Proximity triggers, a Shockwave's release point, a wake
    if (historyBank_.subscribe(wanted) || entitySignals_.size() != historyBank_.ringCount()) {
        // Signals of an entity nobody reads any more go quiet rather than holding their last value.
        for (const EntitySignalIds& old : entitySignals_) {
            for (const signals::SignalId id : old.ids) {
                bus_.set(id, 0.0f);
            }
        }
        entitySignals_.clear();
        for (std::size_t ring = 0; ring < historyBank_.ringCount(); ++ring) {
            EntitySignalIds sig;
            sig.ring = ring;
            const std::string prefix = world::entitySignalPrefix(historyBank_.ringNode(ring));
            // Declared with the ranges parameters-and-modulation.md gives them, so the panel's
            // meters and a route's normalisation have something honest to read.
            static constexpr std::array<std::pair<float, float>, 6> kRanges{
                {{0.0f, 50.0f}, {-50.0f, 50.0f}, {-50.0f, 50.0f}, {-50.0f, 50.0f}, {0.0f, 100.0f}, {0.0f, 500.0f}}};
            for (std::size_t k = 0; k < sig.ids.size(); ++k) {
                sig.ids[k] = bus_.declare(prefix + std::string(world::kEntitySignalLeaves[k]), kRanges[k].first,
                                          kRanges[k].second);
            }
            entitySignals_.push_back(sig);
        }
    }
    if (cameraSpeedSignal_ == signals::kInvalidSignal) {
        cameraSpeedSignal_ = bus_.declare("camera.speed", 0.0f, 50.0f);
    }
}

void Engine::recordHistory() {
    if (historyBank_.empty()) {
        return;
    }
    if (const scene::Composition* comp = composition()) {
        // The play's finals already carry the timeline, the routes and the entities' offsets: no
        // automation to re-apply.
        comp->recordHistory(historyBank_, timelineClock_.seconds);
    }
}

void Engine::publishEntitySignals() {
    if (cameraSpeedSignal_ != signals::kInvalidSignal) {
        bus_.set(cameraSpeedSignal_, glm::length(cameraVelocityOnTimeline()));
    }
    if (entitySignals_.empty()) {
        return;
    }
    // The camera at the instant of the step the entities are read at. A baked camera track is a
    // pure function of time, so a scrub reads the same distance a play does; a camera nobody keyed
    // is wherever it was last drawn.
    const params::Track* cameraTrack = timeline_.findTrack("camera/position", -1);
    const bool keyed = cameraTrack != nullptr && cameraTrack->enabled && !cameraTrack->keys.empty() &&
                       cameraTrack->timeBase == params::TimeBase::Seconds;
    const glm::vec3 drawnCamera = controller_ ? controller_->scene().camera.position : glm::vec3(0.0f);
    for (const EntitySignalIds& sig : entitySignals_) {
        glm::vec3 velocity(0.0f);
        glm::vec3 acceleration(0.0f);
        float cameraDistance = 0.0f;
        if (sig.ring < historyBank_.ringCount() && historyBank_.sampleCount(sig.ring) > 0) {
            static_cast<void>(historyBank_.velocity(sig.ring, velocity));
            static_cast<void>(historyBank_.acceleration(sig.ring, acceleration));
            const world::HistorySample& last = historyBank_.sample(sig.ring, historyBank_.sampleCount(sig.ring) - 1);
            glm::vec3 camera = drawnCamera;
            if (keyed) {
                const params::KeyValue v = cameraTrack->evaluate(last.t);
                camera = glm::vec3(v[0], v[1], v[2]);
            }
            cameraDistance = glm::length(last.position - camera);
        }
        bus_.set(sig.ids[0], glm::length(velocity));
        bus_.set(sig.ids[1], velocity.x);
        bus_.set(sig.ids[2], velocity.y);
        bus_.set(sig.ids[3], velocity.z);
        bus_.set(sig.ids[4], glm::length(acceleration));
        bus_.set(sig.ids[5], cameraDistance);
    }
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
