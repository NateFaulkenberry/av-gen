#include "scene/floaters.hpp"

#include "core/noise.hpp"
#include "scene/struct_hash.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace avgen::scene {
namespace {

using json = nlohmann::json;
using detail::StructHash;

// Four decorrelated uniforms in [0, 1) for instance `i` of a layer seeded `seed`. The same hash
// the point cloud uses, so a floating layer and a scattered one draw from the same family and two
// layers with the same seed do not line up with each other.
glm::vec4 instanceRandom(std::uint32_t seed, int i) {
    const auto index = static_cast<std::uint32_t>(i);
    return {noise::hashIndex(seed, index, 0), noise::hashIndex(seed, index, 1),
            noise::hashIndex(seed, index, 2), noise::hashIndex(seed, index, 3)};
}

float fractional(float v) { return v - std::floor(v); }

} // namespace

Result<void> FloatSpec::validate() const {
    if (water.empty()) {
        return fail("float: 'water' must name the terrain node whose water this floats on");
    }
    if (count < 0 || count > 20000) {
        return fail("float: count must be in [0, 20000]");
    }
    if (!(sizeMin > 0.0f) || sizeMax < sizeMin || sizeMax > 1000.0f) {
        return fail("float: sizeMin/sizeMax must satisfy 0 < min <= max <= 1000");
    }
    if (driftScale < -20.0f || driftScale > 20.0f) {
        return fail("float: driftScale must be in [-20, 20]");
    }
    if (driftSpread < 0.0f || driftSpread > 4.0f) {
        return fail("float: driftSpread must be in [0, 4]");
    }
    if (lateral < 0.0f || lateral > 1.0f) {
        return fail("float: lateral must be in [0, 1]");
    }
    if (margin < 0.0f || margin > 1.0f) {
        return fail("float: margin must be in [0, 1]");
    }
    if (clustering < 0.0f || clustering > 1.0f) {
        return fail("float: clustering must be in [0, 1]");
    }
    if (clusters < 1 || clusters > 4096) {
        return fail("float: clusters must be in [1, 4096]");
    }
    if (bob < 0.0f || bob > 100.0f) {
        return fail("float: bob must be in [0, 100] metres");
    }
    if (tilt < 0.0f || tilt > 1.5f) {
        return fail("float: tilt must be in [0, 1.5] radians");
    }
    return {};
}

std::uint64_t FloatSpec::structuralHash() const {
    StructHash h;
    h.boolean(enabled);
    h.str(water);
    h.str(body);
    h.i32(count);
    h.u32(seed);
    h.f32(sizeMin);
    h.f32(sizeMax);
    h.f32(driftScale);
    h.f32(driftSpread);
    h.f32(lateral);
    h.f32(margin);
    h.f32(clustering);
    h.i32(clusters);
    h.f32(spin);
    h.f32(bob);
    h.f32(bobRate);
    h.f32(tilt);
    h.f32(sink);
    return h.value();
}

json FloatSpec::toJson() const {
    return json{{"enabled", enabled}, {"water", water},         {"body", body},
                {"count", count},     {"seed", seed},           {"sizeMin", sizeMin},
                {"sizeMax", sizeMax}, {"driftScale", driftScale},
                {"driftSpread", driftSpread},                   {"lateral", lateral},
                {"margin", margin},   {"clustering", clustering}, {"clusters", clusters},
                {"spin", spin},       {"bob", bob},             {"bobRate", bobRate},
                {"tilt", tilt},       {"sink", sink}};
}

Result<FloatSpec> FloatSpec::fromJson(const json& j) {
    if (!j.is_object()) {
        return fail("float must be an object");
    }
    FloatSpec s;
    const auto readFloat = [&](const char* key, float& target) -> Result<void> {
        if (!j.contains(key)) return Result<void>{};
        if (!j.at(key).is_number()) return fail("float '{}' must be a number", key);
        target = j.at(key).get<float>();
        return Result<void>{};
    };
    if (j.contains("enabled")) {
        if (!j.at("enabled").is_boolean()) return fail("float 'enabled' must be a boolean");
        s.enabled = j.at("enabled").get<bool>();
    }
    for (const auto& [key, target] : {std::pair{"water", &s.water}, std::pair{"body", &s.body}}) {
        if (!j.contains(key)) continue;
        if (!j.at(key).is_string()) return fail("float '{}' must be a string", key);
        *target = j.at(key).get<std::string>();
    }
    if (j.contains("count")) {
        if (!j.at("count").is_number_integer()) return fail("float 'count' must be an integer");
        s.count = j.at("count").get<int>();
    }
    if (j.contains("clusters")) {
        if (!j.at("clusters").is_number_integer()) return fail("float 'clusters' must be an integer");
        s.clusters = j.at("clusters").get<int>();
    }
    if (j.contains("seed")) {
        if (!j.at("seed").is_number_unsigned()) return fail("float 'seed' must be an unsigned integer");
        s.seed = j.at("seed").get<std::uint32_t>();
    }
    for (const auto& [key, target] :
         {std::pair{"sizeMin", &s.sizeMin}, std::pair{"sizeMax", &s.sizeMax},
          std::pair{"driftScale", &s.driftScale}, std::pair{"driftSpread", &s.driftSpread},
          std::pair{"lateral", &s.lateral}, std::pair{"margin", &s.margin},
          std::pair{"clustering", &s.clustering}, std::pair{"spin", &s.spin},
          std::pair{"bob", &s.bob}, std::pair{"bobRate", &s.bobRate}, std::pair{"tilt", &s.tilt},
          std::pair{"sink", &s.sink}}) {
        if (auto r = readFloat(key, *target); !r) {
            return std::unexpected(r.error());
        }
    }
    if (auto r = s.validate(); !r) {
        return std::unexpected(r.error());
    }
    return s;
}

