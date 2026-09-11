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

// Where p projects onto the segment a-b, as a parameter in [0, 1].
float projectOnSegment(glm::vec2 p, glm::vec2 a, glm::vec2 b) {
    const glm::vec2 ab = b - a;
    const float d2 = glm::dot(ab, ab);
    if (d2 < kEps) {
        return 0.0f;
    }
    return glm::clamp(glm::dot(p - a, ab) / d2, 0.0f, 1.0f);
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

const char* waterBodyKindName(WaterBodyKind kind) {
    switch (kind) {
    case WaterBodyKind::River:
        return "river";
    case WaterBodyKind::Still:
        return "still";
    }
    return "river";
}

Result<void> WaterFlowSettings::validate() const {
    if (!(flowSpeed >= 0.0f) || flowSpeed > 100.0f) {
        return fail("water flow: flowSpeed must be in [0, 100] m/s");
    }
    if (gradientResponse < 0.0f || gradientResponse > 4.0f) {
        return fail("water flow: gradientResponse must be in [0, 4]");
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
    return {};
}

std::uint64_t WaterFlowSettings::structuralHash() const {
    StructHash h;
    h.f32(flowSpeed);
    h.f32(gradientResponse);
    h.f32(bankShear);
    h.f32(meander);
    h.f32(stillFactor);
    h.f32(windDirection.x);
    h.f32(windDirection.y);
    return h.value();
}

// ---- WaterBody ------------------------------------------------------------------------------

float WaterBody::transitSeconds() const {
    return speed > kEps ? length / speed : 0.0f;
}

glm::vec3 WaterBody::pointAt(float along01) const {
    if (centre.empty()) {
        return glm::vec3(0.0f);
    }
    if (centre.size() == 1 || length <= kEps) {
        return centre.front();
    }
    const float s = glm::clamp(along01, 0.0f, 1.0f) * length;
    // The arc table is ascending, so the segment is found by a binary search rather than a walk:
    // a drifting object asks this every frame and a smoothed river is a few hundred points.
    const auto it = std::upper_bound(arc.begin(), arc.end(), s);
    const std::size_t hi = std::min<std::size_t>(
        static_cast<std::size_t>(it - arc.begin()), centre.size() - 1);
    const std::size_t lo = hi > 0 ? hi - 1 : 0;
    const float span = arc[hi] - arc[lo];
    const float t = span > kEps ? (s - arc[lo]) / span : 0.0f;
    return glm::mix(centre[lo], centre[hi], t);
}

glm::vec2 WaterBody::tangentAt(float along01) const {
    if (centre.size() < 2) {
        return stillDirection;
    }
    const float s = glm::clamp(along01, 0.0f, 1.0f) * length;
    const auto it = std::upper_bound(arc.begin(), arc.end(), s);
    const std::size_t hi = std::min<std::size_t>(
        static_cast<std::size_t>(it - arc.begin()), centre.size() - 1);
    const std::size_t lo = hi > 0 ? hi - 1 : 0;
    return safeNormalize(xz(centre[hi]) - xz(centre[lo]));
}

FlowSample WaterBody::flowAt(glm::vec2 p) const {
    FlowSample out;
    if (centre.empty()) {
        return out;
    }
    if (centre.size() == 1) {
        out.surface = centre.front().y;
        out.distance = glm::distance(p, xz(centre.front()));
        out.inside = out.distance <= halfWidth;
        out.direction = stillDirection;
        out.speed = speed * (out.inside ? 1.0f : 0.0f);
        return out;
    }

    // Nearest point on the polyline. Linear in the number of segments and called per water vertex
    // at build time and per floating object per frame; a smoothed Glowmere river is 113 segments,
    // which is a few microseconds. If a world ever carries a hundred rivers this wants a grid, and
    // the place to put one is here rather than in each of this function's callers.
    float best = std::numeric_limits<float>::max();
    std::size_t bestSeg = 0;
    float bestT = 0.0f;
    for (std::size_t i = 0; i + 1 < centre.size(); ++i) {
        const glm::vec2 a = xz(centre[i]);
        const glm::vec2 b = xz(centre[i + 1]);
        const float t = projectOnSegment(p, a, b);
        const glm::vec2 q = glm::mix(a, b, t);
        const float d2 = glm::dot(p - q, p - q);
        if (d2 < best) {
            best = d2;
            bestSeg = i;
            bestT = t;
        }
    }
    const glm::vec2 a = xz(centre[bestSeg]);
    const glm::vec2 b = xz(centre[bestSeg + 1]);
    const glm::vec2 q = glm::mix(a, b, bestT);
    const glm::vec2 tangent = safeNormalize(b - a, stillDirection);

    out.distance = std::sqrt(best);
    out.surface = glm::mix(centre[bestSeg].y, centre[bestSeg + 1].y, bestT);
    out.inside = out.distance <= halfWidth;
    // Signed side: positive to the right of the downstream direction.
    const glm::vec2 normal(-tangent.y, tangent.x);
    const float side = glm::dot(p - q, normal);
    out.across = halfWidth > kEps ? glm::clamp(side / halfWidth, -1.0f, 1.0f) : 0.0f;
    const float s = glm::mix(arc[bestSeg], arc[bestSeg + 1], bestT);
    out.along = length > kEps ? glm::clamp(s / length, 0.0f, 1.0f) : 0.0f;

    if (kind == WaterBodyKind::Still) {
        out.direction = stillDirection;
        out.speed = speed * (out.inside ? 1.0f : 0.0f);
        return out;
    }
    out.direction = tangent;
    // Shear: fastest at the centreline, slowest at the bank, and nothing outside it. The profile is
    // the parabola a real open channel has, not a linear ramp, because the difference is visible --
    // a linear ramp puts the whole midstream at one speed and reads as a belt.
    const float r = glm::clamp(out.distance / std::max(halfWidth, kEps), 0.0f, 1.0f);
    out.speed = speed * (1.0f - shearProfile(r));
    return out;
}

float WaterBody::shearProfile(float r) const {
    return glm::clamp(bankShear * r * r, 0.0f, 1.0f);
}

// ---- WaterBodySet ---------------------------------------------------------------------------

int WaterBodySet::nearestBody(glm::vec2 p) const {
    int best = -1;
    float bestD = std::numeric_limits<float>::max();
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        const FlowSample s = bodies[i].flowAt(p);
        // A point inside a channel belongs to it outright, whatever else is nearby.
        const float d = s.inside ? -1.0f : s.distance;
        if (d < bestD) {
            bestD = d;
            best = static_cast<int>(i);
        }
    }
    return best;
}

FlowSample WaterBodySet::flowAt(glm::vec2 p) const {
    FlowSample out;
    float bestD = std::numeric_limits<float>::max();
    for (const WaterBody& body : bodies) {
        const FlowSample s = body.flowAt(p);
        const float d = s.inside ? s.distance - body.halfWidth : s.distance;
        if (d < bestD) {
            bestD = d;
            out = s;
        }
    }
    if (settings.meander > 0.0f && out.speed > kEps) {
        // A slow wander off the centreline tangent, from noise in world space, so the current is a
        // current and not an arrow: neighbouring points differ a little and the same point always
        // differs the same way. Deterministic (no time term): the *pattern* moves because the water
        // moves through it, which is what a river looks like.
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
        if (body.name == name) {
            return &body;
        }
    }
    return nullptr;
}

std::uint64_t WaterBodySet::structuralHash() const {
    StructHash h;
    h.u64(settings.structuralHash());
    for (const WaterBody& body : bodies) {
        h.str(body.name);
        h.u32(static_cast<std::uint32_t>(body.kind));
        h.f32(body.halfWidth);
        h.f32(body.length);
        h.f32(body.speed);
        h.u64(body.centre.size());
        for (const glm::vec3& p : body.centre) {
            h.v3(p);
        }
    }
    return h.value();
}

// ---- derivation -----------------------------------------------------------------------------

WaterBodySet waterBodies(const WorldMap& map, const WaterFlowSettings& settings) {
    WaterBodySet set;
    set.settings = settings;
    const glm::vec2 wind = safeNormalize(settings.windDirection, glm::vec2(0.7071f, 0.7071f));

    // The average gradient over every water feature, so `gradientResponse` measures a reach against
    // this world's own rivers rather than against an absolute nobody can name. Computed first
    // because every body needs it and it is cheap.
    float gradientSum = 0.0f;
    int gradientCount = 0;
    for (const Feature& f : map.features) {
        if (!f.water) {
            continue;
        }
        const std::vector<glm::vec3>& path = f.samplePath();
        if (path.size() < 2) {
            continue;
        }
        float len = 0.0f;
        for (std::size_t i = 0; i + 1 < path.size(); ++i) {
            len += glm::distance(xz(path[i]), xz(path[i + 1]));
        }
        if (len > kEps) {
            gradientSum += std::fabs(path.front().y - path.back().y) / len;
            ++gradientCount;
        }
    }
    const float meanGradient = gradientCount > 0 ? gradientSum / static_cast<float>(gradientCount) : 0.0f;

    for (const Feature& f : map.features) {
        if (!f.water) {
            continue;
        }
        const std::vector<glm::vec3>& path = f.samplePath();
        if (path.empty()) {
            continue;
        }
        WaterBody body;
        body.name = f.name;
        body.centre = path;
        body.halfWidth = std::max(f.width, 0.5f);
        body.bankShear = glm::clamp(settings.bankShear, 0.0f, 1.0f);
        // The feature holds water `waterDepth` above the level its path carries, so the body's
        // surface is the path lifted by that much -- the same arithmetic WorldMap::waterSurface
        // does, read here so a floating thing and the surface it floats on cannot disagree.
        for (glm::vec3& p : body.centre) {
            p.y += f.waterDepth;
        }

        body.arc.assign(body.centre.size(), 0.0f);
        for (std::size_t i = 1; i < body.centre.size(); ++i) {
            body.arc[i] = body.arc[i - 1] + glm::distance(xz(body.centre[i - 1]), xz(body.centre[i]));
        }
        body.length = body.arc.back();
        glm::vec3 sum(0.0f);
        for (const glm::vec3& p : body.centre) {
            sum += p;
        }
        body.centroid = sum / static_cast<float>(body.centre.size());

        // Downstream is the direction the level falls. A path authored uphill is reversed here
        // rather than rejected: which end an artist started drawing from is not a statement about
        // which way the water runs, and the terrain was cut from the same levels either way.
        body.fall = body.centre.front().y - body.centre.back().y;
        if (body.fall < 0.0f && body.length > kEps) {
            std::reverse(body.centre.begin(), body.centre.end());
            for (std::size_t i = 1; i < body.centre.size(); ++i) {
                body.arc[i] = body.arc[i - 1] + glm::distance(xz(body.centre[i - 1]), xz(body.centre[i]));
            }
            body.fall = -body.fall;
        }
        body.gradient = body.length > kEps ? body.fall / body.length : 0.0f;

        // A course with no fall in it is not a river, whatever kind the feature calls itself: the
        // tarn is authored as a Valley with one point and no descent, and a lake bed drawn as a
        // long flat Flat would be the same. One centimetre of fall over the whole course is the
        // threshold, which is below what any authored river has and above float noise.
        const bool flowing = body.length > kEps && body.fall > 0.01f && path.size() >= 2;
        body.kind = flowing ? WaterBodyKind::River : WaterBodyKind::Still;
        if (flowing) {
            const float ratio = meanGradient > kEps ? body.gradient / meanGradient : 1.0f;
            body.speed = settings.flowSpeed *
                         glm::clamp(glm::mix(1.0f, ratio, glm::clamp(settings.gradientResponse, 0.0f, 4.0f)),
                                    0.15f, 4.0f);
            body.stillDirection = safeNormalize(xz(body.centre.back()) - xz(body.centre.front()), wind);
        } else {
            body.speed = settings.flowSpeed * glm::clamp(settings.stillFactor, 0.0f, 1.0f);
            body.stillDirection = wind;
        }
        set.bodies.push_back(std::move(body));
    }
    return set;
}

Result<WaterFlowSettings> waterFlowFromJson(const json& j) {
    WaterFlowSettings f;
    if (!j.is_object()) {
        return fail("water flow must be an object");
    }
    auto speed = readFloat(j, "flowSpeed", f.flowSpeed);
    if (!speed) return std::unexpected(speed.error());
    f.flowSpeed = *speed;
    auto gradient = readFloat(j, "gradientResponse", f.gradientResponse);
    if (!gradient) return std::unexpected(gradient.error());
    f.gradientResponse = *gradient;
    auto shear = readFloat(j, "bankShear", f.bankShear);
    if (!shear) return std::unexpected(shear.error());
    f.bankShear = *shear;
    auto meander = readFloat(j, "meander", f.meander);
    if (!meander) return std::unexpected(meander.error());
    f.meander = *meander;
    auto still = readFloat(j, "stillFactor", f.stillFactor);
    if (!still) return std::unexpected(still.error());
    f.stillFactor = *still;
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
    return json{{"flowSpeed", flow.flowSpeed},
                {"gradientResponse", flow.gradientResponse},
                {"bankShear", flow.bankShear},
                {"meander", flow.meander},
                {"stillFactor", flow.stillFactor},
                {"windDirection", json::array({flow.windDirection.x, flow.windDirection.y})}};
}

} // namespace avgen::world
