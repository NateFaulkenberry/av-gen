// CPU/GPU parity of the tornado field (ADR-580). `shaders/tornado.wgsl` claims to be a
// transliteration of `core/tornado.cpp`; a compute harness makes it earn the claim by evaluating
// `sampleTornado` at a spread of positions and comparing every component with the CPU's answer.
//
// The comparison goes through the PACKED form, which is the whole reason `packTornado` exists as a
// separate step: both sides start from bytes that are identical by construction, so a disagreement
// is a disagreement about the maths and never about how a field was interpreted on the way in.
// `core/wind.cpp` (ADR-055) and `core/vortex.cpp` (ADR-388) are the precedents and this is
// deliberately the same shape, including the mistakes those two paid for:
//
//   * **ADR-401**: the kernel takes the WHOLE uniform block as one member, never a field list. The
//     vortex harness copied field by field, ADR-389 added a seventh `vec4` to the real struct on
//     both sides and to that list on neither, and the test spent a day comparing a GPU field with
//     three features switched off against a CPU field with them on. 75 assertions failed and not
//     one of them was a disagreement about arithmetic.
//   * **ADR-387, per field**: a correct value is not a reached value. The reachability probe at the
//     bottom shows every number the block carries moving the GPU's answer, because a field that
//     reaches nothing is indistinguishable, from inside a parity comparison, from a field that
//     agrees.
//
// Why it matters beyond tidiness: ADR-360 requires a render to be reproducible, and the way that is
// always lost is something integrating a frame delta instead of evaluating a function of the
// transport second. A sampler two implementations agree on at arbitrary (position, time) pairs
// cannot be integrating anything.

#include "core/log.hpp"
#include "core/tornado.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <memory>
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

// One thread per sample. Output per sample: (density, radialT, heightT, envelope) and (velocity, 0).
constexpr const char* kKernel = R"(
// ADR-401: the whole uniform block as ONE member, not a field list. A tenth `vec4` added to
// `TornadoUniformsWgsl` is carried here with no edit, and one added on only one side fails the
// `sizeof` assertion below or Dawn's minBindingSize check rather than silently reading zero.
struct Args {
    v: TornadoUniformsWgsl,
    time: vec4<f32>,
};
@group(0) @binding(0) var<uniform> args: Args;
@group(0) @binding(1) var<storage, read> samples: array<vec4<f32>>;
@group(0) @binding(2) var<storage, read_write> results: array<vec4<f32>>;

@compute @workgroup_size(64)
fn cs_sample(@builtin(global_invocation_id) gid: vec3<u32>) {
    let i = gid.x;
    if (i >= arrayLength(&samples)) { return; }
    let s = sampleTornado(args.v, samples[i].xyz, args.time.x);
    results[i * 2u] = vec4<f32>(s.density, s.radialT, s.heightT, s.envelope);
    results[i * 2u + 1u] = vec4<f32>(s.velocity, 0.0);
}
)";

struct Gpu {
    float density = 0.0f;
    float radialT = 0.0f;
    float heightT = 0.0f;
    float envelope = 0.0f;
    glm::vec3 velocity{0.0f};
};

