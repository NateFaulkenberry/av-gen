#include "pathtrace/environment.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace avgen::pathtrace {
namespace {

constexpr float kPi = 3.14159265358979323846f;

[[nodiscard]] float luminance(const glm::vec3& c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }

// shaders/sky_background.wgsl `skyEquirectUv` (and environment.wgsl `equirectUv`): u from the azimuth, 0.5 along +X;
// v from the polar angle, 0 at +Y (the first row of the image).
[[nodiscard]] glm::vec2 equirectUv(const glm::vec3& d) {
    const float phi = std::atan2(d.z, d.x);
    const float theta = std::acos(std::clamp(d.y, -1.0f, 1.0f));
    return {0.5f + phi / (2.0f * kPi), theta / kPi};
}

[[nodiscard]] glm::vec3 equirectDirection(float u, float v) {
    const float phi = (u - 0.5f) * 2.0f * kPi;
    const float theta = v * kPi;
    const float s = std::sin(theta);
    return {s * std::cos(phi), std::cos(theta), s * std::sin(phi)};
}

// The first index i with cdf[i + 1] > x, over a CDF of n + 1 entries starting at 0.
[[nodiscard]] std::uint32_t findInterval(const float* cdf, std::uint32_t n, float x) {
    const float* begin = cdf + 1;
    const float* it = std::upper_bound(begin, begin + n, x);
    const auto i = static_cast<std::uint32_t>(it - begin);
    return std::min(i, n - 1);
}

} // namespace

Environment Environment::build(const scene::Environment& env, const std::vector<scene::PunctualLight>& lights,
                               const std::vector<scene::TextureData>& textures) {
    Environment out;
    out.backgroundColour_ = env.backgroundColor;
    out.cosRot_ = std::cos(env.environmentRotation);
    out.sinRot_ = std::sin(env.environmentRotation);
    out.visibleIntensity_ = std::max(env.skyIntensity, 0.0f);
    out.visibleTint_ = env.skyTint;
    out.intensity_ = env.environmentIntensity;
    out.sky_ = scene::resolveSky(env.sky, lights);

    const scene::TextureId id = env.environmentMap;
    const bool haveMap = id != scene::kInvalidTexture && id < textures.size() && textures[id].isHdr() &&
                         textures[id].width > 0 && textures[id].height > 0 &&
                         textures[id].data.size() >= static_cast<std::size_t>(textures[id].width) *
                                                         textures[id].height * 16;
    const bool skyIbl = !haveMap && env.sky.enabled;

    if (haveMap) {
        out.lighting_ = Lighting::Map;
    } else if (env.sky.enabled) {
        out.lighting_ = Lighting::AnalyticSky;
    } else {
        out.lighting_ = Lighting::Background;
    }

    // The raster's background rule, verbatim (scene_types.hpp skyBackgroundFor; sky_background.wgsl skyBackgroundAt).
    switch (scene::skyBackgroundFor(env, haveMap || skyIbl, skyIbl)) {
    case scene::SkyBackground::FlatColour: out.background_ = Background::FlatColour; break;
    case scene::SkyBackground::Analytic: out.background_ = Background::AnalyticSky; break;
    case scene::SkyBackground::IblCube: out.background_ = haveMap ? Background::Map : Background::AnalyticSky; break;
    }
    // The analytic sky behind the scene with no map is the IBL cube, which the raster builds from the sky at its
    // `intensity` and then draws times `params.w` -- the sky's intensity again (scene_renderer.cpp, the IBL's
    // `envIntensity`). Matched here so the camera sees what the raster draws. With a map, the analytic background
    // (ADR-345) is drawn from the sky's own model once.
    out.skyBackgroundScale_ = skyIbl ? env.sky.intensity : 1.0f;

    if (!haveMap) return out;

    const scene::TextureData& tex = textures[id];
    out.width_ = tex.width;
    out.height_ = tex.height;
    const std::size_t n = static_cast<std::size_t>(tex.width) * tex.height;
    out.pixels_.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        float rgba[4];
        std::memcpy(rgba, tex.data.data() + i * 16, sizeof(rgba));
        glm::vec3 c{rgba[0], rgba[1], rgba[2]};
        for (int k = 0; k < 3; ++k) {
            if (!std::isfinite(c[k]) || c[k] < 0.0f) c[k] = 0.0f; // a damaged texel must not poison the CDF
        }
        out.pixels_[i] = c;
    }

    // Piecewise-constant distribution over texels: luminance times sin(theta) at the row's centre, so the density in
    // solid angle follows the radiance rather than the texel count (equirect rows near the poles cover less sky).
    const std::uint32_t w = out.width_;
    const std::uint32_t h = out.height_;
    out.texelWeight_.assign(n, 0.0f);
    out.rowWeight_.assign(h, 0.0f);
    out.conditionalCdf_.assign(static_cast<std::size_t>(h) * (w + 1), 0.0f);
    out.marginalCdf_.assign(h + 1, 0.0f);
    double total = 0.0;
    for (std::uint32_t y = 0; y < h; ++y) {
        const float sinTheta = std::sin(kPi * (static_cast<float>(y) + 0.5f) / static_cast<float>(h));
        double row = 0.0;
        float* cdf = out.conditionalCdf_.data() + static_cast<std::size_t>(y) * (w + 1);
        for (std::uint32_t x = 0; x < w; ++x) {
            const float weight = luminance(out.pixels_[static_cast<std::size_t>(y) * w + x]) * sinTheta;
            out.texelWeight_[static_cast<std::size_t>(y) * w + x] = weight;
            row += weight;
            cdf[x + 1] = static_cast<float>(row);
        }
        if (row > 0.0) {
            for (std::uint32_t x = 1; x <= w; ++x) cdf[x] = static_cast<float>(cdf[x] / row);
        }
        cdf[w] = row > 0.0 ? 1.0f : 0.0f;
        out.rowWeight_[y] = static_cast<float>(row);
        total += row;
        out.marginalCdf_[y + 1] = static_cast<float>(total);
    }
    if (total > 0.0) {
        for (std::uint32_t y = 1; y <= h; ++y) out.marginalCdf_[y] = static_cast<float>(out.marginalCdf_[y] / total);
        out.marginalCdf_[h] = 1.0f;
    }
    out.totalWeight_ = total;
    return out;
}

