// ADR-1950/1976: the "tiles" kernel writes the ground the CPU mirror computes. One rule, two halves: every record of
// this frame's window is the tile scene/tile_terrain.cpp says stands in that cell -- at the cell's centre, at its
// height, with its four channels -- and a coarser ring leaves the finer ring's window empty and stands each of its
// cells at the mean of the fine tiles it covers. This is the test that makes "the terrain query agrees with the
// rendered surface" (brief §13) true by measurement rather than by intention.

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/procedural_renderer.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/generator.hpp"
#include "scene/procedural.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
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

scene::ProceduralGeometry tilesObject(const std::string& name, float cell, float view, int align, float finePitch,
                                      float holeCell, float holeView, int holeAlign) {
    scene::ProceduralGeometry pg;
    pg.name = name;
    pg.slabs = true;
    pg.source.kind = scene::PrimitiveKind::Box;
    pg.source.size = {cell * 0.92f, 0.2f, cell * 0.92f};
    pg.distribution.kind = scene::DistributionKind::Generator;
    scene::GeneratorSpec& g = pg.distribution.generator;
    g.name = scene::kGeneratorTiles;
    g.cellSize = cell;
    g.viewDistance = view;
    g.align = align;
    g.finePitch = finePitch;
    g.holeCellSize = holeCell;
    g.holeViewDistance = holeView;
    g.holeAlign = holeAlign;
    g.tiles.seed = 11;
    g.tiles.continentWavelength = 260.0f; // sea and land inside a small window
    g.tiles.path = {{-30.0f, -10.0f}, {0.0f, 5.0f}, {40.0f, -12.0f}};
    pg.variation.seed = 5;
    REQUIRE(pg.validate().has_value());
    REQUIRE(pg.rebuild());
    return pg;
}

struct Rig {
    explicit Rig(gpu::Context& ctx)
        : shaders(ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)}), renderer(ctx, shaders) {
        REQUIRE(renderer.init().has_value());
    }
    gpu::ShaderLibrary shaders;
    rendering::SceneRenderer renderer;
};

struct Parity {
    std::uint64_t present = 0;
    std::uint64_t holes = 0;
    std::uint64_t identity = 0; // present/absent disagreements
    float worstHeight = 0.0f;
    float worstChannel = 0.0f;
    float worstXZ = 0.0f;
};

Parity compare(rendering::SceneRenderer& renderer, const scene::ProceduralGeometry& pg, const glm::vec3& camera) {
    Parity out;
    const scene::GeneratorSpec& g = pg.distribution.generator;
    const glm::vec3 camGen = rendering::generatorCameraSpace(pg, glm::mat4(1.0f), camera);
    const scene::GeneratorWindow w = renderer.procedurals().generatorWindow(pg.name);
    const scene::GeneratorWindow expect = scene::generatorWindow(g, camGen);
    REQUIRE(w.originX == expect.originX);
    REQUIRE(w.originZ == expect.originZ);
    REQUIRE(w.cells() == expect.cells());
    const scene::GeneratorTileHole hole = scene::generatorTileHole(g, camGen);
    auto records = renderer.procedurals().readInstanceRecords(pg.name);
    REQUIRE(records.has_value());
    REQUIRE(records->size() == w.cells());
    for (std::int32_t z = 0; z < w.countZ; ++z) {
        for (std::int32_t x = 0; x < w.countX; ++x) {
            const auto& r = (*records)[static_cast<std::size_t>(z) * static_cast<std::size_t>(w.countX) +
                                       static_cast<std::size_t>(x)];
            const std::int32_t ix = w.originX + x;
            const std::int32_t iz = w.originZ + z;
            const bool inHole = hole.valid && ix >= hole.minX && ix <= hole.maxX && iz >= hole.minZ && iz <= hole.maxZ;
            const bool gpuPresent = r.scale.x > 0.0f;
            if (gpuPresent == inHole) {
                ++out.identity;
                continue;
            }
            if (inHole) {
                ++out.holes;
                continue;
            }
            ++out.present;
            const scene::TileSample t = scene::generatorTile(g, ix, iz);
            out.worstHeight = std::max(out.worstHeight, std::abs(r.position.y - t.height));
            out.worstXZ = std::max({out.worstXZ, std::abs(r.position.x - (static_cast<float>(ix) + 0.5f) * g.cellSize),
                                    std::abs(r.position.z - (static_cast<float>(iz) + 0.5f) * g.cellSize)});
            out.worstChannel = std::max({out.worstChannel, std::abs(r.random.x - t.water), std::abs(r.random.y - t.shore),
                                         std::abs(r.random.z - t.flat), std::abs(r.random.w - t.path)});
        }
    }
    return out;
}

} // namespace

TEST_CASE("The tiles kernel writes the ground the CPU mirror computes", "[gpu][generator][geography][adr1950]") {
    auto ctx = makeContext();
    Rig rig(*ctx);
    const auto pg = tilesObject("ground", 1.0f, 48.0f, 4, 0.0f, 0.0f, 0.0f, 1);
    for (const glm::vec3 camera : {glm::vec3(0.5f, 30.0f, 0.5f), glm::vec3(-173.3f, 40.0f, 911.8f)}) {
        scene::Scene s;
        s.procedurals.push_back(pg);
        s.camera.position = camera;
        s.camera.target = camera + glm::vec3(0.0f, -0.5f, -1.0f);
        REQUIRE(rig.renderer.renderToImage(s, FrameTime{1.0, 1.0 / 30.0, 1}, 160, 90).has_value());
        const Parity p = compare(rig.renderer, pg, camera);
        CAPTURE(camera.x, camera.z, p.present, p.identity, p.worstHeight, p.worstChannel, p.worstXZ);
        CHECK(p.identity == 0);
        CHECK(p.present > 9000);
        CHECK(p.worstXZ <= 1e-3f);
        CHECK(p.worstHeight <= 1e-3f);  // metres: float rounding through the same operations
        CHECK(p.worstChannel <= 1e-4f);
    }
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("A coarse tile ring leaves the fine window empty and stands at its children's mean",
          "[gpu][generator][geography][adr1976]") {
    auto ctx = makeContext();
    Rig rig(*ctx);
    const auto fine = tilesObject("fine", 1.0f, 24.0f, 4, 0.0f, 0.0f, 0.0f, 1);
    const auto coarse = tilesObject("coarse", 4.0f, 96.0f, 1, 1.0f, 1.0f, 24.0f, 4);
    const glm::vec3 camera{13.7f, 35.0f, -21.2f};
    scene::Scene s;
    s.procedurals.push_back(fine);
    s.procedurals.push_back(coarse);
    s.camera.position = camera;
    s.camera.target = camera + glm::vec3(0.0f, -0.5f, -1.0f);
    REQUIRE(rig.renderer.renderToImage(s, FrameTime{1.0, 1.0 / 30.0, 1}, 160, 90).has_value());
    const Parity pf = compare(rig.renderer, fine, camera);
    const Parity pc = compare(rig.renderer, coarse, camera);
    CAPTURE(pf.present, pc.present, pc.holes, pc.identity, pc.worstHeight);
    CHECK(pf.identity == 0);
    CHECK(pc.identity == 0);
    // The hole is exactly the fine window, in 4 m blocks.
    const scene::GeneratorWindow fw = rig.renderer.procedurals().generatorWindow("fine");
    CHECK(pc.holes * 16 == fw.cells());
    CHECK(pc.worstHeight <= 1e-3f);
    CHECK(pc.worstChannel <= 1e-4f);
    CHECK(ctx->errorCount() == 0);
}
