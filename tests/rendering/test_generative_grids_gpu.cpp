// Generative R&D (docs/prototypes/generative-rd/): the GPU kernels of the two grid modes it added, against their CPU
// references in spatial::GridField::step().
//   ADR-1450: cs_osc_kick + cs_oscillate -- every coupling feature on at once (disk, links, populations, lag, mask,
//   frequency field, kicks) within 2e-3 of the CPU over two seconds; with noise, bit-identical across two runs, unit
//   phases; and a synchronisation transition on the GPU.
//   ADR-1451: cs_reaction on a plane with iterations, speed and a regime field within 1e-3 of the CPU, and a pattern
//   that forms in seconds.

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/field_uniforms.hpp"
#include "rendering/simulation.hpp"
#include "scene/scene.hpp"
#include "spatial/field.hpp"
#include "spatial/grid_field.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <filesystem>
#include <memory>
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

class SimHarness {
public:
    SimHarness(gpu::Context& ctx, gpu::ShaderLibrary& shaders) : ctx_(ctx), fields_(ctx), simulation_(ctx, shaders) {
        auto ok = simulation_.init(fields_.gridBuffer());
        if (!ok) {
            FAIL(ok.error().message);
        }
    }
    void frame(const scene::Scene& scene, double renderTime, std::uint64_t frameIndex) {
        fields_.update(scene.fields, renderTime);
        FrameTime time{};
        time.renderTime = renderTime;
        time.deltaTime = 1.0 / 60.0;
        time.frameIndex = frameIndex;
        wgpu::CommandEncoder encoder = ctx_.device().CreateCommandEncoder();
        simulation_.update(encoder, scene, time, &fields_);
        wgpu::CommandBuffer commands = encoder.Finish();
        ctx_.queue().Submit(1, &commands);
        ctx_.waitForQueue();
    }
    std::vector<float> read(const spatial::FieldSet& fields, std::size_t index) {
        auto data = simulation_.readGrid(fields, index);
        REQUIRE(data.has_value());
        return std::move(*data);
    }
    [[nodiscard]] const rendering::SimulationStats& stats() const { return simulation_.stats(); }

private:
    gpu::Context& ctx_;
    rendering::FieldUniforms fields_;
    rendering::Simulation simulation_;
};

double maxAbsDifference(const std::vector<float>& a, const std::vector<float>& b) {
    REQUIRE(a.size() == b.size());
    double worst = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        worst = std::max(worst, static_cast<double>(std::abs(a[i] - b[i])));
    }
    return worst;
}

// Plays `frames` frames of `perFrame` sub-steps on the GPU and the same sub-steps (each at its own second) on the CPU.
void playBoth(SimHarness& h, const scene::Scene& scene, spatial::GridField& reference, int frames, int perFrame) {
    reference.reset();
    long step = 0;
    for (int f = 0; f < frames; ++f) {
        const double t = static_cast<double>((f + 1) * perFrame) / reference.simRate;
        h.frame(scene, t, static_cast<std::uint64_t>(f));
        for (int i = 0; i < perFrame; ++i) {
            ++step;
            reference.step(1.0f / reference.simRate, static_cast<double>(step) / reference.simRate, &scene.fields);
        }
    }
}

spatial::GridField oscillators(int n) {
    spatial::GridField g;
    g.name = "osc";
    g.mode = spatial::GridMode::Oscillator;
    g.wrap = spatial::GridWrap::Wrap;
    g.resolution = {n, 1, n};
    g.boundsMin = {-8.0f, -1.0f, -8.0f};
    g.boundsMax = {8.0f, 1.0f, 8.0f};
    g.simRate = 60.0f;
    g.maxSubSteps = 8;
    return g;
}

float order(const std::vector<float>& d) {
    std::complex<double> sum = 0.0;
    for (std::size_t c = 0; c < d.size(); c += 4) {
        sum += std::complex<double>(d[c], d[c + 1]);
    }
    return static_cast<float>(std::abs(sum) / static_cast<double>(d.size() / 4));
}

} // namespace

