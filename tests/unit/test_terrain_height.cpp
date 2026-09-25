// The terrain height bake and the `fogGroundFollow` control (ADR-715, ADR-575 §18).
//
// Three claims, each the kind that fails silently:
//
//   1. **The bake is the terrain.** A known map -- flat, and a V-shaped valley painted from a
//      three-pixel height image, whose every height is a formula -- gives known heights at every
//      grid point and, through the same bilinear the shader runs, between them. A bake taken on the
//      wrong grid, or off by one sample, is a fog that follows a terrain shifted by two metres,
//      which nobody would see and everybody would be looking at.
//   2. **It is baked once.** A rebuild that does not change the terrain reuses the bake by pointer
//      -- ADR-575 §18 forbids per-frame CPU work, and ADR-092's cache is what makes "once" true.
//   3. **The control is reachable and kept.** It reaches the environment through the modulator
//      (the seam four fog controls have died at, ADR-561/565/571), and both serialisers keep it --
//      the project one after a frame has run, because a frame is what writes the live state a
//      save photographs (ADR-264). The GPU half -- 0 is bit-identical, the readers agree -- is
//      `tests/rendering/test_height_fog_gpu.cpp` and `test_terrain_fog_gpu.cpp`.

#include "app/engine.hpp"
#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "params/modulation.hpp"
#include "params/parameter.hpp"
#include "scene/composition.hpp"
#include "world/terrain_height.hpp"
#include "world/world_map.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <unistd.h>

using namespace avgen;
using Catch::Approx;
namespace fs = std::filesystem;

namespace {

// A 640 m map whose height is exactly `60 * |x| / 320`: a V-shaped valley running along Z, floor
// at x = 0, rims 60 m up at the map's east and west edges. Painted from a three-pixel image
// [1, 0, 1] (the image is corner-aligned, so its middle pixel sits on x = 0) with no noise layers,
// so the height is that formula and nothing else -- a terrain whose every height is known.
world::WorldMap valleyMap() {
    world::WorldMap m;
    m.size = glm::vec2(640.0f, 640.0f);
    m.baseHeight = 0.0f;
    auto image = std::make_shared<world::HeightImage>();
    image->width = 3;
    image->height = 1;
    image->samples = {1.0f, 0.0f, 1.0f};
    m.image = image;
    m.imageHeight = 60.0f;
    m.imageBlend = 1.0f;
    m.prepare();
    return m;
}

float valleyHeight(float x) { return 60.0f * std::abs(x) / 320.0f; }

FrameTime frameAt(double seconds) {
    FrameTime t;
    t.renderTime = seconds;
    t.deltaTime = 0.0;
    t.frameIndex = 0;
    return t;
}

fs::path scratch(const char* name) {
    const fs::path dir = fs::temp_directory_path() / ("avgen_terrain_height_" + std::to_string(getpid()) + "_" + name);
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

// A scene with one terrain node on a flat 160 m map, 3 m up, the node lifted 2 m more.
constexpr const char* kTerrainScene = R"({
  "format": "avgen-scene", "version": 1, "name": "terrain-fog",
  "environment": { "volumeDensity": 0.01, "fogHeight": 4.0, "fogHeightFalloff": 0.1 },
  "nodes": [
    { "name": "ground", "kind": "terrain", "position": [0, 2, 0], "rotation": [0, 0, 0], "scale": [1, 1, 1],
      "terrain": { "chunkSize": 40.0, "resolution": 8, "lodLevels": 1 },
      "world": { "name": "flat", "seed": 1, "size": [160.0, 160.0], "baseHeight": 3.0, "layers": [], "features": [] } },
    { "name": "orb", "kind": "orb" }
  ]
})";

} // namespace

