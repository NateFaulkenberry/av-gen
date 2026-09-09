// CPU/GPU parity of the field library (ADR-025): a compute harness uploads sample positions and
// a packed FieldBlock, evaluates fieldScalar / fieldVector / fieldColor / fieldWeight from
// shaders/fields.wgsl for every FieldKind (and every FalloffKind on a Radial field, invert,
// compounds with every combine), and compares with spatial::sampleScalar / sampleVector /
// sampleColor / sampleWeight on the same FieldSpec within 1e-4 (noise kinds 1e-3).
#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/field_uniforms.hpp"
#include "spatial/field.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

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

// The harness kernel: one thread per sample; positions.w selects the slot. Output per sample:
// (scalar, weight, 0, 0), (vector, 0), colour.
constexpr const char* kKernel = R"(
@group(0) @binding(0) var<uniform> fieldBlock: FieldBlock;
@group(0) @binding(1) var<storage, read> samples: array<vec4<f32>>;
@group(0) @binding(2) var<storage, read_write> results: array<vec4<f32>>;

@compute @workgroup_size(64)
fn cs_sample(@builtin(global_invocation_id) gid: vec3<u32>) {
    let i = gid.x;
    if (i >= arrayLength(&samples)) { return; }
    let s = samples[i];
    let slot = i32(floor(s.w + 0.5));
    let p = s.xyz;
    results[i * 3u] = vec4<f32>(fieldScalar(slot, p), fieldWeight(slot, p), 0.0, 0.0);
    results[i * 3u + 1u] = vec4<f32>(fieldVector(slot, p), 0.0);
    results[i * 3u + 2u] = fieldColor(slot, p);
}
)";

struct Sample {
    float scalar = 0.0f;
    float weight = 0.0f;
    glm::vec3 vector{0.0f};
    glm::vec4 color{0.0f};
};

// Runs the harness over `positions` (xyz, w = slot) with the given field set at `time`.
class FieldHarness {
public:
    explicit FieldHarness(gpu::Context& ctx) : ctx_(ctx), shaders_(ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)}) {
        auto source = shaders_.loadSource("fields.wgsl");
        REQUIRE(source.has_value());
        auto module = shaders_.compile(*source + kKernel, "fields-harness");
        REQUIRE(module.has_value());
        const auto& device = ctx_.device();
        // Binding 15 is the simulated-grid table fields.wgsl declares (ADR-032); the harness
        // binds FieldUniforms' own (empty) table so no Grid field can resolve.
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
        entries[3].binding = 15;
        entries[3].visibility = wgpu::ShaderStage::Compute;
        entries[3].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
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

    std::vector<Sample> run(const spatial::FieldSet& fields, double time, const std::vector<glm::vec4>& positions) {
        rendering::FieldUniforms block(ctx_);
        block.update(fields, time);
        const auto& device = ctx_.device();
        wgpu::BufferDescriptor sdesc{};
        sdesc.label = "harness-samples";
        sdesc.size = positions.size() * sizeof(glm::vec4);
        sdesc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
        wgpu::Buffer samples = device.CreateBuffer(&sdesc);
        ctx_.queue().WriteBuffer(samples, 0, positions.data(), sdesc.size);
        wgpu::BufferDescriptor rdesc{};
        rdesc.label = "harness-results";
        rdesc.size = positions.size() * 3 * sizeof(glm::vec4);
        rdesc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopySrc;
        wgpu::Buffer results = device.CreateBuffer(&rdesc);
        std::array<wgpu::BindGroupEntry, 4> entries{};
        entries[0].binding = 0;
        entries[0].buffer = block.buffer();
        entries[0].size = rendering::FieldUniforms::kBufferSize;
        entries[1].binding = 1;
        entries[1].buffer = samples;
        entries[1].size = sdesc.size;
        entries[2].binding = 2;
        entries[2].buffer = results;
        entries[2].size = rdesc.size;
        entries[3].binding = 15;
        entries[3].buffer = block.gridBuffer();
        entries[3].size = rendering::FieldUniforms::kGridBufferSize;
        wgpu::BindGroupDescriptor gdesc{};
        gdesc.layout = layout_;
        gdesc.entryCount = entries.size();
        gdesc.entries = entries.data();
        wgpu::BindGroup group = device.CreateBindGroup(&gdesc);
        wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
        wgpu::ComputePassEncoder pass = encoder.BeginComputePass();
        pass.SetPipeline(pipeline_);
        pass.SetBindGroup(0, group);
        pass.DispatchWorkgroups(static_cast<std::uint32_t>((positions.size() + 63) / 64));
        pass.End();
        wgpu::CommandBuffer commands = encoder.Finish();
        ctx_.queue().Submit(1, &commands);
        auto bytes = gpu::readBuffer(ctx_, results, 0, rdesc.size);
        REQUIRE(bytes.has_value());
        std::vector<glm::vec4> raw(positions.size() * 3);
        std::memcpy(raw.data(), bytes->data(), rdesc.size);
        std::vector<Sample> out(positions.size());
        for (std::size_t i = 0; i < positions.size(); ++i) {
            out[i].scalar = raw[i * 3].x;
            out[i].weight = raw[i * 3].y;
            out[i].vector = glm::vec3(raw[i * 3 + 1]);
            out[i].color = raw[i * 3 + 2];
        }
        return out;
    }

private:
    gpu::Context& ctx_;
    gpu::ShaderLibrary shaders_;
    wgpu::BindGroupLayout layout_;
    wgpu::ComputePipeline pipeline_;
};

