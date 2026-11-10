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
#include "scene/tile_dynamics.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace avgen::scene {

// ADR-1965: the water bodies and dry valleys set into the land, each at its own level.
inline constexpr std::size_t kMaxTileRivers = 4;
inline constexpr std::size_t kMaxTileRiverPoints = 32;   // per river
inline constexpr std::size_t kMaxTileRiverPool = 64;     // all the rivers' points together (the GPU block)
inline constexpr std::size_t kMaxTilePonds = 8;
inline constexpr std::size_t kMaxTileValleys = 8;       // ADR-1992 (CPD-R3): 4 -> 8
inline constexpr std::size_t kMaxTileValleyPool = 64;    // all the valleys' points together (ADR-1992: 32 -> 64)
// ADR-1992: the raised land -- ridges and rises -- added to the land before any carving.
inline constexpr std::size_t kMaxTileRidges = 8;
inline constexpr std::size_t kMaxTileRidgePoints = 32;   // per ridge
inline constexpr std::size_t kMaxTileRidgePool = 64;     // all the ridges' points together
inline constexpr std::size_t kMaxTileRises = 8;

// A river: a polyline from its source (first point) to its mouth (last). Within `valleyWidth / 2` the land eases to the
// water's level with the smoothstep profile (no walls); within `width / 2` the tiles are river water. The level eases
// down from the source to the mouth (sea level): with `bedDepth` < 0 (the default) the whole river sits at the sea
// floor's level, as the original single river did; with `bedDepth` >= 0 the source is `sourceLevel` (metres) or, when
// unset, "auto" -- the land at the source minus `bedDepth` -- so an upland stream runs at its own height.
struct TileRiver {
    std::string name;
    std::vector<glm::vec2> points;
    float width = 8.0f;
    float valleyWidth = 200.0f;
    float bedDepth = -1.0f;
    std::optional<float> sourceLevel;
};
// A pond: water at `level` (metres; unset = "auto", the basin rim's lowest land minus 1 m, so a pond can sit up on a
// plateau) within `radius`, its shoreline made irregular by `shoreNoise` (0..1); around it the land eases down to the
// level over `basinWidth` with the same profile.
struct TilePond {
    std::string name;
    glm::vec2 centre{0.0f};
    float radius = 20.0f;
    float basinWidth = 40.0f;
    std::optional<float> level;
    float shoreNoise = 0.0f;
};
// A dry valley: a polyline the land sinks along over `valleyWidth`, by `depth` metres -- or, with `depthFraction` > 0,
// by that fraction of the local relief (the land's height above the shore) -- never below the shore's top, and with no
// water.
struct TileValley {
    std::string name;
    std::vector<glm::vec2> points;
    float valleyWidth = 120.0f;
    float depth = 6.0f;
    float depthFraction = 0.0f;
};

// ADR-1992: a ridge -- land raised by `height` metres along a polyline, falling to nothing at `width / 2` from it with
// the smoothstep profile. Where its profile is above about half (its core) it is land, even over the sea.
struct TileRidge {
    std::string name;
    std::vector<glm::vec2> points;
    float height = 20.0f;
    float width = 120.0f;
};
// ADR-1992: a rise -- land raised by `height` within `radius` of `centre`, easing to nothing over `edge` metres beyond
// it. Its core is land: in the sea, an island.
struct TileRise {
    std::string name;
    glm::vec2 centre{0.0f};
    float radius = 40.0f;
    float height = 20.0f;
    float edge = 40.0f;
};

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
    // ADR-1965: the rivers, ponds and dry valleys (see TileRiver, TilePond, TileValley). The CP-A single `river` key is
    // gone (ADR-441: no alias): its river is `rivers[0]` with the default bedDepth. The flight follows a river by its
    // index or name.
    std::vector<TileRiver> rivers;
    std::vector<TilePond> ponds;
    std::vector<TileValley> valleys;
    // ADR-1992: ridges and rises, added to the land's height (metres, not scaled by heightScale) before the valleys,
    // rivers and ponds cut it; their cores make land in the sea.
    std::vector<TileRidge> ridges;
    std::vector<TileRise> rises;
    // Resolved by resolveWater() (not authored, not saved, not hashed): each river's source level and length, each
    // pond's level. tileSample computes any it is missing, slowly, so an unresolved spec is still right.
    struct Resolved {
        std::vector<float> riverSource;
        std::vector<float> riverLength;
        std::vector<float> pondLevel;
    };
    Resolved resolved;
    bool waterResolved = false;
    // Scales the land's relief above the shore (hills, crowns, the valley's banks): the art's lever on how tall the
    // world stands without retuning the shapes. 1 = as authored.
    float heightScale = 1.0f;

    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] std::uint64_t hash() const;
    // ADR-1965: works out the "auto" levels (on the static ground) and the rivers' lengths. Idempotent.
    void resolveWater();
    // The river named `name`, or "river" / "river0".."river3" / a bare index: -1 when there is none.
    [[nodiscard]] int riverIndex(const std::string& name) const;
};

