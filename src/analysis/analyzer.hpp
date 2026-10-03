#pragma once

// Streaming STFT feature extractor (ADR-004). Deterministic: identical input samples produce
// identical frames regardless of how the input is chunked. The raw features are published unsmoothed
// and shaped by the modulation chain; the three ADR-897 features that are defined by a time
// constant (the long-term band levels, the onset rate and the energy composite) carry their own.

#include "analysis/causal_onsets.hpp"

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

    // ADR-897: the features that survive a flat master.
    // Long-term band levels: each band's power through a one-pole of this time constant, read on a
    // fixed dB scale (never divided by a running maximum).
    float levelSmoothingSeconds = 1.0f;
    // The energy composite's one-pole, and the leaky window the onset rate is counted over.
    float energySmoothingSeconds = 1.0f;
    float onsetRateSeconds = 2.0f;

    static std::vector<BandDefinition> defaultBands();
};

// ADR-897: where a long-term band level of 0 sits. 1 is 0 dB -- a full-scale sine in the band -- so
// the scale is 60 dB wide and 6 dB is 0.1.
constexpr float kBandLevelFloorDb = -60.0f;
// The periodic Hann window's equivalent noise bandwidth, in bins: a sine of amplitude A leaves
// 1.5 A^2 in the sum of its bins' squared (sine-normalised) magnitudes. Dividing a band's summed
// power by it reads the band in sine-amplitude units, so a full-scale sine is 0 dB.
constexpr double kHannEnergyGain = 1.5;
// `power`: a band's summed squared magnitude divided by kHannEnergyGain. Returns 0..1.
[[nodiscard]] float bandLevelFromPower(double power);

// ADR-897: the loudness-independent energy composite, 0..1, from the ingredients of the Rebuild
// analysis (docs/glowmere-valley-3/01-music.md §1.2-1.3) that do not move with the master's level:
//
//   high band   power above 2 kHz relative to the whole spectrum, -36..-12 dB    weight 2
//   brightness  spectral centroid, log-scaled 250 Hz..6 kHz                       weight 2
//   flux        spectral flux relative to the frame's magnitude, 0.08..0.24      weight 1
//   density     percussive onsets per second, 2..12                              weight 1 (when known)
//   width       stereo side/mid RMS, 0.1..0.4                                     weight 1 (when known)
//
// Each term is clamped to 0..1 on its fixed range and the weighted mean is taken over the terms
// that are known: live input has no stereo and no percussive onset rate, and a missing term is left
// out rather than read as zero. Silence (no spectral power) is 0. Unsmoothed; callers smooth it.
struct EnergyTerms {
    float highRatioDb = -120.0f;
    float centroidHz = 0.0f;
    float relativeFlux = 0.0f;
    float onsetRate = 0.0f;
    float width = 0.0f;
    bool hasOnsetRate = false;
    bool hasWidth = false;
    bool silent = true;
};
[[nodiscard]] float energyComposite(const EnergyTerms& terms);

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

    // ---- ADR-897: features that survive a flat master ---------------------------------------------
    // Each band's power smoothed over `levelSmoothingSeconds` and read on the fixed scale
    // `bandLevelFromPower` defines (0 = -60 dB, 1 = 0 dB). Never auto-gained: a passage 5 dB quieter
    // reads 0.083 lower for as long as it lasts, which `bands` above forgets within seconds.
    std::array<float, kMaxBands> bandLevels{};
    float highRatioDb = -120.0f; // power above 2 kHz relative to the whole spectrum, dB
    float relativeFlux = 0.0f;   // the unclamped flux over the frame's summed magnitude (level-free)
    float width = 0.0f;          // stereo side/mid RMS over the window; meaningful when `stereo`
    bool stereo = false;
    // Onsets per second through a leaky window of `onsetRateSeconds`. Offline (AnalysisTrack) it
    // counts the percussive band onsets below; live input, which has none, counts `onset`.
    float onsetRate = 0.0f;
    bool onsetRateIsPercussive = false;
    // The energy composite (`energyComposite`), smoothed over `energySmoothingSeconds`, 0..1.
    float energy = 0.0f;

    // ---- ADR-898: band-limited onsets (offline: AnalysisTrack's post-pass) ------------------------
    // low: a kick -- a percussive attack in 100-300 Hz, told from a bass note by its percussive share;
    // mid: a snare or clap -- 2-6 kHz; high: a hat -- 6-16 kHz. Strength is the attack against the
    // strongest of its band in the surrounding second, 0..1.
    bool lowOnset = false;
    float lowOnsetStrength = 0.0f;
    bool midOnset = false;
    float midOnsetStrength = 0.0f;
    bool highOnset = false;
    float highOnsetStrength = 0.0f;

    // ---- ADR-1060: causal onsets (live: AnalysisRunner; file: AnalysisTrack's pass, the same detector) ---------
    CausalOnsets causal;
    // Live only: the frame index each event above was first raised on. The runner carries an event forward until
    // the render thread has acquired a frame that holds it, so one between two render frames is not lost; the
    // consumer fires an event only when its stamp is newer than the last it fired (`LiveEventLatch`). 0 = none.
    std::uint64_t onsetStamp = 0, beatStamp = 0, lowStamp = 0, midStamp = 0, highStamp = 0;
    std::uint64_t liveSerial = 0; // live only: this frame's position in the runner's output, from 1
};

// ADR-1060: the render thread's half of the live event carry (see `AnalysisFrame::onsetStamp`). An event whose stamp
// is not newer than the last one fired for its kind was already seen on an earlier frame, and is cleared.
struct LiveEventLatch {
    std::uint64_t onset = 0, beat = 0, low = 0, mid = 0, high = 0;
    std::uint64_t serial = 0; // the newest frame seen: a smaller one means a new runner, and the latch restarts
    void apply(AnalysisFrame& frame);
};

class Analyzer {
public:
    explicit Analyzer(AnalyzerConfig config);

    [[nodiscard]] const AnalyzerConfig& config() const { return config_; }
    [[nodiscard]] std::size_t binCount() const { return config_.windowSize / 2 + 1; }
    [[nodiscard]] float binHz(std::size_t bin) const;

    // Feed mono samples in any chunk size. `side` -- (left - right) / 2, one per mono sample -- is
    // optional; when every push carries it the frames measure stereo width (ADR-897).
    void push(std::span<const float> mono, std::span<const float> side = {});
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
    std::vector<float> sideFifo_; // the side channel, parallel to fifo_ while stereo_
    bool stereo_ = true;          // every push so far carried a side channel
    std::uint64_t fifoStartFrame_ = 0;
    std::deque<AnalysisFrame> ready_;
    std::vector<float> previousMagnitude_;
    std::deque<float> fluxHistory_;
    std::array<float, kMaxBands> bandPeak_{};
    double lastOnsetTime_ = -1.0;
    float lastFlux_ = 0.0f;
    float prevFlux_ = 0.0f;
    bool havePrevious_ = false;
    // ADR-897 state: the smoothed band powers, the leaky onset count and the smoothed composite.
    std::array<double, kMaxBands> bandPower_{};
    float onsetRate_ = 0.0f;
    float energy_ = 0.0f;
    bool haveLevels_ = false;
};

} // namespace avgen::analysis
