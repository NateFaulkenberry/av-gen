// ADR-1400: the resonance field on the GPU agrees with the CPU reference.
//
// A compute harness samples resonance fields from shaders/fields.wgsl -- every channel, a reader that
// shares a resonator, a listening window, a window laid along an axis, a rotated and scaled frame
// with a falloff, point and lattice sources -- through the production FieldUniforms (so the mode
// table's layout is what is tested), at several clocks of a synthetic song, and compares each value
// with spatial::sampleScalar / sampleVector on the same FieldSet.
#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/field_uniforms.hpp"
#include "spatial/audio_history.hpp"
#include "spatial/field.hpp"
#include "spatial/resonance.hpp"

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

// samples[i].xyz = position, .w = slot. results[2i] = (fieldScalar, fieldVector), results[2i + 1] = fieldColor.
constexpr const char* kKernel = R"(
@group(0) @binding(0) var<uniform> fieldBlock: FieldBlock;
@group(0) @binding(1) var<storage, read> samples: array<vec4<f32>>;
@group(0) @binding(2) var<storage, read_write> results: array<vec4<f32>>;

@compute @workgroup_size(64)
fn cs_sample(@builtin(global_invocation_id) gid: vec3<u32>) {
    let i = gid.x;
    if (i >= arrayLength(&samples)) { return; }
    let s = samples[i];
    let slot = i32(s.w + 0.5);
    let v = fieldVector(slot, s.xyz);
    results[2u * i] = vec4<f32>(fieldScalar(slot, s.xyz), v);
    results[2u * i + 1u] = fieldColor(slot, s.xyz);
}
)";

class Harness {
public:
    explicit Harness(gpu::Context& ctx)
        : ctx_(ctx), shaders_(ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)}), block_(ctx) {
        auto source = shaders_.loadSource("fields.wgsl");
        REQUIRE(source.has_value());
        auto module = shaders_.compile(*source + kKernel, "resonance-harness");
        REQUIRE(module.has_value());
        const auto& device = ctx_.device();
        std::array<wgpu::BindGroupLayoutEntry, 4> entries{};
        const wgpu::BufferBindingType types[4] = {wgpu::BufferBindingType::Uniform, wgpu::BufferBindingType::ReadOnlyStorage,
                                                  wgpu::BufferBindingType::Storage, wgpu::BufferBindingType::ReadOnlyStorage};
        const std::uint32_t bindings[4] = {0, 1, 2, 15};
        for (int i = 0; i < 4; ++i) {
            entries[static_cast<std::size_t>(i)].binding = bindings[i];
            entries[static_cast<std::size_t>(i)].visibility = wgpu::ShaderStage::Compute;
            entries[static_cast<std::size_t>(i)].buffer.type = types[i];
        }
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

    std::vector<glm::vec4> run(const spatial::FieldSet& fields, double time, const std::vector<glm::vec4>& samples) {
        block_.update(fields, time);
        const auto& device = ctx_.device();
        wgpu::BufferDescriptor sdesc{};
        sdesc.size = samples.size() * sizeof(glm::vec4);
        sdesc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
        wgpu::Buffer in = device.CreateBuffer(&sdesc);
        ctx_.queue().WriteBuffer(in, 0, samples.data(), sdesc.size);
        wgpu::BufferDescriptor rdesc{};
        rdesc.size = 2 * samples.size() * sizeof(glm::vec4);
        rdesc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopySrc;
        wgpu::Buffer out = device.CreateBuffer(&rdesc);
        std::array<wgpu::BindGroupEntry, 4> entries{};
        entries[0].binding = 0;
        entries[0].buffer = block_.buffer();
        entries[0].size = rendering::FieldUniforms::kBufferSize;
        entries[1].binding = 1;
        entries[1].buffer = in;
        entries[1].size = sdesc.size;
        entries[2].binding = 2;
        entries[2].buffer = out;
        entries[2].size = rdesc.size;
        entries[3].binding = 15;
        entries[3].buffer = block_.gridBuffer();
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
        pass.DispatchWorkgroups(static_cast<std::uint32_t>((samples.size() + 63) / 64));
        pass.End();
        wgpu::CommandBuffer commands = encoder.Finish();
        ctx_.queue().Submit(1, &commands);
        auto bytes = gpu::readBuffer(ctx_, out, 0, rdesc.size);
        REQUIRE(bytes.has_value());
        std::vector<glm::vec4> values(2 * samples.size());
        std::memcpy(values.data(), bytes->data(), rdesc.size);
        return values;
    }

    const rendering::FieldUniforms& block() const { return block_; }

private:
    gpu::Context& ctx_;
    gpu::ShaderLibrary shaders_;
    rendering::FieldUniforms block_;
    wgpu::BindGroupLayout layout_;
    wgpu::ComputePipeline pipeline_;
};

