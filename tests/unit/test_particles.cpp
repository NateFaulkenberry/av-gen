#include "params/parameter_set.hpp"
#include "scene/particles.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace avgen;

TEST_CASE("Particle parameters register and apply with rest-relative scaling", "[scene][particles]") {
    params::ParameterSet params;
    scene::ParticleSystem rest;
    rest.name = "sparks";
    rest.spawnRate = 1000.0f;
    rest.lifetimeMin = 1.0f;
    rest.lifetimeMax = 3.0f;
    rest.sizeStart = 0.05f;
    rest.sizeEnd = 0.01f;
    auto p = scene::registerParticleParameters(params, rest);
    REQUIRE(p.spawnRate != nullptr);
    CHECK(params.find("particles/sparks/spawnRate") != nullptr);
    CHECK(params.find("particles/sparks/colorStart")->kind() == params::ParamKind::Color);
    CHECK(params.find("particles/sparks/enabled")->kind() == params::ParamKind::Bool);
    CHECK(p.spawnRate->value() == 1000.0f);

    p.spawnRate->setBase(250.0f);
    p.lifetime->setBase(2.0f);
    p.size->setBase(0.5f);
    p.burst->setBase(40.0f);
    p.enabled->setBase(false);
    params.resetFinals();
    scene::ParticleSystem live = rest;
    scene::applyParticleParameters(p, rest, live);
    CHECK(live.spawnRate == 250.0f);
    CHECK(live.lifetimeMin == 2.0f);
    CHECK(live.lifetimeMax == 6.0f);
    CHECK(live.sizeStart == 0.025f);
    CHECK(live.sizeEnd == 0.005f);
    CHECK(live.burst == 40.0f);
    CHECK_FALSE(live.enabled);
    // Registering the same system twice returns the same parameters (no duplicates).
    auto again = scene::registerParticleParameters(params, rest);
    CHECK(again.spawnRate == p.spawnRate);
    // ADR-040 added stretch and trailWidth; ADR-367 added softness, which had existed as a field
    // and a serialised value for far longer and was registered by nobody.
    CHECK(params.size() == 23);
    // Named as well as counted. A bare count says a parameter arrived and not which one, and this
    // number has now been wrong twice for the same reason -- somebody made a value reachable.
    CHECK(params.find("particles/sparks/softness") == p.softness);
}

TEST_CASE("Field force modes and per-slot strength parameters", "[scene][particles]") {
    for (const auto mode : {scene::FieldForceMode::Force, scene::FieldForceMode::Velocity, scene::FieldForceMode::Turbulence,
                            scene::FieldForceMode::Kill}) {
        CHECK(scene::fieldForceModeFromName(scene::fieldForceModeName(mode)) == mode);
    }
    CHECK(scene::fieldForceModeName(scene::FieldForceMode::Turbulence) == std::string("turbulence"));
    CHECK_FALSE(scene::fieldForceModeFromName("Force").has_value());

    params::ParameterSet params;
    scene::ParticleSystem rest;
    rest.name = "dust";
    scene::FieldForce a;
    a.field = "wind";
    a.strength = 2.0f;
    scene::FieldForce b;
    b.field = "swirl";
    b.mode = scene::FieldForceMode::Turbulence;
    b.strength = 0.5f;
    rest.fieldForces = {a, b};
    auto p = scene::registerParticleParameters(params, rest);
    CHECK(params.size() == 25); // 23 base (ADR-367's softness included) + 2 field-force strengths
    REQUIRE(p.fieldStrength[0] != nullptr);
    REQUIRE(p.fieldStrength[1] != nullptr);
    CHECK(p.fieldStrength[2] == nullptr);
    CHECK(params.find("particles/dust/fieldForce/1/strength") == p.fieldStrength[0]);
    CHECK(p.fieldStrength[1]->label() == "turbulence/strength");
    CHECK(p.fieldStrength[0]->value() == 2.0f);
    p.fieldStrength[1]->setBase(3.0f);
    params.resetFinals();
    scene::ParticleSystem live = rest;
    live.fieldForces.clear(); // the list shape follows rest
    scene::applyParticleParameters(p, rest, live);
    REQUIRE(live.fieldForces.size() == 2);
    CHECK(live.fieldForces[0].strength == 2.0f);
    CHECK(live.fieldForces[1].strength == 3.0f);
    CHECK(live.fieldForces[1].field == "swirl");
}

// ---- ADR-040: lifetime curves, velocity stretching and the trail memory budget ----------------