TEST_CASE("the height bake of a known terrain gives known heights", "[terrain][height][fog]") {
    SECTION("flat ground is its base height at every sample") {
        world::WorldMap flat;
        flat.size = glm::vec2(640.0f, 640.0f);
        flat.baseHeight = 12.5f;
        flat.prepare();
        const world::TerrainHeightField f = world::bakeTerrainHeight(flat, 7);
        // 2 m samples, vertex-aligned: 320 spacings and a fencepost.
        CHECK(f.spacing == 2.0f);
        CHECK(f.width == 321);
        CHECK(f.depth == 321);
        CHECK(f.origin == glm::vec2(-320.0f, -320.0f));
        CHECK(f.extentMax() == glm::vec2(320.0f, 320.0f));
        CHECK(f.hash == 7);
        REQUIRE(f.heights.size() == 321u * 321u);
        for (const float h : f.heights) {
            REQUIRE(h == 12.5f);
        }
    }

    SECTION("a V-shaped valley is the formula at every sample and between them") {
        const world::WorldMap m = valleyMap();
        // The instrument first: the map really is the valley the test says it is.
        REQUIRE(m.height(glm::vec2(0.0f, 17.0f)) == Approx(0.0f).margin(1e-4));
        REQUIRE(m.height(glm::vec2(-320.0f, 0.0f)) == Approx(60.0f).margin(1e-3));
        REQUIRE(m.height(glm::vec2(160.0f, -90.0f)) == Approx(30.0f).margin(1e-3));

        const world::TerrainHeightField f = world::bakeTerrainHeight(m);
        REQUIRE(f.width == 321);
        int onTheFloor = 0;
        for (std::uint32_t j = 0; j < f.depth; j += 7) {
            for (std::uint32_t i = 0; i < f.width; ++i) {
                const glm::vec2 p = f.origin + glm::vec2(static_cast<float>(i), static_cast<float>(j)) * f.spacing;
                INFO("sample " << i << "," << j << " at x=" << p.x);
                // Bit for bit what the map says there: the bake IS `WorldMap::height` on a grid.
                REQUIRE(f.at(i, j) == m.height(p));
                REQUIRE(f.at(i, j) == Approx(valleyHeight(p.x)).margin(1e-3));
                onTheFloor += f.at(i, j) < 0.5f ? 1 : 0;
            }
        }
        // The valley floor is a grid line (x = 0 is sample 160), so the kink is sampled, not cut.
        CHECK(onTheFloor > 0);
        CHECK(f.at(160, 40) == Approx(0.0f).margin(1e-4));
        // Between samples: the bilinear the shader runs, on a surface that is linear between them.
        for (const glm::vec2 p : {glm::vec2(1.3f, 5.1f), glm::vec2(-47.7f, 200.9f), glm::vec2(311.1f, -319.2f),
                                  glm::vec2(-0.9f, 0.0f)}) {
            INFO("x=" << p.x << " z=" << p.y);
            CHECK(f.bilinear(p) == Approx(valleyHeight(p.x)).margin(1e-3));
        }
    }

    SECTION("placed in the world: translated, scaled, and flat off the edge") {
        auto field = std::make_shared<const world::TerrainHeightField>(world::bakeTerrainHeight(valleyMap()));
        const world::TerrainGround g =
            world::placeTerrainGround(field, glm::vec3(100.0f, 5.0f, -50.0f), glm::vec3(1.0f, 2.0f, 1.0f), false);
        REQUIRE(g.valid());
        CHECK(g.worldMin() == glm::vec2(-220.0f, -370.0f));
        CHECK(g.worldMax() == glm::vec2(420.0f, 270.0f));
        CHECK(g.fade == Approx(32.0f)); // 5% of 640 m
        // Inside: 5 m up, heights doubled.
        CHECK(g.groundAt(glm::vec2(100.0f, 0.0f)) == Approx(5.0f).margin(1e-3));
        CHECK(g.groundAt(glm::vec2(260.0f, -50.0f)) == Approx(5.0f + 2.0f * 30.0f).margin(1e-3));
        // Past the east edge: the edge's height, fading to 0 across `fade` metres. Half-way across
        // is half the edge (5 + 2 * 60 = 125 m).
        CHECK(g.groundAt(glm::vec2(420.0f + 16.0f, 0.0f)) == Approx(62.5f).margin(1e-2));
        // And beyond it, exactly the flat plane's reference: 0.
        CHECK(g.groundAt(glm::vec2(420.0f + 33.0f, 0.0f)) == 0.0f);
        CHECK(g.groundAt(glm::vec2(-5000.0f, 9000.0f)) == 0.0f);
        // The lanes the shaders read agree with the placement.
        CHECK(g.map0() == glm::vec4(-220.0f, -370.0f, 0.5f, 0.5f));
        CHECK(g.map1() == glm::vec4(2.0f, 5.0f, 32.0f, 1.0f));
    }

    SECTION("what is not baked says so") {
        auto field = std::make_shared<const world::TerrainHeightField>(world::bakeTerrainHeight(valleyMap()));
        // A rotated terrain is not honoured, and the ground is then the flat plane everywhere.
        const world::TerrainGround rotated = world::placeTerrainGround(field, glm::vec3(0.0f), glm::vec3(1.0f), true);
        CHECK_FALSE(rotated.valid());
        CHECK(rotated.groundAt(glm::vec2(200.0f, 0.0f)) == 0.0f);
        CHECK(rotated.map1().w == 0.0f);
        const world::TerrainGround none;
        CHECK_FALSE(none.valid());
        CHECK(none.map1() == glm::vec4(0.0f));
    }
}

