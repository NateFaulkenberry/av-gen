# Bioluminescent Valley: reference analysis, systems and reuse plan

Spec: `00-spec.md` (verbatim). Reference: `~/Desktop/av-gen-review/45-bioluminescent-valley/00-reference.png` (not
committed: 2.7 MB). Branch `proto/bio-valley`, worktree `../av-gen-biovalley`. ADR range **1280-1299**
(1240-1259 is astral-forge-2, 1260-1279 astral-furnace, 1202 rift-jungle). Track: Trench.

## 1. The reference, panel by panel (spec §2)

The reference is a 2 x 2 collage (1536 x 1024; each panel 766 x 510, 3:2) of one world. The four panels are four
**shot types** the camera visits over the track. Numbers below were measured from the image (sRGB 0-255; "glowing" =
sRGB luma > 0.35 and saturation > 0.35; "near-black" = luma < 0.06).

| | 1 RIVERBANK | 2 VISTA | 3 MACRO | 4 MEADOW |
|---|---|---|---|---|
| Mean sRGB luma | 0.133 | 0.182 | 0.155 | 0.162 |
| Near-black share | 33% | 20% | 22% | 24% |
| Glowing share | 6.0% | 9.2% | 8.3% | 6.3% |
| Brightest 1% at | 0.65 | 0.63 | 0.68 | 0.66 |
| Ambient (median of mid-darks) | (12, 26, 43) | (18, 31, 65) | (10, 28, 36) | (14, 28, 42) |
| Glow hues (share by 30°) | 120-240°, peak 180-210 (cyan) | 210-240° dominates (65%: blue haze and river), 180-210 16% | 90-210°, peak 180-210 | 60-210°, broad (yellow-green to cyan), a little violet |

**What the numbers say.** The reference is *not* a black image with a few lights. Its median pixel is a saturated
dark blue (luma ~0.10-0.15), a fifth to a third of it is near-black, and 6-9% of it is glowing. The darkness is in
the foreground and the undersides; the middle distance is lit haze. That is the calibration target, and the reason the
Rootworks' "too dark" note matters: the darks must be *blue-dark*, not black, except in the foreground.

### Panel 1, RIVERBANK
- **Camera:** about 0.5 m above the water, pitch about -3°. The horizon (where the stream meets the falls) is at
  about 45% from the top.
- **Layers:**
  - FG: the left bank, with moss cushions, dark rocks, glowing stalks and a translucent hero "tree-fungus" top-left.
    The water fills the lower right half.
  - MG: a rock shelf, small cyan fungi at centre, a green stalk tuft on the far right bank.
  - BG: a waterfall in mist (x 78-86%, y 18-40%) and a dark canopy.
- **Brightest:** the stalk tips and hero fungus (cyan-green); the violet bokeh column on the water, lower right.
- **Darkest:** the canopy top-centre, rock undersides, the water between reflections.
- **Focal path:** the glowing bank, along the stream to the falls.
- **Quiet:** the top band, the dark woods right-centre.
- **Key look:** reflections of emitters on black water, broken into vertical streaks and bokeh. The water carries
  the light; it is not lit itself.

### Panel 2, VISTA
- **Camera:** high above the valley floor, about 150-250 m; pitch about -6 to -8°. The far valley's vanishing area
  is at about 42% from the top, slightly right of centre.
- **Layers:**
  - FG (bottom 20% and left 25%): dark silhouetted trees and bushes, a few glowing plants.
  - MG: the valley floor (45-85% height), dense forest canopy dotted with glow (yellow-green, cyan), a winding
    river catching blue light, mist banks, waterfalls off the cliffs.
  - BG: a left group of massive jagged peaks reaching the top 8%; a cliff-massif on the right with a falls; spires
    receding into violet haze; a pink-violet horizon glow under clouds.
