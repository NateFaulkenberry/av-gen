#pragma once

// Musical Geography's ground (ADR-1950): one surface, read by everything.
//
// The landscape is a lattice of square tiles. Tile (ix, iz) has its centre at ((ix + 0.5) * pitch, (iz + 0.5) * pitch)
// and ONE height: its top is flat, so the ground is stepped, and "the tile under a point" is the whole answer to
// "how high is the ground here". The height is a pure function of the tile's centre and the description below. The GPU
// evaluates it in the "tiles" generator kernel (shaders/generator.wgsl, `tileSample`), every frame, for the window of
// tiles around the camera; the CPU evaluates the same function here, operation for operation, for whatever needs the
// ground without the GPU -- the characters, the camera's clearance and the tests. The two are one rule written twice,
// as ADR-1117's generator mirror is: integer hashes (generatorHash), the same value noise, the same order of
// operations, so they agree to float rounding.
//
// The formations are designed functions, not per-tile noise (brief §4; the owner's decisions of 2026-11-08: rolling
// land, no cliffs, no mesas, no canyon):
//   * the SEA: a slow continent field below a threshold; low, flat ground where the waves (Phase 3) will run;
//   * the SHORE: the band where the continent field crosses the threshold; a gentle rise, no wall;
//   * rolling HILLS, the main landform: domain-warped fractal value noise at long wavelengths, shaped by a smoothstep,
//     rising gradually inland from the shore;
//   * CROWNS: where a hill top passes `crownThreshold`, its excess is flattened, so broad hills carry gentle flat tops
//     (the characters' pads);
//   * the PATH: a polyline band raised by `pathLift`.
// A slow REGION field varies the hills' wavelength and height across the world, so the endless lattice does not
// repeat one landscape.
//
// The tile also carries four channels the material reads (written into the record's random lanes by the kernel):
// water, shore, flat (crown) and path, each in [0, 1]. Its lattice hash goes in the colour's alpha, so a program can
// vary a tile by its world identity, never by its record index (request 1978).

#include "core/error.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <vector>

namespace avgen::scene {

struct TileTerrainSpec {
    std::uint32_t seed = 1;
    // The sea and the shore. `continentWavelength` is the scale of land and sea; `landThreshold` (0..1) of the
    // continent field is the shoreline (lower = more land); `shoreBand` is the continent-field width of the
    // shore's transition (sea floor to shore top); `inlandBand` the width over which the hills rise to full height.
    float continentWavelength = 900.0f;
    float landThreshold = 0.45f;
    float shoreBand = 0.035f;
    float inlandBand = 0.12f;
    float seaLevel = 0.0f;           // metres: the sea's surface (the top of a sea tile at rest)
    float seaDepth = 0.25f;          // metres below the shore's foot
    float shoreRise = 0.5f;          // metres: the land's height at the top of the shore
    // The hills.
    float hillWavelength = 160.0f;   // metres
    float hillHeight = 16.0f;        // metres, at full height
    int hillOctaves = 4;             // 1..6
    float hillWarp = 45.0f;          // metres of domain warp
    float warpWavelength = 260.0f;   // metres
    float hillLow = 0.28f;           // the fBm value mapped to the valley floor (smoothstep edge 0)
    float hillHigh = 0.78f;          // the fBm value mapped to the summit (smoothstep edge 1)
    // Crowns: hill tops above `crownThreshold` (0..1 of the shaped hill) are flattened by `crownFlatten` (0..1).
    float crownThreshold = 0.72f;
    float crownFlatten = 0.85f;
    // The region field: how far, across the world, the hills' wavelength and height wander (0 = not at all).
    float regionWavelength = 2400.0f;
    float regionVariation = 0.3f;
    // The path: a polyline in metres (x, z), at most kMaxTilePathPoints; empty = no path.
    std::vector<glm::vec2> path;
    float pathWidth = 1.0f;          // metres
    float pathLift = 0.35f;          // metres

    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] std::uint64_t hash() const;
};

inline constexpr std::size_t kMaxTilePathPoints = 16;

// What one tile is.
struct TileSample {
    float height = 0.0f; // metres: the top of the tile
    float water = 0.0f;  // 1 = sea
    float shore = 0.0f;  // 1 = on the shoreline, falling to 0 a shore band away
    float flat = 0.0f;   // 1 = a crown (a flattened hill top)
    float path = 0.0f;   // 1 = on the path
};

// The tile at generator-space centre (x, z), in metres.
[[nodiscard]] TileSample tileSample(const TileTerrainSpec& spec, float x, float z);

// The value noise and fBm the formations are built from, exposed for the parity tests. `n` is in [0, 1).
[[nodiscard]] float tileValueNoise(std::uint32_t seed, float x, float z);
[[nodiscard]] float tileFbm(std::uint32_t seed, float x, float z, int octaves);
// Distance from (x, z) to the path polyline (a large number when there is no path).
[[nodiscard]] float tilePathDistance(const TileTerrainSpec& spec, float x, float z);

// The tile under a generator-space point, and its centre, for a lattice of `pitch` metres.
[[nodiscard]] glm::ivec2 tileIndexAt(float pitch, float x, float z);
[[nodiscard]] glm::vec2 tileCentre(float pitch, glm::ivec2 index);
// The height of the ground under (x, z): the top of the tile that contains it.
[[nodiscard]] float tileHeightAt(const TileTerrainSpec& spec, float pitch, float x, float z);

// JSON (the "tiles" object of a "tiles" generator). Every key is optional; an unknown key is refused.
[[nodiscard]] nlohmann::json tileTerrainToJson(const TileTerrainSpec& spec);
[[nodiscard]] Result<TileTerrainSpec> tileTerrainFromJson(const nlohmann::json& j);

} // namespace avgen::scene