TEST_CASE("the height sample spacing is 2 m until a side would pass 1024 samples", "[terrain][height]") {
    CHECK(world::terrainHeightSpacing(glm::vec2(160.0f, 160.0f)) == 2.0f);
    CHECK(world::terrainHeightSpacing(glm::vec2(640.0f, 640.0f)) == 2.0f);
    CHECK(world::terrainHeightSpacing(glm::vec2(2046.0f, 100.0f)) == 2.0f);
    CHECK(world::terrainHeightSpacing(glm::vec2(4092.0f, 900.0f)) == Approx(4.0f));
    world::WorldMap huge;
    huge.size = glm::vec2(8000.0f, 3000.0f);
    huge.prepare();
    const world::TerrainHeightField f = world::bakeTerrainHeight(huge);
    CHECK(f.width <= world::kTerrainHeightMaxSamples);
    CHECK(f.depth <= world::kTerrainHeightMaxSamples);
    CHECK(f.extentMax().x >= 4000.0f - 1e-2f); // the whole footprint is still covered
}

TEST_CASE("a terrain node bakes its height once, and the scene carries it", "[terrain][height][fog][composition]") {
    assets::AssetRegistry registry;
    auto comp = scene::Composition::fromJson(nlohmann::json::parse(kTerrainScene), registry);
    REQUIRE(comp.has_value());
    params::ParameterSet params;
    params::Modulator modulator;
    (*comp)->attach(params, modulator);
    (*comp)->update(frameAt(0.0));

    const world::TerrainGround& ground = (*comp)->scene().terrainGround;
    REQUIRE(ground.valid());
    CHECK(ground.field->width == 81); // 160 m at 2 m
    CHECK(ground.field->hash != 0);
    // 3 m of ground under a node lifted 2 m.
    CHECK(ground.groundAt(glm::vec2(0.0f, 0.0f)) == Approx(5.0f));
    CHECK(ground.groundAt(glm::vec2(-79.0f, 63.0f)) == Approx(5.0f));
    // The node owns it -- the rebuild cache, beside the meshes.
    const scene::CompositionNode* node = (*comp)->findNode("ground");
    REQUIRE(node != nullptr);
    CHECK(node->terrainProducts.height.get() == ground.field.get());

    // A rebuild for an unrelated reason reuses the bake: the SAME object, not an equal one.
    const world::TerrainHeightField* before = ground.field.get();
    scene::CompositionNode extra;
    extra.kind = scene::NodeKind::Orb;
    extra.name = "another";
    REQUIRE((*comp)->addNode(std::move(extra)).has_value());
    (*comp)->update(frameAt(0.1));
    REQUIRE((*comp)->scene().terrainGround.valid());
    CHECK((*comp)->scene().terrainGround.field.get() == before);
}

TEST_CASE("a scene with no terrain has no ground to follow", "[terrain][height][fog][composition]") {
    assets::AssetRegistry registry;
    auto comp = scene::Composition::fromJson(nlohmann::json::parse(R"({
      "format": "avgen-scene", "version": 1, "name": "no-terrain",
      "environment": { "volumeDensity": 0.01, "fogGroundFollow": 1.0 },
      "nodes": [ { "name": "orb", "kind": "orb" } ]
    })"),
                                             registry);
    REQUIRE(comp.has_value());
    params::ParameterSet params;
    params::Modulator modulator;
    (*comp)->attach(params, modulator);
    (*comp)->update(frameAt(0.0));
    CHECK_FALSE((*comp)->scene().terrainGround.valid());
    // The control is still the scene's -- it is simply inert, and the renderer takes the flat
    // branch (`frame.fogShape.z` is 0 without a terrain; test_terrain_fog_gpu.cpp holds it to that).
    CHECK((*comp)->scene().environment.fogGroundFollow == 1.0f);
}