inline constexpr std::size_t kMaxTilePathPoints = 16;

// What one tile is.
struct TileSample {
    float height = 0.0f; // metres: the top of the tile
    float water = 0.0f;  // 1 = sea
    float shore = 0.0f;  // 1 = on the shoreline, falling to 0 a shore band away
    float flat = 0.0f;   // 1 = a crown (a flattened hill top)
    float path = 0.0f;   // 1 = on the path
    float crest = 0.0f;  // ADR-1953: the crest channel, 0..1 (0 on a static ground)
    float landing = 0.0f; // ADR-1960: how lit a tile is by a nearby landing, 0..1 (the springs' envelope, not their swing)
    // ADR-1965: which body the water is: the river and pond shares of `water` (the sea's is water - river - pond), and
    // the distance downstream along the nearest river (metres), for its drift.
    float river = 0.0f;
    float pond = 0.0f;
    float riverS = 0.0f;
};
// ADR-1965: the body a tile's water belongs to, as the record carries it (0 sea, 1 river, 2 pond): the larger share.
[[nodiscard]] inline int tileWaterClass(const TileSample& s) {
    if (s.river >= 0.5f * s.water && s.river > s.pond && s.river > 0.0f) return 1;
    if (s.pond >= 0.5f * s.water && s.pond > 0.0f) return 2;
    return 0;
}

// ADR-1965: the resolved water levels the kernel is packed with (metres, absolute) and a river's length (metres):
// river `i`'s source level, its mouth level (the sea floor's), and pond `i`'s level.
[[nodiscard]] float tileRiverSourceLevel(const TileTerrainSpec& spec, std::size_t i);
[[nodiscard]] float tileRiverMouthLevel(const TileTerrainSpec& spec);
[[nodiscard]] float tileRiverLength(const TileTerrainSpec& spec, std::size_t i);
[[nodiscard]] float tilePondLevel(const TileTerrainSpec& spec, std::size_t i);

// The tile at generator-space centre (x, z), in metres: the static ground.
[[nodiscard]] TileSample tileSample(const TileTerrainSpec& spec, float x, float z);
// ADR-1953: the base at time t, with the breathing and the rolling (no waves, no rings). With `dyn` inactive this is
// tileSample(spec, x, z) exactly.
[[nodiscard]] TileSample tileSample(const TileTerrainSpec& spec, float x, float z, float t, const TileDynamics& dyn);
// ADR-1953: the waves and the rings at (x, z, t) on tile `tile` (its water channels: ADR-1965 weights the waves by the
// body -- sea, river, pond -- and drifts a river downstream), for a ring of `cellSize` metres (the aliasing fade).
// Returns (height offset, crest). `events` is eventsAt(t), passed so a caller sampling many tiles finds it once.
[[nodiscard]] glm::vec2 tileMotion(const TileDynamics& dyn, const std::vector<TileEvent>& events, float x, float z,
                                   float t, const TileSample& tile, float cellSize);
