#pragma once

// The audiovisual pipeline (architecture.md): AudioPlayer -> AnalysisRunner -> SignalBus ->
// Modulator -> ParameterSet -> OrbScene -> Scene. The renderer and UI sit beside it and only
// read the Scene / parameters. Two modes (ADR-012): Live (audio device is the clock, analysis
// runs on a thread) and Offline (fixed-step clock, analysis precomputed and indexed by time).

#include "analysis/analysis_runner.hpp"
#include "app/control_hub.hpp"
#include "app/render_settings.hpp"
#include "audio/audio_input.hpp"
#include "analysis/analysis_track.hpp"
#include "analysis/analyzer.hpp"
#include "audio/audio_file.hpp"
#include "audio/audio_player.hpp"
#include "core/error.hpp"
#include "core/time.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "params/preset.hpp"
#include "params/timeline.hpp"
#include "assets/asset_registry.hpp"
#include "scene/composition.hpp"
#include "scene/gltf_scene.hpp"
#include "scene/orb_scene.hpp"
#include "scene/post_settings.hpp"
#include "scene/scene_controller.hpp"
#include "shaders/shader_layers.hpp"
#include "signals/audio_signals.hpp"
#include "signals/signal_bus.hpp"
#include "signals/source.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::app {

enum class EngineMode { Live, Offline };

// Where the beat clock (beat.* signals, SourceContext tempo fields, the timeline's beat time)
// comes from: the audio analyser, or an incoming MIDI clock (ADR-021 follow-up). With MidiClock
// selected but no running clock the analyser is used; the analyser keeps running either way.
enum class TempoSource { Analysis, MidiClock };
[[nodiscard]] const char* tempoSourceName(TempoSource source);          // "analysis" | "midi"
[[nodiscard]] std::optional<TempoSource> tempoSourceFromName(std::string_view name);

struct EngineStats {
    double analysisHopMicros = 0.0;
    std::uint64_t analysisFrames = 0;
    double modulationMicros = 0.0;
};

class Engine {
public:
    explicit Engine(EngineMode mode);
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    [[nodiscard]] EngineMode mode() const { return mode_; }

    // Decodes the file. Live: installs it in the player and starts the analysis thread.
    // Offline: precomputes the analysis track. Returns the decoded duration.
    [[nodiscard]] Result<double> loadAudio(const std::filesystem::path& path);

    // Replaces the scene controller with a glTF scene (parameters and routes are rebuilt).
    [[nodiscard]] Result<void> loadScene(const std::filesystem::path& path);
    // Restores the built-in orb scene.
    void loadOrbScene();
    // Scene composition (milestone 0.7): a new empty composition, a composition from a scene
    // file, saving the current composition, and node editing.
    void newComposition();
    [[nodiscard]] Result<void> loadComposition(const std::filesystem::path& path);
    [[nodiscard]] Result<void> saveComposition(const std::filesystem::path& path);
    [[nodiscard]] scene::Composition* composition() { return dynamic_cast<scene::Composition*>(controller_.get()); }
    // Adds a node to the current composition (converting the orb/glTF controller into one first).
    [[nodiscard]] Result<scene::CompositionNode*> addNode(scene::CompositionNode node);
    void removeNode(const std::string& name);
    [[nodiscard]] assets::AssetRegistry& assets() { return registry_; }
    [[nodiscard]] const std::filesystem::path& compositionPath() const { return compositionPath_; }
    // Loads an equirectangular HDR and installs it as the current scene's environment map.
    [[nodiscard]] Result<void> loadEnvironment(const std::filesystem::path& path);
    [[nodiscard]] const std::filesystem::path& environmentPath() const { return environmentPath_; }
    // Routes any supported file by extension: audio, .gltf/.glb, .hdr, .json (project).
    [[nodiscard]] Result<void> loadFile(const std::filesystem::path& path);

    // ---- project (milestone 0.9, ADR-019): parameters, routes, sources, presets, shaders,
    // timeline, plus the asset references (audio, scene, environment) that make a project a
    // complete session. Paths are written relative to the project file. ----
    static constexpr const char* kAppVersion = "0.1.0";
    [[nodiscard]] Result<void> saveProject(const std::filesystem::path& path);
    // Restores the assets first (a missing one is a warning, see projectWarnings()), then the
    // rest. Fails only when the document itself is invalid.
    [[nodiscard]] Result<void> loadProject(const std::filesystem::path& path);
    [[nodiscard]] const std::filesystem::path& projectPath() const { return projectPath_; }
    [[nodiscard]] const std::vector<std::string>& projectWarnings() const { return projectWarnings_; }
    void clearProjectPath() { projectPath_.clear(); }
    // Resets everything but the audio: orb scene, no sources/presets/timeline/shaders/environment,
    // default post settings, no project path.
    void newProject();
    // Every file the current session references (audio, environment, scene files and their
    // assets recursively, shader layers), absolute, without duplicates.
    [[nodiscard]] std::vector<std::filesystem::path> referencedFiles() const;
    // Copies every referenced file into <dir>/assets (scene files rewritten with relative
    // references) and writes <dir>/project.json pointing at the copies.
    [[nodiscard]] Result<void> exportBundle(const std::filesystem::path& dir);

