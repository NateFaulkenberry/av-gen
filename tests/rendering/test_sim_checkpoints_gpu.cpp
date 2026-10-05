// ADR-1119 / ADR-1120: per-step inputs, GPU checkpoints and the agents population, against the seek rule.
//
// A stateful GPU system is exact under seek when the state a seek lands on is byte-for-byte the state
// play reached. Every case here compares raw GPU buffers (the grid's cells and the agents), not images,
// so "exact" means equal bytes:
//   * an agents grid whose deposits follow ONSET and SPECTRUM fields, steered by a moving vortex: played
//     at 30 fps vs a fresh replay from 0 vs a restore from a checkpoint after scrubbing past it;
//   * a scalar grid fed by a travelling wave (a time-varying input): exact only because every sub-step
//     reads its own second's field block (before ADR-1119, every sub-step of a frame read the frame's);
//   * a changed input key drops the checkpoints (slower, never inexact);
//   * a small budget doubles the spacing instead of overflowing.
#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/field_uniforms.hpp"
#include "rendering/simulation.hpp"
#include "scene/scene.hpp"
#include "spatial/audio_history.hpp"
#include "spatial/grid_field.hpp"

#include <catch2/catch_test_macros.hpp>

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

std::shared_ptr<const spatial::AudioHistory> song() {
    const int rows = 94 * 60; // a minute
    std::vector<float> data(static_cast<std::size_t>(rows) * spatial::kAudioBins);
    for (int r = 0; r < rows; ++r) {
        for (int b = 0; b < spatial::kAudioBins; ++b) {
            data[static_cast<std::size_t>(r) * spatial::kAudioBins + static_cast<std::size_t>(b)] =
                0.5f + 0.5f * std::sin(0.05f * static_cast<float>(r) + 0.3f * static_cast<float>(b));
        }
    }
    std::array<std::vector<spatial::AudioOnset>, spatial::kOnsetSources> onsets{};
    for (int k = 0; k < 120; ++k) {
        onsets[0].push_back({0.25 + 0.5 * k, 1.0f});
    }
    return std::make_shared<const spatial::AudioHistory>(
        spatial::AudioHistory::whole(93.75, 0.0, std::move(data), std::move(onsets)));
}

scene::Scene agentsScene() {
    scene::Scene s;
    s.fields.audio = song();
    spatial::FieldSpec kick;
    kick.name = "kick";
    kick.kind = spatial::FieldKind::Onset;
    kick.onsetSource = spatial::OnsetSource::Low;
    kick.onsetDecay = 3.0f;
    kick.strength = 2.0f;
    kick.falloff.kind = spatial::FalloffKind::None;
    s.fields.fields.push_back(kick);
    spatial::FieldSpec bands;
    bands.name = "bands";
    bands.kind = spatial::FieldKind::Spectrum;
    bands.audioBand = spatial::AudioBand::Element; // each species its own band
    bands.bandLow = 0.0f;
    bands.bandHigh = 1.0f;
    bands.audioSpeed = 8.0f;
    bands.falloff.kind = spatial::FalloffKind::None;
    spatial::FieldSpec deposit;
    deposit.name = "deposit";
    deposit.kind = spatial::FieldKind::Compound;
    deposit.children = {"kick", "bands"};
    deposit.combine = spatial::FieldCombine::Add;
    deposit.falloff.kind = spatial::FalloffKind::None;
    s.fields.fields.push_back(bands);
    s.fields.fields.push_back(deposit);
    spatial::FieldSpec swirl;
    swirl.name = "swirl";
    swirl.kind = spatial::FieldKind::CurlNoise;
    swirl.frequency = 0.08f;
    swirl.speed = 0.4f; // the flow moves in time: a per-step input
    swirl.falloff.kind = spatial::FalloffKind::None;
    s.fields.fields.push_back(swirl);
    spatial::GridField g;
    g.name = "mycelium";
    g.mode = spatial::GridMode::Agents;
    g.wrap = spatial::GridWrap::Wrap;
    g.resolution = {128, 1, 128};
    g.boundsMin = {-32.0f, -1.0f, -32.0f};
    g.boundsMax = {32.0f, 1.0f, 32.0f};
    g.agentCount = 20000;
    g.species = 3;
    g.depositField = "deposit";
    g.velocityField = "swirl";
    g.advect = 2.0f;
    g.diffusion = 0.4f;
    g.dissipation = 1.2f;
    g.checkpointInterval = 2.0f;
    g.maxSubSteps = 8;
    g.seed = 99;
    REQUIRE(g.validate().has_value());
    s.fields.grids.push_back(g);
    s.fields.inputKey = 1234;
    return s;
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
    std::vector<float> cells(const scene::Scene& s) {
        auto g = sim.readGrid(s.fields, 0);
        REQUIRE(g.has_value());
        return *g;
    }
    std::vector<glm::vec4> agents(const scene::Scene& s) {
        auto a = sim.readAgents(s.fields, 0);
        REQUIRE(a.has_value());
        return *a;
    }
    gpu::Context& ctx;
    rendering::FieldUniforms fields;
    rendering::Simulation sim;
};

