#include "world/water.hpp"

#include "core/noise.hpp"
#include "scene/struct_hash.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace avgen::world {
namespace {

using json = nlohmann::json;
using scene::detail::StructHash;

constexpr float kEps = 1e-5f;

glm::vec2 xz(const glm::vec3& v) { return {v.x, v.z}; }

glm::vec2 safeNormalize(glm::vec2 v, glm::vec2 fallback = {0.0f, 1.0f}) {
    const float len = glm::length(v);
    return len > kEps ? v / len : fallback;
}

Result<float> readFloat(const json& j, const char* key, float def) {
    if (!j.contains(key)) {
        return def;
    }
    if (!j.at(key).is_number()) {
        return fail("water flow '{}' must be a number", key);
    }
    return j.at(key).get<float>();
}

} // namespace

Result<void> WaterFlowSettings::validate() const {
    if (!(speedScale >= 0.0f) || speedScale > 100.0f) {
        return fail("water flow: speedScale must be in [0, 100]");
    }
    if (!(speedOverride >= 0.0f) || speedOverride > 100.0f) {
        return fail("water flow: speedOverride must be in [0, 100] m/s (0 = use the course)");
    }
    if (bankShear < 0.0f || bankShear > 1.0f) {
        return fail("water flow: bankShear must be in [0, 1]");
    }
    if (meander < 0.0f || meander > 1.5f) {
        return fail("water flow: meander must be in [0, 1.5] radians");
    }
    if (stillFactor < 0.0f || stillFactor > 1.0f) {
        return fail("water flow: stillFactor must be in [0, 1]");
    }
    if (!(stillSpeed >= 0.0f) || stillSpeed > 100.0f) {
        return fail("water flow: stillSpeed must be in [0, 100] m/s");
    }
    return {};
}

std::uint64_t WaterFlowSettings::structuralHash() const {
    StructHash h;
    h.f32(speedScale);
    h.f32(speedOverride);
    h.f32(bankShear);
    h.f32(meander);
    h.f32(stillFactor);
    h.f32(stillSpeed);
    h.f32(windDirection.x);
    h.f32(windDirection.y);
    return h.value();
}

// ---- WaterBody ------------------------------------------------------------------------------

float WaterBody::transitSeconds() const {
    return speed > kEps ? length() / speed : 0.0f;
}

float WaterBody::shearProfile(float r) const {
    // A parabola, not a linear ramp: that is the profile an open channel has, and the difference is
    // visible -- a linear ramp puts the whole of midstream at one speed and reads as a belt.
    return glm::clamp(bankShear * r * r, 0.0f, 1.0f);
}

glm::vec3 WaterBody::pointAt(float along01) const {
    const std::vector<glm::vec3>& line = centre();
    if (line.empty()) {
        return glm::vec3(0.0f);
    }
    if (line.size() == 1 || length() <= kEps || arc.size() != line.size()) {
        return line.front();
    }
    const float s = glm::clamp(along01, 0.0f, 1.0f) * arc.back();
    // The arc table is ascending, so the segment is a binary search rather than a walk: a drifting
    // object asks this every frame and a smoothed river is a few hundred nodes.
    const auto it = std::upper_bound(arc.begin(), arc.end(), s);
    const std::size_t hi = std::min<std::size_t>(static_cast<std::size_t>(it - arc.begin()), line.size() - 1);
    const std::size_t lo = hi > 0 ? hi - 1 : 0;
    const float span = arc[hi] - arc[lo];
    const float t = span > kEps ? (s - arc[lo]) / span : 0.0f;
    return glm::mix(line[lo], line[hi], t);
}

glm::vec2 WaterBody::tangentAt(float along01) const {
    const std::vector<glm::vec3>& line = centre();
    if (line.size() < 2 || arc.size() != line.size()) {
        return stillDirection;
    }
    const float s = glm::clamp(along01, 0.0f, 1.0f) * arc.back();
    const auto it = std::upper_bound(arc.begin(), arc.end(), s);
    const std::size_t hi = std::min<std::size_t>(static_cast<std::size_t>(it - arc.begin()), line.size() - 1);
    const std::size_t lo = hi > 0 ? hi - 1 : 0;
    return safeNormalize(xz(line[hi]) - xz(line[lo]), stillDirection);
}