TEST_CASE("fogGroundFollow reaches the environment and round-trips the scene file",
          "[fog][environment][height][composition]") {
    assets::AssetRegistry registry;
    nlohmann::json doc = nlohmann::json::parse(kTerrainScene);
    doc["environment"]["fogGroundFollow"] = 0.4f;
    auto comp = scene::Composition::fromJson(doc, registry);
    REQUIRE(comp.has_value());
    params::ParameterSet params;
    params::Modulator modulator;
    (*comp)->attach(params, modulator);
    (*comp)->update(frameAt(0.0));
    CHECK((*comp)->scene().environment.fogGroundFollow == 0.4f);

    params::IParameter* p = params.find("scene/fogGroundFollow");
    REQUIRE(p != nullptr);
    CHECK(p->baseComponent(0) == 0.4f);
    // Clamped to its range: 0 is the plane, 1 is the ground, and there is nothing past either.
    p->setBaseComponent(0, 3.0f);
    CHECK(p->baseComponent(0) == 1.0f);
    // Moved the way a panel moves it, then carried by the modulator the way a frame carries it.
    p->setBaseComponent(0, 0.7f);
    params.resetFinals();
    (*comp)->update(frameAt(0.1));
    CHECK((*comp)->scene().environment.fogGroundFollow == 0.7f);

    const nlohmann::json j = (*comp)->toJson();
    CHECK(j["environment"]["fogGroundFollow"] == 0.7f);
    auto again = scene::Composition::fromJson(j, registry);
    REQUIRE(again.has_value());
    CHECK((*again)->toJson() == j);

    // THE CONTROL: a scene that never set it writes no key, so a file saved before ADR-715
    // round-trips to the same bytes.
    auto plain = scene::Composition::fromJson(nlohmann::json::parse(kTerrainScene), registry);
    REQUIRE(plain.has_value());
    CHECK_FALSE((*plain)->toJson()["environment"].contains("fogGroundFollow"));
}

TEST_CASE("fogGroundFollow survives the project document after a frame has run",
          "[integration][fog][environment][height]") {
    const fs::path dir = scratch("project");
    {
        std::ofstream out(dir / "scene.json");
        out << kTerrainScene;
    }
    app::Engine session(app::EngineMode::Offline);
    REQUIRE(session.loadComposition(dir / "scene.json").has_value());
    params::IParameter* p = session.params().find("scene/fogGroundFollow");
    REQUIRE(p != nullptr);
    p->setBaseComponent(0, 0.55f);
    // A frame first: saves photograph what a frame wrote (ADR-264), and a round trip taken
    // straight after a load passes whatever the frame would have done.
    session.update(frameAt(0.0));
    session.update(frameAt(1.5));
    CHECK(session.scene().environment.fogGroundFollow == 0.55f);
    REQUIRE(session.saveProject(dir / "once.json").has_value());

    app::Engine render(app::EngineMode::Offline);
    REQUIRE(render.loadProject(dir / "once.json").has_value());
    render.update(frameAt(0.0));
    render.update(frameAt(1.5));
    REQUIRE(render.params().find("scene/fogGroundFollow") != nullptr);
    CHECK(render.params().find("scene/fogGroundFollow")->baseComponent(0) == 0.55f);
    CHECK(render.scene().environment.fogGroundFollow == 0.55f);
    CHECK(render.scene().terrainGround.valid());

    // The control: the loaded engine would have said 0 had the project not carried it.
    app::Engine fresh(app::EngineMode::Offline);
    REQUIRE(fresh.loadComposition(dir / "scene.json").has_value());
    fresh.update(frameAt(0.0));
    CHECK(fresh.scene().environment.fogGroundFollow == 0.0f);
    fs::remove_all(dir);
}

// ---- ADR-717: the basin a pooling layer is measured from ----------------------------------------

