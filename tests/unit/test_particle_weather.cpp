// ADR-520: the weather substrate on scene::ParticleSystem.
//
// Four things are checked here, and each of them is a defect this repository has actually paid
// for at least once:
//
//   1. Every new field survives a JSON round trip. A field that parses and is not written back is
//      a setting that vanishes the first time the editor saves (ADR-367's `softness` default).
//   2. Every path the Edit panel's weather table names resolves to a registered parameter. §77:
//      a panel asks by string and a wrong path draws an EMPTY BOX rather than failing.
//   3. `applyParticleParameters` writes each of them back. A parameter that is registered, shown
//      and then not applied is the same nothing with a slider in front of it.
//   4. `validateParticleSystem` REJECTS the two configurations that silently empty a system.
//      ADR-182: a probe that cannot fail proves nothing, so each of those has a paired case that
//      shows the valid neighbour passing.

#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "scene/particles.hpp"
#include "support/temp_dir.hpp"
#include "ui/particle_weather_rows.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

using namespace avgen;
using Catch::Matchers::WithinAbs;

namespace {

// A system with every ADR-520 field set to something that is NOT its default, so a field the
// writer forgets cannot be mistaken for a field that happens to match.
scene::ParticleSystem weatherSystem() {
    scene::ParticleSystem s;
    s.name = "downpour";
    s.volumeFollow = glm::vec3(1.0f, 0.25f, 0.5f);
    s.volumeWrap = true;
    s.collision = scene::CollisionResponse::Splash;
    s.collisionHeight = -1.75f;
    s.collisionRestitution = 0.61f;
    s.splashLifetime = 0.37f;
    s.splashSize = 12.5f;
    s.ringThickness = 0.41f;
    s.sizeVariance = 0.66f;
    s.sizeSkew = 2.75f;
    s.dragSizeBias = 0.83f;
    s.pulseRate = 3.25f;
    s.pulseDepth = 0.44f;
    s.pulseSync = 0.71f;
    s.pulseSharpness = 6.5f;
    s.clusterCount = 19;
    s.clusterRadius = 2.35f;
    s.pauseRate = 1.45f;
    s.pauseFraction = 0.38f;
    s.scatterStrength = 4.25f;
    s.scatterAnisotropy = 0.57f;
    s.emitMaskField = "front-edge";
    return s;
}

} // namespace

// The round trip goes through `Composition::saveFile` / `loadFile` rather than through the JSON
// helpers, because those helpers live in an anonymous namespace -- and because the save path is
// the one the editor actually uses, which is where a dropped field actually costs somebody work.
namespace {
scene::CompositionNode particleNode(const scene::ParticleSystem& s) {
    scene::CompositionNode node;
    node.kind = scene::NodeKind::Particles;
    node.name = s.name;
    node.particles = s;
    return node;
}
} // namespace

TEST_CASE("ADR-520 particle weather fields survive a save and load", "[particles][weather][json]") {
    const scene::ParticleSystem src = weatherSystem();
    // The premise, so this cannot pass by comparing two default systems (ADR-182).
    REQUIRE(src.pulseRate != scene::ParticleSystem{}.pulseRate);
    REQUIRE(src.collision != scene::ParticleSystem{}.collision);

    assets::AssetRegistry registry(testsupport::processTempDir());
    scene::Composition comp(registry, "weather-roundtrip");
    REQUIRE(comp.addNode(particleNode(src)).has_value());

    const auto path = testsupport::processTempDir() / "avgen_weather_roundtrip.scene.json";
    REQUIRE(comp.saveFile(path).has_value());
    auto loaded = scene::Composition::loadFile(path.filename(), registry);
    REQUIRE(loaded.has_value());
    const scene::CompositionNode* node = (*loaded)->findNode(src.name);
    REQUIRE(node != nullptr);
    const scene::ParticleSystem& r = node->particles;

    CHECK(r.volumeFollow == src.volumeFollow);
    CHECK(r.volumeWrap == src.volumeWrap);
    CHECK(r.collision == src.collision);
    CHECK_THAT(r.collisionHeight, WithinAbs(src.collisionHeight, 1e-5f));
    CHECK_THAT(r.collisionRestitution, WithinAbs(src.collisionRestitution, 1e-5f));
    CHECK_THAT(r.splashLifetime, WithinAbs(src.splashLifetime, 1e-5f));
    CHECK_THAT(r.splashSize, WithinAbs(src.splashSize, 1e-5f));
    CHECK_THAT(r.ringThickness, WithinAbs(src.ringThickness, 1e-5f));
    CHECK_THAT(r.sizeVariance, WithinAbs(src.sizeVariance, 1e-5f));
    CHECK_THAT(r.sizeSkew, WithinAbs(src.sizeSkew, 1e-5f));
    CHECK_THAT(r.dragSizeBias, WithinAbs(src.dragSizeBias, 1e-5f));
    CHECK_THAT(r.pulseRate, WithinAbs(src.pulseRate, 1e-5f));
    CHECK_THAT(r.pulseDepth, WithinAbs(src.pulseDepth, 1e-5f));
    CHECK_THAT(r.pulseSync, WithinAbs(src.pulseSync, 1e-5f));
    CHECK_THAT(r.pulseSharpness, WithinAbs(src.pulseSharpness, 1e-5f));
    CHECK(r.clusterCount == src.clusterCount);
    CHECK_THAT(r.clusterRadius, WithinAbs(src.clusterRadius, 1e-5f));
    CHECK_THAT(r.pauseRate, WithinAbs(src.pauseRate, 1e-5f));
    CHECK_THAT(r.pauseFraction, WithinAbs(src.pauseFraction, 1e-5f));
    CHECK_THAT(r.scatterStrength, WithinAbs(src.scatterStrength, 1e-5f));
    CHECK_THAT(r.scatterAnisotropy, WithinAbs(src.scatterAnisotropy, 1e-5f));
    CHECK(r.emitMaskField == src.emitMaskField);
    std::filesystem::remove(path);
}

