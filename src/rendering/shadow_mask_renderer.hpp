#pragma once

// The half-resolution screen-space shadow mask (ADR-087). `shaders/shadow_mask.wgsl` runs one
// fullscreen pass over the linear depth target the prepass resolved and writes, per leading
// directional light, exactly the combined visibility the lit pass would otherwise compute per
// pixel: the cascaded PCSS lookup minned with the twelve-step screen-space contact march. The
// shading pass reads it back at group 0 binding 11 and upsamples it with a depth-aware four-tap
// filter, so it pays four texel loads instead of a cascade lookup and a march.
//
// This is the AO pass's shape, for the AO pass's reason: the term is low-frequency and the pass is
// fragment-bound. Unlike AO it has no temporal history, because it has no noise to average -- the
// PCF rotation is a deterministic function of the pixel -- and a history would put a frame of lag
// into the one term a moving shadow is made of.
//
// Bind groups: group 0 is SceneRenderer's frame group with the mask binding replaced by a
// placeholder (a pass cannot read what it writes) and the real shadow atlas and linear depth bound;
// group 1 is this renderer's own, holding nothing but ShadowMaskUniforms.

#include "core/error.hpp"
#include "rendering/render_quality.hpp"

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

struct ShadowMaskStats {
    std::uint32_t width = 0;  // resolution of the mask target (0 = not built this frame)
    std::uint32_t height = 0;
    std::uint32_t lights = 0; // leading directional lights the mask covers (0..3)
    std::uint32_t taps = 0;   // PCF/PCSS taps the mask pass used
    double maskMs = -1.0;     // GPU time of the pass (-1 = unavailable)
};

// Group 1 binding 0 (64 bytes). Mirrors `ShadowMaskUniforms` in shaders/shadow_mask.wgsl.
struct ShadowMaskUniforms {
    glm::vec4 sizes;      // mask width, height, 1 / width, 1 / height
    glm::vec4 fullSize;   // scene width, height, 1 / width, 1 / height
    glm::vec4 projection; // tan(fovY/2) * aspect, tan(fovY/2), near, far
    glm::vec4 params;     // x = directional lights covered, y = PCF taps, zw = 0
};
static_assert(sizeof(ShadowMaskUniforms) == 64);

// At most three directional lights are masked. Three is what fits in an RGBA16Float alongside the
// view depth the bilateral filter rejects on, and a rig with a fourth directional light is already
// past the point where one more full-resolution term is the problem: it falls back to the
// unmasked path and is shaded exactly as before.
inline constexpr std::uint32_t kMaxMaskedDirectionalLights = 3;

class ShadowMaskRenderer {
public:
    ShadowMaskRenderer(gpu::Context& context, gpu::ShaderLibrary& shaders);
    ~ShadowMaskRenderer();
    ShadowMaskRenderer(const ShadowMaskRenderer&) = delete;
    ShadowMaskRenderer& operator=(const ShadowMaskRenderer&) = delete;

    [[nodiscard]] Result<void> init(const wgpu::BindGroupLayout& frameLayout);
    [[nodiscard]] Result<void> reload(); // hot reload of shadow_mask.wgsl (keeps the old pipeline)

    // ADR-255: **export the term as an AOV, without letting the export change the picture.**
    //
    // `--aov shadow` asks for the shadow-map visibility at full resolution. The tiers the Quality
    // Lab renders at -- `high` and `offline` -- set `shadowMaskScale = 1.0`, which is how a tier
    // says "do not build a mask", so at exactly those tiers this pass does not run and an AOV taken
    // from `output()` would be a 1x1 white texel widened into a plausible-looking constant. That is
    // the finding ADR-255 exists for.
    //
    // So an export request runs the pass at scale 1.0 whatever the tier says, and the result is
    // **not consumed by the lit pass**: `active()` stays false, `frame.shadowMaskParams.x` stays 0,
    // and the shaded frame is byte-for-byte the frame the same command would have produced without
    // `--aov shadow`. An AOV that changed the deliverable would not be an AOV *of* it.
    //
    // The cost is that the exported term is **recomputed** rather than captured, which ADR-255 says
    // in as many words must be measured rather than assumed -- see `--quality-arm maskconsume`.
    void setExportRequested(bool requested);
    [[nodiscard]] bool exportRequested() const;

    // Sizes the target and writes the uniforms. Reports nothing and encodes nothing when the tier
    // asks for full resolution (`shadowMaskScale >= 1`) and nobody asked for an export, when the
    // caller switched it off, or when the scene has no directional light: in every one of those the
    // lit pass computes the term itself, exactly as it did before this pass existed.
    void update(std::uint32_t width, std::uint32_t height, const QualitySettings& quality, bool enabled,
                std::uint32_t directionalLights, float fovYRadians, float aspect, float nearPlane,
                float farPlane);
    // Encodes the mask pass. `frameBindGroup` must be the variant whose mask binding is a
    // placeholder and whose shadow atlas and linear depth are the real ones.
    void encode(wgpu::CommandEncoder& encoder, const wgpu::BindGroup& frameBindGroup);
    void setTimeline(gpu::FrameTimeline* timeline);
    void collectTimings();

    // The mask the shading pass samples. Falls back to a 1x1 white texel when the mask is off, so
    // the binding is always valid; `active()` is what decides whether the shader reads it.
    //
    // **`active()` is deliberately false for an export-only pass**, so the shading path binds the
    // same white texel it binds today at `high` and `offline` and nothing about the lit frame
    // moves. The exported texture is reached through `exportTexture()` instead, which is a
    // different question with a different answer.
    [[nodiscard]] const wgpu::TextureView& output() const;
    [[nodiscard]] const wgpu::TextureView& placeholder() const; // 1x1 white
    [[nodiscard]] bool active() const;
    // True when a pass was encoded this frame, for whatever reason -- which is what decides
    // whether the depth prepass has a consumer, and whether there is anything to read back.
    [[nodiscard]] bool encoded() const;
    // The target `--aov shadow` reads. rgb = the shadow-map visibility of directional lights 0, 1
    // and 2; a = the view depth it was computed at. Only valid while `encoded()`.
    [[nodiscard]] const wgpu::Texture& exportTexture() const;
    [[nodiscard]] const ShadowMaskStats& stats() const { return stats_; }

    static constexpr wgpu::TextureFormat kFormat = wgpu::TextureFormat::RGBA16Float;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    ShadowMaskStats stats_;
};

} // namespace avgen::rendering
