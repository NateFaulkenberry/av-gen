// ADR-1116: the audio field kinds on the GPU agree with the CPU reference, through the real ring.
//
// A compute harness samples Spectrum and Onset fields from shaders/fields.wgsl -- with the sampling
// element's own random set the way cs_effectors sets it -- through ONE persistent FieldUniforms, so
// the ring's incremental upload is what is tested: playing forward a frame at a time, stepping back
// inside the window, jumping far, and swapping the history for a new one. Every step compares with
// spatial::sampleScalar on the same FieldSet. A second case drives the production effector pass
// (ProceduralRenderer) and checks that every record hears its own band at its own delay.
#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/field_uniforms.hpp"
#include "rendering/procedural_renderer.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/procedural.hpp"
#include "scene/scene.hpp"
#include "spatial/audio_history.hpp"
#include "spatial/effector.hpp"
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

// samples[i].xyz = position, .w = slot + 0.5 * element (element in [0, 1)) -- the kernel sets
// fieldElement exactly as cs_effectors does before sampling.
constexpr const char* kKernel = R"(
@group(0) @binding(0) var<uniform> fieldBlock: FieldBlock;
@group(0) @binding(1) var<storage, read> samples: array<vec4<f32>>;
@group(0) @binding(2) var<storage, read_write> results: array<f32>;

@compute @workgroup_size(64)
fn cs_sample(@builtin(global_invocation_id) gid: vec3<u32>) {
    let i = gid.x;
    if (i >= arrayLength(&samples)) { return; }
    let s = samples[i];
    let slot = floor(s.w);
    fieldElement = (s.w - slot) * 2.0;
    results[i] = fieldScalar(i32(slot), s.xyz);
}
)";

