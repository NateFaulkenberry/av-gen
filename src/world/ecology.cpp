#include "world/ecology.hpp"

#include "core/wind.hpp"

#include "core/color.hpp"
#include "core/noise.hpp"
#include "scene/struct_hash.hpp"

#include <nlohmann/json.hpp>

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace avgen::world {
namespace {

using json = nlohmann::json;
using scene::detail::StructHash;

constexpr std::uint32_t kAcceptChannel = 11;
constexpr std::uint32_t kJitterXChannel = 12;
constexpr std::uint32_t kJitterZChannel = 13;
constexpr std::uint32_t kScaleChannel = 14;
constexpr std::uint32_t kYawChannel = 15;

float random01(std::uint32_t seed, std::uint32_t cell, std::uint32_t channel) {
    return noise::hashIndex(seed, cell, channel);
}

// A band that is 1 inside and falls to 0 over `fade` on each side, like a biome range but written
// here because these filters are absolute rather than part of a biome's identity.
float within(float v, float lo, float hi) {
    return v >= lo && v <= hi ? 1.0f : 0.0f;
}

float peakDensity(const ScatterLayer& layer) {
    float peak = 0.0f;
    for (const BiomeDensity& d : layer.densities) {
        peak = std::max(peak, d.density);
    }
    return peak;
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

class HabitatIndex {
public:
    HabitatIndex(std::span<const glm::vec3> anchors, float radius) : radius_(radius) {
        for (const glm::vec3& anchor : anchors) {
            const glm::vec2 position(anchor.x, anchor.z);
            bins_[key(cell(position))].push_back(position);
        }
    }

    float nearest(glm::vec2 position) const {
        const glm::ivec2 center = cell(position);
        float nearestSquared = radius_ * radius_;
        for (int offsetZ = -1; offsetZ <= 1; ++offsetZ) {
            for (int offsetX = -1; offsetX <= 1; ++offsetX) {
                const auto found = bins_.find(key(center + glm::ivec2(offsetX, offsetZ)));
                if (found == bins_.end()) {
                    continue;
                }
                for (const glm::vec2 anchor : found->second) {
                    const glm::vec2 delta = position - anchor;
                    nearestSquared = std::min(nearestSquared, glm::dot(delta, delta));
                }
            }
        }
        return std::sqrt(nearestSquared);
    }

private:
    glm::ivec2 cell(glm::vec2 position) const {
        return glm::ivec2(glm::floor(position / radius_));
    }
    static std::uint64_t key(glm::ivec2 coordinate) {
        return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(coordinate.x)) << 32u) |
               static_cast<std::uint32_t>(coordinate.y);
    }
    float radius_;
    std::unordered_map<std::uint64_t, std::vector<glm::vec2>> bins_;
};

} // namespace