namespace {

// The valley's mirror: a ridge along Z, its crest 60 m up at x = 0 and the map's edges at 0.
world::WorldMap ridgeMap() {
    world::WorldMap m = valleyMap();
    auto image = std::make_shared<world::HeightImage>();
    image->width = 3;
    image->height = 1;
    image->samples = {0.0f, 1.0f, 0.0f};
    m.image = image;
    m.prepare();
    return m;
}

} // namespace

TEST_CASE("the basin is the ground low-passed, baked beside it", "[terrain][height][fog]") {
    SECTION("flat ground has a flat basin") {
        world::WorldMap flat;
        flat.size = glm::vec2(160.0f, 160.0f);
        flat.baseHeight = 3.0f;
        flat.prepare();
        const world::TerrainHeightField f = world::bakeTerrainHeight(flat);
        REQUIRE(f.pooled());
        for (std::size_t k = 0; k < f.basin.size(); k += 37) {
            CHECK(f.basin[k] == Approx(3.0f).margin(1e-5));
        }
    }
    SECTION("a valley's basin stands above its floor, and a slope's is the slope") {
        // At an explicit 24 m, so the kernel's 72 m reach fits between the fold and the map's edge.
        world::TerrainHeightField f = world::bakeTerrainHeight(valleyMap());
        REQUIRE(f.pooled());
        constexpr float kSigma = 24.0f;
        world::poolTerrainHeight(f, kSigma);
        const auto placed = world::placeTerrainGround(std::make_shared<world::TerrainHeightField>(f), glm::vec3(0.0f),
                                                      glm::vec3(1.0f), false);
        // A Gaussian over |x| * k at the fold is k * sigma * sqrt(2 / pi) above the floor.
        const float k = 60.0f / 320.0f;
        const float lifted = k * kSigma * std::sqrt(2.0f / 3.14159265f);
        CHECK(placed.basinAt(glm::vec2(0.0f, 0.0f)) == Approx(lifted).epsilon(0.02));
        CHECK(placed.groundAt(glm::vec2(0.0f, 0.0f)) == Approx(0.0f).margin(1e-4));
        // Far from the fold and from the edges the ground is linear under the kernel, so the
        // basin IS the ground: a hillside is not a basin.
        CHECK(placed.basinAt(glm::vec2(150.0f, 20.0f)) == Approx(valleyHeight(150.0f)).margin(1e-3));
        CHECK(placed.basinAt(glm::vec2(-150.0f, -70.0f)) == Approx(valleyHeight(-150.0f)).margin(1e-3));
    }
    SECTION("the bake pools at the shipped basin width") {
        const world::TerrainHeightField baked = world::bakeTerrainHeight(valleyMap());
        world::TerrainHeightField again = baked;
        world::poolTerrainHeight(again, world::kTerrainBasinSigma);
        CHECK(again.basin == baked.basin);
        world::poolTerrainHeight(again, 2.0f * world::kTerrainBasinSigma);
        CHECK(again.basin != baked.basin); // the control: the width is not ignored
    }
    SECTION("a ridge's basin lies below its crest") {
        const auto placed = world::placeTerrainGround(
            std::make_shared<world::TerrainHeightField>(world::bakeTerrainHeight(ridgeMap())), glm::vec3(0.0f),
            glm::vec3(1.0f), false);
        CHECK(placed.groundAt(glm::vec2(0.0f)) == Approx(60.0f).margin(1e-3));
        CHECK(placed.basinAt(glm::vec2(0.0f)) < 60.0f - 3.0f);
    }
    SECTION("the reference: pooling 0 is ADR-715's follow exactly, 1 is the basin") {
        const auto placed = world::placeTerrainGround(
            std::make_shared<world::TerrainHeightField>(world::bakeTerrainHeight(valleyMap())), glm::vec3(0.0f, 2.0f, 0.0f),
            glm::vec3(1.0f), false);
        for (const glm::vec2 p : {glm::vec2(0.0f), glm::vec2(33.0f, -12.0f), glm::vec2(-250.0f, 100.0f)}) {
            CHECK(placed.referenceAt(p, 0.7f, 0.0f) == 0.7f * placed.groundAt(p));
            CHECK(placed.referenceAt(p, 0.7f, 1.0f) == Approx(placed.basinAt(p)).margin(1e-5));
            CHECK(placed.referenceAt(p, 0.0f, 0.5f) == Approx(0.5f * placed.basinAt(p)).margin(1e-5));
        }
        CHECK(placed.poolingLane(3.0f) == 1.0f);
        CHECK(placed.poolingLane(0.25f) == 0.25f);
    }
    SECTION("a field that was never pooled has no basin to pool in") {
        auto field = std::make_shared<world::TerrainHeightField>(world::bakeTerrainHeight(valleyMap()));
        field->basin.clear();
        const auto placed = world::placeTerrainGround(field, glm::vec3(0.0f), glm::vec3(1.0f), false);
        CHECK(placed.valid());
        CHECK_FALSE(placed.poolable());
        CHECK(placed.poolingLane(1.0f) == 0.0f);
        CHECK(world::TerrainGround{}.poolingLane(1.0f) == 0.0f);
    }
}

