// Material program interpreter probes (ADR-030/036), hidden behind `[.perf]` so they never run in
// CI.
//
//   avgen_render_tests "[.perf][material]"
//
// docs/performance.md priced the interpreter at ~1.6 ms per op over a full-screen surface at
// 2880x1800 and blamed the per-pixel storage read of the op records. These probes decided that by
// experiment, and the answer was neither the fetch nor the arithmetic: it was the size of the op
// loop's *body*, which sets the shader's register allocation and so its occupancy. See ADR-050.
//
// They are kept because the same question will be asked again. The crux case is a program of N
// cheap ops against one of N expensive ops: if they cost the same, the arithmetic is free and the
// cost is structural. The A/B case compiles two versions of the interpreter into one process,
// which is the only instrument that survives a shared machine.
//
// The harness is a compute dispatch of one invocation per pixel of a 2880x1800 frame running
// `evaluateMaterialProgram`, with the result stored under a condition that is never true so
// nothing can be folded away. It is the shading path's inner loop with the shading removed.

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/field_uniforms.hpp"
#include "rendering/material_programs.hpp"
#include "scene/material_program.hpp"
#include "spatial/field.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace avgen;
using scene::MaterialOp;
using scene::MaterialOpKind;
using scene::MaterialProgram;

namespace {

// 2880x1800 = 5.18 megapixels, the resolution every figure in docs/performance.md is quoted at.
constexpr std::uint32_t kPixels = 2880u * 1800u;
constexpr int kWarmup = 2;
constexpr int kReps = 9; // interleaved across variants, so this is 9 samples of each

std::unique_ptr<gpu::Context> makeContext() {
    static bool logInit = false;
    if (!logInit) {
        log::init(log::Level::Warn);
        logInit = true;
    }
    auto ctx = gpu::Context::create(gpu::ContextDesc{});
    if (!ctx) {
        SKIP("no GPU adapter available: " << ctx.error().message);
    }
    return std::move(*ctx);
}

// One invocation per pixel. The context varies with the invocation id so no lane sees the same
// inputs as its neighbour -- a program of noise ops must actually evaluate 5.18 M distinct noises.
// The store is guarded by a condition the compiler cannot prove false and the GPU never takes, so
// the dispatch costs the evaluation and almost no bandwidth.
constexpr const char* kKernel = R"(
@group(0) @binding(0) var<uniform> fieldBlock: FieldBlock;
@group(0) @binding(1) var<storage, read> materialPrograms: MaterialProgramBlock;
@group(0) @binding(2) var<uniform> probe: vec4<f32>;   // x = program slot, y = time
@group(0) @binding(3) var<storage, read_write> results: array<vec4<f32>>;

@compute @workgroup_size(64)
fn cs_probe(@builtin(global_invocation_id) gid: vec3<u32>) {
    let i = gid.y * 2880u + gid.x;
    let px = f32(gid.x);
    let py = f32(gid.y);
    var ctx = materialContextZero();
    ctx.worldPosition = vec3<f32>(px * 0.017, py * 0.013, (px + py) * 0.0031);
    ctx.localPosition = ctx.worldPosition * 0.5;
    ctx.normal = normalize(vec3<f32>(px * 0.001 - 1.4, 1.0, py * 0.001 - 0.9));
    ctx.uv = vec2<f32>(px * 0.000347, py * 0.000555);
    ctx.viewDirection = normalize(vec3<f32>(0.3, 0.5, 1.0));
    ctx.time = probe.y;
    ctx.depth = px * 0.01 + py * 0.01;
    ctx.curvature = px * 0.0004 - 0.5;
    ctx.cavity = py * 0.0005;
    ctx.occlusion = 1.0 - py * 0.0004;
    ctx.normalVariance = px * 0.0002;
    ctx.footprint = 0.01;

    var base = materialResultZero();
    base.baseColor = vec3<f32>(0.8, 0.7, 0.6);
    base.metallic = 0.25;
    base.roughness = 0.35;
    base.emission = vec3<f32>(0.1, 0.2, 0.3);
    base.opacity = 0.9;

    let r = evaluateMaterialProgram(i32(probe.x), ctx, base);
    let sum = vec4<f32>(r.baseColor, r.metallic) + vec4<f32>(r.emission, r.roughness) +
              vec4<f32>(r.opacity, r.occlusion, r.height, 0.0) + vec4<f32>(r.normal, 0.0);
    // Never true: every term above is finite. The compiler cannot know that, so the whole
    // evaluation stays live while the dispatch writes essentially nothing.
    if (sum.x + sum.y + sum.z + sum.w < -1.0e30) {
        results[i] = sum;
    }
}
)";

