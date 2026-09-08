#include "rendering/particle_renderer.hpp"

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace avgen::rendering {

namespace {
constexpr std::uint32_t kParticleStride = 48;
constexpr std::uint32_t kWorkgroup = 64;
}

ParticleRenderer::ParticleRenderer(gpu::Context& context, gpu::ShaderLibrary& shaders)
    : context_(context), shaders_(shaders) {}

Result<void> ParticleRenderer::init() {
    const auto& device = context_.device();
    {
        std::array<wgpu::BindGroupLayoutEntry, 6> entries{};
        entries[0].binding = 0;
        entries[0].visibility = wgpu::ShaderStage::Compute;
        entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
        for (std::uint32_t i = 1; i < 6; ++i) {
            entries[i].binding = i;
            entries[i].visibility = wgpu::ShaderStage::Compute;
            entries[i].buffer.type = wgpu::BufferBindingType::Storage;
        }
        wgpu::BindGroupLayoutDescriptor desc{};
        desc.label = "particles-compute-layout";
        desc.entryCount = entries.size();
        desc.entries = entries.data();
        computeLayout_ = device.CreateBindGroupLayout(&desc);
    }
    {
        std::array<wgpu::BindGroupLayoutEntry, 3> entries{};
        entries[0].binding = 0;
        entries[0].visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
        entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[1].binding = 1;
        entries[1].visibility = wgpu::ShaderStage::Vertex;
        entries[1].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
        entries[2].binding = 4;
        entries[2].visibility = wgpu::ShaderStage::Vertex;
        entries[2].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
        wgpu::BindGroupLayoutDescriptor desc{};
        desc.label = "particles-render-layout";
        desc.entryCount = entries.size();
        desc.entries = entries.data();
        renderLayout_ = device.CreateBindGroupLayout(&desc);
    }
    {
        wgpu::PipelineLayoutDescriptor desc{};
        desc.bindGroupLayoutCount = 1;
        desc.bindGroupLayouts = &computeLayout_;
        computePipelineLayout_ = device.CreatePipelineLayout(&desc);
        desc.bindGroupLayouts = &renderLayout_;
        renderPipelineLayout_ = device.CreatePipelineLayout(&desc);
    }
    auto module = shaders_.load("particles.wgsl");
    if (!module) {
        return std::unexpected(module.error());
    }
    if (auto r = createPipelines(*module); !r) {
        return r;
    }
    initialised_ = true;
    return {};
}

Result<void> ParticleRenderer::reload() {
    auto module = shaders_.load("particles.wgsl");
    if (!module) {
        return std::unexpected(module.error());
    }
    return createPipelines(*module);
}

Result<void> ParticleRenderer::createPipelines(const wgpu::ShaderModule& module) {
    const auto& device = context_.device();
    auto makeCompute = [&](const char* entry) -> Result<wgpu::ComputePipeline> {
        wgpu::ComputePipelineDescriptor desc{};
        desc.label = entry;
        desc.layout = computePipelineLayout_;
        desc.compute.module = module;
        desc.compute.entryPoint = entry;
        device.PushErrorScope(wgpu::ErrorFilter::Validation);
        wgpu::ComputePipeline pipeline = device.CreateComputePipeline(&desc);
        std::string error;
        auto future = device.PopErrorScope(
            wgpu::CallbackMode::WaitAnyOnly, [&](wgpu::PopErrorScopeStatus, wgpu::ErrorType type, wgpu::StringView msg) {
                if (type != wgpu::ErrorType::NoError) {
                    error = gpu::Context::toString(msg);
                }
            });
        context_.waitFor(future);
        if (!error.empty() || !pipeline) {
            return fail("particle compute pipeline '{}' failed: {}", entry, error);
        }
        return pipeline;
    };
    auto makeRender = [&](bool additive) -> Result<wgpu::RenderPipeline> {
        wgpu::BlendState blend{};
        blend.color.operation = wgpu::BlendOperation::Add;
        blend.color.srcFactor = additive ? wgpu::BlendFactor::One : wgpu::BlendFactor::SrcAlpha;
        blend.color.dstFactor = additive ? wgpu::BlendFactor::One : wgpu::BlendFactor::OneMinusSrcAlpha;
        blend.alpha.operation = wgpu::BlendOperation::Add;
        blend.alpha.srcFactor = wgpu::BlendFactor::One;
        blend.alpha.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
        wgpu::ColorTargetState colorTarget{};
        colorTarget.format = kHdrFormat;
        colorTarget.blend = &blend;
        colorTarget.writeMask = wgpu::ColorWriteMask::All;
        wgpu::FragmentState fragment{};
        fragment.module = module;
        fragment.entryPoint = "fs_particle";
        fragment.targetCount = 1;
        fragment.targets = &colorTarget;
        wgpu::DepthStencilState depth{};
        depth.format = kDepthFormat;
        depth.depthWriteEnabled = wgpu::OptionalBool::False;
        depth.depthCompare = wgpu::CompareFunction::Less;
        wgpu::RenderPipelineDescriptor desc{};
        desc.label = additive ? "particles-additive" : "particles-alpha";
        desc.layout = renderPipelineLayout_;
        desc.vertex.module = module;
        desc.vertex.entryPoint = "vs_particle";
        desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
        desc.primitive.cullMode = wgpu::CullMode::None;
        desc.depthStencil = &depth;
        desc.multisample.count = 1;
        desc.multisample.mask = 0xFFFFFFFFu;
        desc.fragment = &fragment;
        device.PushErrorScope(wgpu::ErrorFilter::Validation);
        wgpu::RenderPipeline pipeline = device.CreateRenderPipeline(&desc);
        std::string error;
        auto future = device.PopErrorScope(
            wgpu::CallbackMode::WaitAnyOnly, [&](wgpu::PopErrorScopeStatus, wgpu::ErrorType type, wgpu::StringView msg) {
                if (type != wgpu::ErrorType::NoError) {
                    error = gpu::Context::toString(msg);
                }
            });
        context_.waitFor(future);
        if (!error.empty() || !pipeline) {
            return fail("particle render pipeline failed: {}", error);
        }
        return pipeline;
    };
    auto reset = makeCompute("cs_reset");
    if (!reset) return std::unexpected(reset.error());
    auto emit = makeCompute("cs_emit");
    if (!emit) return std::unexpected(emit.error());
    auto simulate = makeCompute("cs_simulate");
    if (!simulate) return std::unexpected(simulate.error());
    auto additive = makeRender(true);
    if (!additive) return std::unexpected(additive.error());
    auto alpha = makeRender(false);
    if (!alpha) return std::unexpected(alpha.error());
    resetPipeline_ = *reset;
    emitPipeline_ = *emit;
    simulatePipeline_ = *simulate;
    additivePipeline_ = *additive;
    alphaPipeline_ = *alpha;
    return {};
}

void ParticleRenderer::ensurePool(std::size_t index, std::uint32_t capacity) {
    if (pools_.size() <= index) {
        pools_.resize(index + 1);
    }
    Pool& pool = pools_[index];
    capacity = std::clamp<std::uint32_t>(capacity, 64, 4u << 20);
    if (pool.capacity == capacity && pool.particles) {
        return;
    }
    const auto& device = context_.device();
    pool = Pool{};
    pool.capacity = capacity;
    auto buffer = [&](const char* label, std::uint64_t size, wgpu::BufferUsage usage) {
        wgpu::BufferDescriptor desc{};
        desc.label = label;
        desc.size = size;
        desc.usage = usage;
        return device.CreateBuffer(&desc);
    };
    pool.uniforms = buffer("particles-uniforms", sizeof(ParticleUniforms), wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst);
    pool.particles = buffer("particles-pool", static_cast<std::uint64_t>(capacity) * kParticleStride,
                            wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst);
    pool.deadList = buffer("particles-dead", static_cast<std::uint64_t>(capacity) * 4, wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst);
    pool.counters = buffer("particles-counters", 16, wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst);
    pool.aliveList = buffer("particles-alive", static_cast<std::uint64_t>(capacity) * 4, wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst);
    pool.indirect = buffer("particles-indirect", 16, wgpu::BufferUsage::Storage | wgpu::BufferUsage::Indirect | wgpu::BufferUsage::CopyDst);

    std::array<wgpu::BindGroupEntry, 6> entries{};
    const wgpu::Buffer* buffers[6] = {&pool.uniforms, &pool.particles, &pool.deadList, &pool.counters, &pool.aliveList, &pool.indirect};
    const std::uint64_t sizes[6] = {sizeof(ParticleUniforms), static_cast<std::uint64_t>(capacity) * kParticleStride,
                                    static_cast<std::uint64_t>(capacity) * 4, 16, static_cast<std::uint64_t>(capacity) * 4, 16};
    for (std::uint32_t i = 0; i < 6; ++i) {
        entries[i].binding = i;
        entries[i].buffer = *buffers[i];
        entries[i].size = sizes[i];
    }
    wgpu::BindGroupDescriptor cdesc{};
    cdesc.label = "particles-compute-group";
    cdesc.layout = computeLayout_;
    cdesc.entryCount = entries.size();
    cdesc.entries = entries.data();
    pool.computeGroup = device.CreateBindGroup(&cdesc);
    std::array<wgpu::BindGroupEntry, 3> rentries = {entries[0], entries[1], entries[4]};
    wgpu::BindGroupDescriptor rdesc{};
    rdesc.label = "particles-render-group";
    rdesc.layout = renderLayout_;
    rdesc.entryCount = rentries.size();
    rdesc.entries = rentries.data();
    pool.renderGroup = device.CreateBindGroup(&rdesc);
    pool.needsReset = true;
}

void ParticleRenderer::resetPool(Pool& pool) {
    std::vector<std::uint32_t> dead(pool.capacity);
    for (std::uint32_t i = 0; i < pool.capacity; ++i) {
        dead[i] = pool.capacity - 1 - i; // pop order: slot 0 first
    }
    context_.queue().WriteBuffer(pool.deadList, 0, dead.data(), dead.size() * 4);
    const std::uint32_t counters[4] = {pool.capacity, 0, 0, 0};
    context_.queue().WriteBuffer(pool.counters, 0, counters, sizeof(counters));
    std::vector<std::uint8_t> zeros(static_cast<std::size_t>(pool.capacity) * kParticleStride, 0);
    context_.queue().WriteBuffer(pool.particles, 0, zeros.data(), zeros.size());
    const std::uint32_t indirect[4] = {6, 0, 0, 0};
    context_.queue().WriteBuffer(pool.indirect, 0, indirect, sizeof(indirect));
    pool.emitCarry = 0.0;
    pool.needsReset = false;
}

void ParticleRenderer::resetAll() {
    for (auto& pool : pools_) {
        pool.needsReset = true;
    }
}

void ParticleRenderer::update(wgpu::CommandEncoder& encoder, const scene::Scene& scene, const FrameTime& time,
                              const glm::mat4& view, const glm::mat4& proj) {
    stats_ = ParticleStats{};
    if (!initialised_) {
        return;
    }
    const glm::mat4 invView = glm::inverse(view);
    const glm::vec3 right = glm::normalize(glm::vec3(invView[0]));
    const glm::vec3 up = glm::normalize(glm::vec3(invView[1]));
    for (std::size_t i = 0; i < scene.particles.size(); ++i) {
        const auto& sys = scene.particles[i];
        ensurePool(i, sys.capacity);
        Pool& pool = pools_[i];
        if (pool.needsReset) {
            resetPool(pool);
        }
        if (!sys.enabled) {
            continue;
        }
        const double dt = std::clamp(time.deltaTime, 0.0, 0.1);
        pool.emitCarry += static_cast<double>(sys.spawnRate) * dt;
        std::uint32_t emitCount = static_cast<std::uint32_t>(std::floor(pool.emitCarry));
        pool.emitCarry -= emitCount;
        emitCount += static_cast<std::uint32_t>(std::max(0.0f, sys.burst));
        emitCount = std::min(emitCount, pool.capacity);

        ParticleUniforms u{};
        u.viewProj = proj * view;
        u.cameraRight = glm::vec4(right, 0.0f);
        u.cameraUp = glm::vec4(up, 0.0f);
        u.emitterPos = glm::vec4(sys.position, static_cast<float>(sys.shape));
        u.extent = glm::vec4(sys.extent, sys.spread);
        u.direction = glm::vec4(sys.direction, sys.drag);
        u.speedLife = glm::vec4(sys.speedMin, sys.speedMax, sys.lifetimeMin, sys.lifetimeMax);
        u.gravity = glm::vec4(sys.gravity, sys.turbulence);
        u.turb = glm::vec4(sys.turbulenceScale, sys.turbulenceSpeed, sys.softness, sys.emissive);
        u.attractor = glm::vec4(sys.attractorPosition, sys.attractorStrength);
        u.attractor2 = glm::vec4(sys.attractorRadius, sys.orbit, sys.sizeStart, sys.sizeEnd);
        u.colorStart = sys.colorStart;
        u.colorEnd = sys.colorEnd;
        u.sim = glm::vec4(static_cast<float>(dt), static_cast<float>(time.renderTime),
                          static_cast<float>(time.frameIndex), static_cast<float>(sys.seed));
        u.counts = glm::uvec4(emitCount, pool.capacity, sys.blend == scene::ParticleBlend::Additive ? 0u : 1u, 0u);
        context_.queue().WriteBuffer(pool.uniforms, 0, &u, sizeof(u));

        wgpu::ComputePassDescriptor cdesc{};
        cdesc.label = "particles-compute";
        wgpu::ComputePassEncoder cp = encoder.BeginComputePass(&cdesc);
        cp.SetBindGroup(0, pool.computeGroup);
        cp.SetPipeline(resetPipeline_);
        cp.DispatchWorkgroups(1);
        if (emitCount > 0) {
            cp.SetPipeline(emitPipeline_);
            cp.DispatchWorkgroups((emitCount + kWorkgroup - 1) / kWorkgroup);
        }
        cp.SetPipeline(simulatePipeline_);
        cp.DispatchWorkgroups((pool.capacity + kWorkgroup - 1) / kWorkgroup);
        cp.End();

        ++stats_.systems;
        stats_.capacity += pool.capacity;
        stats_.emittedThisFrame += emitCount;
    }
}

void ParticleRenderer::draw(wgpu::RenderPassEncoder& pass, const scene::Scene& scene) {
    if (!initialised_) {
        return;
    }
    for (std::size_t i = 0; i < scene.particles.size() && i < pools_.size(); ++i) {
        const auto& sys = scene.particles[i];
        if (!sys.enabled || !pools_[i].particles) {
            continue;
        }
        pass.SetPipeline(sys.blend == scene::ParticleBlend::Additive ? additivePipeline_ : alphaPipeline_);
        pass.SetBindGroup(0, pools_[i].renderGroup);
        pass.DrawIndirect(pools_[i].indirect, 0);
    }
}

} // namespace avgen::rendering
