// A particle system's file representation, round-tripped.
//
// Written during the QA pass of 2026-09-22, which found `particlesToJson`/`particlesFromJson` --
// ~445 lines translating 85-odd fields -- with no direct coverage of any kind. It had none because
// it lived in `composition.cpp`'s anonymous namespace, which a test binary cannot reach, so the
// only thing exercising it was whatever a whole-`Composition` round trip touched. Nothing in
// `tests/unit/test_composition.cpp` wrote a particle system with anything set, so in practice the
// answer was "nothing".
//
// The load-bearing test here is the **JSON fixpoint**: write, read, write again, and require the
// two documents to be equal. That catches the whole family at once, including for fields added
// after this file was written --
//
//   * a key written but never read (the reader keeps its default, so the second write differs),
//   * a key read under one spelling and written under another,
//   * and above all an **elision/default mismatch**: `particlesToJson` omits many fields when they
//     equal a default written as a literal at the call site (`if (s.sizeVariance != 0.3f)`), while
//     the reader's fallback is `ParticleSystem`'s own member initialiser. Those are two copies of
//     the same number in different files, and when they drift, a default-valued system round-trips
//     into a different system with no error anywhere.
//
// A fixpoint test earns its keep only if it can fail, so `the fixpoint notices a field the reader
// drops` below checks the instrument against a document with a hand-planted extra key.

#include "scene/particle_io.hpp"
#include "scene/particles.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace avgen;
using json = nlohmann::json;

