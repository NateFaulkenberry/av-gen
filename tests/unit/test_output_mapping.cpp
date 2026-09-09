// Milestone 1.2: output mapping (homography, blend weights, JSON) and the output set's JSON.

#include "app/output_manager.hpp"
#include "rendering/output_mapping.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <cmath>

using namespace avgen;

namespace {
bool nearF(float a, float b, float eps) { return std::abs(a - b) <= eps; }
bool nearVec(glm::vec2 a, glm::vec2 b, float eps = 1e-4f) {
    return std::abs(a.x - b.x) <= eps && std::abs(a.y - b.y) <= eps;
}
} // namespace

TEST_CASE("Identity corners give the identity homography", "[outputs][mapping]") {
    const rendering::OutputMapping m = rendering::OutputMapping::identity();
    CHECK(m.isIdentity());
    const glm::mat3 h = rendering::homographyFromCorners(m.corners);
    for (int c = 0; c < 3; ++c) {
        for (int r = 0; r < 3; ++r) {
            CHECK(nearF(h[c][r], c == r ? 1.0f : 0.0f, 1e-6f));
        }
    }
    const glm::mat3 inv = rendering::inverseHomography(m.corners);
    CHECK(nearVec(rendering::projectPoint(inv, {0.3f, 0.7f}), {0.3f, 0.7f}));
}

TEST_CASE("A trapezoid homography maps the four corners exactly and inverts consistently", "[outputs][mapping]") {
    const std::array<glm::vec2, 4> corners = {{{0.2f, 0.1f}, {0.8f, 0.1f}, {1.0f, 1.0f}, {0.0f, 1.0f}}};
    const glm::mat3 h = rendering::homographyFromCorners(corners);
    CHECK(nearVec(rendering::projectPoint(h, {0.0f, 0.0f}), corners[0]));
    CHECK(nearVec(rendering::projectPoint(h, {1.0f, 0.0f}), corners[1]));
    CHECK(nearVec(rendering::projectPoint(h, {1.0f, 1.0f}), corners[2]));
    CHECK(nearVec(rendering::projectPoint(h, {0.0f, 1.0f}), corners[3]));
    // A projective (non-affine) quad: the bottom row is not (0, 0, 1).
    CHECK((std::abs(h[0][2]) > 1e-4f || std::abs(h[1][2]) > 1e-4f));
    // Straight lines stay straight: the top edge midpoint lands on the top edge of the quad.
    CHECK(nearVec(rendering::projectPoint(h, {0.5f, 0.0f}), {0.5f, 0.1f}));

    const glm::mat3 inv = rendering::inverseHomography(corners);
    for (const glm::vec2 p : {glm::vec2{0.1f, 0.2f}, glm::vec2{0.5f, 0.5f}, glm::vec2{0.9f, 0.95f}}) {
        CHECK(nearVec(rendering::projectPoint(inv, rendering::projectPoint(h, p)), p));
    }
    // The centre of the quad has positive w under the normalised inverse.
    const glm::vec2 centre = 0.25f * (corners[0] + corners[1] + corners[2] + corners[3]);
    const glm::vec3 q = inv * glm::vec3(centre, 1.0f);
    CHECK(q.z > 0.0f);
    // Points outside the trapezoid map outside the unit square.
    const glm::vec2 outside = rendering::projectPoint(inv, {0.02f, 0.05f});
    CHECK((outside.x < 0.0f || outside.y < 0.0f));

    // An affine quad keeps the bottom row (0, 0, 1).
    const std::array<glm::vec2, 4> affine = {{{0.1f, 0.1f}, {0.9f, 0.2f}, {1.0f, 0.9f}, {0.2f, 0.8f}}};
    const glm::mat3 ha = rendering::homographyFromCorners(affine);
    CHECK(nearF(ha[0][2], 0.0f, 1e-6f));
    CHECK(nearF(ha[1][2], 0.0f, 1e-6f));
    CHECK(nearF(ha[2][2], 1.0f, 1e-6f));
    CHECK(nearVec(rendering::projectPoint(ha, {1.0f, 1.0f}), affine[2]));
}

TEST_CASE("Blend weights are 0 at the edge, 1 past the width and follow the gamma curve", "[outputs][mapping]") {
    CHECK(nearF(rendering::blendWeight(0.0f, 0.2f, 2.2f), 0.0f, 1e-6f));
    CHECK(nearF(rendering::blendWeight(0.2f, 0.2f, 2.2f), 1.0f, 1e-6f));
    CHECK(nearF(rendering::blendWeight(0.5f, 0.2f, 2.2f), 1.0f, 1e-6f));
    CHECK(nearF(rendering::blendWeight(0.1f, 0.2f, 1.0f), 0.5f, 1e-6f));
    CHECK(nearF(rendering::blendWeight(0.1f, 0.2f, 2.0f), 0.25f, 1e-6f));
    CHECK(nearF(rendering::blendWeight(0.0f, 0.0f, 2.2f), 1.0f, 1e-6f)); // no width: no blend
    rendering::OutputBlend blend;
    blend.left = 0.5f;
    blend.top = 0.25f;
    CHECK(nearF(rendering::blendWeightAt({0.25f, 1.0f}, blend, 1.0f), 0.5f, 1e-6f));
    CHECK(nearF(rendering::blendWeightAt({0.25f, 0.125f}, blend, 1.0f), 0.25f, 1e-6f));
    CHECK(nearF(rendering::blendWeightAt({0.9f, 0.9f}, blend, 1.0f), 1.0f, 1e-6f));
    // Monotone ramp across a left blend.
    float previous = -1.0f;
    for (int i = 0; i <= 20; ++i) {
        const float w = rendering::blendWeightAt({static_cast<float>(i) / 20.0f, 0.5f}, blend, 2.2f);
        CHECK(w >= previous);
        previous = w;
    }
}