Result<void> ScatterLayer::validate() const {
    if (name.empty()) {
        return fail("a scatter layer needs a name");
    }
    if (asset.empty()) {
        return fail("scatter '{}': needs an asset path", name);
    }
    if (densities.empty()) {
        return fail("scatter '{}': needs at least one biome density", name);
    }
    for (const BiomeDensity& d : densities) {
        if (d.biome.empty()) {
            return fail("scatter '{}': a density entry needs a biome name", name);
        }
        if (d.density < 0.0f || d.density > 100.0f) {
            return fail("scatter '{}': density for '{}' must be in [0, 100] per square metre", name, d.biome);
        }
    }
    for (std::size_t i = 0; i < densities.size(); ++i) {
        for (std::size_t j = i + 1; j < densities.size(); ++j) {
            if (densities[i].biome == densities[j].biome) {
                return fail("scatter '{}': two densities for '{}'", name, densities[i].biome);
            }
        }
    }
    if (peakDensity(*this) <= 0.0f) {
        return fail("scatter '{}': every density is zero", name);
    }
    if (!(maxSlope >= minSlope) || !(maxAltitude >= minAltitude)) {
        return fail("scatter '{}': a filter range is inverted", name);
    }
    if (height < 0.0f || height > 1000.0f) {
        return fail("scatter '{}': height must be in [0, 1000] metres", name);
    }
    if (!(maxScale >= minScale) || minScale <= 0.0f) {
        return fail("scatter '{}': scale range must be positive and ordered", name);
    }
    if (clustering < 0.0f || clustering > 1.0f) {
        return fail("scatter '{}': clustering must be in [0, 1]", name);
    }
    if (!(clusterScale > 0.0f)) {
        return fail("scatter '{}': clusterScale must be positive", name);
    }
    if (alignToGround < 0.0f || alignToGround > 1.0f) {
        return fail("scatter '{}': alignToGround must be in [0, 1]", name);
    }
    if (maxInstances < 1 || maxInstances > 1000000) {
        return fail("scatter '{}': maxInstances must be in [1, 1000000]", name);
    }
    if (proximity) {
        const auto& rule = *proximity;
        if (rule.layer.empty() || rule.layer == name) {
            return fail("scatter '{}': proximity needs another layer's name", name);
        }
        if (!std::isfinite(rule.minDistance) || !std::isfinite(rule.maxDistance) ||
            rule.minDistance < 0.0f || rule.maxDistance <= rule.minDistance ||
            rule.maxDistance > 4096.0f) {
            return fail("scatter '{}': proximity distances must be finite, ordered and in [0, 4096]", name);
        }
        if (!std::isfinite(rule.fade) || rule.fade < 0.0f ||
            rule.fade > (rule.maxDistance - rule.minDistance) * 0.5f ||
            !std::isfinite(rule.strength) || rule.strength < 0.0f || rule.strength > 1.0f) {
            return fail("scatter '{}': proximity fade must fit the band and strength must be in [0, 1]", name);
        }
    }
    return {};
}

std::uint64_t ScatterLayer::structuralHash() const {
    StructHash h;
    h.str(name);
    h.str(asset);
    // Densities are a set, not a list: they are written as a JSON object, whose key order is not
    // preserved, and naming the same biomes in a different order is the same layer. Hashing them in
    // the order they happen to be in makes a file that round-trips report itself as changed.
    std::vector<const BiomeDensity*> sorted;
    sorted.reserve(densities.size());
    for (const BiomeDensity& d : densities) {
        sorted.push_back(&d);
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const BiomeDensity* a, const BiomeDensity* b) { return a->biome < b->biome; });
    h.u64(sorted.size());
    for (const BiomeDensity* d : sorted) {
        h.str(d->biome);
        h.f32(d->density);
    }
    for (const float v : {minSlope, maxSlope, minAltitude, maxAltitude, shoreOffset, height, minScale, maxScale, sink,
                          minScreenRadius, viewDistance,
                          alignToGround, randomYaw, clusterScale, clustering}) {
        h.f32(v);
    }
    h.v3(tint);
    h.v3(emissiveColor);
    h.f32(emissiveIntensity);
    h.f32(hueField);
    h.f32(hueFieldScale);
    h.f32(hueRandom);
    h.f32(emissiveRandom);
    h.f32(chromaDrift);
    h.f32(chromaDriftScale);
    h.f32(chromaDriftSpeed);
    h.f32(emissiveSparsity);
    h.str(materialProgram);
    h.boolean(avoidWater);
    h.boolean(castsShadow);
    h.u32(seed);
    h.i32(maxInstances);
    h.i32(meshBudget);
    if (proximity) {
        h.str(proximity->layer);
        h.f32(proximity->minDistance);
        h.f32(proximity->maxDistance);
        h.f32(proximity->fade);
        h.f32(proximity->strength);
    }
    return h.value();
}

