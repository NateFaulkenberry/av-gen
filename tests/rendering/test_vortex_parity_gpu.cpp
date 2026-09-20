// CPU/GPU parity of the vortex field (ADR-388). `shaders/vortex.wgsl` claims to be a
// transliteration of `core/vortex.cpp`; a compute harness makes it earn the claim by evaluating
// `sampleVortex` at a spread of positions and comparing every component with the CPU's answer.
//
// The comparison goes through the PACKED form, which is the whole reason `packVortex` exists as a
// separate step: both sides start from bytes that are identical by construction, so a disagreement
// is a disagreement about the maths and never about how a field was interpreted on the way in.
// `wind.wgsl` ↔ `core/wind.cpp` is the precedent (ADR-055) and this is deliberately the same shape.
//
// Why it matters beyond tidiness: ADR-091 requires an offline render of second N to be identical to
// a realtime playthrough of second N, and the way that is always lost is something integrating a
// frame delta instead of evaluating a function of the transport second. A sampler that two
// implementations agree on at arbitrary (position, time) pairs cannot be integrating anything.

#include "core/log.hpp"
#include "core/vortex.hpp"
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

// One thread per sample. Output per sample: (density, radialT, depthT, envelope) and (velocity, 0).
constexpr const char* kKernel = R"(
// ADR-401: the WHOLE uniform block, as one member, and not a field list.
//
// It was a field list -- `v0: vec4<f32>` through `v4` -- copied member by member into a
// `VortexUniformsWgsl` in the kernel below. ADR-389 then added `v6` (smokeWarp, smokeBillow,
// detail) to the real struct on both sides and to this harness on neither, so `var v:
// VortexUniformsWgsl` left it zero-initialised and this test spent a day comparing a GPU vortex
// with the domain warp, the billow and the third octave all switched OFF against a CPU vortex with
// them on. 75 assertions failed and none of them was a disagreement about arithmetic.
//
// Naming the type instead of its fields is what makes that unrepeatable: a seventh vec4 added to
// `VortexUniformsWgsl` is carried here with no edit, and one added on only one side fails the
// `sizeof` assertion below or Dawn's minBindingSize check rather than silently reading zero.
struct Args {
    v: VortexUniformsWgsl,
    time: vec4<f32>,
};
@group(0) @binding(0) var<uniform> args: Args;
@group(0) @binding(1) var<storage, read> samples: array<vec4<f32>>;
@group(0) @binding(2) var<storage, read_write> results: array<vec4<f32>>;

@compute @workgroup_size(64)
fn cs_sample(@builtin(global_invocation_id) gid: vec3<u32>) {
    let i = gid.x;
    if (i >= arrayLength(&samples)) { return; }
    let s = sampleVortex(args.v, samples[i].xyz, args.time.x);
    results[i * 2u] = vec4<f32>(s.density, s.radialT, s.depthT, s.envelope);
    results[i * 2u + 1u] = vec4<f32>(s.velocity, 0.0);
}
)";

struct Gpu {
    float density = 0.0f;
    float radialT = 0.0f;
    float depthT = 0.0f;
    float envelope = 0.0f;
    glm::vec3 velocity{0.0f};
};