class Harness {
public:
    explicit Harness(gpu::Context& ctx)
        : ctx_(ctx), shaders_(ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)}) {
        // `tornado.wgsl` includes nothing and needs nothing -- there is no fBM in it, which is the
        // architecture rather than an omission (ADR-580 §5). So unlike the vortex harness there is
        // no `noise.wgsl` to prepend, and no include-order hazard to get wrong.
        // Phase 4: `tornado.wgsl` needs `fbm3` and deliberately does not include it, because the
        // include directive does not de-duplicate and `volume.wgsl` already brings noise in through
        // `fields.wgsl`. So this harness prepends it, exactly as the vortex harness does.
        auto noise = shaders_.loadSource("noise.wgsl");
        REQUIRE(noise.has_value());
        auto tornado = shaders_.loadSource("tornado.wgsl");
        REQUIRE(tornado.has_value());
        auto module = shaders_.compile(*noise + *tornado + kKernel, "tornado-parity");
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

    std::vector<Gpu> run(const tornado::TornadoUniforms& u, float time,
                         const std::vector<glm::vec3>& positions) {
        const auto& device = ctx_.device();
        struct Args {
            tornado::TornadoUniforms v;
            glm::vec4 time;
        } args{u, glm::vec4(time, 0.0f, 0.0f, 0.0f)};
        static_assert(sizeof(Args) == sizeof(tornado::TornadoUniforms) + sizeof(glm::vec4),
                      "the kernel's Args must be the uniform block plus the time vector");
        wgpu::BufferDescriptor adesc{};
        adesc.size = sizeof(Args);
        adesc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        wgpu::Buffer argBuf = device.CreateBuffer(&adesc);
        ctx_.queue().WriteBuffer(argBuf, 0, &args, sizeof(Args));

        std::vector<glm::vec4> padded;
        padded.reserve(positions.size());
        for (const glm::vec3& p : positions) {
            padded.emplace_back(p, 0.0f);
        }
        wgpu::BufferDescriptor sdesc{};
        sdesc.size = padded.size() * sizeof(glm::vec4);
        sdesc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
        wgpu::Buffer samples = device.CreateBuffer(&sdesc);
        ctx_.queue().WriteBuffer(samples, 0, padded.data(), sdesc.size);

        wgpu::BufferDescriptor rdesc{};
        rdesc.size = padded.size() * 2 * sizeof(glm::vec4);
        rdesc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopySrc;
        wgpu::Buffer results = device.CreateBuffer(&rdesc);

        std::array<wgpu::BindGroupEntry, 3> bind{};
        bind[0].binding = 0;
        bind[0].buffer = argBuf;
        bind[0].size = adesc.size;
        bind[1].binding = 1;
        bind[1].buffer = samples;
        bind[1].size = sdesc.size;
        bind[2].binding = 2;
        bind[2].buffer = results;
        bind[2].size = rdesc.size;
        wgpu::BindGroupDescriptor gdesc{};
        gdesc.layout = layout_;
        gdesc.entryCount = bind.size();
        gdesc.entries = bind.data();
        wgpu::BindGroup group = device.CreateBindGroup(&gdesc);

        wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
        wgpu::ComputePassEncoder pass = encoder.BeginComputePass();
        pass.SetPipeline(pipeline_);
        pass.SetBindGroup(0, group);
        pass.DispatchWorkgroups(static_cast<std::uint32_t>((padded.size() + 63) / 64));
        pass.End();
        wgpu::CommandBuffer commands = encoder.Finish();
        ctx_.queue().Submit(1, &commands);
        auto bytes = gpu::readBuffer(ctx_, results, 0, rdesc.size);
        REQUIRE(bytes.has_value());
        std::vector<glm::vec4> raw(padded.size() * 2);
        std::memcpy(raw.data(), bytes->data(), rdesc.size);
        std::vector<Gpu> out(padded.size());
        for (std::size_t i = 0; i < padded.size(); ++i) {
            out[i].density = raw[i * 2].x;
            out[i].radialT = raw[i * 2].y;
            out[i].heightT = raw[i * 2].z;
            out[i].envelope = raw[i * 2].w;
            out[i].velocity = glm::vec3(raw[i * 2 + 1]);
        }
        return out;
    }

private:
    gpu::Context& ctx_;
    gpu::ShaderLibrary shaders_;
    wgpu::BindGroupLayout layout_;
    wgpu::ComputePipeline pipeline_;
};