    // ---- built-in post-processing ----
    [[nodiscard]] scene::PostSettings& post() { return post_; }
    [[nodiscard]] const scene::PostSettings& post() const { return post_; }

    // ---- user shader layers ----
    [[nodiscard]] shaders::ShaderLayerSet& shaderLayers() { return shaderLayers_; }
    [[nodiscard]] const shaders::ShaderLayerSet& shaderLayers() const { return shaderLayers_; }
    [[nodiscard]] Result<std::uint32_t> addShaderLayer(const std::filesystem::path& path, shaders::LayerStage stage);
    void removeShaderLayer(std::uint32_t id);

    // ---- modulation sources and presets ----
    [[nodiscard]] signals::SourceRack& sources() { return sources_; }
    [[nodiscard]] params::PresetBank& presets() { return presets_; }
    // Adds a source of the given kind with a unique name derived from `baseName`; attaches and
    // re-binds. Returns the source.
    signals::Source& addSource(const std::string& kind, const std::string& baseName);
    void removeSource(const std::string& kind, const std::string& name);
    // Rebinds routes after routes/sources/parameters changed (UI edits).
    void rebind();
    // Preset helpers: capture the current base values, apply one, or morph between two.
    params::Preset& storePreset(const std::string& name);
    [[nodiscard]] bool recallPreset(const std::string& name);
    void morphPresets(const std::string& a, const std::string& b, float t);

    // ---- live control (milestone 1.1, ADR-021) ----
    [[nodiscard]] ControlHub& control() { return controlHub_; }
    [[nodiscard]] const ControlHub& control() const { return controlHub_; }
    // The always-present "control" source (control.<channel> signals).
    [[nodiscard]] signals::ControlSource& controlSource();
    // Live audio input instead of a file: opens the capture device (substring match, "" =
    // default) and runs the analysis on it. Live mode only. stopAudioInput() returns to the
    // player (silent until a file is loaded).
    [[nodiscard]] Result<void> useAudioInput(const std::string& deviceName = "");
    void stopAudioInput();
    [[nodiscard]] bool hasLiveInput() const { return input_ != nullptr; }
    [[nodiscard]] audio::AudioInput* audioInput() { return input_.get(); }
    // Tempo source (saved in the project's "control" block as "tempoSource").
    void setTempoSource(TempoSource source);
    [[nodiscard]] TempoSource tempoSource() const { return tempoSource_; }
    // True when this frame's beat clock came from the MIDI clock (source selected and running).
    [[nodiscard]] bool midiClockActive() const { return midiClockActive_; }

    // ---- outputs (milestone 1.2): the application owns the windows; the engine only carries the
    // project's "outputs" block so it saves and loads with everything else ----
    [[nodiscard]] const nlohmann::json& outputsJson() const { return outputs_; }
    void setOutputsJson(nlohmann::json outputs) { outputs_ = std::move(outputs); }

    // ---- offline render settings (milestone 1.0), saved in the project under "render" ----
    [[nodiscard]] RenderSettings& renderSettings() { return render_; }
    [[nodiscard]] const RenderSettings& renderSettings() const { return render_; }

    // ---- timeline (milestone 0.8) ----
    [[nodiscard]] params::Timeline& timeline() { return timeline_; }
    [[nodiscard]] const params::Timeline& timeline() const { return timeline_; }
    // The clock the timeline is evaluated against this frame: audio time (render time without
    // audio) and beats from the beat clock.
    [[nodiscard]] const params::TimelineClock& timelineClock() const { return timelineClock_; }
    // Records a key for `path` at the current timeline time with the parameter's base value.
    params::Track* recordKey(const std::string& path, int component = -1,
                             params::KeyInterp interp = params::KeyInterp::Linear,
                             params::TimeBase base = params::TimeBase::Seconds);
    // Index of the cue currently in effect (-1 = none) and its morph progress.
    [[nodiscard]] params::Timeline::CueState cueState() const { return cueState_; }

    // Built-in signals published every frame: time.seconds, time.progress, time.playing,
    // beat.phase (per-frame extrapolated), beat.pulse (event), beat.count, beat.bpm, beat.bar.
    struct TimeSignals {
        signals::SignalId seconds = signals::kInvalidSignal;
        signals::SignalId progress = signals::kInvalidSignal;
        signals::SignalId playing = signals::kInvalidSignal;
        signals::SignalId beatPhase = signals::kInvalidSignal;
        signals::SignalId beatPulse = signals::kInvalidSignal;
        signals::SignalId beatCount = signals::kInvalidSignal;
        signals::SignalId bpm = signals::kInvalidSignal;
        signals::SignalId barPhase = signals::kInvalidSignal;
    };
    [[nodiscard]] const TimeSignals& timeSignals() const { return timeSignals_; }
    [[nodiscard]] const signals::SourceContext& sourceContext() const { return sourceContext_; }
    [[nodiscard]] bool hasAudio() const { return audioFile_ != nullptr; }
    [[nodiscard]] const std::filesystem::path& audioPath() const { return audioPath_; }
    [[nodiscard]] std::shared_ptr<const audio::AudioFile> audioFile() const { return audioFile_; }

