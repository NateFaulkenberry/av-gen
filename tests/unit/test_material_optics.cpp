// ADR-1143 (thin film and anisotropy in pbr_shade): the parts with no GPU in them -- the scene-file
// blocks on every owner that authors a material, the parameters, the packing the shader decodes, and the
// CPU twin of the thin-film tint the shader evaluates. The ObjectUniforms layout (optics at 512, 528
// bytes in a 768-byte slot) is pinned by static_asserts in scene_renderer.hpp and, against the WGSL
// mirror, by test_renderer_layout_guards.cpp.

#include "assets/asset_registry.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "pathtrace/snapshot.hpp"
#include "rendering/optics_pack.hpp"
#include "scene/composition.hpp"
#include "scene/material_optics.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/procedural.hpp"
#include "scene/scene.hpp"
#include "scene/sdf_object.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace avgen;
using Catch::Approx;

namespace {

// Hue in degrees (0..360) of a linear RGB triple, the HSV way.
float hueDegrees(const glm::vec3& c) {
    const float mx = std::max({c.r, c.g, c.b});
    const float mn = std::min({c.r, c.g, c.b});
    const float d = mx - mn;
    if (d <= 1e-6f) {
        return 0.0f;
    }
    float h = 0.0f;
    if (mx == c.r) {
        h = std::fmod((c.g - c.b) / d, 6.0f);
    } else if (mx == c.g) {
        h = (c.b - c.r) / d + 2.0f;
    } else {
        h = (c.r - c.g) / d + 4.0f;
    }
    h *= 60.0f;
    return h < 0.0f ? h + 360.0f : h;
}

constexpr float kSteelF0 = 0.56f; // a polished steel's normal-incidence reflectance (luminance)

} // namespace

TEST_CASE("Thin-film and anisotropy blocks read, write back exactly and refuse what they do not know",
          "[material_optics][adr1143]") {
    scene::ThinFilm t;
    REQUIRE(scene::readThinFilm(nlohmann::json::parse(R"({"thickness": 60, "ior": 2.2})"), t).has_value());
    CHECK(t.thickness == 60.0f);
    CHECK(t.ior == Approx(2.2f));
    CHECK(t.enabled());
    scene::ThinFilm tBack;
    REQUIRE(scene::readThinFilm(scene::thinFilmToJson(t), tBack).has_value());
    CHECK(scene::thinFilmToJson(tBack) == scene::thinFilmToJson(t));
    CHECK_FALSE(scene::thinFilmIsDefault(t));
    CHECK(scene::thinFilmIsDefault(scene::ThinFilm{}));
    CHECK_FALSE(scene::ThinFilm{}.enabled());

    scene::Anisotropy a;
    REQUIRE(scene::readAnisotropy(nlohmann::json::parse(R"({"strength": -0.5, "rotation": 1.25})"), a).has_value());
    CHECK(a.strength == -0.5f);
    CHECK(a.rotation == 1.25f);
    CHECK(a.enabled());
    scene::Anisotropy aBack;
    REQUIRE(scene::readAnisotropy(scene::anisotropyToJson(a), aBack).has_value());
    CHECK(scene::anisotropyToJson(aBack) == scene::anisotropyToJson(a));
    CHECK(scene::anisotropyIsDefault(scene::Anisotropy{}));

    // A key it does not know is named; a wrong type and a non-number are refused.
    scene::ThinFilm u;
    const auto bad = scene::readThinFilm(nlohmann::json::parse(R"({"thicknes": 60})"), u);
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().message.find("thicknes") != std::string::npos);
    CHECK_FALSE(scene::readThinFilm(nlohmann::json::parse(R"({"thickness": "60"})"), u).has_value());
    CHECK_FALSE(scene::readThinFilm(nlohmann::json::parse(R"(60)"), u).has_value());
    scene::Anisotropy v;
    CHECK_FALSE(scene::readAnisotropy(nlohmann::json::parse(R"({"angle": 1})"), v).has_value());

    // Only an authored block is written: a default material adds no key to the file it writes.
    nlohmann::json m = nlohmann::json::object();
    scene::writeMaterialOptics(scene::Material{}, m);
    CHECK(m.empty());
    scene::Material film;
    film.thinFilm.thickness = 40.0f;
    scene::writeMaterialOptics(film, m);
    CHECK(m.contains("thinFilm"));
    CHECK_FALSE(m.contains("anisotropy"));
}

