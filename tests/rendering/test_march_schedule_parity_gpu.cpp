// ADR-710: `shaders/march_schedule.wgsl` and `src/world/march_schedule.cpp` compute the same
// schedule. The CPU side is what `test_march_schedule.cpp` asserts the contract on; this is what
// makes that assertion about the shader the frame actually runs (ADR-565's rule: a CPU/GPU pair
// with no parity test is two implementations with a convention).

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "world/march_schedule.hpp"

#include <glm/vec4.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstring>
#include <filesystem>
#include <memory>
#include <vector>

using Catch::Approx;
using namespace avgen;

namespace {

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

// One case per invocation: four vec4 of intervals and (maxDistance, steps, ambient, 0) in; the
// segment count and seventeen (start, step, count, 0) out.
constexpr const char* kKernel = R"(
struct Case {
    a: vec4<f32>,
    b: vec4<f32>,
    c: vec4<f32>,
    d: vec4<f32>,
    e: vec4<f32>,
};
@group(0) @binding(0) var<storage, read> cases: array<Case>;
@group(0) @binding(1) var<storage, read_write> results: array<vec4<f32>>;

@compute @workgroup_size(64)
fn cs_schedule(@builtin(global_invocation_id) gid: vec3<u32>) {
    let i = gid.x;
    if (i >= arrayLength(&cases)) { return; }
    let k = cases[i];
    var iv: array<vec2<f32>, 8>;
    iv[0] = k.a.xy;
    iv[1] = k.a.zw;
    iv[2] = k.b.xy;
    iv[3] = k.b.zw;
    iv[4] = k.c.xy;
    iv[5] = k.c.zw;
    iv[6] = k.d.xy;
    iv[7] = k.d.zw;
    let s = marchSchedule(iv, k.e.x, i32(k.e.y), k.e.z > 0.5);
    let base = i * 18u;
    results[base] = vec4<f32>(f32(s.segments), 0.0, 0.0, 0.0);
    for (var g = 0u; g < 17u; g = g + 1u) {
        if (g < s.segments) {
            results[base + 1u + g] = vec4<f32>(s.start[g], s.step[g], f32(s.count[g]), 0.0);
        } else {
            results[base + 1u + g] = vec4<f32>(0.0);
        }
    }
}
)";

constexpr std::size_t kStride = 1 + world::kMarchMaxSegments;

struct Case {
    std::array<glm::vec2, world::kMarchMaxIntervals> intervals;
    float maxDistance;
    int steps;
    bool ambient;
};

std::vector<glm::vec4> runOnGpu(gpu::Context& ctx, const std::vector<Case>& cases) {
    gpu::ShaderLibrary shaders(ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    auto source = shaders.loadSource("march_schedule.wgsl");
    REQUIRE(source.has_value());
    auto module = shaders.compile(*source + kKernel, "march-schedule-parity");
    REQUIRE(module.has_value());
    const auto& device = ctx.device();

    std::array<wgpu::BindGroupLayoutEntry, 2> entries{};
    entries[0].binding = 0;
    entries[0].visibility = wgpu::ShaderStage::Compute;
    entries[0].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
    entries[1].binding = 1;
    entries[1].visibility = wgpu::ShaderStage::Compute;
    entries[1].buffer.type = wgpu::BufferBindingType::Storage;
    wgpu::BindGroupLayoutDescriptor ldesc{};
    ldesc.entryCount = entries.size();
    ldesc.entries = entries.data();
    wgpu::BindGroupLayout layout = device.CreateBindGroupLayout(&ldesc);
    wgpu::PipelineLayoutDescriptor pdesc{};
    pdesc.bindGroupLayoutCount = 1;
    pdesc.bindGroupLayouts = &layout;
    wgpu::ComputePipelineDescriptor cdesc{};
    cdesc.layout = device.CreatePipelineLayout(&pdesc);
    cdesc.compute.module = *module;
    cdesc.compute.entryPoint = "cs_schedule";
    wgpu::ComputePipeline pipeline = device.CreateComputePipeline(&cdesc);
    REQUIRE(pipeline != nullptr);

    std::vector<glm::vec4> packed;
    for (const Case& c : cases) {
        for (std::size_t k = 0; k < world::kMarchMaxIntervals; k += 2) {
            packed.emplace_back(c.intervals[k].x, c.intervals[k].y, c.intervals[k + 1].x, c.intervals[k + 1].y);
        }
        packed.emplace_back(c.maxDistance, static_cast<float>(c.steps), c.ambient ? 1.0f : 0.0f, 0.0f);
    }
    wgpu::BufferDescriptor sdesc{};
    sdesc.size = packed.size() * sizeof(glm::vec4);
    sdesc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
    wgpu::Buffer in = device.CreateBuffer(&sdesc);
    ctx.queue().WriteBuffer(in, 0, packed.data(), sdesc.size);
    wgpu::BufferDescriptor rdesc{};
    rdesc.size = cases.size() * kStride * sizeof(glm::vec4);
    rdesc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopySrc;
    wgpu::Buffer out = device.CreateBuffer(&rdesc);

    std::array<wgpu::BindGroupEntry, 2> bind{};
    bind[0].binding = 0;
    bind[0].buffer = in;
    bind[0].size = sdesc.size;
    bind[1].binding = 1;
    bind[1].buffer = out;
    bind[1].size = rdesc.size;
    wgpu::BindGroupDescriptor gdesc{};
    gdesc.layout = layout;
    gdesc.entryCount = bind.size();
    gdesc.entries = bind.data();
    wgpu::BindGroup group = device.CreateBindGroup(&gdesc);

    wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
    wgpu::ComputePassEncoder pass = encoder.BeginComputePass();
    pass.SetPipeline(pipeline);
    pass.SetBindGroup(0, group);
    pass.DispatchWorkgroups(static_cast<std::uint32_t>((cases.size() + 63) / 64));
    pass.End();
    wgpu::CommandBuffer commands = encoder.Finish();
    ctx.queue().Submit(1, &commands);
    auto bytes = gpu::readBuffer(ctx, out, 0, rdesc.size);
    REQUIRE(bytes.has_value());
    std::vector<glm::vec4> raw(cases.size() * kStride);
    std::memcpy(raw.data(), bytes->data(), rdesc.size);
    return raw;
}

} // namespace

