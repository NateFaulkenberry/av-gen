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
// Group 2 is this renderer's WaterUniforms -- one 240-byte record per water material, addressed by
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

#include <array>
#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
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
    glm::vec4 shore{0.0f};        // foamWidth, edgeFade, refraction, cascade (ADR-985)
    glm::vec4 life{0.0f};         // glowScale, glowCoverage, glowDepth, swell
    glm::vec4 params{0.0f};       // flow time, fastest body speed, linear depth valid, 0
    // ADR-916, appended so no offset above moved.
    glm::vec4 tears{0.0f};        // amount, shear (m), coverage, lattice cell (m)
    glm::vec4 tearShape{0.0f};    // spacing (m), stretch, drift (m/s), wind coupling
    glm::vec4 tearFrame{0.0f};    // seam direction x, z (in XZ), 1 when it follows the wind, 0
    // ADR-1286, appended so no offset above moved. Read only by the cinematic pipeline.
    glm::vec4 cineA{0.0f};        // ssr, ssrDistance (m), transmission, roughnessVariation
    glm::vec4 cineB{0.0f};        // undulation, undulationScale, ssrSteps, ssrGain
    glm::vec4 cineAbsorb{0.0f};   // absorption rgb per metre, 0
    glm::vec4 cineC{0.0f};        // streak, reflection rays, tint patches, glints
    glm::vec4 cineTint2{0.0f};    // second reflection tint rgb, patch scale
};
static_assert(sizeof(WaterUniforms) == 320);
// The part of the record a shader without the cinematic block reads (its binding's minimum size).
inline constexpr std::uint64_t kWaterUniformsPlainSize = 240;

// How many distinct water materials one frame may carry. Uniform slots are cheap; the number is a
// bound on the dynamic-offset buffer and nothing else.
inline constexpr std::uint32_t kMaxWaterMaterials = 8;

// Fills a WaterUniforms from the authored settings. `flowTime` is the timeline second the surface
// is being drawn at -- never a wall clock and never an accumulated delta, because an offline render
// must land on the same water as the live one. `fastest` is the speed of the quickest body in the
// world, which is what the vertex's speed lane is a fraction of.
[[nodiscard]] WaterUniforms waterUniformsFrom(const scene::WaterSettings& settings, float flowTime,
                                              float fastest, bool linearDepthValid);

// ADR-916. `water.wgsl` with its tears compiled out: every line from a `---- tears (ADR-916) begin ----`
// marker through the next `end` marker removed. A surface with no tears is drawn with a pipeline built
// from this, so it runs exactly the shader it ran before the tears existed. Source with no markers comes
// back unchanged; a block that never closes is an error.
[[nodiscard]] Result<std::string> waterSourceWithoutTears(const std::string& source);
// ADR-1286: the same for any marked block, `---- <tag> begin ----` through `---- <tag> end ----`.
[[nodiscard]] Result<std::string> waterSourceWithout(const std::string& source, std::string_view tag);

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
    // ADR-1286. The bind group slot `slot` is drawn with: the cinematic one (uniforms plus the copy of the
    // lit opaque frame) for a cinematic material, the plain one otherwise.
    [[nodiscard]] const wgpu::BindGroup& bindGroup(std::size_t slot) const {
        return slot < cine_.size() && cine_[slot] && cineBindGroup_ ? cineBindGroup_ : bindGroup_;
    }
    // Whether any material uploaded this frame is cinematic, i.e. whether the frame copy is needed.
    [[nodiscard]] bool anyCinematic() const {
        for (std::uint32_t i = 0; i < uploaded_; ++i) {
            if (cine_[i]) return true;
        }
        return false;
    }
    // The copy of the lit opaque frame the cinematic surfaces read. Rebuilds the cinematic bind group;
    // called whenever the copy is (re)allocated.
    void setSceneColor(const wgpu::TextureView& view);
    // The pipeline slot `slot`'s material is drawn with (ADR-916): the one with the tear code when its
    // `tears` is above 0, and otherwise the one with it compiled out -- so a surface without tears is
    // byte-identical to the shader before them, which one shader with the code gated could not promise
    // (the compiler builds the shared ripple path differently once the tear's data flows through it).
    [[nodiscard]] const wgpu::RenderPipeline& pipeline(std::size_t slot) const {
        if (slot < cine_.size() && cine_[slot] && cineBindGroup_) {
            return cinePipeline_;
        }
        return slot < torn_.size() && torn_[slot] ? pipeline_ : plainPipeline_;
    }
    [[nodiscard]] bool ready() const {
        return static_cast<bool>(pipeline_) && static_cast<bool>(plainPipeline_) && static_cast<bool>(cinePipeline_);
    }
    [[nodiscard]] std::uint32_t materialCount() const { return uploaded_; }

private:
    // Dynamic uniform offsets must be a multiple of the device's minimum alignment, which is 256
    // on every backend this runs on. 320 bytes of record (ADR-1286), padded.
    static constexpr std::uint32_t kStride = 512;
    static_assert(kStride % 256 == 0);
    static_assert(sizeof(WaterUniforms) <= kStride);

    [[nodiscard]] Result<void> createPipeline(gpu::ShaderLibrary& shaders);
    [[nodiscard]] Result<wgpu::RenderPipeline> buildPipeline(const wgpu::ShaderModule& module, const char* label,
                                                             const wgpu::PipelineLayout& layout);

    gpu::Context* context_ = nullptr;
    wgpu::TextureFormat hdrFormat_ = wgpu::TextureFormat::RGBA16Float;
    wgpu::TextureFormat depthFormat_ = wgpu::TextureFormat::Depth32Float;
    wgpu::BindGroupLayout waterLayout_;
    wgpu::PipelineLayout pipelineLayout_;
    wgpu::RenderPipeline pipeline_;      // with the tears (ADR-916)
    wgpu::RenderPipeline plainPipeline_; // with them compiled out
    // ADR-1286: the cinematic path, its own layout (uniforms, the frame copy, a sampler) and bind group.
    wgpu::BindGroupLayout cineLayout_;
    wgpu::PipelineLayout cinePipelineLayout_;
    wgpu::RenderPipeline cinePipeline_;
    wgpu::BindGroup cineBindGroup_;
    wgpu::Sampler cineSampler_;
    std::array<bool, kMaxWaterMaterials> cine_{};
    std::array<bool, kMaxWaterMaterials> torn_{};
    wgpu::Buffer uniforms_;
    wgpu::BindGroup bindGroup_;
    std::uint32_t uploaded_ = 0;
    std::vector<std::uint8_t> staging_;
};

} // namespace avgen::rendering
