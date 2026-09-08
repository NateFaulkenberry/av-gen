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
#include "scene/orb_scene.hpp"
#include "signals/audio_signals.hpp"
#include "signals/signal_bus.hpp"

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
    [[nodiscard]] const scene::Scene& scene() const { return orbScene_.scene(); }
    [[nodiscard]] scene::OrbScene& orbScene() { return orbScene_; }
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

    EngineMode mode_;
    params::ParameterSet params_;
    signals::SignalBus bus_;
    signals::AudioSignals audioSignals_;
    params::Modulator modulator_;
    scene::OrbScene orbScene_;

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
