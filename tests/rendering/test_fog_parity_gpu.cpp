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
#include "world/effects/effect_registry.hpp"

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

// The kernel reads the same lanes `mediumFogUniforms` hands the field in the march, in the same
// order, so this cannot pass by assembling them differently from the renderer.
//
// ADR-713/714: the harness now takes the WHOLE slot and assembles the struct exactly as
// `mediumFogUniforms` does. It used to take the seven lanes by name, which was right until the
// struct grew -- and a harness that hands the field a lane list of its own is a second copy of the
// assembly that can drift from the first.
constexpr const char* kKernel = R"(
struct Args {
    lanes: array<vec4<f32>, 16>,
    time: vec4<f32>,   // x = t, yzw = the camera position the distance colour measures from
    base: vec4<f32>,   // rgb = the depth colour the tint is applied to
};
@group(0) @binding(0) var<uniform> args: Args;
@group(0) @binding(1) var<storage, read> samples: array<vec4<f32>>;
@group(0) @binding(2) var<storage, read_write> results: array<vec4<f32>>;

@compute @workgroup_size(64)
fn cs_sample(@builtin(global_invocation_id) gid: vec3<u32>) {
    let i = gid.x;
    if (i >= arrayLength(&samples)) { return; }
    let l = args.lanes;
    let f = FogUniformsWgsl(l[0], l[13], l[14], l[7], l[12], l[2], l[6], l[1], l[3], l[5], l[8], l[9],
                            l[10], l[11]);
    let p = samples[i].xyz;
    let t = args.time.x;
    let rel = p - f.f0.xyz;
    // ADR-566: the PRIMITIVE distance, which is what the march calls -- not the bank's ellipse,
    // which is one arm of it. A parity test aimed at the arm the dispatch no longer takes is
    // ADR-565's entry 22 written a second time, and this pair is where it would be least visible.
    // ADR-575: TWO vec4s per sample now. The emission's height influence is a term the march
    // evaluates on the GPU, and a parity harness that does not read it is a pair with a gap --
    // which was found by breaking the shader and watching this test pass.
    results[i * 4u] = vec4<f32>(fogShapeAt(f, p, t), fogPrimitiveDistance(f, rel),
                                fogVerticalProfile(f, rel.y), fogMacroDetail(f, p, t));
    // ADR-713/714: FOUR vec4s per sample. ADR-575's lesson, applied before rather than after: every
    // term the march evaluates is a quantity this harness reads, or the pair has a gap.
    let camDist = distance(p, args.time.yzw);
    results[i * 4u + 1u] = vec4<f32>(fogEmissionHeight(f, rel.y), fogSwell(f, t),
                                     fogHeightColourWeight(f, rel.y), fogDistanceColourWeight(f, camDist));
    results[i * 4u + 2u] = vec4<f32>(fogTurbulence(f, p, t), 0.0);
    results[i * 4u + 3u] = vec4<f32>(fogTintedColour(f, args.base.rgb, rel.y, camDist),
                                     fogStructureFrame(f, p, t).x);
}
)";

struct Gpu {
    float shape = 0.0f;
    float distance = 0.0f;
    float vertical = 0.0f;
    float detail = 0.0f;
    float emissionHeight = 0.0f; // ADR-575 (§26)
    // ADR-713 (§16)
    float swell = 0.0f;
    glm::vec3 turbulence{0.0f};
    float structureX = 0.0f;
    // ADR-714 (§25)
    float heightWeight = 0.0f;
    float distanceWeight = 0.0f;
    glm::vec3 tinted{0.0f};
};

