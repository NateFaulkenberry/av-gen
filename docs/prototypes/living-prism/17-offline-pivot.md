# The Living Prism, aurora revision: the offline pivot (2026-10-23)

## The owner's decision, verbatim

> I mean it sounds like our budget is already extremely tight and I still thinnk this scene needs a lot more visually - textures, rock formations, the crystal refraction right now is crude, obviously all the music modulation capabilities, aurora, etc. If you agree we're already too restricted in our frame budget to add a bunch more stuff, then let's keep working on this scene but switch our focus to it being an offline render, maybe we could make a live scene out of a macro shot at some point

**I agree.** At Phase 3 the live frame was 30.5 ms of GPU time against 33.3 ms. Everything still to come costs frame time:
- the aurora (Phase 4);
- the river's long-exposure reflection (Phase 5);
- textures and richer rock;
- refraction on more than two formations.

A 30 fps budget would force each of those into a cut-down version.

## What changes

1. **The aurora valley is an offline render scene.** The 30 fps live budget no longer constrains it. The quality target is the reference.
2. **New budget: render time.**
   - About 2 s per frame at most at 1080p, so iteration clips stay practical.
   - A 4K max-quality master overnight.
   - Perf is reported as seconds per frame, not ms against 33.3.
3. **Offline tier:** `--tier offline` (the Offline tier is pinned in a render, ADR-1083), `--supersample`, full shadows and volumes, `--render-limits unlimited` (no LOD or distance culling).
4. **Kept and approved:** the Phase 3 crystal geometry and material.
   - Refraction goes on every hero formation, and wherever else it improves the image.
   - The refraction itself is to be improved: thicker absorption and colour transmission, stronger visible distortion, a more vivid interior with the reference's bright pink and blue cloudy colour, and chromatic dispersion if achievable.
5. **The phases continue at offline quality:**
   - rock textures and materials (Poly Haven CC0 PBR, recorded in the manifest);
   - richer rock formations;
   - the aurora (Phase 4);
   - the river's long-exposure aurora reflections (Phase 5);
   - circuits;
   - the music modulation (Phase 7), with stems and roles (ADR-1630).

   Offline extras such as temporal accumulation and motion blur are allowed wherever they help the long-exposure look; seek equals play in a render.
6. **The live project is on hold.**
   - `aurora-valley.json` stays in the branch as a build output, but it is not registered as live (it is not in `examples/index.json`).
   - The offline project, `aurora-valley-offline.json`, is the deliverable.

## The refraction: what the engine does, and what would need an engine change

ADR-1603's refraction is one screen-space layer:
- one copy of the lit opaque frame, read at a UV offset by the view-space normal (`strength`);
- absorbed per channel over the depth behind the surface (`absorption`, at most `thickness` metres);
- with the surface's own colour over `opacity` of it.

**Possible now, by authoring alone:**
- stronger distortion (`strength`);
- deeper, coloured absorption;
- less of the surface's own colour over the transmission (`opacity`), so the interior and the world behind read through;
- supersampling, which removes the edge prisms' aliasing.

**Would need an engine change (flagged, not built):**
- **Chromatic dispersion.** `fs_proc_refract` reads the copy once. Dispersion is three reads at three offsets (R, G, B scaled by IOR per channel). It is a small, contained shader change; offline it is affordable.
- **Crystal seen through crystal.** The copy holds only the opaque frame, so a refractive crystal behind another refractive crystal is missing from it, and the far one vanishes where they overlap. Fixing that needs multi-layer refraction: depth peeling (N passes, each copying the frame) or OIT. This is a larger change, and the cost is irrelevant offline. Until it lands, the layout keeps refractive formations from overlapping much (the layering check), and distant formations behind a hero can stay opaque.
- **A higher-quality copy.** The copy is already full HDR at the render size. With `--supersample` it is taken at the supersampled size, so no change is needed.

## Elements for a possible live macro scene later

The pieces that suit a close, static, live shot of this world:
- **A single hero crystal formation**: the Phase 3 geometry, the `au_crystal` program, the edge prisms, and refraction on one object, which is cheap.
- **A small patch of mossy bank with a few scan rocks**: the ground program and decimated scans.
- **A strip of the river surface**, with SSR on a small area.
- **The aurora palette roles and their bindings**: the shared representation. The macro scene would reuse the same palette states and recolour with the music.
- **A slice of aurora sky** behind the crystal, in shallow depth of field.

