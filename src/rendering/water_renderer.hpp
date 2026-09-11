#pragma once

// Water surfaces (ADR-099). A pipeline inside the scene pass, not a pass of its own: water is
// geometry, it is depth-tested against the world it sits in, and it wants to be composited over
// the bank behind it while the bank's depth is still bound.
//
// It has its own pipeline rather than a branch in `pbr_shade.wgsl` for two reasons. The first is
// what the surface needs -- the scene's linear depth to know how thick it is, the environment cube
// to reflect, a normal from travelling ripples rather than one from a vertex -- none of which the
// shared metallic-roughness path carries. The second is cost: the scene pass is the frame's
// dominant term and is fragment-bound, and a branch in the shader every entity in the world runs
// is a bad place to put a feature one of them uses.
//
// Bind groups are the scene's own, apart from group 2: group 0 is the frame group, group 1 is the
// object uniforms (the terrain node's transform, by dynamic offset), group 3 is the IBL group.
// Group 2 is this renderer's WaterUniforms -- one 192-byte record per water material, addressed by
// dynamic offset, so a scene with two rivers of different colour is two writes and no extra
// pipeline.
//
// The surface is blended, which has a consequence worth stating: blended geometry is not in the
// depth prepass, so the linear depth this shader reads is the *bed*, which is exactly the quantity
// the shoreline and the depth colour are made of. Water being opaque was the reason there was
// nothing to read.

#include "core/error.hpp"
#include "scene/scene_types.hpp"
#include "scene/water_surface.hpp"

#include <glm/glm.hpp>
#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <vector>

namespace avgen::gpu {
class Context;
class ShaderLibrary;
} // namespace avgen::gpu



namespace avgen::rendering {

// Group 2 binding 0. Mirrors `WaterUniforms` in shaders/water.wgsl.
struct WaterUniforms {
    glm::vec4 shallowColor{0.0f}; // rgb, w = metres of depth over which the colour reaches deep
    glm::vec4 deepColor{0.0f};    // rgb, w = clarity
    glm::vec4 foamColor{0.0f};    // rgb, w = foam amount
    glm::vec4 glowColor{0.0f};    // rgb, w = glow amount
    glm::vec4 sparkleColor{0.0f}; // rgb, w = sparkle amount
    glm::vec4 reflectTint{0.0f};  // rgb, w = reflection multiplier
    glm::vec4 emissive{0.0f};     // rgb * intensity
    glm::vec4 surface{0.0f};      // fresnel, specular, roughness, maxOpacity
    glm::vec4 ripples{0.0f};      // amplitude, scale, speed, chop
    glm::vec4 shore{0.0f};        // foamWidth, edgeFade, refraction, 0
    glm::vec4 life{0.0f};         // glowScale, glowCoverage, glowDepth, swell
    glm::vec4 params{0.0f};       // flow time, fastest body speed, linear depth valid, 0
};
static_assert(sizeof(WaterUniforms) == 192);

// How many distinct water materials one frame may carry. Uniform slots are cheap; the number is a
// bound on the dynamic-offset buffer and nothing else.
inline constexpr std::uint32_t kMaxWaterMaterials = 8;

// Fills a WaterUniforms from the authored settings. `flowTime` is the timeline second the surface
// is being drawn at -- never a wall clock and never an accumulated delta, because an offline render
// must land on the same water as the live one. `fastest` is the speed of the quickest body in the
// world, which is what the vertex's speed lane is a fraction of.
[[nodiscard]] WaterUniforms waterUniformsFrom(const scene::WaterSettings& settings, float flowTime,
                                              float fastest, bool linearDepthValid);

class WaterRenderer {
public:
    WaterRenderer() = default;
    WaterRenderer(const WaterRenderer&) = delete;
    WaterRenderer& operator=(const WaterRenderer&) = delete;

    // `frameLayout`, `objectLayout` and `iblLayout` are the scene renderer's own.
    [[nodiscard]] Result<void> init(gpu::Context& context, gpu::ShaderLibrary& shaders,
                                    wgpu::TextureFormat hdrFormat, wgpu::TextureFormat depthFormat,
                                    const wgpu::BindGroupLayout& frameLayout,
                                    const wgpu::BindGroupLayout& objectLayout,
                                    const wgpu::BindGroupLayout& iblLayout);
    // Rebuilds the pipeline from a reloaded shader module (hot reload); keeps the layouts.
    [[nodiscard]] Result<void> reload(gpu::ShaderLibrary& shaders);

    // Uploads this frame's water materials. Slot `i` is addressed by `offset(i)`.
    void upload(const wgpu::Queue& queue, const std::vector<WaterUniforms>& materials);
    [[nodiscard]] std::uint32_t offset(std::size_t slot) const {
        return static_cast<std::uint32_t>(slot) * kStride;
    }
    [[nodiscard]] const wgpu::BindGroup& bindGroup() const { return bindGroup_; }
    [[nodiscard]] const wgpu::RenderPipeline& pipeline() const { return pipeline_; }
    [[nodiscard]] bool ready() const { return static_cast<bool>(pipeline_); }
    [[nodiscard]] std::uint32_t materialCount() const { return uploaded_; }

private:
    // Dynamic uniform offsets must be a multiple of the device's minimum alignment, which is 256
    // on every backend this runs on. 192 bytes of record, padded.
    static constexpr std::uint32_t kStride = 256;

    [[nodiscard]] Result<void> createPipeline(gpu::ShaderLibrary& shaders);

    gpu::Context* context_ = nullptr;
    wgpu::TextureFormat hdrFormat_ = wgpu::TextureFormat::RGBA16Float;
    wgpu::TextureFormat depthFormat_ = wgpu::TextureFormat::Depth32Float;
    wgpu::BindGroupLayout waterLayout_;
    wgpu::PipelineLayout pipelineLayout_;
    wgpu::RenderPipeline pipeline_;
    wgpu::Buffer uniforms_;
    wgpu::BindGroup bindGroup_;
    std::uint32_t uploaded_ = 0;
    std::vector<std::uint8_t> staging_;
};

} // namespace avgen::rendering
