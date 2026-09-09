#include "params/parameter_set.hpp"
#include "scene/particles.hpp"

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
    CHECK(params.size() == 20);
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
    CHECK(params.size() == 22);
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