// A deterministic spread of sample positions around the origin (inside and outside falloffs).
std::vector<glm::vec3> samplePositions() {
    std::vector<glm::vec3> out;
    for (int i = 0; i < 48; ++i) {
        const float a = static_cast<float>(i) * 0.61803f * 6.2831853f;
        const float r = 0.3f + static_cast<float>(i) * 0.37f;
        out.emplace_back(std::cos(a) * r, std::sin(a * 1.7f) * r * 0.6f - 0.5f, std::sin(a) * r);
    }
    out.emplace_back(0.0f, 0.0f, 0.0f);
    out.emplace_back(0.0f, 10.0f, 0.0f);
    out.emplace_back(-3.0f, 2.0f, 7.5f);
    return out;
}

bool isNoiseKind(spatial::FieldKind kind) {
    using K = spatial::FieldKind;
    return kind == K::Noise || kind == K::Voronoi || kind == K::CurlNoise || kind == K::NoiseColor || kind == K::Compound;
}

float maxAbs(const glm::vec3& v) {
    return std::max(std::abs(v.x), std::max(std::abs(v.y), std::abs(v.z)));
}
float maxAbs(const glm::vec4& v) {
    return std::max(std::max(std::abs(v.x), std::abs(v.y)), std::max(std::abs(v.z), std::abs(v.w)));
}

// Compares every sample of slot `slot` in `set` between the CPU and the harness output.
void compareField(const spatial::FieldSet& set, int slot, double time, const std::vector<glm::vec3>& positions,
                  const std::vector<Sample>& gpu, std::size_t offset, float tolerance) {
    const spatial::FieldSpec& field = set.fields[static_cast<std::size_t>(slot)];
    float worst = 0.0f;
    for (std::size_t i = 0; i < positions.size(); ++i) {
        const glm::vec3& p = positions[i];
        const Sample& g = gpu[offset + i];
        const float s = spatial::sampleScalar(field, p, time, &set);
        const glm::vec3 v = spatial::sampleVector(field, p, time, &set);
        const glm::vec4 c = spatial::sampleColor(field, p, time, &set);
        const float w = spatial::sampleWeight(field, p, time);
        const float ds = std::abs(s - g.scalar);
        const float dv = maxAbs(v - g.vector);
        const float dc = maxAbs(c - g.color);
        const float dw = std::abs(w - g.weight);
        worst = std::max(worst, std::max(std::max(ds, dv), std::max(dc, dw)));
        INFO(spatial::fieldKindName(field.kind) << " '" << field.name << "' sample " << i << " p=(" << p.x << "," << p.y
                                               << "," << p.z << ") scalar cpu " << s << " gpu " << g.scalar << " vector cpu ("
                                               << v.x << "," << v.y << "," << v.z << ") gpu (" << g.vector.x << ","
                                               << g.vector.y << "," << g.vector.z << ") colour cpu (" << c.x << "," << c.y
                                               << "," << c.z << "," << c.w << ") gpu (" << g.color.x << "," << g.color.y
                                               << "," << g.color.z << "," << g.color.w << ")");
        REQUIRE(ds <= tolerance);
        REQUIRE(dv <= tolerance);
        REQUIRE(dc <= tolerance);
        REQUIRE(dw <= tolerance);
    }
    INFO(spatial::fieldKindName(field.kind) << " worst deviation " << worst);
    CHECK(worst <= tolerance);
}

