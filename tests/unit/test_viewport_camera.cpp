// Viewport camera gestures (ADR-068). These are pure vector arithmetic over an eye and a target,
// which is the whole reason they live apart from the SDL plumbing: an orbit that drifts, flips at
// the pole or dollies through its subject is a bug you can only find by flying the camera, unless
// the maths is separable, in which case you can find it here.

#include "app/viewport_camera.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>

using namespace avgen;
using app::CameraPose;
using app::ViewportControlSettings;
using app::ViewportGesture;

namespace {
constexpr float kUp = 1.0f;

CameraPose startPose() {
    CameraPose pose;
    pose.eye = glm::vec3(0.0f, 3.0f, 10.0f);
    pose.target = glm::vec3(0.0f, 1.0f, 0.0f);
    return pose;
}

float distanceOf(const CameraPose& pose) { return glm::length(pose.eye - pose.target); }
} // namespace

TEST_CASE("Orbiting swings the eye and leaves the subject alone", "[viewport][camera]") {
    const ViewportControlSettings settings;
    const CameraPose before = startPose();
    const CameraPose after = app::applyDrag(before, ViewportGesture::Orbit, glm::vec2(60.0f, 0.0f), settings);

    // The target is what "orbit" is about: it does not move.
    CHECK_THAT(glm::length(after.target - before.target), Catch::Matchers::WithinAbs(0.0, 1e-6));
    // The eye moved, and stayed the same distance away -- an orbit that changes its radius is a
    // dolly wearing an orbit's name.
    CHECK(glm::length(after.eye - before.eye) > 0.5f);
    CHECK_THAT(distanceOf(after), Catch::Matchers::WithinRel(distanceOf(before), 1e-4f));
}

TEST_CASE("Orbiting cannot reach the pole", "[viewport][camera]") {
    ViewportControlSettings settings;
    const CameraPose start = startPose();

    // Drag far past vertical, repeatedly. Without a clamp the view direction crosses straight up,
    // the azimuth becomes undefined and the camera spins in place; with one it stops just short.
    CameraPose pose = start;
    for (int i = 0; i < 40; ++i) {
        pose = app::applyDrag(pose, ViewportGesture::Orbit, glm::vec2(0.0f, 200.0f), settings);
    }
    const glm::vec3 dir = glm::normalize(pose.eye - pose.target);
    const float toUp = std::acos(std::clamp(dir.y, -1.0f, 1.0f));
    CHECK(toUp >= settings.poleMargin * 0.999f);
    CHECK(distanceOf(pose) > 0.0f);
    CHECK(std::isfinite(pose.eye.x));
    CHECK(std::isfinite(pose.eye.y));
    CHECK(std::isfinite(pose.eye.z));

    // The other pole too.
    pose = start;
    for (int i = 0; i < 40; ++i) {
        pose = app::applyDrag(pose, ViewportGesture::Orbit, glm::vec2(0.0f, -200.0f), settings);
    }
    const glm::vec3 down = glm::normalize(pose.eye - pose.target);
    CHECK(std::acos(std::clamp(down.y, -1.0f, 1.0f)) <= 3.14159265f - settings.poleMargin * 0.999f);
}

TEST_CASE("Panning moves the eye and its subject together", "[viewport][camera]") {
    const ViewportControlSettings settings;
    const CameraPose before = startPose();
    const CameraPose after = app::applyDrag(before, ViewportGesture::Pan, glm::vec2(40.0f, 25.0f), settings);

    const glm::vec3 eyeMove = after.eye - before.eye;
    const glm::vec3 targetMove = after.target - before.target;
    // Rigid: the framing slides, the relationship does not change.
    CHECK_THAT(glm::length(eyeMove - targetMove), Catch::Matchers::WithinAbs(0.0, 1e-5));
    CHECK(glm::length(eyeMove) > 0.0f);
    CHECK_THAT(distanceOf(after), Catch::Matchers::WithinRel(distanceOf(before), 1e-5f));
}

