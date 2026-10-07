#include "rendering/ecosystem_renderer.hpp"

#include "gpu/context.hpp"
#include "gpu/frame_timeline.hpp"
#include "gpu/resource_stats.hpp"
#include "gpu/shader_library.hpp"
#include "scene/scene.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace avgen::rendering {

namespace {

struct EcoFrameGpu {
    glm::vec4 size;
    glm::vec4 misc;
};

struct LayerUniformGpu {
    glm::vec4 color;
    glm::vec4 excited;
    glm::vec4 response;
    glm::vec4 rest;
    glm::vec4 pulse;
    glm::ivec4 slots;
    glm::vec4 bound;
    glm::vec4 optics; // iridescence, scale, speed, bob
    glm::vec4 motion; // bob rate, unused x3
};
static_assert(sizeof(LayerUniformGpu) == 144);

struct HostGpu {
    glm::vec4 row0, row1, row2;
};

constexpr float kFixedPoint = 2048.0f;

wgpu::BindGroupLayoutEntry bufferEntry(std::uint32_t binding, wgpu::ShaderStage stages,
                                       wgpu::BufferBindingType type) {
    wgpu::BindGroupLayoutEntry e{};
    e.binding = binding;
    e.visibility = stages;
    e.buffer.type = type;
    return e;
}

std::uint64_t mix(std::uint64_t h, std::uint64_t v) {
    return (h ^ (v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2))) * 1099511628211ull;
}

} // namespace

bool EcosystemRenderer::wants(const scene::Scene& scene) {
    return scene.ecosystem.active();
}

Result<void> EcosystemRenderer::init(gpu::Context& context, gpu::ShaderLibrary& shaders, wgpu::TextureFormat hdrFormat,
                                     wgpu::TextureFormat depthFormat) {
    context_ = &context;
    hdrFormat_ = hdrFormat;
    depthFormat_ = depthFormat;
    const wgpu::Device& device = context.device();
    const auto C = wgpu::ShaderStage::Compute;
    const auto VF = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
    {
        std::array<wgpu::BindGroupLayoutEntry, 9> e{};
        e[0] = bufferEntry(0, C, wgpu::BufferBindingType::Uniform);
        e[1] = bufferEntry(1, C, wgpu::BufferBindingType::Uniform);
        e[2] = wgpu::BindGroupLayoutEntry{};
        e[2].binding = 2;
        e[2].visibility = C;
        e[2].texture.sampleType = wgpu::TextureSampleType::UnfilterableFloat;
        e[2].texture.viewDimension = wgpu::TextureViewDimension::e2D;
        e[3] = bufferEntry(3, C, wgpu::BufferBindingType::Storage);
        e[4] = bufferEntry(4, C, wgpu::BufferBindingType::Storage);
        e[5] = bufferEntry(5, C, wgpu::BufferBindingType::Storage);
        e[6] = bufferEntry(6, C, wgpu::BufferBindingType::Uniform);
        e[7] = bufferEntry(7, C, wgpu::BufferBindingType::Storage);
        e[8] = bufferEntry(15, C, wgpu::BufferBindingType::ReadOnlyStorage);
        wgpu::BindGroupLayoutDescriptor d{};
        d.label = "ecosystem-compute-0";
        d.entryCount = e.size();
        d.entries = e.data();
        computeLayout0_ = device.CreateBindGroupLayout(&d);
    }
    {
        wgpu::BindGroupLayoutDescriptor d{};
        d.label = "ecosystem-empty";
        emptyLayout_ = device.CreateBindGroupLayout(&d);
    }
    {
        std::array<wgpu::BindGroupLayoutEntry, 3> e{};
        e[0] = bufferEntry(0, C, wgpu::BufferBindingType::Uniform);
        e[1] = bufferEntry(1, C, wgpu::BufferBindingType::ReadOnlyStorage);
        e[2] = bufferEntry(2, C, wgpu::BufferBindingType::ReadOnlyStorage);
        wgpu::BindGroupLayoutDescriptor d{};
        d.label = "ecosystem-layer";
        d.entryCount = e.size();
        d.entries = e.data();
        layerLayout_ = device.CreateBindGroupLayout(&d);
    }
    {
        std::array<wgpu::BindGroupLayoutEntry, 2> e{};
        e[0] = bufferEntry(6, VF, wgpu::BufferBindingType::Uniform);
        e[1] = bufferEntry(8, wgpu::ShaderStage::Fragment, wgpu::BufferBindingType::ReadOnlyStorage);
        wgpu::BindGroupLayoutDescriptor d{};
        d.label = "ecosystem-resolve";
        d.entryCount = e.size();
        d.entries = e.data();
        resolveLayout_ = device.CreateBindGroupLayout(&d);
    }
    {
        std::array<wgpu::BindGroupLayoutEntry, 2> e{};
        e[0] = bufferEntry(0, VF, wgpu::BufferBindingType::Uniform);
        e[1] = bufferEntry(9, wgpu::ShaderStage::Vertex, wgpu::BufferBindingType::ReadOnlyStorage);
        wgpu::BindGroupLayoutDescriptor d{};
        d.label = "ecosystem-sprite";
        d.entryCount = e.size();
        d.entries = e.data();
        spriteLayout_ = device.CreateBindGroupLayout(&d);
    }
    {
        wgpu::BufferDescriptor d{};
        d.label = "ecosystem-frame";
        d.size = sizeof(EcoFrameGpu);
        d.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        frameBuffer_ = device.CreateBuffer(&d);
        d.label = "ecosystem-counters";
        d.size = 16;
        d.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
        counters_ = device.CreateBuffer(&d);
        d.label = "ecosystem-sprite-args";
        d.size = 16;
        d.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::Indirect | wgpu::BufferUsage::CopyDst;
        spriteArgsBuffer_ = device.CreateBuffer(&d);
    }
    return createPipelines(shaders);
}