Avoid in a live macro: refraction on many objects, the wide terrain with noise layers, and the far ridges.

## Phase 3b gate: the crystal at offline quality (2026-10-23)

Media: `~/Desktop/av-gen-review/82-living-prism/aurora-phase3b/`

| File | Shows |
|---|---|
| `phase3b-hero-vs-reference.jpg` | the reference's crystals beside the two heroes |
| `phase3b-mid-vs-reference.jpg` | the mid groups beside the reference's mid crystals |
| `phase3-vs-3b-heroes.jpg` | Phase 3 (live quality) against 3b, same crops |
| `phase3b-frame.jpg`, `phase3b-vs-reference.jpg` | the full frame |
| `phase3b-palette-pair.jpg` | Green Veil against Rose Veil, offline |

**Refraction (author-side, no engine change):**
- On all eight hero formations: L1, L1b, L2, L3, R1, R2, R3, R4.
- Stronger distortion: strength 0.05 → 0.12.
- Deep coloured absorption [0.15, 0.6, 0.05] over up to 4 m. Green goes first, so the stone transmits violet-pink.
- Less surface over the transmission: opacity 0.35 → 0.3.
- Distant formations (L4-L6, R5, R6) stay opaque, because of the single-layer copy (above).

**Interior:**
- The reference's dark navy stone with saturated pink and blue clouds through its full height.
- An OKLab ramp of navy, pink, navy, blue, driven mostly by a slow object-space noise, with a gentle height term and the view-angle shift kept for depth.
- New palette role `crystalBlue`.
- Interior gain 0.85 → 1.6; environment gain 0.55 → 0.35 (it washed the tips pale).

**Offline render** (`--tier offline --supersample 2 --render-limits unlimited`, the rebased Release binary, AC power):

| Render | Time |
|---|---|
| 1080p, 30 frames | 9.0 s of render loop: **0.30 s/frame** |
| 4K, 3 frames | **1.27 s/frame** |

- Each run also spends about 45 s once on start-up (scene load, stem analysis, shaders).
- A 3-minute master at 4K is about 2 hours.
- Both are well inside the 2 s target, so there is room for 3-4x supersampling and the later phases.
- Supersampling also removed the edge prisms' aliasing.

**Still open:**
- Dispersion, and crystal seen through crystal: engine changes (above).
- The reference's crack network is denser and sharper.
- The rock, aurora, river and circuits are the next phases at offline quality.

## Phase 3c: the crystal pass against the reference (2026-10-23)

The owner approved the plan. Dispersion goes to the engineering agent (an ADR-1603 extension); crystal-through-crystal refraction comes later.

Media: `~/Desktop/av-gen-review/82-living-prism/aurora-phase3c/`

| File | Shows |
|---|---|
| `phase3c-hero-vs-reference.jpg` | the reference's crystals beside the heroes |
| `phase3b-vs-3c-heroes.jpg` | 3b against 3c, same crops |
| `phase3c-mid-vs-reference.jpg` | the mid groups beside the reference's mid crystals |
| `phase3c-frame.jpg`, `phase3c-frame-rose.jpg` | the full frame in both looks |

1. **Selective edges.** Prisms now run only along the silhouette edges as the camera sees each formation (one face toward it, the other away) and about 15% of the other ridges. Each is tapered toward its ends, so its strength varies along its length. The dim set is gone: every other facet boundary reads by value contrast alone.
2. **Facet contrast.** A per-facet random sorts the faces into three classes:
   - **Dark** (about a third): near-black, the interior at 5%.
   - **Mirror** (about a quarter): the aurora colour they face, reflected at full strength; roughness 0.025, so the lights give sharp highlights; little interior.
   - **Nebula** (the rest): a steep, high-contrast object-space noise through a navy, pink, blue and teal OKLab ramp, so the interior breaks into vivid patches instead of an even lilac wash.

   The program also writes roughness now. It is 48 ops.
