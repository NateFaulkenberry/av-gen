#pragma once

// Structural hashing (FNV-1a over the bit patterns) for scene data that must rebuild when it
// changes: the same construction as procedural.cpp's StructHash (kept identical so hashes of the
// shared value types agree), shared by grammar.cpp and hierarchy.cpp.

#include "scene/scene_types.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <bit>
#include <cstdint>
#include <string_view>

namespace avgen::scene::detail {

class StructHash {
public:
    void u32(std::uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            h_ = (h_ ^ ((v >> (8 * i)) & 0xFFu)) * 0x100000001b3ULL;
        }
    }
    void u64(std::uint64_t v) {
        u32(static_cast<std::uint32_t>(v));
        u32(static_cast<std::uint32_t>(v >> 32));
    }
    void i32(int v) { u32(std::bit_cast<std::uint32_t>(v)); }
    void f32(float v) { u32(std::bit_cast<std::uint32_t>(v == 0.0f ? 0.0f : v)); } // -0 == +0
    void boolean(bool v) { u32(v ? 1u : 0u); }
    void v3(const glm::vec3& v) {
        f32(v.x);
        f32(v.y);
        f32(v.z);
    }
    void quat(const glm::quat& q) {
        f32(q.x);
        f32(q.y);
        f32(q.z);
        f32(q.w);
    }
    void transform(const Transform& t) {
        v3(t.position);
        quat(t.rotation);
        v3(t.scale);
    }
    void str(std::string_view s) {
        u64(s.size());
        for (const char c : s) {
            u32(static_cast<std::uint8_t>(c));
        }
    }
    [[nodiscard]] std::uint64_t value() const { return h_; }

private:
    std::uint64_t h_ = 0xcbf29ce484222325ULL;
};

// Mixes a term into a running 64-bit hash (order-sensitive).
[[nodiscard]] inline std::uint64_t mixHash(std::uint64_t h, std::uint64_t term) {
    h ^= term + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    return h;
}

} // namespace avgen::scene::detail
