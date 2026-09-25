#pragma once

// What the five XFORM kinds (Orbit, Spiral, Float, Shake, Bounce) share. Internal to those files.
//
// Their numbers live in `EffectInstance::values` under `<key>/<leaf>`. The per-frame path must not
// allocate and `storeKey` returns a std::string, so the key is built on the stack here, and a row's
// default comes from the row itself so the resolver and the registry cannot disagree about it.

#include "world/effects/effect_registry.hpp"
#include "world/effects/transform_frame.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <span>
#include <string_view>

namespace avgen::world::xform_rows {

struct KeyBuf {
    std::array<char, 64> chars{};
    std::size_t size = 0;
    KeyBuf(std::string_view key, std::string_view leaf) {
        const std::size_t k = std::min(key.size(), chars.size() - 2);
        std::memcpy(chars.data(), key.data(), k);
        chars[k] = '/';
        const std::size_t n = std::min(leaf.size(), chars.size() - k - 1);
        std::memcpy(chars.data() + k + 1, leaf.data(), n);
        size = k + 1 + n;
    }
    [[nodiscard]] std::string_view view() const { return {chars.data(), size}; }
};

// One kind's rows, read and written by leaf.
struct Rows {
    std::string_view key;
    std::span<const EffectField> fields;

    [[nodiscard]] float defaultOf(std::string_view leaf) const {
        for (const EffectField& f : fields) {
            if (std::string_view(f.leaf) == leaf) {
                return f.storedDefault;
            }
        }
        return 0.0f;
    }
    [[nodiscard]] float get(const EffectInstance& e, std::string_view leaf) const {
        return e.values.getFloat(KeyBuf(key, leaf).view(), defaultOf(leaf));
    }
    [[nodiscard]] bool flag(const EffectInstance& e, std::string_view leaf) const {
        return e.values.getBool(KeyBuf(key, leaf).view(), defaultOf(leaf) > 0.5f);
    }
    [[nodiscard]] int choice(const EffectInstance& e, std::string_view leaf) const {
        return static_cast<int>(std::lround(get(e, leaf)));
    }
    void put(EffectInstance& e, std::string_view leaf, float v) const { e.values.setFloat(KeyBuf(key, leaf).view(), v); }
    void putFlag(EffectInstance& e, std::string_view leaf, bool v) const {
        e.values.setBool(KeyBuf(key, leaf).view(), v);
    }
};

inline constexpr float kPi = 3.14159265358979f;
inline constexpr float kDeg = kPi / 180.0f;

// Transport seconds wrapped to a whole number of `period`s near t, so a phase keeps float precision
// on a long timeline: `fract(t / period)` computed in double, returned in [0, 1).
[[nodiscard]] inline double cycles(double seconds, float period, float phase) {
    const double p = std::max(static_cast<double>(period), 1e-3);
    const double c = seconds / p + static_cast<double>(phase);
    return c - std::floor(c);
}

// Yaw (about +Y), pitch (about +X) and roll (about +Z), in radians, applied yaw outermost.
[[nodiscard]] inline glm::quat yawPitchRoll(float yaw, float pitch, float roll) {
    return glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f)) * glm::angleAxis(pitch, glm::vec3(1.0f, 0.0f, 0.0f)) *
           glm::angleAxis(roll, glm::vec3(0.0f, 0.0f, 1.0f));
}

} // namespace avgen::world::xform_rows
