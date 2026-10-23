# The Living Prism 15: the aurora revision, Phase 1 (audit, research, references, architecture, budget)

**Spec:** `14-art-direction-revision.md` §13 Phase 1. **Branch:** `scene/living-prism-aurora`.

**The reference** is `~/Desktop/av-gen-review/82-living-prism/new-direction.jpg`, a copy of `~/Desktop/living
prism/new-direction.jpg` (1024 x 1024). It is not committed, because its provenance is unknown.

**Reference sheet:** `82-living-prism/aurora-phase1/reference-sheet.jpg` (`aurora/scripts/ref_sheet.py`). It holds
eight annotated crops and the sampled palette.

**Frame target:** 1080p30 (33.3 ms), the owner's decision. Every timing is also reported against 8.33 ms.

## 1. What the reference is (from the image, not from memory)

**Composition:**
- **Camera:** a high, wide view down a valley.
- **River:** an S-curve that starts wide at the bottom centre and narrows to the horizon at the frame's centre. It is
  the leading line.
- **Mountains:** dark, layered, framing both sides and converging at the vanishing point.
- **Sky:** the aurora fills the top 40%. A warm pink-peach glow sits at the horizon under it.
- **Format:** the image is square, the live output 16:9. The adaptation keeps the river's leading line and the two
  bank masses, and widens the sky.

**Crystals:**
- **Formations:** about seven, around twenty prominent tips:
  - a tall, leaning left cluster near the camera (6-10 shards);
  - a big wide pair on the right bank, tilted;
  - mid-distance spires on both banks;
  - small distant tips at the horizon.
- **Shape:** long, irregular shards, not hexagonal prisms. Large flat faces, few facets, sharp oblique terminations.
- **Material:** clear bodies:
  - pink, cyan and violet nebula-like interiors;
  - pale, bright facet-boundary lines;
  - dark, thin internal crack lines;
  - fine star-like speckles inside;
  - the aurora's green on the faces turned toward it.

**Aurora:**
- **Form:** broad vertical curtains with fine striation.
- **Colour:** green and teal at the base, violet, pink and lavender above and between.
- **Structure:** clear dark gaps between the folds; a few stars.

**River:** a long-exposure look:
- smooth, elongated streaks of pink, mint and pearl along the flow;
- dark between the streaks;
- banks edged pale.

**Valley:**
- deep green moss and grass;
- near-black rock;
- the green is dark, never bright.

**Circuits:**
- thin mint lines with right-angle branches and small node rings on the near banks;
- dense in the lower right, sparse elsewhere.

**Palette (sampled):**
- **Darks:** dark slate and teal hold about 55% of the image.
- **Mid-tones:** blue-grey, about 25%.
- **Pastel accents:**
  - pearl (212, 209, 213), 7%;
  - lavender (167, 158, 191), 6%;
  - mint (138, 197, 197), 5.5%;
  - pink in the crystals and the river.
- **The finding:** the image is **far less saturated than it reads**. It is pastel luminous accents over dark slate,
  not neon. The palette and the grade must hold that.

## 2. Audit of what exists (source and builds, checked)

