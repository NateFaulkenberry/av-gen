#pragma once

// Biomes (ADR-047): what kind of place each point of the world is, derived from the geography
// rather than painted on top of it. A biome is a set of ranges over the things a WorldMap already
// knows -- altitude, slope, moisture -- plus optional regions that say "and also, here".
//
// Weights, not an index. Every biome scores a point, the scores are normalised, and what a caller
// gets back is a blend. That is what makes a transition a band rather than a line: the ground
// material, and later the scatter densities, read the same weights and cross over together.
//
// One authored ordering. The set is ordered, and that order is the axis a terrain material blends
// along, because a material program can mask on exactly one scalar per register channel and a
// vertex has two floats to spend. So biomes must be authored in an order where neighbours in the
// list are neighbours on the ground -- wetland, meadow, forest, scree, alpine. Two biomes far apart
// in the list that meet on the ground will blend through the colours in between. That is a real
// constraint of this encoding and the reason to keep a set small and ordered like a gradient.

#include "core/error.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace avgen::world {

constexpr int kMaxBiomes = 8;

// A band with soft edges. `fade` is how far outside [lo, hi] the membership takes to reach zero, so
// a biome's edge is a slope rather than a step and two neighbours overlap instead of abutting.
struct Range {
    float lo = 0.0f;
    float hi = 1.0f;
    float fade = 0.1f;
    [[nodiscard]] float membership(float v) const;
};

struct BiomeRule {
    Range altitude;   // 0..1 over the map's measured height range
    Range slope;      // 0 flat .. 1 vertical
    Range moisture;   // 0 dry .. 1 at the water's edge
    float weight = 1.0f; // multiplies the product; how strongly this biome claims what it matches
};

// An explicit place a biome is, whatever the rules say. This is how a fungal grove ends up
// somewhere a shot can be built around instead of wherever the moisture happened to land.
struct BiomeRegion {
    std::vector<glm::vec2> path; // world XZ; one point is a radial region, two or more a corridor
    float width = 40.0f;
    float falloff = 1.0f;
    float strength = 1.0f;       // added to this biome's score at the centre
};

struct Biome {
    std::string name;
    BiomeRule rule;
    std::vector<BiomeRegion> regions;
    glm::vec3 groundColor{0.02f, 0.09f, 0.06f}; // linear
    glm::vec3 rockColor{0.09f, 0.11f, 0.15f};   // linear; what steep ground in this biome shows
    float roughness = 0.9f;

    [[nodiscard]] Result<void> validate() const;
};

// A normalised blend. `count` is the set's size, so `axis` knows what to divide by.
struct BiomeWeights {
    std::array<float, kMaxBiomes> weights{};
    int count = 0;
    [[nodiscard]] int dominant() const;
    // The blend's position along the authored order, 0..1. This is the single scalar a terrain
    // vertex carries and a material program blends a palette along.
    [[nodiscard]] float axis() const;
};

struct BiomeSet {
    std::vector<Biome> biomes;

    // `altitude` is 0..1, `slope` 0..1, `moisture` 0..1, `p` world XZ for the regions. Returns an
    // even blend of everything when nothing scores, rather than nothing, so a point is always
    // somewhere.
    [[nodiscard]] BiomeWeights at(float altitude, float slope, float moisture, glm::vec2 p) const;
    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] std::uint64_t structuralHash() const;
    [[nodiscard]] bool empty() const { return biomes.empty(); }
};

// The biomes of the shipped world, in gradient order: the wet floor of the basin, its meadows, the
// forested flanks, the scree where the slope turns over, and the pale rim.
[[nodiscard]] BiomeSet defaultBiomes();

[[nodiscard]] Result<BiomeSet> biomeSetFromJson(const nlohmann::json& j);
[[nodiscard]] nlohmann::json biomeSetToJson(const BiomeSet& set);

} // namespace avgen::world
