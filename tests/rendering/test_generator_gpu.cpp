// ADR-1117: the generator distribution on the GPU, against its CPU mirror and the frame rules.
//
//   * Mirror parity: for fixed cameras, every cell of the window the kernel wrote is present on the GPU
//     exactly when the mirror says so (identity is integer-exact), and present elements agree in
//     position, size and random lanes within float tolerance.
//   * The records reach the frame through the existing cull: the visible count is positive, no larger
//     than what is present, and empty cells are never drawn.
//   * Stateless means seekable: the same (t, camera) draws the same bytes, whatever was drawn before.
//   * An unbounded world costs its window: at a kilometre or a thousand, the same number of cells.
#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/procedural_renderer.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/generator.hpp"
#include "scene/procedural.hpp"
#include "scene/scene.hpp"
#include "spatial/effector.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <memory>

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

scene::ProceduralGeometry meadowObject(bool bounded) {
    scene::ProceduralGeometry pg;
    pg.name = "meadow";
    pg.source.kind = scene::PrimitiveKind::Cylinder;
    pg.source.radius = 0.05f;
    pg.source.height = 0.8f;
    pg.source.radialSegments = 6;
    pg.distribution.kind = scene::DistributionKind::Generator;
    scene::GeneratorSpec& g = pg.distribution.generator;
    g.cellSize = 0.4f;
    g.viewDistance = 30.0f;
    g.presence = 0.7f;
    g.clusterSize = 12.0f;
    g.clusterContrast = 0.8f;
    g.jitter = 0.9f;
    g.sizeMin = 0.6f;
    g.sizeMax = 1.4f;
    g.tilt = 0.25f;
    g.groundAmplitude = 2.0f;
    g.groundFrequency = 0.04f;
    g.bounded = bounded;
    g.regionMin = {-20.0f, -20.0f};
    g.regionMax = {20.0f, 20.0f};
    g.regionRadius = bounded ? 18.0f : 0.0f;
    pg.materialVariation.valueRandom = 0.3f;
    pg.materialVariation.emissiveRandom = 0.5f;
    pg.materialVariation.emissiveSparsity = 0.2f;
    pg.material.emissiveIntensity = 2.0f;
    pg.variation.seed = 4242;
    pg.distributionTransform.position = {3.0f, -1.0f, 2.0f};
    pg.distributionTransform.rotation = glm::angleAxis(0.4f, glm::vec3(0.0f, 1.0f, 0.0f));
    REQUIRE(pg.validate().has_value());
    REQUIRE(pg.rebuild());
    return pg;
}

scene::Scene sceneWith(const scene::ProceduralGeometry& pg, const glm::vec3& camera) {
    scene::Scene s;
    s.procedurals.push_back(pg);
    s.camera.position = camera;
    s.camera.target = camera + glm::vec3(0.0f, -0.6f, -1.0f);
    return s;
}

struct Rig {
    explicit Rig(gpu::Context& ctx)
        : shaders(ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)}), renderer(ctx, shaders) {
        REQUIRE(renderer.init().has_value());
    }
    gpu::ShaderLibrary shaders;
    rendering::SceneRenderer renderer;
};

constexpr std::uint32_t kW = 320;
constexpr std::uint32_t kH = 180;