Result<void> Ecology::validate(const BiomeSet& biomes) const {
    if (layers.size() > 64) {
        return fail("{} scatter layers (max 64)", layers.size());
    }
    std::unordered_set<std::string> preceding;
    for (const ScatterLayer& l : layers) {
        if (auto r = l.validate(); !r) {
            return r;
        }
        if (l.proximity && !preceding.contains(l.proximity->layer)) {
            return fail("scatter '{}': proximity layer '{}' must precede it", l.name, l.proximity->layer);
        }
        if (!preceding.insert(l.name).second) {
            return fail("duplicate scatter layer '{}'", l.name);
        }
        // A density naming a biome that does not exist is silently nothing, which is the worst
        // possible outcome: a layer that was authored, validated, built, and grew no plants.
        for (const BiomeDensity& d : l.densities) {
            const bool known = std::any_of(biomes.biomes.begin(), biomes.biomes.end(),
                                           [&](const Biome& b) { return b.name == d.biome; });
            if (!known) {
                return fail("scatter '{}': no biome named '{}'", l.name, d.biome);
            }
        }
    }
    return {};
}

std::uint64_t Ecology::structuralHash() const {
    StructHash h;
    h.u64(layers.size());
    for (const ScatterLayer& l : layers) {
        h.u64(l.structuralHash());
    }
    // Clearances are structural: moving a corridor has to replant the world, not merely redraw it.
    h.u64(clearances.size());
    for (const ScatterClearance& c : clearances) {
        h.f32(c.center.x);
        h.f32(c.center.y);
        h.f32(c.radius);
        h.f32(c.softness);
        h.f32(c.strength);
        h.f32(c.minHeight);
    }
    return h.value();
}

float clearanceWeight(std::span<const ScatterClearance> clearances, glm::vec2 p, float layerHeight) {
    float weight = 1.0f;
    for (const ScatterClearance& c : clearances) {
        if (c.radius <= 0.0f || c.strength <= 0.0f) {
            continue;
        }
        if (c.minHeight > 0.0f && layerHeight < c.minHeight) {
            continue;   // short enough to grow in the lane
        }
        const float distance = glm::length(p - c.center);
        // Smooth from the rim outwards. A hard edge reads as a stencil cut in the vegetation --
        // the eye finds the circle rather than the clearing -- so `softness` is the metres over
        // which the world grows back.
        const float outside = c.softness > 0.0f
                                  ? glm::smoothstep(c.radius, c.radius + c.softness, distance)
                                  : (distance >= c.radius ? 1.0f : 0.0f);
        weight *= glm::mix(1.0f, outside, glm::clamp(c.strength, 0.0f, 1.0f));
        if (weight <= 0.0f) {
            return 0.0f;
        }
    }
    return weight;
}

