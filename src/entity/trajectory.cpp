#include "entity/trajectory.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::entity {
namespace {

glm::vec3 flat(const glm::vec3& v) { return glm::vec3(v.x, 0.0f, v.z); }

float flatLength(const glm::vec3& v) { return std::sqrt((v.x * v.x) + (v.z * v.z)); }

glm::vec3 flatUnit(const glm::vec3& v) {
    const float len = flatLength(v);
    return len > 1e-5f ? glm::vec3(v.x / len, 0.0f, v.z / len) : glm::vec3(0.0f);
}

} // namespace

TrajectorySample sampleTrajectory(const TrajectorySettings& settings,
                                  std::span<const glm::vec3> waypoints, const glm::vec3& position,
                                  float speed) {
    TrajectorySample out;
    if (waypoints.empty()) {
        return out;   // nowhere to go; no direction, and saying so beats inventing one
    }

    out.direction = flatUnit(waypoints.front() - position);
    if (flatLength(out.direction) < 1e-5f && waypoints.size() > 1) {
        // Standing on the first waypoint: the direction is the next leg's.
        out.direction = flatUnit(waypoints[1] - waypoints[0]);
    }

    // Walk the polyline forward by the distance the body will cover in `lookaheadSeconds`. The
    // distance is a function of the *current* pace, so a running body looks further than a walking
    // one without anybody authoring a second number.
    const float reach = std::max(speed, 0.0f) * std::max(settings.lookaheadSeconds, 0.0f);
    if (reach <= 1e-5f) {
        return out;   // standing still: there is no "shortly" to look at
    }

    float remaining = reach;
    glm::vec3 cursor = position;
    glm::vec3 tangent(0.0f);
    for (std::size_t i = 0; i < waypoints.size(); ++i) {
        const glm::vec3 leg = flat(waypoints[i] - cursor);
        const float legLength = flatLength(leg);
        if (legLength < 1e-5f) {
            continue;
        }
        if (remaining <= legLength) {
            // The lookahead point falls on this leg, so the direction the body will be heading is
            // this leg's. **The tangent of the path, not the bearing to the point**: a bearing
            // from the body to a point around a corner cuts across the corner, which is precisely
            // the over-eager anticipation this file's header warns about.
            tangent = flatUnit(leg);
            remaining = 0.0f;
            break;
        }
        remaining -= legLength;
        cursor = waypoints[i];
        tangent = flatUnit(leg);
    }
    if (remaining > 0.0f) {
        // The lookahead ran off the end of the path. The body is arriving, not cornering, and it
        // has no future direction -- which is not the same as "carry straight on".
        return out;
    }

    if (flatLength(tangent) < 1e-5f || flatLength(out.direction) < 1e-5f) {
        return out;
    }
    const float dot = std::clamp(glm::dot(out.direction, tangent), -1.0f, 1.0f);
    out.cornerAngle = std::acos(dot);
    if (out.cornerAngle < glm::radians(std::max(settings.minCornerDegrees, 0.0f))) {
        // Too gentle to anticipate. Suppressed rather than applied weakly, so a body following a
        // slightly wobbly path does not weave along it chasing its own lookahead.
        return out;
    }
    out.futureDirection = tangent;
    out.seconds = settings.lookaheadSeconds;
    out.hasFuture = true;
    return out;
}

} // namespace avgen::entity
