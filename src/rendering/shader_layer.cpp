#include "rendering/shader_layer.hpp"

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"

#include <array>
#include <cstring>

namespace avgen::rendering {

namespace {
constexpr const char* kErrorBody = R"(
fn mainImage(uv: vec2<f32>, fragCoord: vec2<f32>) -> vec4<f32> {
    let stripe = step(0.5, fract((uv.x + uv.y) * 8.0 + std.time * 0.5));
    return vec4<f32>(mix(vec3<f32>(1.0, 0.0, 1.0), vec3<f32>(0.1, 0.0, 0.1), stripe), 1.0);
}
)";
}

ShaderLayerGpu::ShaderLayerGpu(gpu::Context& context, gpu::ShaderLibrary& shaders, std::uint32_t id)
    : context_(context), shaders_(shaders), id_(id) {
    wgpu::SamplerDescriptor sdesc{};
    sdesc.label = "shader-layer-sampler";
    sdesc.addressModeU = wgpu::AddressMode::ClampToEdge;
    sdesc.addressModeV = wgpu::AddressMode::ClampToEdge;
    sdesc.magFilter = wgpu::FilterMode::Linear;
    sdesc.minFilter = wgpu::FilterMode::Linear;
    sampler_ = context_.device().CreateSampler(&sdesc);
    black_ = gpu::solidTexture(context_, 0, 0, 0, 255, false, "shader-layer-black");
}

Result<wgpu::ShaderModule> ShaderLayerGpu::compileModule(const std::string& source, const std::string& label) {
    return shaders_.compile(source, label);
}

Result<void> ShaderLayerGpu::compile(const shaders::ShaderLayer& layer) {
    const std::string label = "user:" + layer.name + "#" + std::to_string(id_);
    auto module = compileModule(layer.moduleSource, label);
    if (!module) {
        error_ = module.error().message;
        if (!module_) {
            // No good module yet: fall back to the error shader with the same bindings.
            const std::string fallback = shaders::generateModuleSource(layer.parsed.description, kErrorBody);
            auto fb = compileModule(fallback, label + ":error");
            if (!fb) {
                return fail("shader '{}' failed and so did the fallback: {}", layer.name, fb.error().message);
            }
            module = fb;
        } else {
            version_ = layer.version;
            return std::unexpected(module.error());
        }
    } else {
        error_.clear();
    }

    // Bind group layout: std, inputs, sampler, inputImage, audioSpectrum, one per named target.
    std::vector<wgpu::BindGroupLayoutEntry> entries;
    auto uniform = [&](std::uint32_t binding) {
        wgpu::BindGroupLayoutEntry e{};
        e.binding = binding;
        e.visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
        e.buffer.type = wgpu::BufferBindingType::Uniform;
        entries.push_back(e);
    };
    auto texture = [&](std::uint32_t binding) {
        wgpu::BindGroupLayoutEntry e{};
        e.binding = binding;
        e.visibility = wgpu::ShaderStage::Fragment;
        e.texture.sampleType = wgpu::TextureSampleType::Float;
        e.texture.viewDimension = wgpu::TextureViewDimension::e2D;
        entries.push_back(e);
    };
    uniform(0);
    uniform(1);
    {
        wgpu::BindGroupLayoutEntry e{};
        e.binding = 2;
        e.visibility = wgpu::ShaderStage::Fragment;
        e.sampler.type = wgpu::SamplerBindingType::Filtering;
        entries.push_back(e);
    }
    texture(3);
    texture(4);
    std::uint32_t binding = 5;
    for (const auto& pass : layer.parsed.description.passes) {
        if (!pass.target.empty()) {
            texture(binding++);
        }
    }
    wgpu::BindGroupLayoutDescriptor ldesc{};
    ldesc.label = "shader-layer-layout";
    ldesc.entryCount = entries.size();
    ldesc.entries = entries.data();
    wgpu::BindGroupLayout newLayout = context_.device().CreateBindGroupLayout(&ldesc);
    wgpu::PipelineLayoutDescriptor pdesc{};
    pdesc.bindGroupLayoutCount = 1;
    pdesc.bindGroupLayouts = &newLayout;
    wgpu::PipelineLayout newPipelineLayout = context_.device().CreatePipelineLayout(&pdesc);

    module_ = *module;
    layout_ = newLayout;
    pipelineLayout_ = newPipelineLayout;
    pipelines_.clear();
    description_ = layer.parsed.description;
    version_ = layer.version;
    targetsVersion_ = 0; // force target rebuild (pass list may have changed)

    // Uniform buffers: std per pass, inputs shared.
    stdBuffers_.clear();
    for (std::size_t i = 0; i < description_.passes.size(); ++i) {
        wgpu::BufferDescriptor bdesc{};
        bdesc.label = "shader-layer-std";
        bdesc.size = sizeof(shaders::StdUniforms);
        bdesc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        stdBuffers_.push_back(context_.device().CreateBuffer(&bdesc));
    }
    inputsBufferSize_ = std::max<std::size_t>(layer.layout.size, 16);
    wgpu::BufferDescriptor idesc{};
    idesc.label = "shader-layer-inputs";
    idesc.size = inputsBufferSize_;
    idesc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
    inputsBuffer_ = context_.device().CreateBuffer(&idesc);

    if (!error_.empty()) {
        return fail("{}", error_);
    }
    return {};
}

