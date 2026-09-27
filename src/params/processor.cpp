#include "params/processor.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace avgen::params {

namespace {

// Shapes |x| with `f` and restores the sign, so every curve is odd-symmetric and bipolar
// signals keep their polarity. Each shape maps 0 -> 0 and 1 -> 1.
template <typename F>
float oddSymmetric(float x, F&& f) {
    const float magnitude = std::fabs(x);
    const float shaped = f(magnitude);
    return x < 0.0f ? -shaped : shaped;
}

float logistic(float x, float steepness) {
    return 1.0f / (1.0f + std::exp(-steepness * (x - 0.5f)));
}

} // namespace

float applyCurve(float x, CurveType curve, float amount) {
    switch (curve) {
    case CurveType::Linear:
        return x;
    case CurveType::Power:
        if (amount <= 0.0f) {
            return x;
        }
        return oddSymmetric(x, [amount](float m) { return std::pow(m, amount); });
    case CurveType::Log:
        if (amount <= 0.0f) {
            return x;
        }
        return oddSymmetric(x, [amount](float m) { return std::log1p(amount * m) / std::log1p(amount); });
    case CurveType::Exp:
        if (amount <= 0.0f) {
            return x;
        }
        return oddSymmetric(x, [amount](float m) { return std::expm1(amount * m) / std::expm1(amount); });
    case CurveType::SCurve: {
        if (amount <= 0.0f) {
            return x;
        }
        const float lo = logistic(0.0f, amount);
        const float hi = logistic(1.0f, amount);
        return oddSymmetric(x, [=](float m) { return (logistic(m, amount) - lo) / (hi - lo); });
    }
    }
    return x;
}

float smoothingCoefficient(float timeMs, double dt) {
    if (timeMs <= 0.0f) {
        return 1.0f;
    }
    const double tau = static_cast<double>(std::min(timeMs, ProcessorChain::kMaxTimeMs)) / 1000.0;
    return static_cast<float>(1.0 - std::exp(-dt / tau));
}

double ProcessorChain::delaySeconds() const {
    return static_cast<double>(std::clamp(delayMs, 0.0f, kMaxDelayMs)) / 1000.0;
}

ProcessorChain::Delayed ProcessorChain::delay(float x, bool event, double dt, State& state) const {
    const double d = delaySeconds();
    if (dt < 0.0) {
        // A clock that ran backwards is a jump, not a frame: nothing recorded before it is history
        // of what comes after it.
        state.history.clear();
        state.historyHead = 0;
        state.emittedThrough = -std::numeric_limits<double>::infinity();
        state.repeatTarget = std::numeric_limits<double>::quiet_NaN();
        dt = 0.0;
    }
    state.clock += dt;

    // Record this frame's input. A second frame at the same instant merges into the first: its value
    // replaces a continuous sample's, and an event already recorded there is never overwritten.
    std::vector<DelaySample>& h = state.history;
    if (state.historyHead < h.size() && std::abs(h.back().time - state.clock) <= kInstantEpsilon) {
        DelaySample& last = h.back();
        if (event) {
            last.value = last.event ? std::max(last.value, x) : x;
            last.event = true;
        } else if (!last.event) {
            last.value = x;
        }
    } else {
        h.push_back(DelaySample{state.clock, x, event});
    }

    const double target = state.clock - d;
    if (!std::isnan(state.repeatTarget) && std::abs(target - state.repeatTarget) <= kInstantEpsilon) {
        return Delayed{state.repeatValue, state.repeatEvent};
    }

    // Every event in (emittedThrough, target] lands now, as one event at the strongest strength.
    // Otherwise the value is the signal at `target`: interpolated between the two samples either side
    // of it, so a delay that falls between frames is the same number of seconds at any frame rate,
    // and held from the latest sample at or before it next to an event sample. An event's value is
    // its strength for the one frame it lands on and 0 afterwards, as the bus clears it.
    bool fired = false;
    float strength = 0.0f;
    std::size_t holdIndex = h.size();
    for (std::size_t i = state.historyHead; i < h.size(); ++i) {
        const DelaySample& s = h[i];
        if (s.time > target + kInstantEpsilon) {
            break;
        }
        holdIndex = i;
        if (s.event && s.time > state.emittedThrough + kInstantEpsilon) {
            strength = fired ? std::max(strength, s.value) : s.value;
            fired = true;
        }
    }
    Delayed out;
    if (fired) {
        out = Delayed{strength, true};
    } else if (holdIndex == h.size()) {
        // Before the first recorded sample the first one holds, as a timeline's first key does.
        const DelaySample& s = h[state.historyHead];
        out = Delayed{s.event ? 0.0f : s.value, false};
    } else {
        const DelaySample& a = h[holdIndex];
        out = Delayed{a.event ? 0.0f : a.value, false};
        if (holdIndex + 1 < h.size()) {
            const DelaySample& b = h[holdIndex + 1];
            const double span = b.time - a.time;
            const double into = target - a.time;
            if (!a.event && !b.event && span > kInstantEpsilon && into > kInstantEpsilon) {
                out.value = a.value + (b.value - a.value) * static_cast<float>(into / span);
            }
        }
    }
    state.emittedThrough = std::max(state.emittedThrough, target);
    state.repeatTarget = target;
    state.repeatValue = out.value;
    state.repeatEvent = out.event;

    // Keep the latest sample at or before the target (the value being held) and everything after it.
    while (state.historyHead + 1 < h.size() && h[state.historyHead + 1].time <= target + kInstantEpsilon) {
        ++state.historyHead;
    }
    // Compact once the dead prefix is most of the buffer; in a steady state the capacity is reused.
    if (state.historyHead >= 32 && state.historyHead * 2 >= h.size()) {
        h.erase(h.begin(), h.begin() + static_cast<std::ptrdiff_t>(state.historyHead));
        state.historyHead = 0;
    }
    return out;
}