TEST_CASE("the march's sample schedule is the same on the GPU as on the CPU", "[gpu][volume][parity]") {
    auto ctx = makeContext();
    const glm::vec2 none(1.0f, -1.0f);
    const auto iv = [&](glm::vec2 a, glm::vec2 b = glm::vec2(1.0f, -1.0f), glm::vec2 c = glm::vec2(1.0f, -1.0f),
                        glm::vec2 d = glm::vec2(1.0f, -1.0f)) {
        return std::array<glm::vec2, world::kMarchMaxIntervals>{a, b, c, d, none, none, none, none};
    };
    std::vector<Case> cases = {
        {iv(none), 320.0f, 12, true},
        {iv(none), 4000.0f, 32, false},
        {iv(glm::vec2(3400.0f, 3800.0f)), 4000.0f, 32, false},
        {iv(glm::vec2(3400.0f, 3800.0f)), 4000.0f, 32, true},
        {iv(glm::vec2(100.0f, 300.0f), glm::vec2(250.0f, 500.0f)), 4000.0f, 32, false},
        {iv(glm::vec2(1000.0f, 1300.0f), glm::vec2(100.0f, 200.0f)), 4000.0f, 32, true},
        {iv(glm::vec2(2000.0f, 2100.0f), glm::vec2(500.0f, 600.0f), glm::vec2(3000.0f, 3100.0f),
            glm::vec2(1000.0f, 1100.0f)),
         4000.0f, 32, true},
        {{glm::vec2(3700.0f, 3800.0f), glm::vec2(250.0f, 350.0f), glm::vec2(2350.0f, 2450.0f), glm::vec2(700.0f, 800.0f),
          glm::vec2(3250.0f, 3350.0f), glm::vec2(1150.0f, 1250.0f), glm::vec2(1600.0f, 1700.0f),
          glm::vec2(2800.0f, 2900.0f)},
         4000.0f, 32, true},
        {iv(glm::vec2(12.5f, 12.51f)), 187.3f, 48, true},
        {iv(glm::vec2(0.0f, 187.3f)), 187.3f, 48, false},
    };
    // A sweep of odd numbers, so a disagreement in the rounding of a gap's cell count shows.
    for (int i = 0; i < 40; ++i) {
        const float a = 37.0f * static_cast<float>(i) + 0.3f;
        cases.push_back({iv(glm::vec2(a, a + 11.0f + 3.1f * static_cast<float>(i)), glm::vec2(a + 400.0f, a + 470.0f)),
                         900.0f + 13.7f * static_cast<float>(i), 7 + i, (i % 2) == 0});
    }
    const std::vector<glm::vec4> gpu = runOnGpu(*ctx, cases);
    for (std::size_t i = 0; i < cases.size(); ++i) {
        const Case& c = cases[i];
        const world::MarchSchedule cpu = world::marchSchedule(c.intervals, c.maxDistance, c.steps, c.ambient);
        INFO("case " << i);
        REQUIRE(static_cast<std::uint32_t>(gpu[i * kStride].x) == cpu.segmentCount);
        for (std::uint32_t g = 0; g < cpu.segmentCount; ++g) {
            const glm::vec4 seg = gpu[i * kStride + 1 + g];
            INFO("segment " << g);
            CHECK(seg.x == Approx(cpu.segments[g].start).margin(1e-3));
            CHECK(seg.y == Approx(cpu.segments[g].step).epsilon(1e-5));
            CHECK(static_cast<int>(seg.z) == cpu.segments[g].count);
        }
    }
}