Result<wgpu::RenderPipeline> ShaderLayerGpu::pipelineFor(wgpu::TextureFormat format, bool withDepth) {
    const std::uint32_t key = static_cast<std::uint32_t>(format) | (withDepth ? 1u << 16 : 0u);
    if (auto it = pipelines_.find(key); it != pipelines_.end()) {
        return it->second;
    }
    wgpu::ColorTargetState colorTarget{};
    colorTarget.format = format;
    colorTarget.writeMask = wgpu::ColorWriteMask::All;
    wgpu::FragmentState fragment{};
    fragment.module = module_;
    fragment.entryPoint = "fs_main";
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;
    wgpu::DepthStencilState depth{};
    depth.format = wgpu::TextureFormat::Depth24Plus;
    depth.depthWriteEnabled = wgpu::OptionalBool::False;
    depth.depthCompare = wgpu::CompareFunction::Always;
    wgpu::RenderPipelineDescriptor desc{};
    desc.label = "shader-layer-pipeline";
    desc.layout = pipelineLayout_;
    desc.vertex.module = module_;
    desc.vertex.entryPoint = "vs_main";
    desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    desc.primitive.cullMode = wgpu::CullMode::None;
    desc.depthStencil = withDepth ? &depth : nullptr;
    desc.multisample.count = 1;
    desc.multisample.mask = 0xFFFFFFFFu;
    desc.fragment = &fragment;

    context_.device().PushErrorScope(wgpu::ErrorFilter::Validation);
    wgpu::RenderPipeline pipeline = context_.device().CreateRenderPipeline(&desc);
    std::string error;
    auto future = context_.device().PopErrorScope(
        wgpu::CallbackMode::WaitAnyOnly, [&](wgpu::PopErrorScopeStatus, wgpu::ErrorType type, wgpu::StringView msg) {
            if (type != wgpu::ErrorType::NoError) {
                error = gpu::Context::toString(msg);
            }
        });
    context_.waitFor(future);
    if (!error.empty() || !pipeline) {
        error_ = "pipeline: " + error;
        return fail("shader layer pipeline failed: {}", error);
    }
    pipelines_[key] = pipeline;
    return pipeline;
}