TEST_CASE("Optics packing: off is exactly zero whatever the ior and rotation say", "[material_optics][adr1143]") {
    CHECK(rendering::packOptics(scene::Material{}) == glm::vec4(0.0f));
    // Explicitly zero thickness and strength with non-default ior and rotation: the same bytes.
    scene::Material zero;
    zero.thinFilm.thickness = 0.0f;
    zero.thinFilm.ior = 1.7f;
    zero.anisotropy.strength = 0.0f;
    zero.anisotropy.rotation = 1.2f;
    CHECK(rendering::packOptics(zero) == glm::vec4(0.0f));

    scene::Material on;
    on.thinFilm.thickness = 300.0f;
    on.thinFilm.ior = 2.4f;
    on.anisotropy.strength = 0.8f;
    on.anisotropy.rotation = 0.5f;
    CHECK(rendering::packOptics(on) == glm::vec4(300.0f, 2.4f, 0.8f, 0.5f));
    on.thinFilm.ior = 9.0f;
    on.anisotropy.strength = -3.0f;
    const glm::vec4 clamped = rendering::packOptics(on);
    CHECK(clamped.y == 5.0f);
    CHECK(clamped.z == -1.0f);
}

TEST_CASE("The thin-film tint is neutral at zero and walks the temper colours as the oxide thickens",
          "[material_optics][adr1143]") {
    // Zero thickness is exactly neutral, at every angle, for a metal and a dielectric.
    for (float cosV : {1.0f, 0.7f, 0.2f}) {
        CHECK(scene::thinFilmTint(cosV, 0.0f, 2.4f, kSteelF0, 1.0f) == glm::vec3(1.0f));
        CHECK(scene::thinFilmTint(cosV, 0.0f, 1.5f, 0.04f, 0.0f) == glm::vec3(1.0f));
    }
    // And continuous into it: a 1 nm film is within a percent of neutral.
    const glm::vec3 thin = scene::thinFilmTint(1.0f, 1.0f, 2.4f, kSteelF0, 1.0f);
    CHECK(glm::all(glm::lessThan(glm::abs(thin - glm::vec3(1.0f)), glm::vec3(0.01f))));

    // The first-order temper sequence on steel (Bhadeshia): straw, bronze, purple, blue, pale blue.
    // With an oxide of ior 2.4 it spans about 20..90 nm of physical thickness.
    const glm::vec3 straw = scene::thinFilmTint(1.0f, 30.0f, 2.4f, kSteelF0, 1.0f);
    const glm::vec3 purple = scene::thinFilmTint(1.0f, 55.0f, 2.4f, kSteelF0, 1.0f);
    const glm::vec3 blue = scene::thinFilmTint(1.0f, 70.0f, 2.4f, kSteelF0, 1.0f);
    INFO("straw " << straw.r << " " << straw.g << " " << straw.b);
    INFO("purple " << purple.r << " " << purple.g << " " << purple.b);
    INFO("blue " << blue.r << " " << blue.g << " " << blue.b);
    CHECK(straw.r > straw.g);
    CHECK(straw.g > straw.b); // warm: red over green over blue
    CHECK(hueDegrees(straw) > 30.0f);
    CHECK(hueDegrees(straw) < 60.0f);
    CHECK(purple.b > purple.g);
    CHECK(purple.r > purple.g); // red and blue over green
    CHECK(hueDegrees(purple) > 260.0f);
    CHECK(hueDegrees(purple) < 320.0f);
    CHECK(blue.b > blue.g);
    CHECK(blue.g > blue.r);
    CHECK(hueDegrees(blue) > 200.0f);
    CHECK(hueDegrees(blue) < 250.0f);

    // Monotone: from straw on, the hue only ever turns one way (down through red into the blues), so the
    // sequence cannot skip back -- a straw never appears between the purple and the blue.
    std::vector<float> unwrapped;
    for (float nm = 25.0f; nm <= 80.0f; nm += 2.5f) {
        const float h = hueDegrees(scene::thinFilmTint(1.0f, nm, 2.4f, kSteelF0, 1.0f));
        float u = h;
        if (!unwrapped.empty()) {
            while (u > unwrapped.back() + 180.0f) {
                u -= 360.0f;
            }
            while (u < unwrapped.back() - 180.0f) {
                u += 360.0f;
            }
        }
        unwrapped.push_back(u);
    }
    for (std::size_t i = 1; i < unwrapped.size(); ++i) {
        INFO("step " << i << ": " << unwrapped[i - 1] << " -> " << unwrapped[i]);
        CHECK(unwrapped[i] <= unwrapped[i - 1] + 1.0f);
    }
    CHECK(unwrapped.front() - unwrapped.back() > 150.0f); // straw to blue is a long way round

    // A thicker film repeats the sequence (second order) and still tints: 300 nm is not neutral.
    const glm::vec3 t300 = scene::thinFilmTint(1.0f, 300.0f, 2.4f, kSteelF0, 1.0f);
    CHECK(glm::length(t300 - glm::vec3(1.0f)) > 0.2f);
    // The colour moves with the view angle (the optical path shortens with cos theta_t).
    const glm::vec3 grazing = scene::thinFilmTint(0.3f, 55.0f, 2.4f, kSteelF0, 1.0f);
    CHECK(glm::length(grazing - purple) > 0.05f);
}

