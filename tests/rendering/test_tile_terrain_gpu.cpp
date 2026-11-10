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
#include <array>
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

// ADR-1965: a record's emissive.w without the water body's class above it, and the class.
float belowClass(float w) {
    return w - scene::kTileClassStride * std::floor(w / scene::kTileClassStride);
}
int recordClass(float w) {
    return static_cast<int>(std::floor(w / scene::kTileClassStride));
}

// ADR-1991: a record's drawn top -- the centred box's centre plus half its stretched thickness. It is the tile's
// height: the surface bodies stand on and flight clears.
template <class Record>
float drawnTop(const scene::ProceduralGeometry& pg, const Record& r) {
    return r.position.y + 0.5f * pg.source.size.y * pg.sourceTransform.scale.y * r.scale.y;
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
    g.tiles.rivers = {scene::TileRiver{
        .name = "river", .points = {{-60.0f, 30.0f}, {-10.0f, 20.0f}, {50.0f, 35.0f}}, .valleyWidth = 60.0f}};
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
    std::uint64_t classWrong = 0; // ADR-1965: records whose water body is not the CPU's
    std::array<std::uint64_t, 3> classes{};
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
            const scene::TileSample t = scene::generatorTileInWindow(g, ix, iz, w);
            out.worstHeight = std::max(out.worstHeight, std::abs(drawnTop(pg, r) - t.height));
            out.worstXZ = std::max({out.worstXZ, std::abs(r.position.x - (static_cast<float>(ix) + 0.5f) * g.cellSize),
                                    std::abs(r.position.z - (static_cast<float>(iz) + 0.5f) * g.cellSize)});
            out.worstChannel = std::max({out.worstChannel, std::abs(r.random.x - t.water), std::abs(r.random.y - t.shore),
                                         std::abs(r.random.z - t.flat), std::abs(r.random.w - t.path)});
            const int cls = recordClass(r.emissive.w);
            out.classWrong += cls != scene::tileWaterClass(t) ? 1u : 0u;
            if (cls >= 0 && cls < 3) ++out.classes[static_cast<std::size_t>(cls)];
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
        CHECK(p.classWrong == 0);
    }
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("A coarse tile ring leaves the fine window empty and stands at its children's mean",
          "[gpu][generator][geography][adr1976]") {
    auto ctx = makeContext();
    Rig rig(*ctx);
    auto fine = tilesObject("fine", 1.0f, 24.0f, 4, 0.0f, 0.0f, 0.0f, 1);
    fine.distribution.generator.blendCells = 3; // the seam band, toward the 4 m blocks (ADR-1976 amended)
    fine.distribution.generator.blendRatio = 4;
    REQUIRE(fine.validate().has_value());
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

TEST_CASE("A skirt drops a land tile's box below its top and leaves the sea's slabs thin",
          "[gpu][generator][geography][adr1950]") {
    auto ctx = makeContext();
    Rig rig(*ctx);
    auto pg = tilesObject("skirted", 1.0f, 32.0f, 4, 0.0f, 0.0f, 0.0f, 1);
    pg.distribution.generator.skirt = 2.0f;
    REQUIRE(pg.validate().has_value());
    const glm::vec3 camera{0.5f, 30.0f, 0.5f};
    scene::Scene s;
    s.procedurals.push_back(pg);
    s.camera.position = camera;
    s.camera.target = camera + glm::vec3(0.0f, -0.5f, -1.0f);
    REQUIRE(rig.renderer.renderToImage(s, FrameTime{1.0, 1.0 / 30.0, 1}, 160, 90).has_value());
    const scene::GeneratorSpec& g = pg.distribution.generator;
    const scene::GeneratorWindow w = rig.renderer.procedurals().generatorWindow(pg.name);
    auto records = rig.renderer.procedurals().readInstanceRecords(pg.name);
    REQUIRE(records.has_value());
    REQUIRE(records->size() == w.cells());
    float worst = 0.0f;
    int land = 0;
    int sea = 0;
    for (std::int32_t z = 0; z < w.countZ; ++z) {
        for (std::int32_t x = 0; x < w.countX; ++x) {
            const auto& r = (*records)[static_cast<std::size_t>(z) * static_cast<std::size_t>(w.countX) +
                                       static_cast<std::size_t>(x)];
            const scene::TileSample t = scene::generatorTileInWindow(g, w.originX + x, w.originZ + z, w);
            const float k = 2.0f * (1.0f - std::clamp(t.water, 0.0f, 1.0f));
            // ADR-1991: the box's top (centre + half the stretched thickness) is the tile's height, skirt or none.
            const float thickness = pg.source.size.y;
            worst = std::max(worst, std::abs(drawnTop(pg, r) - t.height));
            worst = std::max(worst, std::abs(r.scale.y - (thickness + k) / thickness));
            land += t.water < 0.01f;
            sea += t.water > 0.99f;
        }
    }
    CAPTURE(worst, land, sea);
    CHECK(worst <= 1e-3f);
    CHECK(land > 100);
    CHECK(ctx->errorCount() == 0);
}

// ADR-1953: the moving ground on the GPU is the CPU mirror's at the same time -- waves, interference, breathing,
// rolling and a ring, the crest channel in the emissive's spare lane -- on the fine ring and on a coarse one.
TEST_CASE("The tiles kernel moves the ground the CPU mirror moves", "[gpu][generator][geography][dynamics]") {
    auto ctx = makeContext();
    Rig rig(*ctx);
    scene::TileDynamics d;
    scene::TileWave a;
    a.amplitude = 0.7f;
    a.wavelength = 18.0f;
    a.speed = 5.0f;
    a.direction = 35.0f;
    a.sharpness = 0.4f;
    scene::TileWave b;
    b.circular = true;
    b.amplitude = 0.5f;
    b.wavelength = 11.0f;
    b.speed = -3.0f;
    b.centre = {8.0f, -6.0f};
    b.onLand = 0.3f;
    d.waves = {a, b};
    d.breathingAmplitude = 0.25f;
    d.breathingPeriod = 9.0f;
    d.breathingWavelength = 90.0f;
    d.rolling = {1.2f, -0.7f};
    d.rings.amplitude = 1.1f;
    d.rings.speed = 12.0f;
    d.rings.wavelength = 7.0f;
    d.rings.width = 5.0f;
    d.events = {{5.0f, {4.0f, 3.0f}, 1.0f}};
    const float time = 7.25f;
    for (const float cell : {1.0f, 2.0f}) {
        const auto pg = tilesObject("moving", cell, 40.0f * cell, 4, cell > 1.0f ? 1.0f : 0.0f, 0.0f, 0.0f, 1);
        const glm::vec3 camera(0.5f, 30.0f, 0.5f);
        scene::Scene s;
        s.procedurals.push_back(pg);
        s.environment.tileDynamics = d;
        s.camera.position = camera;
        s.camera.target = camera + glm::vec3(0.0f, -0.5f, -1.0f);
        REQUIRE(rig.renderer.renderToImage(s, FrameTime{time, 1.0 / 30.0, 1}, 160, 90).has_value());
        const scene::GeneratorSpec& g = pg.distribution.generator;
        const scene::GeneratorWindow w = rig.renderer.procedurals().generatorWindow(pg.name);
        auto records = rig.renderer.procedurals().readInstanceRecords(pg.name);
        REQUIRE(records.has_value());
        float worstHeight = 0.0f;
        float worstCrest = 0.0f;
        float moved = 0.0f;
        float crestSeen = 0.0f;
        for (std::int32_t z = 0; z < w.countZ; ++z) {
            for (std::int32_t x = 0; x < w.countX; ++x) {
                const auto& r = (*records)[static_cast<std::size_t>(z) * static_cast<std::size_t>(w.countX) +
                                           static_cast<std::size_t>(x)];
                const std::int32_t ix = w.originX + x;
                const std::int32_t iz = w.originZ + z;
                const scene::TileSample moving = scene::generatorTileInWindow(g, ix, iz, w, &d, time);
                const scene::TileSample still = scene::generatorTileInWindow(g, ix, iz, w);
                worstHeight = std::max(worstHeight, std::abs(drawnTop(pg, r) - moving.height));
                worstCrest = std::max(worstCrest, std::abs(belowClass(r.emissive.w) - moving.crest));
                moved = std::max(moved, std::abs(moving.height - still.height));
                crestSeen = std::max(crestSeen, moving.crest);
            }
        }
        CAPTURE(cell, worstHeight, worstCrest, moved, crestSeen);
        CHECK(moved > 0.5f);       // the control: the ground really moved
        CHECK(crestSeen > 0.5f);
        CHECK(worstHeight <= 2e-3f); // GPU trig and pow against the CPU's
        CHECK(worstCrest <= 2e-3f);
    }
    CHECK(ctx->errorCount() == 0);
}

// ADR-1954: the landing springs on the GPU are the CPU mirror's -- several landings at different ages, alone (so the
// springs are the only motion) and on top of a wave.
TEST_CASE("The tiles kernel springs the ground the CPU mirror springs", "[gpu][generator][geography][springs]") {
    auto ctx = makeContext();
    Rig rig(*ctx);
    for (const bool withWave : {false, true}) {
        scene::TileDynamics d;
        if (withWave) {
            scene::TileWave a;
            a.amplitude = 0.6f;
            a.wavelength = 20.0f;
            a.speed = 4.0f;
            d.waves = {a};
        }
        d.spring.depth = 1.2f;
        d.springs = {{6.6f, {4.5f, 3.5f}, 1.0f}, {6.9f, {-6.5f, -2.5f}, 1.4f}, {5.8f, {1.5f, -8.5f}, 0.8f}};
        for (int i = 0; i < 40; ++i) { // CP-D r2: well past the old cap of 8, every one still ringing
            const float ang = 0.9f * static_cast<float>(i);
            d.springs.push_back({3.0f + 0.1f * static_cast<float>(i), {14.0f * std::cos(ang), 14.0f * std::sin(ang)},
                                 0.5f + 0.02f * static_cast<float>(i)});
        }
        const float time = 7.25f;
        const auto pg = tilesObject("sprung", 1.0f, 40.0f, 4, 0.0f, 0.0f, 0.0f, 1);
        const glm::vec3 camera(0.5f, 30.0f, 0.5f);
        scene::Scene s;
        s.procedurals.push_back(pg);
        s.environment.tileDynamics = d;
        s.camera.position = camera;
        s.camera.target = camera + glm::vec3(0.0f, -0.5f, -1.0f);
        REQUIRE(rig.renderer.renderToImage(s, FrameTime{time, 1.0 / 30.0, 1}, 160, 90).has_value());
        const scene::GeneratorSpec& g = pg.distribution.generator;
        const scene::GeneratorWindow w = rig.renderer.procedurals().generatorWindow(pg.name);
        auto records = rig.renderer.procedurals().readInstanceRecords(pg.name);
        REQUIRE(records.has_value());
        scene::TileDynamics unsprung = d;
        unsprung.springs.clear();
        float worstHeight = 0.0f;
        float sprung = 0.0f;
        float worstLanding = 0.0f;
        float litMax = 0.0f;
        for (std::int32_t z = 0; z < w.countZ; ++z) {
            for (std::int32_t x = 0; x < w.countX; ++x) {
                const auto& r = (*records)[static_cast<std::size_t>(z) * static_cast<std::size_t>(w.countX) +
                                           static_cast<std::size_t>(x)];
                const std::int32_t ix = w.originX + x;
                const std::int32_t iz = w.originZ + z;
                const scene::TileSample moving = scene::generatorTileInWindow(g, ix, iz, w, &d, time);
                const scene::TileSample plain = scene::generatorTileInWindow(g, ix, iz, w, &unsprung, time);
                worstHeight = std::max(worstHeight, std::abs(drawnTop(pg, r) - moving.height));
                sprung = std::max(sprung, std::abs(moving.height - plain.height));
                // ADR-1960: the landing glow packed above the crest, as the CPU twin computes it
                const float gpuLanding = std::floor(belowClass(r.emissive.w) / 32.0f) / 255.0f;
                worstLanding = std::max(worstLanding, std::abs(gpuLanding - std::round(moving.landing * 255.0f) / 255.0f));
                litMax = std::max(litMax, moving.landing);
            }
        }
        CAPTURE(withWave, worstHeight, sprung, worstLanding, litMax);
        CHECK(sprung > 0.3f); // the control: the springs really moved tiles
        CHECK(worstHeight <= 2e-3f);
        CHECK(litMax > 0.3f);
        CHECK(worstLanding <= 1.5f / 255.0f); // one quantisation step either way
    }
    CHECK(ctx->errorCount() == 0);
}

// ADR-1970: the contour pass flags exactly the sides across which floor((top - offset) / interval) drops, computed here
// from the CPU mirror's tops; and with no interval the lane is the crest alone.
TEST_CASE("The contour pass flags the sides a contour level drops across", "[gpu][generator][geography][adr1970]") {
    auto ctx = makeContext();
    Rig rig(*ctx);
    const auto pg = tilesObject("contours", 1.0f, 40.0f, 4, 0.0f, 0.0f, 0.0f, 1);
    const glm::vec3 camera(0.5f, 30.0f, 0.5f);
    scene::Scene s;
    s.procedurals.push_back(pg);
    s.environment.tileDynamics.contourInterval = 0.5f;
    s.environment.tileDynamics.contourOffset = 0.1f;
    s.camera.position = camera;
    s.camera.target = camera + glm::vec3(0.0f, -0.5f, -1.0f);
    REQUIRE(rig.renderer.renderToImage(s, FrameTime{1.0, 1.0 / 30.0, 1}, 160, 90).has_value());
    const scene::GeneratorSpec& g = pg.distribution.generator;
    const scene::GeneratorWindow w = rig.renderer.procedurals().generatorWindow(pg.name);
    auto records = rig.renderer.procedurals().readInstanceRecords(pg.name);
    REQUIRE(records.has_value());
    const auto level = [](float h) { return std::floor((h - 0.1f) / 0.5f); };
    const auto top = [&](std::int32_t x, std::int32_t z) {
        return scene::generatorTileInWindow(g, w.originX + x, w.originZ + z, w).height;
    };
    int edges = 0;
    int wrong = 0;
    int checked = 0;
    for (std::int32_t z = 1; z + 1 < w.countZ; ++z) {
        for (std::int32_t x = 1; x + 1 < w.countX; ++x) {
            const auto& r = (*records)[static_cast<std::size_t>(z) * static_cast<std::size_t>(w.countX) +
                                       static_cast<std::size_t>(x)];
            const float own = level(top(x, z));
            // Skip tiles within float rounding of a level boundary (the GPU's top may land on the other side).
            const auto near = [](float h) {
                const float u = (h - 0.1f) / 0.5f;
                return std::abs(u - std::round(u)) < 1e-3f;
            };
            if (near(top(x, z)) || near(top(x + 1, z)) || near(top(x - 1, z)) || near(top(x, z + 1)) ||
                near(top(x, z - 1))) {
                continue;
            }
            std::uint32_t expect = 0;
            if (level(top(x + 1, z)) < own) expect |= 1u;
            if (level(top(x - 1, z)) < own) expect |= 2u;
            if (level(top(x, z + 1)) < own) expect |= 4u;
            if (level(top(x, z - 1)) < own) expect |= 8u;
            const auto got = static_cast<std::uint32_t>(std::floor(belowClass(r.emissive.w) * 0.5f));
            ++checked;
            edges += expect != 0u ? 1 : 0;
            wrong += got != expect ? 1 : 0;
        }
    }
    CAPTURE(checked, edges, wrong);
    CHECK(checked > 5000);
    CHECK(edges > 100); // the control: there are contours in this window
    CHECK(wrong == 0);
    CHECK(ctx->errorCount() == 0);
}

// ADR-1965: rivers (an upland stream at its own level), ponds (an auto level and a set one, an irregular shore) and dry
// valleys (by depth and by a fraction of the relief) -- the kernel writes the ground, the channels and the water body's
// class the CPU computes, still and moving (the waves weighted by body, the river's drift).
TEST_CASE("The tiles kernel draws the CPU's rivers, ponds and dry valleys", "[gpu][generator][geography][adr1965]") {
    auto ctx = makeContext();
    Rig rig(*ctx);
    auto pg = tilesObject("water", 1.0f, 48.0f, 4, 0.0f, 0.0f, 0.0f, 1);
    scene::TileTerrainSpec& t = pg.distribution.generator.tiles;
    t.rivers.clear();
    // the highest land near the origin, so the features sit in the land
    glm::vec2 p(0.0f);
    float best = -1e9f;
    for (float z = -160.0f; z <= 160.0f; z += 4.0f) {
        for (float x = -160.0f; x <= 160.0f; x += 4.0f) {
            const scene::TileSample s = scene::tileSample(t, x, z);
            if (s.water < 0.01f && s.height > best) {
                best = s.height;
                p = {x, z};
            }
        }
    }
    REQUIRE(best > t.seaLevel + t.shoreRise + 1.0f);
    t.rivers = {scene::TileRiver{.name = "stream",
                                 .points = {p + glm::vec2(-24.0f, -18.0f), p + glm::vec2(-4.0f, 2.0f),
                                            p + glm::vec2(22.0f, 10.0f)},
                                 .width = 3.0f,
                                 .valleyWidth = 12.0f,
                                 .bedDepth = 1.0f}};
    t.ponds = {scene::TilePond{.name = "auto", .centre = p + glm::vec2(10.0f, -14.0f), .radius = 5.0f,
                               .basinWidth = 6.0f, .shoreNoise = 0.4f},
               scene::TilePond{.name = "set", .centre = p + glm::vec2(-16.0f, 16.0f), .radius = 4.0f,
                               .basinWidth = 5.0f, .level = best - 2.0f}};
    t.valleys = {scene::TileValley{.name = "deep", .points = {p + glm::vec2(18.0f, -24.0f), p + glm::vec2(-22.0f, -30.0f)},
                                   .valleyWidth = 14.0f, .depthFraction = 0.5f},
                 scene::TileValley{.name = "shallow", .points = {p + glm::vec2(24.0f, 18.0f), p + glm::vec2(30.0f, -6.0f)},
                                   .valleyWidth = 10.0f, .depth = 2.0f}};
    REQUIRE(t.validate().has_value());
    t.resolveWater();
    REQUIRE(pg.validate().has_value());
    const glm::vec3 camera(p.x, best + 30.0f, p.y);
    {
        scene::Scene s;
        s.procedurals.push_back(pg);
        s.camera.position = camera;
        s.camera.target = camera + glm::vec3(0.0f, -0.5f, -1.0f);
        REQUIRE(rig.renderer.renderToImage(s, FrameTime{1.0, 1.0 / 30.0, 1}, 160, 90).has_value());
        const Parity par = compare(rig.renderer, pg, camera);
        CAPTURE(par.present, par.worstHeight, par.worstChannel, par.classWrong, par.classes[0], par.classes[1],
                par.classes[2]);
        CHECK(par.identity == 0);
        CHECK(par.worstHeight <= 1e-3f);
        CHECK(par.worstChannel <= 1e-4f);
        CHECK(par.classWrong == 0);
        CHECK(par.classes[1] > 20); // the stream
        CHECK(par.classes[2] > 20); // the ponds
    }
    // moving: river and pond waves at their own weights, and the stream's drift
    scene::TileDynamics d;
    scene::TileWave a;
    a.amplitude = 0.6f;
    a.wavelength = 18.0f;
    a.speed = 4.0f;
    a.direction = 20.0f;
    a.onLand = 0.0f;
    a.onWater = 1.0f;
    d.waves = {a};
    d.riverWaves = 0.3f;
    d.pondWaves = 0.1f;
    d.riverDriftAmplitude = 0.25f;
    d.riverDriftWavelength = 9.0f;
    d.riverDriftSpeed = 1.5f;
    REQUIRE(scene::validateTileDynamics(d).has_value());
    const float time = 3.4f;
    scene::Scene s;
    s.procedurals.push_back(pg);
    s.environment.tileDynamics = d;
    s.camera.position = camera;
    s.camera.target = camera + glm::vec3(0.0f, -0.5f, -1.0f);
    REQUIRE(rig.renderer.renderToImage(s, FrameTime{time, 1.0 / 30.0, 1}, 160, 90).has_value());
    const scene::GeneratorSpec& g = pg.distribution.generator;
    const scene::GeneratorWindow w = rig.renderer.procedurals().generatorWindow(pg.name);
    auto records = rig.renderer.procedurals().readInstanceRecords(pg.name);
    REQUIRE(records.has_value());
    float worstHeight = 0.0f;
    float riverMoved = 0.0f;
    float pondMoved = 0.0f;
    for (std::int32_t z = 0; z < w.countZ; ++z) {
        for (std::int32_t x = 0; x < w.countX; ++x) {
            const auto& r = (*records)[static_cast<std::size_t>(z) * static_cast<std::size_t>(w.countX) +
                                       static_cast<std::size_t>(x)];
            const scene::TileSample moving = scene::generatorTileInWindow(g, w.originX + x, w.originZ + z, w, &d, time);
            const scene::TileSample still = scene::generatorTileInWindow(g, w.originX + x, w.originZ + z, w);
            worstHeight = std::max(worstHeight, std::abs(drawnTop(pg, r) - moving.height));
            if (still.river > 0.99f) riverMoved = std::max(riverMoved, std::abs(moving.height - still.height));
            if (still.pond > 0.99f) pondMoved = std::max(pondMoved, std::abs(moving.height - still.height));
        }
    }
    CAPTURE(worstHeight, riverMoved, pondMoved);
    CHECK(worstHeight <= 1e-3f);
    CHECK(riverMoved > 0.2f);                 // drift + 0.3 of the wave
    CHECK(pondMoved > 0.0f);
    CHECK(pondMoved < 0.6f * 0.1f + 1e-3f);   // a tenth of the wave, no drift
    CHECK(ctx->errorCount() == 0);
}

// ADR-1992: ridges, rises and 8 valleys -- the kernel raises (and lands) what the CPU does, still and moving.
TEST_CASE("The tiles kernel raises the CPU's ridges and rises, with 8 valleys", "[gpu][generator][geography][adr1992]") {
    auto ctx = makeContext();
    Rig rig(*ctx);
    auto pg = tilesObject("raised", 1.0f, 48.0f, 4, 0.0f, 0.0f, 0.0f, 1);
    scene::TileTerrainSpec& t = pg.distribution.generator.tiles;
    // a world with sea and land in the window: find both near the origin
    glm::vec2 sea(1e9f);
    glm::vec2 land(1e9f);
    for (float z = -40.0f; z <= 40.0f; z += 2.0f) {
        for (float x = -40.0f; x <= 40.0f; x += 2.0f) {
            const scene::TileSample s = scene::tileSample(t, x, z);
            if (s.water > 0.999f && glm::length(glm::vec2(x, z)) < glm::length(sea)) sea = {x, z};
            if (s.water < 0.001f && glm::length(glm::vec2(x, z)) < glm::length(land)) land = {x, z};
        }
    }
    REQUIRE(sea.x < 1e8f);
    REQUIRE(land.x < 1e8f);
    t.ridges = {scene::TileRidge{.name = "spine", .points = {land, sea, sea + glm::vec2(10.0f, -12.0f)}, .height = 9.0f,
                                 .width = 10.0f, .noise = 0.4f}};
    t.rises = {scene::TileRise{.name = "isle", .centre = sea + glm::vec2(-8.0f, 6.0f), .radius = 3.0f, .height = 6.0f,
                               .edge = 4.0f, .noise = 0.7f}, // ADR-1994
               scene::TileRise{.centre = land + glm::vec2(6.0f, 6.0f), .radius = 2.0f, .height = -3.0f, .edge = 3.0f}};
    for (int i = 0; i < 8; ++i) {
        const float z = -28.0f + 8.0f * static_cast<float>(i);
        scene::TileValley v{.points = {}, .valleyWidth = 6.0f, .depth = 1.5f};
        for (int k = 0; k < 8; ++k) v.points.push_back({-28.0f + 8.0f * static_cast<float>(k), z + (k % 2 ? 2.0f : 0.0f)});
        if (i % 2) v.depthFraction = 0.4f;
        t.valleys.push_back(std::move(v));
    }
    REQUIRE(t.validate().has_value());
    t.resolveWater();
    REQUIRE(pg.validate().has_value());
    const glm::vec3 camera(0.5f, 30.0f, 0.5f);
    {
        scene::Scene s;
        s.procedurals.push_back(pg);
        s.camera.position = camera;
        s.camera.target = camera + glm::vec3(0.0f, -0.5f, -1.0f);
        REQUIRE(rig.renderer.renderToImage(s, FrameTime{1.0, 1.0 / 30.0, 1}, 160, 90).has_value());
        const Parity par = compare(rig.renderer, pg, camera);
        CAPTURE(par.present, par.worstHeight, par.worstChannel, par.classWrong);
        CHECK(par.identity == 0);
        CHECK(par.worstHeight <= 1e-3f);
        CHECK(par.worstChannel <= 1e-4f);
        CHECK(par.classWrong == 0);
    }
    // moving: the raised land under the scene's waves and breathing
    scene::TileDynamics d;
    scene::TileWave a;
    a.amplitude = 0.6f;
    a.wavelength = 18.0f;
    a.speed = 4.0f;
    a.onLand = 0.5f;
    d.waves = {a};
    d.breathingAmplitude = 0.2f;
    const float time = 2.6f;
    scene::Scene s;
    s.procedurals.push_back(pg);
    s.environment.tileDynamics = d;
    s.camera.position = camera;
    s.camera.target = camera + glm::vec3(0.0f, -0.5f, -1.0f);
    REQUIRE(rig.renderer.renderToImage(s, FrameTime{time, 1.0 / 30.0, 1}, 160, 90).has_value());
    const scene::GeneratorSpec& g = pg.distribution.generator;
    const scene::GeneratorWindow w = rig.renderer.procedurals().generatorWindow(pg.name);
    auto records = rig.renderer.procedurals().readInstanceRecords(pg.name);
    REQUIRE(records.has_value());
    float worst = 0.0f;
    for (std::int32_t z = 0; z < w.countZ; ++z) {
        for (std::int32_t x = 0; x < w.countX; ++x) {
            const auto& r = (*records)[static_cast<std::size_t>(z) * static_cast<std::size_t>(w.countX) +
                                       static_cast<std::size_t>(x)];
            const scene::TileSample m = scene::generatorTileInWindow(g, w.originX + x, w.originZ + z, w, &d, time);
            worst = std::max(worst, std::abs(drawnTop(pg, r) - m.height));
        }
    }
    CAPTURE(worst);
    CHECK(worst <= 1e-3f);
    CHECK(ctx->errorCount() == 0);
}
