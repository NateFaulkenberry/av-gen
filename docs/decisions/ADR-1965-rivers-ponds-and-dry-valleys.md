# ADR-1965: Rivers, ponds and dry valleys, each at its own level, and the water body's class

- Status: Accepted (Musical Geography CP-D r2, request CPD-R1, 2026-11-10), scene/musical-geography.
- Code:
  - `scene::TileRiver`, `TilePond`, `TileValley`, `TileTerrainSpec::resolveWater` / `riverIndex`, `applyFeatures`,
    `tileWaterClass`, `tileMotion` (`src/scene/tile_terrain.{hpp,cpp}`);
  - the kernel's twin (`tileSample`, `tileMotion`, the class in `cs_generate_tiles`, `shaders/generator.wgsl`);
  - the packing (`packGeneratorTiles`, `src/rendering/procedural_renderer.cpp`);
  - the `waterBody` material input (`material_program.cpp`, `material.wgsl`);
  - the flight's `follow` (`src/scene/flight.cpp`).

## Context

The owner on draft3: "terrain needs more variety: hills, valleys, rivers and ponds". TileTerrain had one river
(16 points) cut down to the sea floor, and `water` was one class. The art agent faked a pond with a wide river
capsule and a dry valley with a 1 m channel, one at a time (`02-requests.md`, CPD-R1).

## Decision

**One surface.** The features are part of the tiles' one rule (`tileSample` and its WGSL twin). Rendering, the
characters' ground (`TileGround`), navigation and the flight all read that rule, so they cannot disagree. Every
feature is a pure function of the spec and the time: seek equals play.

Applied in order on the land part, before the shore mix:

1. **Dry valleys** (`valleys`, at most 4, 32 points together). The land sinks along the polyline over `valleyWidth` by
   `depth` metres, or by `depthFraction` of the local relief above the shore. It never goes below the shore's top, and
   it never makes water.
2. **Rivers** (`rivers`, at most 4, each 2..32 points, 64 together).
   - Within `valleyWidth / 2` the land eases to the water's level with the smoothstep profile. Within `width / 2` the
     tiles are river water.
   - The level eases (smoothstep in arc length) from the source level at the first point to the sea floor at the
     last.
   - `bedDepth < 0` (the default) is the CP-A river: at the sea floor all along. Otherwise the source is `sourceLevel`,
     or `"auto"`: the land at the source, with the rivers before it applied, less `bedDepth`. So an upland stream runs
     at its own height.
3. **Ponds** (`ponds`, at most 8): water at `level` within `radius`.
   - `"auto"` is the lowest land on the basin's rim (32 samples at `radius + basinWidth`), less 1 m, so a pond can sit
     on a plateau.
   - `shoreNoise` (0..1) wobbles the shoreline by up to half the radius.
   - The land eases down to the level over `basinWidth`.

**"Auto" levels are resolved once.** They read the static ground when the spec is loaded (`resolveWater`), so a moving
or modulated ground never moves a pond's or a river's level. The kernel is packed with the resolved values; it never
recomputes them.

**No `river` alias (amended in R2, ADR-441).** R1 shipped the scene file's `river`, `riverWidth` and `valleyWidth` (the
CP-A single river) as an alias for `rivers[0]`, at the art agent's request, while the scene moved over. Once the art
scene wrote only `rivers` (e3f6383f), the alias was removed: those keys are refused like any unknown key. The CP-A
river is `rivers[0]` with the default `bedDepth` (< 0, the sea floor).

**The water body's class.**
- The sample carries `river` and `pond`, the shares of `water`; the sea's share is the rest.
- The record carries the class (0 sea, 1 river, 2 pond: the larger share, `tileWaterClass`) in `emissive.w`, above
  everything else, at × 8192. The `tileContour` op strips it.
- The `waterBody` material input decodes it as (sea, river, pond) one-hot and the class.

**Motion by body.** `tileDynamics.water` sets:
- `riverWaves` and `pondWaves`: multipliers on each wave's `onWater` weight. The sea's is 1.
- `driftAmplitude`, `driftWavelength` and `driftSpeed`: a ripple that travels downstream along each river's arc
  length, on river water only. It counts as motion: `TileDynamics::active()`.

The rings (ADR-1953) reach every water body as before, so a still pond (`pondWaves` 0) still rings on onsets.

The flight's `follow` takes `"path"`, `"river"` (the first), `"riverN"`, an index, or a river's name. A river that is
not there is refused when the route is resolved.

## Consequences

- The art scene writes its CP-A river as `rivers[0]`, which keeps the sea-floor bed by default.
- The kernel's uniform block grows by 79 lanes (1.26 KB). The loops are bounded by the counts, and each river and
  valley is skipped outside its bounding box (widened by half its valley).
- Characters do not follow polylines today; a river is ground to them like any other water.
- Tests:
  - `test_tile_terrain` [adr1965]:
    - the upland stream (auto source, monotonic fall, set level, the CP-A default);
    - the pond (auto level against the rim, class 2, no wall, set level);
    - the dry valley (depth, fraction, never below the shore, dry);
    - reading, writing and the caps;
    - the waves by body and the drift.
  - `test_tile_terrain_gpu`: the kernel's ground, channels and class against the CPU, still and moving, on a world
    with a stream, two ponds and two valleys. The class is checked in the general parity test too.
  - `test_material_gpu`: `waterBody` parity.
  - `test_flight`: following a river by name and index.