Result<void> EcosystemRenderer::reload(gpu::ShaderLibrary& shaders) {
    if (context_ == nullptr) {
        return {};
    }
    return createPipelines(shaders);
}

Result<void> EcosystemRenderer::createPipelines(gpu::ShaderLibrary& shaders) {
    auto module = shaders.load("ecosystem.wgsl");
    if (!module) {
        return std::unexpected(module.error());
    }
    const wgpu::Device& device = context_->device();
    std::string error;
    auto check = [&](const char* what) -> Result<void> {
        auto future = device.PopErrorScope(wgpu::CallbackMode::WaitAnyOnly,
                                           [&](wgpu::PopErrorScopeStatus, wgpu::ErrorType type, wgpu::StringView msg) {
                                               if (type != wgpu::ErrorType::NoError) {
                                                   error = std::string(msg.data, msg.length);
                                               }
                                           });
        context_->instance().WaitAny(future, UINT64_MAX);
        if (!error.empty()) {
            return fail("ecosystem pipeline ({}): {}", what, error);
        }
        return {};
    };

    // Compute: group 0 (frame, fields, depth, buffers), group 1 empty (the common module's object group), group 2 layer.
    std::array<wgpu::BindGroupLayout, 3> groups = {computeLayout0_, emptyLayout_, layerLayout_};
    wgpu::PipelineLayoutDescriptor pl{};
    pl.label = "ecosystem-compute";
    pl.bindGroupLayoutCount = groups.size();
    pl.bindGroupLayouts = groups.data();
    const wgpu::PipelineLayout computeLayout = device.CreatePipelineLayout(&pl);
    pl.label = "ecosystem-args";
    pl.bindGroupLayoutCount = 1;
    const wgpu::PipelineLayout argsLayout = device.CreatePipelineLayout(&pl);

    device.PushErrorScope(wgpu::ErrorFilter::Validation);
    wgpu::ComputePipelineDescriptor cd{};
    cd.label = "ecosystem-emit";
    cd.layout = computeLayout;
    cd.compute.module = *module;
    cd.compute.entryPoint = "cs_emit";
    wgpu::ComputePipeline emit = gpu::createComputePipeline(device, &cd);
    cd.label = "ecosystem-sprite-args";
    cd.layout = argsLayout;
    cd.compute.entryPoint = "cs_sprite_args";
    wgpu::ComputePipeline args = gpu::createComputePipeline(device, &cd);
    if (auto r = check("compute"); !r) {
        return r;
    }

    // Render: HDR + emission, additive; the emission alpha (bloom weight) keeps the larger.
    wgpu::BlendState add{};
    add.color.operation = wgpu::BlendOperation::Add;
    add.color.srcFactor = wgpu::BlendFactor::One;
    add.color.dstFactor = wgpu::BlendFactor::One;
    add.alpha.operation = wgpu::BlendOperation::Add;
    add.alpha.srcFactor = wgpu::BlendFactor::Zero;
    add.alpha.dstFactor = wgpu::BlendFactor::One;
    wgpu::BlendState emissionBlend = add;
    emissionBlend.alpha.operation = wgpu::BlendOperation::Max;
    emissionBlend.alpha.srcFactor = wgpu::BlendFactor::One;
    emissionBlend.alpha.dstFactor = wgpu::BlendFactor::One;
    std::array<wgpu::ColorTargetState, 2> targets{};
    targets[0].format = hdrFormat_;
    targets[0].blend = &add;
    targets[1].format = wgpu::TextureFormat::RGBA16Float;
    targets[1].blend = &emissionBlend;

    auto makeRender = [&](const char* label, const wgpu::BindGroupLayout& g0, const char* vs, const char* fs,
                          bool depth) {
        wgpu::PipelineLayoutDescriptor rl{};
        rl.label = label;
        rl.bindGroupLayoutCount = 1;
        rl.bindGroupLayouts = &g0;
        wgpu::FragmentState fragment{};
        fragment.module = *module;
        fragment.entryPoint = fs;
        fragment.targetCount = targets.size();
        fragment.targets = targets.data();
        wgpu::DepthStencilState ds{};
        ds.format = depthFormat_;
        ds.depthWriteEnabled = wgpu::OptionalBool::False;
        ds.depthCompare = wgpu::CompareFunction::Less;
        wgpu::RenderPipelineDescriptor rd{};
        rd.label = label;
        rd.layout = device.CreatePipelineLayout(&rl);
        rd.vertex.module = *module;
        rd.vertex.entryPoint = vs;
        rd.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
        rd.primitive.cullMode = wgpu::CullMode::None;
        rd.depthStencil = depth ? &ds : nullptr;
        rd.multisample.count = 1;
        rd.multisample.mask = 0xFFFFFFFFu;
        rd.fragment = &fragment;
        return gpu::createRenderPipeline(device, &rd);
    };
    device.PushErrorScope(wgpu::ErrorFilter::Validation);
    wgpu::RenderPipeline resolve = makeRender("ecosystem-resolve", resolveLayout_, "vs_resolve", "fs_resolve", false);
    wgpu::RenderPipeline sprite = makeRender("ecosystem-sprite", spriteLayout_, "vs_sprite", "fs_sprite", true);
    if (auto r = check("render"); !r) {
        return r;
    }
    if (!emit || !args || !resolve || !sprite) {
        return fail("ecosystem pipelines were not created");
    }
    emit_ = emit;
    spriteArgs_ = args;
    resolve_ = resolve;
    sprite_ = sprite;
    return {};
}