- **Sky:** a large crescent moon top-right, about 12% of the frame wide, its dark side faintly visible. Stars.
- **Brightest:** the moon, the valley floor's glow clusters, the river, the horizon glow.
- **Darkest:** FG trees, the bottom edge.
- **Palette:**
  - sky (27, 40, 94);
  - spire faces (14, 27, 67): only slightly darker than the sky, so aerial perspective carries them;
  - far valley haze (106, 88, 154);
  - horizon glow (72, 68, 138), its p95 (137, 115, 187);
  - valley floor glow (54, 83, 150).
- **Focal path:** the moon → the left peaks → down the falls → the glowing floor → the river toward the lower right.
- **Quiet:** the sky's upper left; the dark FG mass.

### Panel 3, MACRO
- **Camera:** about 20-30 cm above moss, level or slightly up. No horizon.
- **Layers:**
  - FG: out-of-focus glowing bulbs on dark mounds (big blue and green bokeh, bottom).
  - MG: a hero translucent cap pair (emerald-cyan, top-left of centre); lacy violet-blue lattice fungi; thin stalks
    with glowing tips.
  - BG: dark trunks with blue rim light; floating motes.
- **Brightest:** the hero caps; the tips.
- **Darkest:** the top right; the trunk interiors.
- **Palette:** the hero cap at median (21, 102, 93), p95 (89, 196, 192).

### Panel 4, MEADOW
- **Camera:** about 1 m; pitch about -4°. The horizon is at about 36% from the top.
- **Layers:**
  - FG (bottom 25%): dark rocks; a small stream bottom-centre-right reflecting the tips.
  - MG (38-70%): a carpet of glowing tip-stalks, densest in drifts, with gaps of dark moss.
  - BG: arched trees and mushroom-trees on the left; spires at the horizon; a violet-to-rose horizon glow right of
    centre (110, 74, 109), p95 (224, 156, 133), under dramatic clouds.
- **Brightest:** the stalk tips (yellow-green, cyan); the horizon glow.
- **Darkest:** the FG rocks; the upper-left sky.

### Across the panels
- **Glow is in clusters, with dark gaps** (drifts in the meadow, banks by the water, colonies in the macro). It is
  never a uniform field.
