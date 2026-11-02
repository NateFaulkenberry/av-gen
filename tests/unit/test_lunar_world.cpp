// ADR-1882: the lunar world generator, and ADR-1879's portal block in a composition.

#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "scene/lunar_world.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>

using namespace avgen;
using Catch::Approx;

namespace {

scene::LunarGen small() {
    scene::LunarGen g;
    g.extent = 400.0f;
    g.spacing = 4.0f;
    g.skirtExtent = 1600.0f;
    g.skirtSpacing = 32.0f;
    g.rocks = 0.01f;
    g.heroes = {{0.0f, 0.0f, 160.0f, 1.0f}};
    return g;
}

} // namespace

TEST_CASE("the lunar world is deterministic, cratered where asked, rocky, and its block round-trips", "[lunar-world][adr1882]") {
    const scene::LunarGen g = small();
    REQUIRE(g.validate().has_value());
    const auto a = scene::generateLunarWorld(g);
    const auto b = scene::generateLunarWorld(g);
    REQUIRE(a->height.size() == b->height.size());
    CHECK(a->height == b->height);
    CHECK(a->rockCount > 0u);
    CHECK(a->triangles > 10000u);
    // the hero is a bowl about 0.21 D deep below its rim (Linne-calibrated), give or take the small craters on it
    const float floor = a->sample(0.0f, 0.0f);
    float rim = -1e9f;
    for (int k = 0; k < 16; ++k) {
        const float ang = static_cast<float>(k) * 0.3927f;
        rim = std::max(rim, a->sample(80.0f * std::cos(ang), 80.0f * std::sin(ang)));
    }
    INFO("floor " << floor << " rim " << rim);
    CHECK(rim - floor > 0.12f * 160.0f);
    CHECK(rim - floor < 0.3f * 160.0f);
    // every chunk's vertices are finite and its indices in range
    for (const scene::LunarChunk& ch : a->chunks) {
        REQUIRE(ch.positions.size() == ch.normals.size());
        REQUIRE(ch.positions.size() == ch.uvs.size());
        for (std::uint32_t i : ch.indices) REQUIRE(i < ch.positions.size());
        for (const glm::vec3& p : ch.positions) REQUIRE(std::isfinite(p.x + p.y + p.z));
    }
    // a different seed is a different Moon
    scene::LunarGen other = g;
    other.seed = 9;
    CHECK(scene::generateLunarWorld(other)->height != a->height);
    CHECK(other.hash() != g.hash());

    auto block = scene::lunarWorldFromJson(nlohmann::json::parse(
        R"({"seed": 4, "extent": 400, "spacing": 4, "skirtExtent": 1600, "skirtSpacing": 32,
            "heroes": [{"x": 10, "z": -20, "diameter": 120, "fresh": 0.9}]})"));
    REQUIRE(block.has_value());
    auto back = scene::lunarWorldFromJson(scene::lunarWorldToJson(*block));
    REQUIRE(back.has_value());
    CHECK(back->gen.hash() == block->gen.hash());
    CHECK_FALSE(scene::lunarWorldFromJson(nlohmann::json::parse(R"({"sead": 1})")).has_value());
    CHECK_FALSE(scene::lunarWorldFromJson(nlohmann::json::parse(R"({"spacing": 0.1})")).has_value());
}

