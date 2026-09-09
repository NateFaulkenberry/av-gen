#include "rendering/particle_renderer.hpp"

#include "rendering/field_uniforms.hpp"
#include "rendering/spline_buffers.hpp"

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/gpu_timer.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace avgen::rendering {

namespace {
constexpr std::uint32_t kParticleStride = 48;
constexpr std::uint32_t kWorkgroup = 64;   // cs_emit / cs_simulate
constexpr std::uint32_t kScanBlock = 1024; // slots per compaction workgroup (256 threads x 4)
// 0 uniforms, 1..7 storage, 8 field block, 9 spline tables, 15 the simulated-grid table
// (declared by fields.wgsl; ADR-032).
constexpr std::uint32_t kComputeBindings = 11;
constexpr std::uint32_t kComputeBindingSlots[kComputeBindings] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 15};
} // namespace

ParticleRenderer::ParticleRenderer(gpu::Context& context, gpu::ShaderLibrary& shaders)
    : context_(context), shaders_(shaders) {}

ParticleRenderer::~ParticleRenderer() = default;

void ParticleRenderer::collectTimings() {
    if (timer_) {
        const double ms = timer_->collect();
        if (ms >= 0.0) {
            lastSimulateMs_ = ms;
        }
    }
    stats_.simulateMs = passThisFrame_ ? lastSimulateMs_ : -1.0;
}