| Need | Exists | Verdict |
|---|---|---|
| **Aurora** | `atmosphere_fx.wgsl`, `world::Aurora` (ADR-230): curtains as 1-5 vertical cylindrical shells solved per view ray at the far plane, a three-colour vertical ramp, edge brightness, filaments, sparkle, horizon glow, audio depths, styles, and a **`ground` term** (a pool of aurora-coloured light on the ground). Examples: `sonic-vfx/aurora-tundra.json`, `glowmere-atmospherics` | **Reuse unchanged**, authored as an effect. Phase 4 tunes the shape and adds a second aurora (two are allowed) for the pink and violet layer |
| **Stars** | the `stars` effect (ADR-1350: the whole sky) | reuse |
| **Sky gradient** | the procedural sky's zenith, horizon and ground colours | reuse: a dark slate zenith, a pink-peach horizon |
| **Valley and river** | terrain node with world features (`river` polyline with water, `ridge`/`valley`), as biovalley and the grotto's pool use | reuse: one winding river feature, two valley walls |
| **Water** | ADR-1286 cinematic water: SSR, transmission, undulation, streak (view-stretched reflection rays), roughness variation, **tint patches** (a second reflection tint), glints. `waterSsrScale` ladder lever (ADR-1603) | reuse. **Risk:** SSR cannot reflect the aurora, which is drawn at the far plane after the depth the march reads. The fallback is the environment cube, and the cube does not carry the aurora. **Plan:** test in Phase 2. If confirmed, the long-exposure streaks come from `tintPatches`/`reflectionTint`/`streak` and a bound aurora colour: a stylised reflection, which is what the reference shows anyway |
| **Mountains and rock** | Poly Haven CC0 in `assets/terrain`: `rock_moss_set_01/02` (moss rocks), `coastal_cliff_02/04`, `mountainside`, `rock_face_01`, `namaqualand_cliff_02` (formations). Rainbow World's rock adaptation (`blender/rocks.py`). Biovalley's distant ranges (`ranges.glb` approach) | reuse: moss rocks on the banks, cliff formations as the valley walls, low-poly ranges for the far mountains |
| **Crystals** | the grotto's `gem` builder (two-tier terminations, chips, sub-crystals), the glass programs (iridescent rim, glitter, accent cores, veils), refraction (ADR-1603), thin film | **reuse, modified:** shards rather than hex prisms (fewer, larger faces, tabular, oblique tips). The programs gain fractures (thin voronoi edges inside the faces), speckles and aurora coupling |
| **Aurora coupling** | the palette system binds named roles to anything: material constants, lights, water, fog, the aurora's colours (effect parameters are ordinary parameters) | **the shared representation:** three aurora roles (`auroraLow`, `auroraMid`, `auroraTop`) drive the aurora, the crystals' rim, interior and edge colours, the water's tints, the ground term and the fill. One palette change moves the whole world (spec 5.4, 10.4). Refraction shows the lit sky through the glass where it is in the frame copy (Phase 3 tests whether the aurora is in that copy) |
| **Circuits** | the arch-run technique: a baked path distance in `instanceIndex`, a travelling band in the program | reuse for travelling pulses. The lines themselves are Blender geometry draped on the terrain (thin emissive strips, one instanced segment set), so they keep perspective. **Expendable** |
| **Signals** | `signal.*` (ADR-1602), the grotto's `music.py` (nine drivers, stem first, mix fallback), the onset register (ADR-1604), CC 9/10 palette | reuse the drivers and re-target them (Phase 7) |
| **Post** | bloom, tonemap chroma retention, grade, vignette, film grain (`post/look`), halation (`lens`) | reuse; Phase 6 |
| **Measurement** | the honest profiler (ADR-1600), the live baseline (ADR-1608), sound to light (ADR-1605) | reuse |

**What is missing:**
1. A **planar reflection** of the sky in the water. Not needed if the stylised reflection works (see Water).
2. A **baked aurora light probe**. Approximated instead by the aurora's ground term plus a fill bound to the aurora
   roles.

Neither is a prerequisite.

## 3. Research and decisions (per spec section 3)

| Feature | Options weighed | Decision | Expected cost [est] |
|---|---|---|---|
| **Crystal transmission** | true refraction (ADR-1603) on every crystal; a view-dependent shader only; refraction on the 3-4 largest | **Refraction on the largest formations only.** The rest use the shader approximation: an internal gradient bound to the aurora roles, plus Fresnel and glitter | 1 ms per 2-3 refractive nodes (grotto: 3 nodes = 3.0 ms) |
| **Fractures and speckles** | modelled cracks; textures; procedural masks | **procedural**: thin `voronoiEdge` lines in object space inside the faces (dark cracks plus a few bright ones), and a thresholded high-frequency noise for speckles. No textures (the program has no texture inputs) | about 6-8 ops |
| **Glowing edges** | geometry outlines; `curvature`/`convexity` inputs (screen-space derivative edges); Fresnel | **`convexity`** for the facet-boundary lines (a pixel or two wide at any distance, so it never thickens with distance), plus Fresnel for the silhouette | about 3 ops |
| **Aurora** | a new skydome shader; the existing ADR-230 shells | **existing**: 2-3 shells in a green-teal aurora, and a second aurora for the pink and violet upper layer. Curtain height and radius are set so the curtains rise behind the mountains | about 1-3 ms (shells are per sky pixel) |
| **Aurora lighting the valley** | many lights; a probe; the ground term plus a bound fill | **ground term + one bound directional fill + the crystal and water programs reading the aurora roles** | about 0 |
| **Long-exposure water** | temporal accumulation (ghosting, cost); SSR only (cannot see the aurora); a stylised single-frame shader | **single-frame:** the cinematic water's `streak` (view-stretched rays), rough reflection, `tintPatches` coloured by the aurora roles, `undulation` slowed. Stable by construction | about 1-2.5 ms |
| **Circuits** | a terrain material layer (bleeds across slopes); screen-space (detached); draped geometry | **draped geometry**, instanced short segments with a baked path index, so a pulse can travel them | under 0.5 ms |
| **Grain and halation** | none / bloom / grain | Phase 6, only after the image works | under 0.5 ms |

