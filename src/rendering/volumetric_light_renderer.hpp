#pragma once

// Volumetric lighting (ADR-2210): a camera-aligned froxel grid that carries the environment's air from the
// eye out to `environment.volumetricLight.distance` and lights it with the scene's own lights and shadow
// views. `shaders/volumetric_light.wgsl` holds the two compute passes (inject, integrate); the apply is one
// trilinear tap inside the volume composite (`fs_composite`, ADR-2217), so the frame gains no full-screen
// pass.
//
// One lighting architecture (ADR-2210/2214): the inject reads the frame uniforms' eight lights, the packed
// light buffer, the froxel light lists, the shadow uniforms and the shadow atlas that the surfaces read --
// re-bound here for the compute stage, because the frame group's layout is fragment-only. No light list,
// shadow map or light type of its own.
//
// One medium (ADR-2211): the air is the environment's `volume*` / `fog*` controls, through the same
// height-fog profile the march and the surface fog use. With volumetric lighting on, the march keeps only
// the placed media and the surface fog takes the air over at `distance` (VolumeRenderer::surfaceFogStart).
// `airDensityAt` is the medium on the CPU, for the tracer and for the tests.
//
// No history (ADR-2212): a frame is a pure function of the scene, the time and the camera.
//
// Off is free: `active(scene)` false means no allocation and no pass, and the frame is byte-identical to
// a build without this renderer.

#include "core/error.hpp"
#include "core/time.hpp"
#include "rendering/render_quality.hpp"
#include "rendering/volumetric_light_medium.hpp"

#include <glm/glm.hpp>
#include <webgpu/webgpu_cpp.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace avgen::gpu {
class Context;
class FrameTimeline;
class ShaderLibrary;
} // namespace avgen::gpu

namespace avgen::scene {
struct Scene;
struct Environment;
struct PunctualLight;
} // namespace avgen::scene

namespace avgen::rendering {

struct VolumetricLightStats {
    bool active = false;
    VolumetricLightDecline decline = VolumetricLightDecline::Off;
    std::uint32_t gridX = 0, gridY = 0, gridZ = 0;
    std::uint32_t shadowTaps = 0;
    std::uint32_t subsamples = 0;
    std::uint32_t localLightsPerFroxel = 0;
    std::uint32_t shadowedLights = 0; // frame lights whose air is shadowed this frame
    float distance = 0.0f;            // the hand-over distance (m)
    std::uint64_t memoryBytes = 0;    // the two 3D textures
};

// What the volume composite needs to apply the grid (VolumeRenderer::update).
struct VolumetricLightApply {
    bool active = false;
    wgpu::TextureView integrated; // rgb = in-scatter from the eye to the far end of slice z, a = transmittance
    float nearDepth = 0.0f;       // view depth where slice 0 ends at w = 0 (the exponential's origin)
    float logRatio = 0.0f;        // log(far / near)
    float distance = 0.0f;        // the hand-over distance (m, along the ray)
    std::uint32_t slices = 0;
};

// The frame's resources the inject reads, owned by SceneRenderer (ADR-2210's compute-visible binding).
struct VolumetricLightInputs {
    wgpu::Buffer frameUniforms;
    wgpu::Buffer lights;         // the packed scene lights (directional first)
    wgpu::Buffer clusters;       // the froxel light lists
    wgpu::Buffer shadowUniforms;
    wgpu::TextureView shadowAtlas;
    wgpu::Sampler shadowSampler; // comparison
    wgpu::TextureView terrainHeight;
    wgpu::Buffer particleGlow;   // ParticleRenderer::glowBuffer()
    std::uint32_t particleGlowSystems = 0;
};

class VolumetricLightRenderer {
public:
    VolumetricLightRenderer(gpu::Context& context, gpu::ShaderLibrary& shaders);
    ~VolumetricLightRenderer();
    VolumetricLightRenderer(const VolumetricLightRenderer&) = delete;
    VolumetricLightRenderer& operator=(const VolumetricLightRenderer&) = delete;

    [[nodiscard]] Result<void> init();
    [[nodiscard]] Result<void> reload();

    // Whether this scene's air is carried by the froxel grid, and if not, why.
    [[nodiscard]] static VolumetricLightDecline decline(const scene::Scene& scene) {
        return volumetricLightDecline(scene);
    }
    [[nodiscard]] static bool active(const scene::Scene& scene) { return volumetricLightActive(scene); }

    // Per frame, after the light buffer, clusters and shadow views are packed for this frame. `packedIndexOf`
    // gives, for each of the frame uniforms' lights (in their order), its index in the packed light buffer
    // or -1. Does nothing (and clears the stats) when inactive.
    void update(const scene::Scene& scene, const FrameTime& time, std::uint32_t width, std::uint32_t height,
                const std::array<int, 8>& packedIndexOf, const std::array<bool, 8>& shadowsAir,
                const QualitySettings& quality, std::uint32_t particleGlowSystems);
    // Encodes inject and integrate. No-op when the last update() found it inactive.
    // `allowDiagnosticRepeat` false (the Offline tier) ignores AVGEN_VOLUMETRIC_REPEAT, and says so once.
    void encode(wgpu::CommandEncoder& encoder, const VolumetricLightInputs& inputs, bool allowDiagnosticRepeat = true);

    // For the volume composite: the integrated grid and how to address it (`active` false when off).
    [[nodiscard]] VolumetricLightApply apply() const;
    [[nodiscard]] const VolumetricLightStats& stats() const { return stats_; }
    void setTimeline(gpu::FrameTimeline* timeline);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    VolumetricLightStats stats_;
};

} // namespace avgen::rendering