void evaluateFloaters(const world::WaterBodySet& bodies, const FloatSpec& spec, float time,
                      std::vector<Floater>& out) {
    out.clear();
    if (!spec.enabled || spec.count <= 0 || bodies.empty()) {
        return;
    }
    // Which bodies this layer may land on. Naming one is how a scene puts blossoms on the tarn and
    // leaves on the river without two worlds.
    std::vector<const world::WaterBody*> pool;
    for (const world::WaterBody& b : bodies.bodies) {
        if (spec.body.empty() || b.name() == spec.body) {
            pool.push_back(&b);
        }
    }
    if (pool.empty()) {
        return;
    }
    out.reserve(static_cast<std::size_t>(spec.count));

    for (int i = 0; i < spec.count; ++i) {
        const glm::vec4 r = instanceRandom(spec.seed, i);
        const world::WaterBody& body = *pool[static_cast<std::size_t>(r.x * static_cast<float>(pool.size())) %
                                             pool.size()];

        // Where along the course it started. Clustering interpolates between an even spread and a
        // set of knots: `clusters` anchors drawn from the same hash, each with its own tight
        // spread, so raising `clustering` gathers the same instances rather than moving them
        // somewhere new -- which is what makes the knob usable while looking at the result.
        const float even = (static_cast<float>(i) + 0.5f) / static_cast<float>(spec.count);
        const int clusterCount = std::max(spec.clusters, 1);
        const int which = i % clusterCount;
        const glm::vec4 anchorRandom = instanceRandom(spec.seed ^ 0x51ED2701u, which);
        const float clustered = fractional(anchorRandom.x + (r.y - 0.5f) * 0.09f);
        float along = glm::mix(even, clustered, glm::clamp(spec.clustering, 0.0f, 1.0f));

        // Drift. Every instance travels at its own fraction of the body's speed, and wraps at the
        // mouth: a river is a loop as far as a leaf on it is concerned, and the alternative is a
        // course that empties out over a long shot.
        const float spread = 1.0f + (r.z - 0.5f) * 2.0f * spec.driftSpread;
        const float metresPerSecond = body.speed * spec.driftScale * std::max(spread, 0.05f);
        const float transit = body.length() > 1e-3f ? body.length() : 1.0f;
        along = fractional(along + metresPerSecond * time / transit);

        Floater f;
        const glm::vec3 centre = body.pointAt(along);
        const glm::vec2 tangent = body.tangentAt(along);
        const glm::vec2 normal(-tangent.y, tangent.x);
        // Across the channel, pulled off the bank by `margin`. The offset is fixed per instance: a
        // leaf does not wander across a river inside one shot, and one that does reads as a fish.
        // The banks are a planar test; the bed is not planar. `WaterBody::wettedHalfWidth` is how
        // far across this reach there *is* water, measured once when the body was derived rather
        // than per leaf per frame, and the offset is expressed as a fraction of it -- so a pad in a
        // narrow reach sits in the narrow reach instead of on the shoal beside it, and as it drifts
        // into one it slides in rather than teleporting.
        const float usable = std::max(spec.lateral - spec.margin, 0.0f);
        const float reach = body.wettedHalfWidth(along);
        const float across = (r.w * 2.0f - 1.0f) * usable * reach;
        const glm::vec2 p = glm::vec2(centre.x, centre.z) + normal * across;
        // A reach with no water in it at all holds nothing. Faded rather than cut, because a pad
        // drifts from the head to the mouth and wraps, and a hard test would blink it out at the
        // shallow head and blink it back a second later.
        const float wetness = glm::clamp(reach / std::max(body.halfWidth() * 0.25f, 1e-3f), 0.0f, 1.0f);
        if (wetness <= 0.001f) {
            continue;
        }

        // The surface it sits on is the centreline's at this arc position -- a body's surface is
        // flat across its width, which is what makes it a water surface -- and `centre` already is
        // that point. Asking the body for a flow sample here would re-project the point that was
        // just computed *from* the arc position, which is the same walk again for an answer already
        // in hand: the shear profile takes the offset directly.
        const float surface = centre.y;
        const float phase = r.x * 6.28318531f;
        const float bob = std::sin(time * spec.bobRate * 6.28318531f + phase) * spec.bob;
        f.position = glm::vec3(p.x, surface + bob - spec.sink, p.y);

        // Orientation: a yaw that turns slowly, plus a small fixed lean, so a raft of pads is not a
        // set of parallel discs. The spin's sign comes off the hash, because every pad rotating the
        // same way is the second-loudest tell after a uniform speed.
        const float spinSign = r.y < 0.5f ? -1.0f : 1.0f;
        const float yaw = phase + spinSign * spec.spin * time * (0.4f + r.z);
        const float lean = spec.tilt * (r.w - 0.5f) * 2.0f;
        const float leanAxis = r.z * 6.28318531f;
        f.rotation = glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f)) *
                     glm::angleAxis(lean, glm::normalize(glm::vec3(std::cos(leanAxis), 0.0f,
                                                                   std::sin(leanAxis))));
        f.scale = glm::mix(spec.sizeMin, spec.sizeMax, r.z) * wetness;
        f.random = r.w;
        const float shear = body.halfWidth() > 1e-4f
                                ? body.shearProfile(glm::clamp(std::fabs(across) / body.halfWidth(),
                                                               0.0f, 1.0f))
                                : 0.0f;
        f.speed = body.speed * (1.0f - shear) * spec.driftScale * spread;
        out.push_back(f);
    }
}

} // namespace avgen::scene