3. **Cracks.** Object-space Voronoi edges at twice the frequency, thinner, branching across facets, in runs over about half the stone. Over most of the stone they read as a honeycomb net, so they are kept partial.
4. **Shapes.**
   - The hexagon's corners are jittered well off regular (±0.22 of a sixth of a turn), so the faces differ in width; the radii vary 0.8-1.12.
   - The cross-section is sometimes tabular.
   - Tips are 30-45% of the length (was 18-30%), with a narrower second stage: long, sharp blades.

**Offline render:** 1080p, 2x supersample, `--tier offline`: 30 frames in 8.5 s, **0.28 s/frame**.

**Checks:** clearance minimum 9 m (L5); terrain sweep at most 15% hidden and 22% buried (R5).

## Rock pass (2026-10-23)

**Textures.** The Poly Haven CC0 scans now keep their own 1k PBR textures (albedo, normal, roughness), embedded in the build GLBs. The GLBs are gitignored, and the manifest is updated. The scene tints them a cool lavender-grey with no program, so the scanned detail reads.

**Terrain textures need an engine change (flagged).** Material programs take no texture inputs (ADR-030), so the terrain stays procedurally shaded. Real rock and moss PBR on the terrain needs either a texture input for programs or a triplanar texture layer on terrain materials.

**Richer rock, placed by view:**
- `build_scene.auto_cliffs`: candidate cliffs on both walls are projected through the camera and kept only where the camera sees bare wall. Each must be clear of every crystal outline, with its top visible past the terrain.
- `auto_outcrops`: 12-30 m textured boulders and moss-rocks on the open foreground banks.
- Cliffs now sit on the ground at their centre. Placed from the lowest corner, a cliff on a steep wall was swallowed by its uphill side.

**Still open:** the cliffs read as texture on the walls rather than as the reference's dramatic rock faces. Bigger, layered rock geometry, and terrain textures through the engine, are the next steps for rock.

## Phase 4 gate: the aurora (2026-10-23)

Media: `~/Desktop/av-gen-review/82-living-prism/aurora-phase4/`

| File | Shows |
|---|---|
| `phase4-frame.jpg`, `phase4-frame-rose.jpg` | the full frame in both looks |
| `phase4-vs-reference.jpg` | the full frame beside the reference |
| `phase4-sky-vs-reference.jpg` | the sky beside the reference's sky |
| `phase3c-vs-4.jpg` | before and after |
| `phase4-palette-pair.jpg` | one palette switch |
| `phase4-motion.jpg` | the sky at t = 10, 30 and 60 s |

**What changed (spec 6.1-6.3).** There are two layered curtain systems:
- **Main:** green to teal to violet, 2600 m out, two curtains, deep folds (wave amplitude 1.2) and fine rays (complexity 90, filaments 3), with dark gaps from the wave scale (6).
- **Second layer:** pink to violet, 3300 m out, its own wave scale (3.2), so pink and green regions interleave across the sky by azimuth instead of stacking.

Both rise from below the ridgeline and run out of the top of the frame. The effect's curtain base is a straight horizontal line, and its top a rim, so the mountains hide one and the frame edge the other. Each earlier pass had shown one of them as a hard edge across the sky. The aurora now fills the upper half of the frame with dark sky between the curtains (6.3, keep dark regions), and the palette still drives every colour.

**Motion (6.4):** slow flow and drift (0.015-0.02), which changes the curtains gently between t = 10, 30 and 60 s. No audio yet (Phase 7).

**Offline render:** 30 frames in 9.0 s at 1080p (2x supersample, `--tier offline`): **0.30 s/frame**. The larger aurora costs no measurable render time.

**Gap against the reference, and what it would take:**
- The reference's aurora is broad ribbons arching and swirling across the sky, with bright lower edges that curve. This effect draws vertical curtains on cylinders around the camera, with a straight base, so mine read as light columns rather than sweeping arcs.
- Getting the reference's shape needs an engine extension of the aurora effect (ADR-230): either a curtain whose base height follows a noise or path in azimuth, or a ribbon defined along a 3D path. Flagged for the owner and engineering.
- Without it, the next art levers are colour regions (a third layer, cyan or blue) and the 6.5 lighting coupling. The palette already lights the valley through the fill lights and the crystal environment.