class AudioHarness {
public:
    explicit AudioHarness(gpu::Context& ctx)
        : ctx_(ctx), shaders_(ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)}), block_(ctx) {
        auto source = shaders_.loadSource("fields.wgsl");
        REQUIRE(source.has_value());
        auto module = shaders_.compile(*source + kKernel, "audio-fields-harness");
        REQUIRE(module.has_value());
        const auto& device = ctx_.device();
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

    std::vector<float> run(const spatial::FieldSet& fields, double time, const std::vector<glm::vec4>& samples) {
        block_.update(fields, time);
        const auto& device = ctx_.device();
        wgpu::BufferDescriptor sdesc{};
        sdesc.size = samples.size() * sizeof(glm::vec4);
        sdesc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
        wgpu::Buffer in = device.CreateBuffer(&sdesc);
        ctx_.queue().WriteBuffer(in, 0, samples.data(), sdesc.size);
        wgpu::BufferDescriptor rdesc{};
        rdesc.size = samples.size() * sizeof(float);
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
        std::vector<float> values(samples.size());
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

// A history with structure in both time and frequency, distinct per row and bin.
std::shared_ptr<const spatial::AudioHistory> songHistory(int rows, std::uint32_t salt) {
    std::vector<float> data(static_cast<std::size_t>(rows) * spatial::kAudioBins);
    for (int r = 0; r < rows; ++r) {
        for (int b = 0; b < spatial::kAudioBins; ++b) {
            const float x = std::sin(0.031f * static_cast<float>(r) + 0.37f * static_cast<float>(b) +
                                     static_cast<float>(salt)) *
                                0.5f +
                            0.5f;
            data[static_cast<std::size_t>(r) * spatial::kAudioBins + static_cast<std::size_t>(b)] = x;
        }
    }
    std::array<std::vector<spatial::AudioOnset>, spatial::kOnsetSources> onsets{};
    for (int k = 0; k < 400; ++k) {
        onsets[0].push_back({0.5 + 0.55 * k, 0.5f + 0.5f * static_cast<float>(k % 3) / 2.0f});
        onsets[1].push_back({0.8 + 1.1 * k, 0.7f});
        onsets[3].push_back({0.25 + 0.5 * k, 1.0f});
    }
    return std::make_shared<const spatial::AudioHistory>(
        spatial::AudioHistory::whole(93.75, 0.0107, std::move(data), std::move(onsets)));
}

spatial::FieldSpec audioField(spatial::FieldKind kind, const std::string& name) {
    spatial::FieldSpec f;
    f.name = name;
    f.kind = kind;
    f.position = {1.0f, 0.0f, -2.0f};
    f.rotationDegrees = {0.0f, 25.0f, 0.0f};
    f.falloff.kind = spatial::FalloffKind::None;
    f.strength = 1.3f;
    f.waveGeometry = spatial::WaveGeometry::Radial;
    f.bandLow = 0.05f;
    f.bandHigh = 0.9f;
    return f;
}

spatial::FieldSet audioSet(std::shared_ptr<const spatial::AudioHistory> audio) {
    spatial::FieldSet set;
    set.audio = std::move(audio);
    auto a = audioField(spatial::FieldKind::Spectrum, "range");
    a.audioBand = spatial::AudioBand::Range;
    a.audioSpeed = 9.0f;
    set.fields.push_back(a);
    auto b = audioField(spatial::FieldKind::Spectrum, "element");
    b.audioBand = spatial::AudioBand::Element;
    b.audioDelay = 0.3f;
    b.audioSpeed = 4.0f;
    set.fields.push_back(b);
    auto c = audioField(spatial::FieldKind::Spectrum, "angle");
    c.audioBand = spatial::AudioBand::Angle;
    c.bandRepeat = 3;
    c.audioSpeed = 20.0f;
    c.invert = true;
    set.fields.push_back(c);
    auto d = audioField(spatial::FieldKind::Onset, "kick-front");
    d.onsetSource = spatial::OnsetSource::Low;
    d.audioSpeed = 25.0f;
    d.onsetWidth = 3.0f;
    d.onsetDecay = 1.5f;
    set.fields.push_back(d);
    auto e = audioField(spatial::FieldKind::Onset, "beat-flash");
    e.onsetSource = spatial::OnsetSource::Beat;
    e.onsetDecay = 6.0f;
    set.fields.push_back(e);
    return set;
}

// Positions out to 130 m (the delay passes the 16.4 s ring for the slow fields) and elements.
std::vector<glm::vec4> samplesFor(const spatial::FieldSet& set) {
    std::vector<glm::vec4> out;
    for (std::size_t slot = 0; slot < set.fields.size(); ++slot) {
        for (int i = 0; i < 160; ++i) {
            const float a = static_cast<float>(i) * 2.39996f;
            const float r = static_cast<float>(i) * 0.8f;
            const float element = std::fmod(static_cast<float>(i) * 0.618034f, 1.0f) * 0.999f;
            out.emplace_back(std::cos(a) * r, 0.3f * std::sin(a * 3.0f), std::sin(a) * r,
                             static_cast<float>(slot) + 0.5f * element);
        }
    }
    return out;
}

// Max |CPU - GPU| over every sample; fails with the worst one in the message.
float compare(const spatial::FieldSet& set, double time, const std::vector<glm::vec4>& samples,
              const std::vector<float>& gpu) {
    float worst = 0.0f;
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const float slotF = std::floor(samples[i].w);
        const float element = (samples[i].w - slotF) * 2.0f;
        const auto& field = set.fields[static_cast<std::size_t>(slotF)];
        const float cpu = spatial::sampleScalar(field, glm::vec3(samples[i]), time, &set, element);
        const float d = std::abs(cpu - gpu[i]);
        if (d > 2e-4f) {
            INFO(field.name << " t=" << time << " sample " << i << " cpu " << cpu << " gpu " << gpu[i]);
            CHECK(d <= 2e-4f);
        }
        worst = std::max(worst, d);
    }
    return worst;
}

} // namespace

