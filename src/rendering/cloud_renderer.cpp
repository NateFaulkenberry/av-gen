#include "rendering/cloud_renderer.hpp"

#include "gpu/context.hpp"
#include "gpu/frame_timeline.hpp"
#include "gpu/resource_stats.hpp"
#include "gpu/shader_library.hpp"
#include "scene/scene.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstring>
#include <string>
#include <vector>

namespace avgen::rendering {

namespace {

struct CloudUniformGpu {
    glm::vec4 light;        // xyz toward the key, w = intensity
    glm::vec4 lightColor;   // rgb, w = anisotropy
    glm::vec4 ambientTop;   // rgb, w = extinction per metre
    glm::vec4 ambientBottom; // rgb, w = powder
    glm::vec4 warm;         // xyz toward the warm glow, w = warm height (m)
    glm::vec4 warmColor;    // rgb, w = noise amount
    glm::vec4 haze;         // rgb, w = haze distance (m)
    glm::vec4 march;        // x = steps, y = light steps, z = light distance (m), w = noise scale
    glm::vec4 bounds;       // xyz = bounding sphere centre, w = its radius
    glm::vec4 misc;         // x = lobe count, y = seed, zw = 0
};
static_assert(sizeof(CloudUniformGpu) == 160);

struct LobeGpu {
    glm::vec4 centreRadius;
    glm::vec4 scaleDensity;
    glm::vec4 soft;
};

std::uint64_t mixKey(std::uint64_t h, float v) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &v, 4);
    return (h ^ (bits + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2))) * 1099511628211ull;
}

} // namespace

bool CloudRenderer::wants(const scene::Scene& scene) const {
    return scene.clouds.active();
}

Result<void> CloudRenderer::init(gpu::Context& context, gpu::ShaderLibrary& shaders, wgpu::TextureFormat hdrFormat) {
    context_ = &context;
    hdrFormat_ = hdrFormat;
    const wgpu::Device& device = context.device();
    std::array<wgpu::BindGroupLayoutEntry, 4> e{};
    e[0].binding = 0;
    e[0].visibility = wgpu::ShaderStage::Fragment | wgpu::ShaderStage::Vertex;
    e[0].buffer.type = wgpu::BufferBindingType::Uniform;
    e[1].binding = 2;
    e[1].visibility = wgpu::ShaderStage::Fragment;
    e[1].texture.sampleType = wgpu::TextureSampleType::UnfilterableFloat;
    e[1].texture.viewDimension = wgpu::TextureViewDimension::e2D;
    e[2].binding = 3;
    e[2].visibility = wgpu::ShaderStage::Fragment;
    e[2].buffer.type = wgpu::BufferBindingType::Uniform;
    e[3].binding = 4;
    e[3].visibility = wgpu::ShaderStage::Fragment;
    e[3].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
    wgpu::BindGroupLayoutDescriptor d{};
    d.label = "clouds-0";
    d.entryCount = e.size();
    d.entries = e.data();
    layout_ = device.CreateBindGroupLayout(&d);
    wgpu::BufferDescriptor bd{};
    bd.label = "clouds-uniforms";
    bd.size = sizeof(CloudUniformGpu);
    bd.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
    uniforms_ = device.CreateBuffer(&bd);
    return createPipeline(shaders);
}

Result<void> CloudRenderer::reload(gpu::ShaderLibrary& shaders) {
    if (context_ == nullptr) {
        return {};
    }
    return createPipeline(shaders);
}

Result<void> CloudRenderer::createPipeline(gpu::ShaderLibrary& shaders) {
    auto module = shaders.load("clouds.wgsl");
    if (!module) {
        return std::unexpected(module.error());
    }
    const wgpu::Device& device = context_->device();
    wgpu::PipelineLayoutDescriptor pl{};
    pl.label = "clouds";
    pl.bindGroupLayoutCount = 1;
    pl.bindGroupLayouts = &layout_;
    // Premultiplied over the lit frame: what is behind the cloud survives by its transmittance.
    wgpu::BlendState blend{};
    blend.color.operation = wgpu::BlendOperation::Add;
    blend.color.srcFactor = wgpu::BlendFactor::One;
    blend.color.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
    blend.alpha.operation = wgpu::BlendOperation::Add;
    blend.alpha.srcFactor = wgpu::BlendFactor::Zero;
    blend.alpha.dstFactor = wgpu::BlendFactor::One;
    wgpu::ColorTargetState target{};
    target.format = hdrFormat_;
    target.blend = &blend;
    wgpu::FragmentState fragment{};
    fragment.module = *module;
    fragment.entryPoint = "fs_clouds";
    fragment.targetCount = 1;
    fragment.targets = &target;
    wgpu::RenderPipelineDescriptor rd{};
    rd.label = "clouds";
    rd.layout = device.CreatePipelineLayout(&pl);
    rd.vertex.module = *module;
    rd.vertex.entryPoint = "vs_clouds";
    rd.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    rd.multisample.count = 1;
    rd.multisample.mask = 0xFFFFFFFFu;
    rd.fragment = &fragment;
    device.PushErrorScope(wgpu::ErrorFilter::Validation);
    wgpu::RenderPipeline pipeline = gpu::createRenderPipeline(device, &rd);
    std::string error;
    auto future = device.PopErrorScope(wgpu::CallbackMode::WaitAnyOnly,
                                       [&](wgpu::PopErrorScopeStatus, wgpu::ErrorType type, wgpu::StringView msg) {
                                           if (type != wgpu::ErrorType::NoError) {
                                               error = std::string(msg.data, msg.length);
                                           }
                                       });
    context_->instance().WaitAny(future, UINT64_MAX);
    if (!error.empty() || !pipeline) {
        return fail("clouds pipeline: {}", error);
    }
    pipeline_ = pipeline;
    return {};
}

