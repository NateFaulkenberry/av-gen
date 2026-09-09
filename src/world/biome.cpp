#include "world/biome.hpp"

#include "scene/struct_hash.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace avgen::world {
namespace {

using json = nlohmann::json;
using scene::detail::StructHash;

float smoothEdge(float u) {
    const float t = glm::clamp(u, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float distanceToPath(const std::vector<glm::vec2>& path, glm::vec2 p) {
    if (path.empty()) {
        return std::numeric_limits<float>::max();
    }
    if (path.size() == 1) {
        return glm::distance(p, path[0]);
    }
    float best = std::numeric_limits<float>::max();
    for (std::size_t i = 0; i + 1 < path.size(); ++i) {
        const glm::vec2 ab = path[i + 1] - path[i];
        const float len2 = glm::dot(ab, ab);
        const float t = len2 > 1e-12f ? glm::clamp(glm::dot(p - path[i], ab) / len2, 0.0f, 1.0f) : 0.0f;
        best = std::min(best, glm::distance(p, path[i] + ab * t));
    }
    return best;
}

Result<float> readFloat(const json& j, const char* key, float def) {
    if (!j.contains(key)) {
        return def;
    }
    if (!j.at(key).is_number()) {
        return fail("'{}' must be a number", key);
    }
    return j.at(key).get<float>();
}

Result<Range> readRange(const json& j, const char* key, Range def) {
    if (!j.contains(key)) {
        return def;
    }
    const json& r = j.at(key);
    if (r.is_array() && (r.size() == 2 || r.size() == 3)) {
        for (const json& e : r) {
            if (!e.is_number()) {
                return fail("'{}' must be [lo, hi] or [lo, hi, fade]", key);
            }
        }
        Range out;
        out.lo = r.at(0).get<float>();
        out.hi = r.at(1).get<float>();
        out.fade = r.size() == 3 ? r.at(2).get<float>() : def.fade;
        return out;
    }
    return fail("'{}' must be [lo, hi] or [lo, hi, fade]", key);
}

Result<glm::vec3> readColor(const json& j, const char* key, glm::vec3 def) {
    if (!j.contains(key)) {
        return def;
    }
    const json& a = j.at(key);
    if (!a.is_array() || a.size() != 3 || !a.at(0).is_number()) {
        return fail("'{}' must be an array of 3 numbers", key);
    }
    return glm::vec3(a.at(0).get<float>(), a.at(1).get<float>(), a.at(2).get<float>());
}

} // namespace

float Range::membership(float v) const {
    if (v >= lo && v <= hi) {
        return 1.0f;
    }
    const float f = std::max(fade, 1e-4f);
    return v < lo ? smoothEdge(1.0f - (lo - v) / f) : smoothEdge(1.0f - (v - hi) / f);
}

int BiomeWeights::dominant() const {
    int best = 0;
    for (int i = 1; i < count && i < kMaxBiomes; ++i) {
        if (weights[static_cast<std::size_t>(i)] > weights[static_cast<std::size_t>(best)]) {
            best = i;
        }
    }
    return best;
}

float BiomeWeights::axis() const {
    if (count <= 1) {
        return 0.0f;
    }
    float sum = 0.0f;
    float mean = 0.0f;
    for (int i = 0; i < count && i < kMaxBiomes; ++i) {
        const float w = weights[static_cast<std::size_t>(i)];
        mean += w * static_cast<float>(i);
        sum += w;
    }
    return sum > 1e-6f ? glm::clamp(mean / sum / static_cast<float>(count - 1), 0.0f, 1.0f) : 0.0f;
}

BiomeWeights BiomeSet::at(float altitude, float slope, float moisture, glm::vec2 p) const {
    BiomeWeights out;
    out.count = static_cast<int>(std::min<std::size_t>(biomes.size(), kMaxBiomes));
    float total = 0.0f;
    for (int i = 0; i < out.count; ++i) {
        const Biome& b = biomes[static_cast<std::size_t>(i)];
        // A product, not a sum: a biome has to satisfy every one of its ranges to be here at all.
        // Summing would put alpine scree in the valley on the strength of its slope alone.
        float score = b.rule.weight * b.rule.altitude.membership(altitude) * b.rule.slope.membership(slope) *
                      b.rule.moisture.membership(moisture);
        for (const BiomeRegion& r : b.regions) {
            const float d = distanceToPath(r.path, p);
            if (r.width > 0.0f && d < r.width) {
                const float u = smoothEdge(1.0f - d / r.width);
                score += r.strength * (r.falloff == 1.0f ? u : std::pow(u, std::max(r.falloff, 0.01f)));
            }
        }
        out.weights[static_cast<std::size_t>(i)] = std::max(score, 0.0f);
        total += out.weights[static_cast<std::size_t>(i)];
    }
    if (total > 1e-6f) {
        for (int i = 0; i < out.count; ++i) {
            out.weights[static_cast<std::size_t>(i)] /= total;
        }
    } else if (out.count > 0) {
        // Nowhere is nowhere. A point that satisfies no biome gets an even blend rather than a
        // hole, which keeps the axis continuous instead of snapping to zero in the gaps.
        const float even = 1.0f / static_cast<float>(out.count);
        for (int i = 0; i < out.count; ++i) {
            out.weights[static_cast<std::size_t>(i)] = even;
        }
    }
    return out;
}

Result<void> Biome::validate() const {
    if (name.empty()) {
        return fail("a biome needs a name");
    }
    for (const auto& [label, r] : {std::pair{"altitude", rule.altitude}, std::pair{"slope", rule.slope},
                                   std::pair{"moisture", rule.moisture}}) {
        if (!(r.hi >= r.lo)) {
            return fail("biome '{}': {} range is inverted", name, label);
        }
        if (r.fade < 0.0f || r.fade > 100.0f) {
            return fail("biome '{}': {} fade must be in [0, 100]", name, label);
        }
    }
    if (!(rule.weight >= 0.0f) || rule.weight > 1000.0f) {
        return fail("biome '{}': weight must be in [0, 1000]", name);
    }
    if (regions.size() > 64) {
        return fail("biome '{}': {} regions (max 64)", name, regions.size());
    }
    for (const BiomeRegion& r : regions) {
        if (r.path.empty()) {
            return fail("biome '{}': a region needs at least one path point", name);
        }
        if (!(r.width > 0.0f)) {
            return fail("biome '{}': region width must be positive", name);
        }
    }
    if (roughness < 0.0f || roughness > 1.0f) {
        return fail("biome '{}': roughness must be in [0, 1]", name);
    }
    return {};
}

Result<void> BiomeSet::validate() const {
    if (biomes.size() > static_cast<std::size_t>(kMaxBiomes)) {
        return fail("{} biomes (max {})", biomes.size(), kMaxBiomes);
    }
    for (const Biome& b : biomes) {
        if (auto r = b.validate(); !r) {
            return r;
        }
    }
    for (std::size_t i = 0; i < biomes.size(); ++i) {
        for (std::size_t j = i + 1; j < biomes.size(); ++j) {
            if (biomes[i].name == biomes[j].name) {
                return fail("two biomes named '{}'", biomes[i].name);
            }
        }
    }
    return {};
}

std::uint64_t BiomeSet::structuralHash() const {
    StructHash h;
    h.u64(biomes.size());
    for (const Biome& b : biomes) {
        h.str(b.name);
        for (const Range& r : {b.rule.altitude, b.rule.slope, b.rule.moisture}) {
            h.f32(r.lo);
            h.f32(r.hi);
            h.f32(r.fade);
        }
        h.f32(b.rule.weight);
        h.u64(b.regions.size());
        for (const BiomeRegion& r : b.regions) {
            h.u64(r.path.size());
            for (const glm::vec2& q : r.path) {
                h.f32(q.x);
                h.f32(q.y);
            }
            h.f32(r.width);
            h.f32(r.falloff);
            h.f32(r.strength);
        }
        h.v3(b.groundColor);
        h.v3(b.rockColor);
        h.f32(b.roughness);
    }
    return h.value();
}

BiomeSet defaultBiomes() {
    // Ordered as a gradient from the water up, because the order is the axis a material blends
    // along. Colours are linear and deliberately quiet: this is the ground the bioluminescence will
    // have to read against, and a saturated floor leaves nothing for an organism to be brighter
    // than. Each one still has a hue of its own, so the frame is not grey when nothing is glowing.
    BiomeSet set;
    auto biome = [](std::string name, Range altitude, Range slope, Range moisture, float weight,
                    glm::vec3 ground, glm::vec3 rock, float roughness) {
        Biome b;
        b.name = std::move(name);
        b.rule.altitude = altitude;
        b.rule.slope = slope;
        b.rule.moisture = moisture;
        b.rule.weight = weight;
        b.groundColor = ground;
        b.rockColor = rock;
        b.roughness = roughness;
        return b;
    };
    // The bands are written against the world's actual spread, not against round numbers: slope on
    // this terrain has a median of 0.065 and a 90th percentile of 0.19, so "steep" starts at 0.17,
    // not at the 0.34 that sounds steep and owns one per cent of the map. `avgen_world_preview`
    // prints those percentiles and the share each biome ends up owning, which is how these were set.
    // Hue, not just value, and along a path rather than around a wheel. A first pass had every
    // biome a slightly different dark teal, which on screen was one dark teal. The correction
    // overshot: an olive meadow between a green forest and a violet scree made the ground rainbow,
    // because the palette is blended along the authored order and an entry off the path is a colour
    // every transition has to travel through. These walk teal -> green -> deep green -> violet-grey
    // -> cool grey-blue, which is variety the eye can name without a hue that arrives from nowhere.
    // Linear, converted from #0E5B52, #2C6B3A, #17452F, #4A4A63, #6E7A93.
    set.biomes.push_back(biome("marsh", {0.0f, 0.30f, 0.20f}, {0.0f, 0.16f, 0.14f}, {0.70f, 1.0f, 0.22f}, 1.4f,
                               {0.0044f, 0.1046f, 0.0844f}, {0.0132f, 0.0742f, 0.0700f}, 0.82f));
    set.biomes.push_back(biome("meadow", {0.0f, 0.42f, 0.22f}, {0.0f, 0.16f, 0.14f}, {0.32f, 0.72f, 0.22f}, 1.1f,
                               {0.0252f, 0.1470f, 0.0423f}, {0.0356f, 0.1010f, 0.0512f}, 0.88f));
    set.biomes.push_back(biome("forest", {0.04f, 0.66f, 0.24f}, {0.0f, 0.20f, 0.14f}, {0.0f, 0.44f, 0.24f}, 1.1f,
                               {0.0086f, 0.0595f, 0.0284f}, {0.0180f, 0.0530f, 0.0380f}, 0.90f));
    set.biomes.push_back(biome("scree", {0.05f, 1.0f, 0.18f}, {0.17f, 1.0f, 0.10f}, {0.0f, 0.85f, 0.26f}, 1.6f,
                               {0.0685f, 0.0685f, 0.1248f}, {0.0900f, 0.0880f, 0.1480f}, 0.94f));
    set.biomes.push_back(biome("rim", {0.70f, 1.0f, 0.24f}, {0.0f, 1.0f, 0.30f}, {0.0f, 0.60f, 0.30f}, 1.2f,
                               {0.1559f, 0.1946f, 0.2918f}, {0.1900f, 0.2280f, 0.3300f}, 0.86f));

    // The hollow the opening shot is composed in is a fungal grove because the shot needs it to be,
    // not because the moisture happened to land there. A region is how that gets said.
    BiomeRegion grove;
    grove.path = {{4.0f, -14.0f}};
    grove.width = 46.0f;
    grove.falloff = 1.4f;
    grove.strength = 2.2f;
    set.biomes[0].regions.push_back(grove);
    return set;
}

Result<BiomeSet> biomeSetFromJson(const json& j) {
    if (!j.is_array()) {
        return fail("'biomes' must be an array");
    }
    BiomeSet set;
    for (const json& e : j) {
        if (!e.is_object()) {
            return fail("'biomes' entries must be objects");
        }
        Biome b;
        if (!e.contains("name") || !e.at("name").is_string()) {
            return fail("a biome needs a string 'name'");
        }
        b.name = e.at("name").get<std::string>();
        for (const auto& [key, target] : {std::pair{"altitude", &b.rule.altitude}, std::pair{"slope", &b.rule.slope},
                                          std::pair{"moisture", &b.rule.moisture}}) {
            auto r = readRange(e, key, *target);
            if (!r) {
                return fail("biome '{}': {}", b.name, r.error().message);
            }
            *target = *r;
        }
        auto weight = readFloat(e, "weight", b.rule.weight);
        if (!weight) return fail("biome '{}': {}", b.name, weight.error().message);
        b.rule.weight = *weight;
        auto roughness = readFloat(e, "roughness", b.roughness);
        if (!roughness) return fail("biome '{}': {}", b.name, roughness.error().message);
        b.roughness = *roughness;
        auto ground = readColor(e, "groundColor", b.groundColor);
        if (!ground) return fail("biome '{}': {}", b.name, ground.error().message);
        b.groundColor = *ground;
        auto rock = readColor(e, "rockColor", b.rockColor);
        if (!rock) return fail("biome '{}': {}", b.name, rock.error().message);
        b.rockColor = *rock;
        if (e.contains("regions")) {
            if (!e.at("regions").is_array()) {
                return fail("biome '{}': 'regions' must be an array", b.name);
            }
            for (const json& rj : e.at("regions")) {
                BiomeRegion r;
                if (!rj.is_object() || !rj.contains("path") || !rj.at("path").is_array()) {
                    return fail("biome '{}': a region needs a 'path' array of [x, z] points", b.name);
                }
                for (const json& pt : rj.at("path")) {
                    if (!pt.is_array() || pt.size() != 2 || !pt.at(0).is_number()) {
                        return fail("biome '{}': region path points must be [x, z]", b.name);
                    }
                    r.path.emplace_back(pt.at(0).get<float>(), pt.at(1).get<float>());
                }
                for (const auto& [key, target] : {std::pair{"width", &r.width}, std::pair{"falloff", &r.falloff},
                                                  std::pair{"strength", &r.strength}}) {
                    auto v = readFloat(rj, key, *target);
                    if (!v) return fail("biome '{}': {}", b.name, v.error().message);
                    *target = *v;
                }
                r.path.shrink_to_fit();
                b.regions.push_back(std::move(r));
            }
        }
        set.biomes.push_back(std::move(b));
    }
    if (auto r = set.validate(); !r) {
        return fail("{}", r.error().message);
    }
    return set;
}

json biomeSetToJson(const BiomeSet& set) {
    json out = json::array();
    for (const Biome& b : set.biomes) {
        json e;
        e["name"] = b.name;
        const auto range = [](const Range& r) { return json::array({r.lo, r.hi, r.fade}); };
        e["altitude"] = range(b.rule.altitude);
        e["slope"] = range(b.rule.slope);
        e["moisture"] = range(b.rule.moisture);
        e["weight"] = b.rule.weight;
        e["groundColor"] = json::array({b.groundColor.x, b.groundColor.y, b.groundColor.z});
        e["rockColor"] = json::array({b.rockColor.x, b.rockColor.y, b.rockColor.z});
        e["roughness"] = b.roughness;
        if (!b.regions.empty()) {
            json regions = json::array();
            for (const BiomeRegion& r : b.regions) {
                json path = json::array();
                for (const glm::vec2& q : r.path) {
                    path.push_back(json::array({q.x, q.y}));
                }
                regions.push_back(json{{"path", std::move(path)},
                                       {"width", r.width},
                                       {"falloff", r.falloff},
                                       {"strength", r.strength}});
            }
            e["regions"] = std::move(regions);
        }
        out.push_back(std::move(e));
    }
    return out;
}

} // namespace avgen::world
