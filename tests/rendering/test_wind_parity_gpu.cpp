// CPU/GPU parity of the wind field (ADR-055 / ADR-370). `shaders/wind_field.wgsl` claims to be a
// term-for-term transliteration of `src/core/wind.cpp`; this makes it earn the claim by evaluating
// both over the same grid of positions and times and comparing every member of `WindSample`.
//
// Why this exists when `test_wind_gpu.cpp` already says "parity": that test renders a stalk and
// measures where its tip landed, in a wind with regionAmount, regionDrift, turbulence and
// gustAmount all set to ZERO -- the only version of a wind whose answer can be written down. It is
// a good test of the deformation and it cannot see three of the five numbers this field produces.
// In particular it cannot see `phase`, because `turbulence = 0` also zeroes the flutter gain that
// `phase` drives, and `phase` is exactly the field `shaders/particles.wgsl`'s hand-maintained
// second copy of this arithmetic had silently dropped.
//
// So: every stochastic term ON, the full five members, and a control that would fire if the two
// arms had agreed for the wrong reason (`vortex.wgsl`'s parity test is the shape this follows).

#include "core/log.hpp"
#include "core/wind.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
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

// One thread per sample. Output per sample: (direction.x, direction.y, strength, gust) then
// (phase, 0, 0, 0). All five members, because a parity test that reads four of them is how the
// fifth goes missing.
constexpr const char* kKernel = R"(
struct Args {
    dir: vec4<f32>,
    region: vec4<f32>,
    gust: vec4<f32>,
    turbulence: vec4<f32>,
    time: vec4<f32>,
};
@group(0) @binding(0) var<uniform> args: Args;
@group(0) @binding(1) var<storage, read> samples: array<vec4<f32>>;
@group(0) @binding(2) var<storage, read_write> results: array<vec4<f32>>;

@compute @workgroup_size(64)
fn cs_sample(@builtin(global_invocation_id) gid: vec3<u32>) {
    let i = gid.x;
    if (i >= arrayLength(&samples)) { return; }
    var w: WindField;
    w.dir = args.dir;
    w.region = args.region;
    w.gust = args.gust;
    w.turbulence = args.turbulence;
    let s = windSampleFrom(w, samples[i].xyz, args.time.x);
    results[i * 2u] = vec4<f32>(s.direction.x, s.direction.y, s.strength, s.gust);
    results[i * 2u + 1u] = vec4<f32>(s.phase, 0.0, 0.0, 0.0);
}
)";

struct Gpu {
    glm::vec2 direction{0.0f};
    float strength = 0.0f;
    float gust = 0.0f;
    float phase = 0.0f;
};

