#include "world/effects/effect_timing.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

// ADR-702: the half of ADR-207's `effects.cpp` that every effect type shares, moved out of the
// surface-wave file it had lived in.

namespace avgen::world {
namespace {

constexpr float kEps = 1e-5f;

bool finite(float v) { return std::isfinite(v); }

// Smoothstep on a raw ratio, guarding the degenerate width that would otherwise divide by zero and
// give a hard edge exactly where §7 forbids one.
float smoothRamp(float x, float width) {
    if (width <= kEps) {
        return x >= 0.0f ? 1.0f : 0.0f;
    }
    const float t = std::clamp(x / width, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

const char* activationName(Activation a) {
    switch (a) {
    case Activation::Always: return "always";
    case Activation::Window: return "window";
    case Activation::CameraTravel: return "cameraTravel";
    case Activation::HeroFocus: return "heroFocus";
    }
    return "always";
}
std::optional<Activation> activationFromName(std::string_view name) {
    if (name == "always") { return Activation::Always; }
    if (name == "window" || name == "time") { return Activation::Window; }
    if (name == "cameraTravel" || name == "travel" || name == "transition") { return Activation::CameraTravel; }
    if (name == "heroFocus" || name == "focus" || name == "spotlight") { return Activation::HeroFocus; }
    return std::nullopt;
}

Result<void> Sparkle::validate() const {
    if (density < 0.0f || !finite(density)) { return fail("sparkle density must not be negative"); }
    if (size < 0.0f || size > 1.0f) { return fail("sparkle size is a fraction of a cell, 0..1"); }
    if (intensity < 0.0f || !finite(intensity)) { return fail("sparkle intensity must not be negative"); }
    if (fadeDistance <= 0.0f || !finite(fadeDistance)) { return fail("sparkle fade distance must be positive"); }
    return {};
}

Result<void> Timing::validate() const {
    if (delay < 0.0) { return fail("delay must not be negative"); }
    if (lifetime < 0.0) { return fail("lifetime must not be negative (0 = as long as the activation)"); }
    if (fadeIn < 0.0 || fadeOut < 0.0) { return fail("fades must not be negative"); }
    if (windowSeconds < 0.0) { return fail("the window length must not be negative"); }
    if (repeatSeconds < 0.0) { return fail("the repeat interval must not be negative"); }
    return {};
}

std::optional<ActivationWindow> resolveActivationWindow(Activation activation, const Timing& timing,
                                                        double seconds, std::span<const ShotSpan> shots,
                                                        bool followsFocus, std::string_view subject) {
    switch (activation) {
    case Activation::Always:
        return ActivationWindow{0.0, std::numeric_limits<double>::infinity(), nullptr};
    case Activation::Window: {
        const double end = timing.windowStart + timing.windowSeconds;
        if (seconds < timing.windowStart || seconds >= end) {
            return std::nullopt;
        }
        return ActivationWindow{timing.windowStart, end, nullptr};
    }
    case Activation::CameraTravel:
        for (const ShotSpan& s : shots) {
            if (s.travel && seconds >= s.start && seconds < s.end) {
                return ActivationWindow{s.start, s.end, &s};
            }
        }
        return std::nullopt;
    case Activation::HeroFocus:
        for (const ShotSpan& s : shots) {
            if (!s.spotlight || seconds < s.start || seconds >= s.end) {
                continue;
            }
            // A `FocusHero` source follows whatever is spotlit; a named source only fires for its
            // own subject, which is what lets a scene give one hero its own effect.
            if (followsFocus || subject.empty() || subject == s.subject) {
                return ActivationWindow{s.start, s.end, &s};
            }
        }
        return std::nullopt;
    }
    return std::nullopt;
}

float envelopeRamp(float x, float width) { return smoothRamp(x, width); }

float timingEnvelope(const Timing& timing, double local, double windowLength) {
    if (local < 0.0) {
        return 0.0f;
    }
    const double lifetime = timing.lifetime > 0.0 ? timing.lifetime : windowLength;
    if (std::isfinite(lifetime) && local >= lifetime) {
        return 0.0f;
    }
    float envelope = 1.0f;
    if (timing.fadeIn > 0.0) {
        envelope *= smoothRamp(static_cast<float>(local), static_cast<float>(timing.fadeIn));
    }
    if (timing.fadeOut > 0.0 && std::isfinite(lifetime)) {
        envelope *= smoothRamp(static_cast<float>(lifetime - local), static_cast<float>(timing.fadeOut));
    }
    return envelope;
}

} // namespace avgen::world
