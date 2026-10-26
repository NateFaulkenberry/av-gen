#pragma once

// Causal onsets: a low-register attack and a broadband onset, the same live and from a file (ADR-1060).
//
// This detector is causal: every value it produces for frame n is a function of frames 0..n, so it runs on the
// analysis thread live and over the file at load, in the same order, with the same answer. It names no instrument:
// its two classes say where an attack is, not what made it.
//
// Per frame:
//   Y(k)  = log(1 + 40 |X(k)| / ref)                    compression relative to a causal spectral peak (level-free)
//   r(k)  = max(0, Y(k) - max(Y'(k-1..k+1)))            SuperFlux: the previous frame max-filtered across 3 bins
//
// The classes:
//   low    40-120 Hz flux over its own causal median: any low attack (a kick, a bass note, a tom alike).
//   onset  30 Hz-16 kHz flux over its own causal median: any attack.
//
// `ratio` is published continuously per class, `hit` is the decision and `strength` its strength. The Sonic response
// model takes these decisions and shapes their strength with the performer's sensitivity; the detector itself has no
// parameters a live control could change.
//
// A drums-role source's kick, snare and hat are not here: `DrumClassifier` (analysis/drum_classifier.hpp) runs on an
// isolated drum source only, over the flux this detector computes.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace avgen::analysis {

struct AnalysisFrame;

enum class HitClass : std::uint8_t { Low = 0, Onset, Count };
inline constexpr std::size_t kHitClassCount = static_cast<std::size_t>(HitClass::Count);

// What the detector says about one frame. Stored on `AnalysisFrame::causal`.
struct CausalOnsets {
    bool valid = false;                           // the detector ran on this frame
    std::array<float, kHitClassCount> ratio{};    // each class's ODF over its causal threshold (>= 1 is over)
    std::array<bool, kHitClassCount> hit{};       // decided on this frame
    std::array<float, kHitClassCount> strength{}; // 0..1 when hit
    std::array<float, kHitClassCount> odf{};      // each class's detection function
    float flux = 0.0f;         // the broadband ODF
    float bassDb = -120.0f;    // 30-150 Hz power in dBFS (a full-scale sine in the band is 0 dB)
    float levelDb = -120.0f;   // 30 Hz-16 kHz power in dBFS
    // ADR-1723: the mid (250 Hz-4 kHz) and high (4-16 kHz) band powers in dBFS, ADR-1604's register bands
    float midDb = -120.0f;
    float highDb = -120.0f;
    // ADR-1604: the register of this hop's attack -- how the power that arrived since the previous hop divides between
    // low (30-250 Hz), mid (250 Hz-4 kHz) and high (4-16 kHz), summing to 1 (all 0 when no band gained). Known on the
    // onset's own hop.
    std::array<float, 3> onsetRegister{};
};

// ADR-1604: the low/mid/high shares of the power gained between two magnitude spectra (bin k at k x binHz): each
// band's increase in summed squared magnitude, floored at 0 and normalised to sum to 1; all 0 when no band gained.
// Energy-weighted. Shared by the mix (its causal onsets and its picked onset) and the stems.
[[nodiscard]] std::array<float, 3> registerShares(std::span<const float> now, std::span<const float> previous,
                                                  float binHz);

// Turns a ratio into events: fires when the ratio reaches `fire` while armed, rising, and at least `refractory`
// seconds after the last hit; re-arms below `rearm` x fire. Strength = floor + (1 - floor)(1 - fire/ratio): a hit
// that fires always shows (the floor), and twice the threshold is halfway to 1.
struct HitPicker {
    float fire = 1.6f;
    float rearm = 0.75f;
    float floor = 0.35f;
    double refractory = 0.06;

    bool armed = true;
    float previous = 0.0f;
    double since = 1e9;

    // One frame, `dt` seconds after the previous. Returns the strength (> 0) when it fires, else 0.
    float step(float ratio, double dt);
    void reset() {
        armed = true;
        previous = 0.0f;
        since = 1e9;
    }
};

[[nodiscard]] HitPicker defaultHitPicker(HitClass c);

// A detection function over its causal threshold: lambda x the median of the previous `frames` values + delta. Shared
// with the drum classifier.
class CausalThreshold {
public:
    void reset(int frames);
    // The ratio of `odf` to the threshold the frames before it set; then `odf` joins the history.
    [[nodiscard]] float ratio(float odf, float lambda, float delta);

private:
    std::vector<float> history_;
    std::vector<float> scratch_;
    std::size_t head_ = 0;
};

// The mean of `v` over bins [from, to) (0 for an empty band), and a band's power in dBFS (a full-scale sine 0 dB):
// shared by the detector and the drum classifier.
[[nodiscard]] float bandMean(std::span<const float> v, std::size_t from, std::size_t to);
[[nodiscard]] float bandPowerDb(std::span<const float> magnitude, std::size_t from, std::size_t to);
// The bin range [from, to) of lo..hi Hz at `binHz`, clamped to `bins` and at least one bin wide.
struct BinRange {
    std::size_t from = 0, to = 0;
};
[[nodiscard]] BinRange binRange(float lo, float hi, std::size_t bins, float binHz);

struct CausalOnsetConfig {
    int medianFrames = 31;  // the ODFs' causal median, ~0.33 s at a 512 hop
    float lambda = 1.4f;
    // The absolute part of each threshold, per class (low, onset).
    std::array<float, kHitClassCount> delta{0.020f, 0.006f};
};

class CausalOnsetDetector {
public:
    explicit CausalOnsetDetector(CausalOnsetConfig config = {});

    // Fills `frame.causal` from `frame.magnitude`. `binHz` is the analyzer's bin spacing, `hopSeconds` its hop.
    // Frames must come in order; `reset` at a discontinuity.
    void process(AnalysisFrame& frame, float binHz, double hopSeconds);
    void reset();

    // The last processed frame's compressed flux r(k), one per bin (all 0 on the first frame after a reset): what
    // `DrumClassifier` reads.
    [[nodiscard]] std::span<const float> binFlux() const { return flux_; }
    // Whether the last processed frame was the first since a reset (its flux is all 0 and its ratios read 0).
    [[nodiscard]] bool first() const { return first_; }

    [[nodiscard]] const CausalOnsetConfig& config() const { return config_; }

private:
    void layout(std::size_t bins, float binHz);

    CausalOnsetConfig config_;
    std::size_t bins_ = 0;
    float binHz_ = 0.0f;
    BinRange low_, broad_, bass_, mid_, high_; // ADR-1723: mid, high
    std::vector<float> previous_;   // the previous frame's magnitudes
    float reference_ = 0.0f;        // the causal spectral peak the compression is relative to
    std::vector<float> flux_;       // r(k)
    bool havePrevious_ = false;
    bool first_ = true;
    std::array<CausalThreshold, kHitClassCount> thresholds_{};
    std::array<HitPicker, kHitClassCount> pickers_{};
};

// The whole track in order (AnalysisTrack's pass): one fresh detector over every frame.
void detectCausalOnsets(std::span<AnalysisFrame> frames, float binHz, double hopSeconds,
                        const CausalOnsetConfig& config = {});

[[nodiscard]] const char* hitClassName(HitClass c);

} // namespace avgen::analysis