void ShaderLayerGpu::ensureTargets(const shaders::ShaderLayer& layer, const ShaderFrameContext& ctx) {
    if (targetsVersion_ == version_ && targetsWidth_ == ctx.width && targetsHeight_ == ctx.height) {
        return;
    }
    targets_.clear();
    for (const auto& pass : description_.passes) {
        if (pass.target.empty()) {
            continue;
        }
        Target t;
        t.name = pass.target;
        t.persistent = pass.persistent;
        t.format = pass.floatFormat ? wgpu::TextureFormat::RGBA16Float : wgpu::TextureFormat::RGBA8Unorm;
        auto w = shaders::evaluateSizeExpression(pass.widthExpr, ctx.width, ctx.height);
        auto h = shaders::evaluateSizeExpression(pass.heightExpr, ctx.width, ctx.height);
        t.width = w ? *w : ctx.width;
        t.height = h ? *h : ctx.height;
        const int copies = t.persistent ? 2 : 1;
        for (int i = 0; i < copies; ++i) {
            wgpu::TextureDescriptor desc{};
            desc.label = pass.target.c_str();
            desc.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;
            desc.dimension = wgpu::TextureDimension::e2D;
            desc.size = {t.width, t.height, 1};
            desc.format = t.format;
            t.textures[i] = context_.device().CreateTexture(&desc);
            t.views[i] = t.textures[i].CreateView();
        }
        targets_.push_back(std::move(t));
    }
    targetsVersion_ = version_;
    targetsWidth_ = ctx.width;
    targetsHeight_ = ctx.height;
    (void)layer;
}

wgpu::BindGroup ShaderLayerGpu::makeBindGroup(std::size_t passIndex, const ShaderFrameContext& ctx) {
    std::vector<wgpu::BindGroupEntry> entries;
    auto add = [&](std::uint32_t binding) -> wgpu::BindGroupEntry& {
        wgpu::BindGroupEntry e{};
        e.binding = binding;
        entries.push_back(e);
        return entries.back();
    };
    {
        auto& e = add(0);
        e.buffer = stdBuffers_[passIndex];
        e.size = sizeof(shaders::StdUniforms);
    }
    {
        auto& e = add(1);
        e.buffer = inputsBuffer_;
        e.size = inputsBufferSize_;
    }
    add(2).sampler = sampler_;
    add(3).textureView = ctx.inputImage ? ctx.inputImage : black_.view;
    add(4).textureView = ctx.audioSpectrum ? ctx.audioSpectrum : black_.view;
    std::uint32_t binding = 5;
    for (const auto& target : targets_) {
        add(binding++).textureView = target.readView();
    }
    wgpu::BindGroupDescriptor desc{};
    desc.label = "shader-layer-bind-group";
    desc.layout = layout_;
    desc.entryCount = entries.size();
    desc.entries = entries.data();
    return context_.device().CreateBindGroup(&desc);
}

void ShaderLayerGpu::writeUniforms(std::size_t passIndex, const shaders::ShaderLayer& layer,
                                   const ShaderFrameContext& ctx, std::uint32_t passWidth, std::uint32_t passHeight) {
    shaders::StdUniforms u = layer.std;
    u.passIndex = static_cast<float>(passIndex);
    u.renderSize[0] = static_cast<float>(ctx.width);
    u.renderSize[1] = static_cast<float>(ctx.height);
    u.passSize[0] = static_cast<float>(passWidth);
    u.passSize[1] = static_cast<float>(passHeight);
    context_.queue().WriteBuffer(stdBuffers_[passIndex], 0, &u, sizeof(u));
    if (passIndex == 0) {
        std::vector<std::uint8_t> bytes = layer.packedInputs;
        bytes.resize(inputsBufferSize_, 0);
        context_.queue().WriteBuffer(inputsBuffer_, 0, bytes.data(), bytes.size());
    }
}