// A field of `kind` with a non-trivial frame and parameters exercising its lanes.
spatial::FieldSpec makeField(spatial::FieldKind kind, const std::string& name) {
    spatial::FieldSpec f;
    f.name = name;
    f.kind = kind;
    f.position = {0.4f, -0.3f, 0.2f};
    f.rotationDegrees = {12.0f, -35.0f, 20.0f};
    f.scale = {1.2f, 0.9f, 1.1f};
    f.strength = 1.5f;
    f.falloff.kind = spatial::FalloffKind::Smoothstep;
    f.falloff.inner = 1.0f;
    f.falloff.outer = 9.0f;
    f.speed = 0.7f;
    f.phase = 0.3f;
    f.axis = {0.3f, 1.0f, -0.2f};
    f.point = {0.5f, 0.1f, -0.4f};
    f.radius = 4.0f;
    f.length = 6.0f;
    f.size = {2.0f, 1.5f, 3.0f};
    f.softness = 0.8f;
    f.frequency = 0.45f;
    f.seed = 11;
    f.spiralBias = 0.6f;
    f.waveGeometry = spatial::WaveGeometry::Radial;
    f.waveShape = spatial::WaveShape::Sine;
    f.amplitude = 1.3f;
    f.wavelength = 3.0f;
    f.waveSpeed = 2.0f;
    f.waveWidth = 5.0f;
    f.waveOrigin = 0.5f;
    f.colorA = {0.9f, 0.2f, 0.1f, 1.0f};
    f.colorB = {0.1f, 0.4f, 1.0f, 1.0f};
    return f;
}

} // namespace

