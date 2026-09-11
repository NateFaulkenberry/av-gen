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
    if (turbulence < 0.0f || turbulence > 1.0f) {
        return fail("water flow: turbulence must be in [0, 1]");
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
    h.f32(turbulence);
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

float WaterBody::wettedHalfWidth(float along01) const {
    if (wetted.empty()) {
        return halfWidth();
    }
    const float u = glm::clamp(along01, 0.0f, 1.0f) * static_cast<float>(wetted.size() - 1);
    const auto lo = static_cast<std::size_t>(u);
    const std::size_t hi = std::min(lo + 1, wetted.size() - 1);
    return glm::mix(wetted[lo], wetted[hi], u - static_cast<float>(lo)) * halfWidth();
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
    // One projection, not four. `WaterCourse` answers direction, surface, position-along and
    // containment, and each of those walks the polyline for itself -- which is the right shape for a
    // caller asking one question and the wrong shape for this one, which asks all four about the
    // same point and is called once per water vertex of a world and once per floating object per
    // frame. So the walk happens here and every answer comes out of it.
    //
    // That makes this arithmetic a second copy of the course's, which is worth being nervous about:
    // `tests/unit/test_water.cpp` asserts the two agree at several hundred points, so a change to
    // how terrain projects onto a course fails loudly here rather than drifting quietly.
    if (line.size() == 1) {
        out.surface = line.front().y;
        out.distance = glm::distance(p, xz(line.front()));
        out.inside = course.kind == WaterKind::Sea || out.distance <= halfWidth();
        out.direction = stillDirection;
        out.speed = out.inside ? speed : 0.0f;
        return out;
    }

    float best = std::numeric_limits<float>::max();
    std::size_t bestSeg = 0;
    float bestT = 0.0f;
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
            bestSeg = i;
            bestT = t;
        }
    }
    const glm::vec2 a = xz(line[bestSeg]);
    const glm::vec2 b = xz(line[bestSeg + 1]);
    const glm::vec2 nearest = glm::mix(a, b, bestT);
    const glm::vec2 tangent = safeNormalize(b - a, stillDirection);

    out.distance = std::sqrt(best);
    out.surface = glm::mix(line[bestSeg].y, line[bestSeg + 1].y, bestT);
    out.inside = course.kind == WaterKind::Sea || out.distance <= halfWidth();
    if (arc.size() == line.size() && arc.back() > kEps) {
        out.along = glm::clamp(glm::mix(arc[bestSeg], arc[bestSeg + 1], bestT) / arc.back(), 0.0f, 1.0f);
    }
    const glm::vec2 normal(-tangent.y, tangent.x);
    out.across = halfWidth() > kEps ? glm::clamp(glm::dot(p - nearest, normal) / halfWidth(), -1.0f, 1.0f)
                                    : 0.0f;

    if (!flowing) {
        // Still water: no downstream, a slow wind-driven drift instead, and only inside the body.
        out.direction = stillDirection;
        out.speed = out.inside ? speed : 0.0f;
        return out;
    }
    out.direction = tangent;
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
    if (settings.turbulence > 0.0f && out.speed > kEps) {
        // Fast reaches and slow pools. Read from a field twice as coarse as the meander's, so the
        // two do not line up and a bend that turns is not also the bend that speeds up.
        const float v =
            noise::valueNoise(glm::vec3(p.x * 0.006f, 0.0f, p.y * 0.006f), 0x1F10DEu) * 2.0f - 1.0f;
        out.speed = out.speed * glm::clamp(1.0f + v * settings.turbulence, 0.05f, 4.0f);
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

WaterBodySet waterBodies(const std::vector<WaterCourse>& courses, const WaterFlowSettings& settings,
                         const TerrainQuery* terrain) {
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
        // Whether it flows is the course's own answer. `WaterCourse::flowSpeed()` floors at
        // 0.05 m/s, so it is never zero for a river and cannot be the test; the descent is. A
        // course with no fall in it has no direction anyone should trust -- it is either a lake
        // drawn as a river or a path authored uphill, which ADR-090 clamps to zero rather than
        // reversing -- and a body with no direction gets the still treatment and says 0.00 m/s in
        // the terrain node's log, which is a good deal louder than a river running the wrong way.
        const float courseSpeed = course.flowSpeed();
        body.flowing = course.kind == WaterKind::River && course.descent > 0.01f &&
                       course.centreline.size() >= 2;
        if (body.flowing) {
            body.speed = settings.speedOverride > 0.0f ? settings.speedOverride
                                                       : courseSpeed * std::max(settings.speedScale, 0.0f);
            body.stillDirection =
                safeNormalize(xz(course.centreline.back()) - xz(course.centreline.front()), wind);
        } else {
            body.speed = settings.stillSpeed * glm::clamp(settings.stillFactor, 0.0f, 1.0f);
            body.stillDirection = wind;
        }
        // How far across the channel there is water, at each of `kWettedSamples` points along it.
        // Probed outward from the centreline and stopped at the first dry step, because a channel
        // is wet in one connected band around its middle and a probe that skipped a dry step would
        // report a puddle beyond the bank as part of the river.
        if (terrain != nullptr && terrain->valid() && !body.centre().empty()) {
            constexpr int kProbes = 10;
            body.wetted.assign(kWettedSamples, 0.0f);
            for (int i = 0; i < kWettedSamples; ++i) {
                const float along = kWettedSamples > 1
                                        ? static_cast<float>(i) / static_cast<float>(kWettedSamples - 1)
                                        : 0.0f;
                const glm::vec3 c = body.pointAt(along);
                const glm::vec2 tangent = body.tangentAt(along);
                const glm::vec2 normal(-tangent.y, tangent.x);
                const glm::vec2 p0(c.x, c.z);
                if (terrain->waterDepthAt(p0) <= 0.0f) {
                    continue; // the centreline itself is dry here: a shoal, or the head of the run
                }
                float reach = 0.0f;
                for (int k = 1; k <= kProbes; ++k) {
                    const float f = static_cast<float>(k) / static_cast<float>(kProbes);
                    const float d = f * body.halfWidth();
                    // Both banks: the narrower one is the one a floating thing has to respect,
                    // because the offsets are symmetric about the centreline.
                    if (terrain->waterDepthAt(p0 + normal * d) <= 0.0f ||
                        terrain->waterDepthAt(p0 - normal * d) <= 0.0f) {
                        break;
                    }
                    reach = f;
                }
                body.wetted[static_cast<std::size_t>(i)] = reach;
            }
        }
        set.bodies.push_back(std::move(body));
    }
    return set;
}

WaterBodySet waterBodies(const WorldMap& map, const WaterFlowSettings& settings,
                         const TerrainQuery* terrain) {
    return waterBodies(waterCourses(map), settings, terrain);
}

Result<WaterFlowSettings> waterFlowFromJson(const json& j) {
    WaterFlowSettings f;
    if (!j.is_object()) {
        return fail("water flow must be an object");
    }
    for (const auto& [key, target] :
         {std::pair{"speedScale", &f.speedScale}, std::pair{"speedOverride", &f.speedOverride},
          std::pair{"bankShear", &f.bankShear}, std::pair{"meander", &f.meander}, std::pair{"turbulence", &f.turbulence},
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
                {"turbulence", flow.turbulence},
                {"stillFactor", flow.stillFactor},
                {"stillSpeed", flow.stillSpeed},
                {"windDirection", json::array({flow.windDirection.x, flow.windDirection.y})}};
}

} // namespace avgen::world