**Rejected:**
- **Temporal accumulation for the water:** it ghosts when colours change, against spec 7.4.
- **Physically based transmission on every crystal:** its cost is not justified at a fixed distance.
- **Clouds and waterfalls:** excluded by the spec.
- **A new renderer path:** nothing here needs one.

## 4. Proposed composition (to be proved in Phase 2)

**Camera:**
- about 30 m up and 25 m behind the river's near bend;
- pitched down about 12°;
- fov 52° vertical (no fisheye);
- static.

| Element | Description |
|---|---|
| **River** | enters wide at the bottom centre-left, swings right, then left, and narrows to the vanishing point at about 45% of the frame's height. Rough length 600 m |
| **Valley walls** | Poly Haven cliff formations on both sides, mossy, converging. The far mountains are layered low-poly ranges, three planes deep |
| **L1** | the hero left cluster: 6 shards, 25-40 m tall, leaning out over the river, near the camera, occupying about a third of the frame's height |
| **R1** | a wide right pair: 2 large prisms and 2 smaller, tilted, mid-near |
| **L2** | a left-bank spire pair at the first bend |
| **R2** | a tall right-bank prism at the second bend |
| **C1, C2** | two mid-distance groups of 2-3 tips near the river |
| **D** | 3-4 small tips at the horizon (depth cues) |
| **Total** | about 22 tips in 7 formations |
| **Aurora** | the upper 40-45% of the frame; curtains rising behind the far mountains and arching overhead |
| **Circuits** | the near banks, densest lower right |

## 5. Rendering architecture

1. **Static camera:** the camera state is a fixed `camera` block, with no route and no timeline.
2. **Terrain:**
   - one terrain node: valley features, the river (water), and a moss and rock program;
   - cliff and moss-rock meshes from Poly Haven, adapted in Blender (decimated for the fixed view);
   - far ranges as one low-poly mesh.
3. **Crystals:**
   - Blender `shard` builder: the gem builder's facets, chips and sub-crystals, with tabular shards;
   - one mesh per formation;
   - two programs: `aurora_hero` (refraction, fractures, speckles, internal nebula gradient, edges) and
     `aurora_xtal` (the same, without refraction);
   - colours from the aurora roles.
4. **Sky:** the procedural gradient, the stars effect, and one or two aurora effects bound to the roles.
5. **Water:** cinematic, with stylised long-exposure streaks.
6. **Circuits:** draped instanced segments (Phase 6, expendable).
7. **Palette:** the looks are aurora palettes: green-teal, magenta-pink, violet, cyan. Each sets the aurora roles and
   everything that reads them.

## 6. Initial budget (30 fps live; reported against 8.33 ms as well)

The grotto measured about 29 ms at Ultra, 23 ms of it a pixel-bound scene pass. This scene must be leaner: a static
view, few hero objects and no haze.

| Item | Budget (ms) |
|---|---|
| Terrain, rock, vegetation (opaque) | 5 |
| Crystals incl. refraction | 5 |
| Aurora and sky | 3 |
| Water | 2.5 |
| Post | 1.5 |
| Shadows and AO | 1.5 |
| **Total** | **about 18.5** (55% of 33.3 ms) |

**Notes on the budget:**
- **8.33 ms:** reachable only at lower rungs. Reported as measured.
- **Haze:** none. The aurora's horizon glow and the sky gradient give depth instead.

## 7. Performance risks to test early (Phase 2)

1. Aurora shells per pixel over 45% of the frame at 1080p.
2. The cinematic water's SSR over a long river.
3. Terrain and cliff meshes at the fixed view: their triangle counts and LOD.
4. Refraction cost when crystals cover a large share of the frame.

## 8. Asset inventory (first pass; the manifest grows with the work)

| Asset | Source | Licence | Use |
|---|---|---|---|
| `rock_moss_set_01/02` | Poly Haven, `assets/terrain/rocks` | CC0 | bank boulders, crystal bases |
| `coastal_cliff_02/04`, `mountainside`, `rock_face_01`, `namaqualand_cliff_02` | Poly Haven, `assets/terrain/formations` | CC0 | valley walls |
| Crystal shards | generated (Blender script, this branch) | — | heroes |
| Far ranges | generated (Blender script) | — | mountains |
| Circuits | generated | — | lines |

**Not fetched:** nothing yet. Any new asset will be recorded here with its licence first.
