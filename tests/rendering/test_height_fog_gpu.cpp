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
#include "world/terrain_height.hpp"

#include <glm/glm.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string_view>
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
// A case is TWO vec4s: (y0, y1, falloff, 0) then (upper, curve, 0, 0). Five numbers do not fit in
// one, and splitting them across two arrays would let the two get out of step.
constexpr const char* kKernel = R"(
struct Args { count: vec4<f32> };
@group(0) @binding(0) var<uniform> args: Args;
@group(0) @binding(1) var<storage, read> cases: array<vec4<f32>>;
@group(0) @binding(2) var<storage, read_write> results: array<vec4<f32>>;

@compute @workgroup_size(64)
fn cs_check(@builtin(global_invocation_id) gid: vec3<u32>) {
    let i = gid.x;
    if (i * 2u + 1u >= arrayLength(&cases)) { return; }
    let y0 = cases[i * 2u].x;
    let y1 = cases[i * 2u].y;
    let b  = cases[i * 2u].z;
    let upper = cases[i * 2u + 1u].x;
    let curve = cases[i * 2u + 1u].y;

    let closed = fogHeightIntegral(y1, b, upper, curve) - fogHeightIntegral(y0, b, upper, curve);

    let slices = 4096;
    let h = (y1 - y0) / f32(slices);
    var sum = 0.0;
    for (var k = 0; k < slices; k = k + 1) {
        let y = y0 + (f32(k) + 0.5) * h;
        sum = sum + fogHeightProfile(y, b, upper, curve) * h;
    }
    results[i * 2u] = vec4<f32>(closed, sum,
                                fogHeightProfile(y0, b, upper, curve),
                                fogHeightProfile(y1, b, upper, curve));
    // ADR-568's bit-identity claim, answered by the shader rather than argued about: at upper 0
    // and curve 0 the two functions must be EXACTLY the expressions they replaced.
    results[i * 2u + 1u] = vec4<f32>(fogHeightProfile(y1, b, 0.0, 0.0) - exp(-b * max(0.0, y1)),
                                     fogHeightIntegral(y1, b, 0.0, 0.0) -
                                         select((1.0 - exp(-b * y1)) / b, y1, y1 <= 0.0),
                                     0.0, 0.0);
}
)";

struct Row {
    float closed = 0.0f;
    float numeric = 0.0f;
    float profile0 = 0.0f;
    float profile1 = 0.0f;
    float defaultProfileDelta = 0.0f;  // must be exactly 0 (ADR-568's bit-identity claim)
    float defaultIntegralDelta = 0.0f; // must be exactly 0
};

struct Case {
    float y0 = 0.0f;
    float y1 = 0.0f;
    float falloff = 0.17f;
    float upper = 0.0f;
    float curve = 0.0f;
};

