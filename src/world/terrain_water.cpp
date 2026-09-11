#include "world/terrain_water.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace avgen::world {
namespace {

// Where p projects onto a polyline: the segment index, the parameter along it, the distance, and
// the arc length reached. One walk answers every question `WaterCourse` asks, which is why they all
// go through it rather than each doing its own projection.
struct Projection {
    std::size_t segment = 0;
    float t = 0.0f;
    float distance = 0.0f;
    float arc = 0.0f;      // metres from the head to the projected point
    float total = 0.0f;    // metres of the whole polyline
};

Projection project(const std::vector<glm::vec3>& line, glm::vec2 p) {
    Projection best;
    best.distance = std::numeric_limits<float>::max();
    if (line.empty()) {
        return best;
    }
    if (line.size() == 1) {
        best.distance = glm::distance(p, glm::vec2(line[0].x, line[0].z));
        return best;
    }
    float arc = 0.0f;
    for (std::size_t i = 0; i + 1 < line.size(); ++i) {
        const glm::vec2 a(line[i].x, line[i].z);
        const glm::vec2 b(line[i + 1].x, line[i + 1].z);
        const glm::vec2 ab = b - a;
        const float len2 = glm::dot(ab, ab);
        const float len = std::sqrt(len2);
        const float t = len2 > 1e-12f ? glm::clamp(glm::dot(p - a, ab) / len2, 0.0f, 1.0f) : 0.0f;
        const float d = glm::distance(p, a + ab * t);
        if (d < best.distance) {
            best.distance = d;
            best.segment = i;
            best.t = t;
            best.arc = arc + len * t;
        }
        arc += len;
    }
    best.total = arc;
    return best;
}

float polylineLength(const std::vector<glm::vec3>& line) {
    float total = 0.0f;
    for (std::size_t i = 0; i + 1 < line.size(); ++i) {
        total += glm::distance(glm::vec2(line[i].x, line[i].z), glm::vec2(line[i + 1].x, line[i + 1].z));
    }
    return total;
}

} // namespace

const char* waterKindName(WaterKind kind) {
    switch (kind) {
    case WaterKind::River: return "river";
    case WaterKind::Pond: return "pond";
    case WaterKind::Lake: return "lake";
    case WaterKind::Sea: return "sea";
    }
    return "river";
}

glm::vec2 WaterCourse::flowAt(glm::vec2 p) const {
    if (kind != WaterKind::River || centreline.size() < 2) {
        return glm::vec2(0.0f);   // still water has no direction; §12 gives it ripples instead
    }
    const Projection hit = project(centreline, p);
    const glm::vec2 a(centreline[hit.segment].x, centreline[hit.segment].z);
    const glm::vec2 b(centreline[hit.segment + 1].x, centreline[hit.segment + 1].z);
    const glm::vec2 d = b - a;
    const float len = glm::length(d);
    return len > 1e-6f ? d / len : glm::vec2(0.0f);
}

float WaterCourse::surfaceAt(glm::vec2 p) const {
    if (centreline.empty()) {
        return 0.0f;
    }
    if (centreline.size() == 1) {
        return centreline[0].y;
    }
    const Projection hit = project(centreline, p);
    return glm::mix(centreline[hit.segment].y, centreline[hit.segment + 1].y, hit.t);
}

float WaterCourse::flowSpeed() const {
    if (kind != WaterKind::River || length <= 1e-3f) {
        return 0.0f;
    }
    // Gradient to speed, linearly and gently. A 1% grade drifts, a 6% grade runs; the cap is there
    // because a generated headwater can be steep for a few metres and a river that sprints down it
    // reads as a flume. These are appearance numbers, not hydraulics -- §12 asks for a logical
    // downstream direction and a plausible speed, not for Manning's equation.
    const float grade = std::max(descent, 0.0f) / length;
    return glm::clamp(grade * 12.0f, 0.05f, 1.6f);
}

bool WaterCourse::contains(glm::vec2 p) const {
    if (kind == WaterKind::Sea) {
        return true;   // the sea is everywhere the ground is below its level; depth decides
    }
    if (centreline.empty()) {
        return false;
    }
    return project(centreline, p).distance <= halfWidth;
}

float WaterCourse::alongAt(glm::vec2 p) const {
    if (centreline.size() < 2) {
        return 0.0f;
    }
    const Projection hit = project(centreline, p);
    const float total = hit.total > 1e-3f ? hit.total : length;
    return total > 1e-3f ? glm::clamp(hit.arc / total, 0.0f, 1.0f) : 0.0f;
}

std::vector<WaterCourse> waterCourses(const WorldMap& map) {
    std::vector<WaterCourse> out;
    for (std::size_t i = 0; i < map.features.size(); ++i) {
        const Feature& f = map.features[i];
        const bool holdsWater = f.kind == FeatureKind::River || (f.kind == FeatureKind::Flat && f.water);
        if (!holdsWater) {
            continue;
        }
        WaterCourse c;
        c.name = f.name;
        c.feature = static_cast<int>(i);
        c.halfWidth = f.width;
        // The smoothed curve, not the authored polyline: it is what the height function samples, so
        // a course described from the raw path would disagree with the channel that exists.
        c.centreline = f.samplePath();
        // A river's path level *is* its water surface (see WorldMap::height); a still body's surface
        // is its level plus the depth it holds above the bed.
        if (f.kind == FeatureKind::River) {
            c.kind = WaterKind::River;
            c.depth = f.amplitude;
        } else {
            c.kind = c.centreline.size() > 1 ? WaterKind::Lake : WaterKind::Pond;
            c.depth = std::max(f.waterDepth, 0.1f);
            for (glm::vec3& q : c.centreline) {
                q.y += f.waterDepth;
            }
        }
        c.length = polylineLength(c.centreline);
        if (c.centreline.size() >= 2) {
            c.descent = c.centreline.front().y - c.centreline.back().y;
        }
        // A course whose nodes rise downstream is an authoring mistake, not a fact about water, and
        // silently reporting a negative descent would have the renderer run the ripples backwards.
        c.descent = std::max(c.descent, 0.0f);
        out.push_back(std::move(c));
    }
    if (map.seaLevel > -1000.0f) {
        WaterCourse sea;
        sea.name = "sea";
        sea.kind = WaterKind::Sea;
        sea.halfWidth = std::max(map.size.x, map.size.y);
        sea.depth = std::max(map.seaLevel - map.sampledMinHeight, 0.1f);
        sea.centreline = {glm::vec3(0.0f, map.seaLevel, 0.0f)};
        out.push_back(std::move(sea));
    }
    return out;
}

const WaterCourse* nearestCourse(const std::vector<WaterCourse>& courses, glm::vec2 p) {
    const WaterCourse* best = nullptr;
    float bestDistance = std::numeric_limits<float>::max();
    for (const WaterCourse& c : courses) {
        if (c.centreline.empty()) {
            continue;
        }
        const float d = c.kind == WaterKind::Sea ? std::numeric_limits<float>::max() * 0.5f
                                                 : project(c.centreline, p).distance;
        if (d < bestDistance) {
            bestDistance = d;
            best = &c;
        }
    }
    return best;
}

} // namespace avgen::world