TEST_CASE("Panning covers the same picture at any distance", "[viewport][camera]") {
    const ViewportControlSettings settings;
    // The reason pan is scaled by distance. A fixed world-space rate feels weightless when you are
    // two metres from the subject and immovable when you are two hundred; the ratio below is what
    // makes the drag feel like it is moving the *image*.
    CameraPose near = startPose();
    near.eye = near.target + glm::vec3(0.0f, 0.0f, 5.0f);
    CameraPose far = startPose();
    far.eye = far.target + glm::vec3(0.0f, 0.0f, 50.0f);

    const float nearMove =
        glm::length(app::applyDrag(near, ViewportGesture::Pan, glm::vec2(30.0f, 0.0f), settings).eye - near.eye);
    const float farMove =
        glm::length(app::applyDrag(far, ViewportGesture::Pan, glm::vec2(30.0f, 0.0f), settings).eye - far.eye);
    CHECK_THAT(farMove / nearMove, Catch::Matchers::WithinRel(10.0f, 1e-3f));
}

TEST_CASE("Looking swings the view and leaves the camera where it stands", "[viewport][camera]") {
    const ViewportControlSettings settings;
    const CameraPose before = startPose();
    const CameraPose after = app::applyDrag(before, ViewportGesture::Look, glm::vec2(50.0f, 0.0f), settings);

    CHECK_THAT(glm::length(after.eye - before.eye), Catch::Matchers::WithinAbs(0.0, 1e-6));
    CHECK(glm::length(after.target - before.target) > 0.1f);
    // Looking around is not also a dolly: the target keeps its distance.
    CHECK_THAT(distanceOf(after), Catch::Matchers::WithinRel(distanceOf(before), 1e-4f));
}

TEST_CASE("Dollying is geometric and never reaches the subject", "[viewport][camera]") {
    ViewportControlSettings settings;
    const CameraPose start = startPose();

    const CameraPose closer = app::applyDolly(start, 1.0f, settings);
    CHECK(distanceOf(closer) < distanceOf(start));
    const CameraPose further = app::applyDolly(start, -1.0f, settings);
    CHECK(distanceOf(further) > distanceOf(start));

    // Same fraction per notch wherever you start: a wheel that takes a fixed *step* crawls when far
    // away and slams into the subject when close.
    CameraPose scaled = start;
    scaled.eye = scaled.target + (start.eye - start.target) * 7.0f;
    const float ratioNear = distanceOf(app::applyDolly(start, 1.0f, settings)) / distanceOf(start);
    const float ratioFar = distanceOf(app::applyDolly(scaled, 1.0f, settings)) / distanceOf(scaled);
    CHECK_THAT(ratioNear, Catch::Matchers::WithinRel(ratioFar, 1e-4f));

    // Wheeling in forever stops at the minimum rather than passing through the subject and
    // inverting the view, which is the classic way a viewport camera turns inside out.
    CameraPose pose = start;
    for (int i = 0; i < 200; ++i) {
        pose = app::applyDolly(pose, 1.0f, settings);
    }
    CHECK(distanceOf(pose) >= settings.minDistance * 0.999f);
    CHECK(glm::dot(glm::normalize(pose.eye - pose.target), glm::normalize(start.eye - start.target)) > 0.99f);
}

TEST_CASE("Framing a sphere keeps the direction and fixes the distance", "[viewport][camera]") {
    const CameraPose before = startPose();
    const glm::vec3 center(20.0f, 4.0f, -30.0f);
    const float radius = 6.0f;
    const float fov = 0.87f;
    const CameraPose after = app::frameSphere(before, center, radius, fov);

    CHECK_THAT(glm::length(after.target - center), Catch::Matchers::WithinAbs(0.0, 1e-5));
    // The user's angle on the subject is theirs; only how far away they stand is arithmetic.
    const glm::vec3 wasDir = glm::normalize(before.eye - before.target);
    const glm::vec3 isDir = glm::normalize(after.eye - after.target);
    CHECK_THAT(glm::dot(wasDir, isDir), Catch::Matchers::WithinAbs(1.0, 1e-5));
    // Far enough that the sphere fits the vertical field, with a little air.
    const float fits = radius / std::tan(fov * 0.5f);
    CHECK(distanceOf(after) > fits);
    CHECK(distanceOf(after) < fits * 2.0f);
}

TEST_CASE("A gesture of None changes nothing", "[viewport][camera]") {
    const CameraPose before = startPose();
    const CameraPose after =
        app::applyDrag(before, ViewportGesture::None, glm::vec2(100.0f, 100.0f), ViewportControlSettings{});
    CHECK_THAT(glm::length(after.eye - before.eye), Catch::Matchers::WithinAbs(0.0, 1e-9));
    CHECK_THAT(glm::length(after.target - before.target), Catch::Matchers::WithinAbs(0.0, 1e-9));
    static_cast<void>(kUp);
}
