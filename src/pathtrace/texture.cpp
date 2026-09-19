#include "pathtrace/texture.hpp"

#include "core/color.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::pathtrace {
namespace {

[[nodiscard]] glm::vec4 rawTexel(const scene::TextureData& tex, int x, int y) {
    const auto idx = static_cast<std::size_t>(y) * tex.width + static_cast<std::size_t>(x);
    if (tex.format == scene::TextureFormat::Rgba32Float) {
        const auto* f = reinterpret_cast<const float*>(tex.data.data()) + idx * 4;
        return {f[0], f[1], f[2], f[3]};
    }
    const std::uint8_t* b = tex.data.data() + idx * 4;
    constexpr float inv = 1.0f / 255.0f;
    return {b[0] * inv, b[1] * inv, b[2] * inv, b[3] * inv};
}

} // namespace

int wrapTexel(int i, int size, scene::WrapMode mode) {
    if (size <= 0) return 0;
    switch (mode) {
    case scene::WrapMode::Clamp:
        return std::clamp(i, 0, size - 1);
    case scene::WrapMode::Mirror: {
        // Period is 2*size: 0..size-1 forward, then size-1..0 back.
        const int period = 2 * size;
        int m = i % period;
        if (m < 0) m += period;
        return m < size ? m : period - 1 - m;
    }
    case scene::WrapMode::Repeat:
    default: {
        int m = i % size;
        if (m < 0) m += size;
        return m;
    }
    }
}

glm::vec4 texelLinear(const scene::TextureData& tex, int x, int y) {
    const glm::vec4 raw = rawTexel(tex, x, y);
    if (tex.format != scene::TextureFormat::Rgba8Srgb) return raw;
    // Decode RGB only. Alpha is coverage and is already linear.
    const glm::vec3 lin = color::srgbToLinear(glm::vec3(raw));
    return {lin.x, lin.y, lin.z, raw.w};
}

glm::vec4 sampleTexture(const scene::TextureData& tex, const scene::TextureRef& ref, glm::vec2 uv) {
    if (!tex.valid() || tex.width == 0 || tex.height == 0) return glm::vec4(1.0f);

    const auto w = static_cast<int>(tex.width);
    const auto h = static_cast<int>(tex.height);

    if (!ref.linearFilter) {
        const int x = wrapTexel(static_cast<int>(std::floor(uv.x * static_cast<float>(w))), w, ref.wrapU);
        const int y = wrapTexel(static_cast<int>(std::floor(uv.y * static_cast<float>(h))), h, ref.wrapV);
        return texelLinear(tex, x, y);
    }

    // Half-texel offset: texel centres sit at (i + 0.5) / size.
    const float fx = uv.x * static_cast<float>(w) - 0.5f;
    const float fy = uv.y * static_cast<float>(h) - 0.5f;
    const float x0f = std::floor(fx);
    const float y0f = std::floor(fy);
    const float tx = fx - x0f;
    const float ty = fy - y0f;

    const int x0 = wrapTexel(static_cast<int>(x0f), w, ref.wrapU);
    const int x1 = wrapTexel(static_cast<int>(x0f) + 1, w, ref.wrapU);
    const int y0 = wrapTexel(static_cast<int>(y0f), h, ref.wrapV);
    const int y1 = wrapTexel(static_cast<int>(y0f) + 1, h, ref.wrapV);

    // Decode first, blend second (see the header).
    const glm::vec4 c00 = texelLinear(tex, x0, y0);
    const glm::vec4 c10 = texelLinear(tex, x1, y0);
    const glm::vec4 c01 = texelLinear(tex, x0, y1);
    const glm::vec4 c11 = texelLinear(tex, x1, y1);

    return glm::mix(glm::mix(c00, c10, tx), glm::mix(c01, c11, tx), ty);
}

glm::vec4 sampleSlot(std::span<const scene::TextureData> textures, const scene::TextureRef& ref,
                     glm::vec2 uv, glm::vec4 fallback) {
    if (!ref.valid()) return fallback;
    if (ref.texture >= textures.size()) return fallback;
    const scene::TextureData& tex = textures[ref.texture];
    if (!tex.valid()) return fallback;
    return sampleTexture(tex, ref, uv);
}

} // namespace avgen::pathtrace