Result<void> ParticleRenderer::init(wgpu::Buffer fieldBlock, wgpu::Buffer splineTable, wgpu::Buffer gridTable) {
    const auto& device = context_.device();
    fieldBlock_ = std::move(fieldBlock);
    splineTable_ = std::move(splineTable);
    gridTable_ = std::move(gridTable);
    if (!gridTable_) {
        wgpu::BufferDescriptor desc{};
        desc.label = "particles-empty-grid-table";
        desc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
        desc.size = FieldUniforms::kGridBufferSize;
        gridTable_ = device.CreateBuffer(&desc);
    }
    if (!splineTable_) {
        wgpu::BufferDescriptor desc{};
        desc.label = "particles-empty-spline-table";
        desc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
        desc.size = SplineBuffers::kBufferSize;
        splineTable_ = device.CreateBuffer(&desc);
        const SplineInfoGpu zero{};
        context_.queue().WriteBuffer(splineTable_, 0, &zero, sizeof(zero));
    }
    if (!fieldBlock_) {
        wgpu::BufferDescriptor desc{};
        desc.label = "particles-empty-field-block";
        desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        desc.size = FieldUniforms::kBufferSize;
        fieldBlock_ = device.CreateBuffer(&desc);
        const FieldBlock zero{};
        context_.queue().WriteBuffer(fieldBlock_, 0, &zero, sizeof(zero));
    }
    {
        std::array<wgpu::BindGroupLayoutEntry, kComputeBindings> entries{};
        entries[0].binding = 0;
        entries[0].visibility = wgpu::ShaderStage::Compute;
        entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
        for (std::uint32_t i = 1; i < 8; ++i) {
            entries[i].binding = i;
            entries[i].visibility = wgpu::ShaderStage::Compute;
            entries[i].buffer.type = wgpu::BufferBindingType::Storage;
        }
        entries[8].binding = 8;
        entries[8].visibility = wgpu::ShaderStage::Compute;
        entries[8].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[8].buffer.minBindingSize = FieldUniforms::kBufferSize;
        entries[9].binding = 9;
        entries[9].visibility = wgpu::ShaderStage::Compute;
        entries[9].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
        entries[9].buffer.minBindingSize = SplineBuffers::kBufferSize;
        entries[10].binding = 15;
        entries[10].visibility = wgpu::ShaderStage::Compute;
        entries[10].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
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
    timer_ = std::make_unique<gpu::GpuTimer>(context_);
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
    auto emit = makeCompute("cs_emit");
    if (!emit) return std::unexpected(emit.error());
    auto simulate = makeCompute("cs_simulate");
    if (!simulate) return std::unexpected(simulate.error());
    auto reduce = makeCompute("cs_scan_reduce");
    if (!reduce) return std::unexpected(reduce.error());
    auto top = makeCompute("cs_scan_top");
    if (!top) return std::unexpected(top.error());
    auto scatter = makeCompute("cs_scan_scatter");
    if (!scatter) return std::unexpected(scatter.error());
    auto additive = makeRender(true);
    if (!additive) return std::unexpected(additive.error());
    auto alpha = makeRender(false);
    if (!alpha) return std::unexpected(alpha.error());
    emitPipeline_ = *emit;
    simulatePipeline_ = *simulate;
    scanReducePipeline_ = *reduce;
    scanTopPipeline_ = *top;
    scanScatterPipeline_ = *scatter;
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
    pool.blocks = (capacity + kScanBlock - 1) / kScanBlock;
    auto buffer = [&](const char* label, std::uint64_t size, wgpu::BufferUsage usage) {
        wgpu::BufferDescriptor desc{};
        desc.label = label;
        desc.size = size;
        desc.usage = usage;
        return device.CreateBuffer(&desc);
    };
    const std::uint64_t listBytes = static_cast<std::uint64_t>(capacity) * 4;
    const std::uint64_t blockBytes = static_cast<std::uint64_t>(pool.blocks) * 4;
    constexpr auto kStorage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
    pool.uniforms = buffer("particles-uniforms", sizeof(ParticleUniforms), wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst);
    pool.particles = buffer("particles-pool", static_cast<std::uint64_t>(capacity) * kParticleStride, kStorage);
    pool.deadList = buffer("particles-dead", listBytes, kStorage);
    pool.counters = buffer("particles-counters", 16, kStorage | wgpu::BufferUsage::CopySrc);
    pool.aliveList = buffer("particles-alive", listBytes, kStorage);
    pool.indirect = buffer("particles-indirect", 16, kStorage | wgpu::BufferUsage::Indirect);
    pool.flags = buffer("particles-flags", listBytes, kStorage);
    pool.blockSums = buffer("particles-block-sums", blockBytes, kStorage);

    std::array<wgpu::BindGroupEntry, kComputeBindings> entries{};
    const wgpu::Buffer* buffers[kComputeBindings] = {&pool.uniforms, &pool.particles, &pool.deadList, &pool.counters,
                                                     &pool.aliveList, &pool.indirect, &pool.flags, &pool.blockSums,
                                                     &fieldBlock_, &splineTable_, &gridTable_};
    const std::uint64_t sizes[kComputeBindings] = {sizeof(ParticleUniforms),
                                                   static_cast<std::uint64_t>(capacity) * kParticleStride,
                                                   listBytes, 16, listBytes, 16, listBytes, blockBytes,
                                                   FieldUniforms::kBufferSize, SplineBuffers::kBufferSize,
                                                   FieldUniforms::kGridBufferSize};
    for (std::uint32_t i = 0; i < kComputeBindings; ++i) {
        entries[i].binding = kComputeBindingSlots[i];
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
    // The same state the compaction pass would produce for an empty pool: every slot dead, the
    // dead list in slot order, no alive instances.
    std::vector<std::uint32_t> dead(pool.capacity);
    for (std::uint32_t i = 0; i < pool.capacity; ++i) {
        dead[i] = i;
    }
    context_.queue().WriteBuffer(pool.deadList, 0, dead.data(), dead.size() * 4);
    const std::uint32_t counters[4] = {pool.capacity, 0, 0, 0}; // deadCount, aliveCount
    context_.queue().WriteBuffer(pool.counters, 0, counters, sizeof(counters));
    std::vector<std::uint8_t> zeros(static_cast<std::size_t>(pool.capacity) * kParticleStride, 0);
    context_.queue().WriteBuffer(pool.particles, 0, zeros.data(), zeros.size());
    context_.queue().WriteBuffer(pool.flags, 0, zeros.data(), static_cast<std::size_t>(pool.capacity) * 4);
    context_.queue().WriteBuffer(pool.blockSums, 0, zeros.data(), static_cast<std::size_t>(pool.blocks) * 4);
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
                              const glm::mat4& view, const glm::mat4& proj, const FieldUniforms* fields,
                              const SplineBuffers* splines) {
    stats_ = ParticleStats{};
    passThisFrame_ = false;
    if (!initialised_) {
        return;
    }
    collectTimings(); // the previous frame's measurement (its command buffer was submitted by now)
    // The timestamps span every enabled system's compute pass: begin on the first, end on the last.
    std::size_t enabledSystems = 0;
    for (const auto& sys : scene.particles) {
        enabledSystems += sys.enabled ? 1 : 0;
    }
    std::size_t encoded = 0;
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

        // Spline emitters (ADR-026) need an uploaded spline; otherwise the shape falls back to Point.
        int splineSlot = -1;
        if (sys.shape == scene::EmitterShape::Spline && splines != nullptr) {
            splineSlot = splines->slotOf(sys.spline);
        }
        const scene::EmitterShape shape =
            sys.shape == scene::EmitterShape::Spline && splineSlot < 0 ? scene::EmitterShape::Point : sys.shape;

        ParticleUniforms u{};
        u.viewProj = proj * view;
        u.cameraRight = glm::vec4(right, 0.0f);
        u.cameraUp = glm::vec4(up, 0.0f);
        u.emitterPos = glm::vec4(sys.position, static_cast<float>(shape));
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
        u.counts = glm::uvec4(emitCount, pool.capacity, sys.blend == scene::ParticleBlend::Additive ? 0u : 1u,
                              pool.blocks);
        // Field forces (ADR-025): enabled entries bound to an uploaded field, in order.
        std::uint32_t forceCount = 0;
        if (fields != nullptr) {
            for (const auto& f : sys.fieldForces) {
                if (forceCount >= static_cast<std::uint32_t>(scene::kMaxFieldForces)) {
                    break;
                }
                if (!f.enabled) {
                    continue;
                }
                const int slot = fields->slotOf(f.field);
                if (slot < 0) {
                    continue;
                }
                u.fieldForces[forceCount * 2] = glm::vec4(static_cast<float>(f.mode), static_cast<float>(slot), f.strength,
                                                          std::clamp(f.mix, 0.0f, 1.0f));
                u.fieldForces[forceCount * 2 + 1] = glm::vec4(f.axis, 0.0f);
                ++forceCount;
            }
        }
        u.fieldInfo = glm::uvec4(forceCount, static_cast<std::uint32_t>(splineSlot + 1), 0u, 0u);
        context_.queue().WriteBuffer(pool.uniforms, 0, &u, sizeof(u));

        // Pass order (see particles.wgsl): emit -> simulate -> reduce -> top scan -> scatter.
        // Dispatches in one compute pass are ordered, so each reads the previous one's writes;
        // emit consumes last frame's dead list before scatter rewrites it.
        wgpu::ComputePassDescriptor cdesc{};
        cdesc.label = "particles-compute";
        wgpu::PassTimestampWrites writes{};
        if (const wgpu::PassTimestampWrites* both = timer_->passWrites(); both != nullptr) {
            writes.querySet = both->querySet;
            if (enabledSystems == 1) {
                writes = *both;
            } else if (encoded == 0) {
                writes.beginningOfPassWriteIndex = both->beginningOfPassWriteIndex;
            } else if (encoded + 1 == enabledSystems) {
                writes.endOfPassWriteIndex = both->endOfPassWriteIndex;
            }
            if (encoded == 0 || encoded + 1 == enabledSystems) {
                cdesc.timestampWrites = &writes;
            }
        }
        ++encoded;
        wgpu::ComputePassEncoder cp = encoder.BeginComputePass(&cdesc);
        cp.SetBindGroup(0, pool.computeGroup);
        if (emitCount > 0) {
            cp.SetPipeline(emitPipeline_);
            cp.DispatchWorkgroups((emitCount + kWorkgroup - 1) / kWorkgroup);
        }
        cp.SetPipeline(simulatePipeline_);
        cp.DispatchWorkgroups((pool.capacity + kWorkgroup - 1) / kWorkgroup);
        cp.SetPipeline(scanReducePipeline_);
        cp.DispatchWorkgroups(pool.blocks);
        cp.SetPipeline(scanTopPipeline_);
        cp.DispatchWorkgroups(1);
        cp.SetPipeline(scanScatterPipeline_);
        cp.DispatchWorkgroups(pool.blocks);
        cp.End();

        ++stats_.systems;
        stats_.capacity += pool.capacity;
        stats_.emittedThisFrame += emitCount;
    }
    if (encoded > 0 && timer_->available()) {
        timer_->resolve(encoder);
        passThisFrame_ = true;
        stats_.simulateMs = lastSimulateMs_;
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

Result<ParticleCounts> ParticleRenderer::readCounts(std::size_t systemIndex) {
    if (systemIndex >= pools_.size() || !pools_[systemIndex].counters) {
        return fail("particle system {} has no pool", systemIndex);
    }
    auto bytes = gpu::readBuffer(context_, pools_[systemIndex].counters, 0, 16);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    std::uint32_t words[4];
    std::memcpy(words, bytes->data(), sizeof(words));
    return ParticleCounts{words[1], words[0]};
}

} // namespace avgen::rendering
