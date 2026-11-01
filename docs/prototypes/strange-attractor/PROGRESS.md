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
| portals R | dimensional portals: research, audit, plan (`12`-`15`) | **reviewed**: the owner sees "strong potential"; decisions in `15` §6 |
| portals A | the Matrix effect: the scan, geometric (world-space) mosh, glyphs, the site in the rock, the opening (`16`) | **approved by the owner** ("I think it looks good") |
| portals B | Gate A fixes; the portal view, the crossing at 32.0 s, the Moon and the lunar flight (`17`) | **approved by the owner** |
| portals C | the Moon polish; portal 2 in the crater floor, the dusk canyon, the full 60 s (`18`) | reviewed: the floor opening is the highlight |
| portals C 2 | portal 2's exit in a canyon wall, the dusk light, the floor's field, the Earth, the dive (`19`) | **merged** (main 214dafd8, Art Pass 1) |
| Art Pass 2, R2 | research, audit, cast sheet, beat sheet, music, proofs (`30`-`33`, `assets-pass2.md`) | **done, awaiting the owner**: media in `pass2-r/` |
| Art Pass 2, M1 | the Moon at night (terminator, basin, domes, crater portal), the return to the canyon at dusk, the first Sasquatch encounter (`35`; ADR-1885, 1886, 1888) | reviewed: "overall this direction is great"; revised in M1b |
| Art Pass 2, M1b | the world scaled to 0.4, the portal's water spill back, a short Moon, a real campfire (`36`; ADR-1893, 1894) | **approved, merged** (main 4bc5d3da) |
| Art Pass 2, M2 | Mars: the outer cliff portal, Cydonia (mesas, haze, three pyramids), the pyramid portal, the night return; campfire notes (`37`; ADR-1895, 1896) | **done, awaiting the owner**: media in `pass2-m2/` |
| Art Pass 2, M3-M5 | night and ocean, UFO, dawn (`33` §6) | not started: gated on M2 |
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

- 2026-11-01: Art Pass 2, M2 (`37-pass2-m2.md`; ADR-1895, 1896).
  - Journey legs: the canyon's `portals` after its `portal`, each at its own site; Moon and Mars legs on one canyon.
  - The outer cliff behind the camp opens onto Mars (crossing 69.655 s); Cydonia with mesas, yardangs, pads and haze;
    three eroded pyramids (Blender, original); the pyramid portal (crossing 101.4 s) home to the night canyon.
  - Campfire: dark stones, flame tongues, all three Sasquatches in view, no contour streaks at dusk or night.
