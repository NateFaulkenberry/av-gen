// THE ASTRAL FORGE's iteration-4 port, the CPU half: ADR-1146 (tendons), ADR-1147 (shards), ADR-1148 (the
// collapse heat front), ADR-1149 (per-region temper and polish, local sharpness) and ADR-1154 (the engraving's
// domain chain). Each block round-trips, is refused by name where it is wrong or has nothing to draw it, and its
// levels are parameters; the region weight and the domain chain are the stated functions. The GPU half is
// tests/rendering/test_astral_port_gpu.cpp.

#include "assets/asset_registry.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "scene/material_engraving.hpp"
#include "scene/particle_io.hpp"
#include "scene/particles.hpp"
#include "scene/sdf_object.hpp"
#include "spatial/sdf.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <string>

using namespace avgen;
using json = nlohmann::json;

namespace {

Result<std::unique_ptr<scene::Composition>> load(const json& document) {
    static assets::AssetRegistry registry{testsupport::processTempDir()};
    return scene::Composition::fromJson(document, registry);
}

json sceneWith(const json& nodes) {
    return json{{"format", "avgen-scene"}, {"version", 1}, {"name", "port"}, {"nodes", nodes}};
}

json maskNode() {
    return json{{"name", "mask"},
                {"kind", "sdf"},
                {"sdf",
                 {{"tree", {{"root", {{"kind", "sphere"}, {"radius", 1.0}}}}},
                  {"boundsMin", {-2, -2, -2}},
                  {"boundsMax", {2, 2, 2}},
                  {"visible", false}}}};
}

json matterJson() {
    return json::parse(R"({"capacity": 4096, "shape": "box", "extent": [2, 2, 2], "spawnRate": 0, "shape2d": "flake",
        "latent": {"sdf": "mask", "coherence": 1.0,
                   "tendons": {"curves": [[[0, 0, 1], [0.5, 0.5, 1], [1, 0, 1]], [[-1, 0, 1], [-1, 1, 1]]],
                               "speed": 0.8, "stiffness": 30, "spray": 2.5, "ramp": 0.1},
                   "heat": {"origin": [0, -1.45, 0.62], "speed": 30, "width": 0.9, "inject": 0.8, "decay": 3,
                            "fraction": 0.05, "gain": 1.4}},
        "shards": {"pixels": 1.6, "fraction": 0.25, "size": 1.0, "grooves": 40, "bevel": 0.15}})");
}

scene::ParticleSystem readSystem(const json& j) {
    auto s = scene::particlesFromJson(j);
    INFO((s ? std::string() : s.error().message));
    REQUIRE(s);
    return *s;
}

} // namespace

// ---- ADR-1146 / ADR-1147 / ADR-1148: the particle blocks ------------------------------------------------

TEST_CASE("tendons, heat and shards round-trip", "[particles][latent][tendons][heat][shards]") {
    const scene::ParticleSystem s = readSystem(matterJson());
    REQUIRE(s.latent.tendons.active());
    CHECK(s.latent.tendons.curves.size() == 2);
    CHECK(s.latent.tendons.curves[0][1] == glm::vec3(0.5f, 0.5f, 1.0f));
    CHECK(s.latent.tendons.speed == Catch::Approx(0.8f));
    CHECK(s.latent.heat.enabled);
    CHECK(s.latent.heat.origin == glm::vec3(0.0f, -1.45f, 0.62f));
    CHECK(s.latent.heat.fraction == Catch::Approx(0.05f));
    CHECK(s.shards.enabled);
    CHECK(s.shards.bevel == Catch::Approx(0.15f));
    REQUIRE(scene::validateParticleSystem(s));
    const json back = scene::particlesToJson(s);
    const scene::ParticleSystem again = readSystem(back);
    CHECK(scene::particlesToJson(again) == back);
    // Absent: nothing written, nothing on.
    json plain = matterJson();
    plain.erase("shards");
    plain["latent"].erase("tendons");
    plain["latent"].erase("heat");
    const scene::ParticleSystem bare = readSystem(plain);
    CHECK_FALSE(bare.latent.tendons.active());
    CHECK_FALSE(bare.latent.heat.enabled);
    CHECK_FALSE(bare.shards.enabled);
    const json bareBack = scene::particlesToJson(bare);
    CHECK_FALSE(bareBack.contains("shards"));
    CHECK_FALSE(bareBack.at("latent").contains("tendons"));
    CHECK_FALSE(bareBack.at("latent").contains("heat"));
}