// The Classic Cone, which is the preset the lab scene renders and the one a reader has seen, so
// parity is asserted about the shape somebody is actually looking at rather than a rounder one.
// Every feature is ON: the wobble, the lean, the striations and their harmonics, the skirt and the
// cloud. A parity test over a field with half its terms gated off passes and proves half as much.
tornado::TornadoField cone() {
    tornado::TornadoField f;
    f.base = {0.0f, 0.0f, 0.0f};
    f.height = 800.0f;
    f.radiusBottom = 40.0f;
    f.radiusMid = 52.0f;
    f.radiusTop = 70.0f;
    f.taper = 1.4f;
    f.shellWidth = 0.22f;
    f.shellGain = 1.0f;
    f.coreRadius = 0.55f;
    f.coreDensity = 0.35f;
    f.edgeSoft = 0.35f;
    f.wallCloudGain = 0.6f;
    f.cloudWidth = 4.6f;
    f.cloudHeight = 0.16f;
    f.cloudDensity = 0.9f;
    f.touchdown = 1.0f;
    f.footSoft = 0.04f;
    f.skirtWidth = 2.2f;
    f.skirtHeight = 0.10f;
    f.skirtDensity = 0.8f;
    f.skirtFlare = 0.6f;
    f.stripeCount = 4.0f;
    f.stripePitch = 3.4f;
    f.stripeDepth = 0.35f;
    f.stripeHarmonic = 0.4f;
    f.circulation = 900.0f;
    f.inflow = 0.25f;
    f.lift = 1.0f;
    f.rotationBottom = 1.0f;
    f.rotationTop = 0.55f;
    f.rotationCurve = 1.0f;
    // The detail stack ON, or nine of the knobs below are gated off and the reachability probe
    // passes while proving nothing about them -- ADR-401's defect in its other form.
    f.cloudAmount = 0.65f;
    f.macroAmp = 1.0f;
    f.mesoAmp = 0.5f;
    f.microAmp = 0.25f;
    f.detailContrast = 1.6f;
    f.detailScale = 2.4f;
    f.climbRate = 0.07f;
    f.erosion = 1.2f;
    f.edgeWidth = 0.6f;
    f.suctionCount = 4.0f;
    f.suctionStrength = 0.5f;
    f.suctionRadius = 0.95f;
    f.suctionWidth = 0.32f;
    f.suctionSpeed = 0.8f;
    f.lean = {30.0f, -18.0f};
    f.wobbleAmount = 18.0f;
    f.wobbleSpeed = 0.15f;
    return f;
}

// A spread that lands in every branch of `tornadoEvaluate`, including all four early-outs: inside
// the shell, inside the core, past the edge, in the skirt, in the cloud, above the cap, below the
// base, and exactly on the axis -- which is the one place the Burgers-Rott tangential term is a
// 0/0 and the guard that returns its limit has to be exercised on both sides.
std::vector<glm::vec3> positions() {
    std::vector<glm::vec3> out;
    for (int i = 0; i < 96; ++i) {
        const float a = static_cast<float>(i) * 0.61803f * 6.2831853f;
        const float r = static_cast<float>(i) * 4.2f;          // 0 .. 399 m: axis, shell, past the cloud
        const float y = -20.0f + static_cast<float>(i) * 9.5f; // below the base to above the cap
        out.emplace_back(std::cos(a) * r, y, std::sin(a) * r);
    }
    out.emplace_back(0.0f, 0.0f, 0.0f);       // the ground contact, on the axis: the velocity 0/0
    out.emplace_back(0.0f, 400.0f, 0.0f);     // on the axis, mid column
    out.emplace_back(0.0f, 1600.0f, 0.0f);    // far above the cap: the h > 1.08 early-out
    out.emplace_back(0.0f, -400.0f, 0.0f);    // below the base: the h < -0.02 early-out
    out.emplace_back(3000.0f, 400.0f, 3000.0f); // far outside: the radial early-out
    out.emplace_back(46.0f, 380.0f, 0.0f);    // squarely in the shell
    out.emplace_back(10.0f, 380.0f, 0.0f);    // squarely in the core
    out.emplace_back(70.0f, 20.0f, 0.0f);     // squarely in the skirt
    out.emplace_back(180.0f, 780.0f, 0.0f);   // squarely in the wall cloud
    return out;
}