// Compares the GPU's records of one frame with the mirror, cell by cell. Returns the present count.
std::uint64_t compareWithMirror(rendering::SceneRenderer& renderer, const scene::ProceduralGeometry& pg,
                                const glm::vec3& camera) {
    const scene::GeneratorWindow w = renderer.procedurals().generatorWindow(pg.name);
    const scene::GeneratorWindow expect = scene::generatorWindow(
        pg.distribution.generator, rendering::generatorCameraSpace(pg, glm::mat4(1.0f), camera));
    REQUIRE(w.cells() == expect.cells());
    REQUIRE(w.originX == expect.originX);
    REQUIRE(w.originZ == expect.originZ);
    auto records = renderer.procedurals().readInstanceRecords(pg.name);
    REQUIRE(records.has_value());
    REQUIRE(records->size() == w.cells());
    const glm::mat4 genToObject = pg.distributionTransform.matrix();
    const scene::GeneratorVariation variation = pg.generatorVariation();
    std::uint64_t present = 0;
    std::uint64_t mismatched = 0;
    float worstPosition = 0.0f;
    float worstOther = 0.0f;
    for (std::int32_t z = 0; z < w.countZ; ++z) {
        for (std::int32_t x = 0; x < w.countX; ++x) {
            const auto& r = (*records)[static_cast<std::size_t>(z) * static_cast<std::size_t>(w.countX) +
                                       static_cast<std::size_t>(x)];
            const auto e = scene::generatorElement(pg.distribution.generator, pg.variation.seed, variation,
                                                   w.originX + x, w.originZ + z);
            const bool gpuPresent = r.scale.x > 0.0f;
            if (gpuPresent != e.has_value()) {
                ++mismatched;
                continue;
            }
            if (!e) {
                continue;
            }
            ++present;
            const glm::vec3 p = glm::vec3(genToObject * glm::vec4(e->position, 1.0f));
            // In ulps of the coordinate: an f32 position 100 km out is quantised to 7.8 mm, on both sides.
            const float magnitude = std::max({std::abs(p.x), std::abs(p.y), std::abs(p.z), 1.0f});
            const float ulp = std::nextafter(magnitude, 2.0f * magnitude) - magnitude;
            worstPosition = std::max(worstPosition, glm::length(glm::vec3(r.position) - p) / std::max(ulp, 1e-4f));
            worstOther = std::max(worstOther, std::abs(r.scale.x - e->size * pg.distributionTransform.scale.x));
            worstOther = std::max(worstOther, glm::length(r.random - e->random));
            worstOther = std::max(worstOther, std::abs(r.color.x - e->value));
            worstOther = std::max(worstOther, std::abs(r.emissive.x - e->emission));
        }
    }
    INFO("present " << present << ", identity mismatches " << mismatched << ", worst position " << worstPosition
                    << " ulps (floor 1e-4 m), worst other " << worstOther);
    CHECK(mismatched == 0);
    CHECK(worstPosition <= 16.0f);
    CHECK(worstOther <= 1e-4f);
    CHECK(present == scene::generatorPresentCount(pg.distribution.generator, pg.variation.seed, w));
    return present;
}

} // namespace

