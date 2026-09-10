#pragma once

// Cascaded shadow maps and spot shadow maps (ADR-034).
//
// Directional lights that cast get `QualitySettings::cascadeCount` cascades fitted to the visible
// depth range and stabilised by snapping the light-space origin to texel increments, so the shadow
// edges do not crawl as the camera moves. Spot lights that cast get one perspective map. Point
// lights get no map in this implementation and rely on contact shadows and occlusion.
//
// This class owns the maths, the atlas (a `texture_depth_2d_array`, one layer per view), the
// per-view uniform buffers (a copy of the frame block with `viewProj` replaced by the light's, so
// the depth-only passes reuse the existing vertex shaders unchanged) and the `ShadowUniforms`
// block the shading pass reads. The *encoding* of the depth-only draws belongs to SceneRenderer,
// which is the only object that can reach entities, procedural instances and SDFs at once.
//
// The cascade maths lives in rendering/shadow_math.hpp, free of any GPU type, so the tests check
// it against a CPU reference without a device.

#include "core/error.hpp"
#include "rendering/render_quality.hpp"
#include "rendering/shadow_math.hpp"
#include "scene/scene.hpp"

#include <glm/glm.hpp>
#include <webgpu/webgpu_cpp.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace avgen::gpu {
class Context;
} // namespace avgen::gpu

namespace avgen::rendering {

struct ShadowStats {
    std::uint32_t views = 0;      // depth-only passes encoded this frame
    std::uint32_t cascades = 0;   // of those, directional cascades
    std::uint32_t spots = 0;
    std::uint32_t resolution = 0; // one square map
    double shadowMs = -1.0;       // GPU time of the depth passes (-1 = unavailable)
    std::uint32_t entityDraws = 0; // entity draws recorded across every cascade this frame
    std::uint32_t entitiesCulled = 0; // casters a cascade's own frustum rejected (ADR-055)
};

class ShadowRenderer {
public:
    ShadowRenderer(gpu::Context& context);
    ~ShadowRenderer();
    ShadowRenderer(const ShadowRenderer&) = delete;
    ShadowRenderer& operator=(const ShadowRenderer&) = delete;

    // Allocates the atlas and the per-view uniform buffers. `frameUniformSize` is
    // `sizeof(FrameUniforms)`; the shadow views hold a copy of it.
    [[nodiscard]] Result<void> init(std::uint64_t frameUniformSize);

    // Chooses this frame's views and computes their matrices. `lights` are the scene lights in the
    // order they were packed (so `ShadowView::lightIndex` indexes the GPU light buffer);
    // `sceneRadius` sizes the caster range behind the visible frustum. Returns the number of views.
    std::uint32_t update(const std::vector<const scene::PunctualLight*>& lights, const glm::mat4& viewProj,
                         float cameraNear, float cameraFar, float sceneRadius, const QualitySettings& quality);
    // Writes each view's frame-uniform copy (the caller's block with `viewProj` replaced) and the
    // ShadowUniforms block. Call after update() and after the frame block is final.
    void upload(const void* frameUniforms, std::uint64_t frameUniformSize);

    [[nodiscard]] const std::vector<ShadowView>& views() const { return views_; }
    // The shadow view index a packed light should carry, or -1. Cascaded lights name their first.
    [[nodiscard]] int viewForLight(std::uint32_t lightIndex, bool& cascaded) const;
    [[nodiscard]] const wgpu::Buffer& viewUniforms(std::uint32_t view) const;
    [[nodiscard]] const wgpu::TextureView& layerView(std::uint32_t view) const;
    [[nodiscard]] const wgpu::TextureView& atlasView() const;     // the whole array, for shading
    [[nodiscard]] const wgpu::TextureView& dummyAtlasView() const; // 1x1 stand-in for the depth passes
    [[nodiscard]] const wgpu::Buffer& uniforms() const;
    [[nodiscard]] const wgpu::Sampler& comparisonSampler() const;
    [[nodiscard]] std::uint32_t resolution() const;
    [[nodiscard]] const ShadowStats& stats() const { return stats_; }
    [[nodiscard]] bool ready() const;

    // The same format as the scene depth buffer, so the depth-only pipelines of every renderer
    // (entities, procedural instances, raymarched SDFs) serve both the prepass and the shadow maps.
    static constexpr wgpu::TextureFormat kFormat = wgpu::TextureFormat::Depth24Plus;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::vector<ShadowView> views_;
    ShadowStats stats_;
};

} // namespace avgen::rendering
