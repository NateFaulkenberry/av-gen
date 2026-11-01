#pragma once

// Authored settings for the temporal effect family (ADR-410, brief §9-§17).
//
// **The load-bearing thing in this file is `historyFrames()`.** Every temporal effect must state,
// as a function of its own settings, how many frames of history it reads. That number is what
// makes the family scrub-safe: it sizes the ring, it sizes the seek warm-up, and it is what the
// artist is told is still settling. An effect that cannot state a bound is an accumulator, which
// ADR-410 forbids outright -- and because "I forgot to declare a bound" does not fail to compile,
// does not throw and does not log, `temporal_conformance.hpp` asks every kind for its bound and
// fails by name for the one that cannot give it.
//
// Parameters live under `temporal/<effect>/<leaf>`. That prefix is in `kBeginnerPrefixes`
// (`ui/ui_logic.hpp`), not merely the intermediate list: these are flagship effects and the
// authoring layer does not get to decide whether they exist (ADR-375).

#include "core/error.hpp"
#include "params/parameter.hpp"

#include <array>
#include <cstdint>
#include <nlohmann/json_fwd.hpp>
#include <string_view>

namespace avgen::params {
class ParameterSet;
}

namespace avgen::scene {

// The family's kinds. The switches over this enum have no `default` on purpose, so adding a kind
// is at minimum a `-Wswitch` diagnostic at every site that must learn about it -- and because
// warnings are not errors in this build, `test_temporal_conformance` also reads the enum and fails
// by name when a table has fallen behind it (the ADR-392 pattern).
enum class TemporalEffectKind : std::uint8_t {
    FrameEcho = 0, // §9
    Mosh = 1,      // ADR-1049: data-mosh blocks and channel shift (All You Got art pass 2)
    Feedback = 2,  // ADR-1066: zoom/rotate/drift/hue feedback, unrolled over the clean ring (Sonic VFX)
    Slit = 3,      // ADR-1066: slit-scan time displacement, each pixel a different age (Sonic VFX)
    Count,
};

constexpr std::uint32_t kTemporalEffectKindCount = static_cast<std::uint32_t>(TemporalEffectKind::Count);

// The ceiling on the ring's depth in LAYERS. ADR-1931 raised it from ADR-410's 32 to 256 for slit scan, which needs
// seconds of history rather than a second. Kept here as well as in `rendering::kMaxTemporalFrames` because `scene/`
// must not include a WebGPU header; `temporal_effects.cpp` static_asserts the two equal.
constexpr int kMaxTemporalFrames = 256;
// ADR-1931: the per-pixel TAP ceiling of the effects that loop over the ring (echo, mosh, feedback). Their cost is one
// fetch per tap, so the old 32 stays their limit; only the slit's span, which costs two fetches whatever its length,
// reaches deeper.
constexpr int kMaxTemporalTaps = 32;
// ADR-1931: capture every `stride`-th frame (1..8), so 256 layers can span up to 2,048 frames.
constexpr int kMaxTemporalStride = 8;
// The longest history any effect may declare, in frames: every layer, at the widest stride.
constexpr int kMaxTemporalHistoryFrames = kMaxTemporalFrames * kMaxTemporalStride;

inline constexpr std::array<TemporalEffectKind, kTemporalEffectKindCount> kTemporalEffectKinds{
    TemporalEffectKind::FrameEcho,
    TemporalEffectKind::Mosh,
    TemporalEffectKind::Feedback,
    TemporalEffectKind::Slit,
};

[[nodiscard]] constexpr const char* temporalEffectKindName(TemporalEffectKind kind) {
    switch (kind) {
    case TemporalEffectKind::FrameEcho: return "echo";
    case TemporalEffectKind::Mosh: return "mosh";
    case TemporalEffectKind::Feedback: return "feedback";
    case TemporalEffectKind::Slit: return "slit";
    case TemporalEffectKind::Count: break;
    }
    return "unknown";
}

[[nodiscard]] bool temporalEffectKindFromName(std::string_view name, TemporalEffectKind& out);

// §9 frame echo. An FIR filter over the ring: out = current + strength * Σ decay^i * history[i].
//
// Bounded by construction -- `frames` IS the bound, and it is an authored value rather than a
// consequence of how long the effect has been running. That is the difference between this and a
// feedback echo, which would be the same picture and an unrebuildable one.
struct FrameEchoSettings {
    bool enabled = false;
    // How many frames back the echo reaches. The brief's 1-32 (§8) is the hard range.
    int frames = 6;
    float strength = 0.45f; // how much of the echo is added to the current frame
    float decay = 0.72f;    // per-tap falloff; 0 is one ghost, near 1 is an even smear
};

// ADR-1049: simulation corruption. A block of the frame is replaced by the same block from up to
// `frames` frames ago, dragged `smear` pixels -- the look of a data-moshed codec that lost its
// keyframe -- and the colour channels are pulled apart by `shift` pixels. Which blocks, how far
// back and which way are hashes of the block and of `floor(time * rate) + seed`, so the picture is a
// pure function of the clean history ring and the clock: an FIR filter, scrub-safe like the echo.
// `amount` (the share of blocks corrupted) and `shift` are the knobs to key or route from an event;
// at 0 and 0 the pass is skipped and only the ring is kept warm.
struct MoshSettings {
    bool enabled = false;
    int frames = 8;        // how far back a frozen block may come from (the bound)
    float amount = 0.0f;   // 0..1, the share of blocks corrupted
    float block = 32.0f;   // block edge in pixels at 1080 lines (scaled with the output height)
    float smear = 24.0f;   // pixels a corrupted block is dragged, at 1080 lines
    float shift = 0.0f;    // pixels the red and blue channels are pulled apart, at 1080 lines
    float rate = 12.0f;    // how many times a second the corrupted blocks are re-chosen
    float seed = 0.0f;     // added to the epoch: key it to pick a different pattern
};

// ADR-1066: video feedback (MilkDrop's zoom and turn, a hue that walks) as an FIR over the CLEAN ring, never an
// accumulator. The loop F = C + d g(F_prev(T uv)) unrolled K = `frames` taps:
//   out = C + amount x (1 - decay) x sum_k decay^(k-1) x hue^k(R_k(T^k uv)),  k = 1..K
// where T zooms by `zoom` and turns by `rotate` degrees per frame about the centre, and drifts by (`driftX`, `driftY`)
// of the frame per frame, and hue^k turns the colour k x `hue` turns in OKLab. Exact, bounded and rebuildable: a seek
// refills it in K frames like the echo.
struct FeedbackSettings {
    bool enabled = false;
    int frames = 8;          // taps, the bound
    float amount = 0.0f;     // 0..2: how much of the tail is added (0 encodes nothing)
    float decay = 0.75f;     // per tap
    float zoom = 1.02f;      // per frame: > 1 pours outward, < 1 falls in
    float rotate = 0.0f;     // degrees per frame
    float driftX = 0.0f;     // frame widths per frame
    float driftY = 0.0f;
    float hue = 0.0f;        // turns per frame, OKLab
};

// ADR-1066 / ADR-1931: slit scan as a time map. Each pixel shows the moment `q(d(uv)) x span` frames ago, where d runs
// 0..1 across the frame and q optionally quantises it into `strips`:
//   mode 0: along a direction, `angle` degrees (0: age grows left to right, so the left edge is now -- a vertical slit
//           scan; 90: it grows top to bottom -- a horizontal one; anything between is directional);
//   mode 1: radial (centre now); mode 2: luminance (bright now).
// `scan` offsets d and `scanSpeed` moves it (cycles per second), wrapping, so the seam between now and the oldest
// moment travels through the picture: the moving slit. `edge` softens strip boundaries; `blend` 1 cross-fades between
// neighbouring stored frames and 0 takes the nearest (crisper strips, visible steps). FIR over the clean ring: the
// span is in frames and costs two fetches per pixel whatever its length.
struct SlitSettings {
    bool enabled = false;
    float amount = 0.0f;     // 0..1: mix (0 encodes nothing)
    float span = 16.0f;      // frames back at the far end of the map (the declared bound is ceil(span))
    float mode = 0.0f;       // 0 direction, 1 radial, 2 luminance
    float angle = 0.0f;      // degrees, mode 0
    float strips = 0.0f;     // 0 continuous, else the number of strips
    float edge = 0.0f;       // 0 hard strip edges .. 1 fully soft
    float blend = 1.0f;      // 0 nearest stored frame .. 1 linear between the two nearest
    float scan = 0.0f;       // 0..1 offset of the map
    float scanSpeed = 0.0f;  // cycles per second
    float reverse = 0.0f;    // 1 flips which end is now
};

// ADR-1931: the ring itself. `capacity` 0 sizes it from the live effects' bounds (ADR-410's behaviour, reallocating when
// a bound changes); above 0 it is that many layers, fixed, so the effects' spans can be routed every frame without the
// ring ever being reallocated -- which drops the history at exactly the musical moment. Structural: changing capacity
// or stride resets the history. Not a tier setting (ADR-410's rule for history lengths): the tier scales resolution.
struct TemporalHistorySettings {
    int capacity = 0; // layers, 0 = auto, else 1..kMaxTemporalFrames
    int stride = 1;   // capture every stride-th frame (anchored to the frame index), 1..kMaxTemporalStride
};

struct TemporalSettings {
    FrameEchoSettings echo;
    MoshSettings mosh;
    FeedbackSettings feedback; // ADR-1066
    SlitSettings slit;         // ADR-1066
    TemporalHistorySettings history; // ADR-1931

