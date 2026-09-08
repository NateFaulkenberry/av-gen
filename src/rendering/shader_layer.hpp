#pragma once

// GPU side of user shader layers (milestone 0.4): compiles the generated module, owns pass
// targets (ping-pong for persistent feedback targets), builds bind groups per pass, and draws.
// Mirrors shaders::ShaderLayerSet by (id, version); on compile failure the last good pipelines
// stay in use, or an error shader (magenta stripes) if there were none.

#include "core/error.hpp"
#include "gpu/texture.hpp"
#include "shaders/shader_format.hpp"
#include "shaders/shader_layers.hpp"

#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace avgen::gpu {
class Context;
class ShaderLibrary;
} // namespace avgen::gpu

namespace avgen::rendering {

// Per-frame inputs shared by every layer.
struct ShaderFrameContext {
    std::uint32_t width = 0;  // layer output size (the HDR target)
    std::uint32_t height = 0;
    wgpu::TextureView inputImage;    // post stage: the current HDR scene; else null (1x1 black used)
    wgpu::TextureView audioSpectrum; // binCount x 1 RGBA16F; null = 1x1 black
    std::uint64_t frameIndex = 0;
};

class ShaderLayerGpu {
public:
    ShaderLayerGpu(gpu::Context& context, gpu::ShaderLibrary& shaders, std::uint32_t id);

    // (Re)compiles from the layer's generated module. Returns the compile error (also kept in
    // error()) but leaves the previous pipelines in place.
    Result<void> compile(const shaders::ShaderLayer& layer);
    [[nodiscard]] std::uint64_t version() const { return version_; }
    [[nodiscard]] const std::string& error() const { return error_; }
    [[nodiscard]] bool usable() const { return module_ != nullptr; }

    // Runs every non-output pass into its target (own render passes on the encoder).
    void renderPasses(wgpu::CommandEncoder& encoder, const shaders::ShaderLayer& layer, const ShaderFrameContext& ctx);
    // Draws the output pass into the caller's render pass (fullscreen triangle).
    void drawOutput(wgpu::RenderPassEncoder& pass, wgpu::TextureFormat targetFormat, bool withDepth,
                    const shaders::ShaderLayer& layer, const ShaderFrameContext& ctx);

private:
    struct Target {
        std::string name;
        bool persistent = false;
        wgpu::TextureFormat format = wgpu::TextureFormat::RGBA8Unorm;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        wgpu::Texture textures[2];
        wgpu::TextureView views[2];
        int current = 0; // write index for persistent targets
        bool cleared = false;
        [[nodiscard]] const wgpu::TextureView& readView() const { return persistent ? views[1 - current] : views[0]; }
        [[nodiscard]] const wgpu::TextureView& writeView() const { return persistent ? views[current] : views[0]; }
    };

    Result<wgpu::RenderPipeline> pipelineFor(wgpu::TextureFormat format, bool withDepth);
    void ensureTargets(const shaders::ShaderLayer& layer, const ShaderFrameContext& ctx);
    wgpu::BindGroup makeBindGroup(std::size_t passIndex, const ShaderFrameContext& ctx);
    void writeUniforms(std::size_t passIndex, const shaders::ShaderLayer& layer, const ShaderFrameContext& ctx,
                       std::uint32_t passWidth, std::uint32_t passHeight);
    Result<wgpu::ShaderModule> compileModule(const std::string& source, const std::string& label);

    gpu::Context& context_;
    gpu::ShaderLibrary& shaders_;
    std::uint32_t id_;
    std::uint64_t version_ = 0;
    std::string error_;
    shaders::ShaderDescription description_; // of the compiled module
    wgpu::ShaderModule module_;
    wgpu::BindGroupLayout layout_;
    wgpu::PipelineLayout pipelineLayout_;
    std::map<std::uint32_t, wgpu::RenderPipeline> pipelines_; // key: format | depth << 16
    std::vector<wgpu::Buffer> stdBuffers_;                    // one per pass
    wgpu::Buffer inputsBuffer_;
    std::size_t inputsBufferSize_ = 0;
    wgpu::Sampler sampler_;
    gpu::GpuTexture black_;
    std::vector<Target> targets_; // named targets in pass order (output pass has none)
    std::uint32_t targetsWidth_ = 0;
    std::uint32_t targetsHeight_ = 0;
    std::uint64_t targetsVersion_ = 0;
};

class ShaderStack {
public:
    ShaderStack(gpu::Context& context, gpu::ShaderLibrary& shaders);
    // Creates/recompiles GPU layers to match the set; drops removed ones.
    void sync(const shaders::ShaderLayerSet& layers);
    [[nodiscard]] ShaderLayerGpu* find(std::uint32_t id);
    [[nodiscard]] std::string errorFor(std::uint32_t id) const;
    [[nodiscard]] std::size_t size() const { return layers_.size(); }

private:
    gpu::Context& context_;
    gpu::ShaderLibrary& shaders_;
    std::map<std::uint32_t, std::unique_ptr<ShaderLayerGpu>> layers_;
};

} // namespace avgen::rendering