std::vector<Row> run(gpu::Context& ctx, const std::vector<Case>& list) {
    std::vector<glm::vec4> cases;
    cases.reserve(list.size() * 2);
    for (const Case& c : list) {
        cases.emplace_back(c.y0, c.y1, c.falloff, 0.0f);
        cases.emplace_back(c.upper, c.curve, 0.0f, 0.0f);
    }
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
    pass.DispatchWorkgroups(static_cast<std::uint32_t>((list.size() + 63) / 64));
    pass.End();
    wgpu::CommandBuffer commands = encoder.Finish();
    ctx.queue().Submit(1, &commands);
    auto bytes = gpu::readBuffer(ctx, out, 0, rdesc.size);
    REQUIRE(bytes.has_value());
    std::vector<glm::vec4> raw(cases.size());
    std::memcpy(raw.data(), bytes->data(), rdesc.size);
    std::vector<Row> rows(list.size());
    for (std::size_t i = 0; i < list.size(); ++i) {
        rows[i] = Row{raw[i * 2].x, raw[i * 2].y, raw[i * 2].z, raw[i * 2].w,
                      raw[i * 2 + 1].x, raw[i * 2 + 1].y};
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
    std::vector<Case> cases;
    for (const float b : {0.004f, 0.05f, 0.17f, 0.25f, 0.6f, 2.0f}) {
        // ADR-568: and across the two SHAPE controls, including both ends of each. The compact
        // quadratic reaches exactly zero at 2/b, so `curve` 1 is the setting where the integral
        // has a case split the exponential does not -- which is the arm a closed form gets wrong.
        for (const auto [upper, curve] : std::initializer_list<std::pair<float, float>>{
                 {0.0f, 0.0f},   // what the model was before ADR-568
                 {0.35f, 0.0f},  // a haze floor under the exponential
                 {0.0f, 1.0f},   // a layer with a definite top
                 {0.0f, 0.5f},   // mid-blend, where neither family's formula alone is right
                 {0.2f, 0.75f},  // both at once
                 {1.0f, 1.0f}}) {// degenerate: a uniform atmosphere, no layer at all
            for (const auto [y0, y1] : std::initializer_list<std::pair<float, float>>{
                     {-120.0f, -10.0f},  // wholly inside the layer
                     {-80.0f, 40.0f},    // climbing out through the top
                     {60.0f, 180.0f},    // wholly above it
                     {-5.0f, 5.0f},      // straddling the seam closely
                     {200.0f, -200.0f},  // descending, so the sign of the span is exercised
                     {0.0f, 260.0f}}) {  // starting exactly on the join
                cases.push_back(Case{y0, y1, b, upper, curve});
            }
        }
    }

    const std::vector<Row> rows = run(*ctx, cases);
    REQUIRE(rows.size() == cases.size());

    int crossedTheSeam = 0;
    int sawFullDensity = 0;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const Case& c = cases[i];
        INFO("y0=" << c.y0 << " y1=" << c.y1 << " falloff=" << c.falloff << " upper=" << c.upper
             << " curve=" << c.curve << " closed=" << rows[i].closed
             << " numeric=" << rows[i].numeric);
        // The absolute tolerance is in METRES of full-density air. A span of 400 m integrated with
        // 4096 midpoint slices carries far less error than this; what it is really guarding
        // against is a shader that computes a different function, which misses by tens of metres.
        CHECK(rows[i].closed == Approx(rows[i].numeric).margin(0.02));
        if ((c.y0 < 0.0f) != (c.y1 < 0.0f)) {
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

TEST_CASE("the ADR-568 controls at zero leave the layer bit-identical", "[gpu][fog][height]") {
    // The promise every existing scene depends on, answered by the shader rather than by the
    // algebra: at `upper` 0 and `curve` 0 the profile and its integral must be EXACTLY the
    // expressions they replaced, not approximately. `mix(x, y, 0)` is `x*1 + y*0` and
    // `0 + 1*shape` is `shape`, both exact in IEEE for finite inputs -- but that is an argument,
    // and the difference is computed on the GPU here and required to be zero.
    //
    // It is the control on the whole of ADR-568: a height model that changed every scene by a
    // fraction of a level would be found by nothing else in the suite, because the volumetric
    // checkpoints are hashes of frames nobody would think to re-baseline for a "no-op" default.
    auto ctx = makeContext();
    std::vector<Case> cases;
    for (const float b : {0.004f, 0.05f, 0.17f, 0.6f, 2.0f}) {
        for (const float y : {-200.0f, -1.0f, 0.0f, 1.0f, 40.0f, 400.0f}) {
            cases.push_back(Case{0.0f, y, b, 0.0f, 0.0f});
        }
    }
    const std::vector<Row> rows = run(*ctx, cases);
    REQUIRE(rows.size() == cases.size());
    for (std::size_t i = 0; i < rows.size(); ++i) {
        INFO("y=" << cases[i].y1 << " falloff=" << cases[i].falloff);
        CHECK(rows[i].defaultProfileDelta == 0.0f);
        CHECK(rows[i].defaultIntegralDelta == 0.0f);
    }
}

TEST_CASE("the numerical integral in the case above is fine enough to be evidence",
          "[gpu][fog][height]") {
    // The control on the instrument rather than on the thing measured. If 4096 midpoint slices
    // were too coarse for these spans, the case above would be comparing the closed form against a
    // number with its own error and calling the agreement a result. Halving the span halves the
    // step, so the midpoint rule's error should fall by about four -- and here it should already
    // be so far below the tolerance that both halves agree with the closed form to well inside it.
    auto ctx = makeContext();
    const std::vector<Case> cases{Case{-200.0f, 200.0f, 0.17f, 0.2f, 0.5f},
                                  Case{-200.0f, 0.0f, 0.17f, 0.2f, 0.5f},
                                  Case{0.0f, 200.0f, 0.17f, 0.2f, 0.5f}};
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

// ---- ADR-715: the layer follows the ground, and the two readers still agree ---------------------
//
// `fogGroundFollow` puts the layer's top at `top + follow * ground(x, z)`. The MARCH evaluates
// `fogGroundProfileAt` per sample -- exact at every point. The SURFACE PASS cannot integrate that
// in closed form for an arbitrary terrain, so `fogGroundMean` treats the ground as piecewise linear
// along the ray, eight pieces, and integrates each piece with the same `fogHeightIntegral` as above.
// So the claim ADR-567 guards has a precise extension:
//
//   * on ground that is AFFINE along the ray (a plane, a slope) the two are the same integral, and
//     must agree to the tolerance the flat layer does -- a lost `follow`, a ground read at the wrong
//     place, a piece integrated with the wrong endpoints all miss by metres;
//   * on ground that is NOT (a valley) they differ by the terrain's departure from its chords, and
//     that difference is measured and bounded here rather than left to be discovered in a render.
//
// Both use the shipped WGSL with a height texture of the test's own, placed by the same
// `world::TerrainGround` lanes the renderer uploads -- and the ground itself is checked against the
// CPU twin, so the texture read the two readers share is not taken on trust either.
//
// **How it fails.** Change `fogLayerAltitude` to `y - (top + ground)` (ignoring `follow`) and the
// two readers go on AGREEING -- they share the function -- so the profile is also held against a CPU
// restatement at a known altitude, and that fails on every follow-0.5 case. Read the ground at
// `a.xz` for every piece of `fogGroundMean` and the sloped cases fail; swap `map0.zw` for
// `map0.xy` in `terrainGroundAt` and the ground parity check fails before either.

namespace {

constexpr const char* kGroundKernel = R"(
struct GroundArgs { count: vec4<f32>, map0: vec4<f32>, map1: vec4<f32> };
@group(0) @binding(0) var<uniform> gargs: GroundArgs;
@group(0) @binding(1) var<storage, read> gcases: array<vec4<f32>>;
@group(0) @binding(2) var<storage, read_write> gresults: array<vec4<f32>>;
@group(0) @binding(3) var heights: texture_2d<f32>;

@compute @workgroup_size(64)
fn cs_ground(@builtin(global_invocation_id) gid: vec3<u32>) {
    let i = gid.x;
    if (i * 3u + 2u >= arrayLength(&gcases)) { return; }
    let a = gcases[i * 3u].xyz;
    let top = gcases[i * 3u].w;
    let e = gcases[i * 3u + 1u].xyz;
    let follow = gcases[i * 3u + 1u].w;
    let b = gcases[i * 3u + 2u].x;
    let upper = gcases[i * 3u + 2u].y;
    let curve = gcases[i * 3u + 2u].z;
    let pooling = gcases[i * 3u + 2u].w; // ADR-717
    let len = length(e - a);

    // What the SURFACE pass multiplies the ray's length by (applyFog's follow branch).
    let closed = fogGroundMean(heights, gargs.map0, gargs.map1, a, e, top, follow, pooling, b, upper, curve) * len;
    // What the MARCH accumulates: its own per-sample term, integrated finely.
    let slices = 4096;
    let h = len / f32(slices);
    var sum = 0.0;
    for (var k = 0; k < slices; k = k + 1) {
        let p = mix(a, e, (f32(k) + 0.5) / f32(slices));
        sum = sum + fogGroundProfileAt(heights, gargs.map0, gargs.map1, p, top, follow, pooling, b, upper, curve) * h;
    }
    gresults[i * 3u] = vec4<f32>(closed, sum,
                                 terrainGroundAt(heights, gargs.map0, gargs.map1, a.xz),
                                 terrainGroundAt(heights, gargs.map0, gargs.map1, e.xz));
    gresults[i * 3u + 1u] = vec4<f32>(fogGroundProfileAt(heights, gargs.map0, gargs.map1, a, top, follow, pooling, b, upper, curve),
                                      fogGroundProfileAt(heights, gargs.map0, gargs.map1, e, top, follow, pooling, b, upper, curve),
                                      terrainGroundPairAt(heights, gargs.map0, gargs.map1, a.xz).y,
                                      terrainGroundPairAt(heights, gargs.map0, gargs.map1, e.xz).y);
    // ADR-717's bit-identity arm: the surface pass and the march's term as ADR-715 wrote them.
    gresults[i * 3u + 2u] = vec4<f32>(fogGroundMeanAdr715(heights, gargs.map0, gargs.map1, a, e, top, follow, b, upper, curve) * len,
                                      fogGroundProfileAtAdr715(heights, gargs.map0, gargs.map1, a, top, follow, b, upper, curve),
                                      fogGroundProfileAtAdr715(heights, gargs.map0, gargs.map1, e, top, follow, b, upper, curve),
                                      0.0);
}

// ---- ADR-715's two functions, verbatim, before ADR-717 generalised their reference ------------
fn fogLayerAltitudeAdr715(y: f32, top: f32, follow: f32, ground: f32) -> f32 {
    return y - (top + follow * ground);
}
fn fogGroundProfileAtAdr715(tex: texture_2d<f32>, map0: vec4<f32>, map1: vec4<f32>, p: vec3<f32>, top: f32,
                            follow: f32, b: f32, upper: f32, curve: f32) -> f32 {
    let ground = terrainGroundAt(tex, map0, map1, p.xz);
    return fogHeightProfile(fogLayerAltitudeAdr715(p.y, top, follow, ground), b, upper, curve);
}
fn fogGroundMeanAdr715(tex: texture_2d<f32>, map0: vec4<f32>, map1: vec4<f32>, a: vec3<f32>, e: vec3<f32>,
                       top: f32, follow: f32, b: f32, upper: f32, curve: f32) -> f32 {
    var sum = 0.0;
    var d0 = fogLayerAltitudeAdr715(a.y, top, follow, terrainGroundAt(tex, map0, map1, a.xz));
    for (var k = 1; k <= kFogGroundSegments; k = k + 1) {
        let p1 = mix(a, e, f32(k) / f32(kFogGroundSegments));
        let d1 = fogLayerAltitudeAdr715(p1.y, top, follow, terrainGroundAt(tex, map0, map1, p1.xz));
        var mean = 1.0;
        if (max(d0, d1) > 0.0) {
            mean = fogHeightProfile(d0, b, upper, curve);
            let rise = d1 - d0;
            if (abs(rise) > 1e-3) {
                mean = (fogHeightIntegral(d1, b, upper, curve) - fogHeightIntegral(d0, b, upper, curve)) / rise;
            }
        }
        sum = sum + mean;
        d0 = d1;
    }
    return sum / f32(kFogGroundSegments);
}
)";

struct GroundCase {
    glm::vec3 a{0.0f};
    glm::vec3 e{0.0f};
    float top = 0.0f;
    float follow = 1.0f;
    float falloff = 0.1f;
    float upper = 0.0f;
    float curve = 0.0f;
    float pooling = 0.0f; // ADR-717
};

struct GroundRow {
    float closed = 0.0f;  // metres of full-density air, the surface pass
    float numeric = 0.0f; // the same, integrated from the march's term
    float groundA = 0.0f;
    float groundE = 0.0f;
    float profileA = 0.0f;
    float profileE = 0.0f;
    float basinA = 0.0f; // ADR-717: the low-passed ground, as the shader reads it
    float basinE = 0.0f;
    // ADR-715's functions, restated: the bit-identity arm at pooling 0.
    float closed715 = 0.0f;
    float profileA715 = 0.0f;
    float profileE715 = 0.0f;
};

std::vector<GroundRow> runGround(gpu::Context& ctx, const world::TerrainGround& ground,
                                 const std::vector<GroundCase>& list) {
    REQUIRE(ground.valid());
    std::vector<glm::vec4> cases;
    for (const GroundCase& c : list) {
        cases.emplace_back(c.a, c.top);
        cases.emplace_back(c.e, c.follow);
        cases.emplace_back(c.falloff, c.upper, c.curve, c.pooling);
    }
    gpu::ShaderLibrary shaders(ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    auto src = shaders.loadSource("height_fog.wgsl");
    REQUIRE(src.has_value());
    auto module = shaders.compile(*src + kGroundKernel, "height-fog-ground-check");
    REQUIRE(module.has_value());

    const auto& device = ctx.device();
    const world::TerrainHeightField& field = *ground.field;
    wgpu::TextureDescriptor tdesc{};
    tdesc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
    tdesc.size = {field.width, field.depth, 1};
    // The renderer's layout (ADR-717): r = the ground, g = the basin.
    tdesc.format = wgpu::TextureFormat::RG32Float;
    wgpu::Texture texture = device.CreateTexture(&tdesc);
    wgpu::TexelCopyTextureInfo dst{};
    dst.texture = texture;
    wgpu::TexelCopyBufferLayout tl{};
    tl.bytesPerRow = field.width * 8;
    tl.rowsPerImage = field.depth;
    const wgpu::Extent3D extent = {field.width, field.depth, 1};
    REQUIRE(field.pooled());
    std::vector<float> texels(field.heights.size() * 2);
    for (std::size_t k = 0; k < field.heights.size(); ++k) {
        texels[k * 2] = field.heights[k];
        texels[k * 2 + 1] = field.basin[k];
    }
    ctx.queue().WriteTexture(&dst, texels.data(), texels.size() * sizeof(float), &tl, &extent);

    std::array<wgpu::BindGroupLayoutEntry, 4> entries{};
    entries[0].binding = 0;
    entries[0].visibility = wgpu::ShaderStage::Compute;
    entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
    entries[1].binding = 1;
    entries[1].visibility = wgpu::ShaderStage::Compute;
    entries[1].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
    entries[2].binding = 2;
    entries[2].visibility = wgpu::ShaderStage::Compute;
    entries[2].buffer.type = wgpu::BufferBindingType::Storage;
    entries[3].binding = 3;
    entries[3].visibility = wgpu::ShaderStage::Compute;
    entries[3].texture.sampleType = wgpu::TextureSampleType::UnfilterableFloat;
    entries[3].texture.viewDimension = wgpu::TextureViewDimension::e2D;
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
    cdesc.compute.entryPoint = "cs_ground";
    wgpu::ComputePipeline pipeline = device.CreateComputePipeline(&cdesc);
    REQUIRE(pipeline != nullptr);

    const std::array<glm::vec4, 3> args{glm::vec4(static_cast<float>(list.size()), 0.0f, 0.0f, 0.0f), ground.map0(),
                                        ground.map1()};
    wgpu::BufferDescriptor adesc{};
    adesc.size = sizeof(args);
    adesc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
    wgpu::Buffer argBuf = device.CreateBuffer(&adesc);
    ctx.queue().WriteBuffer(argBuf, 0, args.data(), sizeof(args));
    wgpu::BufferDescriptor sdesc{};
    sdesc.size = cases.size() * sizeof(glm::vec4);
    sdesc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
    wgpu::Buffer in = device.CreateBuffer(&sdesc);
    ctx.queue().WriteBuffer(in, 0, cases.data(), sdesc.size);
    wgpu::BufferDescriptor rdesc{};
    rdesc.size = list.size() * 3 * sizeof(glm::vec4);
    rdesc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopySrc;
    wgpu::Buffer out = device.CreateBuffer(&rdesc);

    std::array<wgpu::BindGroupEntry, 4> bind{};
    bind[0].binding = 0;
    bind[0].buffer = argBuf;
    bind[0].size = adesc.size;
    bind[1].binding = 1;
    bind[1].buffer = in;
    bind[1].size = sdesc.size;
    bind[2].binding = 2;
    bind[2].buffer = out;
    bind[2].size = rdesc.size;
    bind[3].binding = 3;
    bind[3].textureView = texture.CreateView();
    wgpu::BindGroupDescriptor gdesc{};
    gdesc.layout = layout;
    gdesc.entryCount = bind.size();
    gdesc.entries = bind.data();
    wgpu::BindGroup group = device.CreateBindGroup(&gdesc);

    wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
    wgpu::ComputePassEncoder pass = encoder.BeginComputePass();
    pass.SetPipeline(pipeline);
    pass.SetBindGroup(0, group);
    pass.DispatchWorkgroups(static_cast<std::uint32_t>((list.size() + 63) / 64));
    pass.End();
    wgpu::CommandBuffer commands = encoder.Finish();
    ctx.queue().Submit(1, &commands);
    auto bytes = gpu::readBuffer(ctx, out, 0, rdesc.size);
    REQUIRE(bytes.has_value());
    std::vector<glm::vec4> raw(list.size() * 3);
    std::memcpy(raw.data(), bytes->data(), rdesc.size);
    std::vector<GroundRow> rows(list.size());
    for (std::size_t i = 0; i < list.size(); ++i) {
        const glm::vec4& r0 = raw[i * 3];
        const glm::vec4& r1 = raw[i * 3 + 1];
        const glm::vec4& r2 = raw[i * 3 + 2];
        rows[i] = GroundRow{r0.x, r0.y, r0.z, r0.w, r1.x, r1.y, r1.z, r1.w, r2.x, r2.y, r2.z};
    }
    return rows;
}

// A height field over [-200, 200]^2 at 2 m from any function of (x, z), placed with a translation.
// ADR-717: `half` widens the square, so a test can keep its rays more than the basin kernel's reach
// (3 sigma, 72 m) from the edge, where the repeated edge samples bend an affine ground's basin.
world::TerrainGround groundFrom(float (*height)(float, float), glm::vec3 translation, float half = 200.0f) {
    auto field = std::make_shared<world::TerrainHeightField>();
    const auto n = static_cast<std::uint32_t>(half) + 1; // 2 m spacing over [-half, half]
    field->origin = glm::vec2(-half);
    field->spacing = 2.0f;
    field->width = field->depth = n;
    field->hash = 1;
    field->heights.resize(static_cast<std::size_t>(n) * n);
    for (std::uint32_t j = 0; j < n; ++j) {
        for (std::uint32_t i = 0; i < n; ++i) {
            const glm::vec2 p = field->origin + glm::vec2(static_cast<float>(i), static_cast<float>(j)) * 2.0f;
            field->heights[static_cast<std::size_t>(j) * n + i] = height(p.x, p.y);
        }
    }
    world::poolTerrainHeight(*field); // ADR-717: the basin, as the bake makes it
    return world::placeTerrainGround(field, translation, glm::vec3(1.0f), false);
}

float slope(float x, float z) { return 3.0f + 0.15f * x - 0.08f * z; }

// `fogHeightProfile` restated, for the one thing agreement cannot see: WHICH altitude both readers
// agree on. A defect in `fogLayerAltitude` moves the march and the surface together, and they go
// on agreeing about the wrong layer.
float profileOnCpu(float y, float b, float upper, float curve) {
    const float d = std::max(0.0f, y);
    const float capped = std::max(0.0f, 1.0f - b * d * 0.5f);
    const float shape = std::exp(-b * d) * (1.0f - curve) + capped * capped * curve;
    return upper + (1.0f - upper) * shape;
}
float valley(float x, float z) { return 0.2f * std::abs(x) + 0.02f * z; }

// Rays that stay inside the footprint (the fade outside it is not affine), crossing the followed
// top in both directions, sitting wholly inside and wholly above it, and climbing and descending.
std::vector<GroundCase> groundCases() {
    std::vector<GroundCase> cases;
    const std::initializer_list<std::pair<glm::vec3, glm::vec3>> rays{
        {{0.0f, 30.0f, 60.0f}, {-150.0f, 2.0f, -140.0f}},  // a camera on a hill, down into the far side
        {{-120.0f, 1.0f, 100.0f}, {150.0f, 25.0f, -60.0f}}, // low to high, across the whole slope
        {{40.0f, 80.0f, 0.0f}, {-40.0f, 70.0f, -10.0f}},    // well above the layer
        {{10.0f, 0.5f, 10.0f}, {-60.0f, -3.0f, 40.0f}},     // skimming the ground
        {{-150.0f, 60.0f, -150.0f}, {150.0f, -40.0f, 150.0f}}, // corner to corner, through it
        {{0.0f, 5.0f, 0.0f}, {90.0f, 5.0f, -30.0f}}}; // level
    for (const auto& [a, e] : rays) {
        for (const float follow : {0.5f, 1.0f}) {
            for (const float top : {4.0f, -10.0f}) {
                for (const float b : {0.05f, 0.1f, 0.6f}) {
                    for (const auto [upper, curve] : std::initializer_list<std::pair<float, float>>{
                             {0.0f, 0.0f}, {0.2f, 0.75f}, {0.0f, 1.0f}}) {
                        cases.push_back(GroundCase{a, e, top, follow, b, upper, curve});
                    }
                }
            }
        }
    }
    return cases;
}

} // namespace

TEST_CASE("with the layer on the ground, the surface fog integrates what the march marches",
          "[gpu][fog][height][terrain]") {
    auto ctx = makeContext();
    const world::TerrainGround ground = groundFrom(&slope, glm::vec3(10.0f, 1.0f, -20.0f));
    const std::vector<GroundCase> cases = groundCases();
    const std::vector<GroundRow> rows = runGround(*ctx, ground, cases);
    REQUIRE(rows.size() == cases.size());

    int crossed = 0;
    int full = 0;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const GroundCase& c = cases[i];
        INFO("a=(" << c.a.x << "," << c.a.y << "," << c.a.z << ") e=(" << c.e.x << "," << c.e.y << "," << c.e.z
                   << ") top=" << c.top << " follow=" << c.follow << " falloff=" << c.falloff << " upper="
                   << c.upper << " curve=" << c.curve << " closed=" << rows[i].closed
                   << " numeric=" << rows[i].numeric);
        // The ground the two readers share IS the terrain, as the CPU twin reads it.
        CHECK(rows[i].groundA == Approx(ground.groundAt(glm::vec2(c.a.x, c.a.z))).margin(1e-3));
        CHECK(rows[i].groundE == Approx(ground.groundAt(glm::vec2(c.e.x, c.e.z))).margin(1e-3));
        // ...and the layer they share is the one the control describes: its top at
        // `top + follow * ground`, so at follow 0.5 a point is measured from half the ground.
        const float wantA = profileOnCpu(c.a.y - (c.top + c.follow * ground.groundAt(glm::vec2(c.a.x, c.a.z))),
                                         c.falloff, c.upper, c.curve);
        CHECK(rows[i].profileA == Approx(wantA).margin(1e-4));
        // Metres of full-density air, the flat layer's tolerance: on affine ground the piecewise
        // integral is not an approximation.
        CHECK(rows[i].closed == Approx(rows[i].numeric).margin(0.02));
        const float da = c.a.y - (c.top + c.follow * ground.groundAt(glm::vec2(c.a.x, c.a.z)));
        const float de = c.e.y - (c.top + c.follow * ground.groundAt(glm::vec2(c.e.x, c.e.z)));
        crossed += (da < 0.0f) != (de < 0.0f) ? 1 : 0;
        full += (rows[i].profileA > 0.99f || rows[i].profileE > 0.99f) ? 1 : 0;
    }
    // The controls, as in the flat case: both branches of the piecewise profile were exercised.
    CHECK(crossed >= 12);
    CHECK(full >= 12);
}

TEST_CASE("with the layer on a valley floor, the surface fog's chords stay close to the march",
          "[gpu][fog][height][terrain]") {
    // Not affine: the ground folds at x = 0, so a ray crossing the fold is integrated against a
    // chord that cuts the corner. This is the approximation ADR-715 accepts, measured.
    auto ctx = makeContext();
    const world::TerrainGround ground = groundFrom(&valley, glm::vec3(0.0f));
    std::vector<GroundCase> cases;
    for (const float follow : {0.5f, 1.0f}) {
        for (const float b : {0.05f, 0.1f, 0.3f}) {
            // Across the valley at a camera's height, down from a rim to the far floor, and -- the
            // hard one -- skimming just over the fold, where the layer thins to a ridge of clear air
            // a chord can step over. Slid across the fold in uneven steps, so the result is not an
            // accident of where the chord ends happen to fall.
            for (const float o : {0.0f, 3.7f, 7.1f, 11.3f, 17.9f, 23.3f}) {
                cases.push_back(GroundCase{{-150.0f + o, 34.0f, 0.0f}, {150.0f + o, 34.0f, 20.0f}, 4.0f, follow, b});
                cases.push_back(GroundCase{{-120.0f + o, 30.0f, 50.0f}, {40.0f + o, 9.0f, -80.0f}, 4.0f, follow, b});
                cases.push_back(GroundCase{{-30.0f - o, 8.0f, 0.0f}, {170.0f - o, 8.0f, 0.0f}, 4.0f, follow, b});
            }
        }
    }
    const std::vector<GroundRow> rows = runGround(*ctx, ground, cases);
    float worst = 0.0f;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const float err = std::abs(rows[i].closed - rows[i].numeric);
        const float rel = err / std::max(rows[i].numeric, 1.0f);
        INFO("case " << i << " closed=" << rows[i].closed << " numeric=" << rows[i].numeric << " rel=" << rel);
        worst = std::max(worst, rel);
        // An eighth of the ray per chord. Measured worst over this sweep: 9.1% of the air at 4
        // pieces, 2.6% at 8 (shipped), 0.5% at 16 -- so 5% fails a regression to 4 pieces.
        CHECK(rel < 0.05f);
    }
    WARN("valley: worst relative gap between the surface pass and the march " << worst);
    // The control: the fold is real, so the two are NOT identical here -- if they were, this case
    // would be measuring an affine ground by mistake.
    CHECK(worst > 1e-4f);
}

// ---- ADR-717: the layer pools in the basins -----------------------------------------------------
//
// `fogPooling` measures the layer's top from `mix(follow * ground, basin, pooling)`, where the
// basin is the bake low-passed (`world::poolTerrainHeight`, the texture's second channel). Three
// claims, each checked from the shipped WGSL:
//
//   1. **0 is ADR-715, bit for bit.** ADR-717 routed ADR-715's reference through a new function;
//      at pooling 0 that function must BE the old arithmetic, not equal to it within rounding, or
//      every ground-following scene moves by an ULP. ADR-715's two functions are restated in the
//      kernel above and compared by memcmp.
//   2. **The readers agree, and about the right layer.** The surface pass's chords against the
//      march's per-sample term, on terrains whose basin is curved, bounded as ADR-715 bounds its
//      valley; the basin read against its CPU twin; the profile against a CPU restatement at the
//      altitude `TerrainGround::referenceAt` names -- because, as ADR-715 found, two readers that
//      share a wrong function go on agreeing.
//   3. **It pools.** Columns of air over valley floors and ridge crests on ridged ground that also
//      climbs, so one valley sits HIGHER than one ridge: pooled, every valley holds more fog than
//      every ridge; flat, that high valley holds less than that low ridge (the control -- the flat
//      plane cannot do this); followed, every column holds the same (ADR-715's blanket).
//
// **How it fails.** Read `.x` for the basin in `terrainGroundPairAt` and claim 3 fails (pooling
// becomes follow's blanket) as does the basin parity in 2; drop `pooling` from the mix and claim 2's
// CPU restatement fails; take the pair branch at pooling 0 and claim 1 fails.

namespace {

// Ridges 100 m apart on ground that climbs 0.25 m per metre: valleys at x = -50 and 50, ridges at
// x = -100, 0 and 100 -- and the valley at 50 (-2.5 m) is above the ridge at -100 (-10 m).
float ridged(float x, float /*z*/) { return 0.25f * x + 15.0f * std::cos(x * 6.2831853f / 100.0f); }
// Rolling ground with no axis to line up with, for the agreement sweep.
float rolling(float x, float z) {
    return 9.0f * std::sin(x * 0.041f) * std::cos(z * 0.033f) + 6.0f * std::sin((x + z) * 0.023f);
}

} // namespace

TEST_CASE("at fogPooling 0 the ground-following layer is ADR-715's, bit for bit", "[gpu][fog][height][terrain]") {
    auto ctx = makeContext();
    const world::TerrainGround ground = groundFrom(&slope, glm::vec3(10.0f, 1.0f, -20.0f));
    std::vector<GroundCase> cases = groundCases(); // every one at pooling 0
    const std::vector<GroundRow> rows = runGround(*ctx, ground, cases);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        INFO("case " << i << " closed=" << rows[i].closed << " adr715=" << rows[i].closed715);
        REQUIRE(std::memcmp(&rows[i].closed, &rows[i].closed715, sizeof(float)) == 0);
        REQUIRE(std::memcmp(&rows[i].profileA, &rows[i].profileA715, sizeof(float)) == 0);
        REQUIRE(std::memcmp(&rows[i].profileE, &rows[i].profileE715, sizeof(float)) == 0);
    }
    // The control: at pooling 0.5 the shipped functions measure from a different layer, so the
    // comparison above could see a difference if there were one.
    for (GroundCase& c : cases) {
        c.pooling = 0.5f;
    }
    const std::vector<GroundRow> half = runGround(*ctx, ground, cases);
    std::size_t differ = 0;
    for (const GroundRow& r : half) {
        differ += std::memcmp(&r.closed, &r.closed715, sizeof(float)) != 0 ? 1 : 0;
    }
    CHECK(differ > half.size() / 2);
}

TEST_CASE("with the layer pooled on affine ground, the surface fog integrates what the march marches",
          "[gpu][fog][height][terrain]") {
    // ADR-715's exactness claim, for the pooled reference. The basin of an affine ground is that
    // ground (a normalised kernel over a plane is the plane), so away from the field's edge the
    // pooled reference is affine along every ray and the surface pass's chords are exact. The field
    // is widened to +-400 m so every ray stays well over 72 m inside it.
    auto ctx = makeContext();
    const world::TerrainGround ground = groundFrom(&slope, glm::vec3(10.0f, 1.0f, -20.0f), 400.0f);
    std::vector<GroundCase> cases;
    for (GroundCase c : groundCases()) {
        for (const float pooling : {0.5f, 1.0f}) {
            c.pooling = pooling;
            cases.push_back(c);
        }
    }
    const std::vector<GroundRow> rows = runGround(*ctx, ground, cases);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const GroundCase& c = cases[i];
        const glm::vec2 a(c.a.x, c.a.z);
        INFO("case " << i << " pooling=" << c.pooling << " follow=" << c.follow << " closed=" << rows[i].closed
                     << " numeric=" << rows[i].numeric);
        // The basin the readers share IS the bake's basin, as the CPU twin reads it -- and on a plane
        // it is the plane.
        CHECK(rows[i].basinA == Approx(ground.basinAt(a)).margin(1e-3));
        CHECK(rows[i].basinE == Approx(ground.basinAt(glm::vec2(c.e.x, c.e.z))).margin(1e-3));
        CHECK(ground.basinAt(a) == Approx(ground.groundAt(a)).margin(1e-3));
        // ...and the layer they share is the one the control describes, at the altitude
        // `referenceAt` names -- the check two readers sharing a wrong function cannot pass.
        const float want = profileOnCpu(c.a.y - (c.top + ground.referenceAt(a, c.follow, c.pooling)), c.falloff,
                                        c.upper, c.curve);
        CHECK(rows[i].profileA == Approx(want).margin(1e-4));
        CHECK(rows[i].closed == Approx(rows[i].numeric).margin(0.02));
    }
}