TEST_CASE("A procedural node's material carries thin film and anisotropy through its file and its parameters",
          "[material_optics][adr1143]") {
    const auto j = nlohmann::json::parse(R"({
        "source": {"kind": "sphere", "radius": 1.0}, "distribution": {"kind": "single"},
        "material": {"baseColor": [0.6, 0.6, 0.6], "metallic": 1,
                     "thinFilm": {"thickness": 55, "ior": 2.3}, "anisotropy": {"strength": 0.8, "rotation": 0.4}}})");
    auto g = scene::ProceduralGeometry::fromJson(j);
    REQUIRE(g.has_value());
    CHECK(g->material.thinFilm.thickness == 55.0f);
    CHECK(g->material.thinFilm.ior == Approx(2.3f));
    CHECK(g->material.anisotropy.strength == Approx(0.8f));
    CHECK(g->material.anisotropy.rotation == Approx(0.4f));
    const nlohmann::json written = g->toJson();
    CHECK(written.at("material").at("thinFilm").at("thickness") == 55.0f);
    CHECK(written.at("material").at("anisotropy").at("rotation") == 0.4f);
    auto again = scene::ProceduralGeometry::fromJson(written);
    REQUIRE(again.has_value());
    CHECK(again->toJson() == written);

    // Never written unless authored; an unknown key inside the block is an error naming it.
    auto plain = scene::ProceduralGeometry::fromJson(nlohmann::json::parse(
        R"({"source": {"kind": "sphere", "radius": 1.0}, "distribution": {"kind": "single"}})"));
    REQUIRE(plain.has_value());
    CHECK_FALSE(plain->toJson().at("material").contains("thinFilm"));
    CHECK_FALSE(plain->toJson().at("material").contains("anisotropy"));
    const auto bad = scene::ProceduralGeometry::fromJson(nlohmann::json::parse(
        R"({"source": {"kind": "sphere", "radius": 1.0}, "distribution": {"kind": "single"},
            "material": {"thinFilm": {"depth": 3}}})"));
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().message.find("depth") != std::string::npos);

    // Registered whatever the file said, so either look can be switched on from the panel or a route.
    params::ParameterSet params;
    auto p = scene::registerProceduralParameters(params, *plain, "procedural/ball/");
    auto* thickness = params.findAs<float>("procedural/ball/material/thinFilm/thickness");
    auto* ior = params.findAs<float>("procedural/ball/material/thinFilm/ior");
    auto* strength = params.findAs<float>("procedural/ball/material/anisotropy/strength");
    auto* rotation = params.findAs<float>("procedural/ball/material/anisotropy/rotation");
    REQUIRE(thickness != nullptr);
    REQUIRE(ior != nullptr);
    REQUIRE(strength != nullptr);
    REQUIRE(rotation != nullptr);
    CHECK(thickness->value() == 0.0f);
    CHECK(ior->value() == Approx(2.4f));
    CHECK(strength->value() == 0.0f);
    thickness->setBase(70.0f);
    strength->setBase(-0.6f);
    rotation->setBase(1.0f);
    params.resetFinals();
    scene::ProceduralGeometry live = *plain;
    REQUIRE(scene::applyProceduralParameterValues(p, *plain, live));
    CHECK(live.material.thinFilm.thickness == 70.0f);
    CHECK(live.material.anisotropy.strength == Approx(-0.6f));
    CHECK(live.material.anisotropy.rotation == 1.0f);
    scene::unregisterProceduralParameters(params, p);
    CHECK(params.findAs<float>("procedural/ball/material/thinFilm/thickness") == nullptr);
    CHECK(params.findAs<float>("procedural/ball/material/anisotropy/rotation") == nullptr);
}

