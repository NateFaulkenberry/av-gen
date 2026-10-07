#pragma once

// ADR-1200: the ENVIRONMENT seam -- how a specialised renderer joins the frame (docs/prototypes/bioluminescent/
// 03-architecture.md §2, "The seam contract").
//
// An Environment is a scene-level block (its own JSON key, its own struct on scene::Scene, its own parameters
// registered by the Composition) drawn by an EnvironmentRenderer that SceneRenderer owns. Every frame, after the lit
// pass has written HDR, the aux targets and depth -- and before the volumetric medium, post and tonemap -- SceneRenderer
// calls `encode` on each environment that `wants` the scene, in registration order. An environment may:
//   * run any compute it likes (its own buffers, its own simulation),
//   * read the frame's uniforms, the field block, the simulated-grid table and the prepass's linear depth,
//   * write HDR and the emission target (additively or not), and test against -- or write -- the depth buffer.
// Everything downstream (medium, bloom, temporal, tonemap, PNG/EXR/video, AOVs, the live quality ladder) then treats
// its pixels like any other. With no environment wanting the scene, nothing is recorded: the frame is byte-identical.
//
// Deliberately small: no registry, no plugin loading, no scene graph. A new environment is a new block, a new class
// deriving from this, and one line in SceneRenderer's constructor.

#include "core/error.hpp"
#include "core/time.hpp"

#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <string_view>

namespace avgen::gpu {
class FrameTimeline;
class ShaderLibrary;
} // namespace avgen::gpu

namespace avgen::scene {
struct Scene;
}

namespace avgen::rendering {

class FieldUniforms;
struct QualitySettings;

// What an environment gets from the frame. All views are this frame's; all sizes are the HDR target's (render scale
// applied). `timeline` may be null (no timestamps): pass it to FrameTimeline::mark for each pass you encode.
struct EnvironmentFrame {
    wgpu::CommandEncoder* encoder = nullptr;
    const scene::Scene* scene = nullptr;
    FrameTime time{};
    const wgpu::Buffer* frameUniforms = nullptr; // the scene renderer's FrameUniforms (shaders/common.wgsl `frame`)
    const FieldUniforms* fields = nullptr;       // the field block and the simulated-grid table (fields.wgsl)
    wgpu::TextureView linearDepth;               // R32Float view-space distance from the depth prepass (1e7 = sky)
    wgpu::TextureView hdr;                       // RGBA16Float scene radiance
    wgpu::TextureView emission;                  // RGBA16Float emitted radiance; alpha = bloom weight
    wgpu::TextureView depth;                     // Depth24Plus (load it; store it if you write it)
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    const QualitySettings* quality = nullptr;    // the live ladder's levers (render scale, tier...)
    gpu::FrameTimeline* timeline = nullptr;
};

class EnvironmentRenderer {
public:
    virtual ~EnvironmentRenderer() = default;
    // Short, unique: used in pass labels and timings ("ecosystem.emit" ...).
    [[nodiscard]] virtual std::string_view name() const = 0;
    // True when `scene` holds this environment's block, active. Cheap: called twice a frame. A true here also makes
    // SceneRenderer run its depth prepass (the linear depth is part of the contract).
    [[nodiscard]] virtual bool wants(const scene::Scene& scene) const = 0;
    // Records this frame's work into `frame.encoder`.
    virtual void encode(const EnvironmentFrame& frame) = 0;
    // Rebuilds pipelines from reloaded shaders; keeps the old ones on failure.
    [[nodiscard]] virtual Result<void> reload(gpu::ShaderLibrary& shaders) = 0;
};

} // namespace avgen::rendering