TEST_CASE("a portal loads its world, carries the camera through its transform, and hands the renderer its view",
          "[portal][adr1879]") {
    const auto dir = testsupport::processTempDir();
    const nlohmann::json moon = {
        {"format", "avgen-scene"}, {"version", 1}, {"name", "moon"},
        {"camera", {{"mode", 1}, {"position", {0.0, 10.0, 0.0}}, {"target", {0.0, 0.0, -10.0}}}},
        {"lunarWorld", {{"extent", 400}, {"spacing", 8}, {"skirtExtent", 1600}, {"skirtSpacing", 64}, {"rocks", 0}}}};
    std::ofstream(dir / "portal-moon.scene.json") << moon.dump();
    const nlohmann::json canyon = {
        {"format", "avgen-scene"}, {"version", 1}, {"name", "canyon"},
        {"camera", {{"mode", 1}, {"position", {0.0, 30.0, 100.0}}, {"target", {0.0, 30.0, 0.0}}}},
        {"nodes", {{{"name", "rock"}, {"kind", "grid"}}}},
        {"corruption", {{"targets", {"rock"}}, {"focus", {0.0, 30.0, 0.0}}, {"axis", {0.0, 0.0, -1.0}}, {"amount", 1.0}}},
        {"portal", {{"world", "portal-moon.scene.json"}, {"exit", {50.0, 20.0, 60.0}}, {"exitYaw", 90.0}}}};
    std::ofstream(dir / "portal-canyon.scene.json") << canyon.dump();
    assets::AssetRegistry registry{dir};
    auto c = scene::Composition::loadFile(dir / "portal-canyon.scene.json", registry);
    REQUIRE(c.has_value());
    params::ParameterSet params;
    params::Modulator modulator;
    (*c)->attach(params, modulator);
    params.resetFinals();
    FrameTime ft{};
    ft.renderTime = 1.0;
    (*c)->update(ft);
    const scene::Scene& s = (*c)->scene();
    REQUIRE(s.portal.active());
    CHECK_FALSE(s.portal.crossed); // no flight here: the camera never crosses
    REQUIRE((*c)->portalWorld() != nullptr);
    const float ground = (*c)->portalWorld()->lunarWorld().world->sample(50.0f, 60.0f);
    // the camera, 100 m in front of the focus along +z, lands 100 m from the exit along the turned axis:
    // a yaw of 90 degrees takes +z to -x (x' = cos x - sin z, z' = sin x + cos z)
    const glm::vec3 expect = glm::vec3(50.0f - 100.0f, ground + 20.0f + 0.0f, 60.0f);
    const glm::vec3 got = s.portal.world->camera.position;
    INFO("got (" << got.x << ", " << got.y << ", " << got.z << "), expected (" << expect.x << ", " << expect.y << ", " << expect.z << ")");
    CHECK(glm::length(got - expect) < 1e-2f);
    // the view direction turns with it
    const glm::vec3 fwd = glm::normalize(s.portal.world->camera.target - got);
    CHECK(fwd.x == Approx(1.0f).margin(1e-3));
}

