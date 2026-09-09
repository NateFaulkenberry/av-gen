// Composition data (ADR-038): focal points, depth layers and exclusions as fields.

#include "scene/composition_data.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nlohmann/json.hpp>

using namespace avgen;
using Catch::Matchers::WithinAbs;

namespace {
scene::CompositionData makeComposition() {
    scene::CompositionData c;
    scene::FocalPoint hero;
    hero.name = "hero";
    hero.position = glm::vec3(0.0f, 4.0f, 0.0f);
    hero.radius = 10.0f;
    hero.clearance = 6.0f;
    hero.weight = 2.0f;
    c.focalPoints.push_back(hero);
    c.layers.push_back({"near", 0.0f, 20.0f, 0.4f, 1.2f, 1.1f, 1.5f});
    c.layers.push_back({"mid", 20.0f, 80.0f, 1.0f, 1.0f, 1.0f, 1.0f});
    c.layers.push_back({"far", 80.0f, 400.0f, 1.6f, 0.7f, 0.6f, 0.4f});
    scene::ExclusionRegion clear;
    clear.name = "corridor";
    clear.shape = scene::ExclusionRegion::Shape::Box;
    clear.position = glm::vec3(0.0f, 6.0f, -30.0f);
    clear.size = glm::vec3(6.0f, 8.0f, 40.0f);
    clear.softness = 2.0f;
    c.exclusions.push_back(clear);
    c.cameraTarget = "hero";
    c.targetScreenPosition = glm::vec2(0.333f, 0.5f);
    c.framingStrength = 0.7f;
    return c;
}
} // namespace

TEST_CASE("Composition data validates its own consistency", "[composition]") {
    scene::CompositionData c = makeComposition();
    CHECK(c.validate().has_value());

    scene::CompositionData bad = c;
    bad.cameraTarget = "missing";
    CHECK_FALSE(bad.validate().has_value());

    bad = c;
    bad.focalPoints[0].radius = 0.0f;
    CHECK_FALSE(bad.validate().has_value());

    bad = c;
    bad.layers[2].start = 5.0f; // out of order
    CHECK_FALSE(bad.validate().has_value());

    bad = c;
    bad.layers[1].end = bad.layers[1].start;
    CHECK_FALSE(bad.validate().has_value());
}

TEST_CASE("Depth layers select by distance and clamp at the far band", "[composition]") {
    const scene::CompositionData c = makeComposition();
    CHECK(c.layerAt(5.0f).name == "near");
    CHECK(c.layerAt(50.0f).name == "mid");
    CHECK(c.layerAt(200.0f).name == "far");
    CHECK(c.layerAt(100000.0f).name == "far"); // clamps rather than falling through
    CHECK_THAT(c.layerAt(5.0f).density, WithinAbs(0.4, 1e-6));

    // A scene with no bands behaves as an identity band.
    const scene::CompositionData empty;
    const scene::DepthLayer identity = empty.layerAt(42.0f);
    CHECK_THAT(identity.density, WithinAbs(1.0, 1e-6));
    CHECK_THAT(identity.contrast, WithinAbs(1.0, 1e-6));
}

TEST_CASE("Composition becomes ordinary fields", "[composition]") {
    const scene::CompositionData c = makeComposition();
    spatial::FieldSet fields;
    c.appendFields(fields);
    // clearance + weight for the focal point, plus one exclusion
    REQUIRE(fields.fields.size() == 3);
    CHECK(fields.find("composition.clearance.hero") != nullptr);
    CHECK(fields.find("composition.weight.hero") != nullptr);
    CHECK(fields.find("composition.exclusion.corridor") != nullptr);

    // Clearance empties the middle and opens up outside it.
    const spatial::FieldSpec& clearance = *fields.find("composition.clearance.hero");
    const float atCentre = spatial::sampleScalar(clearance, glm::vec3(0.0f, 4.0f, 0.0f), 0.0);
    const float farAway = spatial::sampleScalar(clearance, glm::vec3(60.0f, 4.0f, 0.0f), 0.0);
    CHECK(atCentre < 0.35f);
    CHECK(farAway > 0.9f);

    // Weight peaks on the focal point and falls off by its radius.
    const spatial::FieldSpec& weight = *fields.find("composition.weight.hero");
    const float peak = spatial::sampleScalar(weight, glm::vec3(0.0f, 4.0f, 0.0f), 0.0);
    const float edge = spatial::sampleScalar(weight, glm::vec3(0.0f, 4.0f, 12.0f), 0.0);
    CHECK(peak > edge);
    CHECK(peak > 1.0f); // scaled by the focal point's weight of 2
    CHECK(edge < 0.2f);

    // The exclusion is empty inside its box and full outside.
    const spatial::FieldSpec& exclusion = *fields.find("composition.exclusion.corridor");
    const float inside = spatial::sampleScalar(exclusion, glm::vec3(0.0f, 6.0f, -30.0f), 0.0);
    const float outside = spatial::sampleScalar(exclusion, glm::vec3(40.0f, 6.0f, -30.0f), 0.0);
    CHECK(inside < 0.1f);
    CHECK(outside > 0.9f);
}

TEST_CASE("Composition round-trips through JSON and hashes its content", "[composition]") {
    const scene::CompositionData c = makeComposition();
    const nlohmann::json j = c.toJson();
    auto back = scene::CompositionData::fromJson(j);
    REQUIRE(back.has_value());
    CHECK(back->focalPoints.size() == 1);
    CHECK(back->focalPoints[0].name == "hero");
    CHECK_THAT(back->focalPoints[0].clearance, WithinAbs(6.0, 1e-6));
    CHECK(back->layers.size() == 3);
    CHECK(back->exclusions.size() == 1);
    CHECK(back->exclusions[0].shape == scene::ExclusionRegion::Shape::Box);
    CHECK(back->cameraTarget == "hero");
    CHECK_THAT(back->framingStrength, WithinAbs(0.7, 1e-6));
    CHECK(back->structuralHash() == c.structuralHash());

    scene::CompositionData moved = c;
    moved.focalPoints[0].position.y += 0.5f;
    CHECK(moved.structuralHash() != c.structuralHash());

    // An empty object is valid and produces nothing.
    auto empty = scene::CompositionData::fromJson(nlohmann::json::object());
    REQUIRE(empty.has_value());
    CHECK(empty->focalPoints.empty());
    CHECK_FALSE(scene::CompositionData::fromJson(nlohmann::json(3)).has_value());
}