void EcosystemRenderer::ensureTargets(std::uint32_t width, std::uint32_t height, std::uint32_t maxSprites) {
    const wgpu::Device& device = context_->device();
    if (width != width_ || height != height_ || !accum_) {
        wgpu::BufferDescriptor d{};
        d.label = "ecosystem-accum";
        d.size = static_cast<std::uint64_t>(std::max(width, 1u)) * std::max(height, 1u) * 3u * sizeof(std::uint32_t);
        d.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
        accum_ = device.CreateBuffer(&d);
        width_ = width;
        height_ = height;
    }
    maxSprites = std::max(maxSprites, 1u);
    if (maxSprites != maxSprites_ || !sprites_) {
        wgpu::BufferDescriptor d{};
        d.label = "ecosystem-sprites";
        d.size = static_cast<std::uint64_t>(maxSprites) * 32u;
        d.usage = wgpu::BufferUsage::Storage;
        sprites_ = device.CreateBuffer(&d);
        maxSprites_ = maxSprites;
    }
}

void EcosystemRenderer::syncLayer(std::size_t index, const scene::Scene& scene, const scene::EmitterLayer& layer) {
    if (layers_.size() <= index) {
        layers_.resize(index + 1);
    }
    LayerGpu& g = layers_[index];
    // Identity of what is uploaded: the hosts (by name, instance count and structure) and the template.
    std::uint64_t key = mix(1469598103934665603ull, layer.pointsHash);
    std::vector<const scene::ProceduralGeometry*> hosts;
    for (const std::string& name : layer.hosts) {
        for (const scene::ProceduralGeometry& p : scene.procedurals) {
            if (p.name == name) {
                hosts.push_back(&p);
                key = mix(key, std::hash<std::string>{}(name));
                key = mix(key, p.instances.size());
                key = mix(key, p.structureVersion);
                key = mix(key, p.builtHash);
                break;
            }
        }
    }
    const wgpu::Device& device = context_->device();
    if (key != g.key || !g.uniforms) {
        std::vector<HostGpu> rows;
        for (const scene::ProceduralGeometry* p : hosts) {
            // The records the procedural renderer draws (distribution, variation and transform already applied),
            // then the source transform the vertex stage applies after them.
            const glm::mat4 source = p->sourceTransform.matrix();
            for (const spatial::InstanceRecord& rec : p->instances) {
                const glm::quat q(rec.rotation.w, rec.rotation.x, rec.rotation.y, rec.rotation.z);
                const glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(rec.position)) * glm::mat4_cast(q) *
                                    glm::scale(glm::mat4(1.0f), glm::vec3(rec.scale)) * source;
                // glm is column-major: row r of the affine is (m[0][r], m[1][r], m[2][r], m[3][r]).
                HostGpu h;
                h.row0 = glm::vec4(m[0][0], m[1][0], m[2][0], m[3][0]);
                h.row1 = glm::vec4(m[0][1], m[1][1], m[2][1], m[3][1]);
                h.row2 = glm::vec4(m[0][2], m[1][2], m[2][2], m[3][2]);
                rows.push_back(h);
            }
        }
        const auto& pts = *layer.points;
        float bound = 0.0f;
        for (const scene::EmitterPoint& pt : pts) {
            bound = std::max(bound, glm::length(pt.position) + pt.radius);
        }
        auto makeBuffer = [&](const char* label, std::uint64_t bytes, wgpu::BufferUsage usage) {
            wgpu::BufferDescriptor d{};
            d.label = label;
            d.size = std::max<std::uint64_t>(bytes, 16);
            d.usage = usage | wgpu::BufferUsage::CopyDst;
            return device.CreateBuffer(&d);
        };
        g.hosts = makeBuffer("ecosystem-hosts", rows.size() * sizeof(HostGpu), wgpu::BufferUsage::Storage);
        g.points = makeBuffer("ecosystem-points", pts.size() * sizeof(scene::EmitterPoint), wgpu::BufferUsage::Storage);
        if (!g.uniforms) {
            g.uniforms = makeBuffer("ecosystem-layer", sizeof(LayerUniformGpu), wgpu::BufferUsage::Uniform);
        }
        if (!rows.empty()) {
            context_->queue().WriteBuffer(g.hosts, 0, rows.data(), rows.size() * sizeof(HostGpu));
        }
        if (!pts.empty()) {
            context_->queue().WriteBuffer(g.points, 0, pts.data(), pts.size() * sizeof(scene::EmitterPoint));
        }
        std::array<wgpu::BindGroupEntry, 3> e{};
        e[0].binding = 0;
        e[0].buffer = g.uniforms;
        e[0].size = sizeof(LayerUniformGpu);
        e[1].binding = 1;
        e[1].buffer = g.hosts;
        e[1].size = g.hosts.GetSize();
        e[2].binding = 2;
        e[2].buffer = g.points;
        e[2].size = g.points.GetSize();
        wgpu::BindGroupDescriptor bd{};
        bd.label = "ecosystem-layer";
        bd.layout = layerLayout_;
        bd.entryCount = e.size();
        bd.entries = e.data();
        g.group = device.CreateBindGroup(&bd);
        g.hostCount = static_cast<std::uint32_t>(rows.size());
        g.pointCount = static_cast<std::uint32_t>(pts.size());
        g.boundRadius = bound;
        g.key = key;
    }
}

