#pragma once

// The audiovisual pipeline (architecture.md): AudioPlayer -> AnalysisRunner -> SignalBus ->
// Modulator -> ParameterSet -> OrbScene -> Scene. The renderer and UI sit beside it and only
// read the Scene / parameters. Two modes (ADR-012): Live (audio device is the clock, analysis
// runs on a thread) and Offline (fixed-step clock, analysis precomputed and indexed by time).

#include "analysis/analysis_runner.hpp"
#include "analysis/analysis_track.hpp"
#include "analysis/analyzer.hpp"
#include "audio/audio_file.hpp"
#include "audio/audio_player.hpp"
#include "core/error.hpp"
#include "core/time.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "params/preset.hpp"
#include "scene/gltf_scene.hpp"
#include "scene/orb_scene.hpp"
#include "scene/scene_controller.hpp"
#include "shaders/shader_layers.hpp"
#include "signals/audio_signals.hpp"
#include "signals/signal_bus.hpp"
#include "signals/source.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace avgen::app {

enum class EngineMode { Live, Offline };

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
    // Loads an equirectangular HDR and installs it as the current scene's environment map.
    [[nodiscard]] Result<void> loadEnvironment(const std::filesystem::path& path);
    [[nodiscard]] const std::filesystem::path& environmentPath() const { return environmentPath_; }
    // Routes any supported file by extension: audio, .gltf/.glb, .hdr, .json (project).
    [[nodiscard]] Result<void> loadFile(const std::filesystem::path& path);

    // ---- project (parameters, routes, sources, presets) ----
    [[nodiscard]] Result<void> saveProject(const std::filesystem::path& path) const;
    [[nodiscard]] Result<void> loadProject(const std::filesystem::path& path);
    [[nodiscard]] const std::filesystem::path& projectPath() const { return projectPath_; }

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

    EngineMode mode_;
    params::ParameterSet params_;
    signals::SignalBus bus_;
    signals::AudioSignals audioSignals_;
    params::Modulator modulator_;
    signals::SourceRack sources_;
    params::PresetBank presets_;
    shaders::ShaderLayerSet shaderLayers_;
    TimeSignals timeSignals_;
    signals::SourceContext sourceContext_;
    std::unique_ptr<scene::SceneController> controller_;
    std::filesystem::path environmentPath_;
    std::filesystem::path projectPath_;
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
