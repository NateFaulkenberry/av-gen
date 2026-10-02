// ADR-1071 (cel lighting) and ADR-1072 (the outline): the parts with no GPU in them -- the scene-file
// blocks, the parameters, and the packing the shaders decode.

#include "params/parameter_set.hpp"
#include "rendering/toon_pack.hpp"
#include "scene/post_settings.hpp"
#include "scene/procedural.hpp"
#include "scene/sdf_object.hpp"
#include "scene/toon_shading.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

using namespace avgen;
using Catch::Approx;

TEST_CASE("A toon block reads, writes back exactly and refuses what it does not know", "[toon][adr1071]") {
    scene::ToonShading t;
    const auto j = nlohmann::json::parse(R"({"bands": 3, "softness": 0.05, "terminator": 0.1,
        "shadowColor": [0.2, 0.1, 0.6], "ambient": 0.5, "rimWidth": 0.3, "rimColor": [1, 0.5, 0],
        "rimIntensity": 2, "specular": 1.5, "specularSize": 0.2})");
    REQUIRE(scene::readToonShading(j, t).has_value());
    CHECK(t.bands == 3.0f);
    CHECK(t.shadowColor == glm::vec3(0.2f, 0.1f, 0.6f));
    CHECK(t.rimColor == glm::vec3(1.0f, 0.5f, 0.0f));
    CHECK(t.enabled());
    scene::ToonShading back;
    REQUIRE(scene::readToonShading(scene::toonShadingToJson(t), back).has_value());
    CHECK(scene::toonShadingToJson(back) == scene::toonShadingToJson(t));
    CHECK_FALSE(scene::toonShadingIsDefault(t));
    CHECK(scene::toonShadingIsDefault(scene::ToonShading{}));

    scene::ToonShading u;
    const auto bad = scene::readToonShading(nlohmann::json::parse(R"({"band": 3})"), u);
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().message.find("band") != std::string::npos);
    CHECK_FALSE(scene::readToonShading(nlohmann::json::parse(R"({"rimColor": [1, 2]})"), u).has_value());
    CHECK_FALSE(scene::readToonShading(nlohmann::json::parse(R"({"bands": "3"})"), u).has_value());
}

TEST_CASE("Toon packing: off is all zero, on rounds the bands and folds the colours", "[toon][adr1071]") {
    const auto off = rendering::packToon(scene::ToonShading{});
    for (const glm::vec4& v : off) {
        CHECK(v == glm::vec4(0.0f));
    }
    scene::ToonShading t;
    t.bands = 2.6f;
    t.softness = 0.0f;
    t.shadowColor = {0.5f, 0.25f, 1.0f};
    t.ambient = 0.4f;
    t.rimWidth = 0.3f;
    t.rimColor = {1.0f, 0.5f, 0.0f};
    t.rimIntensity = 3.0f;
    t.specular = 2.0f;
    t.specularSize = 0.1f;
    const auto p = rendering::packToon(t);
    CHECK(p[0].x == 3.0f);
    CHECK(p[0].y == Approx(1e-3f)); // a hard cut, never a zero-width smoothstep
    CHECK(p[0].w == 2.0f);
    CHECK(p[1].x == Approx(0.2f));
    CHECK(p[1].y == Approx(0.1f));
    CHECK(p[1].z == Approx(0.4f));
    CHECK(p[1].w == Approx(0.3f));
    CHECK(p[2].x == Approx(3.0f));
    CHECK(p[2].y == Approx(1.5f));
    CHECK(p[2].w == Approx(0.1f));
    // Half a band and up is one band: the gate and the count agree.
    t.bands = 0.5f;
    CHECK(rendering::packToon(t)[0].x == 1.0f);
    t.bands = 0.49f;
    CHECK(rendering::packToon(t)[0].x == 0.0f);
}