// Positions concentrated where the field is non-zero, for the reachability probe: a knob that only
// changes the shell is invisible to a sample spread that is mostly empty space.
std::vector<glm::vec3> bodyPositions() {
    std::vector<glm::vec3> out;
    for (int i = 0; i < 80; ++i) {
        const float a = static_cast<float>(i) * 0.61803f * 6.2831853f;
        const float h = 0.02f + static_cast<float>(i % 20) * 0.052f; // 0.02 .. 1.01 of the height
        const float rr = 0.15f + static_cast<float>(i % 7) * 0.22f;  // inside the core to past the wall
        const float radius = 40.0f + h * 40.0f;
        out.emplace_back(std::cos(a) * rr * radius, h * 800.0f, std::sin(a) * rr * radius);
    }
    // The wall cloud's RIM, and these are here because the probe below found the hole on its first
    // run rather than because anybody foresaw it.
    //
    // `cloudWidth` reported "moved 0 of 80 GPU samples". It is not a dead control: the cloud's
    // interior is a PLATEAU -- `1 - smoothstep(0.55, 1.0, dist / cr)` is exactly 1 for every sample
    // inside 55% of the cloud radius -- and the spread above never left that plateau, because it is
    // built from the FUNNEL's radius and the cloud is four times wider. Widening a plateau does not
    // move any point already on it.
    //
    // So the finding was a coverage hole in the probe, and the fix is to cover the feature rather
    // than to relax the check. Worth the paragraph because the failure mode is general: a
    // reachability probe built from one feature's extent is blind to every feature that is larger,
    // and it reports that blindness as a dead control.
    for (int i = 0; i < 24; ++i) {
        const float a = static_cast<float>(i) * 0.61803f * 6.2831853f;
        const float dist = 120.0f + static_cast<float>(i) * 9.0f; // 120 .. 327 m: across the rim
        const float y = 690.0f + static_cast<float>(i % 6) * 18.0f;
        out.emplace_back(std::cos(a) * dist, y, std::sin(a) * dist);
    }
    return out;
}

std::size_t movedCount(const std::vector<Gpu>& a, const std::vector<Gpu>& b) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) {
        if (std::abs(a[i].density - b[i].density) > 1e-5f ||
            std::abs(a[i].envelope - b[i].envelope) > 1e-5f ||
            std::abs(a[i].radialT - b[i].radialT) > 1e-5f ||
            glm::length(a[i].velocity - b[i].velocity) > 1e-4f) {
            ++n;
        }
    }
    return n;
}

} // namespace

TEST_CASE("the tornado shader agrees with core/tornado.cpp", "[gpu][tornado][parity][determinism]") {
    auto ctx = makeContext();
    Harness harness(*ctx);
    const tornado::TornadoUniforms u = tornado::packTornado(cone());
    const std::vector<glm::vec3> pts = positions();

    // Several times, because every temporal term in this field is also a spatial one: the wobble
    // moves the axis and the striations turn. A parity that only holds at t=0 has tested neither.
    int nonZero = 0;
    int moving = 0;
    for (const float t : {0.0f, 6.0f, 41.7f}) {
        const std::vector<Gpu> gpu = harness.run(u, t, pts);
        REQUIRE(gpu.size() == pts.size());
        for (std::size_t i = 0; i < pts.size(); ++i) {
            const tornado::TornadoSample cpu = tornado::sampleTornado(u, pts[i], t);
            INFO("t=" << t << " p=(" << pts[i].x << ", " << pts[i].y << ", " << pts[i].z << ")");
            // The ENVELOPE keeps the 1e-4 bound, because it is closed form throughout -- and
            // holding it to the tighter number is the point: Phase 4 added noise on top, and if the
            // structure underneath had drifted this is where it would show.
            //
            // The DENSITY relaxes to 1e-3 now that three octaves of value noise multiply it, for
            // the reason the vortex's parity test gives for the same bound: fBM at three scales
            // accumulates more rounding than a closed form does, and the Metal compiler contracts
            // the arithmetic differently on each side (ADR-388 measured 62 of 518400 pixels moving
            // for that reason alone).
            CHECK(gpu[i].density == Approx(cpu.density).margin(1e-3));
            CHECK(gpu[i].envelope == Approx(cpu.envelope).margin(1e-4));
            CHECK(gpu[i].radialT == Approx(cpu.radialT).margin(1e-4));
            CHECK(gpu[i].heightT == Approx(cpu.heightT).margin(1e-4));
            // The velocity's margin is looser because `circulation / dist` is large and its
            // absolute error scales with it: a metres-per-second quantity in the hundreds does not
            // deserve the same absolute bound as a normalised density in 0..1.
            CHECK(gpu[i].velocity.x == Approx(cpu.velocity.x).margin(1e-2));
            CHECK(gpu[i].velocity.y == Approx(cpu.velocity.y).margin(1e-2));
            CHECK(gpu[i].velocity.z == Approx(cpu.velocity.z).margin(1e-2));
            if (cpu.density > 1e-4) {
                ++nonZero;
            }
            if (glm::length(cpu.velocity) > 1e-3f) {
                ++moving;
            }
        }
    }
    // THE CONTROL, and ADR-182's point exactly: a parity test over a field that is zero everywhere
    // passes perfectly and proves nothing. These say the samples reached the inside of the column
    // and that the velocity is not the zero vector it is outside it.
    CHECK(nonZero > 20);
    CHECK(moving > 20);
}

