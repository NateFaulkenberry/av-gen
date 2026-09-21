#pragma once

// Volumetric atmosphere (ADR-032): a raymarched fog pass encoded after the lit pass and before
// the post chain. `shaders/volume.wgsl` marches at half resolution into this renderer's own
// RGBA16F target (rgb = in-scattered radiance, a = transmittance), reading the scene depth so
// the fog is occluded by geometry, and a second full-resolution pass composites it into the HDR
// target with a depth-aware upsample and `src One, dst SrcAlpha` blending
// (`hdr = scatter + hdr * transmittance`).
//
// ADR-139: the march resolution is `QualitySettings::volumeResolutionScale`, not a constant. 0.5
// is the half-resolution march this pass shipped with; 1.0 makes the composite an exact copy of
// a per-pixel march, which is what the Offline tier renders (§5.9).
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
#include "rendering/render_quality.hpp"
#include "scene/scene.hpp"
#include "world/atmospherics.hpp"

#include <glm/glm.hpp>
#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <memory>

namespace avgen::gpu {
class Context;
class FrameTimeline;
class ShaderLibrary;
} // namespace avgen::gpu

namespace avgen::rendering {

class FieldUniforms;

struct VolumeStats {
    std::uint32_t steps = 0;            // raymarch samples per pixel this frame (0 = fog off)
    std::uint32_t glowSystems = 0;      // emissive particle systems lighting the fog (ADR-040)
    // ADR-578, the brief's §39: "capture GPU ms, CPU ms, memory, resolution, step count, ACTIVE
    // VOLUME COUNT". Every item on that list was reported except this one, and it is the item
    // ADR-560's headline defect was about -- a second medium in a scene rendered as nothing and
    // no record said so. The count and the shadow steps are the two numbers that decide what this
    // pass costs, and neither reached the workload line.
    std::uint32_t media = 0;            // placed media the march marched this frame
    std::uint32_t mediaDropped = 0;     // ...and the ones that did not fit (ADR-560)
    std::uint32_t shadowSteps = 0;      // ADR-570's self-shadow march, 0 = off
    bool halfResolution = true;         // true when the march runs below the scene's resolution
    // ADR-139: what the tier actually asked for and what it produced, so a reader of a record
    // can tell a scale that was applied from one that was clamped away by a small viewport.
    float resolutionScale = 0.5f;       // QualitySettings::volumeResolutionScale as applied
    std::uint32_t marchWidth = 0;       // the march target's size in texels (0 = fog off)
    std::uint32_t marchHeight = 0;
    // ADR-140: the march and the composite are two different costs and were one number. The
    // march is O(pixels * steps) and scales with `resolutionScale`; the composite is O(full-res
    // pixels) and does not. Only splitting them makes any volumetric trade decidable.
    double marchMs = -1.0;              // GPU time of the raymarch pass alone (-1 = none)
    double compositeMs = -1.0;          // GPU time of the upsample/composite pass alone
    double volumeMs = -1.0;             // GPU time of the march + composite passes (-1 = none)
};

// Group 1 binding 1 of both passes (112 bytes). Mirrors `VolumeUniforms` in shaders/volume.wgsl.
struct VolumeUniforms {
    glm::vec4 params0;     // density, fogHeight, fogHeightFalloff, scattering
    glm::vec4 params1;     // absorption, anisotropy, emission, maxDistance
    glm::vec4 noiseParams; // noiseAmount, noiseScale, noiseSpeed, time
    glm::vec4 info;        // steps, density field slot, colour field slot, frame index
    glm::vec4 sizes;       // half width, half height, full width, full height
    glm::vec4 depthParams; // camera near, camera far, march start jitter (ADR-461), 0
    glm::vec4 fogColor;    // rgb, w = 0
    glm::vec4 glow;        // x = particle glow systems (ADR-040), y = local-light strength, zw = 0
    // ADR-568 (§7): the height layer's SHAPE, in a lane of its own rather than in the zeroes of a
    // lane that means something else. ADR-562 §9's finding is what that costs when it goes wrong,
    // and `VolumeUniforms` has no lane budget to defend -- it is not a packed per-kind block, it
    // is a uniform with a sizeof assertion, so a named lane is free and a reused one is not.
    // x = fogUpperDensity, y = fogHeightCurve, zw = 0.
    glm::vec4 heightFog;
    // ADR-570 (§20/§22): the shared self-shadow march. x = steps along the ray toward each light
    // (0 = off and the shader returns 1.0 from its first branch, so every existing frame is
    // bit-identical), y = strength, zw = 0.
    glm::vec4 selfShadow;
    // ADR-562: the placed media, as lanes. Was twelve named `vortexN` members carrying exactly one
    // medium; a slot is `world::kMediumLanes` `vec4` and there are `world::kMaxMedia` of them, so a
    // second medium is a slot rather than a rewrite. `mediaInfo.x` is how many are live and the
    // gate. Each slot's KIND is in its own last lane (`lane[15].x`), not here -- `mediaInfo` is one
    // `vec4` for the whole uniform and could only ever have held one kind, which is a thing this
    // comment used to claim it did.
    glm::vec4 mediaInfo;
    glm::vec4 media[world::kMaxMedia * world::kMediumLanes];
};
static_assert(sizeof(VolumeUniforms) == 16 * (10 + 1 + world::kMaxMedia * world::kMediumLanes));

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
    // `particleGlow` is ParticleRenderer::glowBuffer(): the emissive aggregates emissive particle
    // systems reduce to, which the march adds as an in-scattering source (ADR-040). A zeroed
    // private buffer is created when null, so the fog renders exactly as before without it.
    [[nodiscard]] Result<void> init(wgpu::TextureFormat colorFormat, wgpu::TextureFormat depthFormat,
                                    const wgpu::BindGroupLayout& frameLayout, wgpu::Buffer fieldBlock = nullptr,
                                    wgpu::Buffer particleGlow = nullptr);
    [[nodiscard]] Result<void> reload(); // hot reload of volume.wgsl (keeps the old pipelines on failure)

