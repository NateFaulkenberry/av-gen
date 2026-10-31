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
