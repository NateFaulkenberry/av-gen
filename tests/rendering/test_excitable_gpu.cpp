// ADR-1201: the excitable propagation grid on the GPU (cs_excite in shaders/simulate.wgsl).
//   * the GPU agrees with the CPU reference (spatial::GridField::step) after many steps of a pacing stimulus
//     and a conductivity barrier (to 1e-3 everywhere without noise, in all but 1 float in 1000 with it);
//   * a seek equals play, byte for byte (fresh replay and checkpoint restore), with an audio-driven stimulus;
//   * a Grid field's `channel` reads u, r, e and w of the simulated table through fields.wgsl.

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/frame_timeline.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/field_uniforms.hpp"
#include "rendering/simulation.hpp"
#include "scene/scene.hpp"
#include "spatial/audio_history.hpp"
#include "spatial/field.hpp"
#include "spatial/grid_field.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <memory>
#include <vector>

using namespace avgen;

namespace {

std::unique_ptr<gpu::Context> makeContext() {
    static bool logInit = false;
    if (!logInit) {
        log::init(log::Level::Error);
        logInit = true;
    }
    auto ctx = gpu::Context::create(gpu::ContextDesc{});
    if (!ctx) {
        SKIP("no GPU adapter available: " << ctx.error().message);
    }
    return std::move(*ctx);
}

struct Sim {
    Sim(gpu::Context& ctx, gpu::ShaderLibrary& shaders) : ctx(ctx), fields(ctx), sim(ctx, shaders) {
        REQUIRE(sim.init(fields.gridBuffer()).has_value());
    }
    void frame(const scene::Scene& s, double t) {
        fields.update(s.fields, t);
        wgpu::CommandEncoder e = ctx.device().CreateCommandEncoder();
        sim.update(e, s, FrameTime{t, 1.0 / 30.0, 0}, &fields);
        wgpu::CommandBuffer c = e.Finish();
        ctx.queue().Submit(1, &c);
        ctx.waitForQueue();
    }
    void seek(const scene::Scene& s, double t) {
        sim.markDiscontinuity();
        frame(s, t);
    }
    std::vector<float> cells(const scene::Scene& s, std::size_t index = 0) {
        auto g = sim.readGrid(s.fields, index);
        REQUIRE(g.has_value());
        return *g;
    }
    gpu::Context& ctx;
    rendering::FieldUniforms fields;
    rendering::Simulation sim;
};

bool sameBytes(const std::vector<float>& a, const std::vector<float>& b) {
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

std::size_t differing(const std::vector<float>& a, const std::vector<float>& b) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i) {
        n += std::memcmp(&a[i], &b[i], sizeof(float)) != 0 ? 1u : 0u;
    }
    return n + (a.size() > b.size() ? a.size() - b.size() : b.size() - a.size());
}

std::size_t fired(const std::vector<float>& cells) {
    std::size_t n = 0;
    for (std::size_t i = 1; i < cells.size(); i += 4) {
        n += cells[i] > 0.0f ? 1u : 0u;
    }
    return n;
}

spatial::GridField medium(int n, float half) {
    spatial::GridField g;
    g.name = "prop";
    g.mode = spatial::GridMode::Excitable;
    g.resolution = {n, 1, n};
    g.boundsMin = {-half, -1.0f, -half};
    g.boundsMax = {half, 1.0f, half};
    g.simRate = 60.0f;
    g.maxSubSteps = 8;
    return g;
}

// A pacing stimulus at the centre (a sphere held on: it refires whenever the refractory lets it) and a barrier
// on +x (an inverted plane: conductivity 1 for x < 5, 0 past 6).
scene::Scene pacedScene(float noise) {
    scene::Scene s;
    spatial::FieldSpec pace;
    pace.name = "pace";
    pace.kind = spatial::FieldKind::Sphere;
    pace.radius = 0.6f;
    pace.softness = 0.3f;
    pace.falloff.kind = spatial::FalloffKind::None;
    s.fields.fields.push_back(pace);
    spatial::FieldSpec banks;
    banks.name = "banks";
    banks.kind = spatial::FieldKind::Plane;
    banks.position = {5.0f, 0.0f, 0.0f};
    banks.axis = {1.0f, 0.0f, 0.0f};
    banks.softness = 1.0f;
    banks.invert = true;
    banks.falloff.kind = spatial::FalloffKind::None;
    s.fields.fields.push_back(banks);
    spatial::GridField g = medium(128, 16.0f);
    g.injectField = "pace";
    g.injectRate = 1.5f;
    g.conductivityField = "banks";
    g.waveSpeed = 5.0f;
    g.refractoryTime = 0.8f;
    g.noise = noise;
    g.seed = 21;
    g.ceiling = 0.9f;
    REQUIRE(g.validate().has_value());
    s.fields.grids.push_back(g);
    return s;
}