class Harness {
public:
    // `source` is the field module's text rather than its name, because the second test case in
    // this file has to compile a deliberately PERTURBED copy of it under the same kernel.
    explicit Harness(gpu::Context& ctx, const std::string& source)
        : ctx_(ctx), shaders_(ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)}) {
        auto module = shaders_.compile(source + kKernel, "wind-parity");
        INFO((module ? std::string() : module.error().message));
        REQUIRE(module.has_value());
        const auto& device = ctx_.device();
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
        layout_ = device.CreateBindGroupLayout(&ldesc);
        wgpu::PipelineLayoutDescriptor pdesc{};
        pdesc.bindGroupLayoutCount = 1;
        pdesc.bindGroupLayouts = &layout_;
        wgpu::ComputePipelineDescriptor cdesc{};
        cdesc.layout = device.CreatePipelineLayout(&pdesc);
        cdesc.compute.module = *module;
        cdesc.compute.entryPoint = "cs_sample";
        pipeline_ = device.CreateComputePipeline(&cdesc);
        REQUIRE(pipeline_ != nullptr);
    }

    std::vector<Gpu> run(const wind::WindUniforms& u, float time, const std::vector<glm::vec3>& positions) {
        const auto& device = ctx_.device();
        struct Args {
            glm::vec4 dir, region, gust, turbulence, time;
        } args{u.dir, u.region, u.gust, u.turbulence, glm::vec4(time, 0.0f, 0.0f, 0.0f)};

        wgpu::BufferDescriptor ud{};
        ud.size = sizeof(Args);
        ud.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        wgpu::Buffer uniforms = device.CreateBuffer(&ud);
        ctx_.queue().WriteBuffer(uniforms, 0, &args, sizeof(args));

        std::vector<glm::vec4> padded;
        padded.reserve(positions.size());
        for (const glm::vec3& p : positions) {
            padded.emplace_back(p, 0.0f);
        }
        wgpu::BufferDescriptor sd{};
        sd.size = padded.size() * sizeof(glm::vec4);
        sd.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
        wgpu::Buffer samples = device.CreateBuffer(&sd);
        ctx_.queue().WriteBuffer(samples, 0, padded.data(), sd.size);

        wgpu::BufferDescriptor rd{};
        rd.size = padded.size() * 2 * sizeof(glm::vec4);
        rd.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopySrc;
        wgpu::Buffer results = device.CreateBuffer(&rd);

        std::array<wgpu::BindGroupEntry, 3> be{};
        be[0].binding = 0;
        be[0].buffer = uniforms;
        be[0].size = sizeof(Args);
        be[1].binding = 1;
        be[1].buffer = samples;
        be[1].size = sd.size;
        be[2].binding = 2;
        be[2].buffer = results;
        be[2].size = rd.size;
        wgpu::BindGroupDescriptor bd{};
        bd.layout = layout_;
        bd.entryCount = be.size();
        bd.entries = be.data();
        wgpu::BindGroup group = device.CreateBindGroup(&bd);

        wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
        wgpu::ComputePassEncoder pass = encoder.BeginComputePass();
        pass.SetPipeline(pipeline_);
        pass.SetBindGroup(0, group);
        pass.DispatchWorkgroups(static_cast<std::uint32_t>((padded.size() + 63) / 64));
        pass.End();
        wgpu::CommandBuffer commands = encoder.Finish();
        ctx_.queue().Submit(1, &commands);

        auto bytes = gpu::readBuffer(ctx_, results, 0, rd.size);
        REQUIRE(bytes.has_value());
        std::vector<glm::vec4> raw(padded.size() * 2);
        std::memcpy(raw.data(), bytes->data(), rd.size);
        std::vector<Gpu> out(padded.size());
        for (std::size_t i = 0; i < out.size(); ++i) {
            out[i].direction = {raw[i * 2].x, raw[i * 2].y};
            out[i].strength = raw[i * 2].z;
            out[i].gust = raw[i * 2].w;
            out[i].phase = raw[i * 2 + 1].x;
        }
        return out;
    }

private:
    gpu::Context& ctx_;
    gpu::ShaderLibrary shaders_;
    wgpu::BindGroupLayout layout_;
    wgpu::ComputePipeline pipeline_;
};

std::string readShader(const char* name) {
    const std::filesystem::path p = std::filesystem::path(AVGEN_SHADER_SOURCE_DIR) / name;
    REQUIRE(std::filesystem::is_regular_file(p));
    std::ifstream in(p);
    REQUIRE(in.good());
    std::stringstream buf;
    buf << in.rdbuf();
    return buf.str();
}

// Every stochastic term on, at values a scene would actually author. A wind with the gusts and the
// turbulence switched off is the one this test exists because the other one already covers.
wind::WindParams fullWind() {
    wind::WindParams w;
    w.enabled = true;
    w.direction = 0.73f;
    w.speed = 1.4f;
    w.regionScale = 48.0f;
    w.regionAmount = 0.52f;
    w.regionDrift = 0.07f;
    w.turbulence = 0.41f;
    w.turbulenceScale = 11.0f;
    w.turbulenceSpeed = 1.6f;
    w.gustAmount = 1.1f;
    w.gustScale = 27.0f;
    w.gustSpeed = 8.0f;
    w.gustSharpness = 3.4f;
    w.flutterScale = 2.6f;
    return w;
}

// A spread over metres and hundreds of metres, on and off the wind axis, so the regional waves, the
// gust front's cross-wind bend and the turbulence all vary across the set rather than sitting at
// one point of one cycle.
std::vector<glm::vec3> positions() {
    std::vector<glm::vec3> out;
    for (int i = 0; i < 128; ++i) {
        const float a = static_cast<float>(i) * 0.61803f * 6.2831853f;
        const float r = static_cast<float>(i) * 2.9f; // 0 .. 368 m
        out.emplace_back(std::cos(a) * r, static_cast<float>(i) * 0.4f, std::sin(a) * r);
    }
    out.emplace_back(0.0f, 0.0f, 0.0f);        // the origin: every spatial term at phase 0
    out.emplace_back(-311.0f, 4.0f, 207.0f);   // far upwind and across
    out.emplace_back(0.37f, 0.0f, -0.41f);     // sub-metre: the gradient across one leaf card
    return out;
}

} // namespace

