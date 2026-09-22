#include "scene/ground_query.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::scene {

const char* groundCategoryName(GroundCategory category) {
    switch (category) {
    case GroundCategory::Unknown: return "unknown";
    case GroundCategory::Terrain: return "terrain";
    case GroundCategory::Water: return "water";
    case GroundCategory::Authored: return "authored";
    }
    return "?";
}

GroundSample PlaneGroundQuery::sampleAt(const glm::vec3& worldPoint) const {
    GroundSample out;
    const float len = glm::length(normal_);
    if (len < 1e-6f) {
        return out; // a degenerate plane has no answer, and says so rather than inventing one
    }
    out.normal = normal_ / len;
    // Where the vertical line through `worldPoint` meets the plane. Vertical rather than along the
    // normal, for the reason `plantOnPlane` gives: a foot is over the ground it is over, and
    // sliding down the normal moves it sideways across the terrain.
    const float denom = out.normal.y;
    if (std::fabs(denom) < 1e-6f) {
        return out; // a vertical plane is not ground under anything
    }
    const float t = glm::dot(point_ - worldPoint, out.normal) / denom;
    out.point = glm::vec3(worldPoint.x, worldPoint.y + t, worldPoint.z);
    out.distance = worldPoint.y - out.point.y;
    out.category = GroundCategory::Authored;
    out.valid = true;
    return out;
}


EnvironmentSample sampleEnvironment(const IGroundQuery& ground, const glm::vec3& centre,
                                    float radius) {
    EnvironmentSample out;
    const float r = std::max(radius, 1e-3f);
    // Centre plus the four axes. The centre is what the category comes from -- the thing the body
    // is standing on -- and the ring is what the slope comes from.
    const glm::vec3 offsets[5] = {
        glm::vec3(0.0f), glm::vec3(r, 0.0f, 0.0f), glm::vec3(-r, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, r), glm::vec3(0.0f, 0.0f, -r),
    };
    float heights[5] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    bool got[5] = {false, false, false, false, false};
    int valid = 0;
    float lowest = 0.0f;
    float highest = 0.0f;
    for (int i = 0; i < 5; ++i) {
        const GroundSample s = ground.sampleAt(centre + offsets[i]);
        if (!s.valid) {
            continue;
        }
        heights[i] = s.point.y;
        got[i] = true;
        if (valid == 0) {
            lowest = highest = s.point.y;
            out.category = s.category;
        } else if (i == 0) {
            out.category = s.category;
        }
        lowest = std::min(lowest, s.point.y);
        highest = std::max(highest, s.point.y);
        ++valid;
    }
    out.coverage = static_cast<float>(valid) / 5.0f;
    if (valid < 3) {
        // Too little to fit anything. **Zero slope is not the answer here** -- it is the answer for
        // flat ground, and reporting it for "no data" is the conflation ADR-551 already had to fix
        // once when a terrain reject was read as "no ground".
        return out;
    }
    out.relief = highest - lowest;

    // The gradient from the opposed pairs, which is a two-point central difference per axis and
    // needs no plane fit. A missing sample falls back to the centre, so a body on the lip of a
    // height field gets a one-sided estimate rather than a wrong two-sided one.
    const float east = got[1] ? heights[1] : heights[0];
    const float west = got[2] ? heights[2] : heights[0];
    const float north = got[3] ? heights[3] : heights[0];
    const float south = got[4] ? heights[4] : heights[0];
    const float dx = (east - west) / (2.0f * r);
    const float dz = (north - south) / (2.0f * r);
    const float grade = std::sqrt((dx * dx) + (dz * dz));
    out.slope = std::atan(grade);
    if (grade > 1e-4f) {
        // Downhill is the negative gradient.
        out.downhill = glm::normalize(glm::vec3(-dx, 0.0f, -dz));
    }
    return out;
}

} // namespace avgen::scene
