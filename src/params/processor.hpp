#pragma once

// Fixed-order signal processing chain applied per modulation route (ADR-011, research §5.2; ADR-900):
//   delay -> gain -> offset -> curve -> clamp -> threshold -> smoothing(attack/decay) -> envelope -> remap
// The chain is a plain settings struct; per-route state is passed in so settings serialise
// cleanly and routes can share a chain definition.
//
// ADR-900 changed two stages:
//   * `delayMs` is the first stage. The chain reads the signal as it was `delayMs` ago, from a
//     time-stamped history, so a delay is the same number of seconds at any frame rate. An event
//     stays a one-frame event: it lands on the first frame whose delayed instant is at or after it.
//   * Smoothing holds an event through its attack. A one-frame event used to be smoothed like any
//     other sample, so an attack of 60 ms let 24% of it through and 1.4 s let 1%. Now an event whose
//     level is above the smoothed value latches that level, and the smoothed value rises to it in a
//     straight line over `attackMs` -- credited from the start of the frame the event arrived in, as
//     the one-pole always credited a sample. The frame the rise completes in shows the level in full,
//     and the decay (`decayMs`) runs from that frame. An event below the smoothed value (a chain that
//     inverts it) does the same over `decayMs`. An edge with no time on it lands the event at once,
//     exactly as before, and a signal with no event flags is smoothed exactly as before.

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace avgen::params {

enum class CurveType : std::uint8_t { Linear, Power, Log, Exp, SCurve };
enum class ThresholdMode : std::uint8_t { None, Gate, Binary, Subtract };
enum class EnvelopeMode : std::uint8_t { None, PeakHold, LinearFall };

struct ProcessorChain {
    // ADR-900: the first stage. 0 = no delay, and no history is kept. Clamped to [0, kMaxDelayMs].
    float delayMs = 0.0f;

    // ADR-1182: the second stage, an automatic gain. > 0: the chain reads the signal's LEVEL (a one-pole of
    // it over `normalizeSmoothMs`) as a fraction of that level's running peak, which rises at once and
    // falls back exponentially with a time constant of `normalizeSeconds`, never below `normalizeFloor`.
    // A loud section sets the peak; a quieter one later is heard relative to what the peak has fallen back
    // to, so a feature driven by "the bass" comes back fully after a breakdown in a second drop that is
    // quieter than the first -- on any song and on live input. The level, not the raw signal, is what a
    // kick-heavy band needs: its instants are all transient. 0 = off.
    float normalizeSeconds = 0.0f;
    float normalizeFloor = 0.05f;
    float normalizeSmoothMs = 250.0f;

    float gain = 1.0f;
    float offset = 0.0f;

    CurveType curve = CurveType::Linear;
    float curveAmount = 1.0f; // Power: exponent; SCurve: steepness; Log/Exp: base scale (>0)

    bool clampEnabled = false;
    float clampMin = 0.0f;
    float clampMax = 1.0f;

    ThresholdMode threshold = ThresholdMode::None;
    float thresholdLevel = 0.5f;

    // Asymmetric one-pole smoothing of a continuous signal; for an event, the time its level takes
    // to rise in full (ADR-900). 0 disables that edge (instant). Frame-rate independent.
    float attackMs = 0.0f;
    float decayMs = 0.0f;

    // Turns impulses (events) into control envelopes. PeakHold: env = max(env, x), holds for
    // holdMs then falls at fallPerSecond (units/second). LinearFall: same without hold.
    EnvelopeMode envelope = EnvelopeMode::None;
    float envelopeHoldMs = 0.0f;
    float envelopeFallPerSecond = 4.0f;

    // ADR-1041: a critically (or under-) damped second-order follower after the envelope, so a change
    // eases in AND out (continuous velocity) instead of cornering like the one-pole: x'' = w^2 (u - x)
    // - 2 zeta w x', w = 2 pi springHz. 0 = off. springDamping (zeta) 1 is critical, < 1 overshoots
    // and settles ("breathing"), > 1 is sluggish. Integrated in fixed sub-steps of at most
    // kSpringStepSeconds, so it is frame-rate independent to within a sub-step.
    float springHz = 0.0f;
    float springDamping = 1.0f;

    bool remapEnabled = false;
    float remapInMin = 0.0f;
    float remapInMax = 1.0f;
    float remapOutMin = 0.0f;
    float remapOutMax = 1.0f;

    // ADR-1041: the last stage. The chain outputs the running integral of the remapped value over time
    // (units per second in, units out): a rate becomes a position -- a pace becomes a distance
    // travelled, a flow speed becomes a phase. The output never jumps whatever the input does, and a
    // zero rate holds it still. Seeks replay it like every other chain state (ADR-901).
    bool integrate = false;
    // ADR-1161: the integral's bounds. The running total itself is clamped to [integrateMin, integrateMax] every
    // frame, so it saturates and recovers at once: a rate that has pushed it to a bound for a minute moves it away
    // the moment the rate changes sign (a dose that charges and heals). The default is unbounded (ADR-1041).
    float integrateMin = -std::numeric_limits<float>::infinity();
    float integrateMax = std::numeric_limits<float>::infinity();

    // One sample of the delay stage's history: the chain's input at `time` on the chain's own clock.
    struct DelaySample {
        double time = 0.0;
        float value = 0.0f;
        bool event = false;
    };

    struct State {
        float smoothed = 0.0f;
        float envelope = 0.0f;
        float holdRemaining = 0.0f;
        bool initialised = false;

        // ADR-900, the event's rise: the level a latched event is rising to, and the seconds of its
        // rise still to run (0 = nothing rising).
        float eventTarget = 0.0f;
        double rampRemaining = 0.0;

        // ADR-900, the delay stage. `clock` is the chain's own time: the sum of every dt it has been
        // given since the state was reset, or the instant a seek's replay left it at (ADR-901).
        // `history` is live from `historyHead` on, in time order. `emittedThrough` is the delayed
        // instant every event at or before which has been emitted. A frame whose delayed instant is
        // the previous frame's (dt = 0: a seek's landing frame, a paused redraw) re-emits what that
        // instant emitted (`repeat*`) rather than losing its event.
        double clock = 0.0;
        std::vector<DelaySample> history;
        std::size_t historyHead = 0;
        double emittedThrough = -std::numeric_limits<double>::infinity();
        double repeatTarget = std::numeric_limits<double>::quiet_NaN();
        float repeatValue = 0.0f;
        bool repeatEvent = false;

        // ADR-1041: the spring's position and velocity (seeded to the first input, at rest), and the
        // integrator's running total (double: a pace integrated over a four-minute film).
        float springPosition = 0.0f;
        float springVelocity = 0.0f;
        bool springInitialised = false;
        double integral = 0.0;
        // ADR-1182: the normalise stage's level and its running peak.
        float level = 0.0f;
        float peak = 0.0f;
    };

    // x: raw signal value; event: true when the source fired this frame; dt: seconds.
    [[nodiscard]] float process(float x, bool event, double dt, State& state) const;

    // The delay stage alone: records (x, event) at the state's clock advanced by dt and returns what
    // the chain reads at `clock - delayMs`. process() calls it first when `delayMs > 0`.
    struct Delayed {
        float value = 0.0f;
        bool event = false;
    };
    [[nodiscard]] Delayed delay(float x, bool event, double dt, State& state) const;

    [[nodiscard]] double delaySeconds() const;

    // ADR-1041: the spring's largest internal step (1/480 s): stable for springHz up to ~50 Hz.
    static constexpr double kSpringStepSeconds = 1.0 / 480.0;
    static constexpr float kMaxSpringHz = 50.0f;

    static constexpr float kMaxTimeMs = 60000.0f;
    // ADR-900: a delay is a stagger or an echo -- a beat, a bar -- not a second timeline. Four seconds
    // is two bars at 120 BPM, and it bounds the history a seek checkpoint carries per route.
    static constexpr float kMaxDelayMs = 4000.0f;
    // Two instants closer than this are one instant. The chain's clock is a sum of dts and a seek's
    // replay steps k / 60, and the two must agree on which frame a delayed event lands on.
    static constexpr double kInstantEpsilon = 1e-6;
};

// Standalone helpers (also used by tests).
float applyCurve(float x, CurveType curve, float amount);
float smoothingCoefficient(float timeMs, double dt); // 1 - exp(-dt / tau); 1 when timeMs <= 0

} // namespace avgen::params
