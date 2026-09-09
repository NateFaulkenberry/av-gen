// Simulated grid fields on the GPU (ADR-032): the kernels in shaders/simulate.wgsl against the
// CPU reference in spatial::GridField::step(), mass conservation under diffusion, a stable
// reaction-diffusion pattern, and bit-identical buffers from two runs of the same project.

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/field_uniforms.hpp"
#include "rendering/simulation.hpp"
#include "scene/scene.hpp"
#include "spatial/field.hpp"
#include "spatial/grid_field.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <bit>
#include <numeric>
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

gpu::ShaderLibrary makeShaders(gpu::Context& ctx) {
    return gpu::ShaderLibrary(ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
}

// Drives Simulation directly, one frame at a time, so a test controls the sub-step count exactly.
class SimHarness {
public:
    explicit SimHarness(gpu::Context& ctx, gpu::ShaderLibrary& shaders)
        : ctx_(ctx), fields_(ctx), simulation_(ctx, shaders) {
        auto ok = simulation_.init(fields_.buffer(), fields_.gridBuffer());
        if (!ok) {
            FAIL(ok.error().message);
        }
    }

    // Runs one frame at `renderTime` and waits for the GPU.
    void frame(const scene::Scene& scene, double renderTime, std::uint64_t frameIndex) {
        fields_.update(scene.fields, renderTime);
        FrameTime time{};
        time.renderTime = renderTime;
        time.deltaTime = 1.0 / 60.0;
        time.frameIndex = frameIndex;
        wgpu::CommandEncoder encoder = ctx_.device().CreateCommandEncoder();
        simulation_.update(encoder, scene, time);
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

// A 8^3 grid of 8 units, cell centres at 0.5, 1.5, ...
spatial::GridField makeGrid(const char* name) {
    spatial::GridField g;
    g.name = name;
    g.resolution = glm::ivec3(8);
    g.boundsMin = glm::vec3(0.0f);
    g.boundsMax = glm::vec3(8.0f);
    g.simRate = 60.0f;
    g.maxSubSteps = 8;
    return g;
}

// One box-shaped source at the centre of cell (1, 4, 4) and a constant wind along +x.
void addSourceAndWind(scene::Scene& scene) {
    spatial::FieldSpec source;
    source.name = "source";
    source.kind = spatial::FieldKind::Box;
    source.position = {1.5f, 4.5f, 4.5f};
    source.size = glm::vec3(0.4f);
    source.softness = 0.1f;
    scene.fields.fields.push_back(source);
    spatial::FieldSpec wind;
    wind.name = "wind";
    wind.kind = spatial::FieldKind::Direction;
    wind.axis = {1.0f, 0.0f, 0.0f};
    wind.strength = 1.0f;
    scene.fields.fields.push_back(wind);
}

double maxAbsDifference(const std::vector<float>& a, const std::vector<float>& b) {
    REQUIRE(a.size() == b.size());
    double worst = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        worst = std::max(worst, static_cast<double>(std::abs(a[i] - b[i])));
    }
    return worst;
}

} // namespace

TEST_CASE("Injection then advection moves density downwind and matches the CPU reference",
          "[simulation][gpu]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    SimHarness harness(*ctx, shaders);

    scene::Scene scene;
    addSourceAndWind(scene);
    spatial::GridField grid = makeGrid("smoke");
    grid.injectField = "source";
    grid.injectRate = 60.0f;
    grid.velocityField = "wind";
    grid.advect = 1.0f;
    scene.fields.grids.push_back(grid);

    // 24 sub-steps at 60 Hz: four frames of six.
    constexpr int kFrames = 4;
    constexpr int kPerFrame = 6;
    for (int f = 0; f < kFrames; ++f) {
        harness.frame(scene, static_cast<double>((f + 1) * kPerFrame) / 60.0, static_cast<std::uint64_t>(f));
    }
    CHECK(harness.stats().grids == 1);
    const std::vector<float> gpuData = harness.read(scene.fields, 0);

    // The reference: the same sub-steps, each evaluating its fields at its frame's render time.
    spatial::GridField reference = grid;
    reference.reset();
    for (int f = 0; f < kFrames; ++f) {
        const double renderTime = static_cast<double>((f + 1) * kPerFrame) / 60.0;
        for (int i = 0; i < kPerFrame; ++i) {
            reference.step(1.0f / 60.0f, renderTime, &scene.fields);
        }
    }
    REQUIRE(gpuData.size() == reference.data.size());
    CHECK(maxAbsDifference(gpuData, reference.data) < 1e-3);

    // and the plume really did move: the source cell and the cells downwind of it hold density,
    // the cells upwind hold none.
    auto value = [&](int i, int j, int k) { return gpuData[reference.index(i, j, k)]; };
    CHECK(value(1, 4, 4) > 0.5f);
    CHECK(value(2, 4, 4) > 0.0f);
    CHECK(value(3, 4, 4) > 0.0f);
    CHECK(value(0, 4, 4) < value(1, 4, 4) * 0.5f);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("Diffusion conserves the total mass of a grid", "[simulation][gpu]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    SimHarness harness(*ctx, shaders);

    scene::Scene scene;
    spatial::GridField grid = makeGrid("blob");
    grid.wrap = spatial::GridWrap::Wrap; // no boundary to lose mass at
    grid.diffusion = 6.0f;
    grid.diffuseIterations = 8;
    grid.seedAmount = 0.5f; // a non-uniform starting state (deterministic in the seed)
    scene.fields.grids.push_back(grid);

    harness.frame(scene, 0.0, 0); // uploads the initial state, takes no sub-step
    const std::vector<float> before = harness.read(scene.fields, 0);
    const double massBefore = std::accumulate(before.begin(), before.end(), 0.0);
    REQUIRE(std::abs(massBefore) > 1e-3);

    for (int f = 1; f <= 6; ++f) {
        harness.frame(scene, static_cast<double>(f * 4) / 60.0, static_cast<std::uint64_t>(f));
    }
    const std::vector<float> after = harness.read(scene.fields, 0);
    const double massAfter = std::accumulate(after.begin(), after.end(), 0.0);
    INFO("mass " << massBefore << " -> " << massAfter);
    CHECK(std::abs(massAfter - massBefore) <= std::abs(massBefore) * 0.01);
    // and the field really did smooth out.
    const auto spread = [](const std::vector<float>& v) {
        return *std::max_element(v.begin(), v.end()) - *std::min_element(v.begin(), v.end());
    };
    CHECK(spread(after) < spread(before));
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("Reaction-diffusion settles into a stable non-uniform pattern", "[simulation][gpu]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    SimHarness harness(*ctx, shaders);

    scene::Scene scene;
    spatial::GridField grid = makeGrid("pattern");
    grid.mode = spatial::GridMode::ReactionDiffusion;
    grid.resolution = glm::ivec3(16);
    grid.boundsMax = glm::vec3(16.0f);
    grid.wrap = spatial::GridWrap::Wrap;
    grid.seedAmount = 1.0f;
    grid.diffusionA = 0.16f; // D * dt <= 1/6 keeps the explicit Euler step stable
    grid.diffusionB = 0.08f;
    grid.feed = 0.030f;      // a regime that patterns rather than dies out with a 6-neighbour 3D Laplacian
    grid.kill = 0.062f;
    grid.simRate = 1.0f; // Gray-Scott's own unit step
    grid.maxSubSteps = 32;
    scene.fields.grids.push_back(grid);

    for (int f = 1; f <= 8; ++f) {
        harness.frame(scene, static_cast<double>(f * 32), static_cast<std::uint64_t>(f));
    }
    const std::vector<float> data = harness.read(scene.fields, 0);
    REQUIRE(data.size() == 16u * 16u * 16u * 2u);
    float minB = 1.0f;
    float maxB = 0.0f;
    for (std::size_t i = 0; i < data.size(); i += 2) {
        const float a = data[i];
        const float b = data[i + 1];
        REQUIRE(std::isfinite(a));
        REQUIRE(std::isfinite(b));
        CHECK(a >= 0.0f);
        CHECK(a <= 1.0f);
        CHECK(b >= 0.0f);
        CHECK(b <= 1.0f);
        minB = std::min(minB, b);
        maxB = std::max(maxB, b);
    }
    INFO("B in [" << minB << ", " << maxB << "]");
    CHECK(maxB > 0.01f);        // B survived: the reaction did not simply die out
    CHECK(maxB > minB + 1e-4f); // a pattern, not a flat field
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("The same project stepped twice gives identical grid buffers", "[simulation][gpu]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);

    scene::Scene scene;
    addSourceAndWind(scene);
    spatial::GridField smoke = makeGrid("smoke");
    smoke.injectField = "source";
    smoke.injectRate = 30.0f;
    smoke.velocityField = "wind";
    smoke.diffusion = 1.5f;
    smoke.diffuseIterations = 4;
    smoke.dissipation = 0.4f;
    smoke.seedAmount = 0.25f;
    scene.fields.grids.push_back(smoke);
    spatial::GridField velocity = makeGrid("velocity");
    velocity.mode = spatial::GridMode::Vector;
    velocity.injectField = "wind";
    velocity.injectRate = 4.0f;
    scene.fields.grids.push_back(velocity);

    auto run = [&](std::vector<std::vector<float>>& out) {
        SimHarness harness(*ctx, shaders);
        for (int f = 1; f <= 5; ++f) {
            harness.frame(scene, static_cast<double>(f * 4) / 60.0, static_cast<std::uint64_t>(f));
        }
        out.push_back(harness.read(scene.fields, 0));
        out.push_back(harness.read(scene.fields, 1));
        CHECK(harness.stats().grids == 2);
        CHECK(harness.stats().tableFloats == smoke.floatCount() + velocity.floatCount());
    };
    std::vector<std::vector<float>> a;
    std::vector<std::vector<float>> b;
    run(a);
    run(b);
    REQUIRE(a.size() == 2);
    REQUIRE(b.size() == 2);
    for (std::size_t i = 0; i < a.size(); ++i) {
        INFO("grid " << i);
        REQUIRE(a[i].size() == b[i].size());
        CHECK(std::equal(a[i].begin(), a[i].end(), b[i].begin(),
                         [](float x, float y) { return std::bit_cast<std::uint32_t>(x) == std::bit_cast<std::uint32_t>(y); }));
    }
    CHECK(ctx->errorCount() == 0);
}

// Hidden performance probe: `avgen_render_tests "[.perf][simulation]"` (Release). Reports the
// GPU time of one frame's sub-steps for a 64^3 grid stepped every frame at 60 Hz, and the frame
// cost of a scene that samples it. See docs/performance/procedural-geometry.md.
TEST_CASE("Simulated grid field throughput", "[.perf][simulation]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);

    struct Case {
        const char* label;
        int resolution;
        spatial::GridMode mode;
        bool advect;
        int diffuseIterations;
    };
    for (const Case c : {Case{"32^3 scalar, inject + advect", 32, spatial::GridMode::Scalar, true, 0},
                         Case{"64^3 scalar, inject + advect", 64, spatial::GridMode::Scalar, true, 0},
                         Case{"64^3 scalar, inject + advect + 4 diffusion sweeps", 64,
                              spatial::GridMode::Scalar, true, 4},
                         Case{"64^3 vector, inject + advect", 64, spatial::GridMode::Vector, true, 0},
                         Case{"64^3 reaction-diffusion", 64, spatial::GridMode::ReactionDiffusion, false, 0}}) {
        SimHarness harness(*ctx, shaders);
        scene::Scene scene;
        addSourceAndWind(scene);
        spatial::GridField grid = makeGrid("perf");
        grid.mode = c.mode;
        grid.resolution = glm::ivec3(c.resolution);
        grid.boundsMax = glm::vec3(static_cast<float>(c.resolution));
        grid.injectField = "source";
        grid.injectRate = 20.0f;
        if (c.advect) {
            grid.velocityField = "wind";
        }
        grid.diffusion = c.diffuseIterations > 0 ? 2.0f : 0.0f;
        grid.diffuseIterations = c.diffuseIterations;
        grid.simRate = 60.0f;
        grid.maxSubSteps = 1;
        scene.fields.grids.push_back(grid);

        double sum = 0.0;
        int counted = 0;
        std::uint32_t dispatches = 0;
        for (int f = 1; f <= 90; ++f) {
            harness.frame(scene, static_cast<double>(f) / 60.0, static_cast<std::uint64_t>(f));
            if (f > 30 && harness.stats().simulateMs >= 0.0) {
                sum += harness.stats().simulateMs;
                ++counted;
            }
            dispatches = harness.stats().dispatches;
        }
        CHECK(ctx->errorCount() == 0);
        WARN(c.label << ": one 60 Hz sub-step per frame, " << dispatches << " dispatches, GPU "
                     << (counted ? sum / counted : -1.0) << " ms, table "
                     << (grid.floatCount() * sizeof(float)) / (1024 * 1024) << " MB");
    }
}