spatial::PointCloud scatter(const WorldMap& map, const ScatterLayer& layer,
                            std::span<const glm::vec3> anchors,
                            std::span<const ScatterClearance> clearances) {
    spatial::PointCloud out;
    const float peak = peakDensity(layer);
    if (peak <= 0.0f || map.biomes.empty()) {
        return out;
    }
    std::optional<HabitatIndex> habitat;
    if (layer.proximity && layer.proximity->strength > 0.0f) {
        if (!layer.validate()) {
            return out;
        }
        if (anchors.empty() && layer.proximity->strength == 1.0f) {
            return out;
        }
        habitat.emplace(anchors, layer.proximity->maxDistance);
    }
    // One cell per expected instance at the layer's peak density, so the grid is as coarse as the
    // layer is sparse. A tree layer at 0.002 per square metre walks a 22 m grid; grass at 0.4 walks
    // a 1.6 m one. Without this every layer would pay for the whole world at the finest spacing.
    const float cell = std::clamp(std::sqrt(1.0f / peak), 0.25f, 64.0f);
    const int nx = std::max(1, static_cast<int>(std::ceil(map.size.x / cell)));
    const int nz = std::max(1, static_cast<int>(std::ceil(map.size.y / cell)));

    // Which biome each density refers to, resolved once. A name that matches nothing is caught by
    // Ecology::validate; here it simply contributes nothing.
    std::vector<std::pair<int, float>> byIndex;
    for (const BiomeDensity& d : layer.densities) {
        for (std::size_t b = 0; b < map.biomes.biomes.size(); ++b) {
            if (map.biomes.biomes[b].name == d.biome) {
                byIndex.emplace_back(static_cast<int>(b), d.density);
                break;
            }
        }
    }
    if (byIndex.empty()) {
        return out;
    }

    std::vector<glm::vec3> positions;
    std::vector<glm::vec4> rotations;
    std::vector<glm::vec3> scales;
    positions.reserve(1024);

    const float cellArea = cell * cell;
    for (int j = 0; j < nz; ++j) {
        for (int i = 0; i < nx; ++i) {
            if (static_cast<int>(positions.size()) >= layer.maxInstances) {
                break;
            }
            const auto cellId = static_cast<std::uint32_t>(j * nx + i);
            const glm::vec2 base = map.min() + glm::vec2(static_cast<float>(i), static_cast<float>(j)) * cell;
            const glm::vec2 p = base + glm::vec2(random01(layer.seed, cellId, kJitterXChannel),
                                                 random01(layer.seed, cellId, kJitterZChannel)) *
                                           cell;
            if (p.x > map.max().x || p.y > map.max().y) {
                continue;
            }
            float habitatWeight = 1.0f;
            if (habitat) {
                const auto& rule = *layer.proximity;
                const float distance = habitat->nearest(p);
                float band = distance >= rule.minDistance && distance < rule.maxDistance ? 1.0f : 0.0f;
                if (rule.fade > 0.0f) {
                    band *= 1.0f - glm::smoothstep(rule.maxDistance - rule.fade, rule.maxDistance, distance);
                    if (rule.minDistance > 0.0f) {
                        band *= glm::smoothstep(rule.minDistance, rule.minDistance + rule.fade, distance);
                    }
                }
                habitatWeight = glm::mix(1.0f, band, rule.strength);
                if (habitatWeight <= 0.0f) {
                    continue;
                }
            }
            // A fixed half-metre, not the layer's cell size. Deriving the epsilon from the cell
            // would measure a sparse tree layer's slope over eleven metres and a dense grass
            // layer's over one, so the same point would be steep for one layer and flat for
            // another -- an accident of density masquerading as a decision. A plant sits on the
            // ground it is standing on, so the filter and the alignment both read that scale.
            const Sample s = map.sample(p, 0.5f);
            if (layer.avoidWater && s.height < s.waterSurface + layer.shoreOffset) {
                continue;
            }
            if (within(s.slope, layer.minSlope, layer.maxSlope) <= 0.0f ||
                within(s.altitude, layer.minAltitude, layer.maxAltitude) <= 0.0f) {
                continue;
            }
            // Before the biome is consulted: a cleared point is cleared whatever grows there.
            const float clearing = clearanceWeight(clearances, p, layer.height);
            if (clearing <= 0.0f) {
                continue;
            }
            const BiomeWeights w = map.biomes.at(s.altitude, s.slope, s.moisture, p);
            float density = 0.0f;
            for (const auto& [index, value] : byIndex) {
                if (index < w.count) {
                    density += w.weights[static_cast<std::size_t>(index)] * value;
                }
            }
            if (density <= 0.0f) {
                continue;
            }
            if (layer.clustering > 0.0f) {
                // Patches and gaps. The noise is world space and independent of the cell grid, so
                // two layers that share a cluster scale clump in the same places -- which is what
                // makes a fern and a mushroom look like they are growing together rather than
                // being two independent scatters that happen to overlap.
                const float patch = noise::fbm3(glm::vec3(p / std::max(layer.clusterScale, 1e-3f), 0.0f),
                                                layer.seed ^ 0x5bf03635u);
                density *= glm::mix(1.0f, glm::clamp(patch * 2.0f, 0.0f, 1.6f), layer.clustering);
            }
            const float expected = density * cellArea * habitatWeight * clearing;
            if (random01(layer.seed, cellId, kAcceptChannel) >= expected) {
                continue;
            }

            glm::vec3 position(p.x, s.height - layer.sink, p.y);
            const float yaw = random01(layer.seed, cellId, kYawChannel) * layer.randomYaw * 6.2831853f;
            glm::quat rotation = glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));
            if (layer.alignToGround > 0.0f) {
                const glm::vec3 up = glm::normalize(glm::mix(glm::vec3(0.0f, 1.0f, 0.0f), s.normal,
                                                             glm::clamp(layer.alignToGround, 0.0f, 1.0f)));
                const glm::vec3 axis = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), up);
                if (glm::dot(axis, axis) > 1e-12f) {
                    const float angle = std::acos(std::clamp(up.y, -1.0f, 1.0f));
                    rotation = glm::angleAxis(angle, glm::normalize(axis)) * rotation;
                }
            }
            const float scale = glm::mix(layer.minScale, layer.maxScale,
                                         random01(layer.seed, cellId, kScaleChannel));

            positions.push_back(position);
            rotations.emplace_back(rotation.x, rotation.y, rotation.z, rotation.w);
            scales.emplace_back(scale);
        }
    }

    out.resize(positions.size());
    if (positions.empty()) {
        return out;
    }
    std::copy(positions.begin(), positions.end(), out.positions().begin());
    std::copy(rotations.begin(), rotations.end(), out.rotations().begin());
    std::copy(scales.begin(), scales.end(), out.scales().begin());
    out.renumberIndices();
    out.reseed(layer.seed);
    return out;
}