    // The bound. `max` over the live effects, zero when none is on -- and zero means the ring is
    // released, because holding history for a feature nobody has enabled is exactly what §8 says
    // not to do.
    [[nodiscard]] std::uint32_t historyFrames() const;
    [[nodiscard]] bool anyEnabled() const;
    // ADR-1931: how many layers the ring holds for these settings (0 when no effect is on): `history.capacity` when set,
    // otherwise enough layers at `stride` for `historyFrames()`. The tier's cap is applied by the renderer.
    [[nodiscard]] std::uint32_t ringLayers() const;
    // The stride, clamped.
    [[nodiscard]] std::uint32_t captureStride() const;
};

// Per-kind bound, for the conformance check and for the panel's own reporting. Exhaustive switch,
// no `default`: that is the compile-time half of the guard.
[[nodiscard]] std::uint32_t temporalEffectHistoryFrames(TemporalEffectKind kind, const TemporalSettings& s);
[[nodiscard]] bool temporalEffectEnabled(TemporalEffectKind kind, const TemporalSettings& s);

// What the renderer reports back about the ring, mirrored here so the UI can read it without
// `scene/` or `ui/` depending on a WebGPU header. The renderer's `TemporalHistoryState` is the
// source; this is the copy the Engine carries for a panel to draw.
//
// It exists because the disclosure is the point of ADR-410: "what you are looking at is not what
// will be rendered" has to be *visible*, and a value the renderer publishes is the only honest way
// to say it. A panel that inferred settling from the settings alone would be guessing.
struct TemporalHistoryReport {
    std::uint32_t framesValid = 0;
    std::uint32_t framesNeeded = 0;
    bool stalled = false;           // asks for more than the ring can hold; waiting will not help
    std::uint64_t bytes = 0;        // measured, not estimated
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    [[nodiscard]] bool complete() const { return framesValid >= framesNeeded; }
    [[nodiscard]] bool settling() const { return !complete() && !stalled; }
};

// ---- parameters --------------------------------------------------------------------------------

struct TemporalParameters {
    params::Parameter<bool>* echoEnabled = nullptr;
    params::Parameter<float>* echoFrames = nullptr;
    params::Parameter<float>* echoStrength = nullptr;
    params::Parameter<float>* echoDecay = nullptr;
    params::Parameter<bool>* moshEnabled = nullptr;
    params::Parameter<float>* moshFrames = nullptr;
    params::Parameter<float>* moshAmount = nullptr;
    params::Parameter<float>* moshBlock = nullptr;
    params::Parameter<float>* moshSmear = nullptr;
    params::Parameter<float>* moshShift = nullptr;
    params::Parameter<float>* moshRate = nullptr;
    params::Parameter<float>* moshSeed = nullptr;
    // ADR-1066
    params::Parameter<bool>* feedbackEnabled = nullptr;
    params::Parameter<float>* feedbackFrames = nullptr;
    params::Parameter<float>* feedbackAmount = nullptr;
    params::Parameter<float>* feedbackDecay = nullptr;
    params::Parameter<float>* feedbackZoom = nullptr;
    params::Parameter<float>* feedbackRotate = nullptr;
    params::Parameter<float>* feedbackDriftX = nullptr;
    params::Parameter<float>* feedbackDriftY = nullptr;
    params::Parameter<float>* feedbackHue = nullptr;
    params::Parameter<bool>* slitEnabled = nullptr;
    params::Parameter<float>* slitAmount = nullptr;
    params::Parameter<float>* slitSpan = nullptr; // ADR-1931
    params::Parameter<float>* slitMode = nullptr;
    params::Parameter<float>* slitAngle = nullptr;
    params::Parameter<float>* slitStrips = nullptr;
    params::Parameter<float>* slitEdge = nullptr;
    params::Parameter<float>* slitBlend = nullptr;
    params::Parameter<float>* slitScan = nullptr;
    params::Parameter<float>* slitScanSpeed = nullptr;
    params::Parameter<float>* slitReverse = nullptr;
    params::Parameter<float>* historyCapacity = nullptr; // ADR-1931
    params::Parameter<float>* historyStride = nullptr;
};

[[nodiscard]] TemporalParameters registerTemporalParameters(params::ParameterSet& params,
                                                            const TemporalSettings& defaults);
void applyTemporalParameters(const TemporalParameters& p, TemporalSettings& settings);

// The prefix a panel computes. Exposed so a test can compute it the same way rather than
// hard-coding the string a second time (ADR-382).
[[nodiscard]] std::string temporalParameterPrefix(TemporalEffectKind kind);

[[nodiscard]] nlohmann::json temporalToJson(const TemporalSettings& s);
[[nodiscard]] Result<TemporalSettings> temporalFromJson(const nlohmann::json& j);

} // namespace avgen::scene
