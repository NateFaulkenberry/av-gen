#pragma once

// DF: the distortion framework's passes (Effect Library Wave 1, roadmap 1.7; the model is in
// world/effects/distortion_frame.hpp and the shader in shaders/distortion.wgsl).
//
// Three steps, encoded by `SceneRenderer::render` AFTER the volumetric composite and the debug pass
// and BEFORE post layers, temporal capture and the post chain -- so fog and media behind a warp bend,
// post (bloom, DoF, grading) sees the bent image, and the temporal ring captures it:
//
//   1. OFFSET. Every proxy of the frame, instanced from one storage buffer (capacity
//      `world::kMaxDistortionProxies`), into two transient RGBA16F targets and one R16F COVER target
//      (Wave 3: the opacity of a black hole's horizon; zero for every other field) with additive
//      blending.
//      No depth attachment: each fragment reads the scene depth as a texture (read-only) and applies
//      the lens-plane rule itself, which is exact and holds with the camera inside a proxy too.
//   2. COPY. The HDR image into a transient scene copy -- only the region the resolve can sample,
//      which is the producers' screen rect grown by their largest displacement.
//   3. RESOLVE. A full-screen triangle scissored to the producers' screen rect, writing HDR (the bent
//      image) and adding the rim to the emission target so selective bloom sees it.
//
// **The gate.** A frame whose `count` is 0 encodes nothing, acquires nothing and writes nothing: the
// frame is byte-identical to one from a renderer without DF (`test_distortion_gpu.cpp` proves it).
//
// **Sky lensing, honestly.** A tap that lands on the far plane reads the sky the copy already holds
// at that screen position -- the sky is at infinity, so the bent ray's radiance IS that pixel. A tap
// that would leave the screen (or the copied region) is CLAMPED to its edge rather than read from
// the environment cube: the visible sky here is not always the IBL (Glowmere's is a background
// shader layer), and a cube sample would put a second, different sky at the frame's edge.
// Gravitational Lens (Wave 3) keeps this rule rather than adding an environment tap: its remap is
// bounded (every tap stays within 2 theta_E of its pixel, inside the lens's own disc), so a lens wholly
// on screen never needs off-screen radiance, and a sky tap on screen already reads exactly the bent
// ray's radiance whatever the sky is. Only a lens crossing the frame's edge is clamped there.

#include "core/error.hpp"
#include "world/effects/distortion_frame.hpp"

#include <glm/glm.hpp>
#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <memory>

namespace avgen::gpu {
class Context;
class FrameTimeline;
class ShaderLibrary;
class TransientPool;
} // namespace avgen::gpu

namespace avgen::rendering {

// What one frame's DF work was. `encoded` false means the gate held and nothing was touched.
struct DistortionStats {
    bool encoded = false;
    std::uint32_t proxies = 0;   // proxies drawn into the offset targets
    std::uint32_t dropped = 0;   // proxies the frame block could not hold (world::DistortionFrame)
    std::uint32_t scissorPixels = 0; // pixels the resolve covers
    std::uint32_t copyPixels = 0;    // pixels the scene copy moves
    bool fullScreen = false;     // a proxy reached the near plane, so the rects are the whole frame
    double offsetMs = -1.0;      // GPU time of the offset pass (-1 unavailable)
    double resolveMs = -1.0;     // GPU time of the copy and the resolve together
};

// Everything about THIS frame the passes need, handed in by the scene renderer.
struct DistortionTargets {
    glm::mat4 viewProj{1.0f};
    float nearPlane = 0.1f;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    wgpu::Texture hdr;          // the scene HDR target: copied from, and resolved into
    wgpu::TextureView hdrView;
    wgpu::TextureView depthView; // the scene depth, read as a texture (never attached)
    wgpu::TextureView emissionView;
    wgpu::BindGroup frameBindGroup; // group 0, the scene's frame block
};

// The screen rect a proxy can touch, in pixels, and the (larger) rect its taps can read. Exposed so a
// CPU test can hold the conservative bound against the analytic one without a device.
struct DistortionRects {
    glm::ivec4 scissor{0}; // x0, y0, x1, y1 (exclusive), clamped to the target
    glm::ivec4 copy{0};
    bool fullScreen = false;
};
[[nodiscard]] DistortionRects distortionRects(const world::DistortionFrame& frame, const glm::mat4& viewProj,
                                              float nearPlane, std::uint32_t width, std::uint32_t height,
                                              float meshScale);

class DistortionRenderer {
public:
    static constexpr wgpu::TextureFormat kOffsetFormat = wgpu::TextureFormat::RGBA16Float;
    static constexpr wgpu::TextureFormat kCoverFormat = wgpu::TextureFormat::R16Float;
    // The largest screen offset one pixel may take, in UV. A safety bound, not a look: the authored
    // fields stay far inside it, and it keeps a runaway route from sampling across the frame.
    static constexpr float kMaxOffsetUv = 0.2f;

    DistortionRenderer(gpu::Context& context, gpu::ShaderLibrary& shaders);
    ~DistortionRenderer();
    DistortionRenderer(const DistortionRenderer&) = delete;
    DistortionRenderer& operator=(const DistortionRenderer&) = delete;

    [[nodiscard]] Result<void> init(const wgpu::BindGroupLayout& frameLayout, wgpu::TextureFormat hdrFormat,
                                    wgpu::TextureFormat emissionFormat);
    [[nodiscard]] Result<void> reload(); // hot reload of distortion.wgsl (keeps the old pipelines on failure)
    void setTimeline(gpu::FrameTimeline* timeline);
    void collectTimings();

    // Encodes the three steps for `frame`. Returns false, having touched nothing, when the frame has
    // no proxy (the gate) or the renderer is not initialised.
    bool encode(wgpu::CommandEncoder& encoder, const world::DistortionFrame& frame,
                const DistortionTargets& targets, gpu::TransientPool& pool);

    [[nodiscard]] const DistortionStats& stats() const { return stats_; }
    [[nodiscard]] float meshScale() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    DistortionStats stats_;
};

} // namespace avgen::rendering