TEST_CASE("portals nest: the camera crosses into the Moon, then through the Moon's floor into a third world",
          "[portal][gate-c]") {
    const auto dir = testsupport::processTempDir();
    const nlohmann::json third = {
        {"format", "avgen-scene"}, {"version", 1}, {"name", "third"},
        {"camera", {{"mode", 1}, {"position", {0.0, 10.0, 0.0}}, {"target", {0.0, 0.0, -10.0}}}},
        {"nodes", {{{"name", "ground"}, {"kind", "grid"}}}}};
    std::ofstream(dir / "nest-third.scene.json") << third.dump();
    // the Moon: a floor portal (axis down; its focus y is a height above the generated ground) to the third world
    const nlohmann::json moon = {
        {"format", "avgen-scene"}, {"version", 1}, {"name", "moon"},
        {"camera", {{"mode", 1}, {"position", {0.0, 10.0, 0.0}}, {"target", {0.0, 0.0, -10.0}}}},
        {"lunarWorld", {{"extent", 400}, {"spacing", 8}, {"skirtExtent", 1600}, {"skirtSpacing", 64}, {"rocks", 0}}},
        {"corruption", {{"targets", {"lunar.surface"}}, {"focus", {0.0, 0.0, 0.0}}, {"axis", {0.0, -1.0, 0.0}}, {"amount", 1.0},
                        {"width", 40.0}, {"height", 40.0}}},
        // Gate C 2: the floor (axis down) turned onto a wall facing +x: a camera dropping in, its up ahead (-z), leaves level
        {"portal", {{"world", "nest-third.scene.json"}, {"exit", {500.0, 300.0, 500.0}}, {"exitAxis", {1.0, 0.0, 0.0}},
                    {"entryUp", {0.0, 0.0, -1.0}}, {"exitUp", {0.0, 1.0, 0.0}}}}};
    std::ofstream(dir / "nest-moon.scene.json") << moon.dump();
    const nlohmann::json canyon = {
        {"format", "avgen-scene"}, {"version", 1}, {"name", "canyon"},
        {"camera", {{"mode", 1}, {"position", {0.0, 30.0, 100.0}}, {"target", {0.0, 30.0, 0.0}}}},
        {"nodes", {{{"name", "rock"}, {"kind", "grid"}}}},
        {"corruption", {{"targets", {"rock"}}, {"focus", {0.0, 30.0, 0.0}}, {"axis", {0.0, 0.0, -1.0}}, {"amount", 1.0}}},
        // after the crossing at 2 s: knots over the Moon (heights above its ground), down through its floor's focus at 6 s
        {"portal", {{"world", "nest-moon.scene.json"}, {"exit", {0.0, 20.0, 100.0}}, {"exitYaw", 0.0},
                    {"flight", {{0.0, 20.0, 5.0, 4.0}, {0.0, 0.0, 0.0, 6.0}, {0.0, -30.0, -2.0, 7.0}}}}}};
    std::ofstream(dir / "nest-canyon.scene.json") << canyon.dump();
    assets::AssetRegistry registry{dir};
    auto c = scene::Composition::loadFile(dir / "nest-canyon.scene.json", registry);
    REQUIRE(c.has_value());
    params::ParameterSet params;
    params::Modulator modulator;
    (*c)->attach(params, modulator);
    params.resetFinals();
    // the source's camera as a pure function of time: straight through the wall's focus at 2 s
    (*c)->setCameraPath([](double t) {
        const glm::vec3 eye(0.0f, 30.0f, 100.0f - 50.0f * static_cast<float>(t));
        return std::optional<scene::Composition::PortalPose>(scene::Composition::PortalPose{eye, eye + glm::vec3(0, 0, -1), glm::vec3(0, 1, 0)});
    });
    const auto at = [&](double t) {
        FrameTime ft{};
        ft.renderTime = t;
        (*c)->update(ft);
    };
    at(1.0);
    CHECK((*c)->portalCrossingSeconds() == Approx(2.0).margin(0.02));
    CHECK_FALSE((*c)->scene().portal.crossed);
    at(3.0);
    CHECK((*c)->scene().portal.crossed);
    scene::Composition* moonWorld = (*c)->portalWorld();
    REQUIRE(moonWorld != nullptr);
    CHECK(moonWorld->portalCrossingSeconds() == Approx(6.0).margin(0.05));
    CHECK_FALSE(moonWorld->scene().portal.crossed);
    at(6.0 + 1.0 / 60.0);
    REQUIRE(moonWorld->scene().portal.active());
    CHECK(moonWorld->scene().portal.crossed);
    // just past the floor, the third world's camera is at the exit (the floor's focus lands there), leaving the wall
    // level along +x with the world's up: the drop turned through the portal
    const scene::Scene& thirdScene = moonWorld->portalWorld()->scene();
    const scene::Camera& cam = thirdScene.camera;
    INFO("third camera (" << cam.position.x << ", " << cam.position.y << ", " << cam.position.z << ")");
    CHECK(glm::length(cam.position - glm::vec3(500.0f, 300.0f, 500.0f)) < 3.0f);
    const glm::vec3 fwd = glm::normalize(cam.target - cam.position);
    INFO("forward (" << fwd.x << ", " << fwd.y << ", " << fwd.z << "), up (" << cam.up.x << ", " << cam.up.y << ", " << cam.up.z << ")");
    CHECK(fwd.x > 0.9f);
    CHECK(cam.up.y > 0.9f);
    // the rock behind the exit wall is clipped for the camera: the plane x = 500, behind it x < 500
    CHECK(thirdScene.portalClip.x == Approx(1.0f));
    CHECK(thirdScene.portalClip.w == Approx(-500.0f));
}

TEST_CASE("the lunar curvature radius brings the ground's fall within reach (Art Pass 2's terminator)",
          "[lunar-world][pass2]") {
    scene::LunarGen g = small();
    scene::LunarGen curved = g;
    curved.curvature = 4000.0f;
    REQUIRE(curved.validate().has_value());
    CHECK(curved.hash() != g.hash());
    const auto flat = scene::generateLunarWorld(g);
    const auto bent = scene::generateLunarWorld(curved);
    // at 150 m from the centre the ground has fallen d^2 / 2R further (the Moon's own radius barely moves it)
    const float drop = flat->sample(150.0f, 0.0f) - bent->sample(150.0f, 0.0f);
    CHECK(drop == Approx(150.0f * 150.0f / (2.0f * 4000.0f)).margin(0.05f));
    auto block = scene::lunarWorldFromJson(nlohmann::json::parse(R"({"extent": 400, "spacing": 4, "skirtExtent": 1600,
        "skirtSpacing": 32, "curvature": 4000})"));
    REQUIRE(block.has_value());
    CHECK(block->gen.curvature == Approx(4000.0f));
    CHECK(scene::lunarWorldFromJson(scene::lunarWorldToJson(*block))->gen.hash() == block->gen.hash());
    CHECK_FALSE(scene::lunarWorldFromJson(nlohmann::json::parse(R"({"curvature": 10})")).has_value());
}