TEST_CASE("A procedural node's material carries toon through its file and its parameters", "[toon][adr1071]") {
    const auto j = nlohmann::json::parse(R"({
        "source": {"kind": "sphere", "radius": 1.0}, "distribution": {"kind": "single"},
        "material": {"baseColor": [1, 1, 1], "toon": {"bands": 2, "rimWidth": 0.2}}})");
    auto g = scene::ProceduralGeometry::fromJson(j);
    REQUIRE(g.has_value());
    CHECK(g->material.toon.bands == 2.0f);
    CHECK(g->material.toon.rimWidth == Approx(0.2f));
    // Written back only because it was authored.
    CHECK(g->toJson().at("material").contains("toon"));
    auto plain = scene::ProceduralGeometry::fromJson(nlohmann::json::parse(
        R"({"source": {"kind": "sphere", "radius": 1.0}, "distribution": {"kind": "single"}})"));
    REQUIRE(plain.has_value());
    CHECK_FALSE(plain->toJson().at("material").contains("toon"));

    params::ParameterSet params;
    auto p = scene::registerProceduralParameters(params, *plain, "procedural/ball/");
    // Registered whatever the file said, so the look can be switched on from the panel.
    auto* bands = params.findAs<float>("procedural/ball/toon/bands");
    REQUIRE(bands != nullptr);
    CHECK(bands->value() == 0.0f);
    CHECK(params.findAs<glm::vec3>("procedural/ball/toon/shadowColor") != nullptr);
    CHECK(params.findAs<float>("procedural/ball/toon/specularSize") != nullptr);
    bands->setBase(3.0f);
    params.findAs<float>("procedural/ball/toon/ambient")->setBase(0.7f);
    params.resetFinals();
    scene::ProceduralGeometry live = *plain;
    REQUIRE(scene::applyProceduralParameterValues(p, *plain, live));
    CHECK(live.material.toon.bands == 3.0f);
    CHECK(live.material.toon.ambient == Approx(0.7f));
    scene::unregisterProceduralParameters(params, p);
    CHECK(params.findAs<float>("procedural/ball/toon/bands") == nullptr);
}

TEST_CASE("An SDF object's material carries toon through its file and its parameters", "[toon][adr1071]") {
    const auto j = nlohmann::json::parse(R"({"name": "orb", "tree": {"root": {"kind": "sphere", "radius": 1.0}},
        "boundsMin": [-1.5, -1.5, -1.5], "boundsMax": [1.5, 1.5, 1.5],
        "material": {"baseColor": [1, 1, 1], "toon": {"bands": 4, "specular": 1}}})");
    auto o = scene::SdfObject::fromJson(j);
    REQUIRE(o.has_value());
    CHECK(o->material.toon.bands == 4.0f);
    CHECK(o->toJson().at("material").at("toon").at("specular") == 1.0f);
    params::ParameterSet params;
    auto p = scene::registerSdfParameters(params, *o, "sdf/orb/");
    auto* bands = params.findAs<float>("sdf/orb/toon/bands");
    REQUIRE(bands != nullptr);
    CHECK(bands->value() == 4.0f);
}

TEST_CASE("The outline's parameters, its scene-file keys and its gate", "[outline][adr1072]") {
    params::ParameterSet params;
    scene::PostSettings defaults;
    CHECK_FALSE(defaults.outline.active());
    auto p = scene::registerPostParameters(params, defaults);
    for (const char* path : {"post/outline/amount", "post/outline/width", "post/outline/intensity",
                             "post/outline/depthThreshold", "post/outline/normalThreshold",
                             "post/outline/silhouette", "post/outline/objectEdges", "post/outline/fadeStart",
                             "post/outline/fadeEnd"}) {
        INFO(path);
        CHECK(params.findAs<float>(path) != nullptr);
    }
    REQUIRE(params.findAs<glm::vec3>("post/outline/color") != nullptr);
    const auto j = nlohmann::json::parse(R"({"outlineAmount": 1, "outlineWidth": 3, "outlineColor": [1, 0.5, 0]})");
    REQUIRE(scene::applyPostJson(j, p).has_value());
    params.resetFinals();
    scene::PostSettings s;
    scene::applyPostParameters(p, s);
    CHECK(s.outline.active());
    CHECK(s.outline.width == 3.0f);
    CHECK(s.outline.color == glm::vec3(1.0f, 0.5f, 0.0f));
    CHECK_FALSE(scene::applyPostJson(nlohmann::json::parse(R"({"outlineColor": 1})"), p).has_value());
}
