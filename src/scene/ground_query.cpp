#include "scene/ground_query.hpp"

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

} // namespace avgen::scene