TEST_CASE("the tornado field is a pure function of position and time",
          "[gpu][tornado][determinism]") {
    // ADR-360's requirement, stated as the property that makes it true: evaluating out of order,
    // repeatedly, at times visited in any sequence, gives the same answer every time. A field that
    // integrated a frame delta could not pass this, and integrating a frame delta is the only way
    // this guarantee has ever been lost in this codebase.
    const tornado::TornadoUniforms u = tornado::packTornado(cone());
    const glm::vec3 p(46.0f, 380.0f, 0.0f);
    const tornado::TornadoSample forward = tornado::sampleTornado(u, p, 12.0f);
    int revisits = 0;
    for (const float t : {0.0f, 41.7f, 3.5f, 12.0f, 99.0f, 12.0f}) {
        const tornado::TornadoSample s = tornado::sampleTornado(u, p, t);
        if (t == 12.0f) {
            CHECK(s.density == forward.density);
            CHECK(s.envelope == forward.envelope);
            CHECK(s.velocity.x == forward.velocity.x);
            CHECK(s.velocity.y == forward.velocity.y);
            CHECK(s.velocity.z == forward.velocity.z);
            ++revisits;
        }
    }
    // The control the equality checks need: without it a loop that never hit 12.0 would pass.
    CHECK(revisits == 2);
    // ...and the field must not be constant in time, or every equality above is trivially true.
    CHECK(tornado::sampleTornado(u, p, 0.0f).density !=
          Approx(tornado::sampleTornado(u, p, 20.0f).density).margin(1e-6));
}

TEST_CASE("a tornado with no height is off, and costs nothing to ask", "[gpu][tornado]") {
    // The gate. `height` at 0 is the default, so this is the state of every scene that has never
    // heard of this effect, and the property that matters is that the field returns before doing
    // any work -- not merely that it returns zero.
    auto ctx = makeContext();
    Harness harness(*ctx);
    tornado::TornadoField f = cone();
    f.height = 0.0f;
    const std::vector<glm::vec3> pts = positions();
    const std::vector<Gpu> gpu = harness.run(tornado::packTornado(f), 6.0f, pts);
    for (std::size_t i = 0; i < pts.size(); ++i) {
        INFO("p=(" << pts[i].x << ", " << pts[i].y << ", " << pts[i].z << ")");
        CHECK(gpu[i].density == 0.0f);
        CHECK(gpu[i].envelope == 0.0f);
        CHECK(glm::length(gpu[i].velocity) == 0.0f);
        const tornado::TornadoSample cpu = tornado::sampleTornado(tornado::packTornado(f), pts[i], 6.0f);
        CHECK(cpu.density == 0.0f);
        CHECK(glm::length(cpu.velocity) == 0.0f);
    }
    // The control: the same positions with the height restored must NOT all be zero, or this test
    // is asserting that a sample spread misses the tornado.
    const std::vector<Gpu> live = harness.run(tornado::packTornado(cone()), 6.0f, pts);
    std::size_t hits = 0;
    for (const Gpu& g : live) {
        if (g.density > 1e-4f) {
            ++hits;
        }
    }
    INFO("with the height restored, " << hits << " of " << pts.size() << " samples are non-zero");
    CHECK(hits > 10);
}