TEST_CASE("a journey returns to its root: the root's look turns at the first crossing, its camera follows the return",
          "[portal][pass2][adr1885][adr1886]") {
    const auto dir = testsupport::processTempDir();
    const nlohmann::json moon = {
        {"format", "avgen-scene"}, {"version", 1}, {"name", "moon"},
        {"camera", {{"mode", 1}, {"position", {0.0, 10.0, 0.0}}, {"target", {0.0, 0.0, -10.0}}}},
        {"lunarWorld", {{"extent", 400}, {"spacing", 8}, {"skirtExtent", 1600}, {"skirtSpacing", 64}, {"rocks", 0}}},
        {"corruption", {{"targets", {"lunar.surface"}}, {"focus", {0.0, 0.0, 0.0}}, {"axis", {0.0, -1.0, 0.0}}, {"amount", 1.0},
                        {"width", 40.0}, {"height", 40.0}}},
        // back to the root: through a wall facing +x at (500, 30, 500)
        {"portal", {{"world", "@root"}, {"exit", {500.0, 30.0, 500.0}}, {"exitAxis", {1.0, 0.0, 0.0}},
                    {"entryUp", {0.0, 0.0, -1.0}}, {"exitUp", {0.0, 1.0, 0.0}}}}};
    std::ofstream(dir / "ret-moon.scene.json") << moon.dump();
    const nlohmann::json canyon = {
        {"format", "avgen-scene"}, {"version", 1}, {"name", "canyon"},
        {"camera", {{"mode", 1}, {"position", {0.0, 30.0, 100.0}}, {"target", {0.0, 30.0, 0.0}}}},
        {"environment", {{"intensity", 0.5}}},
        {"nodes", {{{"name", "rock"}, {"kind", "grid"}}, {{"name", "camp"}, {"kind", "grid"}, {"visible", false}}}},
        {"corruption", {{"targets", {"rock"}}, {"focus", {0.0, 30.0, 0.0}}, {"axis", {0.0, 0.0, -1.0}}, {"amount", 1.0}}},
        {"portal", {{"world", "ret-moon.scene.json"}, {"exit", {0.0, 20.0, 100.0}}, {"exitYaw", 0.0},
                    {"flight", {{0.0, 20.0, 5.0, 4.0}, {0.0, 0.0, 0.0, 6.0}, {0.0, -30.0, -2.0, 7.0}}}}},
        {"looks", {{{"name", "dusk"}, {"from", 1}, {"params", {{"env/intensity", 0.25}, {"nodes/camp/visible", true}}}}}}};
    std::ofstream(dir / "ret-canyon.scene.json") << canyon.dump();
    assets::AssetRegistry registry{dir};
    auto c = scene::Composition::loadFile(dir / "ret-canyon.scene.json", registry);
    REQUIRE(c.has_value());
    params::ParameterSet params;
    params::Modulator modulator;
    (*c)->attach(params, modulator);
    (*c)->setCameraPath([](double t) {
        const glm::vec3 eye(0.0f, 30.0f, 100.0f - 50.0f * static_cast<float>(t));
        return std::optional<scene::Composition::PortalPose>(scene::Composition::PortalPose{eye, eye + glm::vec3(0, 0, -1), glm::vec3(0, 1, 0)});
    });
    const auto at = [&](double t) {
        params.resetFinals();
        FrameTime ft{};
        ft.renderTime = t;
        (*c)->update(ft);
    };
    const auto envIntensity = [&]() { return params.find("env/intensity")->finalComponent(0); };
    const auto campVisible = [&]() { return params.find("nodes/camp/visible")->finalComponent(0) > 0.5f; };
    const auto rootClipped = [&]() { return glm::length((*c)->scene().portalClip) > 0.0f; };
    at(1.0);
    CHECK(envIntensity() == Approx(0.5f));
    CHECK_FALSE(campVisible());
    // the return's exit plane must not clip the root while the camera is still in it (it cut the opening's
    // foreground away)
    CHECK_FALSE(rootClipped());
    at(3.0); // in the Moon: the root is the frame of the Moon's portal view, already at dusk
    CHECK((*c)->scene().portal.crossed);
    CHECK(envIntensity() == Approx(0.25f));
    CHECK(campVisible());
    scene::Composition* moonWorld = (*c)->portalWorld();
    REQUIRE(moonWorld != nullptr);
    CHECK(moonWorld->scene().portal.world == &(*c)->scene()); // the Moon's portal shows the root itself
    CHECK(moonWorld->portalCrossingSeconds() == Approx(6.0).margin(0.05));
    CHECK(rootClipped()); // seen through the Moon's portal, from behind the wall: the rock behind the exit is clipped
    at(6.0 + 1.0 / 60.0); // back in the root: no portal, its camera at the wall's exit
    CHECK_FALSE((*c)->scene().portal.active());
    const scene::Camera& cam = (*c)->scene().camera;
    INFO("root camera (" << cam.position.x << ", " << cam.position.y << ", " << cam.position.z << ")");
    CHECK(glm::length(cam.position - glm::vec3(500.0f, 30.0f, 500.0f)) < 3.0f);
    CHECK(rootClipped()); // for a second after the crossing
    at(1.0); // a seek back is the day again
    CHECK(envIntensity() == Approx(0.5f));
    CHECK_FALSE(campVisible());
    CHECK_FALSE(rootClipped());
}