std::shared_ptr<const spatial::AudioHistory> song() {
    const int rows = 94 * 30;
    std::vector<float> data(static_cast<std::size_t>(rows) * spatial::kAudioBins, 0.0f);
    std::array<std::vector<spatial::AudioOnset>, spatial::kOnsetSources> onsets{};
    for (int k = 0; k < 40; ++k) {
        onsets[0].push_back({0.3 + 0.7 * k, 1.0f});
    }
    return std::make_shared<const spatial::AudioHistory>(
        spatial::AudioHistory::whole(93.75, 0.0, std::move(data), std::move(onsets)));
}

// Kicks (onsets) light a ring that leaves the origin at 6 m/s and fades out by 3 m; the medium carries
// it on from there at its own speed, roughened by noise.
scene::Scene audioScene() {
    scene::Scene s;
    s.fields.audio = song();
    spatial::FieldSpec kick;
    kick.name = "kick";
    kick.kind = spatial::FieldKind::Onset;
    kick.onsetSource = spatial::OnsetSource::Low;
    kick.onsetDecay = 2.0f;
    kick.onsetWidth = 0.5f;
    kick.audioSpeed = 6.0f;
    kick.strength = 2.0f;
    kick.falloff.kind = spatial::FalloffKind::Linear;
    kick.falloff.inner = 0.0f;
    kick.falloff.outer = 3.0f;
    s.fields.fields.push_back(kick);
    spatial::GridField g = medium(256, 24.0f);
    g.wrap = spatial::GridWrap::Wrap;
    g.injectField = "kick";
    g.injectRate = 1.0f;
    g.waveSpeed = 7.0f;
    g.refractoryTime = 0.5f;
    g.noise = 0.35f;
    g.seed = 3;
    g.checkpointInterval = 2.0f;
    REQUIRE(g.validate().has_value());
    s.fields.grids.push_back(g);
    s.fields.inputKey = 99;
    return s;
}

} // namespace

TEST_CASE("The excitable grid on the GPU matches the CPU reference step for step", "[gpu][simulation][excitable]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    for (const float noise : {0.0f, 0.4f}) {
        const scene::Scene s = pacedScene(noise);
        Sim gpu(*ctx, shaders);
        // 3 s at 60 Hz in frames of six steps.
        constexpr int kFrames = 30;
        for (int f = 0; f < kFrames; ++f) {
            gpu.frame(s, static_cast<double>((f + 1) * 6) / 60.0);
        }
        const std::vector<float> got = gpu.cells(s);

        spatial::GridField reference = s.fields.grids[0];
        reference.reset();
        for (int k = 0; k < kFrames * 6; ++k) {
            reference.step(1.0f / 60.0f, static_cast<double>(k + 1) / 60.0, &s.fields);
        }
        REQUIRE(got.size() == reference.data.size());
        double worst = 0.0;
        std::size_t off = 0;
        for (std::size_t i = 0; i < got.size(); ++i) {
            const double d = std::abs(static_cast<double>(got[i]) - reference.data[i]);
            worst = std::max(worst, d);
            off += d > 1e-3 ? 1u : 0u;
        }
        INFO("noise " << noise << ": worst |gpu - cpu| " << worst << ", " << off << " of " << got.size()
                      << " floats beyond 1e-3; fired " << fired(got) << " cells");
        if (noise == 0.0f) {
            // Without noise every float agrees: a front accepted a step later on one side (a log() a ulp apart
            // at the window's edge) lands on the same sub-step arrival, so even that flip leaves no trace.
            CHECK(worst < 1e-3);
        } else {
            // With noise, that later step can fall in the next 0.25 s noise draw, so the arrival differs by the
            // jitter: measured 18 of 65,536 floats beyond 1e-3 (worst 0.025) after 180 steps. Bounded, not exact
            // (ADR-1201, limitations); GPU against GPU stays bit-exact (the seek case below).
            CHECK(off < got.size() / 1000);
            CHECK(worst < 0.1);
        }
        // Not vacuous: the pacemaker emitted fronts that crossed most of the medium...
        CHECK(fired(got) > got.size() / 4 / 4);
        // ...and the barrier held: nothing past x = 6 fired (cells 88.. on x).
        std::size_t past = 0;
        for (int k = 0; k < 128; ++k) {
            for (int i = 89; i < 128; ++i) {
                past += got[reference.index(i, 0, k) + 1] > 0.0f ? 1u : 0u;
            }
        }
        CHECK(past == 0);
    }
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("An audio-driven excitable grid seeks to exactly the played state", "[gpu][simulation][seek][excitable]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    const scene::Scene s = audioScene();
    const double t = 7.0;

    Sim played(*ctx, shaders);
    std::vector<float> at;
    for (int f = 0; f <= 11 * 30; ++f) {
        played.frame(s, f / 30.0);
        if (f == 7 * 30) {
            at = played.cells(s);
        }
    }
    CHECK(played.sim.stats().checkpoints >= 5);
    INFO("fired at 7 s: " << fired(at) << " of " << at.size() / 4);
    CHECK(fired(at) > at.size() / 4 / 4); // the kicks' fronts are crossing the medium

    // Control: a frame later differs, so the equalities below can fail.
    {
        Sim later(*ctx, shaders);
        later.seek(s, t + 1.0 / 30.0);
        CHECK(differing(later.cells(s), at) > at.size() / 50);
    }
    // A fresh simulation replays 0 -> 7 s in one seek.
    Sim fresh(*ctx, shaders);
    fresh.seek(s, t);
    CHECK(fresh.sim.stats().catchUpSteps == 420);
    INFO("fresh seek: " << differing(fresh.cells(s), at) << " floats differ");
    CHECK(sameBytes(fresh.cells(s), at));
    // The played one scrubs back from 11 s: restores the 6 s checkpoint and replays 1 s.
    played.seek(s, t);
    CHECK(played.sim.stats().restores == 1);
    CHECK(played.sim.stats().restoredFromStep == 360);
    INFO("restored seek: " << differing(played.cells(s), at) << " floats differ");
    CHECK(sameBytes(played.cells(s), at));
    CHECK(ctx->errorCount() == 0);
}

