// The height fog layer's antiderivative is its antiderivative (ADR-567).
//
// **What the claim is.** ADR-058 made the surface fog integrate the SAME flat-topped layer the
// volumetric march multiplies its density by -- "a scene whose fog bank has one height in the
// march and another on the surfaces inside it does not read as one atmosphere". The march
// evaluates `fogHeightProfile`; the surface pass replaces its geometric distance with
// `(F(y1) - F(y0)) / (y1 - y0)` times its length, where F is `fogHeightIntegral`. That is only
// the distance through the layer if **F really is the antiderivative of the profile**.
//
// **Nothing checked it.** `test_volume_gpu.cpp` checks the sign (a surface standing clear of the
// layer is fogged less than one buried in it) and the degenerate case (a ray entirely inside the
// layer is bit-identical with the integration on or off). Both pass against any monotone F. The
// relationship itself -- the one thing that makes the two passes describe one atmosphere -- was a
// claim with no test on it, which is the same hole ADR-566 found in the march's ray bound and
// which `docs/testing.md` 24 is the general form of.
//
// **Why it runs on the GPU when the maths is trivial.** Because the maths is not what could be
// wrong. What could be wrong is that the *shader* the frame runs disagrees with the model anyone
// reasoned about -- a lost `max`, a falloff read from the wrong slot, an edit to one of the two
// expressions and not the other. So the numbers come from the shipped WGSL, compiled from the
// file the renderer loads, and the numerical integral is taken of the shader's OWN profile
// function rather than of a restatement of it in C++.
//
// **How it fails.** Drop the `max(0.0, y)` from `fogHeightProfile` in `shaders/height_fog.wgsl`
// (which makes the layer's interior grow exponentially downward instead of being uniform) and the
// comparison fails on every case whose ray starts below the layer's top. Change the `1/b` in
// `fogHeightIntegral` to `1/(b + 0.01)` and it fails on the shallow-falloff cases.

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"

#include <glm/glm.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <memory>
#include <utility>
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

// Each invocation takes one (y0, y1, falloff) and returns, in one vec4:
//   x  the closed form the SURFACE pass uses: F(y1) - F(y0)
//   y  the same quantity integrated numerically from the MARCH's own profile function
//   z  the profile at y0, so the case can assert it sampled somewhere the layer exists
//   w  the profile at y1
//
// The numerical integral uses the midpoint rule over 4096 slices. The profile is C1 and monotone,
// so midpoint converges as h^2 and 4096 slices over at most 400 m leaves an error far below the
// 1e-3 the comparison allows -- which is checked by the self-consistency case below rather than
// asserted here.
constexpr const char* kKernel = R"(
struct Args { count: vec4<f32> };
@group(0) @binding(0) var<uniform> args: Args;
@group(0) @binding(1) var<storage, read> cases: array<vec4<f32>>;
@group(0) @binding(2) var<storage, read_write> results: array<vec4<f32>>;

@compute @workgroup_size(64)
fn cs_check(@builtin(global_invocation_id) gid: vec3<u32>) {
    let i = gid.x;
    if (i >= arrayLength(&cases)) { return; }
    let y0 = cases[i].x;
    let y1 = cases[i].y;
    let b  = cases[i].z;

    let closed = fogHeightIntegral(y1, b) - fogHeightIntegral(y0, b);

    let slices = 4096;
    let h = (y1 - y0) / f32(slices);
    var sum = 0.0;
    for (var k = 0; k < slices; k = k + 1) {
        let y = y0 + (f32(k) + 0.5) * h;
        sum = sum + fogHeightProfile(y, b) * h;
    }
    results[i] = vec4<f32>(closed, sum, fogHeightProfile(y0, b), fogHeightProfile(y1, b));
}
)";

struct Row {
    float closed = 0.0f;
    float numeric = 0.0f;
    float profile0 = 0.0f;
    float profile1 = 0.0f;
};

