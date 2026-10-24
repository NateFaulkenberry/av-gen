# Aurora Borealis 11: pass 2 research, landscape and rendering (Research Agent A)

- **Scope:** the pass-2 brief's Subagent A (`10-brief-pass2.md` §3-§9), from the rendering side. This is research
  only: no production code changed. Agent B's `12-research-pass2-signals.md` covers the signals; this file says
  which signal drives each effect only where the technique depends on it.
- **Build:** branch `scene/aurora-borealis`, HEAD 44aefe92, the worktree's Release `build/release/src/avgen`. That
  binary was built at 12:00 from c661cabe plus the 02d93bf6 working tree. 44aefe92 adds documents only. One build
  served every comparison here.
- **Media:** `~/Desktop/av-gen-review/aurora-borealis/pass2-research-a/`, in three folders:
  - `baseline/`: six stills and a contact sheet;
  - `probes/`: the probe stills, sheets and clips, numbered as in this file;
  - `probe-generator/`: the probe copy of `build.py`, `world.py`, `look.py` and `music.py`, with every change marked
    `PROBE`.
- **Throwaway probes:** each probe was a patched copy of the generator. It wrote into the gitignored
  `examples/aurora-borealis/scene/`, and those files are deleted.

**Measurement caveat: every timing in this file was taken on battery.** The machine stayed on battery for the whole
session (`pmset`: "Battery Power", powermode 0), so the brief's AC-power condition is **not met**. The baseline
timings fall inside pass 1's AC range (11.5-14.8 ms p50), so I report them. Treat each delta as interleaved A/B on
one build with a noise floor of about ±0.2 ms, not as an absolute. **The live run was skipped:** it means nothing on
battery.

## 0. Summary

