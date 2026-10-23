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