void ShaderLayerGpu::renderPasses(wgpu::CommandEncoder& encoder, const shaders::ShaderLayer& layer,
                                  const ShaderFrameContext& ctx) {
    if (!module_ || ctx.width == 0 || ctx.height == 0) {
        return;
    }
    ensureTargets(layer, ctx);
    std::size_t targetIndex = 0;
    for (std::size_t passIndex = 0; passIndex < description_.passes.size(); ++passIndex) {
        const auto& pass = description_.passes[passIndex];
        if (pass.target.empty()) {
            continue; // output pass: drawn by drawOutput
        }
        Target& target = targets_[targetIndex++];
        auto pipeline = pipelineFor(target.format, false);
        if (!pipeline) {
            return;
        }
        writeUniforms(passIndex, layer, ctx, target.width, target.height);
        wgpu::BindGroup group = makeBindGroup(passIndex, ctx);
        wgpu::RenderPassColorAttachment color{};
        color.view = target.writeView();
        color.loadOp = wgpu::LoadOp::Clear;
        color.storeOp = wgpu::StoreOp::Store;
        wgpu::RenderPassDescriptor rpDesc{};
        rpDesc.label = "shader-layer-pass";
        rpDesc.colorAttachmentCount = 1;
        rpDesc.colorAttachments = &color;
        wgpu::RenderPassEncoder rp = encoder.BeginRenderPass(&rpDesc);
        rp.SetPipeline(*pipeline);
        rp.SetBindGroup(0, group);
        rp.Draw(3);
        rp.End();
        if (target.persistent) {
            target.current = 1 - target.current; // what we just wrote becomes next frame's read view
        }
    }
}

void ShaderLayerGpu::drawOutput(wgpu::RenderPassEncoder& pass, wgpu::TextureFormat targetFormat, bool withDepth,
                                const shaders::ShaderLayer& layer, const ShaderFrameContext& ctx) {
    if (!module_ || description_.passes.empty()) {
        return;
    }
    ensureTargets(layer, ctx);
    auto pipeline = pipelineFor(targetFormat, withDepth);
    if (!pipeline) {
        return;
    }
    const std::size_t passIndex = description_.passes.size() - 1;
    writeUniforms(passIndex, layer, ctx, ctx.width, ctx.height);
    if (description_.passes.size() == 1) {
        // Single-pass layer: inputs were not written by renderPasses.
        std::vector<std::uint8_t> bytes = layer.packedInputs;
        bytes.resize(inputsBufferSize_, 0);
        context_.queue().WriteBuffer(inputsBuffer_, 0, bytes.data(), bytes.size());
    }
    wgpu::BindGroup group = makeBindGroup(passIndex, ctx);
    pass.SetPipeline(*pipeline);
    pass.SetBindGroup(0, group);
    pass.Draw(3);
}

// ---- stack --------------------------------------------------------------------------------

ShaderStack::ShaderStack(gpu::Context& context, gpu::ShaderLibrary& shaders) : context_(context), shaders_(shaders) {}

void ShaderStack::sync(const shaders::ShaderLayerSet& layers) {
    std::vector<std::uint32_t> present;
    for (const auto& layer : layers.layers()) {
        present.push_back(layer->id);
        auto it = layers_.find(layer->id);
        if (it == layers_.end()) {
            it = layers_.emplace(layer->id, std::make_unique<ShaderLayerGpu>(context_, shaders_, layer->id)).first;
        }
        if (it->second->version() != layer->version) {
            if (auto r = it->second->compile(*layer); !r) {
                log::error("shader '{}': {}", layer->name, r.error().message);
            }
        }
    }
    for (auto it = layers_.begin(); it != layers_.end();) {
        if (std::find(present.begin(), present.end(), it->first) == present.end()) {
            it = layers_.erase(it);
        } else {
            ++it;
        }
    }
}

ShaderLayerGpu* ShaderStack::find(std::uint32_t id) {
    auto it = layers_.find(id);
    return it == layers_.end() ? nullptr : it->second.get();
}

std::string ShaderStack::errorFor(std::uint32_t id) const {
    auto it = layers_.find(id);
    return it == layers_.end() ? std::string{} : it->second->error();
}

} // namespace avgen::rendering