## Phase 5 gate: the river (2026-10-23)

The owner approved the plan ("go for it"). The aurora ribbon extension (ADR-230) and terrain texture inputs go to an engineering agent after dispersion; I adopt each as it merges.

Media: `~/Desktop/av-gen-review/82-living-prism/aurora-phase5/`

| File | Shows |
|---|---|
| `phase5-river-vs-reference.jpg` | the river beside the reference's, in both looks |
| `phase4-vs-5-river.jpg` | before and after |
| `phase5-frame.jpg`, `phase5-frame-rose.jpg`, `phase5-vs-reference.jpg` | the full frame |

**The water (spec 7), `build_scene.water()`, built per look:**
- **No mint glints.** The surface is nearly still (ripple 0.05, no chop), so the aurora no longer breaks into hundreds of small glints.
- **Long-exposure reflection.** A broad, flow-aligned swell at a long wavelength (undulation 0.8 at 0.02/m) stretches the reflection along the river's course. The reflection is spread along the view (streak 1.6), filtered by 10 rays per pixel and by calm and rough regions (roughness variation 0.6), with a 1500 m SSR at 96 steps. The aurora reflects as a soft streak down the river: mint in Green Veil, pink in Rose Veil.
- **Flowing colour.** Luminous flowing bands: the under-surface glow's patches drift with the current, so they follow the river's shape. They are large (0.025/m), cover half the channel, and take the second aurora colour: pink in Green Veil, teal in Rose Veil.
- **Dark base.** The body and bed are near-black. The SSR is at half strength so the environment reflection, in a lavender tint from the aurora's top colour, carries the rest.
- **Tints.** The reflection tints are not palette-bindable (only the water body colours are), so each look's build sets them from its own aurora colours.

**Tried and rejected:**
- `tears` (stretched along the valley): their seams drew as dashed lines down the river, like road markings.
- Smaller glow patches (0.05/m): blotchy camouflage spots.

**Temporal stability (7.4).** Over a 1 s clip (30 frames, 1080p offline), the near river changes by a mean of 0.11/255 between consecutive frames (min 0.10, max 0.11). There is no flicker or crawl, only the slow drift. No temporal accumulation is used.

**Offline render:** 30 frames in 9.7 s at 1080p (2x supersample): **0.32 s/frame**.

**Gaps:**
- The reference's water has fine, directional streak lines in several colours (pink, teal, lavender, white highlights). Mine has broad flowing bands and one soft aurora streak, without the fine flow lines. The engine has no flow-aligned streak texture for the water (tears follow a fixed direction and read as dashes).
- The far reach still reads grey (the horizon haze reflected).

**Not done yet: environmental integration (spec 6.5).**
- In place already: the palette drives the aurora, both fill lights, the crystals' environment colour and the water.
- Not yet done: a stronger aurora-coloured light on the terrain (the ground term), and the aurora colours in the moss and rock.

## Phase 5b gate: environmental integration and circuits (2026-10-23)

The owner approved Phase 5. A water flow-streak engine extension (ADR-1643) is queued after the aurora ribbons; I adopt it when it lands.

Media: `~/Desktop/av-gen-review/82-living-prism/aurora-phase5b/`

| File | Shows |
|---|---|
| `phase5b-vs-reference-green.jpg`, `phase5b-vs-reference-rose.jpg` | the full frame beside the reference, in both looks |
| `phase5-vs-5b.jpg` | before and after |
| `phase5b-frame.jpg`, `phase5b-frame-rose.jpg` | the full frame in both looks |