// The camera the distance colour is measured from, and the base colour the tint is applied to.
const glm::vec3 kCamera(-900.0f, 60.0f, 350.0f);
const glm::vec3 kBase(0.09f, 0.21f, 0.17f);

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
            glm::vec4 lanes[16];
            glm::vec4 time;
            glm::vec4 base;
        } args{};
        for (int l = 0; l < 16; ++l) {
            args.lanes[l] = m.lane[l];
        }
        args.time = glm::vec4(time, kCamera);
        args.base = glm::vec4(kBase, 0.0f);
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
        rdesc.size = padded.size() * sizeof(glm::vec4) * 4; // ADR-713/714: four vec4s per sample
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
        std::vector<glm::vec4> raw(padded.size() * 4);
        std::memcpy(raw.data(), bytes->data(), rdesc.size);
        std::vector<Gpu> out(padded.size());
        for (std::size_t i = 0; i < padded.size(); ++i) {
            const glm::vec4* r = &raw[i * 4];
            Gpu g;
            g.shape = r[0].x;
            g.distance = r[0].y;
            g.vertical = r[0].z;
            g.detail = r[0].w;
            g.emissionHeight = r[1].x;
            g.swell = r[1].y;
            g.heightWeight = r[1].z;
            g.distanceWeight = r[1].w;
            g.turbulence = glm::vec3(r[2]);
            g.tinted = glm::vec3(r[3]);
            g.structureX = r[3].w;
            out[i] = g;
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
    const world::EffectSchema* s = world::effectSchema(world::EffectKind::VolumetricFog);
    REQUIRE(s != nullptr);
    REQUIRE(s->resolve.pack != nullptr);
    world::EffectInstance e = s->factory("parity");
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
    // ADR-571: a drift with a real velocity, so the parity harness exercises the advection.
    e.values.setFloat("fog/driftSpeed", 3.5f);
    e.values.setFloat("fog/driftVertical", -0.8f);
    // ADR-571 §24: every term of the density curve off its identity, so a shader that dropped one
    // of the three cannot pass.
    e.values.setFloat("fog/densityThreshold", 0.12f);
    e.values.setFloat("fog/densitySoftness", 0.6f);
    e.values.setFloat("fog/emissionHeight", 0.7f); // ADR-575 §26, off both ends of its range
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
            CHECK(gpu[i].emissionHeight ==
                  Approx(world::fogEmissionHeight(slot, rel.y)).margin(1e-4));
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

TEST_CASE("the fog shader agrees about the flow and the colour", "[gpu][fog][parity][flow][colour]") {
    // ADR-713 (§16) and ADR-714 (§25). Every new term is compared as its OWN quantity -- the swell,
    // the turbulence displacement, the swirl's rest frame, both colour weights and the tinted colour
    // -- and through the whole field, on three primitives: the Bank (horizontal-only swell), the
    // Sphere (swells in every direction, isotropic semi-axes) and the Capsule (the one whose
    // turbulence reach is not 1). A parity case that compared only `fogShapeAt` could pass with a
    // turbulence that both sides got equally wrong in a way the field happened to hide.
    auto ctx = makeContext();
    Harness harness(*ctx);
    const std::vector<glm::vec3> pts = positions();
    for (const int shape : {0, 1, 4}) {
        INFO("shape index " << shape);
        world::MediumSlot slot = shippedBank(0.6f, shape, 0.4f);
        // Every flow and colour control off its default, directly in the lanes the packer fills,
        // so this is a test of the pair rather than of the packer (the packer has its own).
        slot.lane[1].z = 0.043f;                            // swirl, rad/s
        slot.lane[3].x = 0.21f;                             // swell amount
        slot.lane[3].y = 0.37f;                             // swell rate
        slot.lane[7].y = 0.33f;                             // turbulence amount
        slot.lane[7].w = 2.2f;                              // turbulence scale
        slot.lane[6].w = 0.6f;                              // turbulence rate
        slot.lane[5].w = 0.8f;                              // height colour amount
        slot.lane[9].w = 0.9f;                              // height colour r
        slot.lane[10].w = 0.3f;                             //               g
        slot.lane[11].w = 0.1f;                             //               b
        slot.lane[8] = glm::vec4(0.1f, 0.2f, 0.7f, 0.65f); // distance colour + amount
        slot.lane[2].w = 750.0f;                            // distance colour range
        int inside = 0;
        int displaced = 0;
        int tinted = 0;
        for (const float t : {0.0f, 7.5f, 53.0f}) {
            const std::vector<Gpu> gpu = harness.run(slot, t, pts);
            REQUIRE(gpu.size() == pts.size());
            for (std::size_t i = 0; i < pts.size(); ++i) {
                const glm::vec3 rel = pts[i] - glm::vec3(slot.lane[0]);
                const float camDist = glm::distance(pts[i], kCamera);
                INFO("t=" << t << " p=(" << pts[i].x << ", " << pts[i].y << ", " << pts[i].z << ")");
                const float cpuShape = world::fogShapeAt(slot, pts[i], t);
                CHECK(gpu[i].shape == Approx(cpuShape).margin(2e-3));
                CHECK(gpu[i].swell == Approx(world::fogSwell(slot, t)).margin(1e-5));
                CHECK(gpu[i].structureX == Approx(world::fogStructureFrame(slot, pts[i], t).x).margin(2e-2));
                const glm::vec3 d = world::fogTurbulence(slot, pts[i], t);
                // Metres, on semi-axes of hundreds of metres: 0.05 m is a relative 1e-4.
                CHECK(gpu[i].turbulence.x == Approx(d.x).margin(0.05));
                CHECK(gpu[i].turbulence.y == Approx(d.y).margin(0.05));
                CHECK(gpu[i].turbulence.z == Approx(d.z).margin(0.05));
                CHECK(gpu[i].heightWeight == Approx(world::fogHeightColourWeight(slot, rel.y)).margin(1e-4));
                CHECK(gpu[i].distanceWeight ==
                      Approx(world::fogDistanceColourWeight(slot, camDist)).margin(1e-4));
                const glm::vec3 c = world::fogTintedColour(slot, kBase, rel.y, camDist);
                CHECK(gpu[i].tinted.r == Approx(c.r).margin(1e-4));
                CHECK(gpu[i].tinted.g == Approx(c.g).margin(1e-4));
                CHECK(gpu[i].tinted.b == Approx(c.b).margin(1e-4));
                inside += cpuShape > 1e-4f ? 1 : 0;
                displaced += glm::length(d) > 1.0f ? 1 : 0;
                tinted += glm::length(c - kBase) > 1e-3f ? 1 : 0;
            }
        }
        // The controls: the samples reached the bank, the turbulence moved them by metres, and the
        // tint changed the colour -- a parity over three zeroes is no parity at all (ADR-182).
        INFO("inside " << inside << " displaced " << displaced << " tinted " << tinted);
        CHECK(inside > 20);
        CHECK(displaced > 100);
        CHECK(tinted > 100);
    }
}
