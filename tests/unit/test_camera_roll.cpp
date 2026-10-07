// ADR-1075: camera/roll turns the camera's up about its line of sight, and nothing else.

#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

using namespace avgen;
using Catch::Approx;

TEST_CASE("camera/roll turns the camera's up about the line of sight", "[camera][roll][adr1075]") {
    assets::AssetRegistry registry{testsupport::processTempDir()};
    auto comp = scene::Composition::fromJson(nlohmann::json::parse(R"({
        "format": "avgen-scene", "version": 1, "name": "roll",
        "camera": { "mode": 1, "position": [0, 0, 10], "target": [0, 0, 0], "fov": 45.0 },
        "nodes": [ { "name": "box", "kind": "procedural", "procedural": {
            "source": { "kind": "box", "size": [1, 1, 1] }, "distribution": { "kind": "single" } } } ] })"),
                                             registry);
    REQUIRE(comp.has_value());
    params::ParameterSet params;
    params::Modulator modulator;
    (*comp)->attach(params, modulator);
    const auto frame = [&] {
        params.resetFinals();
        FrameTime t{};
        t.renderTime = 1.0;
        (*comp)->update(t);
    };
    frame();
    CHECK((*comp)->scene().camera.up.y == Approx(1.0f));
    auto* roll = params.findAs<float>("camera/roll");
    REQUIRE(roll != nullptr);
    roll->setBase(90.0f);
    frame();
    const glm::vec3 up = (*comp)->scene().camera.up;
    CHECK(std::abs(up.x) == Approx(1.0f).margin(1e-4));
    CHECK(up.y == Approx(0.0f).margin(1e-4));
    CHECK((*comp)->scene().camera.position.z == Approx(10.0f));
    roll->setBase(0.0f);
    frame();
    CHECK((*comp)->scene().camera.up == glm::vec3(0.0f, 1.0f, 0.0f));
}

// ADR-1166: a spline camera leans into its turns (camera/splineBank), and only when asked to.
TEST_CASE("a spline camera banks into its turns", "[camera][roll][adr1166]") {
    assets::AssetRegistry registry{testsupport::processTempDir()};
    // A path heading down -Z that turns LEFT (toward -X): a quarter circle of radius 10 after a straight.
    auto comp = scene::Composition::fromJson(nlohmann::json::parse(R"({
        "format": "avgen-scene", "version": 1, "name": "bank",
        "camera": { "mode": 2, "spline": "flight", "fov": 45.0 },
        "nodes": [
          { "name": "flight", "kind": "spline", "spline": { "name": "flight", "kind": "catmullRom", "closed": false,
            "points": [ { "position": [0, 2, 20] }, { "position": [0, 2, 10] }, { "position": [0, 2, 0] },
                        { "position": [-2.93, 2, -7.07] }, { "position": [-10, 2, -10] }, { "position": [-20, 2, -10] } ] } },
          { "name": "box", "kind": "procedural", "procedural": {
            "source": { "kind": "box", "size": [1, 1, 1] }, "distribution": { "kind": "single" } } } ] })"),
                                             registry);
    REQUIRE(comp.has_value());
    params::ParameterSet params;
    params::Modulator modulator;
    (*comp)->attach(params, modulator);
    const auto frame = [&] {
        params.resetFinals();
        FrameTime t{};
        t.renderTime = 1.0;
        (*comp)->update(t);
    };
    auto* t = params.findAs<float>("camera/splineT");
    auto* look = params.findAs<float>("camera/lookAhead");
    auto* bank = params.findAs<float>("camera/splineBank");
    REQUIRE(t != nullptr);
    REQUIRE(look != nullptr);
    REQUIRE(bank != nullptr);
    look->setBase(6.0f);
    t->setBase(0.45f); // entering the left turn
    frame();
    CHECK((*comp)->scene().camera.up == glm::vec3(0.0f, 1.0f, 0.0f)); // bank 0: the world's up, as before ADR-1166
    bank->setBase(30.0f);
    frame();
    const glm::vec3 up = (*comp)->scene().camera.up;
    const glm::vec3 forward = glm::normalize((*comp)->scene().camera.target - (*comp)->scene().camera.position);
    const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    INFO("up " << up.x << ", " << up.y << ", " << up.z);
    CHECK(glm::dot(up, right) < -0.05f); // the up vector leans LEFT, into the turn
    CHECK(up.y > 0.7f);                   // a lean, not a roll over
    t->setBase(0.05f);                    // the straight: nothing to lean into
    frame();
    CHECK((*comp)->scene().camera.up.y == Approx(1.0f).margin(1e-4));
}