namespace {

constexpr const char* kProbe = R"(
@group(0) @binding(0) var<uniform> fieldBlock: FieldBlock;
@group(0) @binding(1) var<storage, read> samples: array<vec4<f32>>;
@group(0) @binding(2) var<storage, read_write> results: array<f32>;

@compute @workgroup_size(64)
fn cs_probe(@builtin(global_invocation_id) gid: vec3<u32>) {
    let i = gid.x;
    if (i >= arrayLength(&samples)) { return; }
    let s = samples[i];
    results[i] = fieldScalar(i32(floor(s.w + 0.5)), s.xyz);
}
)";

// Evaluates fieldScalar(slot, p) on the GPU for each (p, slot) against `table` (binding 15).
std::vector<float> probe(gpu::Context& ctx, gpu::ShaderLibrary& shaders, rendering::FieldUniforms& block,
                         const std::vector<glm::vec4>& positions) {
    auto source = shaders.loadSource("fields.wgsl");
    REQUIRE(source.has_value());
    auto module = shaders.compile(*source + kProbe, "excitable-probe");
    REQUIRE(module.has_value());
    const auto& device = ctx.device();
    std::array<wgpu::BindGroupLayoutEntry, 4> le{};
    le[0].binding = 0;
    le[0].visibility = wgpu::ShaderStage::Compute;
    le[0].buffer.type = wgpu::BufferBindingType::Uniform;
    le[1].binding = 1;
    le[1].visibility = wgpu::ShaderStage::Compute;
    le[1].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
    le[2].binding = 2;
    le[2].visibility = wgpu::ShaderStage::Compute;
    le[2].buffer.type = wgpu::BufferBindingType::Storage;
    le[3].binding = 15;
    le[3].visibility = wgpu::ShaderStage::Compute;
    le[3].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
    wgpu::BindGroupLayoutDescriptor ld{};
    ld.entryCount = le.size();
    ld.entries = le.data();
    wgpu::BindGroupLayout layout = device.CreateBindGroupLayout(&ld);
    wgpu::PipelineLayoutDescriptor pd{};
    pd.bindGroupLayoutCount = 1;
    pd.bindGroupLayouts = &layout;
    wgpu::ComputePipelineDescriptor cd{};
    cd.layout = device.CreatePipelineLayout(&pd);
    cd.compute.module = *module;
    cd.compute.entryPoint = "cs_probe";
    wgpu::ComputePipeline pipeline = device.CreateComputePipeline(&cd);
    REQUIRE(pipeline != nullptr);
    wgpu::BufferDescriptor sd{};
    sd.size = positions.size() * sizeof(glm::vec4);
    sd.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
    wgpu::Buffer samples = device.CreateBuffer(&sd);
    ctx.queue().WriteBuffer(samples, 0, positions.data(), sd.size);
    wgpu::BufferDescriptor rd{};
    rd.size = positions.size() * sizeof(float);
    rd.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopySrc;
    wgpu::Buffer results = device.CreateBuffer(&rd);
    std::array<wgpu::BindGroupEntry, 4> ge{};
    ge[0].binding = 0;
    ge[0].buffer = block.buffer();
    ge[0].size = rendering::FieldUniforms::kBufferSize;
    ge[1].binding = 1;
    ge[1].buffer = samples;
    ge[1].size = sd.size;
    ge[2].binding = 2;
    ge[2].buffer = results;
    ge[2].size = rd.size;
    ge[3].binding = 15;
    ge[3].buffer = block.gridBuffer();
    ge[3].size = rendering::FieldUniforms::kGridBufferSize;
    wgpu::BindGroupDescriptor gd{};
    gd.layout = layout;
    gd.entryCount = ge.size();
    gd.entries = ge.data();
    wgpu::BindGroup group = device.CreateBindGroup(&gd);
    wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
    wgpu::ComputePassEncoder pass = encoder.BeginComputePass();
    pass.SetPipeline(pipeline);
    pass.SetBindGroup(0, group);
    pass.DispatchWorkgroups(static_cast<std::uint32_t>((positions.size() + 63) / 64));
    pass.End();
    wgpu::CommandBuffer commands = encoder.Finish();
    ctx.queue().Submit(1, &commands);
    auto bytes = gpu::readBuffer(ctx, results, 0, rd.size);
    REQUIRE(bytes.has_value());
    std::vector<float> out(positions.size());
    std::memcpy(out.data(), bytes->data(), rd.size);
    return out;
}

} // namespace