    // Transport (Live mode; no-ops offline).
    [[nodiscard]] Result<void> play();
    void pause();
    void togglePlay();
    void stop();
    void seekSeconds(double seconds);
    [[nodiscard]] bool isPlaying() const;
    [[nodiscard]] double positionSeconds() const;
    [[nodiscard]] double durationSeconds() const;
    void setVolume(float volume);
    [[nodiscard]] float volume() const;

    // Produces this frame's time. Live: audio position while playing, free-running while paused.
    FrameTime tick(FrameClock& clock);

    // Evaluates the pipeline for one frame: signals -> modulation -> scene.
    void update(const FrameTime& time);

    // ---- accessors for UI / renderer / tests ----
    [[nodiscard]] const scene::Scene& scene() const { return controller_->scene(); }
    [[nodiscard]] scene::SceneController& controller() { return *controller_; }
    // The orb preset when it is the active controller (nullptr otherwise).
    [[nodiscard]] scene::OrbScene* orbScene() { return dynamic_cast<scene::OrbScene*>(controller_.get()); }
    [[nodiscard]] scene::GltfScene* gltfScene() { return dynamic_cast<scene::GltfScene*>(controller_.get()); }
    [[nodiscard]] params::ParameterSet& params() { return params_; }
    [[nodiscard]] params::Modulator& modulator() { return modulator_; }
    [[nodiscard]] signals::SignalBus& signals() { return bus_; }
    [[nodiscard]] const signals::AudioSignals& audioSignals() const { return audioSignals_; }
    [[nodiscard]] audio::AudioPlayer* player() { return player_.get(); }
    [[nodiscard]] analysis::AnalysisRunner* runner() { return runner_.get(); }
    [[nodiscard]] const analysis::AnalysisTrack* track() const { return track_.get(); }
    [[nodiscard]] const analysis::AnalysisFrame& latestFrame() const { return latest_; }
    [[nodiscard]] bool hasFrame() const { return hasFrame_; }
    [[nodiscard]] const EngineStats& stats() const { return stats_; }
    [[nodiscard]] const analysis::AnalyzerConfig& analyzerConfig() const { return analyzerConfig_; }

    // Per-route "response" convenience used by the UI: amount of the route targeting `path`.
    [[nodiscard]] params::ModRoute* routeForTarget(const std::string& path);

private:
    void publishFrame(const analysis::AnalysisFrame& frame);
    void installController(std::unique_ptr<scene::SceneController> controller);
    Result<void> reapplyEnvironment();
    void updateTimeSignals(const FrameTime& time, bool newAnalysisFrame);
    void addDefaultPostRoutes();
    void updateTimelineClock(const FrameTime& time);
    void applyCues();
    void detachSceneParameters(); // before params_.clear(): composition, shader layers, timeline
    void ensureControlSource();   // the "control" source exists in the rack and its channels are declared

    EngineMode mode_;
    params::ParameterSet params_;
    signals::SignalBus bus_;
    signals::AudioSignals audioSignals_;
    params::Modulator modulator_;
    signals::SourceRack sources_;
    params::PresetBank presets_;
    params::Timeline timeline_;
    RenderSettings render_;
    ControlHub controlHub_;
    nlohmann::json outputs_;
    TempoSource tempoSource_ = TempoSource::Analysis;
    bool midiClockActive_ = false;
    std::unique_ptr<audio::AudioInput> input_;
    params::Parameter<float>* inputGain_ = nullptr;
    params::TimelineClock timelineClock_;
    params::Timeline::CueState cueState_;
    params::Preset cueFrom_;      // base values captured when the current cue started (morphs)
    bool cueApplied_ = false;     // the current cue's preset has been applied at full weight
    shaders::ShaderLayerSet shaderLayers_;
    scene::PostSettings post_;
    scene::PostParameters postParams_;
    TimeSignals timeSignals_;
    signals::SourceContext sourceContext_;
    assets::AssetRegistry registry_;
    std::unique_ptr<scene::SceneController> controller_;
    std::filesystem::path environmentPath_;
    std::filesystem::path compositionPath_;
    std::filesystem::path projectPath_;
    std::vector<std::string> projectWarnings_;
    // Beat clock extrapolated per render frame from the analysis tempo (ADR-012).
    double beatClockPhase_ = 0.0;
    std::uint32_t beatClockCount_ = 0;
    std::uint32_t lastAnalysisBeatCount_ = 0;

    std::shared_ptr<const audio::AudioFile> audioFile_;
    std::filesystem::path audioPath_;
    analysis::AnalyzerConfig analyzerConfig_;
    std::unique_ptr<audio::AudioPlayer> player_;
    std::unique_ptr<analysis::AnalysisRunner> runner_;
    std::unique_ptr<analysis::AnalysisTrack> track_;
    std::size_t offlineFrameCursor_ = 0;

    analysis::AnalysisFrame latest_;
    bool hasFrame_ = false;
    double lastRenderTime_ = 0.0;
    EngineStats stats_;
};

} // namespace avgen::app
