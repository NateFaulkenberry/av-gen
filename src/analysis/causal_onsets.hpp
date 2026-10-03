#pragma once

// Causal onsets: kick, snare, hat, a bass attack and a broadband onset, the same live and from a file (ADR-1060,
// rebuilt for dense mixes by ADR-1067).
//
// ADR-898's band onsets are offline (centred medians, look-ahead HPSS). This detector is causal: every value it
// produces for frame n is a function of frames 0..n, so it runs on the analysis thread live and over the file at
// load, in the same order, with the same answer.
//
// Per frame:
//   Y(k)  = log(1 + 40 |X(k)| / ref)                    compression relative to a causal spectral peak (level-free)
//   r(k)  = max(0, Y(k) - max(Y'(k-1..k+1)))            SuperFlux: the previous frame max-filtered across 3 bins
//   P(k)  = median of r over k-8..k+8                   PERCUSSIVE flux: the frequency-median half of HPSS. A pad's
//                                                       or a bass line's change lives in a few partials and is
//                                                       removed; a drum's attack is broad and survives.
//   N     = mean over 1.5-5 kHz of the median of |X| over k-8..k+8    the band's NOISE FLOOR (partials ignored)
//
// The classes (research/02-modulation.md section 2; ADR-1067 for the measurements behind each rule):
//   low    40-120 Hz raw flux over its own causal median: any low attack, a kick or a bass note.
//   kick   decided one hop late on a local maximum of the low band's rise (the larger of 40-120 and 30-70 Hz, in
//          dB, over the band's minimum of the previous three hops, floored at -60 dBFS, and only once the band
//          reaches -45 dBFS): score = min(rise/6, 1) + min(click/0.01, 1.5)
//          + 0.9 x onGrid, a kick when the score is 1.6 or more (or the rise is 8 dB and there is a click).
//            click   the percussive flux in 1.5-5 kHz around the attack: a beater's click is broadband, a bass
//                    note's attack is not
//            onGrid  the attack is within 30 ms of the period the recent kicks keep (a comb over the pairwise
//                    differences of the kicks of the last 4 s, the longest period that explains most of them):
//                    four-on-the-floor kicks under a pad have no click left, and the grid carries them
//   snare  a percussive-flux attack in 1.5-5 kHz, confirmed kDeferFrames hops later by its noise floor N standing
//          4.5 dB or more over its level just before and within 6 dB of its peak at the attack: a snare's noise
//          lasts, a kick's click and a closed hat's tick do not.
//   hat    a percussive-flux attack in 7-16 kHz.
//   onset  30 Hz-16 kHz raw flux.
//
// `ratio` is published continuously per class; `hit` is the decision, `deferred` marks one decided late (the kick
// one hop, the snare kDeferFrames), and `strength` its strength. The Sonic response model takes these decisions and
// shapes their strength with the performer's sensitivity; the detector itself has no parameters a live control
// could change.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace avgen::analysis {

struct AnalysisFrame;

enum class HitClass : std::uint8_t { Kick = 0, Low, Snare, Hat, Onset, Count };
inline constexpr std::size_t kHitClassCount = static_cast<std::size_t>(HitClass::Count);
// How many hops after its attack a snare is confirmed (43 ms at a 512 hop and 48 kHz).
inline constexpr int kDeferFrames = 4;