glm::vec3 Environment::toMap(const glm::vec3& d) const {
    // shaders/common.wgsl envRotate.
    return {cosRot_ * d.x + sinRot_ * d.z, d.y, -sinRot_ * d.x + cosRot_ * d.z};
}

glm::vec3 Environment::toWorld(const glm::vec3& m) const {
    return {cosRot_ * m.x - sinRot_ * m.z, m.y, sinRot_ * m.x + cosRot_ * m.z};
}

glm::vec3 Environment::texel(int x, int y) const {
    const int w = static_cast<int>(width_);
    const int h = static_cast<int>(height_);
    x %= w;
    if (x < 0) x += w;
    y = std::clamp(y, 0, h - 1);
    return pixels_[static_cast<std::size_t>(y) * width_ + static_cast<std::size_t>(x)];
}

glm::vec3 Environment::mapNearest(const glm::vec3& mapDir) const {
    const glm::vec2 uv = equirectUv(mapDir);
    const int x = static_cast<int>(std::floor(uv.x * static_cast<float>(width_)));
    const int y = static_cast<int>(std::floor(uv.y * static_cast<float>(height_)));
    return texel(x, y);
}

glm::vec3 Environment::mapBilinear(const glm::vec3& mapDir) const {
    const glm::vec2 uv = equirectUv(mapDir);
    const float fx = uv.x * static_cast<float>(width_) - 0.5f;
    const float fy = uv.y * static_cast<float>(height_) - 0.5f;
    const int x0 = static_cast<int>(std::floor(fx));
    const int y0 = static_cast<int>(std::floor(fy));
    const float tx = fx - static_cast<float>(x0);
    const float ty = fy - static_cast<float>(y0);
    const glm::vec3 a = glm::mix(texel(x0, y0), texel(x0 + 1, y0), tx);
    const glm::vec3 b = glm::mix(texel(x0, y0 + 1), texel(x0 + 1, y0 + 1), tx);
    return glm::mix(a, b, ty);
}

