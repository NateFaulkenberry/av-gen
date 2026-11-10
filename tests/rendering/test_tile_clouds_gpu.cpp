// ADR-1966 (CPD-R2): a tiles layer as a cloud deck -- `omit: "water"` leaves the wet cells out, a layer can opt out of
// the scene's tile dynamics and the contour pass, and the window stays about the camera when the node's transform is
// keyed (the wind drift).

#include "assets/asset_registry.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "rendering/procedural_renderer.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/composition.hpp"
#include "scene/generator.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>
#include <memory>

using namespace avgen;

namespace {

struct Gpu {
    std::unique_ptr<gpu::Context> ctx;
    std::unique_ptr<gpu::ShaderLibrary> shaders;
    std::unique_ptr<rendering::SceneRenderer> renderer;
};

bool makeGpu(Gpu& g) {
    static bool logInit = false;
    if (!logInit) {
        log::init(log::Level::Warn);
        logInit = true;
    }
    auto ctx = gpu::Context::create(gpu::ContextDesc{});
    if (!ctx) return false;
    g.ctx = std::move(*ctx);
    g.shaders = std::make_unique<gpu::ShaderLibrary>(*g.ctx, std::vector<std::filesystem::path>{
                                                                  std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    g.renderer = std::make_unique<rendering::SceneRenderer>(*g.ctx, *g.shaders);
    return g.renderer->init().has_value();
}

// A cloud deck: a tiles ground with half its "continent" water, lifted to 300 m.
nlohmann::json cloudScene(const std::string& extra) {
    return nlohmann::json::parse(R"({
        "format": "avgen-scene", "version": 1, "name": "clouds",
        "camera": {"position": [0, 200, 40], "target": [0, 300, 0]},
        "environment": {"tileDynamics": {
            "waves": [ {"amplitude": 2.0, "wavelength": 60, "speed": 5, "onLand": 1, "onWater": 1} ],
            "contour": {"interval": 3.0}
        }},
        "nodes": [
            {"name": "deck", "kind": "procedural", "position": [0, 0, 0],
             "procedural": {"source": {"kind": "box", "size": [15, 3, 15]},
                "distributionTransform": {"position": [0, 300, 0]},
                "distribution": {"kind": "generator", "generator": {"name": "tiles", "version": 1,
                    "cellSize": 16.0, "viewDistance": 600.0,)" + extra + R"(
                    "tiles": {"seed": 9, "continentWavelength": 400, "landThreshold": 0.5}}},
                "slabs": true}}
        ]})");
}

struct Run {
    std::unique_ptr<scene::Composition> comp;
    params::ParameterSet params;
    params::Modulator modulator;
};

void build(Run& run, const nlohmann::json& json) {
    assets::AssetRegistry registry{testsupport::processTempDir()};
    auto c = scene::Composition::fromJson(json, registry);
    if (!c) FAIL(c.error().message);
    run.comp = std::move(*c);
    run.comp->attach(run.params, run.modulator);
}

const scene::ProceduralGeometry& deck(const scene::Scene& s) {
    for (const auto& p : s.procedurals) {
        if (p.distribution.generator.isTiles()) return p;
    }
    FAIL("no tiles object");
    return s.procedurals.front();
}

FrameTime frameAt(double t, std::uint64_t i) {
    FrameTime f{};
    f.renderTime = t;
    f.deltaTime = 1.0 / 30.0;
    f.frameIndex = i;
    return f;
}

} // namespace

TEST_CASE("A cloud deck leaves its water cells out, opts out of the dynamics and the contours",
          "[gpu][generator][geography][clouds][adr1966]") {
    Gpu g;
    if (!makeGpu(g)) SKIP("no GPU");
    rendering::SceneRenderer& renderer = *g.renderer;

    // the same deck twice: as authored, and as a cloud layer
    for (const bool cloud : {false, true}) {
        Run run;
        build(run, cloudScene(cloud ? R"("omit": "water", "dynamics": false, "contours": false,)" : ""));
        const FrameTime t = frameAt(4.0, 1);
        run.comp->update(t);
        const scene::Scene& s = run.comp->scene();
        REQUIRE(renderer.renderToImage(s, t, 160, 90).has_value());
        const scene::ProceduralGeometry& pg = deck(s);
        const scene::GeneratorSpec& gs = pg.distribution.generator;
        CHECK(gs.omitWater == cloud);
        const scene::GeneratorWindow w = renderer.procedurals().generatorWindow(pg.name);
        auto records = renderer.procedurals().readInstanceRecords(pg.name);
        REQUIRE(records.has_value());
        int wet = 0;
        int dry = 0;
        int wrong = 0;
        int masks = 0;
        float worstStill = 0.0f;
        const scene::TileDynamics& dyn = s.environment.tileDynamics;
        for (std::int32_t z = 0; z < w.countZ; ++z) {
            for (std::int32_t x = 0; x < w.countX; ++x) {
                const auto& r = (*records)[static_cast<std::size_t>(z * w.countX + x)];
                const scene::TileSample still = scene::generatorTileInWindow(gs, w.originX + x, w.originZ + z, w);
                const bool present = r.scale.x > 0.0f;
                (still.water >= 0.5f ? wet : dry) += 1;
                if (cloud) {
                    // ADR-1966: a wet cell is not emitted; a dry one stands on the STILL ground, no contour bits
                    wrong += present == (still.water >= 0.5f) ? 1 : 0;
                    if (present) {
                        const float top = r.position.y + 0.5f * pg.source.size.y * r.scale.y -
                                          pg.distributionTransform.position.y; // records are in object space
                        worstStill = std::max(worstStill, std::abs(top - still.height));
                        const float packed = r.emissive.w - scene::kTileClassStride *
                                                                std::floor(r.emissive.w / scene::kTileClassStride);
                        masks += (static_cast<int>(std::floor(packed * 0.5f)) % 16) != 0 ? 1 : 0;
                    }
                } else {
                    wrong += present ? 0 : 1;
                    const scene::TileSample moving =
                        scene::generatorTileInWindow(gs, w.originX + x, w.originZ + z, w, &dyn, 4.0f);
                    worstStill = std::max(worstStill, std::abs(moving.height - still.height)); // it does move
                    const float packed = r.emissive.w - scene::kTileClassStride *
                                                            std::floor(r.emissive.w / scene::kTileClassStride);
                    masks += (static_cast<int>(std::floor(packed * 0.5f)) % 16) != 0 ? 1 : 0;
                }
            }
        }
        CAPTURE(cloud, wet, dry, wrong, masks, worstStill);
        CHECK(wet > 200);
        CHECK(dry > 200);
        CHECK(wrong == 0);
        if (cloud) {
            CHECK(worstStill <= 1e-3f); // no waves on a layer that opted out
            CHECK(masks == 0);          // no contour pass
        } else {
            CHECK(worstStill > 0.5f); // the scene's waves reach an ordinary layer
            CHECK(masks > 0);
        }
    }
    CHECK(g.ctx->errorCount() == 0);
}

TEST_CASE("A keyed node transform drifts the deck and the window stays about the camera",
          "[gpu][generator][geography][clouds][adr1966]") {
    Gpu g;
    if (!makeGpu(g)) SKIP("no GPU");
    rendering::SceneRenderer& renderer = *g.renderer;
    Run run;
    build(run, cloudScene(R"("omit": "water", "dynamics": false, "contours": false,)"));
    params::IParameter* position = run.params.find("nodes/deck/position");
    REQUIRE(position != nullptr);
    int i = 0;
    for (const float drift : {0.0f, 137.0f, 941.0f}) {
        position->setBaseComponent(0, drift); // the wind's key, as the timeline writes it
        run.params.resetFinals();
        const FrameTime t = frameAt(1.0 + i, static_cast<std::uint64_t>(i + 1));
        run.comp->update(t);
        const scene::Scene& s = run.comp->scene();
        REQUIRE(renderer.renderToImage(s, t, 160, 90).has_value());
        const scene::ProceduralGeometry& pg = deck(s);
        CHECK(pg.distributionTransform.position.x == drift);
        const scene::GeneratorWindow w = renderer.procedurals().generatorWindow(pg.name);
        const glm::vec3 camGen = rendering::generatorCameraSpace(pg, glm::mat4(1.0f), s.camera.position);
        const scene::GeneratorWindow expect = scene::generatorWindow(pg.distribution.generator, camGen);
        CHECK(w.originX == expect.originX);
        CHECK(w.originZ == expect.originZ);
        // the window's centre, in the world, is within a cell of the camera
        const float cell = pg.distribution.generator.cellSize;
        const float centreX = (static_cast<float>(w.originX) + 0.5f * static_cast<float>(w.countX)) * cell + drift;
        const float centreZ = (static_cast<float>(w.originZ) + 0.5f * static_cast<float>(w.countZ)) * cell;
        CAPTURE(drift, centreX, centreZ);
        CHECK(std::abs(centreX - s.camera.position.x) <= cell);
        CHECK(std::abs(centreZ - s.camera.position.z) <= cell);
        ++i;
    }
    CHECK(g.ctx->errorCount() == 0);
}

TEST_CASE("A keyed omitBelow cuts a cloud deck to its cores, per cell", "[gpu][generator][geography][clouds][adr1966]") {
    Gpu g;
    if (!makeGpu(g)) SKIP("no GPU");
    rendering::SceneRenderer& renderer = *g.renderer;
    Run run;
    build(run, cloudScene(R"("omit": "water", "dynamics": false, "contours": false,)"));
    params::IParameter* below = run.params.find("procedural/deck/distribution/generator/omitBelow");
    REQUIRE(below != nullptr);
    int previous = -1;
    int i = 0;
    for (const float cut : {-10000.0f, 2.0f, 6.0f}) { // off, then the night's cores rising
        below->setBaseComponent(0, cut);
        run.params.resetFinals();
        const FrameTime t = frameAt(1.0 + i, static_cast<std::uint64_t>(i + 1));
        run.comp->update(t);
        const scene::Scene& s = run.comp->scene();
        REQUIRE(renderer.renderToImage(s, t, 160, 90).has_value());
        const scene::ProceduralGeometry& pg = deck(s);
        CHECK(pg.distribution.generator.omitBelow == cut);
        const scene::GeneratorWindow w = renderer.procedurals().generatorWindow(pg.name);
        auto records = renderer.procedurals().readInstanceRecords(pg.name);
        REQUIRE(records.has_value());
        int present = 0;
        int wrong = 0;
        for (std::int32_t z = 0; z < w.countZ; ++z) {
            for (std::int32_t x = 0; x < w.countX; ++x) {
                const auto& r = (*records)[static_cast<std::size_t>(z * w.countX + x)];
                const scene::TileSample still =
                    scene::generatorTileInWindow(pg.distribution.generator, w.originX + x, w.originZ + z, w);
                const bool expect = still.water < 0.5f && still.height >= cut;
                const bool got = r.scale.x > 0.0f;
                present += got ? 1 : 0;
                wrong += got != expect ? 1 : 0;
            }
        }
        CAPTURE(cut, present, wrong);
        CHECK(wrong == 0);
        if (previous >= 0) CHECK(present < previous); // each cut leaves fewer cells
        previous = present;
        ++i;
    }
    CHECK(g.ctx->errorCount() == 0);
}
