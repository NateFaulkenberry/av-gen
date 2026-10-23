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
