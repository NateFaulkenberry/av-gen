#pragma once

// Phase C §25: a desired trajectory predicted from a `MotionRequest` alone.
//
// "At runtime, generate a desired trajectory from MotionRequest. Potential inputs: current
// velocity, desired velocity, desired facing, turn rate, acceleration limits. The prediction should
// be lightweight. **Do not require navigation or behavior systems.** Phase D will eventually
// provide richer future intent."
//
// This repository already has `entity::sampleTrajectory`, and it is not this: it takes a span of
// **waypoints**, which is a navigation product. §25 is explicit that the runtime prediction must
// not require one, because the matcher needs a trajectory on every frame for every character and
// most of them are not following a route. So this is a second, smaller thing, and the audit that
// found the difference is ADR-606's rule again -- "a trajectory predictor exists" was true and
// answered a neighbouring question.
//
// **It is the same integrator the controller uses.** `stepMotion` is stepped forward at a fixed
// tick, which is what makes the prediction agree with what the body will actually do rather than
// being a second opinion about it -- a prediction that disagrees with the controller trains the
// matcher on motion the character cannot produce. The cost is one `stepMotion` per tick of
// lookahead, which at 0.6 s and a 30 Hz prediction tick is eighteen calls: lightweight, and
// measured rather than asserted in the test.

#include "entity/motion_controller.hpp"
#include "entity/motion_provider.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace avgen::entity {

struct TrajectoryPoint {
    float seconds = 0.0f;
    glm::vec3 position{0.0f}; // relative to the body's position now, in the body's own frame
    glm::vec3 velocity{0.0f};
    glm::vec3 facing{0.0f, 0.0f, 1.0f};
};

struct TrajectoryPrediction {
    std::vector<TrajectoryPoint> points;
    // The state the body would be in at the last horizon, so a caller can continue the prediction
    // rather than restart it.
    MotionState endState;
    std::uint32_t steps = 0; // `stepMotion` calls made; the cost, reported rather than guessed
};

// `horizons` are seconds into the future, ascending -- {0.2, 0.4, 0.6} is what §24's leave-one-out
// retrieval experiment selected on the real corpus, and it is the caller's choice rather than a
// constant here because §8 requires feature configuration to be data-driven.
//
// `tick` is the integration step. Smaller is more faithful to `stepMotion`'s rate limits and costs
// proportionally more; 1/30 s matches the database's sample rate.
[[nodiscard]] TrajectoryPrediction predictTrajectory(const MotionRequest& request,
                                                     const MotionState& state,
                                                     const MotionLimits& limits,
                                                     std::span<const float> horizons,
                                                     float tick = 1.0f / 30.0f);

} // namespace avgen::entity
