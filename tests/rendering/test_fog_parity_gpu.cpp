// CPU/GPU parity of the fog field (ADR-565). `shaders/fog.wgsl` claims to be a transliteration of
// `src/world/fog_field.cpp`; this makes it earn the claim.
//
// **Why it exists, and it is not a precaution.** Within a day of writing the pair I added the macro
// detail term to the shader and not to the CPU side, and the two disagreed for ten minutes. It was
// caught only because an unrelated probe kept failing after the shader had the term — nothing in
// the suite was watching the pair itself.
//
// The general form: **a CPU/GPU pair with no parity test is two implementations with a
// convention.** `core/vortex.cpp` ↔ `shaders/vortex.wgsl` has one, and ADR-388/401 record what its
// absence cost there — `packVortex` with a single caller while the bytes the frame marched were
// produced by an untested hand-written copy.
//
// And the timing argument, which is why this lands before the detail term is tuned rather than
// after: the vortex's parity test exists **because noise is where two transliterations drift
// silently**. A closed-form disagreement shows up as a shape; a noise disagreement shows up as a
// different grain that looks like a tuning choice. Tuning against a CPU side that already disagrees
// with the GPU side you are looking at is the failure this forecloses.
//
// The comparison goes through the PACKED LANES, which is the whole reason `MediumSlot` exists as a
// separate step: both sides start from bytes that are identical by construction, so a disagreement
// is about the maths and never about how a parameter was interpreted on the way in.

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "world/atmospherics.hpp"
#include "world/world_effects/effect_registry.hpp"

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

// The kernel reads the same four lanes `mediumFogUniforms` hands the field in the march, in the
// same order, so this cannot pass by assembling them differently from the renderer.
constexpr const char* kKernel = R"(
struct Args {
    f0: vec4<f32>,
    f1: vec4<f32>,
    f2: vec4<f32>,
    f3: vec4<f32>,
    f4: vec4<f32>,
    time: vec4<f32>,
};
@group(0) @binding(0) var<uniform> args: Args;
@group(0) @binding(1) var<storage, read> samples: array<vec4<f32>>;
@group(0) @binding(2) var<storage, read_write> results: array<vec4<f32>>;

@compute @workgroup_size(64)
fn cs_sample(@builtin(global_invocation_id) gid: vec3<u32>) {
    let i = gid.x;
    if (i >= arrayLength(&samples)) { return; }
    let f = FogUniformsWgsl(args.f0, args.f1, args.f2, args.f3, args.f4);
    let p = samples[i].xyz;
    let t = args.time.x;
    let rel = p - f.f0.xyz;
    // ADR-566: the PRIMITIVE distance, which is what the march calls -- not the bank's ellipse,
    // which is one arm of it. A parity test aimed at the arm the dispatch no longer takes is
    // ADR-565's entry 22 written a second time, and this pair is where it would be least visible.
    results[i] = vec4<f32>(fogShapeAt(f, p, t), fogPrimitiveDistance(f, rel),
                           fogVerticalProfile(f, rel.y), fogMacroDetail(f, p, t));
}
)";

struct Gpu {
    float shape = 0.0f;
    float distance = 0.0f;
    float vertical = 0.0f;
    float detail = 0.0f;
};

class Harness {
public:
    explicit Harness(gpu::Context& ctx)
        : ctx_(ctx), shaders_(ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)}) {
        // noise.wgsl first: fog.wgsl needs fbm3 for the macro detail and deliberately does not
        // include it, because the include directive does not de-duplicate (ADR-360).
        auto noise = shaders_.loadSource("noise.wgsl");
        REQUIRE(noise.has_value());
        auto fog = shaders_.loadSource("fog.wgsl");
        REQUIRE(fog.has_value());
        auto module = shaders_.compile(*noise + *fog + kKernel, "fog-parity");
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

    std::vector<Gpu> run(const world::MediumSlot& m, float time,
                         const std::vector<glm::vec3>& positions) {
        const auto& device = ctx_.device();
        // The four lanes the march hands the field, by value, so this harness has no list to fall
        // behind — the same property the vortex harness gets from passing its uniform block whole.
        struct Args {
            glm::vec4 f0, f1, f2, f3, f4, time;
        } args{m.lane[0],  m.lane[13], m.lane[14], m.lane[7], m.lane[12],
               glm::vec4(time, 0.0f, 0.0f, 0.0f)};
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
        rdesc.size = padded.size() * sizeof(glm::vec4);
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
        std::vector<glm::vec4> raw(padded.size());
        std::memcpy(raw.data(), bytes->data(), rdesc.size);
        std::vector<Gpu> out(padded.size());
        for (std::size_t i = 0; i < padded.size(); ++i) {
            out[i] = Gpu{raw[i].x, raw[i].y, raw[i].z, raw[i].w};
        }
        return out;
    }

private:
    gpu::Context& ctx_;
    gpu::ShaderLibrary shaders_;
    wgpu::BindGroupLayout layout_;
    wgpu::ComputePipeline pipeline_;
};