void EcosystemRenderer::encode(wgpu::CommandEncoder& encoder, const scene::Scene& scene, double time,
                               const wgpu::Buffer& frameUniforms, const FieldUniforms& fields,
                               const wgpu::TextureView& linearDepth, const wgpu::TextureView& hdr,
                               const wgpu::TextureView& emission, const wgpu::TextureView& depth, std::uint32_t width,
                               std::uint32_t height, gpu::FrameTimeline* timeline) {
    stats_ = EcosystemStats{};
    if (!ready() || !wants(scene) || width == 0 || height == 0) {
        return;
    }
    const scene::Ecosystem& eco = scene.ecosystem;
    ensureTargets(width, height, eco.maxSprites);
    const wgpu::Device& device = context_->device();

    EcoFrameGpu frame{};
    frame.size = glm::vec4(static_cast<float>(width), static_cast<float>(height), 1.0f / static_cast<float>(width),
                           1.0f / static_cast<float>(height));
    frame.misc = glm::vec4(static_cast<float>(time), eco.spriteRadius, static_cast<float>(maxSprites_), kFixedPoint);
    context_->queue().WriteBuffer(frameBuffer_, 0, &frame, sizeof(frame));
    encoder.ClearBuffer(accum_, 0, accum_.GetSize());
    encoder.ClearBuffer(counters_, 0, 16);

    // Group 0 for compute.
    wgpu::BindGroup group0;
    {
        std::array<wgpu::BindGroupEntry, 9> e{};
        e[0].binding = 0;
        e[0].buffer = frameUniforms;
        e[0].size = frameUniforms.GetSize();
        e[1].binding = 1;
        e[1].buffer = fields.buffer();
        e[1].size = FieldUniforms::kBufferSize;
        e[2].binding = 2;
        e[2].textureView = linearDepth;
        e[3].binding = 3;
        e[3].buffer = accum_;
        e[3].size = accum_.GetSize();
        e[4].binding = 4;
        e[4].buffer = sprites_;
        e[4].size = sprites_.GetSize();
        e[5].binding = 5;
        e[5].buffer = counters_;
        e[5].size = 16;
        e[6].binding = 6;
        e[6].buffer = frameBuffer_;
        e[6].size = sizeof(EcoFrameGpu);
        e[7].binding = 7;
        e[7].buffer = spriteArgsBuffer_;
        e[7].size = 16;
        e[8].binding = 15;
        e[8].buffer = fields.gridBuffer();
        e[8].size = fields.gridBuffer().GetSize();
        wgpu::BindGroupDescriptor bd{};
        bd.label = "ecosystem-compute-0";
        bd.layout = computeLayout0_;
        bd.entryCount = e.size();
        bd.entries = e.data();
        group0 = device.CreateBindGroup(&bd);
    }
    wgpu::BindGroup empty;
    {
        wgpu::BindGroupDescriptor bd{};
        bd.layout = emptyLayout_;
        empty = device.CreateBindGroup(&bd);
    }

    wgpu::ComputePassDescriptor cpd{};
    cpd.label = "ecosystem-emit";
    cpd.timestampWrites = timeline != nullptr ? timeline->mark("ecosystem.emit", gpu::FrameTimeline::PassKind::Compute)
                                              : nullptr;
    wgpu::ComputePassEncoder cp = encoder.BeginComputePass(&cpd);
    cp.SetPipeline(emit_);
    cp.SetBindGroup(0, group0);
    cp.SetBindGroup(1, empty);
    for (std::size_t i = 0; i < eco.layers.size(); ++i) {
        const scene::EmitterLayer& layer = eco.layers[i];
        if (!layer.enabled || !layer.points || layer.points->empty()) {
            continue;
        }
        syncLayer(i, scene, layer);
        LayerGpu& g = layers_[i];
        if (g.hostCount == 0 || g.pointCount == 0) {
            continue;
        }
        LayerUniformGpu u{};
        u.color = glm::vec4(layer.color, layer.intensity);
        u.excited = glm::vec4(layer.excitedColor, layer.excitedIntensity);
        u.response = glm::vec4(layer.responseGain, layer.responseThreshold, layer.travel, layer.size);
        u.rest = glm::vec4(layer.breath, layer.breathRate, layer.flicker, layer.flickerRate);
        u.pulse = glm::vec4(layer.pulseRate, layer.pulseDecay, layer.sparsity, layer.maxDistance);
        u.slots = glm::ivec4(layer.responseField.empty() ? -1 : fields.slotOf(layer.responseField),
                             layer.lagField.empty() ? -1 : fields.slotOf(layer.lagField),
                             static_cast<int>(g.pointCount), static_cast<int>(g.hostCount));
        const int wakeSlot = layer.wakeField.empty() ? -1 : fields.slotOf(layer.wakeField);
        u.bound = glm::vec4(g.boundRadius + layer.bob, static_cast<float>(wakeSlot), layer.wakeGain, layer.nearFade);
        u.optics = glm::vec4(layer.iridescence, layer.iridescenceScale, layer.iridescenceSpeed, layer.bob);
        u.motion = glm::vec4(layer.bobRate, 0.0f, 0.0f, 0.0f);
        context_->queue().WriteBuffer(g.uniforms, 0, &u, sizeof(u));
        cp.SetBindGroup(2, g.group);
        const std::uint32_t wx = (g.pointCount + 63u) / 64u;
        const std::uint32_t wy = std::min<std::uint32_t>(g.hostCount, 65535u);
        const std::uint32_t wz = (g.hostCount + 65534u) / 65535u;
        cp.DispatchWorkgroups(wx, wy, wz);
        ++stats_.layers;
        ++stats_.dispatches;
        stats_.hosts += g.hostCount;
        stats_.candidates += static_cast<std::uint64_t>(g.hostCount) * g.pointCount;
    }
    cp.SetPipeline(spriteArgs_);
    cp.DispatchWorkgroups(1);
    cp.End();

    // Resolve the accumulation, then the sprites.
    std::array<wgpu::RenderPassColorAttachment, 2> colour{};
    for (auto& c : colour) {
        c.loadOp = wgpu::LoadOp::Load;
        c.storeOp = wgpu::StoreOp::Store;
        c.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    }
    colour[0].view = hdr;
    colour[1].view = emission;
    {
        std::array<wgpu::BindGroupEntry, 2> e{};
        e[0].binding = 6;
        e[0].buffer = frameBuffer_;
        e[0].size = sizeof(EcoFrameGpu);
        e[1].binding = 8;
        e[1].buffer = accum_;
        e[1].size = accum_.GetSize();
        wgpu::BindGroupDescriptor bd{};
        bd.layout = resolveLayout_;
        bd.entryCount = e.size();
        bd.entries = e.data();
        const wgpu::BindGroup group = device.CreateBindGroup(&bd);
        wgpu::RenderPassDescriptor rp{};
        rp.label = "ecosystem-resolve";
        rp.timestampWrites = timeline != nullptr ? timeline->mark("ecosystem.resolve", gpu::FrameTimeline::PassKind::Render)
                                                 : nullptr;
        rp.colorAttachmentCount = colour.size();
        rp.colorAttachments = colour.data();
        wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&rp);
        pass.SetPipeline(resolve_);
        pass.SetBindGroup(0, group);
        pass.Draw(3);
        pass.End();
    }
    {
        std::array<wgpu::BindGroupEntry, 2> e{};
        e[0].binding = 0;
        e[0].buffer = frameUniforms;
        e[0].size = frameUniforms.GetSize();
        e[1].binding = 9;
        e[1].buffer = sprites_;
        e[1].size = sprites_.GetSize();
        wgpu::BindGroupDescriptor bd{};
        bd.layout = spriteLayout_;
        bd.entryCount = e.size();
        bd.entries = e.data();
        const wgpu::BindGroup group = device.CreateBindGroup(&bd);
        wgpu::RenderPassDepthStencilAttachment ds{};
        ds.view = depth;
        ds.depthLoadOp = wgpu::LoadOp::Load;
        ds.depthStoreOp = wgpu::StoreOp::Store;
        ds.depthReadOnly = false;
        wgpu::RenderPassDescriptor rp{};
        rp.label = "ecosystem-sprites";
        rp.timestampWrites = timeline != nullptr ? timeline->mark("ecosystem.sprites", gpu::FrameTimeline::PassKind::Render)
                                                 : nullptr;
        rp.colorAttachmentCount = colour.size();
        rp.colorAttachments = colour.data();
        rp.depthStencilAttachment = &ds;
        wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&rp);
        pass.SetPipeline(sprite_);
        pass.SetBindGroup(0, group);
        pass.DrawIndirect(spriteArgsBuffer_, 0);
        pass.End();
    }
}

} // namespace avgen::rendering
