# The Strange Attractor: progress

> **Change of direction (owner, after Gate 2, 2026-10-27):** "have it remove all that stuff and just go back to the
> canyon - fix that up and make it look good and render a fly through of the canyon".
> - The attractor ribbon, the butte tower and the nested arches are gone: code, parameters, tests and shaders
>   (ADR-1853 supersedes ADR-1852's formation parts).
> - The scene is now the canyon, with a river and a flythrough.
> - The brief (`00-brief.md`) is kept as written. Where it asks for recursive landmarks and an attractor structure,
>   the owner's direction overrides it.

Branch `scene/strange-attractor` in `../av-gen-attractor`, cut from main 3ea9b678. ADRs 1850-1879 (used: 1850-1857).
Review media: `~/Desktop/av-gen-review/101-strange-attractor/gate<N>/`.

The brief is `00-brief.md`, the research `01-research.md`, the plan `02-plan.md`.

**Owner rules that override the brief:**
- one agent, with no subagents;
- art before optimisation;
- no examples index or live switcher entry: open it with `--composition`;
- every avgen run under `tools/gpu-lock.sh`;
- audio only from the current signals.

## Where it stands

| Gate | Stages | State |
|---|---|---|
| 1 | 0 research, 1 skeleton | **approved by the owner** ("solid start - looks fantastic") |
| 2 | 2 recursive landmarks, 3 branching and the attractor | done; the formations were then **removed by the owner** |
| 2b | the canyon alone: the fixes, the river, the flythrough | done |
| 2c | clean-up: camera jitter, geometry, a flowing river, one clean render | done (the jitter persisted: see 2d) |
| 2d | the real jitter cause, glassy water, a 46 s flight | done |
| 2e | cliff edge artifacts traced and fixed (ADR-1856) | done; owner then chose engine-native geometry (`03-canyon-brief.md`) |
| canyon P1 | the canyon as engine triangle geometry (ADR-1857), `04-canyon-phase1.md` | **done, awaiting the owner**: media in `canyon-p1/` |
| canyon P2-P6 | revelation, birds, glyphs, (campsite), QA | not started: gated on P1 acceptance |
| 3 | 4 art direction, 5 audio | not started |
| Final | 6 tiers and performance, 7 polish, tests and docs | not started |

## Gate 2d (the jitter in the picture): ADR-1855

- **The cause.** The shader reconstructed view rays from `invViewProj` at ndc z = 0 and 1. With a 0.1 m near plane and
  a 32 km far plane that is ill-conditioned in float32, so the ray direction wandered by up to 0.35 degrees,
  differently every frame. The whole picture jumped by about 8 px.
  - The camera itself was smooth; the Gate 2c trace was right about that.
  - **Fix:** a double-precision camera basis computed on the CPU.
- **Measured on the videos with a feature tracker:** 8.3 px per-feature jitter became 0.1-0.2 px. The frame-difference
  irregularity went from 22-31% median to 0%.
- **Frozen camera:** 0.006/255 per frame with the river on, 0 with it off.
- **Water:** back to glassy, with one slow, broad, low swell octave along the flow and a sparse bank line. The fine
  chop and flecks are gone.
- **The flight is now 46 s.** The reveal is shorter.
- Scene-local. `shaders/clouds.wgsl` uses the same reconstruction, but its scenes have ordinary far planes.

## Gate 2c (clean-up): ADR-1854

- **The jitter was scene-side, in the flight builder.** The height was re-clamped to a per-frame terrain clearance
  after smoothing, and the 2.5 s look-ahead amplified that into the view.
  - Hard pitch and bank clamps and an end-of-path aim switch made it worse.
  - Measured with the hidden `[.attractor-camera-trace]` case through the real `Composition::update`:
    - before: view noise 1.17 px rms, 19 px peaks, 3,566 deg/s² angular acceleration;
    - after: 0.52 px rms, 1.7 px max, 59 deg/s².
  - No engine code changed.
- **The river flows** (a baked flow field, advected streaks, bank foam). The gorge floor is flat at -2 m (no shoals).
- **Bands are of uneven thickness** (`bandWarp`).
- **Rendering.** The final render is `--supersample 2`: 182.7 s for 1,620 frames, about 113 ms per frame.

## Gate 2b (the canyon alone and its flythrough): ADR-1853

- **The flythrough:** `avgen --headless --composition examples/strange-attractor/strange-attractor-flythrough.scene.json
  --render out.mp4 --range 0:54 --fps 30 --size 1920x1080`. It is 54 s at offline quality (the 4096² field).
- **The faults and their causes** (each found by A/B on the same frame; details in ADR-1853):
  - the dashes (the shadow tested the uncut surface);
  - the moiré (ledges cut along bilinear contours);
  - the hatching (sub-pixel ledges);
  - the vertical streaks (the flutes, and gullies cut in texels);
  - the near blob (relief that could only vary along a face).
- **The river:** a reflecting water plane in the gorge.
- **Offline cost:** about 29 ms per frame end to end (54 s rendered in 47.2 s). Not a live measurement.
- **Known weak spots:**
  - speckled edges on some sunlit caprock rims;
  - the dive (8-17 s) spends a while looking down at shadowed bench;
  - faint broad shading of real gully geometry on bright faces;
  - the river is a flat plane, so it has no flow;
  - the side canyons are subtle.

## How to see it

```
tools/gpu-lock.sh build/release/src/avgen --headless --composition examples/strange-attractor/strange-attractor.scene.json \
    --render <dir> --format png --range 0:0 --size 1920x1080
```

- Variants: copy the scene file and change `attractorWorld.seed`, `resolution` (2048 live, 4096 offline),
  `strataFold`, or `look.palette` (0 Dusk Mineral, 1 Nocturne, 2 Chromatic).
- Nocturne also wants `sunElevation` 2.5, `sunIntensity` 0.7, `ambient` 2.0 and `atmosphere` 1.5.
- `--scene` loads glTF only; a scene file goes through `--composition`.

## Code map

- `src/scene/attractor_world.cpp` holds the generator:
  - the layout (frame, valley trunk, composed masses, strata seed);
  - pass 1a: the field F and the valley distances;
  - pass 1b: the heights (metric cliffs, tiers, bench, gorge, terraces);
  - pass 2: gullies;
  - pass 3: cavity and the masks;
  - the camera search;
  - JSON and parameters.
- `shaders/attractor/world.wgsl` holds the march, the strata colour, the shading, the sky and the fog. The palettes
  are in `src/rendering/attractor_renderer.cpp`.

## Decisions (each with its evidence in 01-research.md §5)

- Canyon country from one erosion-front field, not masses on a plain.
- Metric cliffs (`F / |grad F|`), with a wide stencil and a gradient floor.
- Strata at three scales; ACES rather than AgX; strataFold 0.45 by default.

## Gate 2 (owner notes on Gate 1, and what was done)

| Owner note | Done | Evidence |
|---|---|---|
| The curtain streaks on the cliffs | **Cause fixed** (ADR-1852): B-spline shading normals, the cavity mask off the faces, no gullies under 12 texels | the same-frame A/B: shaders restored to Gate 1 against the current shaders, same binary, same world |
| The bands read as an overlay | the strata offset is baked; the shader cuts the band profile on faces at sub-texel scale (hard bands: ledge and riser) | the same A/B; the ribbon and tower close-ups |
| A stronger fold | `strataFold` 0.7 by default | the primary and Nocturne |
| The comb highlights on the gorge walls | the plain bilinear surface (the smoothstep weighting zigzagged the band ledges) and the B-spline normals | the A/B |
| The lumpy foreground mound | the camera search penalises mounds above its own ground in the lower corners; the near detail is finer and lower | the primary |
| Regeneration off the main thread | ADR-1851: a worker in the live editor; synchronous offline (deterministic) | test `[adr1851]` |
| Tone Chromatic down | its accent slot is a restrained plum; no teal | `chromatic` still |
| Rössler first, Thomas fallback | Rössler kept (a legible nested spiral); Thomas rendered and rejected (a tangle) | `attractor-thomas-rejected` |

**Stages 2 and 3 as built (ADR-1852):**
- **The attractor ribbon (Rössler).** Stood up as weathered stone loops arching out of the gorge; its radius swells
  and thins along it, and its soft bands are recessed.
- **Recursive landmarks: two strategies kept, one rejected.**
  - The leaning butte tower: each tier is the plateau's own profile at 0.58 scale.
  - The nested arches: arch inside arch inside arch.
  - The fin with arches read as masonry and was rejected.
- **Branching.** Side canyons off the gorge, cut back through the cliffs to a rising floor.
- **Camera.** A slow time-based drift (`drift`, 150 s period).

**Bugs found and fixed on the way, each by A/B render:**
- the bounds drawn as dotted shells (outside distance under the hit epsilon);
- knife-ridge pinnacles between side canyons;
- landmarks placed on top of each other, or not at all;
- black notch rows along the ledges (the smoothstep-weighted bilinear).

## Known weaknesses at Gate 2 (honest)

- **Live cost is now a real problem.** A 15 s 1080p drift rendered at 7.6 fps end to end, encoding included. Gate 1's
  bare terrain rendered at 61 fps. The formations are evaluated at every march step inside their bounds, plus in
  the shadow march and AO. This is not a measurement, but it means Stage 6 needs real work: bounds-first stepping,
  a cheaper tube near-field, fewer shadow steps, and render scale.
- The near right cliff in the primary shows a fine wood-grain moiré: bedding lines across the fluted face.
- Faint vertical streaks survive on brightly lit caprock faces. They are shading of real gully geometry, not the
  texel curtains, but similar enough to note.
- A hatched texture on the left mid mesa in the 4096 primary: the band cut where tilted bands cross a face
  obliquely.
- The tower's tiers are regular enough to read a little like a pagoda or chess piece.
- The outer arch's strata ribs make it read partly as a sliced shell.
- Nocturne crushes the lower half to near black, so the formations' detail is lost there.
- The strata do not yet bend around the ribbon (a planned refinement for Stage 4).
- The march now evaluates the formations at every step inside their bounds. The cost is unmeasured (Stage 6).
- Generation is about 2.5 s at 2048² and 10-14 s at 4096².

## Known weaknesses at Gate 1 (honest; status at Gate 2 in the table above)

- The image is a convincing canyon, not yet surreal. The impossible geology (nested arches, the attractor ribbon,
  strata bending around it) is Gate 2's job.
- The near foreground is weak in the primary composition: a shapeless lumpy talus mound at lower left.
- The cliffs show vertical "curtain" streaks: stretched texels on near-vertical faces, with flutes on top.
- The gorge walls show comb-like highlights at 2048 (better at 4096).
- The Chromatic palette is loud: its teal accent lands on talus rubble.
- A seed change regenerates on the main thread (about 1.3 s at 2048).

## Log

- 2026-10-29: Canyon water pass 2. Notes in `11-canyon-water-2.md`; media in `canyon-water2/`.
  - The black bands were one-frame checkerboards of see-through shallow water. The linear depth, unprojected through
    the f32 invViewProj, was noisy from pixel to pixel. It is now taken from the projection's depth mapping
    (ADR-1870). Dark transients over 46 s fell from 507 to 151 px/frame.
  - The opening's saw-teeth were SSR rays bent by aliasing ripples. The march and the sky mirror now use a
    pixel-filtered normal (ADR-1871). Teeth area fell from 207 to 97 px/frame.
  - The 16 s seam is the SSR screen-edge fade, not fixed.
- 2026-10-29: The attractor water block is saved (ADR-1869).
- 2026-10-29: Bank water flicker.
  - The river had an unintended default foam line; the attractor's water block silently dropped keys.
  - The reflected cirrus aliased under the ripples.
  - Fixed in ADR-1868; bank-flip excess over the geometric floor −67%.
- 2026-10-29: Rebased on main d245d0f0. Shadow stair-steps, pass 2.
  - Neither the split scheme nor resolution helped: at 8192 the teeth are pixel-identical.
  - The teeth were caster facets plus T-junction cracks from the RTIN's depth-first error propagation. Fixed with
    level order and a 1 m mesh (ADR-1867): 2.92M triangles, GPU 26.7 → 24.3 ms.
  - The lattice pattern changes with the mesh.
  - Notes in `10-canyon-shadows-2.md`.
- 2026-10-28: Cirrus follow-up.
  - Bands (ADR-1865) thin the wide sky.
  - The water reflects the cirrus along its reflected ray (ADR-1865, about 1.5 ms).
  - Shadow jitter traced to the cascade fit changing size every frame (float `invViewProj`); fixed by fitting to the
    camera basis (ADR-1866).
  - Notes in `09-canyon-cirrus-2.md`.
- 2026-10-28: Rebased on main b22d4a91.
  - Analytic cirrus layer (ADR-1864) replaces the volumetric streak lobes: 20.7 → 0 ms of clouds, GPU 82.7 → 61.3 ms.
  - The wire and lattice effects are on hold, untouched.
  - Notes in `08-canyon-cirrus.md`.
- 2026-10-28: Canyon Phase 2 (geometric revelation). Notes in `07-canyon-phase2.md`.
  - The ray-march is removed (ADR-1861).
  - Mesh wire: the terrain's real triangles, read in place, with a travelling front (ADR-1862).
  - Material programs can hold 96 ops (ADR-1863), for the contours.
  - Audio routes on Feline Footwear and its stems.
- 2026-10-28: Canyon Phase 1c.
  - Sun raised to 14° (warm corridor and reflections).
  - Opening streaks traced to the warped band coordinate (ADR-1860); the snow patch was the flat colour.
  - Rock program generated by a script, with stronger ledges and breakup.
  - Flicker max 0.16.
- 2026-10-27: Canyon Phase 1b: rebased on main 9b4156ea.
  - Flicker traced. Cascade pull-back was fixed with `shadowCasterReach` (ADR-1858), and contact shadows are off.
    Irregularity max went 6.23 -> 0.25.
  - Look moved toward gate2e: sky, streak clouds, warm bounce, four-colour strata, ledge detail normals and baked
    occlusion (ADR-1859).
  - Notes in `05-canyon-phase1b.md`.
- 2026-10-27: Stage 0 research and plan; the Environment, generator, pass and tests (5 cases, 106 assertions, pass);
  Gate 1 media rendered.
- 2026-10-27: Canyon Phase 1: rebased on main 2bc26a7a; RTIN mesh conversion (8.27M triangles), engine sun, sky,
  water and material program; comparison, flythrough and perf; ray-march path kept only behind `"mesh": false`.
- 2026-10-27: Gate 2e: the cliff edge artifacts catalogued, traced and fixed at their causes (ADR-1856).
- 2026-10-27: Gate 2d: the real jitter (ray reconstruction precision) fixed and proven on the video; glassy water;
  46 s.
- 2026-10-27: Gate 2c: the jitter measured and fixed (scene-side), the river flows, geometry clean-up, a clean 2x
  supersampled render.
- 2026-10-27: Gate 2b: the formations removed (owner); the canyon fixed; the river and the flythrough; tests 7 cases.
- 2026-10-27: Gate 1 approved. Gate 2: the owner's fixes, the formations, the side canyons and the drift; ADR-1851
  and ADR-1852; tests 11 cases, 217 assertions, pass; Gate 2 media rendered.