TEST_CASE("a light's flicker is a pure function of the clock", "[pass2][adr1888]") {
    const auto dir = testsupport::processTempDir();
    const nlohmann::json s = {
        {"format", "avgen-scene"}, {"version", 1}, {"name", "fire"},
        {"camera", {{"mode", 1}, {"position", {0.0, 3.0, 10.0}}, {"target", {0.0, 0.0, 0.0}}}},
        {"nodes", {{{"name", "ground"}, {"kind", "grid"}}}},
        {"lights", {{{"name", "fire"}, {"type", "point"}, {"position", {0.0, 1.0, 0.0}}, {"intensity", 100.0},
                     {"flicker", 0.4}, {"flickerRate", 3.0}}}}};
    std::ofstream(dir / "fire.scene.json") << s.dump();
    assets::AssetRegistry registry{dir};
    auto c = scene::Composition::loadFile(dir / "fire.scene.json", registry);
    REQUIRE(c.has_value());
    params::ParameterSet params;
    params::Modulator modulator;
    (*c)->attach(params, modulator);
    const auto intensityAt = [&](double t) {
        params.resetFinals();
        FrameTime ft{};
        ft.renderTime = t;
        (*c)->update(ft);
        for (const auto& l : (*c)->scene().lights) {
            if (l.name == "fire") return l.intensity;
        }
        return -1.0f;
    };
    float lo = 1e9f, hi = -1e9f;
    for (int k = 0; k < 120; ++k) {
        const float v = intensityAt(k / 30.0);
        lo = std::min(lo, v);
        hi = std::max(hi, v);
        CHECK(v >= 0.0f);
        CHECK(v <= 100.0f * 1.41f);
    }
    CHECK(hi - lo > 25.0f);                       // it flickers
    CHECK(intensityAt(1.234) == intensityAt(1.234)); // and a seek lands on the same value
}