// ---- json ----------------------------------------------------------------------------------------

Result<std::vector<ScatterClearance>> clearancesFromJson(const json& j) {
    if (!j.is_array()) {
        return fail("'clearings' must be an array");
    }
    std::vector<ScatterClearance> out;
    for (const json& e : j) {
        if (!e.is_object()) {
            return fail("'clearings' entries must be objects");
        }
        ScatterClearance c;
        if (!e.contains("center") || !e.at("center").is_array() || e.at("center").size() != 2) {
            return fail("a clearing needs a 'center' of [x, z]");
        }
        c.center = glm::vec2(e.at("center")[0].get<float>(), e.at("center")[1].get<float>());
        if (!e.contains("radius") || !e.at("radius").is_number()) {
            return fail("a clearing needs a numeric 'radius'");
        }
        c.radius = e.at("radius").get<float>();
        if (e.contains("softness") && e.at("softness").is_number()) {
            c.softness = e.at("softness").get<float>();
        }
        if (e.contains("strength") && e.at("strength").is_number()) {
            c.strength = e.at("strength").get<float>();
        }
        if (e.contains("minHeight") && e.at("minHeight").is_number()) {
            c.minHeight = e.at("minHeight").get<float>();
        }
        out.push_back(c);
    }
    return out;
}

json clearancesToJson(const std::vector<ScatterClearance>& clearances) {
    json out = json::array();
    for (const ScatterClearance& c : clearances) {
        out.push_back(json{{"center", {c.center.x, c.center.y}},
                           {"radius", c.radius},
                           {"softness", c.softness},
                           {"strength", c.strength},
                           {"minHeight", c.minHeight}});
    }
    return out;
}