TEST_CASE("tendons, heat and shards are refused by name when wrong", "[particles][latent][tendons][heat][shards]") {
    const auto refusedRead = [](json j, const char* needle) {
        auto s = scene::particlesFromJson(j);
        INFO(needle << ": " << (s ? std::string("read") : s.error().message));
        REQUIRE_FALSE(s);
        CHECK(s.error().message.find(needle) != std::string::npos);
    };
    const auto refusedValid = [](json j, const char* needle) {
        auto s = scene::particlesFromJson(j);
        std::string message;
        if (s) {
            auto ok = scene::validateParticleSystem(*s);
            REQUIRE_FALSE(ok);
            message = ok.error().message;
        } else {
            message = s.error().message;
        }
        INFO(needle << ": " << message);
        CHECK(message.find(needle) != std::string::npos);
    };
    json j = matterJson();
    j["latent"]["tendons"]["bogus"] = 1;
    refusedRead(j, "'latent.tendons': unknown key 'bogus'");
    j = matterJson();
    j["latent"]["tendons"]["curves"] = json::array();
    refusedRead(j, "needs 'curves'");
    j = matterJson();
    j["latent"]["tendons"]["curves"][0] = json::array({json::array({0, 0})});
    refusedRead(j, "[x, y, z]");
    j = matterJson();
    j["latent"]["tendons"]["curves"][1] = json::array({json::array({0, 0, 1})});
    refusedValid(j, "curves[1] needs 2..");
    j = matterJson();
    j["latent"]["tendons"]["curves"][1] = json::array({json::array({0, 0, 1}), json::array({0, 0, 1})});
    refusedValid(j, "curves[1] has no length");
    j = matterJson();
    j["latent"]["heat"]["bogus"] = 1;
    refusedRead(j, "'latent.heat': unknown key 'bogus'");
    j = matterJson();
    j["latent"]["heat"]["width"] = 0.0;
    refusedValid(j, "latent.heat");
    j = matterJson();
    j["trailEnabled"] = true;
    refusedValid(j, "cannot have trails");
    j = matterJson();
    j["shards"]["bogus"] = 1;
    refusedRead(j, "'shards': unknown key 'bogus'");
    j = matterJson();
    j["shards"]["fraction"] = 1.5;
    refusedValid(j, "shards:");
    j = matterJson();
    j["shape2d"] = "round";
    refusedValid(j, "shape2d must be \"flake\"");
}

TEST_CASE("tendons, heat and shards register their levels as parameters", "[particles][tendons][heat][shards]") {
    auto comp = load(sceneWith(json::array({maskNode(), json{{"name", "matter"}, {"kind", "particles"}, {"particles", matterJson()}}})));
    INFO((comp ? std::string() : comp.error().message));
    REQUIRE(comp);
    params::ParameterSet parameters;
    params::Modulator modulator;
    (*comp)->attach(parameters, modulator);
    for (const char* path : {"particles/matter/latent/tendons/speed", "particles/matter/latent/heat/inject",
                             "particles/matter/latent/heat/speed", "particles/matter/shards/fraction",
                             "particles/matter/shards/pixels"}) {
        INFO(path);
        REQUIRE(parameters.find(path) != nullptr);
    }
    auto* fraction = parameters.findAs<float>("particles/matter/shards/fraction");
    auto* inject = parameters.findAs<float>("particles/matter/latent/heat/inject");
    REQUIRE(fraction != nullptr);
    REQUIRE(inject != nullptr);
    fraction->setBase(0.0f);
    fraction->resetFinal();
    inject->setBase(100.0f);
    inject->resetFinal();
    (*comp)->update({});
    const scene::Scene& sc = (*comp)->scene();
    REQUIRE(sc.particles.size() == 1);
    CHECK(sc.particles[0].shards.fraction == Catch::Approx(0.0f));
    CHECK(sc.particles[0].latent.heat.inject == Catch::Approx(100.0f));
    CHECK(sc.particles[0].latent.tendons.curves.size() == 2); // the curves are structural: from the file
}