TEST_CASE("a journey goes out and home twice: the second leg opens at its own site after the first return",
          "[portal][pass2][adr1895]") {
    const auto dir = testsupport::processTempDir();
    const auto world = [&](const char* name, glm::vec3 wallExit) {
        return nlohmann::json{
            {"format", "avgen-scene"}, {"version", 1}, {"name", name},
            {"camera", {{"mode", 1}, {"position", {0.0, 10.0, 0.0}}, {"target", {0.0, 0.0, -10.0}}}},
            {"lunarWorld", {{"extent", 400}, {"spacing", 8}, {"skirtExtent", 1600}, {"skirtSpacing", 64}, {"rocks", 0}}},
            {"corruption", {{"targets", {"lunar.surface"}}, {"focus", {0.0, 0.0, 0.0}}, {"axis", {0.0, -1.0, 0.0}}, {"amount", 1.0},
                            {"width", 40.0}, {"height", 40.0}}},
            {"portal", {{"world", "@root"}, {"exit", {wallExit.x, wallExit.y, wallExit.z}}, {"exitAxis", {1.0, 0.0, 0.0}},
                        {"entryUp", {0.0, 0.0, -1.0}}, {"exitUp", {0.0, 1.0, 0.0}}}}};
    };
    std::ofstream(dir / "legs-moon.scene.json") << world("moon", glm::vec3(500.0f, 30.0f, 500.0f)).dump();
    std::ofstream(dir / "legs-mars.scene.json") << world("mars", glm::vec3(-500.0f, 30.0f, 900.0f)).dump();
    const nlohmann::json canyon = {
        {"format", "avgen-scene"}, {"version", 1}, {"name", "canyon"},
        {"camera", {{"mode", 1}, {"position", {0.0, 30.0, 100.0}}, {"target", {0.0, 30.0, 0.0}}}},
        {"nodes", {{{"name", "rock"}, {"kind", "grid"}}}},
        {"corruption", {{"targets", {"rock"}}, {"focus", {0.0, 30.0, 0.0}}, {"axis", {0.0, 0.0, -1.0}}, {"amount", 1.0}}},
        // leg 1: to the Moon; its flight comes down through the Moon's floor at 6 s
        {"portal", {{"world", "legs-moon.scene.json"}, {"exit", {0.0, 20.0, 100.0}}, {"exitYaw", 0.0},
                    {"flight", {{0.0, 20.0, 5.0, 4.0}, {0.0, 0.0, 0.0, 6.0}, {0.0, -30.0, -2.0, 7.0}}}}},
        // leg 2: from the wall the Moon returns to (+x out of it at (500, 30, 500)), along +x into a site at x = 560
        {"portals", {{{"world", "legs-mars.scene.json"}, {"exit", {0.0, 20.0, 100.0}}, {"exitYaw", 0.0},
                      {"entry", {560.0, 30.0, 500.0}}, {"entryAxis", {1.0, 0.0, 0.0}},
                      {"flight", {{0.0, 20.0, 5.0, 12.0}, {0.0, 0.0, 0.0, 14.0}, {0.0, -30.0, -2.0, 15.0}}}}}},
        {"looks", {{{"name", "dusk"}, {"from", 1}, {"params", {{"env/intensity", 0.25}}}},
                   {{"name", "night"}, {"from", 3}, {"params", {{"env/intensity", 0.05}}}}}}};
    nlohmann::json canyonFile = canyon;
    // a small canyon world (as in production, where the root's own camera is its flythrough)
    canyonFile["attractorWorld"] = {{"seed", 3}, {"resolution", 256}, {"meshSpacing", 16.0}, {"meshMinError", 4.0}, {"camera", "flight"}};
    std::ofstream(dir / "legs-canyon.scene.json") << canyonFile.dump();
    assets::AssetRegistry registry{dir};
    auto c = scene::Composition::loadFile(dir / "legs-canyon.scene.json", registry);
    REQUIRE(c.has_value());
    REQUIRE((*c)->portalLegCount() == 2);
    params::ParameterSet params;
    params::Modulator modulator;
    (*c)->attach(params, modulator);
    (*c)->setCameraPath([](double t) {
        const glm::vec3 eye(0.0f, 30.0f, 100.0f - 50.0f * static_cast<float>(t));
        return std::optional<scene::Composition::PortalPose>(scene::Composition::PortalPose{eye, eye + glm::vec3(0, 0, -1), glm::vec3(0, 1, 0)});
    });
    const auto at = [&](double t) {
        params.resetFinals();
        FrameTime ft{};
        ft.renderTime = t;
        (*c)->update(ft);
    };
    const auto env = [&]() { return params.find("env/intensity")->finalComponent(0); };
    at(1.0);
    CHECK((*c)->activePortalLeg() == 0);
    at(3.0);
    CHECK((*c)->scene().portal.crossed); // in the Moon
    at(7.0); // home at dusk (the Moon's crossing at 6 s), out of the wall along +x: leg 2 is next, at its own site
    CHECK((*c)->activePortalLeg() == 1);
    CHECK(env() == Approx(0.25f));
    CHECK(glm::length((*c)->scene().corruption.focus - glm::vec3(560.0f, 30.0f, 500.0f)) < 1e-3f);
    CHECK((*c)->scene().portal.active());
    CHECK_FALSE((*c)->scene().portal.crossed);
    const auto crossings = (*c)->journeyCrossings();
    REQUIRE(crossings.size() >= 3);
    CHECK(crossings[0] == Approx(2.0).margin(0.05));  // into the Moon (z from 100 to 0 at 50 m/s)
    CHECK(crossings[1] == Approx(6.0).margin(0.05));  // home
    INFO("leg 2 crossing " << crossings[2]);
    CHECK(crossings[2] > 6.0);
    CHECK(crossings[2] < 12.0);                         // the return flight flies on along +x through x = 560
    // the second leg's world sees the journey's camera carried through, not the root's stale scene camera: with no
    // camera path of its own (as in production), the Mars camera is continuous across the crossing
    (*c)->setCameraPath(nullptr);
    scene::Composition* mars = (*c)->portalWorld();
    REQUIRE(mars != nullptr);
    at(crossings[2] - 0.034);
    const glm::vec3 before = mars->scene().camera.position;
    at(crossings[2] + 0.034);
    const glm::vec3 after = mars->scene().camera.position;
    INFO("before (" << before.x << ", " << before.y << ", " << before.z << ") after (" << after.x << ", " << after.y << ", " << after.z << ")");
    CHECK(glm::length(after - before) < 5.0f);
    at(crossings[2] + 0.5); // in Mars, the canyon at night behind its portal
    CHECK((*c)->scene().portal.crossed);
    CHECK(env() == Approx(0.05f));
    at(15.0); // home again: the second return
    CHECK_FALSE((*c)->scene().portal.active());
    const scene::Camera& cam = (*c)->scene().camera;
    INFO("root camera (" << cam.position.x << ", " << cam.position.y << ", " << cam.position.z << ")");
    CHECK(glm::length(cam.position - glm::vec3(-500.0f, 30.0f, 900.0f)) < 60.0f); // near Mars's return exit
    at(1.0); // a seek back: the first leg, the day
    CHECK((*c)->activePortalLeg() == 0);
    CHECK(glm::length((*c)->scene().corruption.focus - glm::vec3(0.0f, 30.0f, 0.0f)) < 1e-3f);
}