std::vector<std::filesystem::path> searchDirs(const std::filesystem::path& overrideDir) {
    std::vector<std::filesystem::path> dirs;
    if (!overrideDir.empty()) {
        dirs.push_back(overrideDir);
    }
    dirs.emplace_back(AVGEN_SHADER_SOURCE_DIR);
    return dirs;
}

class Probe {
public:
    // `overrideDir`, when non-empty, is searched before shaders/ -- so a material.wgsl dropped
    // there shadows the real one and two versions of the interpreter end up in one process. That
    // is the only way to A/B them: this machine drifts by more between runs than the change is
    // worth (docs/performance.md, "How much of a difference is a difference").
    explicit Probe(gpu::Context& ctx, const std::filesystem::path& overrideDir = {})
        : ctx_(ctx), shaders_(ctx, searchDirs(overrideDir)) {
        auto fields = shaders_.loadSource("fields.wgsl");
        REQUIRE(fields.has_value());
        auto material = shaders_.loadSource("material.wgsl");
        REQUIRE(material.has_value());
        auto module = shaders_.compile(*fields + "\n" + *material + kKernel, "material-perf-probe");
        if (!module) {
            FAIL(module.error().message);
        }
        const auto& device = ctx_.device();
        std::array<wgpu::BindGroupLayoutEntry, 5> entries{};
        for (std::uint32_t i = 0; i < 4; ++i) {
            entries[i].binding = i;
            entries[i].visibility = wgpu::ShaderStage::Compute;
        }
        entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[1].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
        entries[2].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[3].buffer.type = wgpu::BufferBindingType::Storage;
        entries[4].binding = 15; // fields.wgsl's shared grid table (ADR-032)
        entries[4].visibility = wgpu::ShaderStage::Compute;
        entries[4].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
        wgpu::BindGroupLayoutDescriptor ldesc{};
        ldesc.entryCount = entries.size();
        ldesc.entries = entries.data();
        layout_ = device.CreateBindGroupLayout(&ldesc);
        wgpu::PipelineLayoutDescriptor pdesc{};
        pdesc.bindGroupLayoutCount = 1;
        pdesc.bindGroupLayouts = &layout_;
        wgpu::ComputePipelineDescriptor cdesc{};
        cdesc.layout = device.CreatePipelineLayout(&pdesc);
        cdesc.compute.module = *module;
        cdesc.compute.entryPoint = "cs_probe";
        pipeline_ = device.CreateComputePipeline(&cdesc);
        REQUIRE(pipeline_ != nullptr);

        wgpu::BufferDescriptor rdesc{};
        rdesc.label = "material-perf-results";
        rdesc.size = static_cast<std::uint64_t>(kPixels) * sizeof(glm::vec4);
        rdesc.usage = wgpu::BufferUsage::Storage;
        results_ = device.CreateBuffer(&rdesc);
        wgpu::BufferDescriptor sdesc{};
        sdesc.label = "material-perf-select";
        sdesc.size = 16;
        sdesc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        select_ = device.CreateBuffer(&sdesc);
    }

