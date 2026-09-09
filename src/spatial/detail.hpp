#pragma once

// Internal helpers shared by the spatial/*.cpp translation units (not part of the public API):
// an FNV-1a structural hasher and typed JSON readers with the error style of scene/procedural.cpp.

#include "core/error.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <bit>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace avgen::spatial::detail {

// FNV-1a 64 over bit patterns (-0 hashes as +0 so equal values hash equally).
class Fnv {
public:
    void u8(std::uint8_t v) { h_ = (h_ ^ v) * 0x100000001b3ULL; }
    void u32(std::uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            u8(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFu));
        }
    }
    void u64(std::uint64_t v) {
        u32(static_cast<std::uint32_t>(v));
        u32(static_cast<std::uint32_t>(v >> 32));
    }
    void i32(std::int32_t v) { u32(std::bit_cast<std::uint32_t>(v)); }
    void f32(float v) { u32(std::bit_cast<std::uint32_t>(v == 0.0f ? 0.0f : v)); }
    void boolean(bool v) { u8(v ? 1u : 0u); }
    void v2(const glm::vec2& v) {
        f32(v.x);
        f32(v.y);
    }
    void v3(const glm::vec3& v) {
        f32(v.x);
        f32(v.y);
        f32(v.z);
    }
    void v4(const glm::vec4& v) {
        f32(v.x);
        f32(v.y);
        f32(v.z);
        f32(v.w);
    }
    void str(std::string_view s) {
        u64(s.size());
        for (const char c : s) {
            u8(static_cast<std::uint8_t>(c));
        }
    }
    void bytes(const void* data, std::size_t size) {
        const auto* p = static_cast<const std::uint8_t*>(data);
        for (std::size_t i = 0; i < size; ++i) {
            u8(p[i]);
        }
    }
    [[nodiscard]] std::uint64_t value() const { return h_; }

private:
    std::uint64_t h_ = 0xcbf29ce484222325ULL;
};

// ---- JSON --------------------------------------------------------------------------------------

inline nlohmann::json vecToJson(const glm::vec2& v) {
    return nlohmann::json::array({v.x, v.y});
}
inline nlohmann::json vecToJson(const glm::vec3& v) {
    return nlohmann::json::array({v.x, v.y, v.z});
}
inline nlohmann::json vecToJson(const glm::vec4& v) {
    return nlohmann::json::array({v.x, v.y, v.z, v.w});
}

template <glm::length_t N>
Result<glm::vec<N, float>> readVecN(const nlohmann::json& j, const char* key, const glm::vec<N, float>& def) {
    if (!j.contains(key)) {
        return def;
    }
    const nlohmann::json& a = j.at(key);
    if (!a.is_array() || a.size() != static_cast<std::size_t>(N)) {
        return fail("'{}' must be an array of {} numbers", key, static_cast<int>(N));
    }
    glm::vec<N, float> out{};
    for (glm::length_t i = 0; i < N; ++i) {
        const nlohmann::json& e = a.at(static_cast<std::size_t>(i));
        if (!e.is_number()) {
            return fail("'{}' must be an array of {} numbers", key, static_cast<int>(N));
        }
        out[i] = e.get<float>();
    }
    return out;
}
inline Result<glm::vec2> readVec2(const nlohmann::json& j, const char* key, const glm::vec2& def) {
    return readVecN<2>(j, key, def);
}
inline Result<glm::vec3> readVec3(const nlohmann::json& j, const char* key, const glm::vec3& def) {
    return readVecN<3>(j, key, def);
}
inline Result<glm::vec4> readVec4(const nlohmann::json& j, const char* key, const glm::vec4& def) {
    return readVecN<4>(j, key, def);
}

inline Result<float> readFloat(const nlohmann::json& j, const char* key, float def) {
    if (!j.contains(key)) {
        return def;
    }
    const nlohmann::json& v = j.at(key);
    if (!v.is_number()) {
        return fail("'{}' must be a number", key);
    }
    return v.get<float>();
}

inline Result<int> readInt(const nlohmann::json& j, const char* key, int def) {
    if (!j.contains(key)) {
        return def;
    }
    const nlohmann::json& v = j.at(key);
    if (!v.is_number_integer()) {
        return fail("'{}' must be an integer", key);
    }
    return v.get<int>();
}

inline Result<std::uint32_t> readU32(const nlohmann::json& j, const char* key, std::uint32_t def) {
    if (!j.contains(key)) {
        return def;
    }
    const nlohmann::json& v = j.at(key);
    if (!v.is_number_unsigned() && !(v.is_number_integer() && v.get<long long>() >= 0)) {
        return fail("'{}' must be a non-negative integer", key);
    }
    return v.get<std::uint32_t>();
}

inline Result<bool> readBool(const nlohmann::json& j, const char* key, bool def) {
    if (!j.contains(key)) {
        return def;
    }
    const nlohmann::json& v = j.at(key);
    if (!v.is_boolean()) {
        return fail("'{}' must be a boolean", key);
    }
    return v.get<bool>();
}

inline Result<std::string> readString(const nlohmann::json& j, const char* key, const std::string& def) {
    if (!j.contains(key)) {
        return def;
    }
    const nlohmann::json& v = j.at(key);
    if (!v.is_string()) {
        return fail("'{}' must be a string", key);
    }
    return v.get<std::string>();
}

template <typename Enum>
Result<void> readEnum(const nlohmann::json& j, const char* key, Enum& target,
                      std::optional<Enum> (*fromName)(std::string_view), const char* what) {
    if (!j.contains(key)) {
        return {};
    }
    auto name = readString(j, key, "");
    if (!name) {
        return std::unexpected(name.error());
    }
    const auto value = fromName(*name);
    if (!value) {
        return fail("unknown {} '{}'", what, *name);
    }
    target = *value;
    return {};
}

// Assigns `target` from `reader(j, key, target)` or returns the error from the enclosing function.
#define AVGEN_SPATIAL_READ(target, key, reader)                                                     \
    do {                                                                                            \
        auto value_ = reader(j, key, target);                                                       \
        if (!value_) {                                                                              \
            return std::unexpected(value_.error());                                                 \
        }                                                                                           \
        target = *value_;                                                                           \
    } while (false)

#define AVGEN_SPATIAL_READ_ENUM(target, key, fromName, what)                                        \
    do {                                                                                            \
        if (auto ok_ = ::avgen::spatial::detail::readEnum(j, key, target, fromName, what); !ok_) {   \
            return std::unexpected(ok_.error());                                                    \
        }                                                                                           \
    } while (false)

} // namespace avgen::spatial::detail