TEST_CASE("Mars: a mesa stands flat-topped with a steep wall, yardangs run along the wind", "[lunar-world][pass2][adr1896]") {
    scene::LunarGen moon;
    moon.extent = 1200.0f;
    moon.spacing = 4.0f;
    moon.skirtExtent = 4800.0f;
    moon.skirtSpacing = 64.0f;
    moon.craters = 0.0f;
    moon.rocks = 0.0f;
    moon.relief = 0.0f;
    scene::LunarGen mars = moon;
    mars.mesas.push_back(scene::LunarMesa{0.0f, 0.0f, 200.0f, 120.0f, 0.0f, 100.0f, 3});
    CHECK(mars.hash() != moon.hash());
    scene::LunarGen same = moon;
    same.yardangs = 0.0f;
    CHECK(same.hash() == moon.hash()); // absent, the Moon is the Moon
    auto flat = scene::generateLunarWorld(moon);
    auto w = scene::generateLunarWorld(mars);
    const auto rise = [&](float x, float z) { return w->sample(x, z) - flat->sample(x, z); };
    CHECK(rise(0.0f, 0.0f) > 85.0f);          // the plateau
    CHECK(rise(100.0f, 30.0f) > 85.0f);
    CHECK(rise(420.0f, 0.0f) < 10.0f);        // the plain beyond the apron
    CHECK(rise(0.0f, 260.0f) < 10.0f);
    // the wall is steep: most of the height is lost within a quarter of the plateau's half-width
    const float drop = rise(150.0f, 0.0f) - rise(250.0f, 0.0f);
    CHECK(drop > 60.0f);
    // yardangs: more relief across the wind than along it
    scene::LunarGen yard = moon;
    yard.yardangs = 8.0f;
    yard.wind = 0.0f; // along +x
    auto y = scene::generateLunarWorld(yard);
    float along = 0.0f, across = 0.0f;
    for (int i = 0; i < 100; ++i) {
        along += std::abs(y->sample(-300.0f + 6.0f * static_cast<float>(i + 1), 50.0f) - y->sample(-300.0f + 6.0f * static_cast<float>(i), 50.0f));
        across += std::abs(y->sample(50.0f, -300.0f + 6.0f * static_cast<float>(i + 1)) - y->sample(50.0f, -300.0f + 6.0f * static_cast<float>(i)));
    }
    CHECK(across > 2.0f * along);
    // a pad eases the ground to its level (a pyramid's footing)
    scene::LunarGen padded = yard;
    padded.pads.push_back(scene::LunarPad{0.0f, 0.0f, 150.0f, 2.0f});
    auto pw = scene::generateLunarWorld(padded);
    for (float x : {-80.0f, 0.0f, 60.0f, 110.0f}) CHECK(pw->sample(x, 20.0f) == Approx(2.0f).margin(0.05f));
    // JSON round-trip
    scene::LunarWorldBlock b;
    b.enabled = true;
    b.gen = mars;
    b.gen.yardangs = 6.0f;
    b.gen.wind = 30.0f;
    b.gen.pads.push_back(scene::LunarPad{10.0f, 20.0f, 90.0f, -3.0f});
    auto back = scene::lunarWorldFromJson(scene::lunarWorldToJson(b));
    REQUIRE(back.has_value());
    CHECK(back->gen.hash() == b.gen.hash());
}