// ADR-1960: a tile record's emissive.w, as the kernel and the contour pass pack it: crest (0..1) + 2 * the contour side
// mask (ADR-1970, 0..15) + 32 * the landing glow in 255ths. The `tileContour` op decodes it.
// ADR-1965: and the water body's class above those: + 8192 * class (0 sea, 1 river, 2 pond).
inline constexpr float kTileClassStride = 8192.0f;
[[nodiscard]] inline float tilePackedEmissiveW(float crest, unsigned mask, float landing, int waterClass = 0) {
    const float l = std::round(std::clamp(landing, 0.0f, 1.0f) * 255.0f);
    return std::clamp(crest, 0.0f, 1.0f) + 2.0f * static_cast<float>(mask) + 32.0f * l +
           kTileClassStride * static_cast<float>(waterClass);
}
// ADR-1960: the landing glow at (x, z, t): each ringing landing's envelope -- its spring's decay without the swing,
// after a 60 ms rise, over the tighter glowRadius -- summed and clamped to 0..1. It settles exactly as the tile does.
[[nodiscard]] float tileLanding(const TileSpringSpec& spec, const std::vector<TileEvent>& springs, float x, float z,
                                float t);
// ADR-1954: the landing springs' height offset at (x, z, t) from `springs` (springsAt(t)).
[[nodiscard]] float tileSpring(const TileSpringSpec& spec, const std::vector<TileEvent>& springs, float x, float z,
                               float t);
// ADR-1953: the whole ground at (x, z, t) for a ring of `cellSize`: the base plus the motion, the crest set.
[[nodiscard]] TileSample tileSampleMoving(const TileTerrainSpec& spec, const TileDynamics& dyn, float x, float z,
                                          float t, float cellSize);
[[nodiscard]] Result<void> validateTileDynamics(const TileDynamics& dyn);
// JSON: `environment.tileDynamics`. Every key optional; an unknown key is refused. `live` is never written.
[[nodiscard]] nlohmann::json tileDynamicsToJson(const TileDynamics& dyn);
[[nodiscard]] Result<TileDynamics> tileDynamicsFromJson(const nlohmann::json& j);

// The value noise and fBm the formations are built from, exposed for the parity tests. `n` is in [0, 1).
[[nodiscard]] float tileValueNoise(std::uint32_t seed, float x, float z);
[[nodiscard]] float tileFbm(std::uint32_t seed, float x, float z, int octaves);
// Distance from (x, z) to the path polyline (a large number when there is no path), and to river `river`'s.
[[nodiscard]] float tilePathDistance(const TileTerrainSpec& spec, float x, float z);
[[nodiscard]] float tileRiverDistance(const TileTerrainSpec& spec, float x, float z, std::size_t river = 0);

// The tile under a generator-space point, and its centre, for a lattice of `pitch` metres.
[[nodiscard]] glm::ivec2 tileIndexAt(float pitch, float x, float z);
[[nodiscard]] glm::vec2 tileCentre(float pitch, glm::ivec2 index);
// The height of the ground under (x, z): the top of the tile that contains it.
[[nodiscard]] float tileHeightAt(const TileTerrainSpec& spec, float pitch, float x, float z);
// ADR-1953: the same on the moving ground at time t (the near ring: no aliasing fade below a 1 m pitch).
[[nodiscard]] float tileHeightAt(const TileTerrainSpec& spec, const TileDynamics& dyn, float pitch, float x, float z,
                                 float t);

// JSON (the "tiles" object of a "tiles" generator). Every key is optional; an unknown key is refused.
[[nodiscard]] nlohmann::json tileTerrainToJson(const TileTerrainSpec& spec);
[[nodiscard]] Result<TileTerrainSpec> tileTerrainFromJson(const nlohmann::json& j);

} // namespace avgen::scene