TEST_CASE("with the layer pooled, the surface fog's chords on curved ground are measured",
          "[gpu][fog][height][terrain]") {
    // ADR-715 bounds its chords on its valley fold at 5%, and that bound holds pooled. Rougher ground
    // is measured beside the follow arm on the same rays, because the gap there is ADR-715's chord
    // rule (eight pieces of a 260-280 m ray, so 33-35 m chords against 100-150 m features) and not
    // something pooling introduced: following that ground already misses by 13-17%. The worst cases
    // are rays that GRAZE the layer's top, where a chord's sagitta of a metre is a large fraction of
    // the air the ray sees. ADR-717 records these numbers; the regression bound is the measured
    // worst with a margin.
    auto ctx = makeContext();
    struct Arm {
        const char* name;
        float follow, pooling;
    };
    const std::array<Arm, 4> arms{Arm{"follow 1", 1.0f, 0.0f}, Arm{"pooling 0.5", 0.0f, 0.5f},
                                  Arm{"pooling 1", 0.0f, 1.0f}, Arm{"follow 1 + pooling 0.5", 1.0f, 0.5f}};
    struct Terrain {
        const char* name;
        float (*height)(float, float);
        float bound; // the pooled arms' regression bound, relative to the air
    };
    for (const Terrain t : {Terrain{"valley", &valley, 0.05f}, Terrain{"ridged", &ridged, 0.15f},
                            Terrain{"rolling", &rolling, 0.5f}}) {
        const world::TerrainGround ground = groundFrom(t.height, glm::vec3(0.0f));
        std::array<float, 4> worst{};
        std::array<float, 4> worstMetres{};
        for (std::size_t k = 0; k < arms.size(); ++k) {
            const Arm& arm = arms[k];
            std::vector<GroundCase> cases;
            for (const float b : {0.05f, 0.3f}) {
                for (const float o : {0.0f, 7.1f, 17.9f}) {
                    cases.push_back(GroundCase{{-140.0f + o, 30.0f, 10.0f}, {140.0f + o, 12.0f, -30.0f}, 4.0f,
                                               arm.follow, b, 0.0f, 0.0f, arm.pooling});
                    cases.push_back(GroundCase{{-60.0f - o, 40.0f, -120.0f}, {70.0f - o, -5.0f, 110.0f}, 4.0f,
                                               arm.follow, b, 0.2f, 0.75f, arm.pooling});
                    cases.push_back(GroundCase{{-130.0f, 2.0f + o, 0.0f}, {130.0f, 2.0f + o, 5.0f}, -2.0f, arm.follow,
                                               b, 0.0f, 1.0f, arm.pooling});
                }
            }
            const std::vector<GroundRow> rows = runGround(*ctx, ground, cases);
            for (std::size_t i = 0; i < rows.size(); ++i) {
                const float err = std::abs(rows[i].closed - rows[i].numeric);
                worst[k] = std::max(worst[k], err / std::max(rows[i].numeric, 1.0f));
                worstMetres[k] = std::max(worstMetres[k], err);
            }
        }
        WARN(t.name << ": worst gap, surface pass vs march (fraction of the air / metres of air) -- follow 1 "
                    << worst[0] << " / " << worstMetres[0] << ", pooling 0.5 " << worst[1] << " / " << worstMetres[1]
                    << ", pooling 1 " << worst[2] << " / " << worstMetres[2] << ", follow 1 + pooling 0.5 "
                    << worst[3] << " / " << worstMetres[3]);
        for (std::size_t k = 1; k < arms.size(); ++k) {
            INFO(t.name << " / " << arms[k].name);
            CHECK(worst[k] < t.bound);
        }
        CHECK(worst[2] > 1e-5f); // a curved reference: the chords are an approximation here, not exact
    }
}