**The aurora as light on the valley (spec 6.5).** All palette-bound, so one switch moves it with the sky:
- **Ground:** `au_ground` gained a soft emissive term (now 28 ops). Sky-facing ground (by the normal's upward component, smoothstep 0.3-1) glows in the aurora's colours, the two aurora roles mixed by the large patch noise. The gain is 0.035 of the role colour (`GROUND_AURORA`).
- **Distance haze:** the fog colour is bound to the aurora's top colour (gain 0.15, `FOG_AURORA`). The distance, including the far river reach, now reads violet instead of grey haze.
- **Crystal light:** each of the eight hero formations has a faint point light (range 90 m, no shadows), alternating between the crystal body colour and the low aurora colour. It is subtle, as intended.

**Circuits (spec 8), `build_scene.build_circuits`:**
- A restrained network on the near banks: about 25 traces running along the river at 70-140 m from its centreline, with circuit jogs, a few branches, and node disks at the ends.
- The traces are draped on the engine's ground every 2 m and lifted 0.25 m. They are kept off wet ground and out of crystal footprints.
- They are 0.7 m-wide emissive ribbons in one generated GLB (written directly by the build), bound to the low aurora colour.
- No audio yet (Phase 7 can pulse them along their length).

**Offline render:** unchanged in practice, about 0.3 s/frame at 1080p.

**Open:**
- The left foreground bank reads a little flat mauve under the aurora light.
- The crystal point lights are barely visible.
- The fine flow lines in the water wait for ADR-1643.

## Phase 5c gate (2026-10-23)

The owner's 5b verdict, verbatim: "Yeah send it back lets nix the circuitry lines for now".

Media: `~/Desktop/av-gen-review/82-living-prism/aurora-phase5c/`

| File | Shows |
|---|---|
| `phase5c-vs-reference-green.jpg`, `phase5c-vs-reference-rose.jpg` | the full frame beside the reference, in both looks |
| `phase5b-vs-5c.jpg` | before and after |
| `phase5c-frame.jpg`, `phase5c-frame-rose.jpg` | the full frame in both looks |

**Circuits removed.** The generator stays in `build_scene.py` behind `AU_CIRCUITS=1`, off by default. With the flag unset there is no node, no GLB and no palette binding, so the projects and the scene carry nothing of it.

**The green valley restored.** The aurora colour now lives in the sky, the water and the crystals, as in the reference:
- **Ground aurora light:** 0.035 → 0.008 of the role colour, and only on sky-facing crests (normal.y 0.78-1) as a soft rim.
- **Moss:** a rich, dark, fertile green (deep, lit, cool green, only a trace of olive).
- **Rock:** grey-brown banded instead of blue-grey.
- **The left bank's flat mauve.** Three sources added up there:
  - the violet raking fill: now half the intensity (0.16) and half its palette gain, tinted halfway toward the moss green;
  - the ground term: cut, as above;
  - the pink crystal light beside L1: all eight crystal lights halved, to 700, still subtle.
- **Distance haze:** kept violet. It sits against the reference's pink-peach horizon well enough. The far river carries it instead of grey.

## Phase 5d gate (2026-10-23)

The owner's 5c notes, verbatim: "the lighting of the banks etc, doesnt bother me we dont have to keep trying to match the reference material that closely. here are some revisions I'd like though. I've add an arrow, let's move the camera up so the bototm of the scene is about here. The red square highlighted areas lets fill with more rocky features, cliff like structures, we should have some assets that work - not just tiny rocks, but things that butt up and fit between the crystals. let's also find a good quality moss texture and start covering the ground with moss - areas circled in blue are where I want mossy green earth (or whatever color it ends up being under the sky lighting). moss should be used to cover up places where crystals meet the earth to make them blend better"

The coordinator relayed the annotation coordinates, since the screenshot was not on disk. Dispersion (ADR-1640) merged to main 44362df9; the branch is rebased onto it.

Media: `~/Desktop/av-gen-review/82-living-prism/aurora-phase5d/`

| File | Shows |
|---|---|
| `phase5d-frame.jpg`, `phase5d-frame-rose.jpg` | the full frame in both looks |
| `phase5c-vs-5d.jpg` | before and after |
| `phase5d-details.jpg` | the rock masses and the moss carpet |
| `phase5d-dispersion.jpg` | dispersion on the heroes |

**Camera.** It tilts up 7.3° (the target y 22 → 76), so the 5c frame's y = 915 line is the new bottom edge. The 5c camera is kept as `layout.CAMERA_5C`, because the owner's marks are in its pixels.

**Rock masses (the red boxes).**
- `build_scene.ROCK_BOXES` holds the three boxes. `layout.screen_to_ground()` raycasts them through the 5c camera onto the engine's ground.
- Each box gets two or three textured Poly Haven cliff scans, side by side, scaled about 1.6-2.1x a third of the box's world width, facing the camera and seated at their centre.
- They are deliberately not filtered by the crystal footprints: they butt up against the crystals and fit between them.

**Moss (the blue ellipses and every crystal base).**
- Texture: ambientCG Moss002 (CC0), 2K, in the gitignored asset folder, recorded in `assets/aurora-valley.manifest.json`.
- The terrain cannot sample textures until ADR-1642, so the moss is a carpet. `build_scene.build_moss_carpet` rasterises every moss spot into one 1 m world grid: a union of noisy discs over the owner's six areas (pixels inside each ellipse raycast through the 5c camera) and a ring at every crystal's base.
- The marked cells become one mesh draped on the engine's heights 0.12 m up, UV-mapped in world space (one tile per 2 m), with the moss colour and normal maps embedded. It covers about 37,600 m², leaves wet cells out, and has no overlaps or rims.
- Tried first and removed: individual domed moss pads. They stacked like shingles with lit rims.

**Dispersion (ADR-1640).** `"dispersion": 0.2` on the eight refractive heroes, within the engineer's guidance of 0.1-0.3 at strength 0.12. Spectral fringes now show where the glass bends the world.

**Offline render:** 30 frames in 10.4 s at 1080p (2x supersample, `--tier offline`): **0.35 s/frame**.

**When ADR-1642 (terrain texture inputs) lands:** move the moss from the carpet mesh into the terrain material.

## Phase 5e gate (2026-10-23)

The owner's notes on 5d, verbatim:
- "the moss texture is not reading correctly to me - nix the moss idea go with more of a bushy look (more like the reference art)"
- "start working on making more of the original cliffs look like rocky cliffs"
- "fill in more in between the crystals with rocky cliffs, moss covered rocks, green bushy bushes"
- "why do all the crystals look aliased and jagged lines"
- "some of the terrain geometry looks questionably drawn, lets make sure it's all representative of actual land scapes"

Media: `~/Desktop/av-gen-review/82-living-prism/aurora-phase5e/`. All PNG, so there are no JPEG artefacts in the crops.

| File | Shows |
|---|---|
| `phase5e-frame.png` (4K downsampled to 1080), `phase5e-frame-4k.png`, `phase5e-frame-rose.png` | the full frame |
| `phase5e-bushes-left.png`, `phase5e-bushes-right.png` | the bushes against 5d |
| `phase5e-aliasing-hero.png` | the hero crystal: 5d gate, 5e at 1x, 5e final |
| `phase5e-terrain-audit.png` | the floating cliff lumps before and after |

**The moss carpet is removed:** the code, the mesh, the ambientCG texture files and the manifest entry.

**Bushes.**
- I checked the library first. The Poly Haven shrubs (`assets/nature/plants/shrub_01-04`) are 0.5-2.5 m alpha-dithered leaf cards, sub-pixel or shimmering at 150-500 m.
- So the bushes are built in Blender (`build_bush`): four rounded foliage masses of 6-10 noise-displaced lobes each, with flattened bases.
- They are shaded by a new program, `au_bush`: a dark, saturated green varied per instance and by a leafy clump noise, lighter tops, and a soft rim of the low aurora colour on top (palette-bound).
- About 1,370 instances, overlapping into masses: on the owner's six areas, around every crystal base, and between the crystals.

**Between the crystals.** `between_spots` fills along each bank between neighbouring formations with bush clusters (50%), Poly Haven moss-covered rocks (30%: `rock_moss_set_01` and the newly added `rock_moss_set_02`), and rock masses (cliff scans and boulders, 20%).

**Rocky walls.** View-placed cliff scans may now stand behind crystals as well as beside them (only a cliff in front of a crystal is refused). They are larger (120-220 m), more of them, and further down the valley.

**The terrain audit (the floating lumps).**
- Cause: view-placed cliffs stood proud of the slope, so their scans' ragged, open-backed edges silhouetted against the ridge.
- Every cliff is now support-checked. Its base is buried at its lowest corner (no floating lip), and its top is tucked under the slope uphill of it. It is sunk until both hold, at most 65% of its height, and a cliff that still stands proud is dropped.
- 15 of 26 view-placed cliffs were dropped.
- Trade-off: the walls read as continuous land again, with fewer rock faces. Rock texture on the steep terrain (ADR-1642) is the clean route to "rocky cliffs" on the walls themselves.

**Aliasing.**
- **Was the 5d frame supersampled?** Yes. The log says `render scale 2.00: scene target 3840x2160 -> output 1920x1080`, so 2x alone was not enough for 1-3 px lines.
- **Edge prisms:** one mesh per formation, widened so a line is never under 1.6 px at its distance (×1.0 to ×2.2, `edges.json`), with the glow divided by the same factor so a widened line is not brighter.
- **Cracks:** the line's width is the authored width or about 1.5 px (from the `footprint` input), whichever is wider, its edge a smoothstep over that width, and its energy scaled down as it widens. Cracks fade on distant crystals instead of breaking into stairs.
- **Inclusions (the speckle stars inside the crystals):** removed. They were hard-thresholded hash cells, which cannot be filtered.
- **Supersample:** `--supersample` is capped at 2 in the engine. The gate and final renders are now 4K at 2x supersample (an effective 4x per axis at 1080p), downsampled to 1080p with Lanczos for the gate.

**Render time:**

| Render | Time |
|---|---|
| 4K at 2x supersample (7680x4320 internal), 30 frames | 36.2 s: **1.21 s/frame** |
| 1080p at 2x | about 0.35 s/frame |

**Checks:** clearance minimum 9 m; layering minimum 51%; terrain sweep at most 15% hidden and 22% buried.

## Standing rule: consistent renders (the owner, verbatim: "consistent renders please")

Every image in a gate comes from one build, rendered in one run with identical settings:
- 4K (3840x2160) at `--supersample 2`, `--tier offline`, `--render-limits unlimited`;
- the same time t (10.000 s) and the scene camera;
- downsampled to 1080 with Lanczos.

Both looks and every crop come from those same renders. A before/after pair uses the same settings on both sides, and the "before" is re-rendered when it was made any other way. The build SHA, settings and t go into each image's caption.

`build/aurora/gate.sh <dir>` does this: one build of both looks, one run, and `stamp.txt`.

Why the rule exists: in 5e, `phase5e-frame-rose.png` was rendered at 19:47, before the cliff support check existed, and `phase5e-frame.png` at 19:52, after the check had dropped 15 cliffs. So the rocks "went missing" between the looks. No palette binding hides geometry.

## Phase 5f (2026-10-23)

The owner's notes, verbatim:
- "okay but we loose all the speckles inside the crystals thats not acceptable also the bushes do not look good please use a more realistic texture that matches the reference art"
- "the entire ground should be covered in bushy texture, go beyond just the areas I originally highlighted, all crystals should appear as though their bases are covered by bushes, all ground = bushes, rocky features for the ledges - why did you remove some from the left hand side?"
- "try to add an assortment of smaller rocks along the sides of the river bed in aesthetically pleasing areas too"
- "it can make river rocks larger if they are hard to read"
- "try to prevent it from mistakenly adding stuff like this though"
- "dont have it stop until walls are reading as rock / moss covered rock textures and the ground is bushy and filling the scene"

**Speckles.** Restored as geometry (`build_stars`): small octahedra just under the faces each formation shows the camera, at about 0.35 per m², sized never under about 1.6 px at the formation's distance and dimmed by the square of that widening. The crystal program had no op budget for filtered stars.

**Bushes.**
- Poly Haven CC0 `shrub_02` (modelled leaves with their own textures), `fern_02` for low clumps, and a 5k-face copy of `shrub_02` past 250 m.
- `ground_cover` raycasts a jittered 22 px screen grid of the camera onto the engine's ground, and every gentle hit (normal.y ≥ 0.72) gets bushes sized to its screen share. That covers all visible ground, plus the owner's areas, every crystal base and the gaps between the crystals: about 3,100 instances.

**No debris on the skyline.** No foliage where the ground one or two grid rows up is sky or much further away (a crest), or above 55 m.

**Rock on the walls.**
- Every steep hit, and every gentle wall top above the bush line, gets a rock-face, cliff or mossy-rock scan facing down its slope.
- Rock sits at its centre's height on steep ground; from the lowest corner, the uphill side swallowed it.
- No rock on the skyline rows, or in front of a crystal: inside its screen outline and nearer than 40 m behind it.

**Left cliffs reseated, not dropped.** A wall cliff that stands proud is moved up into the wall (0-150 m) and shrunk if needed (×1, 0.8, 0.6), taking the first supported variant. Three cannot be seated: cliffA at (-221, -660), cliffB at (-115, -132), cliffB at (-142, -616).

**Backed by land.** Every placed piece's top must have ground behind it on the camera ray (within 600 m). Otherwise it is sunk 30% more, or dropped. This removed the flat slab on the top-left skyline.

**River rocks.**
- Groups of 4-9 boulders, stones and mossy rocks, 4-15 m, at the inside of the river's bends.
- They sit at the waterline, found past 40% of the reach's width because the raised water leaves a shallow bed, and only where they land on screen. Foliage keeps a little clear of them.
- They are still modest at 1080p.

**Right far ridges.** Every far ridge rolls off on the right of the river, from about 45% of the half-view at its distance to near nothing at the frame edge. The right walls are lower.

**ADR-1642 prepared.**
- CC0 textures in the gitignored `assets/nature/textures/`, recorded in the manifest: Poly Haven `rock_face`, `mossy_rock` and `forest_leaves_02`, 2K.
- `prog_ground_textured` (three triplanar colour reads and one triplanar normal, mixed by slope and noise) is built when `AU_TERRAIN_TEX=1`, to the preview's schema. It stays off until the merge.

**5f gate** (`~/Desktop/av-gen-review/82-living-prism/aurora-phase5f/`; all from one run, build f2a4909a, 4K at 2x, t = 10, captions stamped):

| File | Shows |
|---|---|
| `phase5f-frame-green.png`, `phase5f-frame-rose.png`, `phase5f-frame-green-4k.png` | the full frame |
| `phase5f-speckles.png` | the hero crystal before and after |
| `phase5f-left-side.png` | the left side before and after |
| `phase5f-skyline.png` | the left skyline before and after |
| `phase5f-right-ridge.png` | the right far ridge before and after |
| `phase5f-bushes.png` | the bushes and river rocks |

- The "before" frames were rendered at the same settings and are labelled with their build.
- The two looks differ only in colour: their edge maps differ on 0.64% of pixels, the aurora curtains' own hue edges.
- Render time: 4K at 2x supersample, 15 frames in 27.2 s: **1.81 s/frame**, inside the 2 s budget.

## Phase 5g gate: terrain textures (2026-10-23)

ADR-1642 merged to main 2d211c8e, and the branch is rebased onto it. The textured ground (`prog_ground_textured`) is now the default; `AU_TERRAIN_TEX=0` builds the procedural one.

- **Textures:** Poly Haven CC0 `rock_face` on steep ground (0.025 tiles/m, a large tile so its features survive distance, with its normal map at strength 1.6), `mossy_rock` between, and `forest_leaves_02` on the flats under the bushes.
- **Mixing:** triplanar, mixed by slope plus a patch noise. Albedo ×1.1 into the night. The aurora rim stays on the crests and stays palette-bound (op 18).
- **Haze:** first test, at the 5f fog density (0.0006), the distant walls hardly changed, because the haze swamped the detail. The density is now 0.00042, so the distant walls show their rock.

Media: `~/Desktop/av-gen-review/82-living-prism/aurora-phase5g/`, one run (build feefc323, 4K at 2x, t = 10, stamped).

| File | Shows |
|---|---|
| `phase5g-frame-green.png`, `phase5g-frame-rose.png` | the full frame in both looks |
| `phase5g-walls-left.png` | the left wall, 5f against 5g |
| `phase5g-walls-distant.png` | the distant walls, 5f against 5g |
| `phase5g-floor.png` | the floor, 5f against 5g |

The 5f "before" is the 5f gate run, at the same settings.

**Render time:** 4K at 2x supersample, 15 frames in 27.5 s: **1.83 s/frame**. The textures cost no measurable time over 5f.