TEST_CASE("Audio fields sample identically on the CPU and the GPU through the ring", "[gpu][fields][audio-fields]") {
    auto ctx = makeContext();
    AudioHarness harness(*ctx);
    spatial::FieldSet set = audioSet(songHistory(20000, 1));
    const auto samples = samplesFor(set);

    // Playing forward one 60 fps frame at a time: the ring writes ~1.5 rows a frame.
    double t = 30.0;
    float worst = 0.0f;
    std::uint32_t firstUpload = 0;
    for (int frame = 0; frame < 12; ++frame) {
        const auto gpu = harness.run(set, t, samples);
        if (frame == 0) {
            firstUpload = harness.block().audioStats().rowsUploaded;
        } else {
            CHECK(harness.block().audioStats().rowsUploaded <= 3);
        }
        worst = std::max(worst, compare(set, t, samples, gpu));
        t += 1.0 / 60.0;
    }
    CHECK(firstUpload == static_cast<std::uint32_t>(spatial::kAudioRingRows)); // a cold ring fills whole
    // Stepped back inside the window (a scrub of 3 s): only the older rows are written.
    t -= 3.0;
    worst = std::max(worst, compare(set, t, samples, harness.run(set, t, samples)));
    CHECK(harness.block().audioStats().rowsUploaded > 200);
    CHECK(harness.block().audioStats().rowsUploaded < 400);
    // A jump far away (a seek to 150 s), and one to before the first row.
    for (const double jump : {150.0, 0.005, 2.0, 210.0}) {
        worst = std::max(worst, compare(set, jump, samples, harness.run(set, jump, samples)));
    }
    // A different history (a new track): the ring refills.
    set.audio = songHistory(20000, 7);
    worst = std::max(worst, compare(set, 210.0, samples, harness.run(set, 210.0, samples)));
    CHECK(harness.block().audioStats().rowsUploaded == static_cast<std::uint32_t>(spatial::kAudioRingRows));
    INFO("worst CPU/GPU deviation " << worst);
    CHECK(worst <= 2e-4f);
    // No history: every audio field is silent on the GPU too (an inverted one reads 1 - 0, on both sides).
    set.audio.reset();
    compare(set, 50.0, samples, harness.run(set, 50.0, samples));
    set.fields[2].invert = false;
    for (const float v : harness.run(set, 50.0, samples)) {
        CHECK(v == 0.0f);
    }
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("The effector pass gives every record its own band at its own delay", "[gpu][fields][audio-fields][effectors]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    scene::Scene scene;
    scene.fields = audioSet(songHistory(20000, 3));
    scene::ProceduralGeometry reeds;
    reeds.name = "reeds";
    reeds.source.kind = scene::PrimitiveKind::Box;
    reeds.distribution.kind = scene::DistributionKind::Grid;
    reeds.distribution.gridCount = {32, 1, 32};
    reeds.distribution.gridSpacing = {3.0f, 1.0f, 3.0f};
    spatial::Effector scale;
    scale.field = "element";
    scale.op = spatial::EffectorOp::Scale;
    // Mix at weight 1 is the target scale (every blend agrees with the CPU since ADR-1121).
    scale.blend = spatial::EffectorBlend::Mix;
    scale.weight = 1.0f;
    scale.strength = 2.0f;
    reeds.effectors.push_back(scale);
    REQUIRE(reeds.rebuild());
    scene.procedurals.push_back(reeds);
    scene.camera.position = {0.0f, 30.0f, 60.0f};

    const double t = 42.0;
    REQUIRE(renderer.renderFrame(scene, FrameTime{t, 1.0 / 60.0, 0}, 160, 90).has_value());
    auto live = renderer.procedurals().readInstanceRecords("reeds");
    REQUIRE(live.has_value());
    REQUIRE(live->size() == reeds.instances.size());

    // The CPU reference of the same pass on the same records.
    std::vector<scene::InstanceRecord> expect = reeds.instances;
    spatial::applyEffectorsToRecords(expect, reeds.effectors, scene.fields, t);
    float worst = 0.0f;
    float spread = 0.0f;
    float lo = 1e9f;
    float hi = -1e9f;
    for (std::size_t i = 0; i < expect.size(); ++i) {
        worst = std::max(worst, std::abs((*live)[i].scale.x - expect[i].scale.x));
        lo = std::min(lo, (*live)[i].scale.x);
        hi = std::max(hi, (*live)[i].scale.x);
    }
    spread = hi - lo;
    INFO("worst " << worst << " spread " << spread);
    CHECK(worst <= 1e-3f);
    CHECK(spread > 0.5f); // records diverge: each hears its own band at its own moment
    CHECK(ctx->errorCount() == 0);
}