    // True when this scene wants volumetrics at all (volumeDensity > 0).
    [[nodiscard]] static bool enabled(const scene::Environment& environment);
    // ADR-387: the vortex lives in the atmospheric effects now, so the whole-scene overload is
    // the one that answers correctly for a scene with a vortex and no fog.
    [[nodiscard]] static bool enabled(const scene::Scene& scene);

    // Per frame, before encode(): sizes the half-res target, resolves the density/colour field
    // names to slots and writes the uniforms. Does nothing (and clears the stats) when off.
    void update(const scene::Scene& scene, const FrameTime& time, std::uint32_t width, std::uint32_t height,
                const wgpu::TextureView& sceneDepth, const FieldUniforms* fields = nullptr,
                std::uint32_t particleGlowSystems = 0,
                const QualitySettings& quality = QualitySettings{});
    // Encodes the march pass and the composite pass onto `color` (loaded and stored). Call right
    // after the lit pass. No-op when the last update() found the fog off.
    void encode(wgpu::CommandEncoder& encoder, const wgpu::TextureView& color,
                const wgpu::BindGroup& frameBindGroup);
    // The shared frame timeline (gpu/frame_timeline.hpp) this renderer's passes mark themselves
    // on. Null leaves them untimed. SceneRenderer sets it once; a standalone user may not.
    void setTimeline(gpu::FrameTimeline* timeline);
    // Reads this frame's march + composite time back off the timeline, after it was collected.
    void collectTimings();

    [[nodiscard]] const VolumeStats& stats() const { return stats_; }
    [[nodiscard]] const gpu::RenderTarget& target() const; // the half-res march target (tests)

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    VolumeStats stats_;
};

} // namespace avgen::rendering
