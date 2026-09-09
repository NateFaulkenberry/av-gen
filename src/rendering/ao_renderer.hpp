#pragma once

// Ground-truth ambient occlusion (ADR-034). `shaders/gtao.wgsl` runs horizon-based occlusion over
// the linear depth target at half resolution, producing a visibility term and a bent normal, then
// a temporal pass reprojects the previous frame's result with the camera motion and clamps it to
// the neighbourhood of the new one. The per-frame sample rotation is a deterministic function of
// the frame index (never a wall clock), so two renders of the same frame are identical.
//
// The result is bound to the shading pass at group 0 binding 6 and upsampled there with a
// depth-aware (bilateral) four-tap filter, which saves a full-resolution target.
//
// Bind groups: group 0 is SceneRenderer's frame group with the AO texture replaced by a
// placeholder (a pass cannot read what it writes); group 1 is this renderer's own -
// 0 = AoUniforms, 1 = linear depth, 2 = the previous frame's result, 3 = this frame's raw AO.

#include "core/error.hpp"
#include "rendering/render_quality.hpp"

#include <glm/glm.hpp>
#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <memory>

namespace avgen::gpu {
class Context;
class ShaderLibrary;
} // namespace avgen::gpu

namespace avgen::rendering {

struct AoStats {
    std::uint32_t width = 0;   // resolution of the AO target (0 = off this frame)
    std::uint32_t height = 0;
    std::uint32_t slices = 0;
    std::uint32_t steps = 0;
    double aoMs = -1.0;        // GPU time of the AO + temporal passes (-1 = unavailable)
};

// Group 1 binding 0 of both AO passes (80 bytes). Mirrors `AoUniforms` in shaders/gtao.wgsl.
struct AoUniforms {
    glm::vec4 sizes;      // AO width, height, 1 / width, 1 / height
    glm::vec4 fullSize;   // scene width, height, 1 / width, 1 / height
    glm::vec4 params;     // world radius, strength, slice count, steps per slice
    glm::vec4 temporal;   // frame index, history blend, 1 when a history exists, thickness
    glm::vec4 projection; // tan(fovY/2) * aspect, tan(fovY/2), near, far
};
static_assert(sizeof(AoUniforms) == 80);

class AoRenderer {
public:
    AoRenderer(gpu::Context& context, gpu::ShaderLibrary& shaders);
    ~AoRenderer();
    AoRenderer(const AoRenderer&) = delete;
    AoRenderer& operator=(const AoRenderer&) = delete;

    [[nodiscard]] Result<void> init(const wgpu::BindGroupLayout& frameLayout);
    [[nodiscard]] Result<void> reload(); // hot reload of gtao.wgsl (keeps the old pipelines on failure)

    // Sizes the targets and writes the uniforms. `linearDepth` is the R32Float scene depth.
    // Does nothing (and reports zero) when the tier has AO off or the size is degenerate.
    void update(std::uint32_t width, std::uint32_t height, const wgpu::TextureView& linearDepth,
                const QualitySettings& quality, std::uint64_t frameIndex, float fovYRadians, float aspect,
                float nearPlane, float farPlane, float worldRadius, float strength);
    // Encodes the occlusion pass and the temporal pass. `frameBindGroup` must be the variant whose
    // AO binding is a placeholder.
    void encode(wgpu::CommandEncoder& encoder, const wgpu::BindGroup& frameBindGroup);
    void collectTimings();
    // Invalidates the temporal history (a scene change, a camera cut, a fresh renderer).
    void resetHistory();

    // The AO result the shading pass samples: rgb = bent normal (world space), a = visibility.
    // Falls back to a white 1x1 texture when AO is off, so the binding is always valid.
    [[nodiscard]] const wgpu::TextureView& output() const;
    [[nodiscard]] const wgpu::TextureView& placeholder() const; // 1x1 white
    [[nodiscard]] bool active() const;
    [[nodiscard]] const AoStats& stats() const { return stats_; }

    static constexpr wgpu::TextureFormat kFormat = wgpu::TextureFormat::RGBA16Float;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    AoStats stats_;
};

} // namespace avgen::rendering