// An authored bank with every control off its default, so a lane the shader reads from the wrong
// place cannot pass by both sides seeing the same number.
world::MediumSlot shippedBank(float detail, int shape = 0, float heightInfluence = 0.0f) {
    const world::EffectSchema* s = world::effectSchema(world::AtmosphereKind::VolumetricFog);
    REQUIRE(s != nullptr);
    REQUIRE(s->resolve.pack != nullptr);
    world::AtmosphericEffect e = s->factory("parity");
    e.vortex.field.center = {12.0f, -40.0f, -7.0f};
    e.vortex.field.radius = 430.0f;
    e.vortex.field.thickness = 96.0f;
    e.vortex.field.cloudNoise = detail;
    e.vortex.density = 2.4f;
    e.values.setFloat("fog/bankLength", 2.3f);
    e.values.setFloat("fog/bankRotation", 37.0f);
    e.values.setFloat("fog/edgeSoftness", 0.42f);
    e.values.setFloat("fog/groundHug", 0.31f);
    e.values.setFloat("fog/heightFalloff", 1.9f);
    e.values.setFloat("fog/domeShape", 0.28f);
    e.values.setFloat("fog/detailScale", 7.5f);
    e.values.setFloat("fog/detailDrift", 0.04f);
    e.values.setFloat("fog/shape", static_cast<float>(shape));
    e.values.setFloat("fog/heightInfluence", heightInfluence);
    world::MediumSlot slot{};
    // ADR-566: the one writer, so the bytes here are the bytes the frame marches -- including the
    // kind tag, which `pack` alone does not write.
    world::packMediumSlot(e, 1.0f, slot);
    return slot;
}

std::vector<glm::vec3> positions() {
    std::vector<glm::vec3> pts;
    const glm::vec3 c(12.0f, -40.0f, -7.0f);
    for (int i = 0; i < 9; ++i) {
        const float rr = 0.14f * static_cast<float>(i);
        for (int k = 0; k < 8; ++k) {
            const float a = 6.2831853f * static_cast<float>(k) / 8.0f;
            for (const float dy : {-140.0f, -30.0f, 0.0f, 55.0f, 190.0f}) {
                pts.push_back(c + glm::vec3(430.0f * rr * std::cos(a), dy,
                                            430.0f * rr * std::sin(a)));
            }
        }
    }
    return pts;
}

} // namespace

