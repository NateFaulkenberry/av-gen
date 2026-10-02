#pragma once

// Causal onsets: kick, snare, hat, a bass attack and a broadband onset, the same live and from a file (ADR-1060).
//
// ADR-898's band onsets are offline: centred medians, a relative test over +-1 s, harmonic/percussive separation
// with look-ahead. They are the better answer for a rendered file, and live input never had them, so a live drum
// kit gave the picture one broadband onset and nothing that knew a kick from a hat. This detector is causal: every
// value it produces for frame n is a function of frames 0..n only, so it runs on the analysis thread live and over
// the file at load, in the same order, with the same answer (the Sonic response model reads it on both paths).
//
// Per frame:
//   Y(k)      = log(1 + 100 |X(k)|)                                   level-free compression (FMP, madmom)
//   flux(k)   = max(0, Y(k) - max(Y'(k-1), Y'(k), Y'(k+1)))           SuperFlux: the previous frame max-filtered
//                                                                     across 3 bins, so vibrato is not an onset
//   ODF(band) = mean of flux(k) over the band's bins
//   ratio     = ODF / (lambda x median(ODF over the last M frames) + delta)   >= 1 is "over its own background"
//
// The classes (research/02-modulation.md, section 2):
//   low    40-120 Hz: any low attack, a kick or a bass note (the brief's "single bass note -> impact").
//   kick   a low attack that arrives with a broadband click (the onset over its threshold on this frame or the
//          last) while the 30-150 Hz band holds most of the frame's power: a drum's strike splatters across the
//          spectrum, a bass note's attack and a low beating do not, and a hat's noise is not low.
//          A pure-sine 808 with no click is a `low`, not a `kick`.
//   snare  1.5-5 kHz noise (with the 150-300 Hz body when present) that holds a real share of the frame's power:
//          a kick's click puts about -26..-37 dB of the total there, a bass pluck and a hat about -20, a snare about
//          -13..-17 (a click is a millisecond in a 43 ms window). In a dense mix whose other parts fill that band a
//          snare reads weaker: `snareRise` (the band over its own floor) is published for a route that wants it.
//   hat    7-16 kHz, holding at least about a third of the frame's new energy (summed log-flux).
//   onset  30 Hz-16 kHz.
//
// The ratios are published continuously; a `HitPicker` turns a ratio into events (threshold, rising edge,
// hysteresis, refractory time). `CausalOnsetDetector` runs one with fixed defaults per class, which is what live
// `audio.onsetLow/Mid/High` fire on. The Sonic response model runs its own pickers over the same ratios with the
// performer's sensitivity, so the detector itself has no parameters a live control could change.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace avgen::analysis {

struct AnalysisFrame;

enum class HitClass : std::uint8_t { Kick = 0, Low, Snare, Hat, Onset, Count };
inline constexpr std::size_t kHitClassCount = static_cast<std::size_t>(HitClass::Count);

// What the detector says about one frame. Stored on `AnalysisFrame::causal`.
struct CausalOnsets {
    bool valid = false;                        // the detector ran on this frame
    std::array<float, kHitClassCount> ratio{}; // each class's ODF over its causal threshold (>= 1 is over); 0 silent
    std::array<bool, kHitClassCount> hit{};    // the default picker fired on this frame
    std::array<float, kHitClassCount> strength{}; // 0..1 when hit: (ratio - fire) / span
    float flux = 0.0f;        // the broadband ODF (mean log-compressed flux per bin), raw
    std::array<float, kHitClassCount> odf{}; // each class's raw detection function (mean log-flux per bin)
    float snareDb = -120.0f;  // 1.5-5 kHz power in dBFS
    float hatDb = -120.0f;    // 7-16 kHz power in dBFS
    float bassDb = -120.0f;   // 30-150 Hz power in dBFS (a full-scale sine in the band is 0 dB)
    float levelDb = -120.0f;  // 30 Hz-16 kHz power in dBFS
    std::array<float, 6> bandFlux{}; // summed flux in 40-120, 120-400, 400-1500, 1.5-5k, 5-7k, 7-16k Hz
    float snareRise = 0.0f;   // 1.5-5 kHz power over its own recent floor, dB
    float bodyRatio = 0.0f;   // 150-300 Hz flux per bin over the 1.5-5 kHz band's (a snare's body)
    float kickShape = 0.0f;   // kick-band flux / (kick-band + harmonic-band flux), 0..1
};

// Turns a ratio into events: fires when the ratio reaches `fire` while armed, rising, and at least `refractory`
// seconds after the last hit; re-arms below `rearm` x fire. Strength = clamp((ratio - fire) / span + floor, 0, 1).
struct HitPicker {
    float fire = 1.6f;
    float rearm = 0.75f;
    float span = 3.0f;
    float floor = 0.15f;
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

// The class's default picker (the live `audio.onset*` firing and the response model's neutral sensitivity).
[[nodiscard]] HitPicker defaultHitPicker(HitClass c);

struct CausalOnsetConfig {
    int medianFrames = 31;   // ~0.33 s at a 512 hop: two kicks a quarter-second apart leave it mostly quiet
    float lambda = 1.4f;
    // The absolute part of the threshold, in mean log-flux per bin: below it a band is quiet whatever its median
    // says (a fade, a room). Per class: the low bands have few bins and much energy, the high bands many and little.
    std::array<float, kHitClassCount> delta{0.020f, 0.020f, 0.008f, 0.006f, 0.006f};
    // Dixon's decaying peak: the threshold is also at least `peakShare` of the band's recent peak ODF, which falls
    // with `peakSeconds`. A bump on the tail of a big attack (a kick's tail beating against a bass note) is not a hit.
    float peakShare = 0.12f;
    double peakSeconds = 0.12;
};

class CausalOnsetDetector {
public:
    explicit CausalOnsetDetector(CausalOnsetConfig config = {});

    // Fills `frame.causal` from `frame.magnitude`. `binHz` is the analyzer's bin spacing, `hopSeconds` its hop.
    // Frames must come in order; `reset` at a discontinuity.
    void process(AnalysisFrame& frame, float binHz, double hopSeconds);
    void reset();

    [[nodiscard]] const CausalOnsetConfig& config() const { return config_; }

private:
    struct Band {
        std::size_t from = 0, to = 0; // [from, to) bins
    };
    void layout(std::size_t bins, float binHz);
    [[nodiscard]] float bandMean(const Band& b) const;
    [[nodiscard]] float bandPower(const AnalysisFrame& frame, const Band& b) const;

    CausalOnsetConfig config_;
    std::size_t bins_ = 0;
    float binHz_ = 0.0f;
    Band kick_, harmonic_, body_, snare_, hat_, broad_, bass_;
    std::array<Band, 6> split_{};
    std::vector<float> previous_; // Y of the previous frame
    std::vector<float> flux_;     // this frame's per-bin flux
    bool havePrevious_ = false;
    // Per class: the ODF history for the median (a ring), and the default picker.
    std::array<std::vector<float>, kHitClassCount> history_{};
    std::array<std::size_t, kHitClassCount> head_{};
    std::array<HitPicker, kHitClassCount> pickers_{};
    std::array<float, kHitClassCount> peak_{};
    float previousOnsetRatio_ = 0.0f;
    float snareFloorDb_ = -120.0f;
    std::vector<float> scratch_;
};

// The whole track in order (AnalysisTrack's pass): one fresh detector over every frame.
void detectCausalOnsets(std::span<AnalysisFrame> frames, float binHz, double hopSeconds,
                        const CausalOnsetConfig& config = {});

[[nodiscard]] const char* hitClassName(HitClass c);

} // namespace avgen::analysis
