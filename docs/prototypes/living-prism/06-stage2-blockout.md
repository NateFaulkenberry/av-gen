# The Living Prism 06: Stage 2, the blockout

- **Spec:** `04-scene-spec.md` §13 Stage 2. **Branch:** `scene/living-prism`.
- **Owner media:** `~/Desktop/av-gen-review/82-living-prism/stage2/`:
  - six stills (`still-1` to `still-6`, 1080p, at the route's stations);
  - `route-loop-95s.mp4`, one whole loop at 1080p30.

## 1. What was built

**Build:** `python3 examples/living-prism/build.py`.
- Blender runs for about 10 s, then the scene is written.
- `--preview` adds Workbench layout stills from the camera stations.
- Every mesh is generated, so there are no external assets and no licences to record.
- The scene (`examples/living-prism/scene/`) is gitignored. The live project `examples/living-prism/living-prism.json`
  is committed and is a pure function of the scripts.

| Element | Construction | Where (`layout.py`) |
|---|---|---|
| **Shell** | the surface of an "air" volume (an ellipsoid chamber 29 x 27 x ~12.5 m, three alcoves and a bent back passage), voxel-remeshed, smoothed, displaced, decimated to 32k tris, normals inward. Casts no shadow | fills the frame in every direction: no world edge is reachable from the route |
| **Floor and pool** | a terrain node; flat base 0.5 m, seven mounds, one two-point river feature cut to 1.4 m (water at y = 0, about 15 x 10 m). The pool uses ADR-1286's cinematic water (SSR, transmission) as a dark mirror | centre |
| **Spire** (primary) | a 6 m upright fan of ten hex prisms on a faceted rock knoll | on a shelf behind the pool, framed by the arch |
| **Fan** | six tabular (flattened) blades leaning 30-66° over the water | left bank |
| **Geode** | a faceted rock half-shell with about 190 small prisms lining its cavity, the opening turned to the pool | right bank |
| **Chandelier** | eight prisms hanging points-down from a rock pendant | the roof over the right half of the pool |
| **Ember** (warm accent) | a stout terraced prism (three growth plates) and a side prism | right foreground, low |
| **Arch** (focal structure) | an asymmetric bevelled curve (a heavy left leg, the crown off centre) with closed caps, voxel-remeshed and faceted, and seven patches of druse on its underside | over the far shore, its legs at the waterline |
| Druse clusters | 3 variants x 10 instances | banks and wall bases |
| Shards | 3 variants x 14 instances, leaning | glints and scale cues |
| Boulders | 4 faceted variants x 14 instances | framing the pool and the walls |

**Silhouettes:** five distinct ones (upright fan, diagonal blades, round hollow, inverted hang, squat block), plus the
arch.

**Visual language:** every surface is flat-shaded, so crystal and rock share one planar vocabulary at two scales.

**Budget:**

| | Count |
|---|---|
| Procedural nodes | 26 |
| Instances | 61 |
| Mesh triangles | 50.5k, of which the shell is 32k and crystal 8.4k |
| Terrain | about 74k tris (3 x 3 chunks of 64 x 64); most of it lies outside the shell and is occluded |
| Lights | 7: one shadowed spot (the shaft, placed above the roof, which casts no shadow), the passage light and five formation glows |

**Materials:**
- Blockout only: dark hued rock and floor programs (two-tone noise), crystals as dark bodies with an emissive colour
  from the Ultraviolet look.
- No palette bindings yet (Stage 3).

## 2. Camera route

- **Shape:** eight stations, each a position and a look-at target, on two closed Catmull-Rom curves sharing one
  parameter. The parameter is re-timed to constant speed along the position curve.
- **Baking:** six looping timeline sources (0.25 s keys) drive `camera/position` and `camera/target`, so seek equals
  play and the loop needs no special case.
- **Loop:** 95.21 s, 48 bars of the test track. Live, it simply loops.
- **Lens:** fov 52° vertical. No fisheye, no roll.

**Measured on the baked route at 30 fps:**

| | Value |
|---|---|
| Speed | 0.39 m/s, constant |
| View turn rate | p50 2.7°/s, p95 6.3°/s, max 7.5°/s |
| Pitch | -18° to +25° |
| Seam | the first and last positions are identical, C1 |

**The stations** (time, then what the frame shows):

| Still | t (s) | Composition |
|---|---|---|
| 1 | 0.0 | **Master:** the pool, the arch and the spire through it, the blue passage behind; the fan at left, the chandelier at top right |
| 2 | 13.4 | Front left, rising: across the pool to the geode; the arch and the spire |
| 3 | 20.9 | Left bank: the geode and the ember across the water, both reflected |
| 4 | 36.9 | Low by the arch's left leg: the spire three-quarter against the blue passage |
| 5 | 59.7 | Low at the near shore: up at the chandelier, the geode below it, the arch at left |
| 6 | 71.9 | Right bank: the fan across the water, the arch and the spire, all mirrored |
| (7, 8) | 80.8, 89.7 | High right looking down onto the reflections; the return to the master |

## 3. Iterations (what the renders showed, and what changed)

1. **Workbench layout pass (Blender, before any GPU time):**
   - The arch mesh failed: its open bevel ends broke the voxel remesh, which left its crystal crust floating in the
     air. Fixed with closed caps.
   - The geode faced the wall.
   - The chandelier hung too high to enter any frame.
   - The arch's crust was a uniform sprinkle; it now grows in seven patches.
   - The chandelier crossed the fan in the master; it moved to the right half of the roof.
   - A foreground boulder blocked the pool.
2. **First engine render:** the volume washed the frame to lavender (density 0.025, volumetric lights), and the water
   was choppy. Fixes: density down to 0.004, calmer ripples, lower IBL.
3. **Second engine render:**
   - The terrain drew its default biome colours (green) near the camera. A terrain without a program gets a
     generated biome program, so the floor now has its own program.
   - A glow light left at the chandelier's old position showed as a lone dot in the haze. Formation lights are now
     derived from `layout.py`.
   - The shaft spot turned the spire pastel; its intensity is now 260.
4. **The route clip:**
   - The spire dominated too much of the loop.
   - Station 4 now looks up at the chandelier and the geode.
   - Station 5 looked at an unlit wall (dead space) and now turns toward the arch.
   - Station 2 passed too close to a blade.

## 4. First perf read

PERF_PLACEHOLDER

## 5. Known weaknesses (for Stages 3 and 5)

- **The crystal reads as pastel plastic:**
  - flat albedo plus one emissive colour;
  - no facet-dependent light, no seams, no internal depth.
  - Stage 3 (programs, refraction once P1-4 lands) is where this is decided.
- **The upper third of several frames is unlit roof:** it is near-black and only marginally "space". Stage 3's
  lighting (roof seams, the shaft's haze) has to give it texture without lighting it flat.
- **The pool's reflection is still a little rippled:** it reads as water, but a stiller mirror may suit the
  reference better. To tune in Stage 3.
- **The arch reads as a portal** from the front stations, because its asymmetry shows only obliquely. If the owner
  wants a more natural bridge, the curve's control points are the lever.
- **The shell's seams:** they do not show at this lighting level. Shell normals and voxel remnants must be rechecked
  once rock gets real light.
- **Coverage:** not yet measured. The pool takes roughly 20-30% of the frame in the low stations, more than the spec's
  10-20%; translucent-candidate crystal (the spire and the fan) roughly 5-10% [est from the stills]. A per-material
  coverage measurement comes with Stage 3's materials.

## 6. Engine limitations found

**The cinematic water's SSR has no quality-ladder lever.**
- `LiveQualityRung` (`src/app/interactive_resolution.hpp`) has no field that reaches `WaterSettings` (`ssr`,
  `ssrSteps`, `reflectionRays`).
- The pool's reflection cost is therefore fixed at every rung.
- The measured cost is in section 4.
- The smallest extension is a rung field (for example `waterSsrScale`, precedent `astralTier`, ADR-1222) that caps
  `ssrSteps` and `reflectionRays` and turns SSR off at the lowest rung. P1-5 (material SSR) needs the same lever, so
  the two should be one change by the engineering agent.