TEST_CASE("the fog shader agrees with world/fog_field.cpp", "[gpu][fog][parity][determinism]") {
    auto ctx = makeContext();
    Harness harness(*ctx);
    const std::vector<glm::vec3> pts = positions();

    // Detail ON, because the noise term is the half that drifts silently and the half this test
    // exists for. Detail OFF is covered below as the analytic control.
    const world::MediumSlot slot = shippedBank(0.6f);

    int inside = 0;
    int detailMoved = 0;
    // Several times: the detail term drifts with `t`, so a parity that only holds at t=0 has not
    // tested the one term that moves.
    for (const float t : {0.0f, 6.0f, 41.7f}) {
        const std::vector<Gpu> gpu = harness.run(slot, t, pts);
        REQUIRE(gpu.size() == pts.size());
        for (std::size_t i = 0; i < pts.size(); ++i) {
            const glm::vec3 rel = pts[i] - glm::vec3(slot.lane[0]);
            INFO("t=" << t << " p=(" << pts[i].x << ", " << pts[i].y << ", " << pts[i].z << ")");
            // 1e-3 for the shape, as the vortex's parity uses for its noise kinds: one octave of
            // value noise accumulates more rounding than a closed form does.
            CHECK(gpu[i].shape == Approx(world::fogShapeAt(slot, pts[i], t)).margin(1e-3));
            CHECK(gpu[i].distance == Approx(world::fogPrimitiveDistance(slot, rel)).margin(1e-4));
            CHECK(gpu[i].vertical == Approx(world::fogVerticalProfile(slot, rel.y)).margin(1e-4));
            CHECK(gpu[i].detail == Approx(world::fogMacroDetail(slot, pts[i], t)).margin(1e-3));
            if (world::fogShapeAt(slot, pts[i], t) > 1e-4f) {
                ++inside;
            }
            if (std::abs(world::fogMacroDetail(slot, pts[i], t) - 1.0f) > 1e-3f) {
                ++detailMoved;
            }
        }
    }
    // THE CONTROLS, and ADR-182's point exactly: a parity test over a field that is zero everywhere
    // passes perfectly and proves nothing. These say the samples reached the inside of the bank,
    // and that the detail term is not the constant 1.0 it is when switched off.
    CHECK(inside > 40);
    CHECK(detailMoved > 40);
}

TEST_CASE("the fog field is a pure function of position and time", "[gpu][fog][determinism]") {
    // ADR-091, stated as the property that makes it true: evaluating out of order, repeatedly, at
    // times visited in any sequence, gives the same answer. A field that integrated a frame delta
    // could not pass this, and integrating a frame delta is the only way this guarantee is lost.
    const world::MediumSlot slot = shippedBank(0.6f);
    const glm::vec3 p(120.0f, -35.0f, 40.0f);
    const float forward = world::fogShapeAt(slot, p, 12.0f);
    for (const float t : {0.0f, 41.7f, 3.5f, 12.0f, 99.0f, 12.0f}) {
        const float s = world::fogShapeAt(slot, p, t);
        if (t == 12.0f) {
            CHECK(s == forward);
        }
    }
}


TEST_CASE("the fog shader agrees about all six primitives", "[gpu][fog][parity][primitive]") {
    // ADR-566 put a dispatch in front of the field, and a dispatch is exactly where a
    // transliteration stops being one: five of these six arms did not exist an hour ago and the
    // case above samples only the arm that did. docs/testing.md 22 is the general form -- a probe
    // written before a dispatch goes on passing about a path nothing takes.
    //
    // The detail term is left ON so that the shapes are compared through the whole field rather
    // than through a closed form, which is the half that drifts silently.
    auto ctx = makeContext();
    Harness harness(*ctx);
    const std::vector<glm::vec3> pts = positions();
    for (int shape = 0; shape < world::kFogShapeCount; ++shape) {
        INFO("shape index " << shape);
        // Height influence at 0.65: neither end of its range, so a side that ignored the control
        // entirely and a side that applied it whole are both visible.
        const world::MediumSlot slot = shippedBank(0.6f, shape, 0.65f);
        REQUIRE(static_cast<int>(world::fogShapeKindOf(slot)) == shape);
        int inside = 0;
        for (const float t : {0.0f, 19.25f}) {
            const std::vector<Gpu> gpu = harness.run(slot, t, pts);
            REQUIRE(gpu.size() == pts.size());
            for (std::size_t i = 0; i < pts.size(); ++i) {
                const glm::vec3 rel = pts[i] - glm::vec3(slot.lane[0]);
                INFO("t=" << t << " p=(" << pts[i].x << ", " << pts[i].y << ", " << pts[i].z << ")");
                CHECK(gpu[i].shape == Approx(world::fogShapeAt(slot, pts[i], t)).margin(1e-3));
                CHECK(gpu[i].distance ==
                      Approx(world::fogPrimitiveDistance(slot, rel)).margin(1e-4));
                if (world::fogShapeAt(slot, pts[i], t) > 1e-4f) {
                    ++inside;
                }
            }
        }
        // The control. A shape whose samples all land outside it agrees with the GPU perfectly and
        // says nothing -- and with six shapes over one set of sample points that is a live risk,
        // not a formality.
        INFO("samples inside this primitive: " << inside);
        CHECK(inside > 20);
    }
}
