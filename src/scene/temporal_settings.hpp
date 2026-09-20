#pragma once

// Authored settings for the temporal effect family (ADR-400, brief §9-§17).
//
// **The load-bearing thing in this file is `historyFrames()`.** Every temporal effect must state,
// as a function of its own settings, how many frames of history it reads. That number is what
// makes the family scrub-safe: it sizes the ring, it sizes the seek warm-up, and it is what the
// artist is told is still settling. An effect that cannot state a bound is an accumulator, which
// ADR-400 forbids outright -- and because "I forgot to declare a bound" does not fail to compile,
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
    Count,
};

constexpr std::uint32_t kTemporalEffectKindCount = static_cast<std::uint32_t>(TemporalEffectKind::Count);

// The authored ceiling on a history length (§8: 1-32 frames). Kept here as well as in
// `rendering::kMaxTemporalFrames` because `scene/` must not include a WebGPU header;
// `temporal_effects.cpp` static_asserts the two equal.
constexpr int kMaxTemporalFrames = 32;

inline constexpr std::array<TemporalEffectKind, kTemporalEffectKindCount> kTemporalEffectKinds{
    TemporalEffectKind::FrameEcho,
};

[[nodiscard]] constexpr const char* temporalEffectKindName(TemporalEffectKind kind) {
    switch (kind) {
    case TemporalEffectKind::FrameEcho: return "echo";
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

struct TemporalSettings {
    FrameEchoSettings echo;

    // The bound. `max` over the live effects, zero when none is on -- and zero means the ring is
    // released, because holding history for a feature nobody has enabled is exactly what §8 says
    // not to do.
    [[nodiscard]] std::uint32_t historyFrames() const;
    [[nodiscard]] bool anyEnabled() const;
};

// Per-kind bound, for the conformance check and for the panel's own reporting. Exhaustive switch,
// no `default`: that is the compile-time half of the guard.
[[nodiscard]] std::uint32_t temporalEffectHistoryFrames(TemporalEffectKind kind, const TemporalSettings& s);
[[nodiscard]] bool temporalEffectEnabled(TemporalEffectKind kind, const TemporalSettings& s);

// ---- parameters --------------------------------------------------------------------------------

struct TemporalParameters {
    params::Parameter<bool>* echoEnabled = nullptr;
    params::Parameter<float>* echoFrames = nullptr;
    params::Parameter<float>* echoStrength = nullptr;
    params::Parameter<float>* echoDecay = nullptr;
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