glm::vec3 Environment::radiance(const glm::vec3& dir) const {
    switch (lighting_) {
    case Lighting::Map:
        // Nearest texel: the same piecewise-constant function `sample` and `pdf` describe, so light sampling and
        // BSDF sampling estimate one integral and their MIS weights sum to one.
        return mapNearest(toMap(dir)) * intensity_;
    case Lighting::AnalyticSky: return scene::skyRadiance(sky_, toMap(dir));
    case Lighting::Background: return backgroundColour_;
    }
    return backgroundColour_;
}

glm::vec3 Environment::backgroundRadiance(const glm::vec3& dir) const {
    switch (background_) {
    case Background::FlatColour: return backgroundColour_;
    case Background::Map: return mapBilinear(toMap(dir)) * visibleIntensity_ * visibleTint_;
    case Background::AnalyticSky:
        return scene::skyRadiance(sky_, toMap(dir)) * skyBackgroundScale_ * visibleIntensity_ * visibleTint_;
    }
    return backgroundColour_;
}

EnvironmentSample Environment::sample(glm::vec2 u) const {
    EnvironmentSample s;
    if (!sampled()) return s;
    const std::uint32_t y = findInterval(marginalCdf_.data(), height_, u.y);
    const float* cdf = conditionalCdf_.data() + static_cast<std::size_t>(y) * (width_ + 1);
    const std::uint32_t x = findInterval(cdf, width_, u.x);
    // Where in the texel: the remainder of each coordinate, rescaled, so the sample is uniform within it.
    const float rowLo = marginalCdf_[y];
    const float rowHi = marginalCdf_[y + 1];
    const float colLo = cdf[x];
    const float colHi = cdf[x + 1];
    const float fy = rowHi > rowLo ? std::clamp((u.y - rowLo) / (rowHi - rowLo), 0.0f, 0.999999f) : 0.5f;
    const float fx = colHi > colLo ? std::clamp((u.x - colLo) / (colHi - colLo), 0.0f, 0.999999f) : 0.5f;
    const float uu = (static_cast<float>(x) + fx) / static_cast<float>(width_);
    const float vv = (static_cast<float>(y) + fy) / static_cast<float>(height_);
    const glm::vec3 mapDir = equirectDirection(uu, vv);
    // Radiance and density are re-derived from the direction, through the same texel lookup `radiance` and `pdf`
    // use, so a sample that rounds across a texel edge is still described by one consistent pair.
    s.direction = toWorld(mapDir);
    s.radiance = mapNearest(mapDir) * intensity_;
    s.pdf = pdf(s.direction);
    s.valid = s.pdf > 0.0f && std::isfinite(s.pdf);
    return s;
}

float Environment::pdf(const glm::vec3& dir) const {
    if (!sampled()) return 0.0f;
    const glm::vec3 m = toMap(dir);
    const glm::vec2 uv = equirectUv(m);
    const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - m.y * m.y));
    if (sinTheta <= 1e-6f) return 0.0f;
    // The texel `mapNearest` reads: x wrapped, y clamped (Environment::texel).
    int x = static_cast<int>(std::floor(uv.x * static_cast<float>(width_))) % static_cast<int>(width_);
    if (x < 0) x += static_cast<int>(width_);
    const int y = std::clamp(static_cast<int>(std::floor(uv.y * static_cast<float>(height_))), 0,
                             static_cast<int>(height_) - 1);
    const float weight = texelWeight_[static_cast<std::size_t>(y) * width_ + static_cast<std::size_t>(x)];
    const float pdfUv = static_cast<float>(weight / totalWeight_) * static_cast<float>(width_) *
                        static_cast<float>(height_);
    return pdfUv / (2.0f * kPi * kPi * sinTheta);
}

} // namespace avgen::pathtrace