template <typename T>
bool sameBytes(const std::vector<T>& a, const std::vector<T>& b) {
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(T)) == 0;
}

template <typename T>
std::size_t differing(const std::vector<T>& a, const std::vector<T>& b) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i) {
        n += std::memcmp(&a[i], &b[i], sizeof(T)) != 0 ? 1u : 0u;
    }
    return n + (a.size() > b.size() ? a.size() - b.size() : b.size() - a.size());
}

} // namespace

TEST_CASE("An audio-driven agents grid seeks to exactly the played state", "[gpu][simulation][seek][agents]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    const scene::Scene s = agentsScene();
    const double t = 7.0;

    // Played at 30 fps from 0 to 7 s (and on to 11 s, collecting checkpoints).
    Sim played(*ctx, shaders);
    std::vector<float> cellsAt;
    std::vector<glm::vec4> agentsAt;
    for (int f = 0; f <= 11 * 30; ++f) {
        const double now = f / 30.0;
        played.frame(s, now);
        if (f == 7 * 30) {
            cellsAt = played.cells(s);
            agentsAt = played.agents(s);
        }
    }
    CHECK(played.sim.stats().checkpoints >= 5); // every 2 s of steps
    // The population is alive and deposits: a trail exists, agents moved.
    float trail = 0.0f;
    for (std::size_t i = 3; i < cellsAt.size(); i += 4) {
        trail += cellsAt[i];
    }
    CHECK(trail > 1.0f);

    // Sensitivity controls: one frame later is a different state (so "equal bytes" below can fail), and
    // the agents have left where they were seeded.
    {
        Sim later(*ctx, shaders);
        later.seek(s, t + 1.0 / 30.0);
        const std::size_t cellDiff = differing(later.cells(s), cellsAt);
        const std::size_t agentDiff = differing(later.agents(s), agentsAt);
        INFO("one frame later: " << cellDiff << " cells, " << agentDiff << " agents differ");
        CHECK(cellDiff > cellsAt.size() / 10);
        CHECK(agentDiff > agentsAt.size() / 2);
        Sim seeded(*ctx, shaders);
        seeded.seek(s, 0.0);
        CHECK(differing(seeded.agents(s), agentsAt) > agentsAt.size() * 9 / 10);
    }

    // A fresh simulation replays 0 -> 7 s in one seek: every step with its own second's inputs.
    Sim fresh(*ctx, shaders);
    fresh.seek(s, t);
    CHECK(fresh.sim.stats().catchUpSteps == 420);
    INFO("fresh seek: " << differing(fresh.cells(s), cellsAt) << " cells, "
                        << differing(fresh.agents(s), agentsAt) << " agents differ");
    CHECK(sameBytes(fresh.cells(s), cellsAt));
    CHECK(sameBytes(fresh.agents(s), agentsAt));

    // The played one scrubs back from 11 s to 7 s: restores the 6 s checkpoint and replays 1 s.
    played.seek(s, t);
    CHECK(played.sim.stats().restores == 1);
    CHECK(played.sim.stats().restoredFromStep == 360);
    CHECK(played.sim.stats().catchUpSteps == 60);
    INFO("restored seek: " << differing(played.cells(s), cellsAt) << " cells differ");
    CHECK(sameBytes(played.cells(s), cellsAt));
    CHECK(sameBytes(played.agents(s), agentsAt));

    // A different input key (an edit): the checkpoints go, and the seek replays from 0, still exact.
    scene::Scene edited = s;
    edited.fields.inputKey = 4321;
    played.seek(edited, 3.0);
    CHECK(played.sim.stats().restores == 0);
    played.seek(edited, t);
    CHECK(played.sim.stats().restores == 0); // the 4 s checkpoint was taken under the new key... none <= 7 s yet
    CHECK(sameBytes(played.cells(edited), cellsAt));
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("A grid fed by a moving field replays exactly because every sub-step reads its own second",
          "[gpu][simulation][seek]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    scene::Scene s;
    spatial::FieldSpec wave;
    wave.name = "wave";
    wave.kind = spatial::FieldKind::Wave;
    wave.waveGeometry = spatial::WaveGeometry::Radial;
    wave.waveShape = spatial::WaveShape::Pulse;
    wave.waveSpeed = 3.0f;
    wave.wavelength = 2.0f;
    wave.waveWidth = 4.0f;
    wave.falloff.kind = spatial::FalloffKind::None;
    s.fields.fields.push_back(wave);
    spatial::GridField g;
    g.name = "smoke";
    g.resolution = {48, 8, 48};
    g.boundsMin = {-12.0f, -1.0f, -12.0f};
    g.boundsMax = {12.0f, 1.0f, 12.0f};
    g.injectField = "wave";
    g.injectRate = 3.0f;
    g.diffusion = 0.5f;
    g.diffuseIterations = 2;
    g.dissipation = 0.4f;
    s.fields.grids.push_back(g);
    Sim played(*ctx, shaders);
    for (int f = 0; f <= 5 * 30; ++f) {
        played.frame(s, f / 30.0);
    }
    const auto atFive = played.cells(s);
    Sim fresh(*ctx, shaders);
    fresh.seek(s, 5.0);
    INFO(differing(fresh.cells(s), atFive) << " of " << atFive.size() << " floats differ");
    CHECK(sameBytes(fresh.cells(s), atFive));
    // Control: a frame later differs (the wave moves), so equality above is not vacuous.
    Sim later(*ctx, shaders);
    later.seek(s, 5.0 + 1.0 / 30.0);
    CHECK(differing(later.cells(s), atFive) > atFive.size() / 20);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("Two runs of an agents grid are bit-identical and a small budget doubles the spacing",
          "[gpu][simulation][agents]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    const scene::Scene s = agentsScene();
    Sim a(*ctx, shaders);
    Sim b(*ctx, shaders);
    // A budget of three checkpoints' worth.
    const std::uint64_t one = s.fields.grids[0].floatCount() * 4 + static_cast<std::uint64_t>(s.fields.grids[0].agentCount) * 16;
    b.sim.setCheckpointBudget(3 * one);
    for (int f = 0; f <= 20 * 30; f += 3) { // 10 fps: three steps a frame... six at 60 Hz, under maxSubSteps 8
        a.frame(s, f / 30.0);
        b.frame(s, f / 30.0);
    }
    CHECK(sameBytes(a.cells(s), b.cells(s)));
    CHECK(sameBytes(a.agents(s), b.agents(s)));
    CHECK(b.sim.stats().checkpointBytes <= 3 * one);
    CHECK(b.sim.stats().checkpoints >= 1);
    CHECK(a.sim.stats().checkpoints == 10); // 2, 4, ... 20 s
    CHECK(ctx->errorCount() == 0);
}

// ADR-1122: a grid's behaviour is a parameter a performer moves while it runs. A change to it must continue
// the population -- every agent one frame further on from where it was -- and change what it does next; only a
// change to the layout (here the seed) may re-seed it.
TEST_CASE("A behaviour change continues an agents grid and only a layout change re-seeds it",
          "[gpu][simulation][agents][grid-params]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    const scene::Scene s = agentsScene();
    const double t = 2.0;
    const double next = t + 1.0 / 30.0; // two 60 Hz steps

    const auto playTo = [&](Sim& sim) {
        for (int f = 0; f <= static_cast<int>(t * 30.0); ++f) {
            sim.frame(s, f / 30.0);
        }
    };
    // Agents are (x, z, heading, species) in cells; how many moved further than `cells` (wrap-aware).
    const auto movedFurther = [&](const std::vector<glm::vec4>& a, const std::vector<glm::vec4>& b, float cells) {
        const glm::vec2 res(static_cast<float>(s.fields.grids[0].resolution.x),
                            static_cast<float>(s.fields.grids[0].resolution.z));
        std::size_t n = 0;
        for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i) {
            glm::vec2 d = glm::abs(glm::vec2(a[i].x - b[i].x, a[i].y - b[i].y));
            d = glm::min(d, res - d);
            n += glm::length(d) > cells ? 1u : 0u;
        }
        return n;
    };

    Sim reference(*ctx, shaders);
    playTo(reference);
    const std::vector<glm::vec4> before = reference.agents(s);
    reference.frame(s, next);
    const std::vector<glm::vec4> unchanged = reference.agents(s);

    // The performer turns the organism's turn and gaze up for the next frame.
    Sim moved(*ctx, shaders);
    playTo(moved);
    scene::Scene behaviour = s;
    behaviour.fields.grids[0].turnAngle = 1.3f;
    behaviour.fields.grids[0].sensorAngle = 1.1f;
    behaviour.fields.grids[0].sensorDistance = 2.0f;
    REQUIRE(behaviour.fields.grids[0].layoutHash() == s.fields.grids[0].layoutHash());
    moved.frame(behaviour, next);
    const std::vector<glm::vec4> after = moved.agents(behaviour);
    // Continued: two steps of at most `stepSize` cells each, so (nearly) every agent is within ~2 cells of
    // where it was. A re-seed would scatter them across the plane.
    const std::size_t far = movedFurther(after, before, 2.5f);
    INFO(far << " of " << after.size() << " agents moved further than 2.5 cells after the behaviour change");
    CHECK(far < after.size() / 100);
    CHECK(moved.sim.stats().catchUpSteps == 0);
    // ...and it changed what they do: headings differ from the unchanged run's for many agents.
    std::size_t turned = 0;
    for (std::size_t i = 0; i < after.size(); ++i) {
        turned += std::abs(after[i].z - unchanged[i].z) > 1e-4f ? 1u : 0u;
    }
    INFO(turned << " agents turned differently");
    CHECK(turned > after.size() / 10);

    // A layout change -- a new seed -- is a new population.
    scene::Scene layout = s;
    layout.fields.grids[0].seed += 1;
    REQUIRE(layout.fields.grids[0].layoutHash() != s.fields.grids[0].layoutHash());
    moved.frame(layout, next + 1.0 / 30.0);
    const std::vector<glm::vec4> reseeded = moved.agents(layout);
    CHECK(movedFurther(reseeded, after, 2.5f) > reseeded.size() / 2);
    CHECK(ctx->errorCount() == 0);
}