TEST_CASE("stagger round-trips and is refused by name out of range or with tendons", "[particles][latent][stagger]") {
    json j = matterJson();
    j["latent"].erase("tendons");
    j["latent"]["stagger"] = 3;
    const scene::ParticleSystem s = readSystem(j);
    CHECK(s.latent.stagger == 3);
    CHECK(scene::particlesToJson(s).at("latent").at("stagger") == 3);
    j["latent"]["stagger"] = 1;
    CHECK_FALSE(scene::particlesToJson(readSystem(j)).at("latent").contains("stagger"));
    j["latent"]["stagger"] = 5;
    auto big = scene::particlesFromJson(j);
    REQUIRE_FALSE(big);
    CHECK(big.error().message.find("latent.stagger must be in 1..4") != std::string::npos);
    j["latent"]["stagger"] = 1.5;
    auto frac = scene::particlesFromJson(j);
    REQUIRE_FALSE(frac);
    CHECK(frac.error().message.find("'latent.stagger' must be an integer") != std::string::npos);
    json t = matterJson();
    t["latent"]["stagger"] = 2;
    auto tendons = scene::particlesFromJson(t);
    REQUIRE_FALSE(tendons);
    CHECK(tendons.error().message.find("latent.stagger") != std::string::npos);
}

// ---- ADR-1149: regions and the sharpness's spread -------------------------------------------------------

TEST_CASE("regions round-trip, weigh as stated, and are refused by name", "[sdf][regions]") {
    const json j = json::parse(R"({"film": 120, "filmNoise": 60, "noiseScale": 0.9, "polish": 0.7, "points": [
        {"center": [-0.76, 0.72, 0.6], "sharpness": 4, "weight": 1},
        {"center": [0, -1.45, 0.62], "scale": [0.8, 2, 1], "sharpness": 3, "weight": 0.6}]})");
    auto r = scene::readSurfaceRegions(j);
    REQUIRE(r);
    CHECK(r->points.size() == 2);
    auto again = scene::readSurfaceRegions(scene::surfaceRegionsToJson(*r));
    REQUIRE(again);
    CHECK(scene::surfaceRegionsToJson(*again) == scene::surfaceRegionsToJson(*r));
    // fw = max_k weight_k exp(-sharpness_k |(q - c_k) s_k|^2): 1 at an eye, 0.6 at the mouth, ~0 far away
    CHECK(scene::surfaceRegionWeight(*r, glm::vec3(-0.76f, 0.72f, 0.6f)) == Catch::Approx(1.0f));
    CHECK(scene::surfaceRegionWeight(*r, glm::vec3(0.0f, -1.45f, 0.62f)) == Catch::Approx(0.6f));
    const glm::vec3 q(0.0f, -1.2f, 0.62f); // 0.25 above the mouth: (0.25 * 2)^2 = 0.25
    CHECK(scene::surfaceRegionWeight(*r, q) == Catch::Approx(0.6f * std::exp(-3.0f * 0.25f)));
    CHECK(scene::surfaceRegionWeight(*r, glm::vec3(5.0f)) < 1e-6f);
    const auto refused = [](const char* text, const char* needle) {
        auto bad = scene::readSurfaceRegions(json::parse(text));
        INFO(needle);
        REQUIRE_FALSE(bad);
        CHECK(bad.error().message.find(needle) != std::string::npos);
    };
    refused(R"({"points": [{"center": [0, 0, 0]}], "bogus": 1})", "no key 'bogus'");
    refused(R"({"points": [{"center": [0, 0, 0], "sharpness": 0}]})", "sharpness must be > 0");
    refused(R"({"points": [{"center": [0, 0, 0], "weight": 2}]})", "weight in 0..1");
    refused(R"({"polish": 1.5, "points": [{"center": [0, 0, 0]}]})", "polish in 0..1");
    refused(R"({"film": 50})", "needs 'points'");
    refused(R"({"points": [{}, {}, {}, {}, {}]})", "at most 4");
    // Only an SDF object draws them: a procedural material refuses the key by name.
    json proc = json::parse(R"({"source": {"kind": "box", "size": [1, 1, 1]}, "distribution": {"kind": "single"}})");
    proc["material"] = json{{"regions", j}};
    auto comp = load(sceneWith(json::array({json{{"name", "box"}, {"kind", "procedural"}, {"procedural", proc}}})));
    REQUIRE_FALSE(comp);
    CHECK(comp.error().message.find("'regions'") != std::string::npos);
}