TEST_CASE("A grid field's channel reads u, r, e and w of a simulated excitable grid on the GPU",
          "[gpu][simulation][excitable][fields]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    scene::Scene s = pacedScene(0.2f);
    // Slots 2..6: the grid's default reading, then channels 0..3.
    for (int c = -1; c < 4; ++c) {
        spatial::FieldSpec f;
        f.name = "read" + std::to_string(c + 1);
        f.kind = spatial::FieldKind::Grid;
        f.reference = "prop";
        f.channel = c;
        f.falloff.kind = spatial::FalloffKind::None;
        s.fields.fields.push_back(f);
    }
    Sim sim(*ctx, shaders);
    for (int f = 0; f <= 45; ++f) {
        sim.frame(s, f / 30.0);
    }
    // The CPU samples the GPU's own cells, so this compares the sampling and nothing else.
    spatial::FieldSet cpu = s.fields;
    cpu.grids[0].data = sim.cells(s);
    REQUIRE(cpu.grids[0].allocated());
    std::vector<glm::vec4> positions;
    std::vector<glm::vec3> points;
    for (int i = 0; i < 64; ++i) {
        const float a = static_cast<float>(i) * 0.61803f * 6.2831853f;
        const float r = 0.2f + static_cast<float>(i) * 0.19f; // across the fronts, 0..12 m out
        points.emplace_back(std::cos(a) * r, 0.0f, std::sin(a) * r);
    }
    for (int slot = 2; slot <= 6; ++slot) {
        for (const glm::vec3& p : points) {
            positions.emplace_back(p, static_cast<float>(slot));
        }
    }
    sim.fields.update(s.fields, 1.5);
    const std::vector<float> got = probe(*ctx, shaders, sim.fields, positions);
    std::array<float, 5> spread{};
    for (int slot = 2; slot <= 6; ++slot) {
        const spatial::FieldSpec& f = cpu.fields[static_cast<std::size_t>(slot)];
        for (std::size_t i = 0; i < points.size(); ++i) {
            const float want = spatial::sampleScalar(f, points[i], 1.5, &cpu);
            const float have = got[static_cast<std::size_t>(slot - 2) * points.size() + i];
            INFO("channel " << f.channel << " at (" << points[i].x << ", " << points[i].z << "): cpu " << want << " gpu "
                            << have);
            REQUIRE(std::abs(want - have) < 1e-5f);
            spread[static_cast<std::size_t>(slot - 2)] += want;
        }
    }
    // The default reading is u, and each channel reads something different (so the selector is not ignored).
    for (std::size_t i = 0; i < points.size(); ++i) {
        CHECK(got[i] == got[points.size() + i]);
    }
    INFO("sums u " << spread[1] << " r " << spread[2] << " e " << spread[3] << " w " << spread[4]);
    CHECK(spread[2] > 0.0f);
    CHECK(spread[1] != spread[2]);
    CHECK(spread[2] != spread[3]);
    CHECK(spread[3] != spread[4]);
    CHECK(ctx->errorCount() == 0);
}