TEST_CASE("every number the tornado uniform carries reaches the shader", "[gpu][tornado][parity]") {
    // **The probe ADR-401 says to write on the day the field is added rather than on the day
    // something breaks.**
    //
    // The vortex harness copied its uniform block into the kernel field by field; ADR-389 added a
    // seventh `vec4` to the real struct on both sides and to that list on neither, so the GPU ran
    // with three features switched off while the CPU ran with them on. A probe that perturbs the
    // thing that changed last passes straight through that. What catches it is ADR-387's rule
    // stated per field: a correct value is not a reached value, so every number the block carries
    // must be shown to move the GPU's answer.
    //
    // This harness takes the block by value so it cannot suffer that exact defect -- but "cannot
    // suffer it by construction" is a stated reason, and ADR-385 is about those. The probe is the
    // evidence.
    auto ctx = makeContext();
    Harness harness(*ctx);
    const std::vector<glm::vec3> pts = bodyPositions();
    constexpr float kT = 6.0f;

    const tornado::TornadoField base = cone();
    const std::vector<Gpu> reference = harness.run(tornado::packTornado(base), kT, pts);

    struct Knob {
        const char* name;
        float tornado::TornadoField::*field;
        float to;
    };
    // Every authored scalar `tornadoEvaluate` or `tornadoVelocityAt` reads. The next field to go
    // missing will not be one somebody predicted, which is why this is the whole list and not a
    // selection.
    const Knob knobs[] = {
        {"height", &tornado::TornadoField::height, 1300.0f},
        {"radiusBottom", &tornado::TornadoField::radiusBottom, 85.0f},
        {"radiusMid", &tornado::TornadoField::radiusMid, 120.0f},
        {"radiusTop", &tornado::TornadoField::radiusTop, 190.0f},
        {"taper", &tornado::TornadoField::taper, 0.45f},
        {"shellWidth", &tornado::TornadoField::shellWidth, 0.7f},
        {"shellGain", &tornado::TornadoField::shellGain, 2.6f},
        {"coreRadius", &tornado::TornadoField::coreRadius, 0.15f},
        {"coreDensity", &tornado::TornadoField::coreDensity, 1.4f},
        {"edgeSoft", &tornado::TornadoField::edgeSoft, 1.2f},
        {"wallCloudGain", &tornado::TornadoField::wallCloudGain, 3.0f},
        {"cloudWidth", &tornado::TornadoField::cloudWidth, 9.0f},
        {"cloudHeight", &tornado::TornadoField::cloudHeight, 0.45f},
        {"cloudDensity", &tornado::TornadoField::cloudDensity, 2.4f},
        {"touchdown", &tornado::TornadoField::touchdown, 0.45f},
        {"footSoft", &tornado::TornadoField::footSoft, 0.2f},
        {"skirtWidth", &tornado::TornadoField::skirtWidth, 5.5f},
        {"skirtHeight", &tornado::TornadoField::skirtHeight, 0.4f},
        {"skirtDensity", &tornado::TornadoField::skirtDensity, 2.6f},
        {"skirtFlare", &tornado::TornadoField::skirtFlare, 2.2f},
        {"stripeCount", &tornado::TornadoField::stripeCount, 11.0f},
        {"stripePitch", &tornado::TornadoField::stripePitch, 14.0f},
        {"stripeDepth", &tornado::TornadoField::stripeDepth, 0.62f},
        {"stripeHarmonic", &tornado::TornadoField::stripeHarmonic, 0.95f},
        {"cloudAmount", &tornado::TornadoField::cloudAmount, 0.2f},
        {"macroAmp", &tornado::TornadoField::macroAmp, 2.6f},
        {"mesoAmp", &tornado::TornadoField::mesoAmp, 1.9f},
        {"microAmp", &tornado::TornadoField::microAmp, 1.4f},
        {"detailContrast", &tornado::TornadoField::detailContrast, 4.1f},
        {"detailScale", &tornado::TornadoField::detailScale, 6.3f},
        {"climbRate", &tornado::TornadoField::climbRate, 0.9f},
        {"erosion", &tornado::TornadoField::erosion, 4.0f},
        {"edgeWidth", &tornado::TornadoField::edgeWidth, 1.8f},
        {"suctionCount", &tornado::TornadoField::suctionCount, 5.0f},
        {"suctionStrength", &tornado::TornadoField::suctionStrength, 0.7f},
        {"suctionRadius", &tornado::TornadoField::suctionRadius, 0.4f},
        {"suctionWidth", &tornado::TornadoField::suctionWidth, 0.9f},
        {"suctionSpeed", &tornado::TornadoField::suctionSpeed, 3.3f},
        {"wobbleAmount", &tornado::TornadoField::wobbleAmount, 140.0f},
        {"wobbleSpeed", &tornado::TornadoField::wobbleSpeed, 1.4f},
        // The velocity half. These move `sampleTornado`'s velocity and not its density, which is
        // exactly why `movedCount` compares the velocity as well: a probe that only watched the
        // density would report every one of these as unreachable and be wrong about all of them.
        {"circulation", &tornado::TornadoField::circulation, 4200.0f},
        {"coreRadiusMetres", &tornado::TornadoField::coreRadiusMetres, 180.0f},
        {"inflow", &tornado::TornadoField::inflow, 1.1f},
        {"lift", &tornado::TornadoField::lift, 2.7f},
        {"rotationBottom", &tornado::TornadoField::rotationBottom, 2.8f},
        {"rotationTop", &tornado::TornadoField::rotationTop, 2.2f},
        {"rotationCurve", &tornado::TornadoField::rotationCurve, 3.4f},
    };

    for (const Knob& k : knobs) {
        tornado::TornadoField f = base;
        f.*(k.field) = k.to;
        const std::vector<Gpu> moved = harness.run(tornado::packTornado(f), kT, pts);
        const std::size_t n = movedCount(reference, moved);
        INFO("changing " << k.name << " moved " << n << " of " << pts.size() << " GPU samples");
        CHECK(n > 0);
    }

    // `lean` is a `vec2` and so cannot go in the table above, but it is a number the block carries
    // and the rule is per number.
    {
        tornado::TornadoField f = base;
        f.lean = {-220.0f, 160.0f};
        const std::size_t n = movedCount(reference, harness.run(tornado::packTornado(f), kT, pts));
        INFO("changing lean moved " << n << " of " << pts.size() << " GPU samples");
        CHECK(n > 0);
    }

    // The control for the loop itself: re-running the SAME field must move nothing. Without it, a
    // harness that returned fresh noise every call would satisfy every assertion above.
    const std::vector<Gpu> again = harness.run(tornado::packTornado(base), kT, pts);
    INFO("the same field twice moved " << movedCount(reference, again) << " samples");
    CHECK(movedCount(reference, again) == 0);
}