Result<Ecology> ecologyFromJson(const json& j) {
    if (!j.is_array()) {
        return fail("'scatter' must be an array");
    }
    Ecology out;
    for (const json& e : j) {
        if (!e.is_object()) {
            return fail("'scatter' entries must be objects");
        }
        ScatterLayer l;
        if (!e.contains("name") || !e.at("name").is_string()) {
            return fail("a scatter layer needs a string 'name'");
        }
        l.name = e.at("name").get<std::string>();
        if (!e.contains("asset") || !e.at("asset").is_string()) {
            return fail("scatter '{}': needs a string 'asset'", l.name);
        }
        l.asset = e.at("asset").get<std::string>();
        if (!e.contains("densities") || !e.at("densities").is_object()) {
            return fail("scatter '{}': 'densities' must be an object of biome name to density", l.name);
        }
        for (const auto& [biome, value] : e.at("densities").items()) {
            if (!value.is_number()) {
                return fail("scatter '{}': density for '{}' must be a number", l.name, biome);
            }
            l.densities.push_back({biome, value.get<float>()});
        }
        struct Field { const char* key; float* target; };
        for (const Field& f : {Field{"minSlope", &l.minSlope}, Field{"maxSlope", &l.maxSlope},
                               Field{"minAltitude", &l.minAltitude}, Field{"maxAltitude", &l.maxAltitude},
                               Field{"shoreOffset", &l.shoreOffset}, Field{"height", &l.height},
                               Field{"minScale", &l.minScale},
                               Field{"maxScale", &l.maxScale}, Field{"sink", &l.sink},
                               Field{"alignToGround", &l.alignToGround}, Field{"randomYaw", &l.randomYaw},
                               Field{"clusterScale", &l.clusterScale}, Field{"clustering", &l.clustering},
                               Field{"minScreenRadius", &l.minScreenRadius},
                               Field{"viewDistance", &l.viewDistance},
                               Field{"hueField", &l.hueField},
                               Field{"hueFieldScale", &l.hueFieldScale},
                               Field{"hueRandom", &l.hueRandom},
                               Field{"emissiveRandom", &l.emissiveRandom},
                               Field{"chromaDrift", &l.chromaDrift},
                               Field{"chromaDriftScale", &l.chromaDriftScale},
                               Field{"chromaDriftSpeed", &l.chromaDriftSpeed},
                               Field{"emissiveSparsity", &l.emissiveSparsity}}) {
            auto v = readFloat(e, f.key, *f.target);
            if (!v) {
                return fail("scatter '{}': {}", l.name, v.error().message);
            }
            *f.target = *v;
        }
        if (e.contains("motion")) {
            if (!e.at("motion").is_object()) {
                return fail("scatter '{}': 'motion' must be an object", l.name);
            }
            l.motion = wind::motionFromJson(e.at("motion"));
        }
        if (e.contains("proximity")) {
            const json& proximity = e.at("proximity");
            if (!proximity.is_object() || !proximity.contains("layer") ||
                !proximity.at("layer").is_string()) {
                return fail("scatter '{}': 'proximity' needs an object with a string 'layer'", l.name);
            }
            l.proximity.emplace();
            l.proximity->layer = proximity.at("layer").get<std::string>();
            for (const Field& field : {Field{"minDistance", &l.proximity->minDistance},
                                       Field{"maxDistance", &l.proximity->maxDistance},
                                       Field{"fade", &l.proximity->fade},
                                       Field{"strength", &l.proximity->strength}}) {
                auto value = readFloat(proximity, field.key, *field.target);
                if (!value) {
                    return fail("scatter '{}': proximity {}", l.name, value.error().message);
                }
                *field.target = *value;
            }
        }
        if (e.contains("materialProgram")) {
            if (!e.at("materialProgram").is_string()) {
                return fail("scatter '{}': 'materialProgram' must be a string", l.name);
            }
            l.materialProgram = e.at("materialProgram").get<std::string>();
        }
        auto emissive = readFloat(e, "emissiveIntensity", l.emissiveIntensity);
        if (!emissive) {
            return fail("scatter '{}': {}", l.name, emissive.error().message);
        }
        l.emissiveIntensity = *emissive;
        for (const auto& [key, target] : {std::pair{"tint", &l.tint}, std::pair{"emissiveColor", &l.emissiveColor}}) {
            if (!e.contains(key)) {
                continue;
            }
            const json& a = e.at(key);
            if (!a.is_array() || a.size() != 3 || !a.at(0).is_number()) {
                return fail("scatter '{}': '{}' must be an array of 3 numbers", l.name, key);
            }
            *target = glm::vec3(a.at(0).get<float>(), a.at(1).get<float>(), a.at(2).get<float>());
        }
        if (e.contains("castsShadow")) {
            if (!e.at("castsShadow").is_boolean()) {
                return fail("scatter '{}': 'castsShadow' must be a boolean", l.name);
            }
            l.castsShadow = e.at("castsShadow").get<bool>();
        }
        if (e.contains("avoidWater")) {
            if (!e.at("avoidWater").is_boolean()) {
                return fail("scatter '{}': 'avoidWater' must be a boolean", l.name);
            }
            l.avoidWater = e.at("avoidWater").get<bool>();
        }
        for (const auto& [key, target] : {std::pair{"maxInstances", &l.maxInstances},
                                          std::pair{"meshBudget", &l.meshBudget}}) {
            if (e.contains(key)) {
                if (!e.at(key).is_number_integer()) {
                    return fail("scatter '{}': '{}' must be an integer", l.name, key);
                }
                *target = e.at(key).get<int>();
            }
        }
        if (e.contains("seed")) {
            if (!e.at("seed").is_number_unsigned()) {
                return fail("scatter '{}': 'seed' must be an unsigned integer", l.name);
            }
            l.seed = e.at("seed").get<std::uint32_t>();
        }
        if (auto r = l.validate(); !r) {
            return fail("{}", r.error().message);
        }
        out.layers.push_back(std::move(l));
    }
    return out;
}

