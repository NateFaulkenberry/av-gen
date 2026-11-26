#pragma once

// The environment the path tracer sees (ADR-2240).
//
// Two questions, deliberately kept apart because the rasteriser keeps them apart:
//
//   * what LIGHTS the scene from infinity -- the image-based environment when the scene has an HDR map (the map times
//     `environmentIntensity`, turned by `environmentRotation`), otherwise the analytic sky the raster's IBL is built
//     from, otherwise the flat background colour (the tracer's long-standing fallback);
//   * what the CAMERA SEES where nothing is hit -- `scene::skyBackgroundFor`, the same rule the raster's background pass
//     follows: the flat background colour, the map, or the analytic sky, each at the visible sky's own intensity and
//     tint (`skyIntensity`, `skyTint`).
//
// Before ADR-2240 an HDR map was reported "degraded" and replaced by the analytic sky. The analytic sky defaults to
// enabled and takes the key light as its sun, so a night scene lit by a moon and an HDR night sky (Neon Dreams) was
// traced under a daylight sky. A substitution is not a degradation; this module removes it.
//
// The map is importance-sampled (piecewise-constant over texels, rows weighted by sin(theta)) so that next-event
// estimation can find it, and MIS-weighted against BSDF sampling the way emissive geometry is.

#include "scene/scene_types.hpp"
#include "scene/sky.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace avgen::pathtrace {

// A direction and the density it was drawn with (solid angle), and the radiance it carries.
struct EnvironmentSample {
    glm::vec3 direction{0.0f};
    glm::vec3 radiance{0.0f};
    float pdf = 0.0f;
    bool valid = false;
};

class Environment {
public:
    enum class Lighting : std::uint8_t { Background, AnalyticSky, Map };
    enum class Background : std::uint8_t { FlatColour, AnalyticSky, Map };

    // Resolves both rules from the scene. `textures` is the snapshot's own copy (the map's id indexes it).
    static Environment build(const scene::Environment& env, const std::vector<scene::PunctualLight>& lights,
                             const std::vector<scene::TextureData>& textures);

    [[nodiscard]] Lighting lighting() const { return lighting_; }
    [[nodiscard]] Background background() const { return background_; }

    // Radiance arriving from direction `dir` (world space, pointing away from the scene) for a path that has
    // already scattered: what reflections, GI and shadowed skylight see.
    [[nodiscard]] glm::vec3 radiance(const glm::vec3& dir) const;
    // What a camera ray that hits nothing sees.
    [[nodiscard]] glm::vec3 backgroundRadiance(const glm::vec3& dir) const;

    // True when the lighting is a map that next-event estimation samples (and BSDF misses are then MIS-weighted).
    [[nodiscard]] bool sampled() const { return lighting_ == Lighting::Map && totalWeight_ > 0.0; }
    [[nodiscard]] EnvironmentSample sample(glm::vec2 u) const;
    // The solid-angle density `sample` has for `dir` (0 when not sampled).
    [[nodiscard]] float pdf(const glm::vec3& dir) const;

    // World <-> map frame (rotation about +Y), exactly shaders/common.wgsl `envRotate` and its inverse.
    [[nodiscard]] glm::vec3 toMap(const glm::vec3& world) const;
    [[nodiscard]] glm::vec3 toWorld(const glm::vec3& map) const;

    // For the capability report.
    [[nodiscard]] std::uint32_t mapWidth() const { return width_; }
    [[nodiscard]] std::uint32_t mapHeight() const { return height_; }

private:
    [[nodiscard]] glm::vec3 texel(int x, int y) const;        // raw map texel, wrapped in x and clamped in y
    [[nodiscard]] glm::vec3 mapBilinear(const glm::vec3& mapDir) const;
    [[nodiscard]] glm::vec3 mapNearest(const glm::vec3& mapDir) const;

    Lighting lighting_ = Lighting::Background;
    Background background_ = Background::FlatColour;
    glm::vec3 backgroundColour_{0.0f};
    scene::SkyRuntime sky_{};
    float skyBackgroundScale_ = 1.0f; // the analytic sky behind the scene: the raster's cube factor, see build()
    float visibleIntensity_ = 1.0f;   // skyIntensity
    glm::vec3 visibleTint_{1.0f};     // skyTint
    float intensity_ = 1.0f;          // environmentIntensity (lighting from the map)
    float cosRot_ = 1.0f;
    float sinRot_ = 0.0f;

    // The map, as linear RGB floats, and its sampling distribution.
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::vector<glm::vec3> pixels_;
    std::vector<float> marginalCdf_;    // height_ + 1 entries, 0 .. 1
    std::vector<float> conditionalCdf_; // height_ * (width_ + 1) entries, each row 0 .. 1
    std::vector<float> rowWeight_;      // per row, unnormalised
    std::vector<float> texelWeight_;    // per texel, unnormalised (luminance * sin(theta))
    double totalWeight_ = 0.0;
};

} // namespace avgen::pathtrace
