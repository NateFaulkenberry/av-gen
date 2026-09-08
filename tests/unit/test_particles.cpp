#include "params/parameter_set.hpp"
#include "scene/particles.hpp"

#include <catch2/catch_test_macros.hpp>

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