std::vector<Row> run(gpu::Context& ctx, const std::vector<glm::vec4>& cases) {
    gpu::ShaderLibrary shaders(ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    // The file the renderer loads, not a copy of it. `height_fog.wgsl` declares no bindings
    // precisely so this line needs nothing else (ADR-567).
    auto src = shaders.loadSource("height_fog.wgsl");
    REQUIRE(src.has_value());
    auto module = shaders.compile(*src + kKernel, "height-fog-check");
    REQUIRE(module.has_value());

    const auto& device = ctx.device();
    std::array<wgpu::BindGroupLayoutEntry, 3> entries{};
    entries[0].binding = 0;
    entries[0].visibility = wgpu::ShaderStage::Compute;
    entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
    entries[1].binding = 1;
    entries[1].visibility = wgpu::ShaderStage::Compute;
    entries[1].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
    entries[2].binding = 2;
    entries[2].visibility = wgpu::ShaderStage::Compute;
    entries[2].buffer.type = wgpu::BufferBindingType::Storage;
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
    cdesc.compute.entryPoint = "cs_check";
    wgpu::ComputePipeline pipeline = device.CreateComputePipeline(&cdesc);
    REQUIRE(pipeline != nullptr);

    const glm::vec4 args(static_cast<float>(cases.size()), 0.0f, 0.0f, 0.0f);
    wgpu::BufferDescriptor adesc{};
    adesc.size = sizeof(glm::vec4);
    adesc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
    wgpu::Buffer argBuf = device.CreateBuffer(&adesc);
    ctx.queue().WriteBuffer(argBuf, 0, &args, sizeof(glm::vec4));

    wgpu::BufferDescriptor sdesc{};
    sdesc.size = cases.size() * sizeof(glm::vec4);
    sdesc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
    wgpu::Buffer in = device.CreateBuffer(&sdesc);
    ctx.queue().WriteBuffer(in, 0, cases.data(), sdesc.size);

    wgpu::BufferDescriptor rdesc{};
    rdesc.size = sdesc.size;
    rdesc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopySrc;
    wgpu::Buffer out = device.CreateBuffer(&rdesc);

    std::array<wgpu::BindGroupEntry, 3> bind{};
    bind[0].binding = 0;
    bind[0].buffer = argBuf;
    bind[0].size = adesc.size;
    bind[1].binding = 1;
    bind[1].buffer = in;
    bind[1].size = sdesc.size;
    bind[2].binding = 2;
    bind[2].buffer = out;
    bind[2].size = rdesc.size;
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
    std::vector<glm::vec4> raw(cases.size());
    std::memcpy(raw.data(), bytes->data(), rdesc.size);
    std::vector<Row> rows(cases.size());
    for (std::size_t i = 0; i < cases.size(); ++i) {
        rows[i] = Row{raw[i].x, raw[i].y, raw[i].z, raw[i].w};
    }
    return rows;
}

} // namespace

TEST_CASE("the surface fog integrates the layer the march marches", "[gpu][fog][height]") {
    auto ctx = makeContext();

    // Spans chosen to cross the join in both directions and to sit wholly on each side of it,
    // because the profile is piecewise and the seam at y = 0 is where an integral goes wrong.
    // Falloffs across the shipped range: `fogHeightFalloff` defaults to 0.17 in `tree_scene.hpp`
    // and the scenes in the repository use 0.004 to 0.6.
    std::vector<glm::vec4> cases;
    for (const float b : {0.004f, 0.05f, 0.17f, 0.25f, 0.6f, 2.0f}) {
        for (const auto [y0, y1] : std::initializer_list<std::pair<float, float>>{
                 {-120.0f, -10.0f},  // wholly inside the layer
                 {-80.0f, 40.0f},    // climbing out through the top
                 {60.0f, 180.0f},    // wholly above it
                 {-5.0f, 5.0f},      // straddling the seam closely
                 {200.0f, -200.0f},  // descending, so the sign of the span is exercised
                 {0.0f, 260.0f}}) {  // starting exactly on the join
            cases.emplace_back(y0, y1, b, 0.0f);
        }
    }

    const std::vector<Row> rows = run(*ctx, cases);
    REQUIRE(rows.size() == cases.size());

    int crossedTheSeam = 0;
    int sawFullDensity = 0;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const glm::vec4& c = cases[i];
        INFO("y0=" << c.x << " y1=" << c.y << " falloff=" << c.z
             << " closed=" << rows[i].closed << " numeric=" << rows[i].numeric);
        // The absolute tolerance is in METRES of full-density air. A span of 400 m integrated with
        // 4096 midpoint slices carries far less error than this; what it is really guarding
        // against is a shader that computes a different function, which misses by tens of metres.
        CHECK(rows[i].closed == Approx(rows[i].numeric).margin(0.02));
        if ((c.x < 0.0f) != (c.y < 0.0f)) {
            ++crossedTheSeam;
        }
        if (rows[i].profile0 > 0.99f || rows[i].profile1 > 0.99f) {
            ++sawFullDensity;
        }
    }
    // The controls. A case set that never crosses the join tests only one branch of a piecewise
    // function, and one that never reaches the uniform interior tests only the exponential -- and
    // both would pass against a shader that had lost the `max`.
    CHECK(crossedTheSeam >= 12);
    CHECK(sawFullDensity >= 12);
}

TEST_CASE("the numerical integral in the case above is fine enough to be evidence",
          "[gpu][fog][height]") {
    // The control on the instrument rather than on the thing measured. If 4096 midpoint slices
    // were too coarse for these spans, the case above would be comparing the closed form against a
    // number with its own error and calling the agreement a result. Halving the span halves the
    // step, so the midpoint rule's error should fall by about four -- and here it should already
    // be so far below the tolerance that both halves agree with the closed form to well inside it.
    auto ctx = makeContext();
    const std::vector<glm::vec4> cases{{-200.0f, 200.0f, 0.17f, 0.0f},
                                       {-200.0f, 0.0f, 0.17f, 0.0f},
                                       {0.0f, 200.0f, 0.17f, 0.0f}};
    const std::vector<Row> rows = run(*ctx, cases);
    REQUIRE(rows.size() == 3);
    // The whole span is the sum of its two halves, for the closed form and for the numeric one.
    INFO("whole " << rows[0].numeric << " halves " << rows[1].numeric << " + " << rows[2].numeric);
    CHECK(rows[0].numeric == Approx(rows[1].numeric + rows[2].numeric).margin(0.001));
    CHECK(rows[0].closed == Approx(rows[1].closed + rows[2].closed).margin(0.001));
    for (const Row& r : rows) {
        CHECK(r.closed == Approx(r.numeric).margin(0.001));
    }
}