    // Binds `programs` for later dispatches; call once before measuring.
    void bind(const std::vector<MaterialProgram>& programs) {
        fields_ = std::make_unique<rendering::FieldUniforms>(ctx_);
        fields_->update(spatial::FieldSet{}, 0.0);
        programs_ = std::make_unique<rendering::MaterialPrograms>(ctx_);
        programs_->update(programs, fields_.get());
        REQUIRE(programs_->count() == programs.size());
        rendering::FieldUniforms& fieldBlock = *fields_;
        rendering::MaterialPrograms& block = *programs_;

        std::array<wgpu::BindGroupEntry, 5> entries{};
        entries[0].binding = 0;
        entries[0].buffer = fieldBlock.buffer();
        entries[0].size = rendering::FieldUniforms::kBufferSize;
        entries[1].binding = 1;
        entries[1].buffer = block.buffer();
        entries[1].size = rendering::MaterialPrograms::kBufferSize;
        entries[2].binding = 2;
        entries[2].buffer = select_;
        entries[2].size = 16;
        entries[3].binding = 3;
        entries[3].buffer = results_;
        entries[3].size = static_cast<std::uint64_t>(kPixels) * sizeof(glm::vec4);
        entries[4].binding = 15;
        entries[4].buffer = fieldBlock.gridBuffer();
        entries[4].size = rendering::FieldUniforms::kGridBufferSize;
        wgpu::BindGroupDescriptor gdesc{};
        gdesc.layout = layout_;
        gdesc.entryCount = entries.size();
        gdesc.entries = entries.data();
        group_ = ctx_.device().CreateBindGroup(&gdesc);
    }

    double dispatch(int slot) { return dispatchOn(group_, slot); }

private:
    double dispatchOn(const wgpu::BindGroup& group, int slot) {
        const glm::vec4 probe(static_cast<float>(slot), 0.25f, 0.0f, 0.0f);
        ctx_.queue().WriteBuffer(select_, 0, &probe, sizeof(probe));
        wgpu::CommandEncoder encoder = ctx_.device().CreateCommandEncoder();
        wgpu::ComputePassEncoder pass = encoder.BeginComputePass();
        pass.SetPipeline(pipeline_);
        pass.SetBindGroup(0, group);
        pass.DispatchWorkgroups(2880u / 64u, 1800u);
        pass.End();
        wgpu::CommandBuffer commands = encoder.Finish();
        const auto start = std::chrono::steady_clock::now();
        ctx_.queue().Submit(1, &commands);
        ctx_.waitForQueue();
        const auto end = std::chrono::steady_clock::now();
        return std::chrono::duration<double, std::milli>(end - start).count();
    }

    gpu::Context& ctx_;
    gpu::ShaderLibrary shaders_;
    wgpu::BindGroupLayout layout_;
    wgpu::ComputePipeline pipeline_;
    wgpu::Buffer results_;
    wgpu::Buffer select_;
    wgpu::BindGroup group_;
    // The bind group holds these buffers, so their owners must outlive it.
    std::unique_ptr<rendering::FieldUniforms> fields_;
    std::unique_ptr<rendering::MaterialPrograms> programs_;
};

// A program of `count` copies of one op, each writing a different register and reading the two
// before it, so the ops form a dependency chain rather than eight independent ones.
MaterialProgram repeated(const std::string& name, MaterialOpKind kind, int count) {
    MaterialProgram p;
    p.name = name;
    for (int i = 0; i < count; ++i) {
        MaterialOp op;
        op.kind = kind;
        op.dst = i % 8;
        op.srcA = (i + 5) % 8;
        op.srcB = (i + 3) % 8;
        op.srcC = (i + 1) % 8;
        op.value = 0.37f + 0.01f * static_cast<float>(i);
        op.constant = glm::vec4(0.2f, 0.4f, 0.6f, 0.8f);
        op.constant2 = glm::vec4(0.3f);
        op.constant3 = glm::vec4(0.5f);
        op.constant4 = glm::vec4(0.7f);
        op.seed = static_cast<std::uint32_t>(i) + 11u;
        p.ops.push_back(op);
    }
    p.baseColorRegister = count > 0 ? (count - 1) % 8 : -1;
    p.roughnessRegister = count > 1 ? (count - 2) % 8 : -1;
    return p;
}