json ecologyToJson(const Ecology& ecology) {
    json out = json::array();
    for (const ScatterLayer& l : ecology.layers) {
        json densities = json::object();
        for (const BiomeDensity& d : l.densities) {
            densities[d.biome] = d.density;
        }
        out.push_back(json{{"name", l.name},
                           {"asset", l.asset},
                           {"densities", std::move(densities)},
                           {"minSlope", l.minSlope},
                           {"maxSlope", l.maxSlope},
                           {"minAltitude", l.minAltitude},
                           {"maxAltitude", l.maxAltitude},
                           {"avoidWater", l.avoidWater},
                           {"castsShadow", l.castsShadow},
                           {"shoreOffset", l.shoreOffset},
                           {"height", l.height},
                           {"minScale", l.minScale},
                           {"maxScale", l.maxScale},
                           {"sink", l.sink},
                           {"alignToGround", l.alignToGround},
                           {"randomYaw", l.randomYaw},
                           {"clusterScale", l.clusterScale},
                           {"clustering", l.clustering},
                           {"minScreenRadius", l.minScreenRadius},
                           {"viewDistance", l.viewDistance},
                           {"motion", wind::motionToJson(l.motion)},
                           {"tint", json::array({l.tint.x, l.tint.y, l.tint.z})},
                           {"emissiveColor", json::array({l.emissiveColor.x, l.emissiveColor.y, l.emissiveColor.z})},
                           {"emissiveIntensity", l.emissiveIntensity},
                           {"hueField", l.hueField},
                           {"hueFieldScale", l.hueFieldScale},
                           {"hueRandom", l.hueRandom},
                           {"emissiveRandom", l.emissiveRandom},
                           {"chromaDrift", l.chromaDrift},
                           {"chromaDriftScale", l.chromaDriftScale},
                           {"chromaDriftSpeed", l.chromaDriftSpeed},
                           {"emissiveSparsity", l.emissiveSparsity},
                           {"materialProgram", l.materialProgram},
                           {"seed", l.seed},
                           {"maxInstances", l.maxInstances},
                           {"meshBudget", l.meshBudget}});
        if (l.proximity) {
            out.back()["proximity"] = {{"layer", l.proximity->layer},
                                        {"minDistance", l.proximity->minDistance},
                                        {"maxDistance", l.proximity->maxDistance},
                                        {"fade", l.proximity->fade},
                                        {"strength", l.proximity->strength}};
        }
    }
    return out;
}

