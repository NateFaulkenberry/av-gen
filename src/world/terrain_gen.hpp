#pragma once

// Terrain generation from artistic parameters (§29-§32 of the world-authoring brief, ADR-090).
//
// What was here before was three octaves of noise and nothing else: no ridge, no valley, no basin,
// no river, no pond. Every generated world was the same rolling dune field at a different seed, and
// every slope-and-altitude rule the ecology is written in had almost nothing to bite on.
//
// The model is not new. `WorldMap` already describes geography properly -- a noise base plus an
// ordered list of `Feature` stamps (ridge, valley, river, flat) drawn as polylines in world space,
// which is exactly the vocabulary an authored world uses. What was missing was anything that
// *writes* those features. So this is a generator for an existing format rather than a second
// terrain system: everything it produces can be serialised, hand-edited, reloaded and rendered by
// the code that already exists, and an artist who wants to move a river the generator placed moves
// three numbers in the world JSON.
//
// **What §30 asks for and what this exposes.** Terrain Style, Elevation min/max, Roughness, Valley
// Strength, Ridge Strength, Water Amount, River Frequency, Pond Frequency, Seed -- and nothing
// else. There is not a frequency, an octave count, a lacunarity or a domain-warp distance in
// `TerrainParams`, because a parameter set that exposes those is a noise editor and the brief is
// explicit that it should not be one. Every noise setting is *derived* from the nine numbers below,
// in `terrain_gen.cpp`, where it can be tuned once for everyone.
//
// **How the stages fit together** (the order is load-bearing):
//
//   1. *Shape.* The style and Roughness choose an octave stack and an erosion weighting.
//   2. *Fit.* The stack is measured over the whole map and rescaled so it spans exactly
//      [elevationMin, elevationMax]. `octaveSum` is linear in the amplitudes, so this is an exact
//      one-pass fit rather than an iteration -- and it is what makes Elevation min/max mean metres
//      instead of meaning "roughly, depending on the seed".
//   3. *Landform.* The style's ridges, valleys, basins and mesas are stamped, in real metres,
//      because step 2 already put the ground where it said it would.
//   4. *Drainage.* Rivers are traced by walking downhill on the landform (§31), tributaries join
//      courses already traced, and each course is given a soft trough to run in before its channel
//      is cut, so the water occupies a depression rather than a slot.
//   5. *Standing water.* Sinks -- points the surrounding ground rises away from on every side --
//      become ponds and lakes, and a river that ends inside the map ends in one.
//
// Determinism (§32): every stage is a pure function of `TerrainParams`. The only randomness is a
// `Rng` seeded from `params.seed`, drawn in a fixed order; there is no wall clock, no
// `random_device`, no container whose iteration order is unspecified, and no floating-point
// reduction whose order depends on thread count. Two calls with equal parameters produce maps with
// equal `structuralHash()` and bit-identical heights.

#include "core/error.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::world {

struct WorldMap;

// The five landforms of §30. A style is not a preset of the other parameters -- it decides which
// features get stamped at all -- which is why it is an enum here and a preset below.
enum class TerrainStyle : std::uint8_t {
    RollingHills,
    Valley,
    Basin,
    Mountainous,
    Plateau,
};
[[nodiscard]] const char* terrainStyleName(TerrainStyle style);
[[nodiscard]] std::optional<TerrainStyle> terrainStyleFromName(std::string_view name);
// Every style, in menu order, so a UI and a test iterate the same list.
[[nodiscard]] std::vector<TerrainStyle> terrainStyles();

struct TerrainParams {
    std::string name = "terrain";
    TerrainStyle style = TerrainStyle::RollingHills;
    std::uint32_t seed = 1;
    float extent = 400.0f;        // metres across; the map is square and centred on the origin

    // The height the *landform* spans, in metres. River beds and pond floors cut below
    // `elevationMin` by their own depth, which is deliberate: the range describes the ground you
    // walk on, and a channel is a hole in it.
    float elevationMin = 0.0f;
    float elevationMax = 48.0f;

    float roughness = 0.45f;      // 0 smooth and broad .. 1 broken and detailed
    float valleyStrength = 0.5f;  // how deeply the style's valleys and basins are cut
    float ridgeStrength = 0.5f;   // how high its ridges stand
    float waterAmount = 0.40f;    // how wide and deep water is where there is any; 0 = a dry world
    float riverFrequency = 0.40f; // 0 none .. 1 a main river and three tributaries
    float pondFrequency = 0.35f;  // 0 none .. 1 sinks everywhere that will hold water

    [[nodiscard]] Result<void> validate() const;
    // Changes whenever the generated map could change. Used to skip regeneration, and the cheapest
    // half of the determinism test: equal parameters must hash equal.
    [[nodiscard]] std::uint64_t structuralHash() const;
    [[nodiscard]] nlohmann::json toJson() const;
    // Every field defaults, so `{"style": "basin"}` is a legal block meaning "a basin, and the rest
    // as usual". A recipe that demanded nine numbers before it would load is a recipe nobody edits.
    // `extentHint` is the world's size when the caller already knows it, so that a block which does
    // not state an elevation range gets the preset's range scaled to the world it is for.
    [[nodiscard]] static Result<TerrainParams> fromJson(const nlohmann::json& j,
                                                       float extentHint = 400.0f);
};

// The starting point for a style: the parameter set that makes that landform read as itself.
// §30 asks for presets and this is them -- an artist picks "mountainous" and gets mountains, then
// moves Roughness rather than discovering that mountains also need the ridge strength at 0.8.
// `seed` and `name` are left at their defaults for the caller to set.
//
// `extent` scales the preset's elevation range and is written into `extent`. It is scaled *linearly*
// with the width of the world, which is not geography -- a region twice as wide is not twice as tall
// -- but is what keeps the slope distribution, and therefore every slope rule in the ecology, the
// same across worlds of different sizes. The density benchmark rungs are the same world at 420 m and
// 840 m, and a 840 m rung with a 420 m world's relief is a plane.
[[nodiscard]] TerrainParams terrainPreset(TerrainStyle style, float extent = 400.0f);

// The world map for these parameters.
//
// The returned map has no `BiomeSet`: biomes are a separate authoring axis, the composer has its
// own five-biome vocabulary that its scatter layers are written against (`composerBiomes`), and a
// generator that installed a set of its own would be the second place the two could disagree. The
// caller assigns `map.biomes` and calls nothing further -- the map comes back `prepare()`d and
// `validate()`-clean.
[[nodiscard]] WorldMap generateTerrain(const TerrainParams& params);

} // namespace avgen::world