namespace {

// Keys whose value differs between two `particles` documents, so a failure names the field instead
// of printing two 80-line objects and leaving the reader to diff them.
std::vector<std::string> differingKeys(const json& a, const json& b) {
    std::vector<std::string> out;
    for (const auto& [key, value] : a.items()) {
        if (!b.contains(key)) {
            out.push_back(key + " (lost on the second write)");
        } else if (b.at(key) != value) {
            out.push_back(key + " (" + value.dump() + " -> " + b.at(key).dump() + ")");
        }
    }
    for (const auto& [key, value] : b.items()) {
        if (!a.contains(key)) {
            out.push_back(key + " (appeared on the second write)");
        }
    }
    return out;
}

// write -> read -> write. Returns the keys that moved; empty means the representation is stable.
std::vector<std::string> fixpointDrift(const scene::ParticleSystem& s) {
    const json first = scene::particlesToJson(s);
    const auto reread = scene::particlesFromJson(first);
    if (!reread) {
        return {"particlesFromJson rejected its own output: " + reread.error().message};
    }
    return differingKeys(first, scene::particlesToJson(*reread));
}

// Every field the struct has, set to something that is not its default and not another field's
// default either, so a reader that crosses two keys shows up as a swap rather than as a match.
scene::ParticleSystem populated() {
    scene::ParticleSystem s;
    // Deliberately not `s.name`: see `the system's name is the node's, not the file's` below.
    s.enabled = false;
    s.capacity = 4096;
    s.seed = 77;

    s.shape = scene::EmitterShape::Box;
    s.spline = "ridge-path";
    s.position = {1.5f, 2.5f, -3.5f};
    s.extent = {4.25f, 5.25f, 6.25f};
    s.spawnRate = 1234.0f;
    s.burst = 9.0f;
    s.lifetimeMin = 0.75f;
    s.lifetimeMax = 4.5f;
    s.direction = {0.0f, -1.0f, 0.25f};
    s.spread = 0.375f;
    s.speedMin = 1.25f;
    s.speedMax = 7.5f;

    s.gravity = {0.5f, -9.81f, 0.25f};
    s.drag = 0.625f;
    s.turbulence = 1.75f;
    s.turbulenceScale = 2.25f;
    s.turbulenceSpeed = 0.875f;
    s.attractorPosition = {-2.0f, 3.0f, 4.0f};
    s.attractorStrength = 2.5f;
    s.attractorRadius = 8.5f;
    s.orbit = 1.125f;
    s.fieldForces = {
        {"updraught", scene::FieldForceMode::Velocity, true, 2.5f, 0.375f, {1.0f, 0.0f, 0.0f}},
        {"cull-band", scene::FieldForceMode::Kill, false, 0.75f, 0.5f, {0.0f, 0.0f, 1.0f}},
    };

    s.sizeStart = 0.125f;
    s.sizeEnd = 0.0625f;
    s.colorStart = {0.125f, 0.25f, 0.375f, 0.5f};
    s.colorEnd = {0.625f, 0.75f, 0.875f, 0.25f};
    s.emissive = 6.5f;
    s.blend = scene::ParticleBlend::Alpha;
    s.shape2d = scene::ParticleShape::Leaf;
    s.tumbleRate = 3.75f;
    s.leafAspect = 0.625f;
    s.twoSided = 0.875f;
    s.windInfluence = 0.75f;
    s.softness = 0.375f;

    s.volumeFollow = {0.25f, 0.5f, 0.75f};
    s.volumeWrap = true;

    s.collision = scene::CollisionResponse::Splash;
    s.collisionHeight = 1.75f;
    s.collisionRestitution = 0.875f;
    s.splashLifetime = 0.625f;
    s.splashSize = 9.5f;
    s.ringThickness = 0.4375f;

    s.sizeVariance = 0.5625f;
    s.sizeSkew = 2.25f;
    s.dragSizeBias = 0.6875f;

    s.pulseRate = 3.25f;
    s.pulseDepth = 0.5625f;
    s.pulseSync = 0.8125f;
    s.pulseSharpness = 2.75f;

    s.clusterCount = 12;
    s.clusterRadius = 2.75f;
    s.pauseRate = 1.375f;
    s.pauseFraction = 0.3125f;

    s.scatterAnchor.terrain = "valley-floor";
    s.scatterAnchor.layers = {"oaks", "ferns"};
    s.scatterAnchor.randomBelow = 0.4375f;
    s.scatterAnchor.litOnly = true;
    s.scatterAnchor.viewDistance = 85.0f;

    s.scatterStrength = 1.625f;
    s.scatterAnisotropy = 0.3125f;
    s.emitMaskField = "weather-front";

    s.sizeCurve.keys = {{0.0f, 0.25f}, {0.5f, 1.5f}, {1.0f, 0.125f}};
    s.colorCurve.keys = {{0.0f, {1.0f, 0.5f, 0.25f}}, {1.0f, {0.125f, 0.25f, 0.5f}}};
    s.opacityCurve.keys = {{0.0f, 0.0f}, {0.25f, 1.0f}, {1.0f, 0.0f}};

    s.velocityStretch = 0.6875f;
    s.stretchMax = 0.8125f;
    s.stretchMin = 0.09375f;

    s.trailEnabled = true;
    s.trailLength = 24;
    s.trailStride = 3;
    s.trailWidth = 1.75f;
    s.trailTaper = 0.5625f;
    s.trailFade = 0.4375f;
    s.trailTint = {0.75f, 0.5f, 0.25f};

    s.fogCoupling = 0.5f;
    s.volumeGlow = 2.25f;
    return s;
}

} // namespace

TEST_CASE("a default particle system's file representation is a fixpoint", "[scene][particles][io]") {
    // The elision case, and the one most likely to break: every field is at the default the writer
    // tests against, so every optional key is omitted and the reader has to reconstruct all of
    // them from its own defaults.
    const scene::ParticleSystem def;
    const auto drift = fixpointDrift(def);
    CHECK(drift.empty());
    for (const std::string& key : drift) {
        FAIL_CHECK("default system drifted at: " << key);
    }
}

TEST_CASE("a fully populated particle system's file representation is a fixpoint",
          "[scene][particles][io]") {
    const auto drift = fixpointDrift(populated());
    CHECK(drift.empty());
    for (const std::string& key : drift) {
        FAIL_CHECK("populated system drifted at: " << key);
    }
}