- 2026-11-01: Art Pass 2, M1b (`36-pass2-m1b.md`; ADR-1893, 1894). Rebased onto main ab41c924.
  - The canyon at world scale 0.4 (`attractorWorld.worldScale`; every other length by `canyon_scale.py`); portal 1
    stays in its bank wall; the opening compared with Pass 1.
  - The corruption spills across the river again (ADR-1873's floor removed).
  - A short Moon: crossing 2 at 47.87 s (bar 25), the domes glimpsed on the rise, the Earth on the bass; the beat
    sheet re-timed (crossings on bars 36, 52, 62, 75, 88).
  - A real campfire (Blender logs and stones, two flame layers, embers, smoke); particle emitters beyond 1 km were
    clamped by a +-1000 m hard range (fixed); alpha-tested fur; the dusk contour lines at a quarter.
- 2026-10-31: Art Pass 2, Milestone 1 (`35-pass2-m1.md`; ADR-1885, 1886, 1888; 1884 revised). Rebased onto main
  3d73b32c (CHRONOS ADR-1930, the shared RTIN mesher).
  - The sequence runs 0-86 s on the 210.5 s cut: crossing 1 at 32.025 s, crossing 2 at 67.700 s (bar 35).
  - The Moon: 150 km curvature, a shadow basin, earthshine plus earthglow, three domes, the crater portal at the
    basin's edge with Gate C 2's dive.
  - One canyon for the whole journey (`@root` portal); dusk is a look. The dusk scene is removed.
  - Three Sasquatches (Mat Rex's Bigfoot, CC BY, rigged and carded in Blender) at a flickering campfire.
  - A foreground regression the return portal caused in the opening was found and fixed before the final render.
- 2026-10-31: Art Pass 2, Gate R2 (`32-pass2-research.md`, `33-pass2-plan.md`, `assets-pass2.md`; ADR-1887).
  - Audit: most machinery exists; missing are fur, morph targets, a cyclic journey (the canyon is visited four times),
    underwater shaft occluders and a portal at the camera.
  - Cast: realistic rigged humans from MPFB (CC0): the suited man, Nordics, and bases for the Sasquatch and the Gray.
    Sketchfab (realistic goat and fallbacks) needs an account: owner decision.
  - Proofs: terminator crossing (new lunar curvature radius), crater domes, earthshine, underwater depth, fur in fire
    light (Blender), the interior lighting, the Gray's triangle and beam.
  - Feline Footwear is 210.5 s; the long cut is about 262 s: music is an owner decision.
- 2026-10-31: Portals Gate C 2 (`19-portals-gate-c2.md`; ADR-1883, 1884, 1873 revised).
  - The second portal's canyon side is in the east wall (site B of four candidates). The Moon's floor is turned onto
    it (the portal now rotates), so the camera drops into the hole and comes out of the wall level; crossing 53.810 s.
  - The dusk scene is clipped at the exit plane for the camera (not the shadows), so it looks out through the rock.
  - Dusk light: a low sun raking the walls, cooler shadow; the floor's field pulled in; the Earth reframed.
- 2026-10-31: Portals Gate C (`18-portals-gate-c.md`; ADR-1883, 1884; 1873 and 1882 revised).
  - The Moon polish: real boulders, micro-relief and craterlets, rays, a 24-degree sun, a reshaped flight with a
    fresh crater beside the glide and a ridge that reveals the hero early, a 3.2-degree Earth (the Gate B "far cut"
    was the node scale cap of 100).
  - The river-bed blocks stay under the water (corruption `floor`); the crossed world is no longer drawn.
  - Portals nest: the hero crater's floor opens under the diving camera onto the dusk canyon; crossing at 51.830 s
    (bar 27); the camera joins the canyon's river run at 55.5 s and runs to 60 s.
  - The dusk canyon: a 2.8-degree sun, a violet-indigo sky, fills that keep the gorge up.
- 2026-10-30: Portals Gate B (`17-portals-gate-b.md`; ADR-1879, 1880, 1882; 1872/1873 revised).
  - Gate A fixes: a clear centre (shards stay on the rim, nothing camera-facing in the aperture), slab lips, a light
    in the opening, pulses from 16 s and glyphs from 19 s, the Moon through the 25 s slit, the 30–31.6 s shards thinned.
  - The Moon is its own scene, rendered by a child renderer through the opening and composited before the water.
  - The crossing at 32.025 s is seamless; the lunar flight leaves C1 with the arrival velocity.
  - The Moon: generated craters calibrated on the LROC Linné DTM, ~46k boulders, lunar-Lambert regolith, Blue Marble
    Earth. The blue shadow cast was a constant fallback ambient; `environment.fallbackAmbient: false` removes it.
  - NASA data is gitignored under `assets/space/`.
- 2026-10-30: Portals Gate A (`16-portals-gate-a.md`; ADR-1872, 1873, 1881).
  - Site D is the river's own east bank wall under the escarpment. The camera crosses at 32.0 s, on bar 17, and
    0–17.9 s is unchanged.
  - The scan grows out of the portal. The data mosh is on the world geometry: blocks, strips, frozen cells, echoes,
    the pull, shards.
  - The opening grows from a seam of light to a torn tunnel as the camera approaches.
  - After the owner's note, the glyphs are dense and start sooner.
  - The flicker spike at 15.13 s is fixed (a jump in the front's origin).
  - CPU-assets and GPU suites exit 0.
- 2026-10-30: Gate R reviewed. The owner sees "strong potential" in the Matrix effect. The direction is recorded in
  `15-portals-plan.md` §6:
  - the portal is cut into the rock and opens as the camera approaches;
  - the data mosh is on the world geometry, not the screen;
  - glyphs are limited to bands near the portal;
  - Earth (Blue Marble) and the LRO Linné DTM are approved;
  - no stars;
  - 20–30 m/s at the crossings;
  - Feline Footwear 0–60 s, with crossings on bar lines.
- 2026-10-30: Dimensional portals, Gate R (research, audit, plan). Brief `12`, coordinator notes `13`, research `14`,
  plan `15`; media in `portals-r/`. No production code: four throwaway proofs.
  - The portal view and crossing, proved by an offline composite of two engine renders. A per-pixel ray-plane
    aperture against the canyon depth gives correct perspective and occlusion, and there is no step at the plane.
  - The Matrix scan to breakdown on the real canyon. Existing tools give a uniform screen glitch (irregularity p90
    0.67). A world-masked prototype is localised and steadier (p90 0.054).
  - The Moon: a procedural crater field rendered by the engine with a lunar-Lambert material program. It needs no new
    shading code; the near field is weak.
  - Dusk look-dev with existing knobs.
  - Chosen: a world set with one renderer per world, a portal pass before water, a hand-over at the plane, one
    corruption field driving meshWire v2, surface code, shards, pull and mosh v2, and a crater-floor portal 2.
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
