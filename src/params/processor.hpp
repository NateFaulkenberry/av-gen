#pragma once

// Fixed-order signal processing chain applied per modulation route (ADR-011, research §5.2):
//   gain -> offset -> curve -> clamp -> threshold -> smoothing(attack/decay) -> envelope -> remap
// The chain is a plain settings struct; per-route state is passed in so settings serialise
// cleanly and routes can share a chain definition.

#include <cstdint>

namespace avgen::params {

enum class CurveType : std::uint8_t { Linear, Power, Log, Exp, SCurve };
enum class ThresholdMode : std::uint8_t { None, Gate, Binary, Subtract };
enum class EnvelopeMode : std::uint8_t { None, PeakHold, LinearFall };

struct ProcessorChain {
    float gain = 1.0f;
    float offset = 0.0f;

    CurveType curve = CurveType::Linear;
    float curveAmount = 1.0f; // Power: exponent; SCurve: steepness; Log/Exp: base scale (>0)

    bool clampEnabled = false;
    float clampMin = 0.0f;
    float clampMax = 1.0f;

    ThresholdMode threshold = ThresholdMode::None;
    float thresholdLevel = 0.5f;

    // Asymmetric one-pole smoothing. 0 disables that edge (instant). Frame-rate independent.
    float attackMs = 0.0f;
    float decayMs = 0.0f;

    // Turns impulses (events) into control envelopes. PeakHold: env = max(env, x), holds for
    // holdMs then falls at fallPerSecond (units/second). LinearFall: same without hold.
    EnvelopeMode envelope = EnvelopeMode::None;
    float envelopeHoldMs = 0.0f;
    float envelopeFallPerSecond = 4.0f;

    bool remapEnabled = false;
    float remapInMin = 0.0f;
    float remapInMax = 1.0f;
    float remapOutMin = 0.0f;
    float remapOutMax = 1.0f;

    struct State {
        float smoothed = 0.0f;
        float envelope = 0.0f;
        float holdRemaining = 0.0f;
        bool initialised = false;
    };

    // x: raw signal value; event: true when the source fired this frame; dt: seconds.
    [[nodiscard]] float process(float x, bool event, double dt, State& state) const;

    static constexpr float kMaxTimeMs = 60000.0f;
};

// Standalone helpers (also used by tests).
float applyCurve(float x, CurveType curve, float amount);
float smoothingCoefficient(float timeMs, double dt); // 1 - exp(-dt / tau); 1 when timeMs <= 0

} // namespace avgen::params