void CloudRenderer::encode(const EnvironmentFrame& f) {
    if (f.encoder == nullptr || f.scene == nullptr || f.frameUniforms == nullptr || !ready() || !wants(*f.scene) ||
        f.width == 0 || f.height == 0) {
        return;
    }
    const scene::Clouds& c = f.scene->clouds;
    const wgpu::Device& device = context_->device();

    // The lobes, uploaded when they change.
    std::uint64_t key = 1469598103934665603ull;
    for (const scene::CloudLobe& l : c.lobes) {
        for (float v : {l.centre.x, l.centre.y, l.centre.z, l.radius, l.scale.x, l.scale.y, l.scale.z, l.density,
                        l.soft}) {
            key = mixKey(key, v);
        }
    }
    glm::vec3 lo(1e30f), hi(-1e30f);
    for (const scene::CloudLobe& l : c.lobes) {
        const glm::vec3 ext = l.radius * l.scale;
        lo = glm::min(lo, l.centre - ext);
        hi = glm::max(hi, l.centre + ext);
    }
    const glm::vec3 centre = 0.5f * (lo + hi);
    const float radius = glm::length(hi - lo) * 0.5f;
    if (key != lobesKey_ || !lobes_) {
        std::vector<LobeGpu> rows;
        rows.reserve(c.lobes.size());
        for (const scene::CloudLobe& l : c.lobes) {
            rows.push_back({glm::vec4(l.centre, l.radius), glm::vec4(l.scale, l.density), glm::vec4(l.soft, 0, 0, 0)});
        }
        wgpu::BufferDescriptor bd{};
        bd.label = "clouds-lobes";
        bd.size = std::max<std::uint64_t>(rows.size() * sizeof(LobeGpu), sizeof(LobeGpu));
        bd.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
        lobes_ = device.CreateBuffer(&bd);
        context_->queue().WriteBuffer(lobes_, 0, rows.data(), rows.size() * sizeof(LobeGpu));
        lobesKey_ = key;
        lobeCount_ = static_cast<std::uint32_t>(rows.size());
    }
    CloudUniformGpu u{};
    u.light = glm::vec4(glm::normalize(c.lightDirection), c.lightIntensity);
    u.lightColor = glm::vec4(c.lightColor, c.anisotropy);
    u.ambientTop = glm::vec4(c.ambientTop, c.extinction);
    u.ambientBottom = glm::vec4(c.ambientBottom, c.powder);
    u.warm = glm::vec4(glm::normalize(c.warmDirection), c.warmHeight);
    u.warmColor = glm::vec4(c.warmColor, c.noiseAmount);
    u.haze = glm::vec4(c.hazeColor, c.hazeDistance);
    u.march = glm::vec4(static_cast<float>(c.steps), static_cast<float>(c.lightSteps), c.lightDistance, c.noiseScale);
    u.bounds = glm::vec4(centre, radius);
    u.misc = glm::vec4(static_cast<float>(lobeCount_), static_cast<float>(c.seed % 1024u), 0.0f, 0.0f);
    context_->queue().WriteBuffer(uniforms_, 0, &u, sizeof(u));

    std::array<wgpu::BindGroupEntry, 4> e{};
    e[0].binding = 0;
    e[0].buffer = *f.frameUniforms;
    e[0].size = f.frameUniforms->GetSize();
    e[1].binding = 2;
    e[1].textureView = f.linearDepth;
    e[2].binding = 3;
    e[2].buffer = uniforms_;
    e[2].size = sizeof(CloudUniformGpu);
    e[3].binding = 4;
    e[3].buffer = lobes_;
    e[3].size = lobes_.GetSize();
    wgpu::BindGroupDescriptor bg{};
    bg.label = "clouds-0";
    bg.layout = layout_;
    bg.entryCount = e.size();
    bg.entries = e.data();
    const wgpu::BindGroup group = device.CreateBindGroup(&bg);

    wgpu::RenderPassColorAttachment colour{};
    colour.view = f.hdr;
    colour.loadOp = wgpu::LoadOp::Load;
    colour.storeOp = wgpu::StoreOp::Store;
    colour.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    wgpu::RenderPassDescriptor rp{};
    rp.label = "clouds";
    rp.timestampWrites = f.timeline != nullptr ? f.timeline->mark("clouds", gpu::FrameTimeline::PassKind::Render)
                                               : nullptr;
    rp.colorAttachmentCount = 1;
    rp.colorAttachments = &colour;
    wgpu::RenderPassEncoder pass = f.encoder->BeginRenderPass(&rp);
    pass.SetPipeline(pipeline_);
    pass.SetBindGroup(0, group);
    pass.Draw(3);
    pass.End();
}

} // namespace avgen::rendering