TEST_CASE("the fixpoint notices a field the reader drops", "[scene][particles][io]") {
    // The teeth check. `differingKeys` is the whole instrument above, so it has to be shown
    // failing on a document that a dropped field would actually produce: a key present in the
    // first write and absent from the second.
    json first = scene::particlesToJson(populated());
    json second = first;
    second.erase("spawnRate");
    const auto drift = differingKeys(first, second);
    REQUIRE(drift.size() == 1);
    CHECK(drift[0].starts_with("spawnRate"));

    // ...and the other direction, a key that appears only on the second write.
    second = first;
    second["invented"] = 1.0f;
    CHECK(differingKeys(first, second).size() == 1);
}

TEST_CASE("a populated particle system reads back field by field", "[scene][particles][io]") {
    // The fixpoint proves the representation is stable; it does not prove it carries the values it
    // claims to, because a writer and reader that agree on the same wrong field would be stable
    // too. These are the values themselves, spot-checked across every group.
    const scene::ParticleSystem before = populated();
    const auto after = scene::particlesFromJson(scene::particlesToJson(before));
    REQUIRE(after);

    CHECK(after->enabled == false);
    CHECK(after->capacity == 4096u);
    CHECK(after->seed == 77u);

    CHECK(after->shape == scene::EmitterShape::Box);
    CHECK(after->spline == "ridge-path");
    CHECK(after->position.x == Catch::Approx(1.5f));
    CHECK(after->extent.z == Catch::Approx(6.25f));
    CHECK(after->spawnRate == Catch::Approx(1234.0f));
    CHECK(after->lifetimeMax == Catch::Approx(4.5f));
    CHECK(after->speedMax == Catch::Approx(7.5f));

    CHECK(after->gravity.y == Catch::Approx(-9.81f));
    CHECK(after->turbulenceScale == Catch::Approx(2.25f));
    CHECK(after->attractorRadius == Catch::Approx(8.5f));

    REQUIRE(after->fieldForces.size() == 2);
    CHECK(after->fieldForces[0].field == "updraught");
    CHECK(after->fieldForces[0].mode == scene::FieldForceMode::Velocity);
    CHECK(after->fieldForces[0].strength == Catch::Approx(2.5f));
    CHECK(after->fieldForces[0].mix == Catch::Approx(0.375f));
    CHECK(after->fieldForces[1].mode == scene::FieldForceMode::Kill);
    CHECK(after->fieldForces[1].enabled == false);

    CHECK(after->blend == scene::ParticleBlend::Alpha);
    CHECK(after->shape2d == scene::ParticleShape::Leaf);
    CHECK(after->colorStart.a == Catch::Approx(0.5f));
    CHECK(after->emissive == Catch::Approx(6.5f));
    CHECK(after->softness == Catch::Approx(0.375f));
    CHECK(after->windInfluence == Catch::Approx(0.75f));

    CHECK(after->volumeWrap == true);
    CHECK(after->volumeFollow.y == Catch::Approx(0.5f));

    CHECK(after->collision == scene::CollisionResponse::Splash);
    CHECK(after->splashSize == Catch::Approx(9.5f));
    CHECK(after->ringThickness == Catch::Approx(0.4375f));

    CHECK(after->sizeVariance == Catch::Approx(0.5625f));
    CHECK(after->sizeSkew == Catch::Approx(2.25f));
    CHECK(after->dragSizeBias == Catch::Approx(0.6875f));
    CHECK(after->pulseSharpness == Catch::Approx(2.75f));
    CHECK(after->clusterCount == 12u);
    CHECK(after->pauseFraction == Catch::Approx(0.3125f));

    CHECK(after->scatterAnchor.terrain == "valley-floor");
    CHECK(after->scatterAnchor.layers == std::vector<std::string>{"oaks", "ferns"});
    CHECK(after->scatterAnchor.litOnly == true);
    CHECK(after->scatterAnchor.viewDistance == Catch::Approx(85.0f));
    CHECK(after->scatterAnchor.active());

    CHECK(after->scatterAnisotropy == Catch::Approx(0.3125f));
    CHECK(after->emitMaskField == "weather-front");

    REQUIRE(after->sizeCurve.keys.size() == 3);
    CHECK(after->sizeCurve.keys[1].t == Catch::Approx(0.5f));
    CHECK(after->sizeCurve.keys[1].value == Catch::Approx(1.5f));
    REQUIRE(after->colorCurve.keys.size() == 2);
    CHECK(after->colorCurve.keys[0].color.g == Catch::Approx(0.5f));
    REQUIRE(after->opacityCurve.keys.size() == 3);
    CHECK(after->opacityCurve.active());

    CHECK(after->velocityStretch == Catch::Approx(0.6875f));
    CHECK(after->stretchMin == Catch::Approx(0.09375f));

    CHECK(after->trailEnabled == true);
    CHECK(after->trailLength == 24u);
    CHECK(after->trailStride == 3u);
    CHECK(after->trailTint.b == Catch::Approx(0.25f));

    CHECK(after->fogCoupling == Catch::Approx(0.5f));
    CHECK(after->volumeGlow == Catch::Approx(2.25f));
}

