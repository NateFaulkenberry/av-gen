// Phase C §25 -- a desired trajectory predicted from a MotionRequest alone.
//
// "At runtime, generate a desired trajectory from MotionRequest… **Do not require navigation or
// behavior systems.**"
//
// The audit finding first, because it is why this file exists: `entity::sampleTrajectory` already
// existed and looked like §25. It takes a span of **waypoints**, which is a navigation product, and
// §25 forbids requiring one -- the matcher needs a trajectory every frame for every character and
// most of them are not following a route. "A trajectory predictor exists" was true and answered a
// neighbouring question (ADR-606).
//
// The property that matters is not that the prediction is smooth or plausible. It is that it
// **agrees with what the body will actually do**: a prediction that disagrees with the controller
// trains the matcher on motion the character cannot produce, and the mismatch would show up as a
// character that consistently selects clips it then fails to follow.

#include "entity/trajectory_prediction.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <chrono>
#include <limits>
#include <vector>

using namespace avgen;
using Catch::Approx;

namespace {
const std::vector<float> kHorizons = {0.2f, 0.4f, 0.6f}; // what §24's experiment selected
}

TEST_CASE("the prediction is what the controller will actually do", "[trajectory][phaseC]") {
    // **The arm that can fail.** Predict 0.6 s ahead, then step the controller 0.6 s for real and
    // compare. They must agree to floating point, because they are the same integrator -- and if
    // someone later "optimises" the prediction into a closed-form straight line, this is what says
    // the two have diverged.
    entity::MotionRequest request;
    request.desiredVelocity = glm::vec3(0.8f, 0.0f, 1.4f);
    request.desiredFacing = glm::normalize(glm::vec3(0.8f, 0.0f, 1.4f));
    entity::MotionLimits limits;
    entity::MotionState start;
    start.velocity = glm::vec3(0.0f, 0.0f, 0.2f);
    start.facing = glm::vec3(0.0f, 0.0f, 1.0f);
    start.started = true;

    const entity::TrajectoryPrediction predicted =
        entity::predictTrajectory(request, start, limits, kHorizons);
    REQUIRE(predicted.points.size() == kHorizons.size());

    entity::MotionState rolling = start;
    glm::vec3 position{0.0f};
    const float tick = 1.0f / 30.0f;
    const int steps = static_cast<int>(0.6f / tick + 0.5f);
    for (int i = 0; i < steps; ++i) {
        const entity::MotionSolution s = entity::stepMotion(request, rolling, limits, tick, rolling);
        position += s.velocity * tick;
    }

    const glm::vec3 end = predicted.points.back().position;
    WARN(fmt::format("predicted 0.6 s -> ({:.4f}, {:.4f}); stepped -> ({:.4f}, {:.4f}); {} steps",
                     end.x, end.z, position.x, position.z, predicted.steps));
    CHECK(end.x == Approx(position.x).margin(1e-5f));
    CHECK(end.z == Approx(position.z).margin(1e-5f));
    CHECK(glm::length(predicted.endState.velocity - rolling.velocity) < 1e-5f);

    // It actually went somewhere, so the agreement above is not two integrators both doing nothing
    // (ADR-611: the companion counts that something happened).
    CHECK(glm::length(end) > 0.1f);
    CHECK(predicted.steps == static_cast<std::uint32_t>(steps));
}

TEST_CASE("the prediction respects the limits rather than the request", "[trajectory][phaseC]") {
    // A straight line at the desired velocity is the obvious cheap prediction and it is wrong: it
    // predicts a future the body cannot reach. At 6 m/s^2 a body cannot be at 4 m/s after 0.2 s
    // from rest, and a matcher asked for that motion would select a run the character then fails
    // to produce.
    entity::MotionRequest request;
    request.desiredVelocity = glm::vec3(0.0f, 0.0f, 4.0f);
    request.desiredFacing = glm::vec3(0.0f, 0.0f, 1.0f);
    entity::MotionLimits limits;
    entity::MotionState rest;
    rest.started = true;

    const entity::TrajectoryPrediction predicted =
        entity::predictTrajectory(request, rest, limits, kHorizons);
    REQUIRE(predicted.points.size() == 3u);

    const float naive = 4.0f * 0.2f; // where a straight-line prediction would put it
    const float actual = predicted.points.front().position.z;
    WARN(fmt::format("at 0.2 s: limited prediction {:.4f} m, naive straight line {:.4f} m", actual,
                     naive));
    CHECK(actual < naive);
    CHECK(actual > 0.0f); // it did accelerate; this is not a prediction that the body stays put
    // And the speed at 0.2 s is bounded by the acceleration limit, which is the thing being
    // respected: 6 m/s^2 for 0.2 s is 1.2 m/s, and it cannot exceed that however hard it is asked.
    CHECK(glm::length(predicted.points.front().velocity) <= limits.maxAcceleration * 0.2f + 1e-3f);
}

TEST_CASE("every horizon gets a point, even two inside one tick", "[trajectory][phaseC]") {
    // A coarse tick can step past two horizons at once. Dropping one would leave a zero where a
    // position belongs in the feature vector -- a silent hole in the thing the matcher searches on.
    entity::MotionRequest request;
    request.desiredVelocity = glm::vec3(0.0f, 0.0f, 1.0f);
    entity::MotionLimits limits;
    entity::MotionState rest;
    rest.started = true;
    const std::vector<float> tight = {0.05f, 0.07f, 0.4f};
    const entity::TrajectoryPrediction predicted =
        entity::predictTrajectory(request, rest, limits, tight, 0.1f); // one tick spans two
    REQUIRE(predicted.points.size() == tight.size());
    for (std::size_t i = 0; i < tight.size(); ++i) {
        CHECK(predicted.points[i].seconds == Approx(tight[i]));
    }
    // Monotone in distance, because time is monotone and the body does not reverse here.
    CHECK(predicted.points[2].position.z >= predicted.points[1].position.z);

    // Degenerate inputs answer rather than crash or lie.
    CHECK(entity::predictTrajectory(request, rest, limits, {}).points.empty());
    const std::vector<float> zero = {0.0f};
    CHECK(entity::predictTrajectory(request, rest, limits, zero).points.size() == 1u);
}

TEST_CASE("the prediction is lightweight", "[trajectory][phaseC]") {
    // §25 says "the prediction should be lightweight", which is a claim with a number behind it or
    // it is nothing. Every character runs this every frame.
    entity::MotionRequest request;
    request.desiredVelocity = glm::vec3(0.6f, 0.0f, 1.2f);
    entity::MotionLimits limits;
    entity::MotionState state;
    state.started = true;
    double best = std::numeric_limits<double>::max();
    for (int r = 0; r < 5; ++r) {
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 20000; ++i) {
            const entity::TrajectoryPrediction p =
                entity::predictTrajectory(request, state, limits, kHorizons);
            (void)p;
        }
        const auto t1 = std::chrono::steady_clock::now();
        best = std::min(best,
                        std::chrono::duration_cast<std::chrono::duration<double, std::micro>>(t1 - t0)
                                .count() /
                            20000.0);
    }
    WARN(fmt::format("predictTrajectory: {:.3f} us per call ({:.0f} characters per 60 Hz frame)",
                     best, 16666.0 / best));
    CHECK(best > 0.0);
    // A hundred characters is §47's scale; this must not be a meaningful fraction of their budget.
    CHECK(best * 100.0 < 1000.0); // under 1 ms for a hundred characters
}
