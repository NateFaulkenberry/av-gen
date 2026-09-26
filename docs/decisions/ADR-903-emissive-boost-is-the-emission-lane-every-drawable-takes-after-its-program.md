# ADR-903: `emissiveBoost` is the emission lane every drawable of a node takes, after its program

**Status:** Accepted
**Date:** 2026-09-26
**Follows:** ADR-179 (a program that asserts emission owns it), ADR-343 (the day/night cycle scales
named nodes' boost), ADR-703 (the FXL lanes, which multiply after the program)
**Found by:** Glowmere Valley 3's first heartbeat (F23; `docs/glowmere-valley-3/07-technical.md`
§7.7), and the owner's instruction in the revision brief: "Fix the recurring emissiveBoost issue.
Do not preserve backwards compatibility for old scene behavior solely to avoid changing the look of
other scenes."
**Implemented by:** `ObjectUniforms::emission` (`src/rendering/scene_renderer.hpp`,
`shaders/common.wgsl`) and `emissionLane` (`shaders/pbr_shade.wgsl`); `Entity::emissionGain`,
`ProceduralGeometry::emissionGain`/`emissionHue`, `SdfObject::emissionGain`, filled by the three
renderers; `Composition::nodeEmissiveBoost` and the node pass of `Composition::applyParameters`
(`src/scene/composition.cpp`); `GltfScene::update`; the path tracer's snapshot.
**Tests:** `tests/rendering/test_emission_lanes_gpu.cpp`:
- "emissiveBoost multiplies a node's final emission after its program, for every drawable kind"
  (and its control section, "the old lane -- the material's intensity -- cannot reach a
  program-lit surface")
- "A route onto emissiveBoost reaches the pixels, and at rest changes none"
- "The emission lane composes with an FXL Glow: the gains multiply"

`tests/unit/test_emission_lanes.cpp`: "emissiveBoost lands on every drawable a node owns, after the
program, and nowhere else"; "A route onto emissiveBoost reaches the lane the same frame".

Re-pinned: `test_composition.cpp` "visibility, emissive boost and roughness scale" and the grove's
part gains, and `test_gltf_scene.cpp`, which asserted the old mechanism (below).

## Context

Every node registers `nodes/<n>/emissiveBoost`, and the panel, the route editor and the timeline all
accept it. The composition applied it in one place: `e.material.emissiveIntensity =
restEmissive[k] * emissiveBoost`, for the **entities** of the node. So it did nothing on:

- **a procedural node**, or any of its material parts: they draw through `scene.procedurals`, and a
  procedural node has no entities;
- **an SDF node**, a **terrain's scatter layers**, and a **particle node**, for the same reason;
- **any program-lit surface, even an entity's**: a program that writes emission replaces the
  material's, and `emissiveIntensity` is discarded (ADR-179).

A route onto it bound, reported nothing, and changed nothing. The known victims:

| Scene | What the boost was supposed to do | Kind |
|---|---|---|
| Glowmere Valley 2 and every project on its scenes (multicam, song, atmospherics, `ufo-stack`, the `_diag-water-*` and `_pre-defects` probes) | `music.downbeat` x0.10 and `music.drop` x0.18 on `elder-2-gills`; `music.section` x0.06 on `elder-2-cap` | procedural, program-lit |
| `glowmere-stylized`, `glowmere-lyrics` | the same routes on `elder-filaments` (no program) and `elder-crown` (`paintedCrown`) | procedural |
| `tree-of-life-floating-island-night` | stars at 2.2 / 2.2 / 2.1, motes at 1.9 | procedural |
| the eleven `_ck-*` check projects beside it | stars at 1.2-1.25, motes at 1.15 | procedural |
| `tree-of-life-ocean-world` | ADR-343's cycle dims the named star nodes by multiplying the boost | procedural |
| Glowmere Valley 3 | the first heartbeat (F23) | procedural, program-lit |

`procedural/<n>/parts/<k>/emissiveGain` had the same defect (a multiplier on `emissiveIntensity`),
and so did `GltfScene`'s `material/emissiveBoost`.

## Decision

- **One lane.** `ObjectUniforms` gains `emission` (x = gain, y = a hue rotation in turns, ADR-905),
  the third of the four spare vec4s ADR-703 recorded. Every draw of every kind fills it: the entity
  renderer from `Entity::emissionGain`, the procedural renderer from
  `ProceduralGeometry::emissionGain`/`emissionHue` (every part of an asset is its own object), the
  SDF renderer from `SdfObject::emissionGain` (raymarched and meshed).
- **Applied after everything that makes the emission.** `shadeSurface` runs the program, the
  emissive texture and the FXL lanes, then applies the lane to the finished emission (the object's
  own, the FXL-added glow and rim) and to the Fresnel rim; the waves then amplify what the surface
  actually emits, and the fog comes last. `pbr.wgsl` applies it to a tree's conducted energy.
- **The composition writes the lane every frame** from `nodeEmissiveBoost(node)` -- the parameter's
  final times ADR-343's star or glow factor:
  - entities: the boost (times a nested scene's own lane);
  - a procedural node's part k: the boost x `procedural/<n>/parts/<k>/emissiveGain`, which moves here
    from the material lane for the same reason;
  - SDFs: the boost; a terrain's chunks: the boost; its scatter layers: the boost x the layer's own
    gain (ADR-905);
  - particles, which have no program: `emissive` x the boost (reset from the rest every frame, so it
    does not compound).
- **The old path is deleted:** the `emissiveIntensity` write, `NodeRange::restEmissive` and its six
  capture sites, and `GltfScene`'s rest intensity. `GltfScene` writes the same lane.
- **Not inherited.** Like `roughnessScale` and `opacity`, the boost is the node's own. A group has
  nothing of its own to boost.

**Why not the FXL gain lane,** which also multiplies after the program:

1. The raymarched SDF pass does not bind the FXL records (`sdf_raymarch.wgsl`), so it could not reach
   an SDF.
2. FXL is rebuilt from effect instances every frame and gated on a flag (`fxA.z`). A node parameter
   in it is a second writer of an effect-owned lane, and every boosted node would open the FXL gate.
3. ADR-905's per-layer hue needs a lane FXL does not have.

The two compose by multiplication: FXL's gain and tint act on the surface's own emission, its added
light joins it, and then the object lane multiplies all of it and adds its hue to FXL's hue cycle.
So a Glow of gain 2 on a node boosted x3 emits x6.

## Consequences

**On pixels, in the unit's own scene** (`test_emission_lanes_gpu.cpp`, the emission target, boost 1
-> 3 on each node): program-lit procedural x2.98, program-less procedural x3.00, mesh x3.00, SDF
x3.01 (main: x1, x1, x3, x1). The control arm, the old lane (the material's intensity x3): the
program-lit box x1.00, the plain box x3.00 -- the old mechanism could not have reached a program-lit
surface. A route `1 + 2 x signal` on the program-lit node: x2.98 with the signal up,
byte-identical at rest, every other node within 1%. With an FXL Glow of gain 2 on top: x5.99.

**The scenes that change,** rendered by main 0b623b88's `avgen` and this branch's from exports of
each tree (same assets, same audio), 640x360, tier high, supersample 1; display-referred PNGs, Rec.709
luminance; "changed" is a pixel whose summed |difference| exceeds 6 of 765:

| Scene, second | What changed | Measured |
|---|---|---|
| `tree-of-life-floating-island-night`, 5 s | the stars and motes take their authored 2.1-2.2x and 1.9x | 0.30% of pixels, all stars; those pixels x1.22 (bloom and the tone curve compress the 2x); the tree, island and sky identical |
| `tree-of-life-ocean-world`, 0 s (midnight) | nothing: the cycle's star factor is 1 at midnight | byte-identical |
| same, 60 s (sunrise, factor 0.28) | the cycle now dims the procedural stars | 0.10% of the sky's pixels, x0.88 |
| same, 197 s (twilight, factor ~0.55) | the same | 0.18% of the sky's pixels, x0.91 |
| `glowmere-stylized`, 10-14 s at 5 fps, close on the elder | the downbeat route on `elder-filaments` is live | at the downbeats (11.6 s, 13.6 s) the filaments' pixels x1.022, decaying over the next frames; with the three boost routes removed from the same build, the two differ only there |
| Glowmere Valley 2, 176-182 s at 10 fps, close on the elder | the downbeat route on `elder-2-gills` is live | at the downbeats (177.3, 179.2, 181.0 s) the gills' lamellae x1.017-1.024 on 0.13-0.32% of the frame, decaying; main (with the routes) matches this build without them everywhere on the elder -- main's routes never reached a pixel |

The drop route (+0.18 on the gills) and the section route (+0.06 on `elder-2-cap`,
`elder-crown`) are live by the same path; no drop or section event fell in the windows rendered.
Before/after strips of every row are in `~/Desktop/av-gen-review/emission-adr903-906/`.

- **The GV2 family's elder routes are live.** `elder-2-gills` pulses +10% on each downbeat and +18%
  on a drop, and `elder-2-cap` +6% per section, as their authors wrote in 2026-09. GV3's project
  carries no route onto either (it removed them), so GV3 does not change from this.
- **The stylized elder's filaments pulse** on downbeats and drops, and its crown on sections.
- **The tree-of-life night film's stars and motes** are 2.1-2.2x and 1.9x brighter, as authored,
  and the eleven check projects' 1.15-1.25x. **The ocean world's day/night cycle** now dims its
  procedural stars through the day instead of only hiding them at zero.
- **A particle node's boost** multiplies its particles' emission. No shipped scene sets one.
- **Meshes do not change.** For an entity with no program the lane is the old multiplier moved:
  `(colour x intensity x texture) x gain` instead of `colour x (intensity x boost) x texture`, rim
  included. So the tree-of-life films' boosted tree meshes, the farm animals GV2 holds at boost 0,
  and the abduction's `glow-rise`/`glow-fade` (a scenario `set` on the target's boost) draw as they
  did. What is new for meshes is reach: a program-lit mesh now answers its boost too.
- **A tree's conducted light (ADR-376) takes the lane** (`pbr.wgsl`), since it is emission. No
  shipped scene has tree energy switched on, so no picture moves from this.
- **Water** is not a `shadeSurface` draw and takes no lane: a terrain's boost reaches its ground
  chunks and its scatter, not its water (the water shader belongs to agent/water).
- **`ObjectUniforms` is 464 bytes** (was 448; the slot stride stays 512). The WGSL layout guard
  reads both sides.
- **The path tracer** folds the entity's and the procedural object's gain into the material's
  intensity (it shades without programs, so the two are the same thing there).
- Two tests asserted the mechanism rather than the effect and were re-pinned to the lane:
  `test_composition.cpp`'s boost section (now `emissionGain == 3` with the material's intensity
  untouched) and the grove's part gains (the emitted product is still 0 and 6), and
  `test_gltf_scene.cpp`.