TEST_CASE("the system's name is the node's, not the file's", "[scene][particles][io]") {
    // `particlesToJson` writes **no** `name`, and that is the design rather than an omission: the
    // owning composition node carries the name, and `Composition::rebuild` assigns
    // `node.particles.name = node.name`. It matters because the name is the **parameter path** --
    // `particles/<name>/spawnRate` -- so if the file carried its own name and the two ever
    // disagreed, a reload would move every particle parameter to a new path and silently orphan
    // the modulation routes and timeline tracks pointing at the old one.
    //
    // This is asserted rather than left implicit so that nobody "fixes" the missing key.
    scene::ParticleSystem s;
    s.name = "a name the file has no place for";
    s.spawnRate = 4321.0f;
    const json j = scene::particlesToJson(s);
    CHECK_FALSE(j.contains("name"));

    const auto back = scene::particlesFromJson(j);
    REQUIRE(back);
    CHECK(back->spawnRate == Catch::Approx(4321.0f));
    CHECK(back->name == scene::ParticleSystem{}.name); // the default, awaiting the node's
}

TEST_CASE("an absent particles object reads as the defaults", "[scene][particles][io]") {
    // What a file written before any of the optional groups existed looks like. It must read, and
    // read as the struct's own defaults, because "off is the default so nothing that does not ask
    // changes" is the promise every one of those ADR-520 comments makes.
    const auto s = scene::particlesFromJson(json::object());
    REQUIRE(s);
    const scene::ParticleSystem def;
    CHECK(s->collision == def.collision);
    CHECK(s->sizeVariance == Catch::Approx(def.sizeVariance));
    CHECK(s->sizeSkew == Catch::Approx(def.sizeSkew));
    CHECK(s->fogCoupling == Catch::Approx(def.fogCoupling));
    CHECK(s->scatterAnisotropy == Catch::Approx(def.scatterAnisotropy));
    CHECK(s->collisionRestitution == Catch::Approx(def.collisionRestitution));
    CHECK(s->trailEnabled == def.trailEnabled);
    CHECK(s->volumeWrap == def.volumeWrap);
    CHECK(s->softness == Catch::Approx(def.softness));
    CHECK(s->windInfluence == Catch::Approx(def.windInfluence));
}

TEST_CASE("a malformed particles object is refused, naming the key", "[scene][particles][io]") {
    SECTION("a field of the wrong JSON type") {
        json j = json::object();
        j["spawnRate"] = "fast";
        const auto s = scene::particlesFromJson(j);
        REQUIRE_FALSE(s);
        CHECK(s.error().message.find("spawnRate") != std::string::npos);
    }
    SECTION("an emitter shape that is not one of the names") {
        json j = json::object();
        j["shape"] = "dodecahedron";
        const auto s = scene::particlesFromJson(j);
        REQUIRE_FALSE(s);
        CHECK(s.error().message.find("dodecahedron") != std::string::npos);
    }
    SECTION("a blend mode that is not one of the names") {
        json j = json::object();
        j["blend"] = "subtract";
        CHECK_FALSE(scene::particlesFromJson(j));
    }
}