TEST_CASE("Lifetime curves evaluate as clamped piecewise-linear ramps", "[scene][particles][curves]") {
    scene::ParticleCurve curve;
    CHECK_FALSE(curve.active());
    curve.keys = {{0.0f, 0.0f}, {0.25f, 1.0f}, {0.75f, 0.5f}, {1.0f, 0.0f}};
    CHECK(curve.active());
    // Clamped outside the first and last key.
    CHECK(curve.evaluate(-1.0f) == Catch::Approx(0.0f));
    CHECK(curve.evaluate(0.0f) == Catch::Approx(0.0f));
    CHECK(curve.evaluate(2.0f) == Catch::Approx(0.0f));
    // Hand-computed interpolations.
    CHECK(curve.evaluate(0.125f) == Catch::Approx(0.5f));          // half way up 0 -> 1
    CHECK(curve.evaluate(0.25f) == Catch::Approx(1.0f));           // exactly on a key
    CHECK(curve.evaluate(0.5f) == Catch::Approx(0.75f));           // half way 1 -> 0.5
    CHECK(curve.evaluate(0.75f) == Catch::Approx(0.5f));
    CHECK(curve.evaluate(0.875f) == Catch::Approx(0.25f));         // half way 0.5 -> 0
    // A single key is a constant, and no keys is the fallback.
    scene::ParticleCurve one;
    one.keys = {{0.4f, 3.0f}};
    CHECK(one.evaluate(0.0f) == Catch::Approx(3.0f));
    CHECK(one.evaluate(1.0f) == Catch::Approx(3.0f));

    scene::ParticleColorCurve colors;
    colors.keys = {{0.0f, {1.0f, 0.0f, 0.0f}}, {1.0f, {0.0f, 0.0f, 1.0f}}};
    const glm::vec3 mid = colors.evaluate(0.5f);
    CHECK(mid.r == Catch::Approx(0.5f));
    CHECK(mid.g == Catch::Approx(0.0f));
    CHECK(mid.b == Catch::Approx(0.5f));
    CHECK(colors.evaluate(-0.5f).r == Catch::Approx(1.0f));
    CHECK(colors.evaluate(9.0f).b == Catch::Approx(1.0f));
}

TEST_CASE("Velocity stretch length matches the analytic formula", "[scene][particles][stretch]") {
    scene::ParticleSystem s;
    s.velocityStretch = 2.0f;
    s.stretchMax = 0.5f;
    s.stretchMin = 0.0f;
    const float shutter = 1.0f / 60.0f; // 180 degrees at 30 fps
    // length = speed * shutter * stretch, clamped to stretchMax.
    CHECK(scene::particleStretchLength(3.0f, shutter, s) == Catch::Approx(3.0f * shutter * 2.0f));
    CHECK(scene::particleStretchLength(0.0f, shutter, s) == Catch::Approx(0.0f));
    CHECK(scene::particleStretchLength(100.0f, shutter, s) == Catch::Approx(0.5f)); // capped
    // A zero shutter angle is exactly no stretch, whatever the speed.
    CHECK(scene::particleStretchLength(100.0f, 0.0f, s) == Catch::Approx(0.0f));
    // Off by default.
    scene::ParticleSystem round;
    CHECK(scene::particleStretchLength(50.0f, shutter, round) == Catch::Approx(0.0f));
    // The minimum keeps slow particles perfectly round rather than slightly oval.
    s.stretchMin = 0.05f;
    CHECK(scene::particleStretchLength(1.0f, shutter, s) == Catch::Approx(0.0f)); // 0.033 < 0.05
    CHECK(scene::particleStretchLength(3.0f, shutter, s) == Catch::Approx(0.1f));
}

TEST_CASE("The trail memory budget refuses over-large ribbon systems", "[scene][particles][trails]") {
    scene::ParticleSystem hero;
    hero.name = "arcs";
    hero.capacity = 32768;
    hero.trailEnabled = true;
    hero.trailLength = 32;
    // 32 k particles keeping 31 previous positions each: 16 bytes a point.
    CHECK(scene::trailHistoryPoints(hero) == 31);
    CHECK(scene::trailMemoryBytes(hero) == 32768ull * 31 * 16);
    CHECK(scene::trailMemoryBytes(hero) < scene::kMaxTrailBytes);
    CHECK(scene::validateParticleSystem(hero).has_value());

    scene::ParticleSystem crowd = hero;
    crowd.name = "dust";
    crowd.capacity = 1u << 20; // a million particles with trails is 496 MiB
    auto refused = scene::validateParticleSystem(crowd);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().message.find("budget") != std::string::npos);
    // The same system without trails costs nothing and is accepted.
    crowd.trailEnabled = false;
    CHECK(scene::trailMemoryBytes(crowd) == 0);
    CHECK(scene::validateParticleSystem(crowd).has_value());

    scene::ParticleSystem badLength = hero;
    badLength.trailLength = 64; // over kMaxTrailPoints
    CHECK_FALSE(scene::validateParticleSystem(badLength).has_value());
    scene::ParticleSystem badStride = hero;
    badStride.trailStride = 0;
    CHECK_FALSE(scene::validateParticleSystem(badStride).has_value());

    scene::ParticleSystem unsorted;
    unsorted.sizeCurve.keys = {{0.5f, 1.0f}, {0.2f, 0.0f}};
    CHECK_FALSE(scene::validateParticleSystem(unsorted).has_value());
    scene::ParticleSystem tooMany;
    for (int i = 0; i <= scene::kMaxCurveKeys; ++i) {
        tooMany.opacityCurve.keys.push_back({static_cast<float>(i) * 0.1f, 1.0f});
    }
    CHECK_FALSE(scene::validateParticleSystem(tooMany).has_value());
}