float ProcessorChain::process(float x, bool event, double dt, State& state) const {
    // delay (ADR-900): everything after this reads the signal as it was `delayMs` ago
    if (delayMs > 0.0f) {
        const Delayed delayed = delay(x, event, dt, state);
        x = delayed.value;
        event = delayed.event;
    }

    // gain -> offset -> curve
    float y = applyCurve(x * gain + offset, curve, curveAmount);

    // clamp
    if (clampEnabled) {
        y = std::clamp(y, std::min(clampMin, clampMax), std::max(clampMin, clampMax));
    }

    // threshold
    switch (threshold) {
    case ThresholdMode::None:
        break;
    case ThresholdMode::Gate:
        y = y >= thresholdLevel ? y : 0.0f;
        break;
    case ThresholdMode::Binary:
        y = y >= thresholdLevel ? 1.0f : 0.0f;
        break;
    case ThresholdMode::Subtract:
        if (thresholdLevel >= 1.0f) {
            y = y >= thresholdLevel ? 1.0f : 0.0f;
        } else {
            y = std::max(0.0f, y - thresholdLevel) / (1.0f - thresholdLevel);
        }
        break;
    }

    // smoothing: asymmetric one-pole for a continuous signal, frame-rate independent; an event's
    // level held through its attack (ADR-900)
    const auto follow = [this, &state](float input, double seconds) {
        const float coefficient = input > state.smoothed ? smoothingCoefficient(attackMs, seconds)
                                                         : smoothingCoefficient(decayMs, seconds);
        state.smoothed = coefficient >= 1.0f ? input : state.smoothed + (input - state.smoothed) * coefficient;
    };
    if (!state.initialised) {
        state.smoothed = y;
        state.initialised = true;
        state.rampRemaining = 0.0;
    } else {
        // An event latches its level: above the smoothed value it is reached over `attackMs`, below it
        // (an event inverted inside the chain) over `decayMs`. A stronger event the same way during a
        // ramp re-aims it from wherever it has got to; anything else leaves the ramp alone. With no
        // time on that edge the one-pole below lands the event at once, as it always has.
        if (event && y != state.smoothed) {
            const bool rising = y > state.smoothed;
            const float edgeMs = rising ? attackMs : decayMs;
            const bool stronger = state.rampRemaining <= 0.0 ||
                                  (rising ? y > state.eventTarget && state.eventTarget > state.smoothed
                                          : y < state.eventTarget && state.eventTarget < state.smoothed);
            if (edgeMs > 0.0f && stronger) {
                state.eventTarget = y;
                state.rampRemaining = static_cast<double>(std::min(edgeMs, kMaxTimeMs)) / 1000.0;
            }
        }
        if (state.rampRemaining > 0.0) {
            // The rise owns the frame. The frame it completes in shows the level in full -- the time
            // left in it is not integrated -- so the event arrives whole at any frame rate, and the
            // decay runs from that frame (ADR-900).
            if (dt >= state.rampRemaining - kInstantEpsilon) {
                state.smoothed = state.eventTarget;
                state.rampRemaining = 0.0;
            } else {
                state.smoothed += (state.eventTarget - state.smoothed) * static_cast<float>(dt / state.rampRemaining);
                state.rampRemaining -= dt;
            }
        } else {
            follow(y, dt);
        }
    }
    y = state.smoothed;

    // envelope
    const float fall = static_cast<float>(static_cast<double>(envelopeFallPerSecond) * dt);
    switch (envelope) {
    case EnvelopeMode::None:
        break;
    case EnvelopeMode::PeakHold:
        if (event || y > state.envelope) {
            state.envelope = std::max(state.envelope, y);
            state.holdRemaining = envelopeHoldMs / 1000.0f;
        } else if (state.holdRemaining > 0.0f) {
            state.holdRemaining -= static_cast<float>(dt);
        } else {
            state.envelope = std::max(0.0f, state.envelope - fall);
        }
        y = state.envelope;
        break;
    case EnvelopeMode::LinearFall:
        if (event || y > state.envelope) {
            state.envelope = std::max(state.envelope, y);
        } else {
            state.envelope = std::max(0.0f, state.envelope - fall);
        }
        y = state.envelope;
        break;
    }

    // remap (no clamping)
    if (remapEnabled) {
        const float inSpan = remapInMax - remapInMin;
        if (std::fabs(inSpan) < 1e-12f) {
            y = remapOutMin;
        } else {
            const float t = (y - remapInMin) / inSpan;
            y = remapOutMin + t * (remapOutMax - remapOutMin);
        }
    }
    return y;
}

} // namespace avgen::params