// What the detector says about one frame. Stored on `AnalysisFrame::causal`.
struct CausalOnsets {
    bool valid = false;                           // the detector ran on this frame
    std::array<float, kHitClassCount> ratio{};    // each class's ODF over its causal threshold (>= 1 is over)
    std::array<bool, kHitClassCount> hit{};       // decided on this frame
    std::array<bool, kHitClassCount> deferred{};  // ...about an attack some hops before (kick 1, snare kDeferFrames)
    std::array<float, kHitClassCount> strength{}; // 0..1 when hit
    std::array<float, kHitClassCount> drumness{}; // ADR-1068: when hit, the share of its band's flux at the attack that
                                                  // was percussive (1 a drum; kick and low read 1)
    std::array<float, kHitClassCount> odf{};      // each class's detection function
    float flux = 0.0f;         // the broadband ODF
    float lowRise = 0.0f;      // dB: the low band over its minimum of the previous three hops (the kick's evidence)
    float click = 0.0f;        // the percussive flux in 1.5-5 kHz (the kick's click, a snare's attack)
    float kickScore = 0.0f;    // the kick decision's score on the frame it was decided
    float kickPeriod = 0.0f;   // seconds: the period the recent kicks keep (0 none yet)
    float noiseDb = -120.0f;   // the 1.5-5 kHz noise floor N, dB
    float snareNoise = 0.0f;   // ADR-1068: the share of the 1.5-5 kHz flux that is percussive (a drum ~1, a note less)
    float hatNoise = 0.0f;     // ...and of the 7-16 kHz flux
    float hatTilt = 0.0f;      // 7-16 kHz percussive flux over 1.5-5 kHz (a hat > 1, a click or a pluck < 1)
    float bassDb = -120.0f;    // 30-150 Hz power in dBFS (a full-scale sine in the band is 0 dB)
    float levelDb = -120.0f;   // 30 Hz-16 kHz power in dBFS
    float snareDb = -120.0f;   // 1.5-5 kHz power in dBFS
    float hatDb = -120.0f;     // 7-16 kHz power in dBFS
};

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

struct CausalOnsetConfig {
    int medianFrames = 31;  // the ODFs' causal median, ~0.33 s at a 512 hop
    float lambda = 1.4f;
    // The absolute part of each threshold, per class (kick, low, snare, hat, onset). The percussive ODFs are smaller
    // than the raw ones: the median removes what a single partial contributes.
    std::array<float, kHitClassCount> delta{0.006f, 0.020f, 0.0024f, 0.0018f, 0.006f};
    int percussiveHalfWidth = 8;  // bins either side of the frequency median
    // The kick (see the header).
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
    [[nodiscard]] static float meanOf(const std::vector<float>& v, const Band& b);
    [[nodiscard]] float bandDb(const AnalysisFrame& frame, const Band& b) const;
    [[nodiscard]] float ratioOf(std::size_t c, float odf);
    void decideKick(CausalOnsets& out, double now);

    CausalOnsetConfig config_;
    std::size_t bins_ = 0;
    float binHz_ = 0.0f;
    Band low_, sub_, snare_, hat_, broad_, bass_;
    std::vector<float> previous_;   // the previous frame's magnitudes
    float reference_ = 0.0f;        // the causal spectral peak the compression is relative to
    std::vector<float> flux_;       // r(k)
    std::vector<float> percussive_; // P(k)
    std::vector<float> floor_;      // the median of |X| across frequency, over the noise band
    std::vector<float> window_;     // scratch for the frequency medians
    bool havePrevious_ = false;
    double seconds_ = 0.0;          // this frame's time on the detector's own clock (frames x hop)
    std::uint64_t frames_ = 0;
    // Per class: the ODF history for the causal median, and the picker.
    std::array<std::vector<float>, kHitClassCount> history_{};
    std::array<std::size_t, kHitClassCount> head_{};
    std::array<HitPicker, kHitClassCount> pickers_{};
    std::vector<float> scratch_;
    // The kick: three hops of each low band's dB, the last two rises, the last two clicks, the accepted kicks.
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
        float share = 1.0f;   // the attack's percussive share
    };
    std::vector<Pending> pending_;
};

// The whole track in order (AnalysisTrack's pass): one fresh detector over every frame.
void detectCausalOnsets(std::span<AnalysisFrame> frames, float binHz, double hopSeconds,
                        const CausalOnsetConfig& config = {});

[[nodiscard]] const char* hitClassName(HitClass c);

} // namespace avgen::analysis
