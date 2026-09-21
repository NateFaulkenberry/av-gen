#include "entity/trajectory_prediction.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::entity {

TrajectoryPrediction predictTrajectory(const MotionRequest& request, const MotionState& state,
                                       const MotionLimits& limits, std::span<const float> horizons,
                                       float tick) {
    TrajectoryPrediction out;
    out.endState = state;
    if (horizons.empty()) {
        return out;
    }
    const float step = std::max(tick, 1e-4f);
    const float last = *std::max_element(horizons.begin(), horizons.end());
    if (!(last > 0.0f)) {
        for (const float t : horizons) {
            out.points.push_back({t, glm::vec3(0.0f), state.velocity, state.facing});
        }
        return out;
    }

    MotionState rolling = state;
    glm::vec3 position{0.0f};
    float elapsed = 0.0f;
    std::size_t next = 0;

    // Integrate forward with the **controller's own** limits. Anything else -- a straight line at
    // the desired velocity, say -- predicts a future the body cannot reach, and the matcher would
    // then be asked for motion that does not exist.
    while (elapsed < last && next < horizons.size()) {
        const MotionSolution solution = stepMotion(request, rolling, limits, step, rolling);
        position += solution.velocity * step;
        elapsed += step;
        ++out.steps;
        // Emit every horizon this step has reached. A loop rather than an index because two
        // horizons can fall inside one tick when the tick is coarse, and dropping one silently
        // would give the feature vector a zero where a position belongs.
        while (next < horizons.size() && horizons[next] <= elapsed + 1e-6f) {
            out.points.push_back({horizons[next], position, solution.velocity, solution.facing});
            ++next;
        }
    }
    // Horizons beyond the integrated span (a caller asking past `last`, or a zero-length tick)
    // extrapolate from the final state rather than being dropped, for the same reason.
    while (next < horizons.size()) {
        const float extra = horizons[next] - elapsed;
        out.points.push_back({horizons[next], position + (rolling.velocity * extra),
                              rolling.velocity, rolling.facing});
        ++next;
    }
    out.endState = rolling;
    return out;
}

} // namespace avgen::entity