TEST_CASE("The oscillator kernels match the CPU reference with every coupling feature on",
          "[simulation][gpu][oscillator]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    SimHarness harness(*ctx, shaders);

    scene::Scene scene;
    spatial::FieldSpec kick;
    kick.name = "kick";
    kick.kind = spatial::FieldKind::Sphere;
    kick.position = {-3.0f, 0.0f, 2.0f};
    kick.radius = 2.0f;
    kick.softness = 1.0f;
    scene.fields.fields.push_back(kick);
    spatial::FieldSpec guide;
    guide.name = "guide";
    guide.kind = spatial::FieldKind::LinearGradient;
    guide.axis = {1.0f, 0.0f, 0.0f};
    guide.length = 16.0f;
    scene.fields.fields.push_back(guide);
    spatial::FieldSpec tune;
    tune.name = "tune";
    tune.kind = spatial::FieldKind::Radial;
    tune.radius = 8.0f;
    tune.strength = 0.3f;
    scene.fields.fields.push_back(tune);

    spatial::GridField grid = oscillators(48);
    grid.seedAmount = 1.0f;
    grid.coupling = 6.0f;
    grid.phaseLag = 0.9f;
    grid.couplingRadius = 3;
    grid.links = 3;
    grid.linkRange = 20.0f;
    grid.linkWeight = 0.8f;
    grid.populationMix = 0.3f;
    grid.detune = 0.1f;
    grid.crossCoupling = 0.4f;
    grid.persistence = 0.5f;
    grid.iterations = 2;
    grid.speed = 1.5f;
    grid.injectField = "kick";
    grid.injectRate = 4.0f;
    grid.conductivityField = "guide";
    grid.frequencyField = "tune";
    scene.fields.grids.push_back(grid);

    spatial::GridField reference = grid;
    playBoth(harness, scene, reference, 4, 8); // 32 sub-steps
    const std::vector<float> gpu = harness.read(scene.fields, 0);
    INFO("max |gpu - cpu| = " << maxAbsDifference(gpu, reference.data));
    CHECK(maxAbsDifference(gpu, reference.data) < 2e-3);
    CHECK(harness.stats().steps == 8);
    // it did something: phases moved away from the seed, and the kicked disk is active
    spatial::GridField seed = grid;
    seed.reset();
    CHECK(maxAbsDifference(gpu, seed.data) > 0.5);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("A noisy oscillator grid repeats bit for bit, keeps unit phases, and synchronises under coupling",
          "[simulation][gpu][oscillator]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    scene::Scene scene;
    spatial::GridField grid = oscillators(64);
    grid.seedAmount = 0.5f;
    grid.spread = 0.06f;
    grid.coupling = 10.0f;
    grid.couplingRadius = 2;
    grid.links = 2;
    grid.noise = 0.2f;
    scene.fields.grids.push_back(grid);

    auto run = [&](double seconds) {
        SimHarness harness(*ctx, shaders);
        const int frames = static_cast<int>(seconds * 60.0 / 8.0);
        for (int f = 1; f <= frames; ++f) {
            harness.frame(scene, static_cast<double>(f * 8) / 60.0, static_cast<std::uint64_t>(f));
        }
        return harness.read(scene.fields, 0);
    };
    const std::vector<float> a = run(12.0);
    const std::vector<float> b = run(12.0);
    CHECK(a == b);
    for (std::size_t c = 0; c < a.size(); c += 4) {
        REQUIRE(std::isfinite(a[c]));
        CHECK(a[c] * a[c] + a[c + 1] * a[c + 1] == Catch::Approx(1.0f).margin(1e-3));
    }
    spatial::GridField seed = grid;
    seed.reset();
    INFO("order " << order(seed.data) << " -> " << order(a));
    CHECK(order(seed.data) < 0.7f);
    CHECK(order(a) > 0.9f);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("A reaction-diffusion plane with iterations and a regime field matches the CPU and forms a pattern",
          "[simulation][gpu][rd]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    SimHarness harness(*ctx, shaders);

    scene::Scene scene;
    spatial::FieldSpec regime;
    regime.name = "regime";
    regime.kind = spatial::FieldKind::LinearGradient;
    regime.axis = {1.0f, 0.0f, 0.0f};
    regime.length = 64.0f;
    regime.position = {32.0f, 0.0f, 32.0f};
    scene.fields.fields.push_back(regime);
    spatial::FieldSpec seedField;
    seedField.name = "seed";
    seedField.kind = spatial::FieldKind::Sphere;
    seedField.position = {32.0f, 0.0f, 32.0f};
    seedField.radius = 6.0f;
    seedField.softness = 0.5f;
    scene.fields.fields.push_back(seedField);

    spatial::GridField grid;
    grid.name = "rd";
    grid.mode = spatial::GridMode::ReactionDiffusion;
    grid.wrap = spatial::GridWrap::Wrap;
    grid.resolution = {64, 1, 64};
    grid.boundsMin = {0.0f, -1.0f, 0.0f};
    grid.boundsMax = {64.0f, 1.0f, 64.0f};
    grid.diffusionA = 0.3f;
    grid.diffusionB = 0.15f;
    grid.feed = 0.04f;
    grid.kill = 0.06f;
    grid.feedB = 0.029f;
    grid.killB = 0.057f;
    grid.regimeField = "regime";
    grid.injectField = "seed";
    grid.injectRate = 3.0f;
    grid.simRate = 60.0f;
    grid.speed = 60.0f * 12.0f;
    grid.iterations = 12; // one model unit per iteration
    grid.maxSubSteps = 8;
    scene.fields.grids.push_back(grid);

    spatial::GridField reference = grid;
    playBoth(harness, scene, reference, 4, 8);
    const std::vector<float> gpu = harness.read(scene.fields, 0);
    INFO("max |gpu - cpu| = " << maxAbsDifference(gpu, reference.data));
    CHECK(maxAbsDifference(gpu, reference.data) < 1e-3);

    // Keep playing on the GPU alone: ~5800 model steps form a pattern across the plane.
    for (int f = 5; f <= 60; ++f) {
        harness.frame(scene, static_cast<double>(f * 8) / 60.0, static_cast<std::uint64_t>(f));
    }
    const std::vector<float> later = harness.read(scene.fields, 0);
    int patterned = 0;
    for (std::size_t c = 0; c < later.size(); c += 2) {
        REQUIRE(std::isfinite(later[c + 1]));
        CHECK(later[c] >= 0.0f);
        CHECK(later[c] <= 1.0f);
        patterned += later[c + 1] > 0.2f ? 1 : 0;
    }
    INFO("cells with B > 0.2: " << patterned << " of 4096");
    CHECK(patterned > 400);
    CHECK(patterned < 3800);
    CHECK(ctx->errorCount() == 0);
}

// ADR-1452: a scalar plane is injected and advected on the GPU like a volume: dye from a spring in a 3 m/s current
// is found downstream after 10 s, and none upstream.
TEST_CASE("A scalar plane carries dye downstream on the GPU", "[simulation][gpu][rd]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    SimHarness harness(*ctx, shaders);
    scene::Scene scene;
    spatial::FieldSpec spring;
    spring.name = "spring";
    spring.kind = spatial::FieldKind::Sphere;
    spring.position = {-60.0f, 2.4f, 0.0f};
    spring.radius = 1.8f;
    spring.softness = 0.8f;
    scene.fields.fields.push_back(spring);
    spatial::FieldSpec current;
    current.name = "current";
    current.kind = spatial::FieldKind::Direction;
    current.axis = {1.0f, 0.0f, 0.0f};
    current.strength = 3.0f;
    scene.fields.fields.push_back(current);
    spatial::GridField dye;
    dye.name = "dye";
    dye.mode = spatial::GridMode::Scalar;
    dye.resolution = {512, 1, 512};
    dye.boundsMin = {-80.0f, 1.4f, -80.0f};
    dye.boundsMax = {80.0f, 3.4f, 80.0f};
    dye.injectField = "spring";
    dye.injectRate = 40.0f;
    dye.velocityField = "current";
    dye.ceiling = 1.0f;
    dye.simRate = 10.0f;
    dye.maxSubSteps = 4;
    scene.fields.grids.push_back(dye);
    for (int f = 1; f <= 300; ++f) {
        harness.frame(scene, static_cast<double>(f) / 30.0, static_cast<std::uint64_t>(f));
    }
    const std::vector<float> d = harness.read(scene.fields, 0);
    auto at = [&](float x, float z) {
        const int i = static_cast<int>((x + 80.0f) / 160.0f * 512.0f);
        const int k = static_cast<int>((z + 80.0f) / 160.0f * 512.0f);
        return d[static_cast<std::size_t>(k * 512 + i)];
    };
    INFO("at spring " << at(-60, 0) << ", 15 m on " << at(-45, 0) << ", 27 m on " << at(-33, 0) << ", upstream "
                      << at(-70, 0));
    CHECK(at(-60.0f, 0.0f) > 0.5f);
    CHECK(at(-45.0f, 0.0f) > 0.2f);
    CHECK(at(-33.0f, 0.0f) > 0.05f);
    CHECK(at(-70.0f, 0.0f) < 0.01f);
    CHECK(ctx->errorCount() == 0);
}

// ADR-1502: a grid that reads a bake grid (here a reaction-diffusion plane whose regime is a travelling wave baked once
// a frame, as RD Pins's) replays a seek exactly as it played: every sub-step re-bakes what it reads at its own clock,
// so a fresh start at t (one catch-up of every sub-step) and a play to t at 30 fps leave identical cells. Before it,
// the catch-up read the table's previous contents (zeros on a cold start) for every replayed step.
TEST_CASE("A grid reading a bake grid replays a seek exactly as it played", "[simulation][gpu][rd][adr1502]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});

    scene::Scene scene;
    spatial::FieldSpec wave; // the regime moves: a planar sine travelling across the plane
    wave.name = "regimeWave";
    wave.kind = spatial::FieldKind::Wave;
    wave.waveGeometry = spatial::WaveGeometry::Planar;
    wave.axis = {1.0f, 0.0f, 0.4f};
    wave.wavelength = 40.0f;
    wave.waveSpeed = 9.0f;
    wave.waveWidth = 0.0f;
    wave.amplitude = 0.5f;
    wave.falloff.kind = spatial::FalloffKind::None;
    scene.fields.fields.push_back(wave);
    spatial::FieldSpec map;
    map.name = "regimeMap";
    map.kind = spatial::FieldKind::Grid;
    map.reference = "regimeBake";
    map.falloff.kind = spatial::FalloffKind::None;
    scene.fields.fields.push_back(map);
    spatial::FieldSpec seedField;
    seedField.name = "seed";
    seedField.kind = spatial::FieldKind::Sphere;
    seedField.position = {32.0f, 0.0f, 32.0f};
    seedField.radius = 8.0f;
    seedField.softness = 0.5f;
    scene.fields.fields.push_back(seedField);

    spatial::GridField bake;
    bake.name = "regimeBake";
    bake.mode = spatial::GridMode::Bake;
    bake.wrap = spatial::GridWrap::Wrap;
    bake.resolution = {32, 1, 32};
    bake.boundsMin = {0.0f, -1.0f, 0.0f};
    bake.boundsMax = {64.0f, 1.0f, 64.0f};
    bake.injectField = "regimeWave";
    bake.simRate = 60.0f;
    bake.maxSubSteps = 1;
    REQUIRE(bake.validate().has_value());
    scene.fields.grids.push_back(bake);
    spatial::GridField grid;
    grid.name = "rd";
    grid.mode = spatial::GridMode::ReactionDiffusion;
    grid.wrap = spatial::GridWrap::Wrap;
    grid.resolution = {64, 1, 64};
    grid.boundsMin = {0.0f, -1.0f, 0.0f};
    grid.boundsMax = {64.0f, 1.0f, 64.0f};
    grid.diffusionA = 0.3f;
    grid.diffusionB = 0.15f;
    grid.feed = 0.055f;
    grid.kill = 0.062f;
    grid.feedB = 0.030f;
    grid.killB = 0.062f;
    grid.regimeField = "regimeMap";
    grid.injectField = "seed";
    grid.injectRate = 3.0f;
    grid.simRate = 60.0f;
    grid.speed = 60.0f * 12.0f;
    grid.iterations = 12;
    grid.maxSubSteps = 8;
    REQUIRE(grid.validate().has_value());
    scene.fields.grids.push_back(grid);

    constexpr int kFrames = 150; // 5 s at 30 fps: 300 sub-steps
    SimHarness play(*ctx, shaders);
    for (int f = 1; f <= kFrames; ++f) {
        play.frame(scene, static_cast<double>(f) / 30.0, static_cast<std::uint64_t>(f));
    }
    const std::vector<float> played = play.read(scene.fields, 1);
    SimHarness seek(*ctx, shaders);
    seek.frame(scene, static_cast<double>(kFrames) / 30.0, 0);
    const std::vector<float> sought = seek.read(scene.fields, 1);
    INFO("max |seek - play| = " << maxAbsDifference(sought, played));
    CHECK(maxAbsDifference(sought, played) == 0.0);

    // ... and the regime is read at all (a frozen regime grows a different pattern)
    scene::Scene frozen = scene;
    frozen.fields.fields[0].waveSpeed = 0.0f;
    SimHarness still(*ctx, shaders);
    still.frame(frozen, static_cast<double>(kFrames) / 30.0, 0);
    CHECK(maxAbsDifference(still.read(frozen.fields, 1), played) > 1e-2);
    CHECK(ctx->errorCount() == 0);
}
