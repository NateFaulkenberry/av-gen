#include "params/processor.hpp"

#include <algorithm>
#include <cmath>

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

float ProcessorChain::process(float x, bool event, double dt, State& state) const {
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

    // smoothing: asymmetric one-pole, frame-rate independent
    if (!state.initialised) {
        state.smoothed = y;
        state.initialised = true;
    } else {
        const float coefficient =
            y > state.smoothed ? smoothingCoefficient(attackMs, dt) : smoothingCoefficient(decayMs, dt);
        state.smoothed = coefficient >= 1.0f ? y : state.smoothed + (y - state.smoothed) * coefficient;
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
