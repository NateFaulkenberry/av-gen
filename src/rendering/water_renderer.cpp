#include "rendering/water_renderer.hpp"

#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_targets.hpp"

#include <algorithm>
#include <array>
#include <cstring>

namespace avgen::rendering {

namespace {

struct VertexLayoutStorage {
    std::array<wgpu::VertexAttribute, 3> attributes{};
    wgpu::VertexBufferLayout layout{};
    VertexLayoutStorage() {
        attributes[0].format = wgpu::VertexFormat::Float32x3;
        attributes[0].offset = offsetof(scene::Vertex, position);
        attributes[0].shaderLocation = 0;
        attributes[1].format = wgpu::VertexFormat::Float32x3;
        attributes[1].offset = offsetof(scene::Vertex, normal);
        attributes[1].shaderLocation = 1;
        attributes[2].format = wgpu::VertexFormat::Float32x2;
        attributes[2].offset = offsetof(scene::Vertex, uv);
        attributes[2].shaderLocation = 2;
        layout.arrayStride = sizeof(scene::Vertex);
        layout.stepMode = wgpu::VertexStepMode::Vertex;
        layout.attributeCount = attributes.size();
        layout.attributes = attributes.data();
    }
};

} // namespace

WaterUniforms waterUniformsFrom(const scene::WaterSettings& s, float flowTime, float fastest,
                                bool linearDepthValid) {
    WaterUniforms u;
    u.shallowColor = glm::vec4(s.shallowColor, std::max(s.shallow, 1e-3f));
    u.deepColor = glm::vec4(s.deepColor, std::max(s.clarity, 1e-3f));
    u.foamColor = glm::vec4(s.foamColor, s.foam);
    u.glowColor = glm::vec4(s.glowColor, s.glow);
    u.sparkleColor = glm::vec4(s.sparkleColor, s.sparkle);
    u.reflectTint = glm::vec4(s.reflectionTint, s.reflection);
    u.emissive = glm::vec4(s.emissiveColor * s.emissiveIntensity, 0.0f);
    // Roughness is floored well above zero: a perfect mirror picks mip 0 of the environment, which
    // on a night sky is the star field reproduced sharply in the river, and reads as a bug.
    u.surface = glm::vec4(s.fresnel, s.specular, std::clamp(s.roughness, 0.02f, 1.0f), s.maxOpacity);
    u.ripples = glm::vec4(s.ripple, std::max(s.rippleScale, 1e-4f), s.rippleSpeed, s.chop);
    u.shore = glm::vec4(s.foamWidth, s.edgeFade, s.refraction, 0.0f);
    u.life = glm::vec4(std::max(s.glowScale, 1e-4f), s.glowCoverage, s.glowDepth, s.swell);
    u.params = glm::vec4(flowTime, std::max(fastest, 1e-3f), linearDepthValid ? 1.0f : 0.0f, 0.0f);
    return u;
}

Result<void> WaterRenderer::init(gpu::Context& context, gpu::ShaderLibrary& shaders,
                                 wgpu::TextureFormat hdrFormat, wgpu::TextureFormat depthFormat,
                                 const wgpu::BindGroupLayout& frameLayout,
                                 const wgpu::BindGroupLayout& objectLayout,
                                 const wgpu::BindGroupLayout& iblLayout) {
    context_ = &context;
    hdrFormat_ = hdrFormat;
    depthFormat_ = depthFormat;
    const wgpu::Device& device = context.device();

    {
        wgpu::BindGroupLayoutEntry entry{};
        entry.binding = 0;
        entry.visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
        entry.buffer.type = wgpu::BufferBindingType::Uniform;
        entry.buffer.hasDynamicOffset = true;
        entry.buffer.minBindingSize = sizeof(WaterUniforms);
        wgpu::BindGroupLayoutDescriptor desc{};
        desc.label = "water-layout";
        desc.entryCount = 1;
        desc.entries = &entry;
        waterLayout_ = device.CreateBindGroupLayout(&desc);
    }
    {
        const std::array<wgpu::BindGroupLayout, 4> layouts = {frameLayout, objectLayout, waterLayout_,
                                                              iblLayout};
        wgpu::PipelineLayoutDescriptor desc{};
        desc.label = "water-pipeline-layout";
        desc.bindGroupLayoutCount = layouts.size();
        desc.bindGroupLayouts = layouts.data();
        pipelineLayout_ = device.CreatePipelineLayout(&desc);
    }
    {
        wgpu::BufferDescriptor desc{};
        desc.label = "water-uniforms";
        desc.size = static_cast<std::uint64_t>(kStride) * kMaxWaterMaterials;
        desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        uniforms_ = device.CreateBuffer(&desc);
        staging_.assign(static_cast<std::size_t>(desc.size), 0);

        wgpu::BindGroupEntry entry{};
        entry.binding = 0;
        entry.buffer = uniforms_;
        entry.size = sizeof(WaterUniforms);
        wgpu::BindGroupDescriptor bg{};
        bg.label = "water-bind-group";
        bg.layout = waterLayout_;
        bg.entryCount = 1;
        bg.entries = &entry;
        bindGroup_ = device.CreateBindGroup(&bg);
    }
    return createPipeline(shaders);
}

Result<void> WaterRenderer::reload(gpu::ShaderLibrary& shaders) {
    if (context_ == nullptr) {
        return {};
    }
    return createPipeline(shaders);
}

Result<void> WaterRenderer::createPipeline(gpu::ShaderLibrary& shaders) {
    auto module = shaders.load("water.wgsl");
    if (!module) {
        return std::unexpected(module.error());
    }
    VertexLayoutStorage vertex;

    wgpu::BlendState blend{};
    blend.color.operation = wgpu::BlendOperation::Add;
    blend.color.srcFactor = wgpu::BlendFactor::SrcAlpha;
    blend.color.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
    blend.alpha.operation = wgpu::BlendOperation::Add;
    blend.alpha.srcFactor = wgpu::BlendFactor::One;
    blend.alpha.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
    // The emission target takes the water's glint and sparkle *additively* rather than replacing
    // what is behind it, which is the only way a transparent surface can contribute to the bloom
    // mask without erasing the bloom of the glowing things it is drawn over.
    wgpu::BlendState additive{};
    additive.color.operation = wgpu::BlendOperation::Add;
    additive.color.srcFactor = wgpu::BlendFactor::One;
    additive.color.dstFactor = wgpu::BlendFactor::One;
    additive.alpha.operation = wgpu::BlendOperation::Add;
    additive.alpha.srcFactor = wgpu::BlendFactor::One;
    additive.alpha.dstFactor = wgpu::BlendFactor::One;

    const auto formats = sceneTargetFormats(hdrFormat_);
    std::array<wgpu::ColorTargetState, kSceneTargetCount> targets{};
    for (std::uint32_t i = 0; i < kSceneTargetCount; ++i) {
        targets[i] = wgpu::ColorTargetState{};
        targets[i].format = formats[i];
        // Normals, velocity and identifiers stay the opaque geometry's (ADR-035): one of those
        // averaged over a transparency is worse than none. Emission is the exception, above.
        targets[i].writeMask = (i == 0 || i == 3) ? wgpu::ColorWriteMask::All : wgpu::ColorWriteMask::None;
    }
    targets[0].blend = &blend;
    targets[3].blend = &additive;

    wgpu::FragmentState fragment{};
    fragment.module = *module;
    fragment.entryPoint = "fs_water";
    fragment.targetCount = kSceneTargetCount;
    fragment.targets = targets.data();

    wgpu::DepthStencilState depth{};
    depth.format = depthFormat_;
    depth.depthWriteEnabled = wgpu::OptionalBool::False;
    depth.depthCompare = wgpu::CompareFunction::Less;

    wgpu::RenderPipelineDescriptor desc{};
    desc.label = "water";
    desc.layout = pipelineLayout_;
    desc.vertex.module = *module;
    desc.vertex.entryPoint = "vs_water";
    desc.vertex.bufferCount = 1;
    desc.vertex.buffers = &vertex.layout;
    desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    desc.primitive.frontFace = wgpu::FrontFace::CCW;
    // Never culled: a surface seen from under it is still a surface, and a camera that dips below
    // the waterline is a shot somebody will take.
    desc.primitive.cullMode = wgpu::CullMode::None;
    desc.depthStencil = &depth;
    desc.multisample.count = 1; // Apple TBDR: every pipeline in this engine is single-sampled
    desc.multisample.mask = 0xFFFFFFFFu;
    desc.fragment = &fragment;

    const wgpu::Device& device = context_->device();
    device.PushErrorScope(wgpu::ErrorFilter::Validation);
    wgpu::RenderPipeline pipeline = device.CreateRenderPipeline(&desc);
    std::string error;
    auto future = device.PopErrorScope(
        wgpu::CallbackMode::WaitAnyOnly,
        [&](wgpu::PopErrorScopeStatus, wgpu::ErrorType type, wgpu::StringView msg) {
            if (type != wgpu::ErrorType::NoError) {
                error = std::string(msg.data, msg.length);
            }
        });
    context_->instance().WaitAny(future, UINT64_MAX);
    if (!error.empty()) {
        return fail("water pipeline: {}", error);
    }
    pipeline_ = pipeline;
    return {};
}

void WaterRenderer::upload(const wgpu::Queue& queue, const std::vector<WaterUniforms>& materials) {
    uploaded_ = static_cast<std::uint32_t>(std::min<std::size_t>(materials.size(), kMaxWaterMaterials));
    if (uploaded_ == 0) {
        return;
    }
    for (std::uint32_t i = 0; i < uploaded_; ++i) {
        std::memcpy(staging_.data() + static_cast<std::size_t>(i) * kStride, &materials[i],
                    sizeof(WaterUniforms));
    }
    queue.WriteBuffer(uniforms_, 0, staging_.data(), static_cast<std::size_t>(uploaded_) * kStride);
}

} // namespace avgen::rendering