FlowSample WaterBody::flowAt(glm::vec2 p) const {
    FlowSample out;
    const std::vector<glm::vec3>& line = centre();
    if (line.empty()) {
        return out;
    }
    // Everything topological comes from the course: which way it runs, how high its surface is here,
    // how far along this is, and whether it is between the banks. This file adds only the motion.
    out.surface = course.surfaceAt(p);
    out.inside = course.contains(p);
    out.along = course.alongAt(p);
    const glm::vec2 downstream = course.flowAt(p);

    // Distance and side. The nearest point on the polyline is not something `WaterCourse` reports,
    // so it is measured here -- the only projection this file does, and only for the two numbers
    // the course does not expose.
    float best = std::numeric_limits<float>::max();
    glm::vec2 nearest = xz(line.front());
    glm::vec2 tangent = downstream;
    for (std::size_t i = 0; i + 1 < line.size(); ++i) {
        const glm::vec2 a = xz(line[i]);
        const glm::vec2 b = xz(line[i + 1]);
        const glm::vec2 ab = b - a;
        const float len2 = glm::dot(ab, ab);
        const float t = len2 > 1e-12f ? glm::clamp(glm::dot(p - a, ab) / len2, 0.0f, 1.0f) : 0.0f;
        const glm::vec2 q = a + ab * t;
        const float d2 = glm::dot(p - q, p - q);
        if (d2 < best) {
            best = d2;
            nearest = q;
            tangent = safeNormalize(ab, downstream);
        }
    }
    if (line.size() == 1) {
        nearest = xz(line.front());
        best = glm::dot(p - nearest, p - nearest);
    }
    out.distance = std::sqrt(best);
    const glm::vec2 reference = glm::length(downstream) > kEps ? downstream : tangent;
    const glm::vec2 normal(-reference.y, reference.x);
    out.across = halfWidth() > kEps ? glm::clamp(glm::dot(p - nearest, normal) / halfWidth(), -1.0f, 1.0f)
                                    : 0.0f;

    if (!flowing) {
        // Still water: no downstream, a slow wind-driven drift instead, and only inside the body.
        out.direction = stillDirection;
        out.speed = out.inside ? speed : 0.0f;
        return out;
    }
    out.direction = downstream;
    const float r = glm::clamp(out.distance / std::max(halfWidth(), kEps), 0.0f, 1.0f);
    out.speed = speed * (1.0f - shearProfile(r));
    return out;
}

// ---- WaterBodySet ---------------------------------------------------------------------------

FlowSample WaterBodySet::flowAt(glm::vec2 p) const {
    FlowSample out;
    float bestD = std::numeric_limits<float>::max();
    for (const WaterBody& body : bodies) {
        const FlowSample s = body.flowAt(p);
        // A point between a body's banks belongs to it; otherwise the nearest bank wins.
        const float d = s.inside ? s.distance - body.halfWidth() : s.distance;
        if (d < bestD) {
            bestD = d;
            out = s;
        }
    }
    if (settings.meander > 0.0f && out.speed > kEps && glm::length(out.direction) > kEps) {
        // A slow wander off the centreline tangent, from noise in world space, so the current is a
        // current and not an arrow: neighbouring points differ a little and the same point always
        // differs the same way. No time term -- the *pattern* moves because the water moves through
        // it, which is what a river looks like.
        const float wobble =
            noise::valueNoise(glm::vec3(p.x * 0.012f, 0.0f, p.y * 0.012f), 0x5EA51DEu) * 2.0f - 1.0f;
        const float a = wobble * settings.meander;
        const float c = std::cos(a);
        const float s = std::sin(a);
        out.direction = glm::vec2(c * out.direction.x - s * out.direction.y,
                                  s * out.direction.x + c * out.direction.y);
    }
    return out;
}

const WaterBody* WaterBodySet::find(std::string_view name) const {
    for (const WaterBody& body : bodies) {
        if (body.name() == name) {
            return &body;
        }
    }
    return nullptr;
}

