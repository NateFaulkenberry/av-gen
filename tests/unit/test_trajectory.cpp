// Curved trajectories (Phase B §38).
//
// **Two measurements, because one of them only ever improves.** Turn onset -- how many frames
// before a corner the body begins to come round -- gets better monotonically as the lookahead
// grows, so optimising it alone drives the setting to infinity and produces a character that
// anticipates corners the viewer cannot see yet. The measurement that catches that is **path
// deviation**: how far off the intended line the body travels while anticipating. Both are here,
// and the second is why `anticipation` is a dial.

#include "entity/motion_controller.hpp"
#include "entity/trajectory.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <span>
#include <vector>

using namespace avgen;
using Catch::Approx;

namespace {

constexpr float kDt = 1.0f / 60.0f;

// An L-shaped path: ten metres north, then ten metres east. The corner is a right angle at (0,0,10).
std::vector<glm::vec3> elbow() {
    return {glm::vec3(0.0f, 0.0f, 10.0f), glm::vec3(10.0f, 0.0f, 10.0f)};
}

struct Run {
    float onsetMetresBeforeCorner = 0.0f;  // how far back the heading began to change
    float worstDeviation = 0.0f;           // furthest from the intended polyline
    glm::vec3 finalHeading{0.0f};
};

// Drive a body along the elbow and report both numbers.
Run travel(float anticipation, float lookahead) {
    entity::TrajectorySettings ts;
    ts.lookaheadSeconds = lookahead;
    ts.anticipation = anticipation;
    entity::MotionLimits limits;
    limits.anticipation = anticipation;
    limits.maxTurnRate = 2.5f;
    limits.maxAcceleration = 8.0f;

    entity::MotionState state;
    state.velocity = glm::vec3(0.0f, 0.0f, 2.0f);
    state.facing = glm::vec3(0.0f, 0.0f, 1.0f);
    state.started = true;
    glm::vec3 position(0.0f, 0.0f, 0.0f);
    const glm::vec3 corner(0.0f, 0.0f, 10.0f);

    const std::vector<glm::vec3> path = elbow();
    std::size_t leg = 0;
    Run out;
    bool turning = false;
    for (int i = 0; i < 600; ++i) {
        // Advance past a waypoint once it is reached, which is what a navigator hands over. The
        // first version of this was a two-condition erase loop that never fired, so the body chased
        // a waypoint it had already passed and never rounded the corner -- the harness failing,
        // not the code.
        if (leg + 1 < path.size() && glm::length(path[leg] - position) < 0.35f) {
            ++leg;
        }
        const std::span<const glm::vec3> ahead(path.data() + leg, path.size() - leg);
        const float speed = std::sqrt((state.velocity.x * state.velocity.x) +
                                      (state.velocity.z * state.velocity.z));
        const entity::TrajectorySample sample =
            entity::sampleTrajectory(ts, ahead, position, speed);
        if (glm::length(sample.direction) < 1e-4f) {
            break;
        }

        entity::MotionRequest request;
        request.desiredVelocity = sample.direction * 2.0f;
        request.desiredFacing = sample.direction;
        if (sample.hasFuture) {
            request.futureDirection = sample.futureDirection;
            request.futureSeconds = sample.seconds;
        }
        entity::MotionState next;
        const entity::MotionSolution s = entity::stepMotion(request, state, limits, kDt, next);
        state = next;
        position += s.velocity * kDt;

        // Onset: the first frame the heading has turned a measurable amount off north, recorded as
        // the distance still to run to the corner.
        if (!turning && std::abs(state.velocity.x) > 0.05f) {
            turning = true;
            out.onsetMetresBeforeCorner = std::max(corner.z - position.z, 0.0f);
        }
        // Deviation from the intended L. Before the corner the line is x=0; after it, z=10.
        const float dev = position.z < 10.0f ? std::abs(position.x)
                                             : std::abs(position.z - 10.0f);
        out.worstDeviation = std::max(out.worstDeviation, dev);
        if (position.x > 9.0f) {
            break;
        }
    }
    out.finalHeading = glm::normalize(glm::vec3(state.velocity.x, 0.0f, state.velocity.z));
    return out;
}

} // namespace

TEST_CASE("the sampler reports where the body will be heading, not where the corner is",
          "[trajectory][phaseB]") {
    // **The tangent of the path, not the bearing to the lookahead point.** A bearing from the body
    // to a point around a corner points diagonally across the corner, which is exactly the
    // over-eager anticipation this stage is trying not to produce.
    entity::TrajectorySettings s;
    // **1.5 s at 2 m/s is 3 m of reach against 2 m to the corner.** The first version used 1.0 s,
    // which reaches *exactly* to the corner and therefore never past it -- so the tangent it found
    // was still the leg the body is on and the corner read as zero degrees. A lookahead that lands
    // on the boundary measures the boundary (testing.md #24).
    s.lookaheadSeconds = 1.5f;
    const std::vector<glm::vec3> path = elbow();
    const entity::TrajectorySample sample =
        entity::sampleTrajectory(s, path, glm::vec3(0.0f, 0.0f, 8.0f), 2.0f);
    REQUIRE(sample.hasFuture);
    INFO("direction " << sample.direction.x << "," << sample.direction.z << "  future "
                      << sample.futureDirection.x << "," << sample.futureDirection.z);
    // Now: due north. Shortly: due east. Neither is the diagonal a bearing would give.
    CHECK(sample.direction.z == Approx(1.0f).margin(1e-3));
    CHECK(sample.futureDirection.x == Approx(1.0f).margin(1e-3));
    CHECK(std::abs(sample.futureDirection.z) < 1e-3f);
    CHECK(sample.cornerAngle == Approx(glm::radians(90.0f)).margin(1e-3));
}