// The shape world::terrainMaterialProgram generates with mottling on: twelve ops, one Ramp pair,
// one fbm, over a full-screen surface. Rebuilt here rather than called so the probe does not
// depend on src/world.
MaterialProgram terrainShaped() {
    MaterialProgram p;
    p.name = "terrain-shaped";
    const auto add = [&p](MaterialOpKind kind, int dst, int srcA = 0, int srcB = 0, int srcC = 0) -> MaterialOp& {
        MaterialOp o;
        o.kind = kind;
        o.dst = dst;
        o.srcA = srcA;
        o.srcB = srcB;
        o.srcC = srcC;
        p.ops.push_back(o);
        return p.ops.back();
    };
    add(MaterialOpKind::Input, 0).input = scene::MaterialInput::Uv;
    add(MaterialOpKind::Swizzle, 1, 0).constant = glm::vec4(0.0f);
    add(MaterialOpKind::Swizzle, 2, 0).constant = glm::vec4(1.0f);
    for (int dst = 3; dst <= 4; ++dst) {
        MaterialOp& r = add(MaterialOpKind::Ramp, dst, 1);
        r.constant = glm::vec4(0.24f, 0.30f, 0.16f, 1.0f);
        r.constant2 = glm::vec4(0.36f, 0.34f, 0.22f, 1.0f);
        r.constant3 = glm::vec4(0.48f, 0.46f, 0.42f, 1.0f);
    }
    add(MaterialOpKind::Smoothstep, 5, 2).constant = glm::vec4(0.28f, 0.50f, 0.0f, 0.0f);
    add(MaterialOpKind::MixBy, 6, 3, 4, 5);
    add(MaterialOpKind::Input, 4).input = scene::MaterialInput::WorldPosition;
    MaterialOp& noise = add(MaterialOpKind::Noise, 7, 4);
    noise.value = 0.045f;
    noise.seed = 41;
    MaterialOp& remap = add(MaterialOpKind::Remap, 7, 7);
    remap.value = 1.0f;
    remap.constant = glm::vec4(0.0f, 1.0f, 0.80f, 1.20f);
    add(MaterialOpKind::Multiply, 6, 6, 7);
    add(MaterialOpKind::Constant, 7).constant = glm::vec4(0.72f);
    p.baseColorRegister = 6;
    p.roughnessRegister = 7;
    return p;
}

void report(const char* label, double ms, double emptyMs, int ops) {
    const double perOp = ops > 0 ? (ms - emptyMs) / static_cast<double>(ops) : 0.0;
    std::printf("  %-28s %8.3f ms   over empty %+7.3f ms   per op %6.3f ms\n", label, ms, ms - emptyMs, perOp);
    std::fflush(stdout);
}

} // namespace

// The crux: if 20 Constant ops cost what 20 Noise ops cost, the price is the fetch and the
// register file, not the arithmetic.
TEST_CASE("material program interpreter: cost per op is not arithmetic", "[.perf][material]") {
    auto ctx = makeContext();
    Probe probe(*ctx);

    std::vector<MaterialProgram> programs;
    programs.push_back(repeated("empty", MaterialOpKind::Constant, 0));
    programs.push_back(repeated("constant8", MaterialOpKind::Constant, 8));
    programs.push_back(repeated("constant20", MaterialOpKind::Constant, 20));
    programs.push_back(repeated("add20", MaterialOpKind::Add, 20));
    programs.push_back(repeated("mix20", MaterialOpKind::Mix, 20));
    programs.push_back(repeated("noise20", MaterialOpKind::Noise, 20));
    programs.push_back(repeated("constant40", MaterialOpKind::Constant, 40));
    probe.bind(programs);
    std::vector<std::vector<double>> samples(programs.size());
    for (int rep = 0; rep < kWarmup + kReps; ++rep) {
        for (std::size_t p = 0; p < programs.size(); ++p) {
            const double t = probe.dispatch(static_cast<int>(p));
            if (rep >= kWarmup) {
                samples[p].push_back(t);
            }
        }
    }
    std::vector<double> ms;
    for (auto& v : samples) {
        std::sort(v.begin(), v.end());
        ms.push_back(v[v.size() / 2]);
    }

    std::printf("\n5.18 M invocations (2880x1800), one evaluateMaterialProgram each, median of %d\n", kReps);
    const double empty = ms[0];
    std::printf("  %-28s %8.3f ms\n", "empty (0 ops)", empty);
    report("20 x Constant", ms[2], empty, 20);
    report("20 x Add", ms[3], empty, 20);
    report("20 x Mix", ms[4], empty, 20);
    report("20 x Noise (fbm3)", ms[5], empty, 20);
    report("8 x Constant", ms[1], empty, 8);
    report("40 x Constant", ms[6], empty, 40);
    std::printf("\n  Constant is one storage read and one register write; Noise adds an fbm3 on top.\n");
    std::printf("  noise20 / constant20 = %.2f\n\n", (ms[5] - empty) / std::max(ms[2] - empty, 1e-9));
    SUCCEED();
}