TEST_CASE("An SDF object's material carries thin film and anisotropy through its file and its parameters",
          "[material_optics][adr1143]") {
    const auto j = nlohmann::json::parse(R"({"name": "orb", "tree": {"root": {"kind": "sphere", "radius": 1.0}},
        "boundsMin": [-1.5, -1.5, -1.5], "boundsMax": [1.5, 1.5, 1.5],
        "material": {"baseColor": [0.6, 0.6, 0.6], "metallic": 1,
                     "thinFilm": {"thickness": 300}, "anisotropy": {"strength": 0.8}}})");
    auto o = scene::SdfObject::fromJson(j);
    REQUIRE(o.has_value());
    CHECK(o->material.thinFilm.thickness == 300.0f);
    CHECK(o->material.thinFilm.ior == Approx(2.4f)); // the default, kept
    CHECK(o->material.anisotropy.strength == Approx(0.8f));
    const nlohmann::json written = o->toJson();
    CHECK(written.at("material").at("thinFilm").at("thickness") == 300.0f);
    auto again = scene::SdfObject::fromJson(written);
    REQUIRE(again.has_value());
    CHECK(again->toJson() == written);

    params::ParameterSet params;
    auto p = scene::registerSdfParameters(params, *o, "sdf/orb/");
    auto* thickness = params.findAs<float>("sdf/orb/material/thinFilm/thickness");
    auto* strength = params.findAs<float>("sdf/orb/material/anisotropy/strength");
    REQUIRE(thickness != nullptr);
    REQUIRE(strength != nullptr);
    CHECK(thickness->value() == 300.0f);
    REQUIRE(params.findAs<float>("sdf/orb/material/thinFilm/ior") != nullptr);
    REQUIRE(params.findAs<float>("sdf/orb/material/anisotropy/rotation") != nullptr);
    thickness->setBase(60.0f);
    strength->setBase(0.0f);
    params.resetFinals();
    scene::SdfObject live = *o;
    scene::applySdfParameters(p, *o, live);
    CHECK(live.material.thinFilm.thickness == 60.0f);
    CHECK(live.material.anisotropy.strength == 0.0f);
    scene::unregisterSdfParameters(params, p);
    CHECK(params.findAs<float>("sdf/orb/material/thinFilm/thickness") == nullptr);
}