TEST_CASE("a gentle bend is not anticipated", "[trajectory][phaseB]") {
    // Suppressed rather than applied weakly, so a body following a slightly wobbly path does not
    // weave along it chasing its own lookahead.
    entity::TrajectorySettings s;
    s.lookaheadSeconds = 1.0f;
    s.lookaheadSeconds = 1.5f;
    const std::vector<glm::vec3> gentle = {glm::vec3(0.0f, 0.0f, 10.0f),
                                           glm::vec3(0.5f, 0.0f, 20.0f)};   // ~3 degrees
    const entity::TrajectorySample sample =
        entity::sampleTrajectory(s, gentle, glm::vec3(0.0f, 0.0f, 8.0f), 2.0f);
    INFO("corner " << glm::degrees(sample.cornerAngle) << " degrees");
    CHECK_FALSE(sample.hasFuture);
    CHECK(glm::length(sample.futureDirection) < 1e-5f);
}

TEST_CASE("a path that ends has no future direction, which is not 'straight on'",
          "[trajectory][phaseB]") {
    entity::TrajectorySettings s;
    s.lookaheadSeconds = 2.0f;                     // reaches past the end
    const std::vector<glm::vec3> one = {glm::vec3(0.0f, 0.0f, 1.0f)};
    const entity::TrajectorySample sample =
        entity::sampleTrajectory(s, one, glm::vec3(0.0f), 2.0f);
    CHECK_FALSE(sample.hasFuture);
    CHECK(glm::length(sample.direction) > 0.9f);   // it still knows where it is going now
}

TEST_CASE("lookahead starts the turn earlier, measured in metres before the corner",
          "[trajectory][phaseB][onset]") {
    // The first of the two numbers. The baseline has no anticipation at all, so the improvement is
    // a measurement of the lookahead rather than of the path being easy (ADR-182).
    const Run none = travel(0.0f, 0.45f);
    const Run some = travel(0.5f, 0.45f);
    INFO("onset without lookahead " << none.onsetMetresBeforeCorner << " m, with " 
                                    << some.onsetMetresBeforeCorner << " m");
    CHECK(some.onsetMetresBeforeCorner > none.onsetMetresBeforeCorner + 0.15f);
    // Both still get round the corner.
    CHECK(some.finalHeading.x > 0.9f);
    CHECK(none.finalHeading.x > 0.9f);
}

TEST_CASE("over-eager lookahead shows in path deviation, which onset cannot see",
          "[trajectory][phaseB][deviation]") {
    // **The measurement that stops the first one running away.** Onset improves monotonically with
    // anticipation, so optimising it alone drives the dial to 1 and cuts the corner. Deviation is
    // what that costs, and it is the number a viewer actually reads as "it turned too early".
    const Run mild = travel(0.35f, 0.45f);
    const Run eager = travel(1.0f, 1.2f);
    INFO("mild: onset " << mild.onsetMetresBeforeCorner << " m, worst deviation "
                        << mild.worstDeviation << " m");
    INFO("eager: onset " << eager.onsetMetresBeforeCorner << " m, worst deviation "
                         << eager.worstDeviation << " m");
    // The eager one does turn earlier -- onset alone would call it better...
    CHECK(eager.onsetMetresBeforeCorner > mild.onsetMetresBeforeCorner);
    // ...and it cuts the corner by measurably more, which is the failure onset cannot see.
    CHECK(eager.worstDeviation > mild.worstDeviation + 0.2f);
    // The default stays inside a corner-cut a viewer would not remark on.
    CHECK(mild.worstDeviation < 1.2f);
}

TEST_CASE("the sampler keeps nothing", "[trajectory][phaseB][determinism]") {
    entity::TrajectorySettings s;
    const std::vector<glm::vec3> path = elbow();
    const entity::TrajectorySample a =
        entity::sampleTrajectory(s, path, glm::vec3(0.0f, 0.0f, 8.0f), 2.0f);
    (void)entity::sampleTrajectory(s, path, glm::vec3(5.0f, 0.0f, 2.0f), 9.0f);
    const entity::TrajectorySample b =
        entity::sampleTrajectory(s, path, glm::vec3(0.0f, 0.0f, 8.0f), 2.0f);
    CHECK(a.direction == b.direction);
    CHECK(a.futureDirection == b.futureDirection);
    CHECK(a.cornerAngle == Approx(b.cornerAngle));
}