- **A three-tier brightness ladder is visible in every panel:**
  - a subtle carpet of tiny dots;
  - active tips and fungi;
  - a few hero organisms (the tree-fungus in 1, the caps in 3, the floor's brightest clusters in 2).
- **Colour:**
  - the environment is blue (blue channel 2-3 times the red);
  - the glow is cyan → emerald → yellow-green, with violet and magenta only as accents (the bokeh, the lattice
    fungi);
  - warm appears only at the horizon.
- **Atmosphere does the depth.** Haze sits in the valley; peaks rise clear of it; distant forms are light, low-contrast
  blue-violet.

## 2. Research notes (short)

- **Karst towers.** Zhangjiajie's quartz-sandstone pillars:
  - over 3,000 of them, up to ~350 m of sheer face (some higher);
  - cut by vertical joint sets, stepped by bedding planes;
  - shaped by physical weathering;
  - plants grow on every ledge and crown.

  Guilin's limestone towers are broader cones. The reference mixes both: jagged pointed masses (left) and sheer cliff
  walls (right). ([revigorate](https://www.revigorate.com/zhangjiajies-sandstone-pillars.html),
  [ANSTO/Melbourne](https://apo.ansto.gov.au/dspace/handle/10238/12499))
- **Fungal light.** Mycena and Panellus emit at 520-530 nm (green), in both mycelium and fruiting body
  ([PMC4629642](https://pmc.ncbi.nlm.nih.gov/articles/PMC4629642)). Glowworms are ~488 nm (the Rootworks' research).
  This is why the reference's fungi are emerald-cyan, and why yellow-green belongs to stalk tips, not caps.
- **Light on rippled water.** Light on rippled water becomes a vertical pillar: each facet reflects locally, and the
  pillar's length grows with the wave amplitude
  ([CU physics demo](https://physicslabs.colorado.edu/demos/optics/geometrical-optics/reflection-from-flat-surfaces/rippled-reflection/)).
  The reference's violet and cyan bokeh columns are exactly this. In AV Gen the water reflects only the environment
  map today (ADR-099), not local emitters. The riverbank shot needs emitter reflections; that is the likely engine
  addition at CP3 (§4).
- **VFX practice.** Notch, Houdini and Niagara converge on the same habits:
  - layered emitters, each population with its own scale, lifetime and motion;
  - emission separated from albedo;
  - glow as core + halo, with bloom only for the core;
  - field-driven motion (curl noise plus attractors).

  The engine already has these as separate systems (§3). The work is art direction, not architecture.
- The Rootworks record (`proto/rift-jungle`, `11-jungle-art-direction.md` §1, §9) holds the deeper research on
  Avatar's Pandora and Weta, translucency, Poisson + warp scatter and audio mapping. It is reused, not repeated.

## 3. What AV Gen already has, and what each visual system uses

| Spec system | AV Gen system | Status |
|---|---|---|
| 1 Sky | Generated HDR environment map (`sky.py`: gradient, horizon glow, clouds, halo; no reference pixels) + the `stars` world effect | **CP1** |
| 2 Distant mountains | Generated karst-tower meshes (`towers.py`), instanced `points`; a generated far-range band | **CP1** |
| 3 Atmospheric depth | The volumetric medium's surface fog with `fogSky` aerial perspective and a height layer (ADR-705/918/058) | CP1 basic; CP5 full |
| 4 Terrain | `world::WorldMap` (noise layers + authored features: valley, flat floor, river, ridge promontory, knolls, a ravine) | **CP1** |
| 5 Water | Terrain water (ADR-099), its rivers carved by `_carve` from the engine's own heights; `cascade` makes the falls chute | CP1; emitter reflections at CP3 |
| 6-8 Vegetation | Generated GLB species (the Rift's `organisms.py` primitives; Rootworks' leaf programs and fruit spheres), CPU `points` placement on engine heights, Poisson + warp + ecological masks | CP2-3 |
| 9 Bioluminescent organisms | ADR-1200 ecosystem emitter layers (millions of points, ~1 ms), three tiers by `intensity` / `sparsity` / `pulseRate` | CP3 |
| 10 Particles | Particle node pools (spores, motes), one population per role | CP5-6 |
| 11 Ground mist | Fog bank height layer, terrain-following (ADR-715) | CP5 |
| 12 Water reflections | Env reflection exists; local emitters do not reflect: an engine item | CP3 |
| 13 Local emissive light | The Rift's lesson: not point lights (~22 ms for 140). A glow field the surfaces and fog read (ecology glow pools ADR-945, or a field) | CP3 |
| 14 Moon | A crescent mesh 20 km out (unlit, emissive), a directional key from the same direction, the sky's halo | **CP1** |
| 15 Camera | Free camera per shot; later scene states and camera shots over Trench | CP1 stills; CP6 sequence |
| 16 Post | Bloom with emission weight and chroma retention (Rootworks' tuning), DOF, tonemap | CP4 |
| 17 Audio | Signal bus, analysis bands, modulators, routes; ADR-1201 excitable grid for travelling, spatial events | CP7 |

**Engine changes so far:** ADR-1280 (`camera/far`, a shot sets its far plane). A world with a moon and ranges
kilometres out had an automatic far plane of 1,400 km, and with a macro shot's 5 cm near plane the sky drew black.

## 4. Geography: one world, four shots

`examples/biovalley/land.py`:
- A valley about 1.1 km wide runs north (+z), its floor ~4 m above datum, its walls rising to a 150-250 m plateau.
- A main river meanders inside the floor, with a back channel and a far lake.
- **VISTA:** a rock promontory standing out of the east side of the floor, 300 m tall, near the valley's south
  end. The camera is 80 m above it and looks north-north-west up the valley. The far valley is at centre, the moon
  right.
- **RIVERBANK:** a tributary cut into the foot of the west wall. Its calm reach is level (capped), and its head
  drops down a chute (the falls, whitewater by the water's `cascade`).
- **MEADOW:** a hummocky terrace on the west side of the floor, with a small stream carved from the engine's heights
  toward the river. It looks north at the far towers.
- **MACRO:** the tributary's mossy bank.

Towers (`valley.py`):
- They are placed as **clusters** (a dominant tower plus satellites, bare wall between clusters), with
  heavy-tailed heights so the skyline has giants.
- A far-end forest of towers recedes into haze.
- Authored hero masses frame the vista: a peak group left of centre and a cliff-massif right.
- A falls tower stands above the riverbank.

Everything generated is gitignored; `python3 examples/biovalley/valley.py` rebuilds it in about 20 s once the
engine is built (heights are probed from `avgen_world_preview` and cached in `build/biovalley/`).

## 5. Build order and checkpoints (spec §35-36)

| CP | Content | Stop? |
|---|---|---|
| 1 | Terrain, river, towers, ranges, sky, moon, four cameras | **STOP for the owner** |
| 2 | Hero vegetation: FG framing trees, the hero fungi, crown and ledge vegetation on the towers, falls meshes | report |
| 3 | Density + bioluminescence: ecological masks, the plant vocabulary, the ecosystem emitters' three tiers, local light (glow field), water reflection of emitters | **STOP for the owner** |
| 4 | Full environment without audio: mist layers, particles, DOF, bloom | report |
| 5 | Audio-reactive frame (signal bus, timescales, spatial regions) | report |
| 6 | 20-30 s Trench clip at 1080p with audio + per-second sheet | **STOP for the owner** |

## 6. CP1: state and verdict

Stills, side-by-sides and stats: `~/Desktop/av-gen-review/45-bioluminescent-valley/cp1/`.

| shot | mean luma ref / ours | near-black ref / ours |
|---|---|---|
| riverbank | 0.133 / 0.085 | 33% / 41% |
| vista | 0.182 / 0.138 | 20% / 12% |
| macro | 0.155 / 0.050 | 22% / 73% |
| meadow | 0.162 / 0.113 | 24% / 24% |

Glow share is ~0% everywhere: there is no bioluminescence yet, by design.

**Honest verdict (the §36 questions, composition only):**
- **VISTA has the reference's large structure:**
  - spire masses framing left and right;
  - an opening to a violet, hazy far valley at centre;
  - a crescent moon upper right;
  - the river winding toward the lower right;
  - haze that sits in the valley while peaks rise clear.

  What is missing is what CP2-3 add: the dark FG trees, and the forest canopy that turns the bare rolling floor into
  the reference's glowing mosaic. The floor is currently smooth bare hills, which read as "procedural landscape".
  The towers are good at distance and plain up close: they have no vegetation on their ledges and crowns, and they
  are uniformly dark. The left hero group is heavy.
- **MEADOW:** the horizon spire line and the clouds work. The near ground is an empty dark plain, and the horizon
  lacks the reference's rose glow in that direction.
- **RIVERBANK:** the stream recedes to a cliff with a falls chute (the right structure). The cliff is too blunt and
  the falls too faint. The banks need rocks and plants, which are CP2/3 subjects.
- **MACRO:** only a location. This shot is all organisms; it cannot be judged before CP2-3.
- **Darkness:** the reference is brighter in the mid-tones (aerial haze, organism light). We match its near-black
  share in the meadow and riverbank. The rest comes with the glow and atmosphere passes, not with exposure.

Known engine-side notes from CP1:
- With the fog off, distant terrain and range tips alias into single dark pixels (no TAA in the offline still).
  Forest cover and fog hide most of it; supersampled offline renders would remove it.
- Terrain chunks: 200 m, view distance 4.5 km. Not yet profiled.