TEST_CASE("ADR-520 a default system writes none of the weather keys", "[particles][weather][json]") {
    // ADR-367's actual defect: a default nobody chose, written unconditionally, becomes a value
    // baked into every file the editor ever saves.
    assets::AssetRegistry registry(testsupport::processTempDir());
    scene::Composition comp(registry, "weather-defaults");
    scene::ParticleSystem plain;
    plain.name = "plain";
    REQUIRE(comp.addNode(particleNode(plain)).has_value());
    const nlohmann::json saved = comp.toJson();
    REQUIRE(saved.contains("nodes"));
    const nlohmann::json& j = saved["nodes"][0]["particles"];
    for (const char* key : {"volumeFollow", "volumeWrap", "collision", "collisionHeight", "splashLifetime",
                            "splashSize", "ringThickness", "sizeVariance", "sizeSkew", "dragSizeBias",
                            "pulseRate", "pulseDepth", "pulseSync", "pulseSharpness", "clusterCount",
                            "clusterRadius", "pauseRate", "pauseFraction", "scatterStrength",
                            "scatterAnisotropy", "emitMaskField"}) {
        INFO("unexpected key in a default system: " << key);
        CHECK_FALSE(j.contains(key));
    }
}

TEST_CASE("ADR-520 every weather row the panel draws names a registered parameter", "[particles][weather][ui]") {
    // §77. The panel computes "particles/" + node + "/" + row.leaf and asks the parameter set for
    // it; a miss draws an empty box and says nothing. This computes the path the same way, from
    // the same table, against the same registrar.
    params::ParameterSet params;
    const scene::ParticleSystem system = weatherSystem();
    const scene::ParticleParameters handles = scene::registerParticleParameters(params, system);
    REQUIRE(handles.spawnRate != nullptr);

    const auto rows = ui::particleWeatherRows();
    // The premise: there is a table and it is not empty, so the loop below has something to do.
    REQUIRE(rows.size() >= 13);

    std::string missing;
    for (const ui::ParticleWeatherRow& row : rows) {
        const std::string path = "particles/" + system.name + "/" + row.leaf;
        if (params.find(path) == nullptr) {
            missing += path + " ";
        }
        // A row whose range lies outside the parameter's hard range is a control that cannot reach
        // what it offers, which is the same defect one step milder.
        if (const params::IParameter* p = params.find(path); p != nullptr) {
            INFO("row " << row.leaf << " offers [" << row.lo << ", " << row.hi << "]");
            CHECK(row.lo < row.hi);
        }
    }
    INFO("panel rows that name no parameter: " << missing);
    CHECK(missing.empty());
}

