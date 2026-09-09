#pragma once

// Volumetric atmosphere (ADR-032): a raymarched fog pass encoded after the lit pass and before
// the post chain. `shaders/volume.wgsl` marches at half resolution into this renderer's own
// RGBA16F target (rgb = in-scattered radiance, a = transmittance), reading the scene depth so
// the fog is occluded by geometry, and a second full-resolution pass composites it into the HDR
// target with a depth-aware upsample and `src One, dst SrcAlpha` blending
// (`hdr = scatter + hdr * transmittance`).
//
// Off is free: `Environment::volumeDensity <= 0` means `enabled()` is false, `update()` allocates
// nothing and no pass is encoded, so a scene without volumetrics renders exactly as before.
//
// Bind groups: group 0 is SceneRenderer's frame group (frame uniforms + the grid table
// fields.wgsl declares); group 1 is this renderer's own - 1 = VolumeUniforms, 2 = the field
// block, 3 = the scene depth texture, 4 = the half-res volume texture (the composite pass; the
// march pass binds its own target's placeholder view, which it never reads).

#include "core/error.hpp"
#include "core/time.hpp"
#include "gpu/render_target.hpp"
#include "scene/scene.hpp"

#include <glm/glm.hpp>
#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <memory>

namespace avgen::gpu {
class Context;
class ShaderLibrary;
} // namespace avgen::gpu

namespace avgen::rendering {

class FieldUniforms;

struct VolumeStats {
    std::uint32_t steps = 0;            // raymarch samples per pixel this frame (0 = fog off)
    bool halfResolution = true;         // the march always runs at half resolution
    double volumeMs = -1.0;             // GPU time of the march + composite passes (-1 = none)
};

// Group 1 binding 1 of both passes (112 bytes). Mirrors `VolumeUniforms` in shaders/volume.wgsl.
struct VolumeUniforms {
    glm::vec4 params0;     // density, fogHeight, fogHeightFalloff, scattering
    glm::vec4 params1;     // absorption, anisotropy, emission, maxDistance
    glm::vec4 noiseParams; // noiseAmount, noiseScale, noiseSpeed, time
    glm::vec4 info;        // steps, density field slot, colour field slot, frame index
    glm::vec4 sizes;       // half width, half height, full width, full height
    glm::vec4 depthParams; // camera near, camera far, 0, 0
    glm::vec4 fogColor;    // rgb, w = 0
};
static_assert(sizeof(VolumeUniforms) == 112);

class VolumeRenderer {
public:
    VolumeRenderer(gpu::Context& context, gpu::ShaderLibrary& shaders);
    ~VolumeRenderer();
    VolumeRenderer(const VolumeRenderer&) = delete;
    VolumeRenderer& operator=(const VolumeRenderer&) = delete;

    // Creates both pipelines. `frameLayout` is SceneRenderer's group 0 layout, `colorFormat` the
    // HDR target's colour format (`depthFormat` is accepted for symmetry with the other
    // renderers; neither pass writes depth), `fieldBlock` the FieldUniforms buffer (a zeroed
    // private one is created when null).
    [[nodiscard]] Result<void> init(wgpu::TextureFormat colorFormat, wgpu::TextureFormat depthFormat,
                                    const wgpu::BindGroupLayout& frameLayout, wgpu::Buffer fieldBlock = nullptr);
    [[nodiscard]] Result<void> reload(); // hot reload of volume.wgsl (keeps the old pipelines on failure)

    // True when this scene wants volumetrics at all (volumeDensity > 0).
    [[nodiscard]] static bool enabled(const scene::Environment& environment);

    // Per frame, before encode(): sizes the half-res target, resolves the density/colour field
    // names to slots and writes the uniforms. Does nothing (and clears the stats) when off.
    void update(const scene::Scene& scene, const FrameTime& time, std::uint32_t width, std::uint32_t height,
                const wgpu::TextureView& sceneDepth, const FieldUniforms* fields = nullptr);
    // Encodes the march pass and the composite pass onto `color` (loaded and stored). Call right
    // after the lit pass. No-op when the last update() found the fog off.
    void encode(wgpu::CommandEncoder& encoder, const wgpu::TextureView& color,
                const wgpu::BindGroup& frameBindGroup);
    // Pumps the pass timer after the frame's command buffer was submitted.
    void collectTimings();

    [[nodiscard]] const VolumeStats& stats() const { return stats_; }
    [[nodiscard]] const gpu::RenderTarget& target() const; // the half-res march target (tests)

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    VolumeStats stats_;
};

} // namespace avgen::rendering