TEST_CASE("Output mapping JSON round-trips and fills defaults", "[outputs][json]") {
    rendering::OutputMapping m;
    m.crop = {0.1f, 0.2f, 0.5f, 0.6f};
    m.corners = {{{0.05f, 0.0f}, {0.95f, 0.02f}, {1.0f, 1.0f}, {0.0f, 0.98f}}};
    m.blend = {0.1f, 0.2f, 0.0f, 0.05f};
    m.blendGamma = 1.8f;
    m.brightness = 0.9f;
    m.gamma = 1.1f;
    m.flipX = true;
    m.flipY = false;
    const auto j = m.toJson();
    CHECK(j["crop"].size() == 4);
    CHECK(j["corners"].size() == 4);
    auto back = rendering::OutputMapping::fromJson(j);
    REQUIRE(back.has_value());
    CHECK(*back == m);
    CHECK_FALSE(back->isIdentity());

    // Missing fields keep defaults; an empty object is the identity.
    auto empty = rendering::OutputMapping::fromJson(nlohmann::json::object());
    REQUIRE(empty.has_value());
    CHECK(empty->isIdentity());
    CHECK(*empty == rendering::OutputMapping::identity());
    auto partial = rendering::OutputMapping::fromJson(nlohmann::json{{"flipY", true}});
    REQUIRE(partial.has_value());
    CHECK(partial->flipY);
    CHECK(partial->blendGamma == 2.2f);

    // Errors: wrong shapes and invalid geometry.
    CHECK_FALSE(rendering::OutputMapping::fromJson(nlohmann::json{{"crop", {0.0, 0.0, 1.0}}}).has_value());
    CHECK_FALSE(rendering::OutputMapping::fromJson(nlohmann::json{{"corners", {{0, 0}, {1, 0}}}}).has_value());
    CHECK_FALSE(rendering::OutputMapping::fromJson(nlohmann::json{{"flipX", 1}}).has_value());
    CHECK_FALSE(rendering::OutputMapping::fromJson(nlohmann::json{{"crop", {0.5, 0.5, 1.0, 1.0}}}).has_value());
    // A bow-tie quad is rejected.
    CHECK_FALSE(rendering::OutputMapping::fromJson(
                    nlohmann::json{{"corners", {{0, 0}, {1, 1}, {1, 0}, {0, 1}}}})
                    .has_value());
}

TEST_CASE("OutputManager JSON round-trips the outputs block with defaults", "[outputs][json]") {
    app::OutputManager manager;
    app::OutputDesc left;
    left.name = "left";
    left.display = 1;
    left.fullscreen = true;
    left.mapping.crop = {0.0f, 0.0f, 0.55f, 1.0f};
    left.mapping.blend.right = 0.1f;
    app::OutputDesc right;
    right.name = "right";
    right.display = 2;
    right.width = 1280;
    right.height = 720;
    right.borderless = false;
    right.alwaysOnTop = true;
    right.enabled = false;
    right.mapping.crop = {0.45f, 0.0f, 0.55f, 1.0f};
    right.mapping.blend.left = 0.1f;
    right.mapping.corners[0] = {0.05f, 0.0f};
    REQUIRE(manager.add(left).has_value());
    REQUIRE(manager.add(right).has_value());
    CHECK_FALSE(manager.add(right).has_value()); // duplicate name
    app::OutputDesc bad;
    CHECK_FALSE(manager.add(bad).has_value()); // empty name
    CHECK(manager.outputs().size() == 2);
    CHECK(manager.openCount() == 0);

    const auto j = manager.toJson();
    REQUIRE(j.is_array());
    REQUIRE(j.size() == 2);
    CHECK(j[0]["name"] == "left");
    CHECK(j[1]["mapping"]["blend"][0] == 0.1f);

    app::OutputManager loaded;
    REQUIRE(loaded.fromJson(j).has_value());
    REQUIRE(loaded.outputs().size() == 2);
    CHECK(loaded.outputs()[0]->desc == left);
    CHECK(loaded.outputs()[1]->desc == right);
    CHECK(loaded.find("right") != nullptr);
    CHECK(loaded.find("nope") == nullptr);
    CHECK(loaded.remove("left"));
    CHECK_FALSE(loaded.remove("left"));
    CHECK(loaded.outputs().size() == 1);

    // Defaults: a name is enough.
    app::OutputManager minimal;
    REQUIRE(minimal.fromJson(nlohmann::json::parse(R"([{"name": "proj"}])")).has_value());
    const auto& d = minimal.outputs()[0]->desc;
    CHECK(d.width == 1920);
    CHECK(d.height == 1080);
    CHECK(d.display == -1);
    CHECK(d.borderless);
    CHECK(d.enabled);
    CHECK_FALSE(d.fullscreen);
    CHECK(d.mapping.isIdentity());

    // Errors: not an array, duplicate names, missing name, bad mapping. The set is untouched.
    CHECK_FALSE(minimal.fromJson(nlohmann::json::object()).has_value());
    CHECK_FALSE(minimal.fromJson(nlohmann::json::parse(R"([{"name": "a"}, {"name": "a"}])")).has_value());
    CHECK_FALSE(minimal.fromJson(nlohmann::json::parse(R"([{"display": 0}])")).has_value());
    CHECK_FALSE(minimal.fromJson(nlohmann::json::parse(R"([{"name": "a", "mapping": {"gamma": 0}}])")).has_value());
    CHECK_FALSE(minimal.fromJson(nlohmann::json::parse(R"([{"name": "a", "width": -5}])")).has_value());
    CHECK(minimal.outputs().size() == 1);
    CHECK(minimal.outputs()[0]->desc.name == "proj");
}
