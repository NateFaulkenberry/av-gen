// ADR-1401: a procedural object can set the finite-difference step of its deformed normal.
#include "scene/procedural.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

using namespace avgen;

TEST_CASE("A procedural object's normalStep round-trips, defaults to automatic and refuses a negative step",
          "[unit][procedural][normal-step]") {
    scene::ProceduralGeometry g;
    CHECK(g.normalStep == 0.0f);
    CHECK_FALSE(g.toJson().contains("normalStep")); // automatic: unwritten, so every existing file is unchanged
    g.normalStep = 0.12f;
    const nlohmann::json j = g.toJson();
    REQUIRE(j.contains("normalStep"));
    auto back = scene::ProceduralGeometry::fromJson(j);
    REQUIRE(back.has_value());
    CHECK(back->normalStep == 0.12f);
    nlohmann::json bad = j;
    bad["normalStep"] = -1.0;
    CHECK_FALSE(scene::ProceduralGeometry::fromJson(bad).has_value());
}