TEST_CASE("fogPooling reaches the environment and round-trips the scene file", "[fog][environment][height][composition]") {
    assets::AssetRegistry registry;
    nlohmann::json doc = nlohmann::json::parse(kTerrainScene);
    doc["environment"]["fogPooling"] = 0.4f;
    auto comp = scene::Composition::fromJson(doc, registry);
    REQUIRE(comp.has_value());
    params::ParameterSet params;
    params::Modulator modulator;
    (*comp)->attach(params, modulator);
    (*comp)->update(frameAt(0.0));
    CHECK((*comp)->scene().environment.fogPooling == 0.4f);
    CHECK((*comp)->scene().terrainGround.poolable()); // the bake carries its basin

    params::IParameter* p = params.find("scene/fogPooling");
    REQUIRE(p != nullptr);
    CHECK(p->baseComponent(0) == 0.4f);
    p->setBaseComponent(0, 3.0f);
    CHECK(p->baseComponent(0) == 1.0f);
    p->setBaseComponent(0, 0.7f);
    params.resetFinals();
    (*comp)->update(frameAt(0.1));
    CHECK((*comp)->scene().environment.fogPooling == 0.7f);

    const nlohmann::json j = (*comp)->toJson();
    CHECK(j["environment"]["fogPooling"] == 0.7f);
    auto again = scene::Composition::fromJson(j, registry);
    REQUIRE(again.has_value());
    CHECK((*again)->toJson() == j);

    // THE CONTROL: a scene that never set it writes no key.
    auto plain = scene::Composition::fromJson(nlohmann::json::parse(kTerrainScene), registry);
    REQUIRE(plain.has_value());
    CHECK_FALSE((*plain)->toJson()["environment"].contains("fogPooling"));
}

TEST_CASE("fogPooling survives the project document after a frame has run", "[integration][fog][environment][height]") {
    const fs::path dir = scratch("pooling");
    {
        std::ofstream out(dir / "scene.json");
        out << kTerrainScene;
    }
    app::Engine session(app::EngineMode::Offline);
    REQUIRE(session.loadComposition(dir / "scene.json").has_value());
    params::IParameter* p = session.params().find("scene/fogPooling");
    REQUIRE(p != nullptr);
    p->setBaseComponent(0, 0.65f);
    session.update(frameAt(0.0));
    session.update(frameAt(1.5));
    CHECK(session.scene().environment.fogPooling == 0.65f);
    REQUIRE(session.saveProject(dir / "once.json").has_value());

    app::Engine render(app::EngineMode::Offline);
    REQUIRE(render.loadProject(dir / "once.json").has_value());
    render.update(frameAt(0.0));
    render.update(frameAt(1.5));
    REQUIRE(render.params().find("scene/fogPooling") != nullptr);
    CHECK(render.params().find("scene/fogPooling")->baseComponent(0) == 0.65f);
    CHECK(render.scene().environment.fogPooling == 0.65f);

    // The control: the loaded engine would have said 0 had the project not carried it.
    app::Engine fresh(app::EngineMode::Offline);
    REQUIRE(fresh.loadComposition(dir / "scene.json").has_value());
    fresh.update(frameAt(0.0));
    CHECK(fresh.scene().environment.fogPooling == 0.0f);
    fs::remove_all(dir);
}