// ADR-1201's cost table. Hidden (a measurement, not a verdict): run it by name under tools/gpu-lock.sh.
// GPU time of the frame's simulate pass (gpu::FrameTimeline, "sim") over frames of 8 steps, median of 60,
// for a quiet medium, a busy one (a pacing stimulus and noise: fronts everywhere), and the busy one with an fbm
// noise field as its conductivity (the field's own evaluation, per cell per step).
TEST_CASE("Excitable grid cost per step", "[.][perf][excitable-perf]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    for (const glm::ivec2 res : {glm::ivec2(512, 1024), glm::ivec2(1024, 1024)}) {
        for (const int variant : {0, 1, 2}) {
            const bool busy = variant > 0;
            scene::Scene s;
            spatial::FieldSpec pace;
            pace.name = "pace";
            pace.kind = spatial::FieldKind::Sphere;
            pace.radius = 2.0f;
            pace.softness = 0.5f;
            pace.falloff.kind = spatial::FalloffKind::None;
            s.fields.fields.push_back(pace);
            spatial::FieldSpec rock;
            rock.name = "rock";
            rock.kind = spatial::FieldKind::Noise;
            rock.frequency = 0.05f;
            rock.falloff.kind = spatial::FalloffKind::None;
            s.fields.fields.push_back(rock);
            spatial::GridField g;
            g.name = "prop";
            g.mode = spatial::GridMode::Excitable;
            g.wrap = spatial::GridWrap::Wrap;
            g.resolution = {res.x, 1, res.y};
            g.boundsMin = {-0.125f * static_cast<float>(res.x), -1.0f, -128.0f};
            g.boundsMax = {0.125f * static_cast<float>(res.x), 1.0f, 128.0f};
            g.simRate = 60.0f;
            g.maxSubSteps = 8;
            g.checkpointInterval = 0.0f; // a checkpoint would split the timed pass
            g.waveSpeed = 12.0f;
            g.refractoryTime = 0.6f;
            if (busy) {
                g.injectField = "pace";
                g.injectRate = 1.5f;
                g.conductivityField = variant == 2 ? "rock" : "";
                g.coupling = 2.0f;
                g.noise = 0.3f;
            }
            REQUIRE(g.validate().has_value());
            s.fields.grids.push_back(g);
            Sim sim(*ctx, shaders);
            gpu::FrameTimeline timeline(*ctx);
            sim.sim.setTimeline(&timeline);
            std::vector<double> ms;
            for (int f = 1; f <= 100; ++f) {
                const double t = f * 8.0 / 60.0; // 8 steps a frame
                sim.fields.update(s.fields, t);
                timeline.beginFrame();
                wgpu::CommandEncoder e = ctx->device().CreateCommandEncoder();
                sim.sim.update(e, s, FrameTime{t, 8.0 / 60.0, static_cast<std::uint64_t>(f)}, &sim.fields);
                timeline.resolve(e);
                wgpu::CommandBuffer c = e.Finish();
                ctx->queue().Submit(1, &c);
                ctx->waitForQueue();
                timeline.collect();
                ctx->waitForQueue();
                timeline.collect();
                const double m = timeline.msFor("sim");
                if (f > 40 && m > 0.0 && sim.sim.stats().steps == 8) {
                    ms.push_back(m);
                }
            }
            REQUIRE(ms.size() >= 30);
            std::sort(ms.begin(), ms.end());
            const double median = ms[ms.size() / 2];
            const std::vector<float> cells = sim.cells(s);
            std::printf("excitable %d x 1 x %d %s: %.4f ms per 8 steps = %.4f ms/step (min %.4f, max %.4f per 8; "
                        "%zu frames; %zu of %zu cells fired)\n",
                        res.x, res.y, variant == 0 ? "quiet" : variant == 1 ? "busy" : "busy + fbm conductivity", median, median / 8.0, ms.front(), ms.back(), ms.size(),
                        fired(cells), cells.size() / 4);
        }
    }
    CHECK(ctx->errorCount() == 0);
}