std::shared_ptr<const spatial::AudioHistory> song(int rows) {
    std::vector<float> data(static_cast<std::size_t>(rows) * spatial::kAudioBins);
    for (int r = 0; r < rows; ++r) {
        for (int b = 0; b < spatial::kAudioBins; ++b) {
            data[static_cast<std::size_t>(r) * spatial::kAudioBins + static_cast<std::size_t>(b)] =
                0.5f + 0.5f * std::sin(0.023f * static_cast<float>(r) * (1.0f + 0.05f * static_cast<float>(b)) +
                                       0.41f * static_cast<float>(b));
        }
    }
    std::array<std::vector<spatial::AudioOnset>, spatial::kOnsetSources> onsets{};
    for (int k = 0; k < 60; ++k) {
        onsets[0].push_back({0.4 + 0.5 * k, 0.9f});
        onsets[1].push_back({0.7 + 0.9 * k, 0.6f});
        onsets[2].push_back({0.3 + 0.35 * k, 0.5f});
    }
    return std::make_shared<const spatial::AudioHistory>(
        spatial::AudioHistory::whole(93.75, 0.0, std::move(data), std::move(onsets)));
}

spatial::FieldSpec reader(const std::string& name, spatial::ResonanceChannel channel) {
    spatial::FieldSpec f;
    f.name = name;
    f.kind = spatial::FieldKind::Resonance;
    f.reference = "ground";
    f.resonance.channel = channel;
    return f;
}

spatial::FieldSet makeFields() {
    spatial::FieldSet set;
    spatial::FieldSpec ground;
    ground.name = "ground";
    ground.kind = spatial::FieldKind::Resonance;
    ground.seed = 11;
    ground.resonance.modes = 13.6f;
    ground.resonance.lambdaMax = 30.0f;
    ground.resonance.lambdaMin = 2.5f;
    ground.resonance.channel = spatial::ResonanceChannel::Sand;
    ground.resonance.sources = {
        spatial::ResonanceSource{spatial::ResonanceSourceKind::Point, {0.0f, 7.0f, 0.0f}, 5, 0.0f, 0.7f, 0.0f, 1.0f},
        spatial::ResonanceSource{spatial::ResonanceSourceKind::Point, {-20.0f, 0.0f, 9.0f}, 3, 0.3f, 1.0f, 40.0f, 0.8f},
        spatial::ResonanceSource{spatial::ResonanceSourceKind::Lattice, {5.0f, 0.0f, -4.0f}, 5, 0.5f, 1.0f, 0.0f, 0.6f}};
    ground.resonance.sources[1].depth = 6.0f;   // a layer about its height
    ground.resonance.sources[2].scale = 1.7f;   // a finer body
    set.fields.push_back(ground);
    set.fields.push_back(reader("disp", spatial::ResonanceChannel::Displacement));
    set.fields.push_back(reader("amp", spatial::ResonanceChannel::Amplitude));
    set.fields.push_back(reader("force", spatial::ResonanceChannel::Force));
    set.fields.push_back(reader("grad", spatial::ResonanceChannel::Gradient));
    auto window = reader("window", spatial::ResonanceChannel::Amplitude);
    window.resonance.listenLow = 0.3f;
    window.resonance.listenHigh = 0.6f;
    window.resonance.listenWidth = 0.1f;
    set.fields.push_back(window);
    auto tuned = reader("tuned", spatial::ResonanceChannel::Displacement);
    tuned.resonance.listenAlongAxis = true;
    tuned.resonance.listenLow = 0.1f;
    tuned.resonance.listenHigh = 0.9f;
    tuned.resonance.listenWidth = 0.15f;
    tuned.axis = glm::vec3(0.2f, 1.0f, 0.1f);
    tuned.point = glm::vec3(1.0f, 4.0f, 0.0f);
    tuned.length = 9.0f;
    set.fields.push_back(tuned);
    auto relief = reader("relief", spatial::ResonanceChannel::Relief);
    relief.resonance.sharpness = 0.9f;
    relief.resonance.reliefLine = 0.4f;
    relief.resonance.reliefLineSharpness = 0.3f;
    relief.resonance.reliefVibration = 0.25f;
    set.fields.push_back(relief);
    auto shading = reader("shading", spatial::ResonanceChannel::Shading);
    shading.resonance.sharpness = 0.8f;
    shading.resonance.reliefLineSharpness = 0.3f;
    set.fields.push_back(shading);
    auto framed = reader("framed", spatial::ResonanceChannel::Sand);
    framed.position = glm::vec3(3.0f, -1.0f, 2.0f);
    framed.rotationDegrees = glm::vec3(10.0f, 35.0f, -5.0f);
    framed.scale = glm::vec3(1.5f, 1.0f, 0.8f);
    framed.strength = 1.7f;
    framed.falloff.kind = spatial::FalloffKind::Smoothstep;
    framed.falloff.inner = 10.0f;
    framed.falloff.outer = 40.0f;
    framed.invert = true;
    set.fields.push_back(framed);
    return set;
}

