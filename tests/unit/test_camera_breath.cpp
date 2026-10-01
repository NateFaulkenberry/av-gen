// ADR-1048: camera breathing -- an authored, roll-free offset in the camera's own frame, driven by
// routes from the beat grid (ADR-1045), applied after whatever placed the camera.

#include "app/engine.hpp"
#include "core/time.hpp"
#include "params/modulation.hpp"
#include "scene/camera.hpp"
#include "scene/composition.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <cmath>

using namespace avgen;
using Catch::Matchers::WithinAbs;

namespace {
double d(float v) {
    return static_cast<double>(v);
}

// The camera's roll: the angle of its right vector out of the horizontal plane.
double roll(glm::vec3 position, glm::vec3 target) {
    const glm::vec3 f = glm::normalize(target - position);
    const glm::vec3 r = glm::cross(f, glm::vec3(0.0f, 1.0f, 0.0f));
    if (glm::length(r) < 1e-6f) {
        return 0.0;
    }
    return std::abs(d(glm::normalize(r).y));
}
} // namespace

TEST_CASE("Camera breath moves along the view, turns the aim without roll, and widens the lens",
          "[camera][breath][adr1048]") {
    const glm::vec3 eye(0.0f, 1.6f, 0.0f);
    const glm::vec3 aim(0.0f, 1.6f, -5.0f); // looking down -Z
    {
        glm::vec3 p = eye;
        glm::vec3 t = aim;
        float fov = 60.0f;
        scene::CameraBreath b;
        b.forward = 0.2f;
        b.lift = 0.05f;
        b.fov = -3.0f;
        scene::applyCameraBreath(b, p, t, fov);
        CHECK_THAT(d(p.z), WithinAbs(-0.2, 1e-6));
        CHECK_THAT(d(p.y), WithinAbs(1.65, 1e-6));
        CHECK_THAT(d(glm::length(t - p)), WithinAbs(5.0, 1e-5)); // the aim travels with the body
        CHECK_THAT(d(fov), WithinAbs(57.0, 1e-6));
    }
    {
        glm::vec3 p = eye;
        glm::vec3 t = aim;
        float fov = 60.0f;
        scene::CameraBreath b;
        b.yaw = 10.0f;
        b.pitch = 5.0f;
        scene::applyCameraBreath(b, p, t, fov);
        const glm::vec3 dir = glm::normalize(t - p);
        CHECK(dir.x < 0.0f); // + yaw turns left (towards -X when looking down -Z)
        CHECK_THAT(d(std::asin(dir.y)) * 180.0 / 3.14159265358979, WithinAbs(5.0, 1e-3));
        CHECK(roll(p, t) < 1e-6);
    }
    {
        // amount scales everything; 0 leaves the camera exactly where it was.
        glm::vec3 p = eye;
        glm::vec3 t = aim;
        float fov = 60.0f;
        scene::CameraBreath b;
        b.amount = 0.0f;
        b.forward = 1.0f;
        b.yaw = 30.0f;
        b.fov = 10.0f;
        scene::applyCameraBreath(b, p, t, fov);
        CHECK(p == eye);
        CHECK(t == aim);
        CHECK(fov == 60.0f);
        b.amount = 0.5f;
        scene::applyCameraBreath(b, p, t, fov);
        CHECK_THAT(d(fov), WithinAbs(65.0, 1e-6));
    }
}

TEST_CASE("Camera breath is a set of parameters a beat-grid route drives, in the engine's frame",
          "[camera][breath][adr1048][beatgrid]") {
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine
                .setCompositionJson(nlohmann::json::parse(R"({"format": "avgen-scene", "version": 1, "name": "breath",
      "camera": {"mode": 1, "position": [0, 1.6, 0], "target": [0, 1.6, -5], "fov": 60},
      "nodes": [{"kind": "orb", "name": "o", "position": [0, 1, -5]}]})"))
                .has_value());
    for (const char* leaf : {"amount", "forward", "lift", "side", "yaw", "pitch", "fov"}) {
        REQUIRE(engine.params().find(std::string("camera/breath/") + leaf) != nullptr);
    }
    nlohmann::json doc = nlohmann::json::array();
    doc.push_back({{"kind", "beatgrid"},
                   {"name", "song"},
                   {"settings", nlohmann::json::parse(R"({"origin": 0, "tempo": [{"bar": 1, "bpm": 120}]})")}});
    REQUIRE(engine.sources().fromJson(doc).has_value());
    engine.sources().attach(engine.signals(), engine.params());
    params::ModRoute r;
    r.source = "grid.song.quarter.wave";
    r.target = "camera/breath/forward";
    r.amount = 0.25f;
    engine.modulator().addRoute(std::move(r));
    engine.rebind();
    engine.setViewport(640, 360);

    // Routes write finals during a frame; the composition reads them on the next, so run two frames
    // at each instant of interest. On a beat (0.5 s at 120 BPM) the wave is 1: pushed in 0.25 m.
    const auto settle = [&](double t) {
        engine.update(FrameTime{t, 1.0 / 60.0, 1});
        engine.update(FrameTime{t, 0.0, 2});
        return engine.scene().camera.position;
    };
    const glm::vec3 onBeat = settle(1.0);
    CHECK_THAT(d(onBeat.z), WithinAbs(-0.25, 1e-3));
    // Half way between beats the wave is 0: back at rest.
    const glm::vec3 offBeat = settle(1.25);
    CHECK_THAT(d(offBeat.z), WithinAbs(0.0, 1e-3));
    // A section that does not breathe: amount 0.
    engine.params().findAs<float>("camera/breath/amount")->setBase(0.0f);
    const glm::vec3 still = settle(1.0);
    CHECK_THAT(d(still.z), WithinAbs(0.0, 1e-6));
}