TEST_CASE("Generated records match the CPU mirror cell for cell and are drawn through the cull",
          "[gpu][generator]") {
    auto ctx = makeContext();
    Rig rig(*ctx);
    for (const bool bounded : {true, false}) {
        const scene::ProceduralGeometry pg = meadowObject(bounded);
        for (const glm::vec3 camera : {glm::vec3(0.0f, 6.0f, 10.0f), glm::vec3(-13.7f, 3.0f, 4.2f)}) {
            const scene::Scene s = sceneWith(pg, camera);
            REQUIRE(rig.renderer.renderFrame(s, FrameTime{5.0, 1.0 / 60.0, 0}, kW, kH).has_value());
            const std::uint64_t present = compareWithMirror(rig.renderer, pg, camera);
            CHECK(present > 1000);
            auto counts = rig.renderer.procedurals().readCullCounts(pg.name);
            REQUIRE(counts.has_value());
            INFO("bounded " << bounded << " visible " << counts->visible << " of " << counts->records);
            CHECK(counts->visible > 0);
            CHECK(counts->visible <= present); // an empty cell is never drawn
            CHECK(rig.renderer.stats().procedural.generatorObjects == 1);
        }
    }
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("A generated world draws the same frame for the same moment whatever came before", "[gpu][generator][seek]") {
    auto ctx = makeContext();
    const scene::ProceduralGeometry pg = meadowObject(false);
    const glm::vec3 cameraA(0.0f, 4.0f, 8.0f);
    const glm::vec3 cameraB(40.0f, 4.0f, -25.0f);
    // A fresh renderer straight to (t, B), against one that played a path through A first.
    Rig fresh(*ctx);
    REQUIRE(fresh.renderer.renderFrame(sceneWith(pg, cameraB), FrameTime{7.0, 1.0 / 30.0, 0}, kW, kH).has_value());
    auto a = fresh.renderer.renderToImage(sceneWith(pg, cameraB), FrameTime{7.0, 1.0 / 30.0, 1}, kW, kH);
    REQUIRE(a.has_value());
    Rig played(*ctx);
    for (int f = 0; f < 20; ++f) {
        const float u = static_cast<float>(f) / 19.0f;
        const glm::vec3 cam = cameraA + (cameraB - cameraA) * u;
        REQUIRE(played.renderer.renderFrame(sceneWith(pg, cam), FrameTime{static_cast<double>(u) * 7.0, 1.0 / 30.0, static_cast<std::uint64_t>(f)},
                                            kW, kH)
                    .has_value());
    }
    auto b = played.renderer.renderToImage(sceneWith(pg, cameraB), FrameTime{7.0, 1.0 / 30.0, 21}, kW, kH);
    REQUIRE(b.has_value());
    CHECK(gpu::hashImage(*a) == gpu::hashImage(*b));
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("An unbounded generated world costs its window, not its distance", "[gpu][generator]") {
    auto ctx = makeContext();
    Rig rig(*ctx);
    const scene::ProceduralGeometry pg = meadowObject(false);
    std::uint64_t cells = 0;
    for (const float far : {0.0f, 1000.0f, 1.0e5f}) {
        const glm::vec3 camera(far, 5.0f, -far * 0.5f);
        REQUIRE(rig.renderer.renderFrame(sceneWith(pg, camera), FrameTime{3.0, 1.0 / 60.0, 0}, kW, kH).has_value());
        const auto& stats = rig.renderer.stats().procedural;
        if (cells == 0) {
            cells = stats.generatorCells;
        }
        CHECK(stats.generatorCells == cells);
        CHECK(stats.generatorBytes == scene::generatorCapacity(pg.distribution.generator).cells() * 96u);
        compareWithMirror(rig.renderer, pg, camera);
        auto counts = rig.renderer.procedurals().readCullCounts(pg.name);
        REQUIRE(counts.has_value());
        INFO("at " << far << " m: visible " << counts->visible);
        CHECK(counts->visible > 0);
    }
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("Effectors move generated records, so audio fields reach a generated world", "[gpu][generator][effectors]") {
    auto ctx = makeContext();
    Rig rig(*ctx);
    scene::ProceduralGeometry pg = meadowObject(true);
    spatial::Effector lift;
    lift.field = "up";
    lift.op = spatial::EffectorOp::PositionOffset;
    lift.strength = 2.0f;
    lift.axis = {0.0f, 1.0f, 0.0f};
    pg.effectors.push_back(lift); // effectors are per frame: no rebuild
    scene::Scene s = sceneWith(pg, glm::vec3(0.0f, 6.0f, 10.0f));
    spatial::FieldSpec up;
    up.name = "up";
    up.kind = spatial::FieldKind::Constant;
    s.fields.fields.push_back(up);
    REQUIRE(rig.renderer.renderFrame(s, FrameTime{1.0, 1.0 / 60.0, 0}, kW, kH).has_value());
    auto records = rig.renderer.procedurals().readInstanceRecords(pg.name); // the live (effected) buffer
    REQUIRE(records.has_value());
    const scene::GeneratorWindow w = rig.renderer.procedurals().generatorWindow(pg.name);
    const glm::mat4 genToObject = pg.distributionTransform.matrix();
    int checked = 0;
    for (std::int32_t z = 0; z < w.countZ && checked < 200; ++z) {
        for (std::int32_t x = 0; x < w.countX && checked < 200; ++x) {
            const auto e = scene::generatorElement(pg.distribution.generator, pg.variation.seed,
                                                   pg.generatorVariation(), w.originX + x, w.originZ + z);
            if (!e) {
                continue;
            }
            const auto& r = (*records)[static_cast<std::size_t>(z * w.countX + x)];
            const glm::vec3 p = glm::vec3(genToObject * glm::vec4(e->position, 1.0f));
            CHECK(r.position.y == Catch::Approx(p.y + 2.0f).margin(1e-3));
            ++checked;
        }
    }
    CHECK(checked == 200);
    CHECK(ctx->errorCount() == 0);
}