bool close(float gpu, float cpu, float scale) {
    return std::abs(gpu - cpu) <= 2e-3f * scale + 2e-3f * std::abs(cpu);
}

} // namespace

TEST_CASE("Resonance fields on the GPU match the CPU reference in every channel", "[gpu][resonance]") {
    auto ctx = makeContext();
    Harness harness(*ctx);
    spatial::FieldSet fields = makeFields();
    fields.audio = song(4000);
    spatial::ResonanceCache cache;
    std::vector<glm::vec4> samples;
    for (int slot = 0; slot < static_cast<int>(fields.fields.size()); ++slot) {
        for (int k = 0; k < 64; ++k) {
            const float a = 0.37f * static_cast<float>(k);
            const float r = 0.9f * static_cast<float>(k);
            samples.emplace_back(r * std::cos(a), 0.3f * static_cast<float>(k % 9) - 1.0f, r * std::sin(a),
                                 static_cast<float>(slot));
        }
    }
    for (const double t : {0.5, 7.25, 19.0, 33.3}) {
        spatial::resolveResonance(fields, cache, t);
        REQUIRE(fields.fields[0].resonanceState != nullptr);
        const auto gpu = harness.run(fields, t, samples);
        // Every reader points at the resonator's rows.
        for (std::size_t i = 0; i < fields.fields.size(); ++i) {
            CHECK(harness.block().block().fields[i].gridBounds0.y == 14.0f);
        }
        int checked = 0;
        for (std::size_t i = 0; i < samples.size(); ++i) {
            const auto slot = static_cast<std::size_t>(samples[i].w);
            const glm::vec3 p(samples[i]);
            const float cpuS = spatial::sampleScalar(fields.fields[slot], p, t, &fields);
            const glm::vec3 cpuV = spatial::sampleVector(fields.fields[slot], p, t, &fields);
            const glm::vec4 cpuC = spatial::sampleColor(fields.fields[slot], p, t, &fields);
            const float scale = 1.0f + std::abs(cpuS) + glm::length(cpuV);
            INFO("t " << t << " field " << fields.fields[slot].name << " p (" << p.x << ", " << p.y << ", " << p.z << ")");
            const glm::vec4 g = gpu[2 * i];
            const glm::vec4 gc = gpu[2 * i + 1];
            CHECK(std::isfinite(g.x));
            CHECK(close(g.x, cpuS, scale));
            CHECK(close(g.y, cpuV.x, scale));
            CHECK(close(g.z, cpuV.y, scale));
            CHECK(close(g.w, cpuV.z, scale));
            for (int c = 0; c < 4; ++c) {
                CHECK(close(gc[c], cpuC[c], scale + std::abs(cpuC[c])));
            }
            ++checked;
        }
        CHECK(checked == static_cast<int>(samples.size()));
    }
}

TEST_CASE("The resonance table holds 48 modes; a resonator that does not fit samples 0 on the GPU", "[gpu][resonance]") {
    auto ctx = makeContext();
    Harness harness(*ctx);
    spatial::FieldSet fields;
    for (const char* name : {"a", "b", "c"}) {
        spatial::FieldSpec f;
        f.name = name;
        f.kind = spatial::FieldKind::Resonance;
        f.resonance.modes = 20.0f;
        f.resonance.channel = spatial::ResonanceChannel::Amplitude;
        fields.fields.push_back(f);
    }
    spatial::ResonanceCache cache;
    spatial::resolveResonance(fields, cache, 3.0);
    const std::vector<glm::vec4> samples{{1.0f, 0.0f, 2.0f, 0.0f}, {1.0f, 0.0f, 2.0f, 1.0f}, {1.0f, 0.0f, 2.0f, 2.0f}};
    const auto gpu = harness.run(fields, 3.0, samples);
    const auto& block = harness.block().block();
    CHECK(block.fields[0].gridBounds0 == glm::vec4(0.0f, 20.0f, 1.0f, 0.0f));
    CHECK(block.fields[1].gridBounds0 == glm::vec4(20.0f, 20.0f, 1.0f, 0.0f));
    CHECK(block.fields[2].gridBounds0.y == 0.0f); // 60 modes do not fit in 48
    CHECK(gpu[0].x > 0.0f);
    CHECK(gpu[2].x == gpu[0].x); // same spec, same clock: same modes, in different rows
    CHECK(gpu[4].x == 0.0f);
}