class Harness {
public:
    explicit Harness(gpu::Context& ctx)
        : ctx_(ctx), shaders_(ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)}) {
        // noise.wgsl first: vortex.wgsl needs fbm3 and deliberately does not include it, because
        // the include directive does not de-duplicate.
        auto noise = shaders_.loadSource("noise.wgsl");
        REQUIRE(noise.has_value());
        auto vortex = shaders_.loadSource("vortex.wgsl");
        REQUIRE(vortex.has_value());
        auto module = shaders_.compile(*noise + *vortex + kKernel, "vortex-parity");
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

    std::vector<Gpu> run(const vortex::VortexUniforms& u, float time,
                         const std::vector<glm::vec3>& positions) {
        const auto& device = ctx_.device();
        // The uniform block by VALUE, so this harness has no list of fields to fall behind.
        struct Args {
            vortex::VortexUniforms v;
            glm::vec4 time;
        } args{u, glm::vec4(time, 0.0f, 0.0f, 0.0f)};
        static_assert(sizeof(Args) == sizeof(vortex::VortexUniforms) + sizeof(glm::vec4),
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
            out[i].depthT = raw[i * 2].z;
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

// The Tree of Life's funnel, which is the one configuration that ships, so parity is asserted about
// the shape somebody is actually looking at rather than about a rounder one.
vortex::VortexField shipped() {
    vortex::VortexField f;
    f.center = {0.0f, -70.0f, 0.0f};
    f.radius = 200.0f;
    f.thickness = 70.0f;
    f.funnelDepth = 1500.0f;
    f.throat = 0.1f;
    f.throatDensity = 0.55f;
    f.swirl = 6.5f;
    f.rotationSpeed = 0.028f;
    f.innerVoid = 0.24f;
    f.contrast = 3.6f;
    f.turbulence = 0.65f;
    f.turbulenceScale = 2.4f;
    f.breathAmount = 0.05f;
    f.breathSpeed = 0.18f;
    return f;
}

// A spread that lands inside the wall, in the void, past the rim, above the mouth and down the
// throat -- every branch of the function, including all four early-outs.
std::vector<glm::vec3> positions() {
    std::vector<glm::vec3> out;
    for (int i = 0; i < 96; ++i) {
        const float a = static_cast<float>(i) * 0.61803f * 6.2831853f;
        const float r = static_cast<float>(i) * 3.4f;      // 0 .. 323 m: void, wall, past the rim
        const float y = -70.0f + 120.0f - static_cast<float>(i) * 18.0f; // above the mouth to -1600
        out.emplace_back(std::cos(a) * r, y, std::sin(a) * r);
    }
    out.emplace_back(0.0f, -70.0f, 0.0f);      // the axis, at the mouth: the void
    out.emplace_back(0.0f, 400.0f, 0.0f);      // far above: `below` must kill it
    out.emplace_back(900.0f, -70.0f, 900.0f);  // far outside: the rr > 1.35 early-out
    out.emplace_back(140.0f, -90.0f, 0.0f);    // squarely in the wall
    return out;
}

} // namespace

TEST_CASE("the vortex shader agrees with core/vortex.cpp", "[gpu][vortex][parity][determinism]") {
    auto ctx = makeContext();
    Harness harness(*ctx);
    const vortex::VortexUniforms u = vortex::packVortex(shipped());
    const std::vector<glm::vec3> pts = positions();

    // Several times, because every temporal term in this field is also a spatial one and a parity
    // that only holds at t=0 is a parity that has not tested the rotation, the breath or the three
    // noise rates.
    int nonZero = 0;
    int moving = 0;
    for (const float t : {0.0f, 6.0f, 41.7f}) {
        const std::vector<Gpu> gpu = harness.run(u, t, pts);
        REQUIRE(gpu.size() == pts.size());
        for (std::size_t i = 0; i < pts.size(); ++i) {
            const vortex::VortexSample cpu = vortex::sampleVortex(u, pts[i], t);
            INFO("t=" << t << " p=(" << pts[i].x << ", " << pts[i].y << ", " << pts[i].z << ")");
            // 1e-3 rather than 1e-4: three octaves of value noise at three scales accumulate more
            // rounding than a closed-form field does, and `fields.wgsl`'s own parity test uses the
            // same relaxed bound for its noise kinds for the same reason.
            CHECK(gpu[i].density == Approx(cpu.density).margin(1e-3));
            CHECK(gpu[i].envelope == Approx(cpu.envelope).margin(1e-3));
            CHECK(gpu[i].radialT == Approx(cpu.radialT).margin(1e-3));
            CHECK(gpu[i].depthT == Approx(cpu.depthT).margin(1e-3));
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
    // passes perfectly and proves nothing. These say the samples reached the inside of the funnel
    // and that the velocity is not the zero vector it is outside it.
    CHECK(nonZero > 20);
    CHECK(moving > 20);
}

TEST_CASE("the vortex field is a pure function of position and time", "[gpu][vortex][determinism]") {
    // ADR-091's requirement, stated as the property that makes it true: evaluating out of order,
    // repeatedly, at times visited in any sequence, gives the same answer every time. A field that
    // integrated a frame delta could not pass this, and integrating a frame delta is the only way
    // this guarantee has ever been lost.
    const vortex::VortexUniforms u = vortex::packVortex(shipped());
    const glm::vec3 p(140.0f, -90.0f, 0.0f);
    const vortex::VortexSample forward = vortex::sampleVortex(u, p, 12.0f);
    for (const float t : {0.0f, 41.7f, 3.5f, 12.0f, 99.0f, 12.0f}) {
        const vortex::VortexSample s = vortex::sampleVortex(u, p, t);
        if (t == 12.0f) {
            CHECK(s.density == forward.density);
            CHECK(s.velocity.x == forward.velocity.x);
        }
    }
    // THE CONTROL: the field is not simply constant in time, so the equality above is a statement
    // about determinism and not about a function that ignores its argument.
    CHECK(vortex::sampleVortex(u, p, 0.0f).density != Approx(forward.density));
}

TEST_CASE("a vortex with no radius is off, and costs nothing to ask", "[gpu][vortex]") {
    vortex::VortexField f = shipped();
    f.radius = 0.0f;
    const vortex::VortexUniforms u = vortex::packVortex(f);
    // Radius 0 is the gate and it is the default: every scene that has not asked for a vortex takes
    // this path, and it must return before any noise is evaluated.
    for (const glm::vec3& p : positions()) {
        const vortex::VortexSample s = vortex::sampleVortex(u, p, 6.0f);
        CHECK(s.density == 0.0f);
        CHECK(s.envelope == 0.0f);
        CHECK(s.velocity == glm::vec3(0.0f));
    }
    CHECK_FALSE(vortex::VortexField{}.active());
}

// ---- ADR-401 -----------------------------------------------------------------------------------

namespace {

// The shipped funnel with ADR-389's smoke on. Nothing in `examples/` authors `smokeWarp` or
// `smokeBillow` yet -- they are new and default to 0 -- so the arm above, which is deliberately the
// configuration that ships, exercises neither the domain warp nor the billow. This one does. A
// parity test that covers only the paths already in use is a parity test that goes red the first
// time somebody turns a feature on.
vortex::VortexField smoky() {
    vortex::VortexField f = shipped();
    f.smokeWarp = 0.35f;
    f.smokeBillow = 0.2f;
    f.detail = 0.8f;
    return f;
}

// A spread that lands INSIDE the wall, densely, which `positions()` deliberately does not: that one
// covers every branch including the four early-outs, so most of it sits where the answer is zero.
// Zero agrees with zero for reasons that have nothing to do with the noise or the curve, so a probe
// about the transfer function needs samples where there is something to transfer.
std::vector<glm::vec3> wallPositions() {
    std::vector<glm::vec3> out;
    // Placed from the geometry rather than by eye. `wall` is `exp(-rel.y^2 / thickness^2)` with the
    // centre at y = -70 and thickness 70, so the band that is actually dense is y in [-140, 0] --
    // NOT deep down the funnel, where `wall` has decayed to nothing and only the narrow throat term
    // survives. A first version of this spread ran to y = -716 and produced 37 live samples out of
    // 480; this one is the same count of points aimed at the shell they are meant to be in.
    for (int i = 0; i < 160; ++i) {
        const float a = static_cast<float>(i) * 0.61803f * 6.2831853f;
        // 0.32..0.92 of the 200 m radius: outside the 0.24 void, inside the rim's 0.72 fade.
        const float rr = 0.32f + 0.60f * (static_cast<float>(i % 20) / 19.0f);
        const float r = 200.0f * rr;
        // Across the wall's own thickness, centred on it.
        const float y = -70.0f + (static_cast<float>(i % 17) - 8.0f) * 8.0f;
        out.emplace_back(std::cos(a) * r, y, std::sin(a) * r);
    }
    return out;
}

// How many of `a` differ from `b` by more than a float's worth of rounding.
std::size_t movedCount(const std::vector<Gpu>& a, const std::vector<Gpu>& b) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) {
        if (std::abs(a[i].density - b[i].density) > 1e-4f) {
            ++n;
        }
    }
    return n;
}

} // namespace

TEST_CASE("the vortex shader agrees with core/vortex.cpp with the smoke on", "[gpu][vortex][parity]") {
    auto ctx = makeContext();
    Harness harness(*ctx);
    const vortex::VortexUniforms u = vortex::packVortex(smoky());
    const std::vector<glm::vec3> pts = wallPositions();

    int warped = 0;
    for (const float t : {0.0f, 6.0f, 41.7f}) {
        const std::vector<Gpu> gpu = harness.run(u, t, pts);
        REQUIRE(gpu.size() == pts.size());
        for (std::size_t i = 0; i < pts.size(); ++i) {
            const vortex::VortexSample cpu = vortex::sampleVortex(u, pts[i], t);
            INFO("t=" << t << " p=(" << pts[i].x << ", " << pts[i].y << ", " << pts[i].z << ")");
            CHECK(gpu[i].density == Approx(cpu.density).margin(1e-3));
            CHECK(gpu[i].envelope == Approx(cpu.envelope).margin(1e-3));
            if (cpu.density > 1e-4) {
                ++warped;
            }
        }
    }
    // The smoke arm has to be a different field from the shipped one, or it is the shipped arm
    // again under another name and covers nothing new.
    CHECK(warped > 250);
    const std::vector<Gpu> plain = harness.run(vortex::packVortex(shipped()), 6.0f, pts);
    const std::vector<Gpu> smoke = harness.run(u, 6.0f, pts);
    INFO("smoke against shipped: " << movedCount(plain, smoke) << " of " << pts.size() << " samples move");
    CHECK(movedCount(plain, smoke) > 100);
}

TEST_CASE("every number the vortex uniform carries reaches the shader", "[gpu][vortex][parity]") {
    // **This is the probe that would have caught ADR-401's defect, and the obvious one would not.**
    //
    // The harness used to copy the uniform block into the kernel field by field, and ADR-389 added
    // `v6` -- smokeWarp, smokeBillow, detail -- to the real struct on both sides and to that list on
    // neither. `var v: VortexUniformsWgsl` zero-initialises, so the GPU ran with the domain warp,
    // the billow and the third octave switched off while the CPU ran with them on, and 75
    // assertions failed with no arithmetic disagreeing anywhere.
    //
    // A probe that perturbs the CONTRAST CURVE -- the natural suspicion, since that is what changed
    // last -- passes straight through that defect, because both sides had the same contrast. What
    // catches it is ADR-387's rule stated per field: a correct value is not a reached value, so
    // every number the block carries must be shown to move the GPU's answer. A field that reaches
    // nothing is indistinguishable, from inside the parity comparison, from a field that agrees.
    auto ctx = makeContext();
    Harness harness(*ctx);
    const std::vector<glm::vec3> pts = wallPositions();
    constexpr float kT = 6.0f;

    const vortex::VortexField base = smoky();
    const std::vector<Gpu> reference = harness.run(vortex::packVortex(base), kT, pts);

    struct Knob {
        const char* name;
        float vortex::VortexField::*field;
        float to;
    };
    // Every authored number `vortexEvaluate` reads. `detail`, `smokeWarp` and `smokeBillow` are the
    // three that were unreachable; the rest are here because the next field to go missing will not
    // be one of those three.
    const Knob knobs[] = {
        {"radius", &vortex::VortexField::radius, 260.0f},
        {"thickness", &vortex::VortexField::thickness, 95.0f},
        {"swirl", &vortex::VortexField::swirl, 3.1f},
        {"rotationSpeed", &vortex::VortexField::rotationSpeed, 0.14f},
        {"innerVoid", &vortex::VortexField::innerVoid, 0.42f},
        {"contrast", &vortex::VortexField::contrast, 2.1f},
        {"turbulence", &vortex::VortexField::turbulence, 0.15f},
        {"turbulenceScale", &vortex::VortexField::turbulenceScale, 4.8f},
        {"breathAmount", &vortex::VortexField::breathAmount, 0.6f},
        {"funnelDepth", &vortex::VortexField::funnelDepth, 700.0f},
        {"throat", &vortex::VortexField::throat, 0.45f},
        {"throatDensity", &vortex::VortexField::throatDensity, 0.95f},
        {"smokeWarp", &vortex::VortexField::smokeWarp, 0.9f},
        {"smokeBillow", &vortex::VortexField::smokeBillow, 0.15f},
        {"detail", &vortex::VortexField::detail, 0.05f},
    };

    for (const Knob& k : knobs) {
        vortex::VortexField f = base;
        f.*(k.field) = k.to;
        const std::vector<Gpu> moved = harness.run(vortex::packVortex(f), kT, pts);
        const std::size_t n = movedCount(reference, moved);
        INFO("changing " << k.name << " moved " << n << " of " << pts.size() << " GPU samples");
        CHECK(n > 0);
    }

    // The control for the loop itself: re-running the SAME field must move nothing. Without it,
    // a harness that returned fresh noise every call would satisfy every assertion above.
    const std::vector<Gpu> again = harness.run(vortex::packVortex(base), kT, pts);
    INFO("the same field twice moved " << movedCount(reference, again) << " samples");
    CHECK(movedCount(reference, again) == 0);
}

TEST_CASE("the vortex parity comparison can fail on the transfer function", "[gpu][vortex][parity]") {
    // ADR-401, and the check the coordinator asked for: the margin is 1e-3 because three octaves of
    // value noise accumulate rounding, and a margin chosen for rounding has to be shown to still
    // reject a real difference in the curve. So the GPU runs one contrast and the CPU is asked
    // about another, by 5%, and the comparison has to say no.
    //
    // `contrast` is the right knob for this because ADR-389 changed the transfer function itself --
    // `pow(n, c)` became a smoothstep of width `1/c` with a `2/(c+1)` compensation -- and a drift
    // between the two sides there is exactly the failure that would look like correct shape and
    // wrong magnitude.
    auto ctx = makeContext();
    Harness harness(*ctx);
    const std::vector<glm::vec3> pts = wallPositions();
    constexpr float kT = 6.0f;

    vortex::VortexField perturbed = smoky();
    perturbed.contrast = smoky().contrast * 1.05f;

    const std::vector<Gpu> gpu = harness.run(vortex::packVortex(smoky()), kT, pts);
    const vortex::VortexUniforms cpuUniforms = vortex::packVortex(perturbed);

    int rejected = 0;
    int live = 0;
    for (std::size_t i = 0; i < pts.size(); ++i) {
        const vortex::VortexSample cpu = vortex::sampleVortex(cpuUniforms, pts[i], kT);
        if (cpu.density > 1e-3 || gpu[i].density > 1e-3) {
            ++live;
            if (gpu[i].density != Approx(cpu.density).margin(1e-3)) {
                ++rejected;
            }
        }
    }
    INFO("a 5% error in the contrast curve: " << rejected << " of " << live
                                              << " live samples rejected");
    // Live samples only: most of this spread is outside the funnel, where both sides are zero and
    // agree for a reason that has nothing to do with the curve.
    REQUIRE(live > 100);
    CHECK(rejected > live / 2);
}