// ---- attribution: which overhead is it? ------------------------------------------------------

// Two synthetic loops with the same trip count as the interpreter's, each doing exactly one of the
// things the interpreter does per op and nothing else.
//
//   cs_fetch  reads one 112-byte MaterialOpGpu out of the storage buffer and adds it up. All
//             lanes in a wave read the same record, so this is the best case for the cache.
//   cs_regs   does one dynamically-indexed read of three vec4s and one dynamically-indexed write
//             into a local array<vec4<f32>, 8>, which no compiler can hold in registers. The
//             indices carry a runtime offset so the loop cannot be unrolled into static ones.
//
// The interpreter does both, per op, on top of the arithmetic.
constexpr const char* kSplitKernel = R"(
@group(0) @binding(0) var<uniform> fieldBlock: FieldBlock;
@group(0) @binding(1) var<storage, read> materialPrograms: MaterialProgramBlock;
@group(0) @binding(2) var<uniform> probe: vec4<f32>;   // x = slot, y = time, z = op count, w = 0
@group(0) @binding(3) var<storage, read_write> results: array<vec4<f32>>;

@compute @workgroup_size(64)
fn cs_fetch(@builtin(global_invocation_id) gid: vec3<u32>) {
    let n = u32(probe.z);
    let pi = u32(probe.x);
    var acc = vec4<f32>(f32(gid.x) * 1e-6, f32(gid.y) * 1e-6, 0.0, 0.0);
    for (var i = 0u; i < MAT_MAX_OPS; i = i + 1u) {
        if (i >= n) { break; }
        let op = materialPrograms.programs[pi].ops[i];
        acc = acc + vec4<f32>(op.value, f32(op.fieldOrdinal), op.pad0, op.pad1) + op.constant + op.constant2 + op.constant3 + op.constant4 +
              vec4<f32>(op.registers) +
              vec4<f32>(f32(op.kind), f32(op.input), f32(op.seed), f32(op.fieldSlot));
    }
    if (acc.x + acc.y + acc.z + acc.w < -1.0e30) { results[gid.y * 2880u + gid.x] = acc; }
}

@compute @workgroup_size(64)
fn cs_regs(@builtin(global_invocation_id) gid: vec3<u32>) {
    let n = u32(probe.z);
    let bias = u32(probe.w);   // runtime, so the index arithmetic cannot be constant-folded
    var regs: array<vec4<f32>, 8>;   // deliberately indexable: this is the mechanism under test
    for (var r = 0u; r < 8u; r = r + 1u) {
        regs[r] = vec4<f32>(f32(gid.x + r) * 1e-4, f32(gid.y) * 1e-4, 0.5, 1.0);
    }
    for (var i = 0u; i < MAT_MAX_OPS; i = i + 1u) {
        if (i >= n) { break; }
        let d = (i + bias) % 8u;
        let a = (i + bias + 5u) % 8u;
        let b = (i + bias + 3u) % 8u;
        let c = (i + bias + 1u) % 8u;
        regs[d] = regs[a] + regs[b] * regs[c];
    }
    var acc = vec4<f32>(0.0);
    for (var r = 0u; r < 8u; r = r + 1u) { acc = acc + regs[r]; }
    if (acc.x + acc.y + acc.z + acc.w < -1.0e30) { results[gid.y * 2880u + gid.x] = acc; }
}