TEST_CASE("Every field kind samples identically on the CPU and the GPU", "[gpu][fields]") {
    auto ctx = makeContext();
    FieldHarness harness(*ctx);
    const auto positions = samplePositions();
    const double time = 1.75;

    // Every non-compound kind, in batches of at most 16 slots.
    std::vector<spatial::FieldKind> kinds;
    for (int k = 0; k < static_cast<int>(spatial::FieldKind::Compound); ++k) {
        kinds.push_back(static_cast<spatial::FieldKind>(k));
    }
    for (std::size_t first = 0; first < kinds.size(); first += static_cast<std::size_t>(spatial::kMaxGpuFields)) {
        spatial::FieldSet set;
        for (std::size_t k = first; k < kinds.size() && k < first + static_cast<std::size_t>(spatial::kMaxGpuFields); ++k) {
            set.fields.push_back(makeField(kinds[k], std::string("f") + std::to_string(k)));
        }
        std::vector<glm::vec4> samples;
        for (std::size_t slot = 0; slot < set.fields.size(); ++slot) {
            for (const auto& p : positions) {
                samples.emplace_back(p, static_cast<float>(slot));
            }
        }
        const auto gpu = harness.run(set, time, samples);
        for (std::size_t slot = 0; slot < set.fields.size(); ++slot) {
            const float tolerance = isNoiseKind(set.fields[slot].kind) ? 1e-3f : 1e-4f;
            compareField(set, static_cast<int>(slot), time, positions, gpu, slot * positions.size(), tolerance);
        }
    }
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("Every falloff kind, wave variant and invert flag match between CPU and GPU", "[gpu][fields]") {
    auto ctx = makeContext();
    FieldHarness harness(*ctx);
    const auto positions = samplePositions();
    const double time = 0.4;
    spatial::FieldSet set;
    for (int k = 0; k <= static_cast<int>(spatial::FalloffKind::NoiseModulated); ++k) {
        auto f = makeField(spatial::FieldKind::Radial, "falloff" + std::to_string(k));
        f.falloff.kind = static_cast<spatial::FalloffKind>(k);
        f.falloff.inner = 0.5f;
        f.falloff.outer = 6.0f;
        f.falloff.exponent = 2.5f;
        f.falloff.curve = {0.9f, 0.5f, 0.2f, 0.0f};
        f.falloff.noiseAmount = 0.8f;
        f.falloff.noiseScale = 0.7f;
        f.radius = 7.0f;
        set.fields.push_back(f);
    }
    // Wave geometries x shapes (a few), inverted scalar / vector / colour fields.
    {
        auto w = makeField(spatial::FieldKind::Wave, "wavePlanarPulse");
        w.waveGeometry = spatial::WaveGeometry::Planar;
        w.waveShape = spatial::WaveShape::Pulse;
        set.fields.push_back(w);
        auto w2 = makeField(spatial::FieldKind::WaveVector, "waveSphericalTriangle");
        w2.waveGeometry = spatial::WaveGeometry::Spherical;
        w2.waveShape = spatial::WaveShape::Triangle;
        set.fields.push_back(w2);
        auto w3 = makeField(spatial::FieldKind::Wave, "waveCylindricalNoWidth");
        w3.waveGeometry = spatial::WaveGeometry::Cylindrical;
        w3.waveWidth = 0.0f;
        set.fields.push_back(w3);
        auto inv = makeField(spatial::FieldKind::Sphere, "invertedSphere");
        inv.invert = true;
        set.fields.push_back(inv);
        auto invV = makeField(spatial::FieldKind::Vortex, "invertedVortex");
        invV.invert = true;
        set.fields.push_back(invV);
        auto invC = makeField(spatial::FieldKind::Gradient, "invertedGradient");
        invC.invert = true;
        set.fields.push_back(invC);
    }
    REQUIRE(set.fields.size() <= static_cast<std::size_t>(spatial::kMaxGpuFields));
    std::vector<glm::vec4> samples;
    for (std::size_t slot = 0; slot < set.fields.size(); ++slot) {
        for (const auto& p : positions) {
            samples.emplace_back(p, static_cast<float>(slot));
        }
    }
    const auto gpu = harness.run(set, time, samples);
    for (std::size_t slot = 0; slot < set.fields.size(); ++slot) {
        const bool noisy = set.fields[slot].falloff.kind == spatial::FalloffKind::NoiseModulated;
        compareField(set, static_cast<int>(slot), time, positions, gpu, slot * positions.size(), noisy ? 1e-3f : 1e-4f);
    }
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("Compound fields combine their children identically on the GPU (one level)", "[gpu][fields]") {
    auto ctx = makeContext();
    FieldHarness harness(*ctx);
    const auto positions = samplePositions();
    const double time = 2.2;
    spatial::FieldSet set;
    set.fields.push_back(makeField(spatial::FieldKind::Radial, "a"));
    set.fields.push_back(makeField(spatial::FieldKind::LinearGradient, "b"));
    set.fields.push_back(makeField(spatial::FieldKind::Vortex, "v"));
    set.fields.push_back(makeField(spatial::FieldKind::Direction, "d"));
    set.fields.push_back(makeField(spatial::FieldKind::Gradient, "c1"));
    set.fields.push_back(makeField(spatial::FieldKind::ConstantColor, "c2"));
    for (int k = 0; k <= static_cast<int>(spatial::FieldCombine::Average); ++k) {
        auto c = makeField(spatial::FieldKind::Compound, "compound" + std::to_string(k));
        c.combine = static_cast<spatial::FieldCombine>(k);
        c.mix = 0.3f;
        c.children = {"a", "b", "v", "c1"};
        c.falloff.kind = spatial::FalloffKind::Linear;
        c.falloff.inner = 2.0f;
        c.falloff.outer = 12.0f;
        set.fields.push_back(c);
    }
    // Missing and out-of-range children are skipped; a compound with no children is 0.
    auto empty = makeField(spatial::FieldKind::Compound, "empty");
    empty.children = {"nope"};
    set.fields.push_back(empty);
    REQUIRE(set.fields.size() <= static_cast<std::size_t>(spatial::kMaxGpuFields));
    std::vector<glm::vec4> samples;
    for (std::size_t slot = 0; slot < set.fields.size(); ++slot) {
        for (const auto& p : positions) {
            samples.emplace_back(p, static_cast<float>(slot));
        }
    }
    const auto gpu = harness.run(set, time, samples);
    for (std::size_t slot = 0; slot < set.fields.size(); ++slot) {
        compareField(set, static_cast<int>(slot), time, positions, gpu, slot * positions.size(), 1e-4f);
    }
    // Invalid slots read as 0.
    const auto invalid = harness.run(set, time, {glm::vec4(1.0f, 2.0f, 3.0f, -1.0f), glm::vec4(0.0f, 0.0f, 0.0f, 40.0f)});
    for (const auto& s : invalid) {
        CHECK(s.scalar == 0.0f);
        CHECK(s.weight == 0.0f);
        CHECK(maxAbs(s.vector) == 0.0f);
        CHECK(maxAbs(s.color) == 0.0f);
    }
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("Disabled fields keep their slot but sample as zero; slotOf resolves names", "[gpu][fields]") {
    auto ctx = makeContext();
    FieldHarness harness(*ctx);
    spatial::FieldSet set;
    set.fields.push_back(makeField(spatial::FieldKind::Constant, "on"));
    auto off = makeField(spatial::FieldKind::Constant, "off");
    off.enabled = false;
    set.fields.push_back(off);
    set.fields.push_back(makeField(spatial::FieldKind::Direction, "dir"));
    rendering::FieldUniforms block(*ctx);
    block.update(set, 0.0);
    CHECK(block.count() == 3);
    CHECK(block.slotOf("on") == 0);
    CHECK(block.slotOf("off") == -1);
    CHECK(block.slotOf("dir") == 2);
    CHECK(block.slotOf("missing") == -1);
    const auto gpu = harness.run(set, 0.0, {glm::vec4(0.5f, 0.2f, 0.1f, 0.0f), glm::vec4(0.5f, 0.2f, 0.1f, 1.0f)});
    CHECK(gpu[0].scalar > 0.0f);
    CHECK(gpu[1].scalar == 0.0f);
    CHECK(gpu[1].weight == 0.0f);
    CHECK(ctx->errorCount() == 0);
}