| Effect | Recommended technique | Engine change | Cost at 1080p Ultra (headless GPU) |
|---|---|---|---|
| **1. Composition** | A rounded-triangle river loop with long, nearly straight reaches and broad meanders (§2) | none (generator only) | GPU 10.6-11.3 ms vs 11.5-11.8 baseline (more land, less sky); CPU p50 1.3 → 1.6 ms if `chunkSize` is 400 |
| **2. Edge glow** | Bake the edge mask into `TEXCOORD_0` (u = metres to the facet's chosen ridge edge, v = that edge's importance). The program draws a pixel-width line from `uv / footprint` | **none.** Proven in probe F: the `uv` input reaches unlit programs on GLB meshes. The tool `meshes.py` needs an optional uv argument | **+0.1 ms** (10.61/10.62 → 10.75/10.71) |
| **3. Flowing water** | Give the river a real speed (`terrain.flow.speedOverride`), raise `water/flowSpeed`, keep ripple tiny, and lengthen and speed up the ADR-1643 streaks | **none** (tuning). Root cause found: the river's flow speed is about **0.02 m/s** | ≈0 (11.68 vs 11.61 at t140; t50 noisy) |
| **4. River pulse** | An ADR-207 world wave effect: `groundPulse`, `directional`, source `camera` dropped to the water by `groundOffset`, direction `cameraVelocity`. It lights the water and the lowest bank in one record, and two instances give B's two-slot ring | **none.** Proven in probe 06 | **+0.05 ms** (11.93/12.00 → 12.00/12.00) |
| **5. Celestial sky** | The existing Stars band, with the scene's tilt bug fixed, plus 3 band fields and one planet disc in `effectStars`, all off by default | **S:** about 30 WGSL lines and 7 stored fields. Off is the existing constants, so frames are identical | band ≈0 (11.64 vs 11.72); the planet is estimated at <0.05 ms |
| **6. Comets** | The existing ADR-230 `comet` effect, one instance, `activation: trigger` on B's `signal.cometCue`, anchor `camera`. The land and river response comes through routes and a third `groundPulse` | **none** | **+1.5 ms while one is on screen** (11.94/11.91 → 13.41/13.45); 0 otherwise |
| **7. Polish** | Aurora-side haze tint in the land program (Quílez's "better fog" sun term, with the aurora as the sun), tonal separation of land from river, and fixing the two bad star values | none | estimated +0.05 ms (3 ops) |

**Cumulative estimate, Ultra 1080p:** about 11 ms baseline (new layout) + 0.1 edges + 0.05 pulses + 1.5 comet
(transient) + ≈0 water and sky ≈ **12.7 ms** with every effect active. That leaves more than 16 ms of the 29.3 ms
controller budget. The aurora stays the one large cost, as in pass 1.

**Two defects found in the current scene (no code needed):**
1. **The river does not move.** It is a flat closed loop with no descent, so `WaterCourse::flowSpeed()` returns its
   0.05 m/s floor (`terrain_water.cpp:95-105`). `fs_water` then multiplies by `rippleSpeed` 0.25
   (`water.wgsl:713`). The streaks and every flow layer therefore travel at about 0.02 m/s: a static mirror, which is
   exactly the owner's complaint (§3).
2. **The Milky Way is mis-set.** `look.py` writes `bandTilt: 35.0`, but the field is in radians (range ±π;
   `star_field.hpp:44`, default 0.6). `density: 0.55` is also above the field's 0.2 range ("0.04 is deep space"). The
   band therefore sits at an arbitrary tilt and never reads. At `bandTilt` 1.0 rad the denser-star band appears
   (probe 08), but its glow is still faint and fixed grey (§5).

## 1. Baseline (current scene, live camera, headless Ultra 1920x1080, target 30)

**Command:**

```
tools/gpu-lock.sh avgen --project examples/aurora-borealis/aurora-borealis.json --live-profile --mode headless
  --quality ultra --target-fps 30 --size 1920x1080 --start <t> --warmup-frames 90 --measure 4 --capture ... --json ... --trace ...
```

Stems are on (the project's own audio and routes). Each row is 120 measured frames.

| Song time | GPU p50 / p95 / max (ms) | Scene pass p50 | CPU work p50 / p99 | Visible tris / draws | Still |
|---|---|---|---|---|---|
| 20 s | 11.63 / 11.85 / 12.29 | 10.78 | 1.27 / 2.60 | 45.8k / 200 | `baseline/baseline-t20.png` |
| 50 s | 11.83 / 12.06 / 13.55 | 10.98 | 1.27 / 2.53 | 44.0k / 193 | `baseline-t50.png` |
| 75 s | 11.81 / 12.17 / 12.91 | 10.95 | 1.27 / 2.71 | 44.4k / 198 | `baseline-t75.png` |
| 115 s | 11.74 / 12.16 / 13.32 | 10.89 | 1.15 / 2.68 | 37.4k / 128 | `baseline-t115.png` |
| 140 s | 11.61 / 11.88 / 12.04 | 10.77 | 1.15 / 2.38 | 38.7k / 140 | `baseline-t140.png` |
| 185 s | 11.47 / 13.40 / 14.21 | 10.60 | 1.40 / 1.88 | 47.2k / 222 | `baseline-t185.png` |

- **Pass breakdown (t20):** depth 0.53 ms, bloom 0.15, composite plus FXAA 0.10, tonemap 0.06. No shadow or AO
  pass. The scene pass, which is mostly the aurora, is about 92% of the frame.
- **The same table on AC** is still owed. The brief's stage 1 should be re-run on AC power with the live run
  (`tools/ci/live-perf.sh` and `--live-profile --mode live`).

## 2. Priority 1: composition

### 2.1 Diagnosis (from `build.py`, `world.py`, `look.py` and the stills)

**Camera now:**
- **Position:** `CAM_H` 28 m above the water, swaying ±22% of the half-width across the river.
- **Aim:** the target is 540 m ahead on the centreline, raised so the target's elevation is **+9°**.
- **Lens:** `useExplicitFov` is false and the focal length is 24 mm on the default 24 mm sensor height, so the
  vertical FOV is **53.1°** and the horizontal about **83°** at 16:9 (`camera.cpp:56`). The scene's `fov: 50` is
  ignored.
- **Result:** the horizon sits about 66% down the frame, so the sky takes about two thirds of it.

**Why the river reads as a channel with raised banks:**

1. **An explicit kerb.** `valley_height` adds a "rock lip" of 1.5-4 m over the first 6 m from the water (a step of
   15-35°). The bed jumps from −6 m (o < 0) to the lip, so the waterline is a ledge, not a slope meeting water.
2. **Ribbon strips.** The band's rows sit at fixed offsets from the water (0, 9, 22, 40, 64 m...), and each row is a
   constant distance from the water's edge. The near facets are long thin strips parallel to the river, and they read
   as a levee or a road (b-t140's left bank).
3. **The loop is too tight to recede.** With R0 = 1300 m the centreline turns about 44° per km, so the river leaves
   the frame behind the inner bank within about 1 km. It wraps round the camera instead of snaking toward the far
   ranges.
4. **The camera hugs a convex bank.** At 28 m with sway toward the inner bank, that bank is one smooth convex mass
   over 30-40% of the frame. The aurora-facing term lights it lavender all over (b-t140, b-t185).
5. **Spurs reach the water.** Their ridges point at the water with half-lengths up to about 1 km, and their foot is
   gated only by `smooth(4, 90, o)`, so they close the corridor ahead.

### 2.2 Research

- **Composition.** Landscape guides agree on three things:
  - a foreground with large, textured shapes that set the scale;
  - a middle ground holding the subject mass (the river bends);
  - a simplified, hazed background.

  A river works best as an S-curve leading line that enters near a bottom corner and recedes, rather than leaving
  the frame sideways [1][2][3]. Haze lowers contrast and saturation with depth and separates the planes [1].
- **Atmospheric perspective in shaders.** Exponential distance fog is the textbook form. Quílez's "better fog" adds a
  term toward the light source's direction, which is in-scattering, cheaply [4]. Sun et al. give the single-scattering
  form, with glows round sources [5]. The engine's volume fog costs 7 ms at Ultra (pass 1), so keep the haze in the
  program, as now, and add the directional term (§7).
- **Terrain meeting water.** The standard approach is a single height function that passes through the water level
  with a non-zero slope, the bed continuing the bank's slope below the surface. The waterline is then simply where
  that surface crosses y = 0, so no seam or lip needs fitting. With faceted meshes the crossing is a clean
  polyline. The scene's "the river is where the land is not" design already allows this; only the lip breaks it.
- **Approaches compared:**

| Approach | For | Against |
|---|---|---|
| A. Keep the tight polar loop; raise and widen the camera only | smallest change | the river still wraps round the camera (curvature, point 3 above). Rejected |
| B. A treadmill: a periodic straight valley with a camera wrap | infinite straight reach | the aurora's world-space folds jump at the wrap; the ranges must also be periodic. Too much |
| **C. A rounded-triangle loop with long reaches** (recommended) | keeps the generator's polar `rc(θ)` and the seamless timeline loop | a larger world, so more water terrain and triangles (measured below) |

### 2.3 Recommended layout (proved in probes A-F; `probes/01`-`04`)

**River (one shared spline).** The centreline is the existing polar form:

`rc(θ) = R0 (1 − 0.10 cos 3θ + A8 sin(8θ + φ8) + A13 sin(13θ + φ13))`

- With a = 0.10, each side's midpoint has zero curvature (9a = 1 − a), so the reaches are straight. The two small
  harmonics add the S-bends.
- **R0:** 2400-3000 m. At 3000 the reaches are about 4.5 km, and the loop is about 880 s at 22 m/s.
- **Meanders:** A8 about 0.016 and A13 about 0.007 (about 48 m and 21 m). Keep the meander amplitude well under the
  valley floor's half-width (point 3 below).
- **Half-width:** 72 ± 14 m.
- **One shared definition:** every consumer derives from `rc`, `half_width` and the arc table:
  - the terrain band's rows;
  - the engine water's `river` feature (256 points maximum; 481 is refused);
  - the camera path;
  - the pulse direction.

**Shoreline falloff (no lip):**
- **Shore slope:** `h(o) = s0·o` through the waterline, with s0 = tan(7-15°) varying along the river. The bed
  continues it, `max(−8, s0·o)` below the water.
- **Lower slope:** about 8° (0.14) out to a floor edge `fl` at 200-420 m.
- **Wall:** `wall_h · smooth(fl, fl + run, o)^1.3`.
- **Spurs and peaks:** gated by `smooth(fl − 150, fl + 120, o)`, so they stand behind the floor instead of reaching
  the water.
- **Row offsets:** each near row's vertices jitter by ±40% of the row spacing, which breaks the parallel strips.

**Three layers, with distinct banks:**
- **Foreground:** the shore slopes and the lower slopes, with large facets from the jittered rows.
- **Middle:** the walls, with distinct silhouettes:
  - outer bank: `wall_h` 190-330 m, steeper;
  - inner bank: 110-220 m, rolling;
  - spur heights ×1.15 outer and ×0.85 inner;
  - spurs set back 450-1100 m.
- **Background:** the three outer rings scaled with R0 (4.3-7, 6.5-11 and 10.5-19 km). The program's haze runs from
  300 m to 9.5 km. `HAZE_FAR` is in decametres and clamps at ±1000.

**Camera:**
- height 75-100 m;
- target 1.5-2.2 km ahead on the centreline;
- **pitch +2-3°**, which puts the horizon about 45-48% down the frame and gives the aurora the upper half;
- 24 mm (vertical FOV 53°). A 28 mm lens (46°) is the fallback if edge stretch shows on the near slopes.
- Keep the gentle roll.

**Water terrain:** size it to the loop (7400 m for R0 3000) with **`chunkSize` 400**.
- At `chunkSize` 200 the bigger world drew 675-1111 draws, with CPU work p50 rising to 3.6-4.6 ms.
- At 400 it is back to 214-312 draws and CPU p50 1.6-1.8 ms.

**What the probes showed:**

| Probe | Change | Read |
|---|---|---|
| A (`01`) | R0 2000 triangle, spurs set back 120 m, camera 70 m | spurs fill the frame. Set them back |
| B | spurs set back 300 m | identical frames. The spurs were not the blocker; the walls were |
| C (`02`, plus top-down map) | wide V (lower slopes and floor edge), camera 100 m | the valley opens; the river still turns out of view at reach ends |
| E (`03`, `03-...-t140.png`) | R0 3000, spurs gated behind the floor, camera 110 m | **the target read**: a wide valley, layered walls, the river snaking into the distance, a clean slope-to-water shoreline with no lip |
| F (`04`) | camera 75 m, pitch 3°, lower slopes 8° | more aurora in the river; the wall closes the view at bends |

**Cost and remaining gaps.** Probes E and F measured GPU p50 10.6-11.3 ms against 11.5-11.8 for the baseline: more
land, which is cheap, and less sky. Visible triangles rise from 37-47k to 61-72k. The open issues for the scene
agent:
1. **The river still hides behind bends in some views.** Keep the meander amplitude below about a quarter of the
   floor width, or aim further ahead.
2. **Camera height trades against the reflection.** A higher camera sees the reflected zenith (dark) in the near
   water, so the aurora's reflection moves to the middle distance. 75-90 m is the balance I would start from.
3. **Generate the camera from the arc table at every key.** The probe's 500 keys over 13-19 km are fine.

## 3. Priority 2: musical edge light on the faceted ridges

### 3.1 What the engine has

- **Screen-space curvature is zero on these meshes.**
  - The `curvature` input and the `curvatureMask` and `edgeWear` ops come from `materialGeometry`, which
    differentiates the normal across a pixel quad (`pbr_shade.wgsl:517`).
  - The faceted GLBs have one normal per triangle, and a quad's helper lanes shade the same triangle.
  - So these inputs read 0 everywhere except at silhouettes. They cannot find facet edges.
- **GLB `TEXCOORD_0` reaches the program.** `gltf_loader.cpp:586` reads it, and `ctx.uv = uv` (`pbr_shade.wgsl`).
  `meshes.write_glb` writes positions and normals only.
- **The ops needed exist:**
  - `footprint` is the world size of a pixel;
  - `power` with value −1 gives a reciprocal;
  - `smoothstep` accepts reversed edges (custom `matSmoothstep1`);
  - `swizzle`, `field` and `cameraDistance` are all there.
- **The ceiling is 48 ops a program.** The rock program has 30.

### 3.2 Approaches compared (research [6]-[9])

| Approach | For | Against |
|---|---|---|
| Screen-space curvature or normal edge detect | no data needed | **does not work here**: flat facets give no derivative, as above |
| Barycentric wireframe (per-vertex barycentrics, `fwidth` for constant pixel width [6][7]) | single pass, anti-aliased | outlines **every** triangle: the "neon wireframe" the brief forbids. With unshared vertices the colouring problem [7] does not arise, but selection still has to be baked |
| **Baked dihedral edge mask** (crease angle per edge, baked to a vertex attribute [8][9]), drawn as a metric distance | selective (ridges, convex creases, height-weighted); exact (distance to a line is affine on a facet, so interpolation is exact); pixel-width and anti-aliased via `footprint`; no runtime geometry | two channels only: one edge per facet (below) |
| A separate line or ribbon mesh along the chosen edges | any width, any count | a new draw with alpha or additive blending; z-fighting on the facets; more triangles. Unnecessary |

### 3.3 Recommended: baked mask in `TEXCOORD_0` (proved, `probes/04-probeF-edges-*`)

**Bake, in the generator, per mesh:**
1. Build an edge adjacency (vertex keys to 1 cm).
2. For each interior edge take the dihedral angle and test convexity: the far vertex of the neighbour is below this
   facet's plane.
3. Importance = `smooth(14°, 40°, angle) × (0.35 + 0.65 · smooth(20, 260, edge height))`, for convex edges only.
4. Each facet takes its most important edge, if that is ≥ 0.08.
5. Write u = the metric distance from each corner to that edge (0 on the edge), and v = the importance.
6. A facet with no edge gets u = 1e4.

The probe's bake (`probe-generator/world.py`, `edge_uvs`) runs in about a second for 40k triangles.

**Program, 15 ops (probe F's).** It is inserted before the haze so the haze and distance soften the edges:
- `uv`, `footprint`, then `power −1`, then `u/fp`;
- `smoothstep(1.6 → 0)`, a pixel-width line, then a broadcast;
- × importance (v);
- × `smoothstep(450 → 40 dam)` on distance, which fades it by 4.5 km;
- × the edge colour (the palette's `facetLit` or `auroraLow`) × the edge glow (routed);
- added to the facet colour.

The program then has 45 ops.

**What it looks like:**
- **Line and AA:** the lines are crisp, about 2 px, anti-aliased, and constant on screen.
- **Selectivity:** only creases and ridges light, not every triangle.
- **Reflection:** the lines reflect in the river through the sky mirror.

**Its limits, and the fixes:**
1. **One edge per facet.** An edge chosen by only one of its two facets draws at half width, and lines stop at
   facet corners. Ridge crests are usually picked by both sides. If it shows, put a second edge's distance in v and
   drop the importance channel: draw width then carries importance. Or let the engine read `TEXCOORD_1`/`COLOR_0`
   (size M; not recommended yet).
2. **Long sliver facets** in the zipped wall rows make long diagonal lines. The bake should down-weight edges whose
   length is above about 4x the facet's altitude.
3. **Small blobs at grazing angles** on short crest edges, where `footprint` is large. Cap the width at
   `min(u/fp, u/w_max)`, which is one more op.

**Musical control (B's signals, the brief's §4 mapping), within the op budget:**
- **Bass, broad intensity:** route `signal.bassBody` to `edgeGlow`. No op cost.
- **Bass waves across the ridges:** one `field` op sampling a Wave field triggered on `signal.bassHit`, × the line
  (2 ops; ADR-906 trigger, ADR-1116 fields).
- **Kick:** a broad pulse on `edgeGlow` (a route).
- **Snare, selected ridges only:** `smoothstep(0.6, 0.9, v)` × the snare (3 ops), so it lights the top importance
  tier.
- **Synth:** the palette binding on the edge colour (B: fix the synth colour signal first).
- **Hi-hat sparkle:** skip unless B's detector quality allows it.

**Op budget.** Probe F plus waves plus snare is about 50 ops, over the 48 ceiling. Trim 3-5 ops from the existing
rock program to fit. The decametre multiply can go if the constants absorb the 0.1, and the crest block's swizzle
plus smoothstep can share a register. **No engine change is needed** for that.

**Cost:** +0.1 ms (interleaved A/B, 2 reps, t140: 10.61/10.62 → 10.75/10.71 ms).

## 4. Priority 3: flowing water

### 4.1 What the engine has

The water is cinematic (ADR-1286) with these terms:
- three ripple layers advected along the baked flow with ADR-914's bounded dual-phase cycle (`flowPhases`);
- undulation;
- the view streak (`streak`), which spreads the normal along the view;
- ADR-1643 flow streaks, lanes along the channel coordinate `uv.y`;
- the ADR-1650 sky mirror;
- per-vertex flow direction, and a speed fraction from `world::buildChunkWater`.

**The defect:** the speed is the course's gradient speed (0.05 m/s floor on a flat loop) × `rippleSpeed` 0.25, about
**0.02 m/s**. So everything is effectively frozen.

### 4.2 Approaches compared (research [10][11])

| Approach | For | Against |
|---|---|---|
| **Flow-map advection, dual phase** (Vlachos, Portal 2 [10]; Catlike Coding [11]) | the industry standard for directional flow without a simulation; no tiling reset visible | needs a flow field. **Already implemented:** ADR-914's `flowPhases` is this technique, with the flow baked per vertex from the river feature |
| Spline-space UV scrolling (u = arc length, v = across) | exact along a spline | the water vertex has no arc coordinate (uv.x is bed depth), so it needs an engine change. Unnecessary given the above |
| Stretched (anisotropic) highlights | the long-exposure look | ADR-1643 streak lanes plus the view `streak` already provide it |

### 4.3 Recommended (zero engine; `probes/05-*`)

**On the terrain node and the water:**
- `terrain.flow: { speedOverride: 2.5-3.5 m/s, bankShear 0.5, meander 0.12, turbulence 0.3 }`;
- `water/flowSpeed` (that is `rippleSpeed`) at 1.0;
- `streakSpeed` 1.5-2, `streakLength` 40-60 m, `streakHighlight` about 1.0;
- ripple **≤ 0.012** at scale 0.03, as the brief's weak second layer.

**Why the ripple must stay that low:** probe w1 used 0.035, and it broke the mountains' reflected silhouettes into a
wobble. Too much for "a stable mirror".

**Leave these alone:**
- undulation 0.02-0.04 at a low scale, for slow large distortion;
- SSR off and the sky mirror on, as now. Pass 1 isolated the dash lattice to SSR plus the streak.

**Music, through existing parameters:**
- bass → `streaks/amount` and `reflection`;
- synth → the streak colours (the palette, already bound);
- drums → a short `undulation` swell (cheap and global; no circular ripples).

**Cost:** ≈0 (t140 11.68 vs 11.61 ms; t50 differed by +0.4 ms with a noisy p95). The terms already ran; they just
moved too slowly.

**Risk:** at 3 m/s the ADR-914 cycle's period is visible as a "breathing" of the ripple contrast if the ripple is
strong. Keep the ripple small, and let the streaks carry the motion.

## 5. Priority 4: travelling river pulses

### 5.1 What the engine has

The ADR-207 world wave effects (`wave_effects.wgsl`, `wave_effect.hpp`):
- **Two metrics:** directional (a front along an axis, with a beam radius) or radial.
- **Envelope:** a front width, a trail with a falloff exponent, and a bright leading-edge band.
- **Vertical mask:** a band about the origin's height that opens with distance.
- **Reach:** up to 16 waves (`kMaxGpuWaves`) per frame. They are added before fog on **every** surface, including
  unlit programs (`pbr_shade.wgsl:870`) and the engine water (`water.wgsl:1185`), and they reach the emission target,
  so they bloom.
- **Sources:** World, Node, Hero, **Camera** (with `groundOffset`).
- **Directions:** Explicit, CameraForward, **CameraVelocity**, ...
- **Activation:** `trigger` on a signal (ADR-906/1061), deterministic, so seek equals play.

### 5.2 Approaches compared

| Approach | For | Against |
|---|---|---|
| Emissive band parameterised by arc length (spline-space) in the water shader | follows the curve exactly | the water vertex has no arc length; needs a new vertex channel or a baked river-coordinate texture plus a binding (M) |
| An angular wave metric about the loop centre | exact for a polar loop | needs an engine kind; the speed varies up to 4x along straight reaches (ds/dθ = d/cos²) |
| Point lights along the river | "real" light | the scene is unlit by design; light objects plus per-frame updates. The brief prefers shaders |
| **A directional wave from the camera, down the river** (recommended) | **zero engine**; one record lights the water and the shoreline; the front is a plane, so the lit region is the plane ∩ (water ∪ low bank), which **follows the actual winding river** wherever it bends; bounded | follows the river only along the camera's heading. Fine, because the camera looks down the river; a reach behind the camera is never seen |

### 5.3 Recommended (proved, `probes/06-river-pulse-*`)

A `groundPulse` effect, world-owned, with:

```
source:      { kind: camera, groundOffset: <CAM_H> }          // origin at the water surface
propagation: { kind: directional, direction: cameraVelocity, beamRadius: 300-400,
               startOffset: 120, speed: 600-900 m/s, range: 2500-3500,
               frontWidth: 90, trailLength: 400-600, falloff: 1.6,
               verticalExtent: 4-6, verticalGrowth: 0.004, ringCount: 0 }
appearance:  palette colours (route or bind), edge band brighter
```

The `verticalExtent` of a few metres about the water surface makes the light take the water and only the first
metres of shore slope. That is "illuminating nearby terrain where the river approaches the shoreline", and with the
new continuous shore slope it is exactly the shore band.

**Probe 06** used a 3 s repeat, the baseline layout and 700 m/s. The difference frames ×4 show the front leaving
120 m ahead, crossing the near water and the bank at 1.0-1.5 s, and travelling out to a lit sliver of distant river
at 2.0-2.5 s. It stays confined to the river corridor, and the sky mirror shows the lit banks reflected.

**Music, as B's §3 ring of two slots:**
- **Kick:** two instances, `activation: trigger`, so at most two fronts are alive. Each is bounded by its lifetime and
  range, and by the 16-wave ceiling.
- **Snare:** a flare on `appearance/edgeIntensity` of the live fronts, never a new pulse.
- **Bass:** `width` or `trailLength`.
- **Synth:** colour.
- **Overlap cap:** the shader **sums** waves. Cap the sum by sizing each pulse's intensity to at most half the
  allowed peak, or route a compressor-style depth.

**Cost:** +0.05 ms (11.93/12.00 → 12.00/12.00, t50, 2 reps). The waves loop is skipped when the count is 0.

**Engine gap: none.** Two optional changes, each with an off = identical test:
1. a `groundOffset` mode "absolute Y", so a camera whose height varies keeps the origin on the water;
2. a "max" instead of "sum" combine for pulses of one group.

Only the first is likely to be needed, and only if the camera height is keyed to vary.

## 6. Priority 5: reversible celestial sky

### 6.1 What the engine has

- **The Stars effect** (`skybox.wgsl:53` `effectStars`):
  - a star field;
  - a galactic band: a Gaussian great circle of **fixed** width 0.14 rad, glow coefficient **fixed** at 0.02, colour
    **fixed** grey-blue (0.34, 0.36, 0.46);
  - mottle and a dark rift;
  - tilt, `bandTilt`, in radians.
- **The moon disc** is a white disc on the sky's sun direction only.
- **There is no planet** layer.
- **Draw order:** sky, then the atmosphere layer (aurora, comets) drawn over sky pixels. Anything drawn in the skybox
  is therefore behind the aurora, which is physically right (the aurora is at about 100 km) and the order the brief
  wants.

### 6.2 Approaches compared (research [12][13][14])

| Approach | For | Against |
|---|---|---|
| An HDRI or equirect Milky Way texture (`skyEquirect`) | photographic | replaces the procedural gradient; IBL rebuilds; generic "space wallpaper"; an asset licence |
| A procedural fBm galaxy band with dust lanes [12][13][14] | cheap; the existing `effectStars` already does a minimal form (mottle plus a rift) | the current constants are too faint to read |
| A planet as a scene sphere (entity, unlit program) | no engine change | **wrong layering**: opaque geometry writes depth, so the atmosphere layer skips those pixels and the aurora cannot pass in front of it; parallax as the camera travels kilometres. Rejected |
| **A planet as an analytic disc in `effectStars`** (direction, angular radius, colour, terminator, limb glow) | correct order (under the aurora, in the mirror); fixed at infinity; about 10 ALU per sky pixel | an engine change (S) |

### 6.3 Recommended

1. **Fix the scene now (no code):** `bandTilt` 0.9-1.1 rad, so the band crosses the upper frame diagonally; `density`
   ≤ 0.03; `band` 1.2-1.6 (probe 08). That is variant "existing sky plus subtle Milky Way", already selectable by the
   one value `band`.
2. **Engine change S1, the band look:** three stored fields on Stars, defaults equal to today's constants:
   - `bandWidth` (0.14);
   - `bandGlow` (0.02);
   - `bandColor` (0.34, 0.36, 0.46).

   A violet-to-blue band at about 3x today's glow, about 0.22 wide, with a stronger rift, makes the subtle Milky Way.
   **Test:** a Stars scene with the defaults matches the pre-change frame hash.
3. **Engine change S2, the planet:** five fields:
   - `planetDir` (azimuth and elevation, in degrees);
   - `planetRadius` (angular, 0 = off);
   - `planetColor`;
   - `planetLight` (terminator direction);
   - `planetHalo`.

   In `effectStars`, a lit disc (`dot(n, light)` wrap), a 1-2 px anti-aliased limb from `fwidth`, and an exponential
   halo. Under the `planetRadius > 0` branch, so off is byte-identical in output.
   **Size:** about 25 WGSL lines, five registry rows and the uniform packing. **Cost:** estimated < 0.05 ms.
   **Test:** radius 0 matches the pre-change hash; radius > 0 brightens a known pixel.

**The four comparison variants** (the brief §7) are then one `stars` effect's values, so no source revert is needed.
Keep them as project **presets** (A/B in the Live panel):
- `band` 0 and `planetRadius` 0 (existing);
- band on;
- planet on;
- both.

Do not route anything to the band except a slow arrangement value, which B can provide.

## 7. Priority 6: comets

### 7.1 What the engine has

- **The ADR-230 `comet` effect** is a great-circle arc with a view-ray trail integrator (`atmosphere_fx.wgsl`):
  - head, halo and tapered tail (length, width, falloff);
  - wisps, sparkle fragments and rainbow;
  - travel time and acceleration.
- **Anchor:** `camera` keeps the bearing as the camera travels.
- **Ground light:** a `ground` term, but it reaches lit PBR and water only.
- **Limits:** at most 6 comets a frame (`kMaxGpuComets`).
- **The meteor shower** uses the same bucket.

### 7.2 Approaches compared (research [15][16])

| Approach | For | Against |
|---|---|---|
| A CPU-rebuilt camera-facing ribbon with a taper along the trail [15][16] | the classic game approach | per-frame geometry (the brief forbids it); sorting or additive overdraw |
| Sprite or particle tails (SpaceEngine [15]) | physically shaped dust and ion tails | particle counts; the brief wants few, bounded events |
| **The existing view-ray comet integrator** | already built, deterministic, palette-colourable, under the aurora in the right layer | +1.5 ms while on screen (below) |

### 7.3 Recommended (proved, `probes/07-comet-*`)

**One `comet` effect:**
- `anchor: camera`;
- `activation: trigger` on B's `signal.cometCue` (B: about 5 a song, never closer than 26 s), lifetime about 9 s.
  A single instance is the hard bound: at most one alive.
- **Path:** start bearing = the camera heading + 30°, elevation 30°; end bearing = the heading − 35°, elevation
  13°. The engine's azimuth is `atan2(x, z)`, with +az to the left of a forward view. For a new layout, compute the
  heading per comet time from the camera track in the generator (B's fire times are deterministic).
- **Travel:** `travelSeconds` 6-7, `distance` 5 km, `arcLift` 200.
- **Look:** tail 2.6 km and 60 m wide, in the palette's lavender, violet and magenta, with `coreIntensity` about 16.
  The probe's 7 s crossing reads as a comet, not a meteor, and sits above the aurora's band.

**Environment response, no new lighting:**
- the same cue envelope → `fx/aurora/intensity` (+15%, B's table), the rock `auroraGlow` and `edgeGlow`, and
  `water/glow`;
- a third `groundPulse` (§5) launched on the cue with a slower 300 m/s front: "a traveling highlight on the river";
- the comet's own `ground` term tints the water.

**Disable independently:** the effect's `enabled` flag; all routes read the cue, so with no comet nothing fires.

**Cost:** +1.5 ms GPU while it is on screen (11.94/11.91 → 13.41/13.45, t54, 2 reps). It is 0 between events, since
the comet bucket is empty. Inside the budget, but it is the largest transient. Measure the comet frame stacked on the
aurora's brightest state (B's stage 7).

**Engine gap: none.**

## 8. Priority 7: unifying polish (optional, judged on renders)

**1. Aurora-side haze** ([4]'s sun term, with the aurora as the "sun"). In the rock program, before the haze mix:
- `gradient(viewDirection, AURORA_DIR)` → `power 4` → `mixBy(HAZE, auroraLow × 0.4)`, 3 ops.

The ranges under the aurora then take its colour with distance, and the opposite side stays indigo. That is the
separation between ranges, and it ties the haze to the palette. The op budget is shared with §3.3 (trim first).

**2. Land against river tonality.** The near shore slopes are now the same value family as the water's dark
reflection. Lower the facet `skyLit` term near the water: a `smoothstep` on height of about 0-25 m, × 0.6.

**3. Edges, pulses and the comet share one palette state** (bind the edge colour to `auroraLow`/`facetLit`), so
palette steps move them together (B's phrase-gated palette).

**4. Fix the stars:** `density` and `bandTilt` (§0).

**5. Aurora variation** ("not one rigid sheet") is aurora shape work: `ribbonCoverage` on a phrase gate, and
`ribbonSwirl` from the bass body. It belongs to the scene agent with B's signals. No rendering change.

**Avoid:** volume fog (7 ms), extra layers, vegetation.

## 9. Engine changes, sized

| # | Change | Size | Needed for | Off = identical test |
|---|---|---|---|---|
| T1 | `tools/sonic_vfx/meshes.py`: `write_glb(..., uvs=None)` writes `TEXCOORD_0` when given | XS (tool) | edge glow | other generators' GLBs byte-identical (a `uvs=None` default) |
| S1 | Stars: `bandWidth`, `bandGlow`, `bandColor` | S | a readable Milky Way | Stars scene frame hash with defaults equal to before |
| S2 | Stars: planet disc (5 fields) | S | the planet variant | `planetRadius` 0 hash equal; a radius > 0 pixel test |
| (opt) | wave `groundOffset` as an absolute Y | XS | pulses if the camera height varies | default mode unchanged |
| (opt) | `TEXCOORD_1`/`COLOR_0` to programs | M | a second edge channel | only if one-edge-per-facet shows |

Nothing else is needed. Edge glow, flowing water, pulses and comets are all **scene authoring on existing engine
features**.

## 10. Risks

1. **Battery-only measurements.** Every delta here is an A/B on one build, on battery. The brief's stage
   measurements (1-8) and the live acceptance run must be repeated on AC power.
2. **The composition is not finished.** The probes found the direction (long reaches, a wide V, the lip removed,
   spurs gated) but not the final numbers. In some views the river still turns out of sight within about 1.5 km. The
   meander-to-floor ratio, `AHEAD` and the camera height need an iteration with a moving clip, not stills.
3. **Camera height against reflection.** Higher shows the valley; lower keeps the aurora in the near water.
4. **A bigger world:** about 60-70k visible triangles, and a 7.4 km water terrain. CPU draws must stay down with
   `chunkSize` ≥ 400. The nav grid warns that it is skipped (harmless).
5. **The program op ceiling (48).** Edges, waves, snare tiers and the haze term need about 20 ops over today's 30.
   The existing program must be trimmed.
6. **Edge artefacts:** half-width lines where only one facet chose the edge; slivers; blobs at grazing angles. All
   are bake-side fixes (§3.3).
7. **Pulses summing.** The wave shader adds overlapping fronts. Cap by intensity sizing (B's two-slot ring).
8. **The comet's cost** (+1.5 ms) stacked on a bright aurora peak. Measure at the brightest cue (151 s or 187 s in
   B's list).
9. **Faster flow shows the dual-phase cycle** if the ripple is not kept small.

## 11. Open questions

1. **Loop size.** Is R0 about 3000 m (an 880 s loop at 22 m/s, so Feline sees a quarter of it) acceptable? Or
   should the loop be shorter so a live set sees the corners more often?
2. **Camera height.** Prefer the valley vista (90-110 m) or the aurora in the near water (60-75 m)?
3. **Planet.** Is S2 worth an engine change, or is the band alone (S1, or just the bug fix) enough for the
   experiment?
4. **Edge light colour.** Bind it to `auroraLow` (hotter, pinker) or `facetLit` (cooler, violet)?

## Sources

1. Landscape composition, foreground, middle ground and background; haze: https://gravitee.io/corpus/gen-1201/kites-spitzweg/landscape-composition.html ; https://jonnorrisphoto.substack.com/p/guide-to-landscape-photography-composition
2. The S-curve as a leading line: https://www.lightstalking.com/s-curve/ ; https://iceland-photo-tours.com/articles/photography-techniques/landscape-photography-composition-techniques-the-s-curve
3. Lead-in lines from the bottom corners: https://amateurphotographer.com/technique/expert_advice/quick-tips-lead-in-lines
4. Íñigo Quílez, "fog" (the directional in-scattering term): https://iquilezles.org/articles/fog/
5. Sun, Ramamoorthi, Narasimhan, Nayar, "A Practical Analytic Single Scattering Model for Real Time Rendering" (SIGGRAPH 2005): https://www.cs.columbia.edu/CAVE/projects/scattering_model/scattering_model.php
6. Barycentric wireframes with `fwidth`, constant pixel width: https://docs.kdab.com/kdgpu/unstable/wireframe_geometry.html ; https://npmjs.com/package/glsl-solid-wireframe
7. Solid wireframes, and the shared-vertex barycentric pitfall: https://tech.metail.com/
8. Curvature and edge masks for stylised edge brightening: https://www.strayspark.studio/blog/curvature-maps-blender-substance-painter-alternative
9. Baking edge angle or curvature into vertex colours: https://forums.unrealengine.com/t/polyart-studio-vertex-forge/2736232 ; https://www.zbrushcentral.com/t/how-to-bake-a-curvature-mask-into-a-vertex-color-channel/426513
10. Alex Vlachos, "Water Flow in Portal 2" (SIGGRAPH 2010), dual-phase flow maps: https://www.advances.realtimerendering.com/s2010/Vlachos-Waterflow(SIGGRAPH%202010%20Advanced%20RealTime%20Rendering%20Course).pdf
11. Catlike Coding, "Texture Distortion" (flow): https://catlikecoding.com/unity/tutorials/flow/texture-distortion/
12. Galván et al., procedural celestial rendering (stars and dust from noise): https://nuilab.org/wp-content/uploads/2022/02/galvan_procedural_celestial_rendering_for_3D_navigation.pdf
13. "Sculpting procedural noise: real-time rendering of interstellar dust clouds": https://hal-lirmm.ccsd.cnrs.fr/LJK_GI_MAVERICK/hal-03762332v1
14. A procedural skybox in one fragment shader (WebGPU): https://hugodaniel.com/pages/pngine/docs/samples/skybox/
15. SpaceEngine comet tails (sprites, closed-form particles, dust and ion tails): https://spaceengine.org/news/blog121026/
16. Tapered camera-facing ribbon trails: https://gamedev.net/forums/topic/562782-how-to-make-a-ribbon-effect/ ; https://babylonjs.medium.com/trail-effect-with-babylon-js-786053389fdc