TEST_CASE("a world reached through a portal finds its way out only after the camera has arrived",
          "[portal][pass2][adr1899]") {
    const auto dir = testsupport::processTempDir();
    // the sea: its own way home is a plane at z = 150 that the root's camera, carried through the portal before the
    // crossing, passes at 1 s; the camera, once there, flies out to z = 250 and back through it at about 6 s
    const nlohmann::json sea = {
        {"format", "avgen-scene"}, {"version", 1}, {"name", "sea"},
        {"camera", {{"mode", 1}, {"position", {0.0, 10.0, 0.0}}, {"target", {0.0, 0.0, -10.0}}}},
        {"lunarWorld", {{"extent", 400}, {"spacing", 8}, {"skirtExtent", 1600}, {"skirtSpacing", 64}, {"rocks", 0},
                        {"craters", 0}, {"relief", 0}}},
        {"corruption", {{"targets", {"lunar.surface"}}, {"focus", {0.0, 20.0, 150.0}}, {"axis", {0.0, 0.0, -1.0}},
                        {"amount", 1.0}, {"width", 40.0}, {"height", 40.0}}},
        {"portal", {{"world", "@root"}, {"exit", {0.0, 30.0, -300.0}}, {"exitAxis", {0.0, 0.0, -1.0}},
                    {"entryUp", {0.0, 1.0, 0.0}}, {"exitUp", {0.0, 1.0, 0.0}}}}};
    std::ofstream(dir / "home-sea.scene.json") << sea.dump();
    const nlohmann::json root = {
        {"format", "avgen-scene"}, {"version", 1}, {"name", "root"},
        {"camera", {{"mode", 1}, {"position", {0.0, 30.0, 100.0}}, {"target", {0.0, 30.0, 0.0}}}},
        {"nodes", {{{"name", "rock"}, {"kind", "grid"}}}},
        {"corruption", {{"targets", {"rock"}}, {"focus", {0.0, 30.0, 0.0}}, {"axis", {0.0, 0.0, -1.0}}, {"amount", 1.0}}},
        {"portal", {{"world", "home-sea.scene.json"}, {"exit", {0.0, 20.0, 100.0}}, {"exitYaw", 0.0},
                    {"flight", {{0.0, 20.0, 180.0, 3.5}, {0.0, 20.0, 250.0, 5.0}, {0.0, 20.0, 100.0, 7.0}}}}}};
    std::ofstream(dir / "home-root.scene.json") << root.dump();
    assets::AssetRegistry registry{dir};
    auto c = scene::Composition::loadFile(dir / "home-root.scene.json", registry);
    REQUIRE(c.has_value());
    params::ParameterSet params;
    params::Modulator modulator;
    (*c)->attach(params, modulator);
    (*c)->setCameraPath([](double t) {
        const glm::vec3 eye(0.0f, 30.0f, 100.0f - 50.0f * static_cast<float>(t));
        return std::optional<scene::Composition::PortalPose>(scene::Composition::PortalPose{eye, eye + glm::vec3(0, 0, -1), glm::vec3(0, 1, 0)});
    });
    for (double t = 0.0; t <= 9.0; t += 0.25) {
        params.resetFinals();
        FrameTime ft{};
        ft.renderTime = t;
        (*c)->update(ft);
    }
    const auto crossings = (*c)->journeyCrossings();
    REQUIRE(crossings.size() >= 2);
    CHECK(crossings[0] == Approx(2.0).margin(0.05)); // into the sea
    INFO("home crossing " << crossings[1]);
    CHECK(crossings[1] > crossings[0]);              // never before the way in (it was found at 1 s)
    CHECK(crossings[1] > 5.0);
    CHECK(crossings[1] < 7.0);
}
