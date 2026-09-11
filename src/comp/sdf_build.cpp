#include "comp/sdf_build.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::comp {

namespace {
constexpr float kInf = 1e20f;
} // namespace

void distanceTransform1d(std::vector<float>& f, std::vector<float>& d, std::vector<int>& v, std::vector<float>& z) {
    const int n = static_cast<int>(f.size());
    if (n == 0) {
        return;
    }
    d.assign(static_cast<std::size_t>(n), 0.0f);
    v.assign(static_cast<std::size_t>(n), 0);
    z.assign(static_cast<std::size_t>(n) + 1, 0.0f);
    const auto at = [](auto& c, int i) -> auto& { return c[static_cast<std::size_t>(i)]; };
    int k = 0;
    at(v, 0) = 0;
    at(z, 0) = -kInf;
    at(z, 1) = kInf;
    for (int q = 1; q < n; ++q) {
        float s = 0.0f;
        for (;;) {
            const auto vk = static_cast<float>(at(v, k));
            const auto fq = at(f, q);
            const auto fv = at(f, at(v, k));
            const auto qf = static_cast<float>(q);
            s = ((fq + qf * qf) - (fv + vk * vk)) / (2.0f * qf - 2.0f * vk);
            if (s <= at(z, k) && k > 0) {
                --k;
                continue;
            }
            break;
        }
        ++k;
        at(v, k) = q;
        at(z, k) = s;
        at(z, k + 1) = kInf;
    }
    k = 0;
    for (int q = 0; q < n; ++q) {
        while (at(z, k + 1) < static_cast<float>(q)) {
            ++k;
        }
        const int vk = at(v, k);
        const auto dx = static_cast<float>(q - vk);
        at(d, q) = dx * dx + at(f, vk);
    }
    f.swap(d);
}

namespace {

// In place: a grid of 0 (seed) / kInf (not a seed) becomes the squared distance to the nearest seed.
void transform2d(std::vector<float>& grid, std::uint32_t width, std::uint32_t height) {
    std::vector<float> line(std::max(width, height));
    std::vector<float> d;
    std::vector<int> v;
    std::vector<float> z;
    for (std::uint32_t y = 0; y < height; ++y) {
        const auto row = static_cast<std::ptrdiff_t>(y) * width;
        line.assign(grid.begin() + row, grid.begin() + row + width);
        distanceTransform1d(line, d, v, z);
        std::copy(line.begin(), line.end(), grid.begin() + row);
    }
    line.resize(height);
    for (std::uint32_t x = 0; x < width; ++x) {
        for (std::uint32_t y = 0; y < height; ++y) {
            line[y] = grid[static_cast<std::size_t>(y) * width + x];
        }
        distanceTransform1d(line, d, v, z);
        for (std::uint32_t y = 0; y < height; ++y) {
            grid[static_cast<std::size_t>(y) * width + x] = line[y];
        }
    }
}

} // namespace

SdfBitmap buildSdf(const std::uint8_t* coverage, std::uint32_t width, std::uint32_t height, std::uint32_t scale,
                   float spreadTexels) {
    SdfBitmap out;
    if (coverage == nullptr || width == 0 || height == 0 || scale == 0 || spreadTexels <= 0.0f) {
        return out;
    }
    const std::size_t count = static_cast<std::size_t>(width) * height;
    std::vector<float> toSolid(count);  // squared distance to the nearest covered texel
    std::vector<float> toEmpty(count);  // squared distance to the nearest uncovered texel
    for (std::size_t i = 0; i < count; ++i) {
        const bool solid = coverage[i] >= 128;
        toSolid[i] = solid ? 0.0f : kInf;
        toEmpty[i] = solid ? kInf : 0.0f;
    }
    transform2d(toSolid, width, height);
    transform2d(toEmpty, width, height);

    out.width = width / scale;
    out.height = height / scale;
    if (out.width == 0 || out.height == 0) {
        out.width = 0;
        out.height = 0;
        return out;
    }
    out.texels.resize(static_cast<std::size_t>(out.width) * out.height);
    const float invScale = 1.0f / static_cast<float>(scale);
    const float perBlock = 1.0f / static_cast<float>(scale * scale);
    for (std::uint32_t y = 0; y < out.height; ++y) {
        for (std::uint32_t x = 0; x < out.width; ++x) {
            float sum = 0.0f;
            for (std::uint32_t sy = 0; sy < scale; ++sy) {
                const std::size_t row = static_cast<std::size_t>(y * scale + sy) * width + x * scale;
                for (std::uint32_t sx = 0; sx < scale; ++sx) {
                    // Positive inside the glyph, so the shader's "> 0.5" reads as "on the glyph".
                    sum += std::sqrt(toEmpty[row + sx]) - std::sqrt(toSolid[row + sx]);
                }
            }
            const float texels = sum * perBlock * invScale; // high-res pixels -> 1x texels
            const float encoded = std::clamp(0.5f + texels / (2.0f * spreadTexels), 0.0f, 1.0f);
            out.texels[static_cast<std::size_t>(y) * out.width + x] =
                static_cast<std::uint8_t>(std::lround(encoded * 255.0f));
        }
    }
    return out;
}

} // namespace avgen::comp
