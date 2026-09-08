#pragma once

// Streaming STFT feature extractor (ADR-004). Deterministic: identical input samples produce
// identical frames regardless of how the input is chunked. No smoothing is applied here; raw
// features are published and shaped by the modulation chain.

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace avgen::analysis {

struct BandDefinition {
    std::string name;
    float lowHz;
    float highHz;
};

constexpr std::size_t kMaxBands = 8;

struct AnalyzerConfig {
    std::uint32_t sampleRate = 48000;
    std::uint32_t windowSize = 2048;
    std::uint32_t hopSize = 512;
    std::vector<BandDefinition> bands = defaultBands();

    // Band normalisation: running maximum with exponential decay (seconds to fall to 1/e).
    float normalizationDecaySeconds = 4.0f;
    float normalizationFloor = 1e-4f; // prevents silence from normalising noise up to 1

    // Onset detection (spectral-flux based): threshold = median(history) * scale + delta.
    std::uint32_t onsetMedianFrames = 11;
    float onsetThresholdScale = 1.5f;
    float onsetThresholdDelta = 0.02f;
    float minOnsetIntervalSeconds = 0.06f;

    static std::vector<BandDefinition> defaultBands();
};

struct AnalysisFrame {
    std::uint64_t frameIndex = 0;   // PCM frame index at the window centre
    double timeSeconds = 0.0;       // frameIndex / sampleRate
    float rms = 0.0f;               // 0..1 (full-scale sine = 0.707)
    float peak = 0.0f;              // 0..1
    std::uint32_t bandCount = 0;
    std::array<float, kMaxBands> bandsRaw{}; // linear band amplitude (0..~1)
    std::array<float, kMaxBands> bands{};    // normalised 0..1 by running max
    float centroidHz = 0.0f;
    float centroidNorm = 0.0f;      // log-frequency position 0..1 between 20 Hz and Nyquist
    float flux = 0.0f;              // half-wave-rectified spectral flux, 0..~1
    float onsetStrength = 0.0f;     // flux relative to adaptive threshold (>=1 means over)
    bool onset = false;             // peak-picked onset event in this hop
    // Beat tracking (filled by AnalysisRunner live / AnalysisTrack offline, not by Analyzer).
    float tempoBpm = 0.0f;          // 0 while unknown
    float tempoConfidence = 0.0f;   // 0..1
    bool beat = false;              // a beat lands in this hop
    float beatPhase = 0.0f;         // 0..1 since the last beat
    std::uint32_t beatCount = 0;    // beats since the last reset
    std::vector<float> magnitude;   // binCount linear magnitudes, sine-normalised
    std::vector<float> spectrum;    // binCount log-compressed 0..1 for display
};

class Analyzer {
public:
    explicit Analyzer(AnalyzerConfig config);

    [[nodiscard]] const AnalyzerConfig& config() const { return config_; }
    [[nodiscard]] std::size_t binCount() const { return config_.windowSize / 2 + 1; }
    [[nodiscard]] float binHz(std::size_t bin) const;

    // Feed mono samples in any chunk size.
    void push(std::span<const float> mono);
    // Pops the oldest completed frame. Returns false when none is ready.
    bool pop(AnalysisFrame& out);
    [[nodiscard]] std::size_t pendingFrames() const { return ready_.size(); }

    // Clears all history and declares that the next pushed sample is PCM frame `startFrameIndex`.
    void reset(std::uint64_t startFrameIndex = 0);

private:
    void computeFrame();

    AnalyzerConfig config_;
    struct Impl;
    std::shared_ptr<Impl> impl_; // FFT + scratch, shared_ptr to keep Analyzer movable
    std::vector<float> fifo_;    // pending input samples
    std::uint64_t fifoStartFrame_ = 0;
    std::deque<AnalysisFrame> ready_;
    std::vector<float> previousMagnitude_;
    std::deque<float> fluxHistory_;
    std::array<float, kMaxBands> bandPeak_{};
    double lastOnsetTime_ = -1.0;
    float lastFlux_ = 0.0f;
    float prevFlux_ = 0.0f;
    bool havePrevious_ = false;
};

} // namespace avgen::analysis
