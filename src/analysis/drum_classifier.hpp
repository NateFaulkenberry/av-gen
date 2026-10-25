#pragma once

// The kick / snare / hat classifier of an isolated drum source (ADR-1060, rebuilt by ADR-1067; ADR-1630: a
// `drums`-role stem or live input only). It never runs on the main mix: on a mix it cannot tell a kick from a bass note
// or a snare from a pluck, so the mix publishes only where an attack is (`CausalOnsetDetector`), never what made it.
//
// It reads the frame's magnitudes and the compressed flux r(k) the source's `CausalOnsetDetector` computed for the same
// frame (`CausalOnsetDetector::binFlux`), and adds:
//   P(k)  = median of r over k-8..k+8                   PERCUSSIVE flux: the frequency-median half of HPSS. A pad's
//                                                       or a bass line's change lives in a few partials and is
//                                                       removed; a drum's attack is broad and survives.
//   N     = mean over 1.5-5 kHz of the median of |X| over k-8..k+8    the band's NOISE FLOOR (partials ignored)
//
//   kick   decided one hop late on a local maximum of the low band's rise (the larger of 40-120 and 30-70 Hz, in
//          dB, over the band's minimum of the previous three hops, floored at -60 dBFS, and only once the band
//          reaches -45 dBFS): score = min(rise/6, 1) + min(click/0.01, 1.5) + 0.9 x onGrid, a kick when the score is
//          1.6 or more (or the rise is 8 dB and there is a click).
//            click   the percussive flux in 1.5-5 kHz around the attack
//            onGrid  the attack is within 30 ms of the period the recent kicks keep
//   snare  a percussive-flux attack in 1.5-5 kHz, confirmed kDeferFrames hops later by its noise floor N standing
//          4.5 dB or more over its level just before and within 6 dB of its peak at the attack.
//   hat    a percussive-flux attack in 7-16 kHz whose band carries at least 0.6 of the 1.5-5 kHz band's.
//
// Causal, deterministic, and reset with its detector.

#include "analysis/causal_onsets.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace avgen::analysis {

enum class DrumClass : std::uint8_t { Kick = 0, Snare, Hat, Count };
inline constexpr std::size_t kDrumClassCount = static_cast<std::size_t>(DrumClass::Count);
// How many hops after its attack a snare is confirmed (43 ms at a 512 hop and 48 kHz).
inline constexpr int kDeferFrames = 4;

// What the classifier decided on one frame.
struct DrumHits {
    std::array<bool, kDrumClassCount> hit{};
    std::array<float, kDrumClassCount> strength{}; // 0..1 when hit
};

struct DrumClassifierConfig {
    int medianFrames = 31;  // the ODFs' causal median, ~0.33 s at a 512 hop
    float lambda = 1.4f;
    float snareDelta = 0.0024f; // the absolute part of the snare's and the hat's thresholds
    float hatDelta = 0.0018f;
    int percussiveHalfWidth = 8;  // bins either side of the frequency median
    // The kick.
    float kickRiseScale = 6.0f;   // dB of rise for a score of 1
    float kickRiseCap = 1.0f;     // the most the rise alone can give: a low attack with no click is not a kick
    float kickClickScale = 0.01f; // percussive flux for a score of 1
    float kickGrid = 0.9f;        // the period's vote
    float kickThreshold = 1.6f;
    float kickStrongRise = 8.0f;  // dB: with any click (> kickStrongClick) a kick whatever the score
    float kickStrongClick = 0.003f;
    float kickCandidateRise = 2.5f; // dB: the least rise that is a candidate
    // The snare's confirmation.
    float snareNoiseRise = 4.5f;  // dB the noise floor must stand over its level before the attack
    // ADR-1068: timbre gates on the snare and the hat.
    float noiseShare = 0.8f;      // the snare's least share of its band's flux that is percussive
    float hatTilt = 0.6f;         // the hat's band over the snare's
};

class DrumClassifier {
public:
    explicit DrumClassifier(DrumClassifierConfig config = {});

    // One frame: its magnitudes and the compressed flux its `CausalOnsetDetector` computed for it (`first` when that
    // was the first frame since the detector's reset). Frames in order; `reset` with the detector.
    [[nodiscard]] DrumHits process(std::span<const float> magnitude, std::span<const float> flux, bool first,
                                   float binHz, double hopSeconds);
    void reset();

private:
    void layout(std::size_t bins, float binHz);
    void decideKick(DrumHits& out, double hop);

    DrumClassifierConfig config_;
    std::size_t bins_ = 0;
    float binHz_ = 0.0f;
    BinRange low_, sub_, snare_, hat_, broad_;
    std::vector<float> percussive_; // P(k)
    std::vector<float> window_;     // scratch for the frequency medians
    double seconds_ = 0.0;          // this frame's time on the classifier's own clock (frames x hop)
    std::uint64_t frames_ = 0;
    CausalThreshold snareThreshold_, hatThreshold_;
    HitPicker snarePicker_, hatPicker_;
    // The kick: three hops of each low band's dB, the last two rises, the last clicks, the accepted kicks.
    std::array<float, 4> lowDb_{}, subDb_{};
    std::array<float, 3> rise_{};
    std::array<float, 5> click_{}; // the clicks of the four hops before this one and this one
    double lastKick_ = -1e9;
    std::vector<double> kicks_;
    // The snare: the noise floor's recent levels, and attacks awaiting confirmation.
    std::array<float, kDeferFrames + 3> noiseHistory_{};
    struct Pending {
        int age = 0;
        float before = 0.0f;
        float strength = 0.0f;
        float peak = -120.0f; // the noise floor's peak over the attack's first two hops
    };
    std::vector<Pending> pending_;
};

[[nodiscard]] const char* drumClassName(DrumClass c);

} // namespace avgen::analysis