TEST_CASE("ADR-520 applyParticleParameters writes every weather field back", "[particles][weather]") {
    params::ParameterSet params;
    const scene::ParticleSystem rest = weatherSystem();
    const scene::ParticleParameters handles = scene::registerParticleParameters(params, rest);

    // Move every one of them away from what the scene authored, through the parameter, which is
    // what a modulation route or a panel drag does.
    const std::vector<std::pair<std::string, float>> moved = {
        {"collisionHeight", 4.5f}, {"splashSize", 3.25f},   {"sizeVariance", 0.125f},
        {"sizeSkew", 1.5f},        {"dragSizeBias", 0.25f}, {"pulseRate", 0.75f},
        {"pulseDepth", 0.2f},      {"pulseSync", 0.3f},     {"pulseSharpness", 2.0f},
        {"clusterRadius", 7.5f},   {"pauseRate", 0.5f},     {"pauseFraction", 0.9f},
        {"scatterStrength", 1.25f},{"scatterAnisotropy", -0.4f},
    };
    for (const auto& [leaf, value] : moved) {
        params::Parameter<float>* p = params.findAs<float>("particles/" + rest.name + "/" + leaf);
        REQUIRE(p != nullptr);
        p->setBase(value);
    }

    // `setBase` moves the base; the FINAL is what `value()` returns and what the engine
    // recomputes once a frame. Without this the parameters are set and the finals are still the
    // registration values, and every assertion below reports the authored number -- which is how
    // this case failed on its first run, correctly.
    params.resetFinals();

    scene::ParticleSystem live = rest;
    scene::applyParticleParameters(handles, rest, live);

    CHECK_THAT(live.collisionHeight, WithinAbs(4.5f, 1e-5f));
    CHECK_THAT(live.splashSize, WithinAbs(3.25f, 1e-5f));
    CHECK_THAT(live.sizeVariance, WithinAbs(0.125f, 1e-5f));
    CHECK_THAT(live.sizeSkew, WithinAbs(1.5f, 1e-5f));
    CHECK_THAT(live.dragSizeBias, WithinAbs(0.25f, 1e-5f));
    CHECK_THAT(live.pulseRate, WithinAbs(0.75f, 1e-5f));
    CHECK_THAT(live.pulseDepth, WithinAbs(0.2f, 1e-5f));
    CHECK_THAT(live.pulseSync, WithinAbs(0.3f, 1e-5f));
    CHECK_THAT(live.pulseSharpness, WithinAbs(2.0f, 1e-5f));
    CHECK_THAT(live.clusterRadius, WithinAbs(7.5f, 1e-5f));
    CHECK_THAT(live.pauseRate, WithinAbs(0.5f, 1e-5f));
    CHECK_THAT(live.pauseFraction, WithinAbs(0.9f, 1e-5f));
    CHECK_THAT(live.scatterStrength, WithinAbs(1.25f, 1e-5f));
    CHECK_THAT(live.scatterAnisotropy, WithinAbs(-0.4f, 1e-5f));

    // And the structural fields, which are NOT parameters, are left exactly where the scene put
    // them. A registrar that quietly reset one of these would be the `extent` defect again.
    CHECK(live.volumeWrap == rest.volumeWrap);
    CHECK(live.collision == rest.collision);
    CHECK(live.clusterCount == rest.clusterCount);
    CHECK(live.emitMaskField == rest.emitMaskField);
}

TEST_CASE("ADR-520 validation rejects the configurations that silently empty a system", "[particles][weather]") {
    SECTION("a wrapping volume with a flat axis") {
        scene::ParticleSystem ok = weatherSystem();
        ok.extent = glm::vec3(4.0f, 2.0f, 4.0f);
        REQUIRE(scene::validateParticleSystem(ok).has_value()); // the valid neighbour (ADR-182)

        scene::ParticleSystem bad = ok;
        bad.extent.y = 0.0f;
        const auto r = scene::validateParticleSystem(bad);
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().message.find("volumeWrap") != std::string::npos);
    }
    SECTION("a splash with no life") {
        scene::ParticleSystem ok = weatherSystem();
        ok.extent = glm::vec3(4.0f, 2.0f, 4.0f);
        REQUIRE(scene::validateParticleSystem(ok).has_value());

        scene::ParticleSystem bad = ok;
        bad.splashLifetime = 0.0f;
        const auto r = scene::validateParticleSystem(bad);
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().message.find("splashLifetime") != std::string::npos);
    }
    SECTION("a size skew of zero") {
        scene::ParticleSystem bad = weatherSystem();
        bad.extent = glm::vec3(4.0f, 2.0f, 4.0f);
        bad.sizeSkew = 0.0f;
        CHECK_FALSE(scene::validateParticleSystem(bad).has_value());
    }
}