TEST_CASE("A terrain or orb node's material carries thin film and anisotropy into the scene",
          "[material_optics][adr1143][composition]") {
    assets::AssetRegistry registry{testsupport::processTempDir()};
    const auto text = R"({
      "format": "avgen-scene", "version": 1, "name": "optics",
      "nodes": [
        { "name": "ball", "kind": "orb", "material": { "baseColor": [0.6, 0.6, 0.6], "metallic": 1,
            "thinFilm": { "thickness": 45, "ior": 2.4 }, "anisotropy": { "strength": 0.7, "rotation": 0.3 } } },
        { "name": "ground", "kind": "terrain", "world": { "name": "small", "size": [40, 40] },
          "terrain": { "chunkSize": 20.0, "resolution": 4, "lodLevels": 1 },
          "material": { "baseColor": [0.3, 0.3, 0.3], "thinFilm": { "thickness": 65 } } }
      ]})";
    auto comp = scene::Composition::fromJson(nlohmann::json::parse(text), registry);
    REQUIRE(comp.has_value());
    const scene::CompositionNode* ball = (*comp)->findNode("ball");
    REQUIRE(ball != nullptr);
    CHECK(ball->terrainMaterial.thinFilm.thickness == 45.0f);
    CHECK(ball->terrainMaterial.anisotropy.strength == Approx(0.7f));

    params::ParameterSet params;
    params::Modulator modulator;
    (*comp)->attach(params, modulator);
    (*comp)->update(FrameTime{});
    bool sawOrb = false;
    for (const scene::Entity& e : (*comp)->scene().entities) {
        if (e.name == "ball") {
            sawOrb = true;
            CHECK(e.material.thinFilm.thickness == 45.0f);
            CHECK(e.material.anisotropy.rotation == Approx(0.3f));
            CHECK(rendering::packOptics(e.material) == glm::vec4(45.0f, 2.4f, 0.7f, 0.3f));
        }
    }
    CHECK(sawOrb);

    // The terrain's block is written back (a terrain node's material is the one composition saves).
    const nlohmann::json j = (*comp)->toJson();
    bool sawGround = false;
    for (const nlohmann::json& n : j.at("nodes")) {
        if (n.at("name") == "ground") {
            sawGround = true;
            CHECK(n.at("material").at("thinFilm").at("thickness") == 65.0f);
            CHECK_FALSE(n.at("material").contains("anisotropy"));
        }
    }
    CHECK(sawGround);

    // An unknown key in the block fails the load and names the node.
    const auto bad = scene::Composition::fromJson(nlohmann::json::parse(R"({
      "format": "avgen-scene", "version": 1, "name": "bad",
      "nodes": [ { "name": "ball", "kind": "orb", "material": { "anisotropy": { "power": 1 } } } ]})"),
                                                  registry);
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().message.find("power") != std::string::npos);
    CHECK(bad.error().message.find("ball") != std::string::npos);
}

TEST_CASE("The CPU path tracer says it traces thin film and anisotropy as the bare material",
          "[material_optics][adr1143][pathtrace]") {
    scene::Scene s;
    const auto mesh = s.addMesh(scene::makeIcosphere(1.0f, 2));
    auto& filmed = s.addEntity("filmed", mesh);
    filmed.material.thinFilm.thickness = 55.0f;
    auto& brushed = s.addEntity("brushed", mesh);
    brushed.material.anisotropy.strength = 0.8f;
    auto& both = s.addEntity("both", mesh);
    both.material.thinFilm.thickness = 300.0f;
    both.material.anisotropy.strength = -0.5f;
    s.addEntity("plain", mesh);

    const auto find = [](const pathtrace::Snapshot& snap, const std::string& feature) -> const pathtrace::Capability* {
        for (const pathtrace::Capability& c : snap.capabilities.entries) {
            if (c.feature == feature) {
                return &c;
            }
        }
        return nullptr;
    };
    const pathtrace::Snapshot snap = pathtrace::buildSnapshot(s);
    const pathtrace::Capability* film = find(snap, "thin-film material");
    const pathtrace::Capability* aniso = find(snap, "anisotropic material");
    REQUIRE(film != nullptr);
    REQUIRE(aniso != nullptr);
    CHECK(film->support == pathtrace::Support::Degraded);
    CHECK(film->count == 2);
    CHECK(aniso->support == pathtrace::Support::Degraded);
    CHECK(aniso->count == 2);
    CHECK(snap.capabilities.anyDegraded());

    // A scene that uses neither says nothing about either.
    scene::Scene plain;
    plain.addEntity("plain", plain.addMesh(scene::makeIcosphere(1.0f, 2)));
    const pathtrace::Snapshot clean = pathtrace::buildSnapshot(plain);
    CHECK(find(clean, "thin-film material") == nullptr);
    CHECK(find(clean, "anisotropic material") == nullptr);
}