TEST_CASE("a pooled layer is deep in the valleys and thin over the ridges", "[gpu][fog][height][terrain]") {
    auto ctx = makeContext();
    const world::TerrainGround ground = groundFrom(&ridged, glm::vec3(0.0f));
    const std::array<float, 2> valleys{-50.0f, 50.0f};
    const std::array<float, 3> ridges{-100.0f, 0.0f, 100.0f};
    // A vertical column from just above the ground to 80 m above it: the march's integral of its own
    // term is the metres of full-density air standing on that spot.
    const auto columns = [&](float follow, float pooling) {
        std::vector<GroundCase> cases;
        for (const float x : valleys) {
            const float g = ground.groundAt(glm::vec2(x, 0.0f));
            cases.push_back(GroundCase{{x, g + 0.05f, 0.0f}, {x, g + 80.0f, 0.0f}, 4.0f, follow, 0.3f, 0.0f, 0.0f,
                                       pooling});
        }
        for (const float x : ridges) {
            const float g = ground.groundAt(glm::vec2(x, 0.0f));
            cases.push_back(GroundCase{{x, g + 0.05f, 0.0f}, {x, g + 80.0f, 0.0f}, 4.0f, follow, 0.3f, 0.0f, 0.0f,
                                       pooling});
        }
        std::vector<float> air;
        for (const GroundRow& r : runGround(*ctx, ground, cases)) {
            air.push_back(r.numeric);
        }
        return air; // valleys -50, 50, then ridges -100, 0, 100
    };
    const std::vector<float> flat = columns(0.0f, 0.0f);
    const std::vector<float> follows = columns(1.0f, 0.0f);
    const std::vector<float> pooled = columns(0.0f, 1.0f);
    const std::vector<float> half = columns(0.0f, 0.5f);
    const auto say = [](const char* name, const std::vector<float>& v) {
        WARN(name << ": valley -50 " << v[0] << " m, valley 50 " << v[1] << " m | ridge -100 " << v[2] << " m, ridge 0 "
                  << v[3] << " m, ridge 100 " << v[4] << " m");
    };
    say("flat", flat);
    say("follow 1", follows);
    say("pooling 0.5", half);
    say("pooling 1", pooled);

    // Pooled: every valley holds more air than every ridge, by a wide margin.
    const float leastValley = std::min(pooled[0], pooled[1]);
    const float mostRidge = std::max({pooled[2], pooled[3], pooled[4]});
    CHECK(leastValley > 3.0f * mostRidge);
    // Including the HIGH valley against the LOW ridge -- which is what the flat plane cannot do:
    // there the valley at 50 m sits above the plane and the ridge at -100 m below it.
    CHECK(pooled[1] > 3.0f * pooled[2]);
    CHECK(flat[1] < flat[2]);
    // ADR-715's blanket: following, every spot holds the same column.
    for (const float v : follows) {
        CHECK(v == Approx(follows[0]).epsilon(0.01));
    }
    // Half-pooled sits between: the high valley gains on the flat plane, the low ridge loses.
    CHECK(half[1] > flat[1]);
    CHECK(half[2] < flat[2]);
}