// Fetch + the whole 31-branch materialEvalOp body (which reads the register file by value and by
// dynamic index), but without the interpreter's bounds checks and register write-back.
@compute @workgroup_size(64)
fn cs_body(@builtin(global_invocation_id) gid: vec3<u32>) {
    let n = u32(probe.z);
    let pi = u32(probe.x);
    var ctx = materialContextZero();
    ctx.worldPosition = vec3<f32>(f32(gid.x) * 0.017, f32(gid.y) * 0.013, 0.3);
    ctx.normal = normalize(vec3<f32>(f32(gid.x) * 0.001 - 1.4, 1.0, f32(gid.y) * 0.001 - 0.9));
    var regs: array<vec4<f32>, 8>;   // deliberately indexable: this is the mechanism under test
    for (var r = 0u; r < 8u; r = r + 1u) {
        regs[r] = vec4<f32>(f32(gid.x + r) * 1e-4, f32(gid.y) * 1e-4, 0.5, 1.0);
    }
    var acc = vec4<f32>(0.0);
    for (var i = 0u; i < MAT_MAX_OPS; i = i + 1u) {
        if (i >= n) { break; }
        acc = acc + materialEvalOp(pi, i, ctx,
                                   MatRegs(regs[0], regs[1], regs[2], regs[3], regs[4], regs[5], regs[6], regs[7]),
                                   MatFields(vec4<f32>(0.0), vec4<f32>(0.0), vec4<f32>(0.0), vec4<f32>(0.0)));
    }
    if (acc.x + acc.y + acc.z + acc.w < -1.0e30) { results[gid.y * 2880u + gid.x] = acc; }
}
)";

namespace {

// Runs one entry point of kSplitKernel at `ops` iterations, `kReps` interleaved samples, median.
class SplitProbe {
public:
    explicit SplitProbe(gpu::Context& ctx)
        : ctx_(ctx), shaders_(ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)}) {
        auto fields = shaders_.loadSource("fields.wgsl");
        REQUIRE(fields.has_value());
        auto material = shaders_.loadSource("material.wgsl");
        REQUIRE(material.has_value());
        auto module = shaders_.compile(*fields + "\n" + *material + kSplitKernel, "material-split-probe");
        if (!module) {
            FAIL(module.error().message);
        }
        const auto& device = ctx_.device();
        std::array<wgpu::BindGroupLayoutEntry, 5> entries{};
        for (std::uint32_t i = 0; i < 4; ++i) {
            entries[i].binding = i;
            entries[i].visibility = wgpu::ShaderStage::Compute;
        }
        entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[1].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
        entries[2].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[3].buffer.type = wgpu::BufferBindingType::Storage;
        entries[4].binding = 15;
        entries[4].visibility = wgpu::ShaderStage::Compute;
        entries[4].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
        wgpu::BindGroupLayoutDescriptor ldesc{};
        ldesc.entryCount = entries.size();
        ldesc.entries = entries.data();
        layout_ = device.CreateBindGroupLayout(&ldesc);
        wgpu::PipelineLayoutDescriptor pdesc{};
        pdesc.bindGroupLayoutCount = 1;
        pdesc.bindGroupLayouts = &layout_;
        wgpu::PipelineLayout pl = device.CreatePipelineLayout(&pdesc);
        for (const char* entry : {"cs_fetch", "cs_regs", "cs_body"}) {
            wgpu::ComputePipelineDescriptor cdesc{};
            cdesc.layout = pl;
            cdesc.compute.module = *module;
            cdesc.compute.entryPoint = entry;
            pipelines_.push_back(device.CreateComputePipeline(&cdesc));
            REQUIRE(pipelines_.back() != nullptr);
        }
        wgpu::BufferDescriptor rdesc{};
        rdesc.size = static_cast<std::uint64_t>(kPixels) * sizeof(glm::vec4);
        rdesc.usage = wgpu::BufferUsage::Storage;
        results_ = device.CreateBuffer(&rdesc);
        wgpu::BufferDescriptor sdesc{};
        sdesc.size = 16;
        sdesc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        select_ = device.CreateBuffer(&sdesc);

        fields_ = std::make_unique<rendering::FieldUniforms>(ctx_);
        fields_->update(spatial::FieldSet{}, 0.0);
        programs_ = std::make_unique<rendering::MaterialPrograms>(ctx_);
        programs_->update({repeated("filler", MaterialOpKind::Constant, 48)}, fields_.get());
        rendering::FieldUniforms& fieldBlock = *fields_;
        rendering::MaterialPrograms& block = *programs_;
        std::array<wgpu::BindGroupEntry, 5> ge{};
        ge[0].binding = 0;
        ge[0].buffer = fieldBlock.buffer();
        ge[0].size = rendering::FieldUniforms::kBufferSize;
        ge[1].binding = 1;
        ge[1].buffer = block.buffer();
        ge[1].size = rendering::MaterialPrograms::kBufferSize;
        ge[2].binding = 2;
        ge[2].buffer = select_;
        ge[2].size = 16;
        ge[3].binding = 3;
        ge[3].buffer = results_;
        ge[3].size = rdesc.size;
        ge[4].binding = 15;
        ge[4].buffer = fieldBlock.gridBuffer();
        ge[4].size = rendering::FieldUniforms::kGridBufferSize;
        wgpu::BindGroupDescriptor gdesc{};
        gdesc.layout = layout_;
        gdesc.entryCount = ge.size();
        gdesc.entries = ge.data();
        group_ = device.CreateBindGroup(&gdesc);
    }

    // `cases` is (pipeline index, op count); returns the median ms of each, interleaved.
    std::vector<double> measure(const std::vector<std::pair<int, int>>& cases) {
        std::vector<std::vector<double>> samples(cases.size());
        for (int rep = 0; rep < kWarmup + kReps; ++rep) {
            for (std::size_t c = 0; c < cases.size(); ++c) {
                const double ms = dispatch(cases[c].first, cases[c].second);
                if (rep >= kWarmup) {
                    samples[c].push_back(ms);
                }
            }
        }
        std::vector<double> out;
        for (auto& s : samples) {
            std::sort(s.begin(), s.end());
            out.push_back(s[s.size() / 2]);
        }
        return out;
    }