TEST_CASE("an sdf object's regions and spread round-trip and are parameters", "[sdf][regions][density]") {
    json sdf = json::parse(R"({"tree": {"root": {"kind": "sphere", "radius": 1}}, "boundsMin": [-2, -2, -2],
        "boundsMax": [2, 2, 2], "material": {"metallic": 1, "regions": {"film": 90, "polish": 0.5,
        "points": [{"center": [0.3, 0.2, 0.9]}]}},
        "density": {"particles": "matter", "iso": 1, "sharpness": 1, "spread": 0.6, "spreadRadii": [2, 2.8, 5]}})");
    auto o = scene::SdfObject::fromJson(sdf);
    INFO((o ? std::string() : o.error().message));
    REQUIRE(o);
    CHECK(o->material.regions.enabled());
    CHECK(o->density.spread == Catch::Approx(0.6f));
    CHECK(o->density.spreadRadii == glm::vec3(2.0f, 2.8f, 5.0f));
    auto again = scene::SdfObject::fromJson(o->toJson());
    REQUIRE(again);
    CHECK(again->toJson() == o->toJson());
    // without spread, nothing new is written
    sdf["density"].erase("spread");
    sdf["density"].erase("spreadRadii");
    auto plain = scene::SdfObject::fromJson(sdf);
    REQUIRE(plain);
    CHECK_FALSE(plain->toJson().at("density").contains("spread"));
    sdf["density"]["spread"] = 1.5;
    auto bad = scene::SdfObject::fromJson(sdf);
    REQUIRE_FALSE(bad);
    CHECK(bad.error().message.find("density.spread") != std::string::npos);
    sdf["density"].erase("spread");
    sdf["density"]["bogus"] = 1;
    auto unknown = scene::SdfObject::fromJson(sdf);
    REQUIRE_FALSE(unknown);
    CHECK(unknown.error().message.find("'bogus'") != std::string::npos);
}

// ---- ADR-1154: the engraving's domain chain ------------------------------------------------------------

TEST_CASE("the domain chain is the leading unary nodes, disabled ones skipped, recurse excluded", "[sdf][engraving]") {
    const auto chain = [](const char* text) {
        auto t = spatial::SdfTree::fromJson(json::parse(text));
        REQUIRE(t);
        return spatial::sdfDomainChainLength(*t);
    };
    CHECK(chain(R"({"root": {"kind": "sphere"}})") == 0);
    CHECK(chain(R"({"root": {"kind": "union", "children": [{"kind": "translate", "children": [{"kind": "sphere"}]}]}})") == 0);
    CHECK(chain(R"({"root": {"kind": "twist", "amount": 0.3, "children": [{"kind": "bend", "children": [
        {"kind": "translate", "children": [{"kind": "union", "children": [{"kind": "translate", "children": [
        {"kind": "sphere"}]}]}]}]}]}})") == 3);
    CHECK(chain(R"({"root": {"kind": "twist", "enabled": false, "children": [{"kind": "rotate", "children": [
        {"kind": "sphere"}]}]}})") == 1);
    CHECK(chain(R"({"root": {"kind": "translate", "children": [{"kind": "recurse", "count": 1, "children": [
        {"kind": "sphere"}]}]}})") == 1);
    // ... and they are the program's and the table's first records, in that order.
    auto t = spatial::SdfTree::fromJson(json::parse(R"({"root": {"kind": "rotate", "rotation": [0, 0, 90], "children": [
        {"kind": "translate", "translation": [0.2, 0, 0], "children": [{"kind": "sphere"}]}]}})"));
    REQUIRE(t);
    std::vector<spatial::SdfNodeGpu> packed;
    REQUIRE(spatial::packSdfTree(*t, packed) > 0);
    CHECK(packed[0].kind == static_cast<std::uint32_t>(spatial::SdfNodeKind::Rotate));
    CHECK(packed[1].kind == static_cast<std::uint32_t>(spatial::SdfNodeKind::Translate));
    std::vector<spatial::SdfNodeGpu> table;
    spatial::sdfCompileTable(*t, table);
    CHECK(table[0].kind == static_cast<std::uint32_t>(spatial::SdfNodeKind::Rotate));
    CHECK(table[1].kind == static_cast<std::uint32_t>(spatial::SdfNodeKind::Translate));
}
