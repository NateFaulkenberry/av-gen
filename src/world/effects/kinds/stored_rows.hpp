#pragma once

// Reading a kind's stored rows (`EffectInstance::values` under `<key>/<leaf>`) with the key built on
// the stack, so a producer's per-frame path allocates nothing -- Space Warp's `KeyBuf`, shared by the
// Wave 2 DF kinds instead of copied into each. Header-only and file-local in use: every includer
// instantiates it over its own `kFields` and key.

#include "world/effects/effect_registry.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>

namespace avgen::world::kinds {

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

// A row reader over one kind's table. `defaultOf` searches the table, so the resolver and the
// registry cannot disagree about a default.
class StoredRows {
public:
    constexpr StoredRows(std::string_view key, std::span<const EffectField> fields) : key_(key), fields_(fields) {}

    [[nodiscard]] float f(const EffectInstance& e, std::string_view leaf) const {
        return e.values.getFloat(KeyBuf(key_, leaf).view(), field(leaf).storedDefault);
    }
    [[nodiscard]] int choice(const EffectInstance& e, std::string_view leaf) const {
        const EffectField& fd = field(leaf);
        const int i = static_cast<int>(f(e, leaf) + 0.5f);
        return std::clamp(i, 0, std::max(fd.choiceCount - 1, 0));
    }
    [[nodiscard]] glm::vec3 rgb(const EffectInstance& e, std::string_view leaf) const {
        return e.values.getColor(KeyBuf(key_, leaf).view(), field(leaf).storedColor);
    }
    void set(EffectInstance& e, std::string_view leaf, float v) const { e.values.setFloat(KeyBuf(key_, leaf).view(), v); }
    void setRgb(EffectInstance& e, std::string_view leaf, glm::vec3 v) const {
        e.values.setColor(KeyBuf(key_, leaf).view(), v);
    }

private:
    [[nodiscard]] const EffectField& field(std::string_view leaf) const {
        for (const EffectField& fd : fields_) {
            if (leaf == fd.leaf) {
                return fd;
            }
        }
        return fields_.front(); // a misspelt leaf in a producer reads the first row; tests catch it
    }
    std::string_view key_;
    std::span<const EffectField> fields_;
};

// FNV-1a of an id, folded into a small float: a per-instance seed that is the same on every run.
[[nodiscard]] inline float seedOf(std::string_view id) {
    std::uint32_t h = 2166136261u;
    for (const char c : id) {
        h ^= static_cast<std::uint8_t>(c);
        h *= 16777619u;
    }
    return static_cast<float>(h % 997u);
}

[[nodiscard]] inline bool finite3(glm::vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

[[nodiscard]] inline float smooth01(float edge0, float edge1, float x) {
    const float t = std::clamp((x - edge0) / std::max(edge1 - edge0, 1e-6f), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Where an entity owner's field is centred: the middle of its drawn bounds (its origin when it has
// none), plus `offset`. False for an owner the scene cannot show.
[[nodiscard]] inline bool ownerCentre(const EffectInstance& e, const EffectContext& ctx, glm::vec3 offset,
                                      glm::vec3& centre, float& radius, NodeView& view) {
    if (ctx.scene == nullptr || !ctx.scene->nodeView(e.owner.name, view)) {
        return false;
    }
    if (view.hasBounds) {
        centre = 0.5f * (view.boundsMin + view.boundsMax) + offset;
        radius = 0.5f * glm::length(view.boundsMax - view.boundsMin);
    } else {
        centre = glm::vec3(view.world[3]) + offset;
        radius = 0.0f;
    }
    return finite3(centre);
}

} // namespace avgen::world::kinds