TEST_CASE("the tornado parity comparison can fail", "[gpu][tornado][parity]") {
    // ADR-182: a probe that cannot fail proves nothing, and the way a parity comparison stops being
    // able to fail is a margin wide enough to swallow a real disagreement. 1e-4 is the margin the
    // test above uses; this shows what it is measured against by comparing the CPU's answer with
    // the GPU's for a DIFFERENT field and requiring the comparison to reject it.
    //
    // The perturbation is deliberately small -- a 2% change in the shell width -- because a probe
    // that only rejects a field with the height set to zero proves the margin is under 800 metres.
    auto ctx = makeContext();
    Harness harness(*ctx);
    const std::vector<glm::vec3> pts = bodyPositions();
    constexpr float kT = 6.0f;

    tornado::TornadoField perturbed = cone();
    perturbed.shellWidth *= 1.02f;
    const std::vector<Gpu> gpu = harness.run(tornado::packTornado(perturbed), kT, pts);
    const tornado::TornadoUniforms honest = tornado::packTornado(cone());

    std::size_t rejected = 0;
    for (std::size_t i = 0; i < pts.size(); ++i) {
        const tornado::TornadoSample cpu = tornado::sampleTornado(honest, pts[i], kT);
        if (std::abs(gpu[i].density - cpu.density) > 1e-4f) {
            ++rejected;
        }
    }
    INFO("a 2% shell-width change is rejected at " << rejected << " of " << pts.size()
         << " samples by the same margin the parity test uses");
    CHECK(rejected > 5);
}