std::vector<GlowCluster> aggregateGlow(const spatial::PointCloud& cloud, const ScatterLayer& layer,
                                       float cellSize, std::uint32_t hueSeed, float lift) {
    if (layer.emissiveIntensity <= 0.0f || cloud.count() == 0 || cellSize <= 0.0f) {
        return {};
    }
    const glm::vec3 colour = glm::vec3(layer.emissiveColor);
    const float peak = std::max({colour.x, colour.y, colour.z});
    if (peak <= 0.0f) {
        return {};
    }

    struct Bin {
        glm::dvec3 weighted{0.0};  // sum of position * weight
        glm::dvec3 sqWeighted{0.0};
        double weight = 0.0;
        std::uint32_t count = 0;
    };
    std::unordered_map<std::uint64_t, Bin> bins;
    const auto positions = cloud.positions();
    const auto scales = cloud.scales();
    const float inv = 1.0f / cellSize;

    for (std::size_t i = 0; i < positions.size(); ++i) {
        const glm::vec3 p = positions[i];
        // An instance's share of the patch is its emitting area, so a large specimen counts for
        // more than a seedling instead of every placement counting once.
        const float s = i < scales.size() ? std::max(scales[i].x, 1e-3f) : 1.0f;
        const double w = static_cast<double>(s) * static_cast<double>(s);
        const auto gx = static_cast<std::int64_t>(std::floor(p.x * inv));
        const auto gz = static_cast<std::int64_t>(std::floor(p.z * inv));
        const auto key = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(gx)) << 32) |
                         static_cast<std::uint32_t>(gz);
        Bin& b = bins[key];
        b.weighted += glm::dvec3(p) * w;
        b.sqWeighted += glm::dvec3(p) * glm::dvec3(p) * w;
        b.weight += w;
        ++b.count;
    }

    std::vector<GlowCluster> out;
    out.reserve(bins.size());
    for (const auto& [key, b] : bins) {
        if (b.weight <= 0.0) {
            continue;
        }
        const glm::dvec3 mean = b.weighted / b.weight;
        // Standard deviation of the placements in the cell, so a tight clump reads as a small
        // bright source and a scattered one as a broad dim wash.
        const glm::dvec3 var = glm::max(b.sqWeighted / b.weight - mean * mean, glm::dvec3(0.0));
        const auto spread = static_cast<float>(std::sqrt(var.x + var.z));
        GlowCluster g;
        g.position = glm::vec3(mean) + glm::vec3(0.0f, layer.height * lift, 0.0f);
        g.radius = std::max(spread, layer.height * 0.5f);
        g.color = colour / peak;
        if (layer.hueField != 0.0f) {
            // The same field, sampled at the same place with the same seed as the instances that
            // stand here, so the light matches the thing emitting it.
            const float scale = std::max(layer.hueFieldScale, 1e-3f);
            const float field = noise::regionField(glm::vec3(mean) / scale, hueSeed ^ 0x9e37u);
            g.color = color::hueShift(g.color, layer.hueField * field);
        }
        g.power = layer.emissiveIntensity * peak * static_cast<float>(b.weight) *
                  (1.0f - std::clamp(layer.emissiveSparsity, 0.0f, 1.0f));
        out.push_back(g);
    }
    // A stable order: the bins come out of a hash map, and the per-frame selection that follows
    // takes a prefix of this list, which must not depend on iteration order.
    std::sort(out.begin(), out.end(), [](const GlowCluster& a, const GlowCluster& b) {
        if (a.position.x != b.position.x) return a.position.x < b.position.x;
        return a.position.z < b.position.z;
    });
    return out;
}

} // namespace avgen::world
