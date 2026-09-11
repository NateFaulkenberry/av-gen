#include "app/engine.hpp"

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
    }
    if (auto r = timeline_.bind(params_); !r) {
        log::warn("{}", r.error().message);
    }
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
        doc["timeline"] = timeline_.toJson();
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
        doc["composition"] = layers_.toJson();
    }
    doc["render"] = render_.toJson();
    doc["control"] = controlHub_.map().toJson();
    if (outputs_.is_array() && !outputs_.empty()) {
        doc["outputs"] = outputs_;
    }
    doc["control"]["tempoSource"] = tempoSourceName(tempoSource_);
    doc["control"]["phraseBars"] = phraseBars_;
    doc["control"]["sectionPhrases"] = sectionPhrases_;
    nlohmann::json assets = nlohmann::json::object();
    if (!audioPath_.empty()) {
        assets["audio"] = assetRefJson(audioPath_, dir);
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

    // ---- assets first: they define the parameter surface the rest of the document targets ----
    if (const auto assets = doc.find("assets"); assets != doc.end() && assets->is_object()) {
        std::filesystem::path sceneEnvironment;
        if (assets->contains("audio")) {
            if (const auto audio = resolveAsset((*assets)["audio"], "audio"); audio && *audio != audioPath_) {
                if (auto r = loadAudio(*audio); !r) {
                    warn("audio: " + r.error().message);
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
    // Parameter values for sources and shader inputs arrive in the same document; apply them
    // again now that those parameters exist (unknown-at-first-pass paths were skipped).
    if (auto r = params::loadProject(doc, params_, modulator_, nullptr, nullptr); !r) {
        return r;
    }
    rebind();
    modulator_.resetState();
    projectPath_ = path;
    reportCuePresetOverrides();
    log::info("project '{}' loaded: {} parameters, {} routes, {} sources, {} presets, {} timeline tracks, {} cues, {} warning(s)",
              path.filename().string(), params_.size(), modulator_.routes().size(), sources_.sources().size(),
              presets_.presets().size(), timeline_.tracks().size(), timeline_.cues().size(), projectWarnings_.size());
    return {};
}

void Engine::newProject() {
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
    for (auto* p : params_.ordered()) {
        if (p->path().rfind("post/", 0) == 0) {
            p->resetToDefault();
        }
    }
    post_ = scene::PostSettings{};
    render_ = RenderSettings{};
    controlHub_.setMap(control::ControlMap{});
    outputs_ = nlohmann::json::array();
    setTempoSource(TempoSource::Analysis);
    ensureControlSource();
    sources_.attach(bus_, params_);
    modulator_.masterGain = 1.0f;
    projectPath_.clear();
    projectWarnings_.clear();
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
    const float masterGain = modulator_.masterGain;
    detachSceneParameters();
    params_.clear();
    modulator_.clearRoutes();
    modulator_.masterGain = masterGain;
    (*comp)->attach(params_, modulator_);
    compositionPath_ = path;
    // ADR-059: the composition's own `post` block, kept until the parameters it names exist again.
    // `params_.clear()` above destroyed the previous set, and installController below is what
    // re-registers the post parameters -- reading postParams_ before that point is a dangling
    // pointer, which is exactly the bug the first version of this had.
    const nlohmann::json postJson = (*comp)->postJson();
    // A scene file may carry its own environment map.
    if (!(*comp)->environmentMap().empty()) {
        environmentPath_ = registry_.resolve((*comp)->environmentMap());
    }
    installController(std::move(*comp));
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
            node->asset = registry_.relativise(node->asset);
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

Result<double> Engine::loadAudio(const std::filesystem::path& path) {
    auto file = audio::AudioFile::load(path);
    if (!file) {
        return std::unexpected(file.error());
    }
    auto shared = std::make_shared<const audio::AudioFile>(std::move(*file));
    analyzerConfig_.sampleRate = shared->sampleRate();

    if (mode_ == EngineMode::Live) {
        runner_.reset();
        if (auto r = player_->setSource(shared); !r) {
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
        analysis::AnalysisTrack::analyze(*shared, analyzerConfig_));
    offlineFrameCursor_ = 0;
    log::info("analysed '{}': {} frames", path.filename().string(), track_->frames().size());
    audioFile_ = shared;
    audioPath_ = path;
    modulator_.resetState();
    music_.reset();
    hasFrame_ = false;
    return shared->durationSeconds();
}

Result<void> Engine::play() {
    if (!player_ || !player_->hasSource()) {
        return fail("no audio loaded");
    }
    return player_->play();
}

void Engine::pause() {
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
    if (player_) {
        player_->stop();
        modulator_.resetState();
        sources_.reset();
        music_.reset();
        beatClockPhase_ = 0.0;
    }
}

void Engine::seekSeconds(double seconds) {
    if (player_) {
        player_->seekSeconds(seconds);
    } else if (track_) {
        offlineFrameCursor_ = 0;
    }
    modulator_.resetState();
    sources_.reset();
    // A seek discontinuity in the energy history reads as a drop; the detector must not carry
    // the old piece across it.
    music_.reset();
    beatClockPhase_ = 0.0;
    lastAnalysisBeatCount_ = 0;
    cueState_ = {};   // cues re-sync from the new position on the next frame
    cueApplied_ = false;
}

bool Engine::isPlaying() const { return player_ && player_->isPlaying(); }

double Engine::positionSeconds() const {
    if (input_) {
        return input_->sampleRate() > 0 ? static_cast<double>(input_->framesCaptured()) / input_->sampleRate() : 0.0;
    }
    if (player_) {
        return player_->positionSeconds();
    }
    return lastRenderTime_;
}

double Engine::durationSeconds() const { return audioFile_ ? audioFile_->durationSeconds() : 0.0; }

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
    if (mode_ == EngineMode::Live && isPlaying()) {
        // Audio is the master clock while playing (ADR-012); dt stays wall-derived for smooth
        // integration because the play-head advances in device-period steps.
        time.renderTime = player_->positionSeconds();
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
    timelineClock_.seconds = audioFile_ ? positionSeconds() : time.renderTime;
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
    modulator_.applyRoutes(bus_, params_, time.deltaTime);
    if (viewportHeight_ > 0) {
        if (auto* comp = composition()) {
            comp->setViewport(viewportWidth_, viewportHeight_);
        }
    }
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
