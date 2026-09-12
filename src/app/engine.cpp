#include "app/engine.hpp"

#include "seq/layer_sink.hpp"

#include "core/phase_profiler.hpp"

#include <functional>
#include <map>
#include <set>

#include "assets/image.hpp"
#include "core/hash.hpp"
#include "core/log.hpp"
#include "params/serialization.hpp"

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
    auto report = seq::install(sequence_, timeline_, params_, sink, sequenceTargets_);
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

void Engine::detachSceneParameters() {
    if (auto* comp = composition()) {
        comp->detach();
    }
    shaderLayers_.detach();
    layers_.detach();
    timeline_.unbind();
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
    if (hasSequence()) {
        doc["sequence"] = sequence_.toJson();
    }
    doc["render"] = render_.toJson();
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
                if (auto r = loadComposition(scenePath); !r) {
                    environmentPath_ = previousEnvironment;
                    warn("scene: " + r.error().message);
                } else {
                    sceneEnvironment = environmentPath_;
                }
            } else if (kind == "composition" && sceneRef.contains("inline")) {
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
    outputs_ = doc.contains("outputs") && doc["outputs"].is_array() ? doc["outputs"] : nlohmann::json::array();
    ensureControlSource();
    sources_.attach(bus_, params_);
    if (doc.contains("shaders")) {
        if (auto r = shaderLayers_.fromJson(doc["shaders"]); !r) {
            return r;
        }
    } else {
        shaderLayers_.clear();
    }
    if (doc.contains("timeline")) {
        if (auto r = timeline_.fromJson(doc["timeline"]); !r) {
            return r;
        }
    } else {
        timeline_.clear();
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
    const auto path = std::filesystem::absolute(rawPath).lexically_normal();
    registry_.setBaseDirectory(path.parent_path());
    auto comp = scene::Composition::loadFile(path, registry_);
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
    // how much audio was loaded. A loaded *file* is a finite, known signal and can be analysed up
    // front whatever mode is playing it; a live input genuinely has no track and correctly gets
    // none.
    //
    // It costs one pass over the file at load -- about 130 ms for ninety seconds -- and both of the
    // places that read `track_` are already behind a mode or player check, so this is inert for
    // live rendering.
    track_ = std::make_unique<analysis::AnalysisTrack>(
        analysis::AnalysisTrack::analyze(*file, analyzerConfig_));
    offlineFrameCursor_ = 0;
    log::info("analysed {:.2f} s of audio: {} frames", file->durationSeconds(), track_->frames().size());
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
        composition->entityWorld().seek(seconds, &params_, nullptr,
                                        composition->scene().camera.position);
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
    // The tempo is for the bars/beats readout and for beat stepping with no analysed grid. It comes
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
    // The analysed grid first: it is where the beats actually are, as opposed to where a constant
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

double Engine::markerBoundary(double fromSeconds, int direction) const {
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

void Engine::stepMarkers(int direction) {
    const double target = markerBoundary(transport_.positionSeconds(), direction);
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
        // Advance the per-frame beat clock; re-sync to the analyser whenever it reports a beat.
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

void Engine::update(const FrameTime& time) {
    const auto start = std::chrono::steady_clock::now();
    bool newFrame = false;

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

    // Live control first: MIDI clock messages feed this frame's beat clock, transport commands
    // move the position the time signals read, parameter writes precede modulation.
    controlHub_.update(*this, time);
    if (controlSource().needsAttach()) {
        sources_.attach(bus_, params_); // new control channels: declare and rebind routes
        rebind();
    }
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
    stats_.allocsModulation = allocsNow() - allocMark;
    allocMark = allocsNow();
    controller_->update(time);
    stats_.allocsController = allocsNow() - allocMark;
    scene::applyPostParameters(postParams_, post_);
    // ---- physical camera (ADR-037) ---------------------------------------------------------
    // After controller_->update() has placed the camera: the lens, the focus tracker's new
    // distance and the exposure block go onto the camera and into the post chain, which applies
    // exposure before bloom (ADR-039). With the defaults the exposure scale is exactly 1 and the
    // lens does not touch the field of view, so scenes authored before this render unchanged.
    scene::applyCameraParameters(cameraParams_, lens_, exposure_, focus_);
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

    stats_.allocsOther = (allocsNow() - allocsAtStart) - stats_.allocsControl - stats_.allocsSignals -
                         stats_.allocsModulation - stats_.allocsController;
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
