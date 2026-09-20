// MotionContext (Phase B §4): the keyhole a procedural motion layer looks through.
//
// Two things are worth testing here and they are not the obvious one. The obvious one -- that the
// struct carries the values put into it -- cannot fail. The two that can:
//
//   * **the normal conversion**, because a normal is a covector and the inverse is the wrong matrix
//     for it under any non-uniform scale. Glowmere draws these bodies at 3.3x to 3.6x, so the
//     difference between right and wrong here is invisible on a uniform scale and a foot rotated
//     into a hillside the moment a scene squashes one axis (ADR-359);
//   * **that the seam arrives**, because a field published into a context nobody reads is exactly
//     the failure ADR-553 and the `LocomotionState` publication gaps were.

#include "scene/motion_context.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/gtc/matrix_transform.hpp>

using namespace avgen;
using Catch::Approx;

namespace {

// A node transform with everything on: a translation, a rotation, and a **non-uniform** scale, so
// that inverse and inverse-transpose genuinely differ. A fixture with a uniform scale would pass
// whichever matrix the code used, which is the whole reason this one does not have one.
scene::MotionContext awkwardNode() {
    glm::mat4 m(1.0f);
    m = glm::translate(m, glm::vec3(120.0f, 7.5f, -64.0f));
    m = glm::rotate(m, glm::radians(37.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    m = glm::scale(m, glm::vec3(3.4f, 1.1f, 0.6f));
    scene::MotionContext ctx;
    ctx.worldFromLocal = m;
    ctx.localFromWorld = glm::inverse(m);
    return ctx;
}

} // namespace

TEST_CASE("a point survives the round trip through both spaces", "[motion][context]") {
    const scene::MotionContext ctx = awkwardNode();
    const glm::vec3 local(0.31f, 1.62f, -0.08f);
    const glm::vec3 back = ctx.toLocal(ctx.toWorld(local));
    CHECK(back.x == Approx(local.x).margin(1e-4));
    CHECK(back.y == Approx(local.y).margin(1e-4));
    CHECK(back.z == Approx(local.z).margin(1e-4));
    // And the world position is genuinely elsewhere, so the round trip is not the identity in
    // disguise (ADR-182).
    const glm::vec3 world = ctx.toWorld(local);
    CHECK(glm::length(world - local) > 100.0f);
}

TEST_CASE("a normal goes through the transpose, not the inverse", "[motion][context]") {
    const scene::MotionContext ctx = awkwardNode();

    // A surface in world space, defined by two tangents, and its normal. Under a non-uniform scale
    // the tangents and the normal transform by *different* matrices, and the only way to be right
    // is to check the answer against the tangents rather than against a formula.
    const glm::vec3 t1 = glm::normalize(glm::vec3(1.0f, 0.4f, 0.2f));
    const glm::vec3 t2 = glm::normalize(glm::vec3(-0.3f, 0.2f, 1.0f));
    const glm::vec3 worldNormal = glm::normalize(glm::cross(t1, t2));

    const glm::vec3 localNormal = ctx.normalToLocal(worldNormal);

    // The tangents are directions: they go through the linear part of the inverse.
    const glm::mat3 linear(ctx.localFromWorld);
    const glm::vec3 localT1 = linear * t1;
    const glm::vec3 localT2 = linear * t2;

    // A normal is perpendicular to the surface in *both* spaces. That is the definition, and it is
    // the thing that fails if the code uses the inverse.
    INFO("dot with t1 " << glm::dot(localNormal, glm::normalize(localT1)) << ", with t2 "
                        << glm::dot(localNormal, glm::normalize(localT2)));
    CHECK(glm::dot(localNormal, glm::normalize(localT1)) == Approx(0.0f).margin(1e-4));
    CHECK(glm::dot(localNormal, glm::normalize(localT2)) == Approx(0.0f).margin(1e-4));
    CHECK(glm::length(localNormal) == Approx(1.0f).margin(1e-4));

    // **The adversarial half.** The naive answer -- push the normal through the inverse like a
    // direction -- is measurably wrong here, so this fixture can tell the two apart. Without this
    // assertion the test above would pass on a uniform scale and prove nothing.
    const glm::vec3 naive = glm::normalize(linear * worldNormal);
    const float naiveError = std::abs(glm::dot(naive, glm::normalize(localT1)));
    INFO("naive inverse-transformed normal is off by " << naiveError);
    CHECK(naiveError > 0.05f);
}

TEST_CASE("a degenerate transform yields up rather than a NaN", "[motion][context]") {
    // §64: degrade predictably. A zero-scale node is authored by accident often enough -- a scale
    // parameter driven to zero by a modulation -- and a NaN normal propagates into a pose and then
    // into the palette, where it is a character that vanishes rather than an error anybody can read.
    scene::MotionContext ctx;
    ctx.worldFromLocal = glm::mat4(0.0f);
    ctx.localFromWorld = glm::mat4(0.0f);
    const glm::vec3 n = ctx.normalToLocal(glm::vec3(0.0f, 1.0f, 0.0f));
    CHECK(std::isfinite(n.x));
    CHECK(std::isfinite(n.y));
    CHECK(std::isfinite(n.z));
    CHECK(n.y == Approx(1.0f).margin(1e-6));
}

TEST_CASE("the mode names round-trip", "[motion][context]") {
    CHECK(std::string(scene::locomotionModeName(scene::LocomotionMode::Idle)) == "idle");
    CHECK(std::string(scene::locomotionModeName(scene::LocomotionMode::Walk)) == "walk");
    CHECK(std::string(scene::locomotionModeName(scene::LocomotionMode::Run)) == "run");
    CHECK(std::string(scene::locomotionModeName(scene::LocomotionMode::Turn)) == "turn");
    CHECK(std::string(scene::locomotionModeName(scene::LocomotionMode::Other)) == "other");
}