private:
    double dispatch(int pipeline, int ops) {
        const glm::vec4 probe(0.0f, 0.25f, static_cast<float>(ops), 0.0f);
        ctx_.queue().WriteBuffer(select_, 0, &probe, sizeof(probe));
        wgpu::CommandEncoder encoder = ctx_.device().CreateCommandEncoder();
        wgpu::ComputePassEncoder pass = encoder.BeginComputePass();
        pass.SetPipeline(pipelines_[static_cast<std::size_t>(pipeline)]);
        pass.SetBindGroup(0, group_);
        pass.DispatchWorkgroups(2880u / 64u, 1800u);
        pass.End();
        wgpu::CommandBuffer commands = encoder.Finish();
        const auto start = std::chrono::steady_clock::now();
        ctx_.queue().Submit(1, &commands);
        ctx_.waitForQueue();
        const auto end = std::chrono::steady_clock::now();
        return std::chrono::duration<double, std::milli>(end - start).count();
    }

    gpu::Context& ctx_;
    gpu::ShaderLibrary shaders_;
    wgpu::BindGroupLayout layout_;
    std::vector<wgpu::ComputePipeline> pipelines_;
    wgpu::Buffer results_;
    wgpu::Buffer select_;
    wgpu::BindGroup group_;
    // The bind group holds these buffers, so their owners must outlive it.
    std::unique_ptr<rendering::FieldUniforms> fields_;
    std::unique_ptr<rendering::MaterialPrograms> programs_;
};

} // namespace

