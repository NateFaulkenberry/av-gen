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

// A spline camera parked at the end of an open spline kept its target on the end point too: the two coincided, and
// the aim was float rounding (single frames pointing anywhere, THE RIFT at 201-207 s). The look-ahead now continues
// along the end tangent, so the camera keeps looking the way it was flying.
TEST_CASE("a spline camera at the end of an open spline keeps looking along it", "[camera][spline]") {
    assets::AssetRegistry registry{testsupport::processTempDir()};
    auto comp = scene::Composition::fromJson(nlohmann::json::parse(R"({
        "format": "avgen-scene", "version": 1, "name": "end",
        "camera": { "mode": 2, "spline": "flight", "fov": 45.0 },
        "nodes": [
          { "name": "flight", "kind": "spline", "spline": { "name": "flight", "kind": "catmullRom", "closed": false,
            "points": [ { "position": [0, 2, 0] }, { "position": [0, 2, -100] }, { "position": [0, 2, -200] },
                        { "position": [0, 2, -300] } ] } },
          { "name": "box", "kind": "procedural", "procedural": {
            "source": { "kind": "box", "size": [1, 1, 1] }, "distribution": { "kind": "single" } } } ] })"),
                                             registry);
    REQUIRE(comp.has_value());
    params::ParameterSet params;
    params::Modulator modulator;
    (*comp)->attach(params, modulator);
    auto* t = params.findAs<float>("camera/splineT");
    auto* look = params.findAs<float>("camera/lookAhead");
    auto* offset = params.findAs<glm::vec3>("camera/splineOffset");
    REQUIRE(t != nullptr);
    REQUIRE(look != nullptr);
    REQUIRE(offset != nullptr);
    look->setBase(20.0f);
    // At, and past, the end (the clamp), with an offset that drifts a little each frame (as an LFO does).
    for (int k = 0; k < 40; ++k) {
        t->setBase(k < 20 ? 1.0f : 1.05f);
        offset->setBase(glm::vec3(3.0f + 0.013f * static_cast<float>(k), 1.5f - 0.007f * static_cast<float>(k), 0.0f));
        params.resetFinals();
        FrameTime ft{};
        ft.renderTime = 1.0 + k / 30.0;
        (*comp)->update(ft);
        const scene::Camera& cam = (*comp)->scene().camera;
        const glm::vec3 forward = glm::normalize(cam.target - cam.position);
        INFO("frame " << k << ": forward " << forward.x << ", " << forward.y << ", " << forward.z);
        REQUIRE(forward.z < -0.99f); // still looking down the path's direction (-Z)
        CHECK(glm::length(cam.target - cam.position) == Approx(20.0f).margin(0.1f));
    }
}

// ADR-1541: a spline camera's up can follow the spline's own frame (its normal, with the points' authored roll),
// so a camera riding a twisting band of land turns with the land -- through upside down -- and only when asked to.
TEST_CASE("a spline camera's up follows the spline's frame when asked", "[camera][roll][adr1541]") {
    assets::AssetRegistry registry{testsupport::processTempDir()};
    // A straight path down -Z whose authored roll turns half a turn: upright at the start, on its side half way,
    // upside down at the end.
    auto comp = scene::Composition::fromJson(nlohmann::json::parse(R"({
        "format": "avgen-scene", "version": 1, "name": "twist",
        "camera": { "mode": 2, "spline": "band", "fov": 45.0 },
        "nodes": [
          { "name": "band", "kind": "spline", "spline": { "name": "band", "kind": "catmullRom", "closed": false,
            "up": [0, 1, 0],
            "points": [ { "position": [0, 2, 0], "roll": 0.0 }, { "position": [0, 2, -100], "roll": 0.5236 },
                        { "position": [0, 2, -200], "roll": 1.0472 }, { "position": [0, 2, -300], "roll": 1.5708 },
                        { "position": [0, 2, -400], "roll": 2.0944 }, { "position": [0, 2, -500], "roll": 2.618 },
                        { "position": [0, 2, -600], "roll": 3.1416 } ] } },
          { "name": "box", "kind": "procedural", "procedural": {
            "source": { "kind": "box", "size": [1, 1, 1] }, "distribution": { "kind": "single" } } } ] })"),
                                             registry);
    REQUIRE(comp.has_value());
    params::ParameterSet params;
    params::Modulator modulator;
    (*comp)->attach(params, modulator);
    auto* t = params.findAs<float>("camera/splineT");
    auto* follow = params.findAs<float>("camera/splineUp");
    REQUIRE(t != nullptr);
    REQUIRE(follow != nullptr);
    const auto frame = [&](float at) {
        t->setBase(at);
        params.resetFinals();
        FrameTime ft{};
        ft.renderTime = 1.0;
        (*comp)->update(ft);
        return (*comp)->scene().camera.up;
    };
    // Off (the default): the world's up wherever the camera is, as before ADR-1541.
    CHECK(frame(0.5f) == glm::vec3(0.0f, 1.0f, 0.0f));
    CHECK(frame(1.0f) == glm::vec3(0.0f, 1.0f, 0.0f));
    follow->setBase(1.0f);
    const auto& spline = *(*comp)->scene().splines.find("band");
    for (float at : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        const glm::vec3 up = frame(at);
        const glm::vec3 n = spline.sampleByDistance(at * spline.length()).normal;
        INFO("t " << at << ": up " << up.x << ", " << up.y << ", " << up.z << "; normal " << n.x << ", " << n.y << ", " << n.z);
        CHECK(glm::dot(up, glm::normalize(n)) == Approx(1.0f).margin(1e-4));
        CHECK(std::abs(up.z) < 1e-3f); // still perpendicular to the path
    }
    CHECK(frame(0.0f).y == Approx(1.0f).margin(1e-3));      // upright at the start
    CHECK(std::abs(frame(0.5f).y) < 0.05f);                  // on its side half way
    CHECK(frame(1.0f).y == Approx(-1.0f).margin(1e-3));     // upside down at the end
    // Half way between the world's up and the frame's: the angle halves.
    follow->setBase(0.5f);
    const glm::vec3 half = frame(0.5f);
    CHECK(half.y == Approx(std::cos(glm::radians(45.0f))).margin(0.02f));
}