TEST_CASE("the wind shader agrees with core/wind.cpp, phase included", "[gpu][wind][parity][determinism]") {
    auto ctx = makeContext();
    Harness harness(*ctx, readShader("wind_field.wgsl"));
    const wind::WindUniforms u = wind::packWind(fullWind());
    const std::vector<glm::vec3> pts = positions();

    // Several times: every temporal term in this field is also a spatial one, and a parity that
    // only holds at t = 0 has tested neither the drift, nor the travelling gust front, nor the
    // turbulence's own advection.
    int gustVaried = 0;
    int turned = 0;
    for (const float t : {0.0f, 3.5f, 47.25f}) {
        const std::vector<Gpu> got = harness.run(u, t, pts);
        REQUIRE(got.size() == pts.size());
        for (std::size_t i = 0; i < pts.size(); ++i) {
            const wind::WindSample cpu = wind::sampleWind(u, pts[i], t);
            INFO("t=" << t << " p=(" << pts[i].x << ", " << pts[i].y << ", " << pts[i].z << ")");
            CHECK(got[i].direction.x == Approx(cpu.direction.x).margin(1e-4));
            CHECK(got[i].direction.y == Approx(cpu.direction.y).margin(1e-4));
            CHECK(got[i].strength == Approx(cpu.strength).margin(1e-4));
            CHECK(got[i].gust == Approx(cpu.gust).margin(1e-4));
            // `phase` grows linearly with distance and reaches ~900 radians at the far samples, so
            // its rounding is relative rather than absolute -- an absolute 1e-4 there would be
            // asking for more precision than a float has.
            CHECK(got[i].phase == Approx(cpu.phase).epsilon(1e-5).margin(1e-4));
            if (cpu.gust > 0.02f && cpu.gust < 1.05f) {
                ++gustVaried;
            }
            if (std::abs(cpu.direction.x - u.dir.x) > 1e-3f) {
                ++turned;
            }
        }
    }
    // The neither-ran guard. Both arms agreeing on a field that is everywhere zero, or everywhere
    // the unturned steady flow, would be an agreement about nothing.
    INFO("samples with a partial gust: " << gustVaried << ", with a turned direction: " << turned);
    CHECK(gustVaried > 50);
    CHECK(turned > 200);
}

TEST_CASE("the wind parity comparison can fail", "[gpu][wind][parity]") {
    // ADR-182 applied to the comparison itself, and ADR-378's lesson applied to this one: the old
    // stalk test passed a shader whose amplitude had been perturbed by +15%, because its tolerance
    // had the wrong FORM. So before trusting the agreement above, perturb the shader and check the
    // same comparison rejects it -- otherwise "identical" might only mean "insensitive".
    auto ctx = makeContext();
    std::string perturbed = readShader("wind_field.wgsl");
    // One term, by a hair: the flutter's spatial wavenumber up by 1%. It moves `phase` and NOTHING
    // else, which is the point -- it is the member the dropped transliteration had lost, so a
    // comparison that cannot see this change is a comparison that could not have caught the defect
    // this whole file exists for.
    const std::string before = "out.phase = w.turbulence.z * (0.7 * a + 0.71 * c);";
    const std::string after = "out.phase = w.turbulence.z * 1.01 * (0.7 * a + 0.71 * c);";
    REQUIRE(perturbed.find(before) != std::string::npos);
    perturbed.replace(perturbed.find(before), before.size(), after);

    Harness harness(*ctx, perturbed);
    const wind::WindUniforms u = wind::packWind(fullWind());
    const std::vector<glm::vec3> pts = positions();
    const std::vector<Gpu> got = harness.run(u, 3.5f, pts);

    int phaseMismatches = 0;
    int otherMismatches = 0;
    for (std::size_t i = 0; i < pts.size(); ++i) {
        const wind::WindSample cpu = wind::sampleWind(u, pts[i], 3.5f);
        if (got[i].phase != Approx(cpu.phase).epsilon(1e-5).margin(1e-4)) {
            ++phaseMismatches;
        }
        if (got[i].strength != Approx(cpu.strength).margin(1e-4) ||
            got[i].gust != Approx(cpu.gust).margin(1e-4) ||
            got[i].direction.x != Approx(cpu.direction.x).margin(1e-4)) {
            ++otherMismatches;
        }
    }
    INFO("a 1% error in the flutter wavenumber: " << phaseMismatches << " of " << pts.size()
                                                  << " samples rejected on phase, " << otherMismatches
                                                  << " on the other members");
    CHECK(phaseMismatches > 100);
    // ...and the perturbation reached only what it was aimed at, or the arm above is failing for
    // some reason other than the one this arm is demonstrating.
    CHECK(otherMismatches == 0);
}