float WaterBodySet::fastest() const {
    float best = 0.0f;
    for (const WaterBody& body : bodies) {
        best = std::max(best, body.speed);
    }
    return best;
}

std::uint64_t WaterBodySet::structuralHash() const {
    StructHash h;
    h.u64(settings.structuralHash());
    for (const WaterBody& body : bodies) {
        h.str(body.name());
        h.u32(static_cast<std::uint32_t>(body.course.kind));
        h.f32(body.halfWidth());
        h.f32(body.length());
        h.f32(body.speed);
        h.u64(body.centre().size());
        for (const glm::vec3& p : body.centre()) {
            h.v3(p);
        }
    }
    return h.value();
}

// ---- derivation -----------------------------------------------------------------------------

WaterBodySet waterBodies(const std::vector<WaterCourse>& courses, const WaterFlowSettings& settings) {
    WaterBodySet set;
    set.settings = settings;
    const glm::vec2 wind = safeNormalize(settings.windDirection, glm::vec2(0.7071f, 0.7071f));

    for (const WaterCourse& course : courses) {
        if (course.centreline.empty()) {
            continue;
        }
        WaterBody body;
        body.course = course;
        body.bankShear = glm::clamp(settings.bankShear, 0.0f, 1.0f);
        body.arc.assign(course.centreline.size(), 0.0f);
        for (std::size_t i = 1; i < course.centreline.size(); ++i) {
            body.arc[i] = body.arc[i - 1] +
                          glm::distance(xz(course.centreline[i - 1]), xz(course.centreline[i]));
        }

        // Whether it flows is the course's own answer -- terrain already refuses to report a
        // negative descent, and a River that descends is the only kind with a direction. A world
        // that generates a flat river gets a still body rather than a conveyor running at zero.
        const float courseSpeed = course.flowSpeed();
        body.flowing = course.kind == WaterKind::River && courseSpeed > 0.0f && course.centreline.size() >= 2;
        if (body.flowing) {
            body.speed = settings.speedOverride > 0.0f ? settings.speedOverride
                                                       : courseSpeed * std::max(settings.speedScale, 0.0f);
            body.stillDirection =
                safeNormalize(xz(course.centreline.back()) - xz(course.centreline.front()), wind);
        } else {
            body.speed = settings.stillSpeed * glm::clamp(settings.stillFactor, 0.0f, 1.0f);
            body.stillDirection = wind;
        }
        set.bodies.push_back(std::move(body));
    }
    return set;
}

WaterBodySet waterBodies(const WorldMap& map, const WaterFlowSettings& settings) {
    return waterBodies(waterCourses(map), settings);
}

Result<WaterFlowSettings> waterFlowFromJson(const json& j) {
    WaterFlowSettings f;
    if (!j.is_object()) {
        return fail("water flow must be an object");
    }
    for (const auto& [key, target] :
         {std::pair{"speedScale", &f.speedScale}, std::pair{"speedOverride", &f.speedOverride},
          std::pair{"bankShear", &f.bankShear}, std::pair{"meander", &f.meander},
          std::pair{"stillFactor", &f.stillFactor}, std::pair{"stillSpeed", &f.stillSpeed}}) {
        auto v = readFloat(j, key, *target);
        if (!v) return std::unexpected(v.error());
        *target = *v;
    }
    if (j.contains("windDirection")) {
        const json& w = j.at("windDirection");
        if (!w.is_array() || w.size() != 2 || !w.at(0).is_number() || !w.at(1).is_number()) {
            return fail("water flow 'windDirection' must be [x, z]");
        }
        f.windDirection = {w.at(0).get<float>(), w.at(1).get<float>()};
    }
    if (auto v = f.validate(); !v) {
        return std::unexpected(v.error());
    }
    return f;
}

json waterFlowToJson(const WaterFlowSettings& flow) {
    return json{{"speedScale", flow.speedScale},
                {"speedOverride", flow.speedOverride},
                {"bankShear", flow.bankShear},
                {"meander", flow.meander},
                {"stillFactor", flow.stillFactor},
                {"stillSpeed", flow.stillSpeed},
                {"windDirection", json::array({flow.windDirection.x, flow.windDirection.y})}};
}

} // namespace avgen::world
