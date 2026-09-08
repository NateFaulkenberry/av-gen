#pragma once

// Built-in post-processing chain (ADR-016) over the transient pool:
//   scene HDR -> [DoF] -> [motion blur] -> [bloom: prefilter, downsample chain, upsample chain]
//             -> composite (lens, bloom mix, grading) -> HDR result for tone mapping.
// Passes run only when their settings are active; with everything off the input is returned.

#include "core/error.hpp"
#include "gpu/transient_pool.hpp"
#include "scene/post_settings.hpp"

#include <glm/glm.hpp>
#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <vector>

namespace avgen::gpu {
class Context;
class ShaderLibrary;
} // namespace avgen::gpu

namespace avgen::rendering {

struct PostFrameInputs {
    wgpu::TextureView sceneHdr;
    wgpu::TextureView depth;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    glm::mat4 prevViewProj{1.0f};
    glm::mat4 invViewProj{1.0f};
    glm::vec3 cameraPos{0.0f};
    std::uint64_t frameIndex = 0;
    const scene::PostSettings* settings = nullptr;
};

struct PostStats {
    std::uint32_t passes = 0;
    std::uint32_t bloomLevels = 0;
};

class PostProcessor {
public:
    PostProcessor(gpu::Context& context, gpu::ShaderLibrary& shaders);
    [[nodiscard]] Result<void> init();
    [[nodiscard]] Result<void> reload();

    // Encodes the chain and returns the view to tone-map (a pool texture, or the input itself).
    wgpu::TextureView run(wgpu::CommandEncoder& encoder, const PostFrameInputs& inputs, gpu::TransientPool& pool);
    [[nodiscard]] const PostStats& stats() const { return stats_; }

    static constexpr wgpu::TextureFormat kHdrFormat = wgpu::TextureFormat::RGBA16Float;

private:
    struct Uniforms {
        glm::vec2 texelSize;
        glm::vec2 outputSize;
        glm::vec4 params0;
        glm::vec4 params1;
        glm::vec4 params2;
        glm::vec4 params3;
        glm::vec4 lift;
        glm::vec4 gamma;
        glm::vec4 gain;
        glm::vec4 cameraPos;
        glm::mat4 prevViewProj;
        glm::mat4 invViewProj;
    };
    static_assert(sizeof(Uniforms) == 16 + 16 * 8 + 128);
    static constexpr std::uint32_t kSlotStride = 512; // dynamic-offset alignment safe
    static constexpr std::uint32_t kMaxSlots = 64;

    Result<void> createPipelines(const wgpu::ShaderModule& module);
    Result<wgpu::RenderPipeline> makePipeline(const wgpu::ShaderModule& module, const char* entry);
    void runPass(wgpu::CommandEncoder& encoder, const wgpu::RenderPipeline& pipeline, const wgpu::TextureView& target,
                 const wgpu::TextureView& source, const wgpu::TextureView& second, const wgpu::TextureView& depth,
                 const Uniforms& uniforms);

    gpu::Context& context_;
    gpu::ShaderLibrary& shaders_;
    bool initialised_ = false;
    wgpu::BindGroupLayout layout_;
    wgpu::PipelineLayout pipelineLayout_;
    wgpu::RenderPipeline prefilter_;
    wgpu::RenderPipeline downsample_;
    wgpu::RenderPipeline upsample_;
    wgpu::RenderPipeline composite_;
    wgpu::RenderPipeline dof_;
    wgpu::RenderPipeline motionBlur_;
    wgpu::Sampler sampler_;
    wgpu::Buffer uniforms_;
    std::uint32_t slot_ = 0;
    wgpu::Texture black_;
    wgpu::TextureView blackView_;
    wgpu::Texture depthPlaceholder_;
    wgpu::TextureView depthPlaceholderView_;
    PostStats stats_;
};

} // namespace avgen::rendering