TEST_CASE("material program interpreter: fetch versus register file", "[.perf][material]") {
    auto ctx = makeContext();
    SplitProbe probe(*ctx);
    const std::vector<double> ms = probe.measure({{0, 0}, {0, 20}, {1, 0}, {1, 20}, {2, 0}, {2, 20}});
    std::printf("\n5.18 M invocations, 20 iterations of one thing each\n");
    std::printf("  %-42s %8.3f ms   per op %6.3f ms\n", "112-byte storage op record fetch", ms[1] - ms[0],
                (ms[1] - ms[0]) / 20.0);
    std::printf("  %-42s %8.3f ms   per op %6.3f ms\n", "dynamically-indexed 8 x vec4 register file",
                ms[3] - ms[2], (ms[3] - ms[2]) / 20.0);
    std::printf("  %-42s %8.3f ms   per op %6.3f ms\n", "fetch + the whole materialEvalOp body",
                ms[5] - ms[4], (ms[5] - ms[4]) / 20.0);
    std::fflush(stdout);
    SUCCEED();
}

// The only honest before/after on this machine: two builds of the interpreter compiled into one
// process and dispatched alternately, so any drift or contention hits both equally.
//
//   AVGEN_MATERIAL_BASELINE=/dir/containing/an/old/material.wgsl \
//     avgen_render_tests "material program interpreter: A/B"
TEST_CASE("material program interpreter: A/B against a baseline shader", "[.perf][material]") {
    const char* baselinePath = std::getenv("AVGEN_MATERIAL_BASELINE");
    if (baselinePath == nullptr) {
        SKIP("set AVGEN_MATERIAL_BASELINE to a material.wgsl to compare against");
    }
    const std::filesystem::path baselineDir(baselinePath);
    REQUIRE(std::filesystem::exists(baselineDir / "material.wgsl"));

    auto ctx = makeContext();
    Probe before(*ctx, baselineDir);
    Probe after(*ctx);

    std::vector<MaterialProgram> programs;
    programs.push_back(repeated("empty", MaterialOpKind::Constant, 0));
    programs.push_back(repeated("constant20", MaterialOpKind::Constant, 20));
    programs.push_back(repeated("mix20", MaterialOpKind::Mix, 20));
    programs.push_back(repeated("noise20", MaterialOpKind::Noise, 20));
    programs.push_back(terrainShaped());
    before.bind(programs);
    after.bind(programs);

    const std::size_t n = programs.size();
    std::vector<std::vector<double>> b(n);
    std::vector<std::vector<double>> a(n);
    for (int rep = 0; rep < kWarmup + kReps; ++rep) {
        for (std::size_t p = 0; p < n; ++p) {
            // Alternate which side goes first: whichever runs second inherits the other's thermal
            // and cache state, and that bias is worth more than the change on a contended machine.
            double tb = 0.0;
            double ta = 0.0;
            if (rep % 2 == 0) {
                tb = before.dispatch(static_cast<int>(p));
                ta = after.dispatch(static_cast<int>(p));
            } else {
                ta = after.dispatch(static_cast<int>(p));
                tb = before.dispatch(static_cast<int>(p));
            }
            if (rep >= kWarmup) {
                b[p].push_back(tb);
                a[p].push_back(ta);
            }
        }
    }
    // The minimum, not the median: two other agents are building and benchmarking on this machine,
    // so every sample is the true cost plus contention that is never negative.
    const auto med = [](std::vector<double> v) { return *std::min_element(v.begin(), v.end()); };
    const char* names[] = {"empty (0 ops)", "20 x Constant", "20 x Mix", "20 x Noise", "terrain-shaped (12 ops)"};
    const int ops[] = {0, 20, 20, 20, 12};
    std::printf("\n5.18 M invocations, interleaved A/B, median of %d\n", kReps);
    std::printf("  %-26s %10s %10s %9s %9s\n", "", "before", "after", "saved", "per op");
    const double be = med(b[0]);
    const double ae = med(a[0]);
    for (std::size_t p = 0; p < n; ++p) {
        const double bm = med(b[p]);
        const double am = med(a[p]);
        std::printf("  %-26s %8.3f ms %8.3f ms %7.3f ms %7.3f -> %.3f\n", names[p], bm, am, bm - am,
                    ops[p] > 0 ? (bm - be) / ops[p] : 0.0, ops[p] > 0 ? (am - ae) / ops[p] : 0.0);
    }
    std::fflush(stdout);
    SUCCEED();
}
