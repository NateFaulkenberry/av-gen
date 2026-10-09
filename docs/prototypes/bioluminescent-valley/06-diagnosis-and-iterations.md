# 06: Visual diagnosis and iteration log (spec 05, §1 and §2)

The baseline is `artpass/baseline/` (a copy of `artpass/final/`, c727a200), with its settings and performance in `baseline/README.md`.
Measurements come from `palette.py` in the scratchpad (matched regions per shot, palette strips, hue histograms of the glowing pixels).
Its output is `palette-*.png` and `palette.md` in each iteration folder.

**The Creative Critic** (`critic submit --mode deep --images ... --intent`) ran on the baseline as job_1a11f49a16f5a4245.
- It reported 0 issues and scored every dimension 1.00.
- On stills with no scene, its rules measure only luma, contrast, clip and saturation (riverbank 0.101 / 0.113 / 0.001 / 0.76).
- No learned model is installed, and it has no reference-comparison mode.
- So it is a regression guard for clipping and exposure here, not a judge of likeness. Likeness is judged by eye at thumbnail and full size, together with the palette regions.

## Diagnosis of the baseline (sbs-all.png, c727a200)

### Riverbank

**Preserve:**
- the composition: a bank of life on the left, the river running into depth, a fall at the back;
- the veilcap hero on the bank;
- reflections that now read as soft dashes.

| # | Difference from the target | Category |
|---|---|---|
| 1 | The water is a flat navy-violet sheet with white specks. The reference water is near-black, carrying long saturated cyan, blue and violet reflections of the bank and a violet glow pool. | material response (water), lighting |
| 2 | The bank is a thin strip of uniform small glints. The reference bank is a tall, dense wall of emerald and cyan life (big fan organisms, stalks) with dark mossy rock between. | distribution, silhouette |
| 3 | The falls are a pink-white block, and the back is a dark mountain void. The reference fall is a misty, cool-lit ribbon in a deep, blue-lit forest. | atmosphere, material, composition |

**Best experiment:** darken and raise the water's specular (less sky fill, no specks) so that the mirrored bank carries the colour.

**Measure:** the water region's luma and saturation against the reference (ref ~0.10 / 0.75), and the hue histogram of the water region.

### Vista (the valley)

**Preserve:**
- the camera, the moon placement, the framing tree on the left;
- the river winding into depth;
- the floor glow reading as life on the valley floor.

| # | Difference from the target | Category |
|---|---|---|
| 1 | The sky is a flat violet/pink band, and the mountains are uniform small triangles. The reference has a saturated royal-blue sky with clouds, rose only at the horizon, and tall, sheer, moonlit karst spires with lit faces. | lighting, silhouette, atmosphere |
| 2 | The floor glow is evenly spread cyan patches over a dark floor. The reference is a luminous valley core of blue, cyan and gold that brightens into the distance, with misty layering. | distribution, atmosphere |
| 3 | The mid-ground lacks scale cues: no layered forest silhouettes or waterfalls on the cliffs. | composition, geometry |

**Best experiment:** a royal-blue sky with a narrow rose horizon, plus a stronger blue moon fill on the rock faces.

**Measure:** the sky luma/saturation (ref 0.197 / 0.72) and the mountain luma (ref 0.234).

### Macro

**Preserve:**
- the library organisms (veilcap, filigree, chalice) have real structure: lacy caps and veined membranes;
- the foreground carpet of dots.

| # | Difference from the target | Category |
|---|---|---|
| 1 | Navy empty sky and a white shape behind (the macrotest's backdrop). The reference is fully enclosed: dark trunks and vines, glowing life to every edge. | composition |
| 2 | The organisms read as pale grey-white and opaque. The reference is saturated cyan/green translucent caps on emerald stems, with blue bead clusters and violet lattice. | material response |
| 3 | The ground is grey-black lumps with uniform dots. The reference is moss and lichen texture, dense chartreuse/green and blue beads, with lacy fans out of focus at the edges. | material, distribution, post (DOF) |

**Best experiment:** checkpoint C, the production macro shot in the valley's own macro camera with the library: an enclosed background from the valley itself, with colourway tints.

**Measure:** the hue histogram (the reference's green-cyan-blue spread) and the near-black share of the top/background region.

### Meadow

**Preserve:**
- the horizon line;
- karst silhouettes against the sky;
- the moon;
- a luminous carpet band across the middle.

| # | Difference from the target | Category |
|---|---|---|
| 1 | Big dark foreground rocks and a bare, grey, flat foreground take the lower half. The reference foreground is dense carpet and stalk mushrooms with a small, dark, glinting stream. | composition, distribution |
| 2 | The carpet is a single cyan-white band. The reference is varied: yellow-green, gold, cyan and blue glints, stalks and mushrooms at mixed scale. | distribution, material |
| 3 | The horizon and sky are flat navy. The reference has a warm rose/orange sunset glow behind the mountains and a cloudy violet sky. | lighting, atmosphere |

**Best experiment:** a multi-hue carpet that extends into the foreground, with the foreground rocks smaller and sunk.

**Measure:** the carpet and foreground regions' luma and hue histogram; the horizon region's mean colour (ref [r>b warm]).

## Iteration log

Each entry gives the problem, its cause, the one change group, the render folder, the result, and the decision.
Regions are luma / saturation, ours against the reference.

1. **Colour round 1** (13d6e5a4, renders `c1`..`c5`).
   - **Problem:** the palette was violet-black, with no emerald, gold or electric blue.
   - **Cause:** a violet sky and fog, dim moonlight, near-black rock, and one cyan colourway for the whole library.
   - **Change:** a royal-blue sky and fog; stronger blue moonlight plus fill; blue-violet rock; regional colourways (emission tints on one mesh); blue-cyan-gold floor glow.
   - **Result:**
     - meadow carpet hues spread from chartreuse to violet (it had been a single cyan peak);
     - vista mountains 0.102 → 0.154 (ref 0.234).
   - **Decision:** KEEP.
2. **Vista sky saturation** (`c6`).
   - **Problem:** the sky read lavender (sRGB [80,70,122], sat 0.45; ref [34,49,110], 0.72).
   - **Cause:** the violet glow lobe was wide (0.4) and the moon halo and cloud rims were red-heavy.
   - **Change:** narrow the lobe to 0.22, a bluer violet, a tighter rose band, a bluer halo.
   - **Result:** sky 0.299/0.45 → 0.248/0.56, green now level with red.
   - **Decision:** KEEP.
3. **Raised rose band** (`c7`).
   - **Problem:** the meadow horizon was not warm (ref [85,69,108]).
   - **Change:** band centre 0.06 → 0.09 rad, width 0.07, warmer clouds.
   - **Result:**
     - the vista sky went pink again (0.352/0.37);
     - the meadow horizon only gained 0.143 → 0.178, because our hills cover that region.
   - **Decision:** REVERT the band. The cause in the meadow is the far-field silhouette, not the sky (a form fix).
4. **Moonlit rock** (`c7`, `c8`).
   - **Change:** rock albedo [0.07,0.085,0.17] → [0.12,0.15,0.33].
   - **Result:** vista mountains 0.154 → 0.172 and readable, with lit faces.
   - **Decision:** KEEP.
5. **Riverbank under the canopy** (`c9`, `c10`).
   - **Problem:** after round 1 the whole riverbank went royal blue (sky/canopy 0.150; ref 0.068, 60% near-black), and the water was a flat blue sheet.
   - **Cause:** global moon, fill and sky light, plus the water's sky reflection at 0.5.
   - **Change:** a per-shot light scale (`LIT`, riverbank 0.35); water reflection 0.22.
   - **Result:**
     - sky/canopy 0.087;
     - water 0.111 → 0.070, near-black 1% → 30%;
     - the mirrored bank now carries the water's colour.
   - **Decision:** KEEP.
6. **Riverbank falls and snow-flecks** (`c11`, `t1`, `t2`).
   - **Problem:** a lavender-white block at the foot of the fall, and white specks over the water.
   - **Cause:** confirmed by removing the node (t1): the block is the spray particles (160/s, 3-11 m, additive at 2.2). The specks are the water's sparkle.
   - **Change:** a wide, faint violet-blue veil (50/s, 5-18 m, 0.18); sparkle 0.
   - **Result:** a soft violet glow at the foot, like the reference's lit fall, and clean dark water.
   - **Decision:** KEEP.
7. **The vista's near floor fading to the far core** (`c13`, `c14`).
   - **Change:** layer nearFade 320 m, then 1300 m, on the glade, understory, crown, ledge and tips layers, plus gain ×1.5; the glades shift bluer.
   - **Result:** no visible change. The near patches are the glade bulbs' own lit meshes plus the glow-map mist, not the emitter layers.
   - **Decision:** REVERT the fade; KEEP the bluer glade colours (glade 1 [0.25,0.65,1.0], glade 3 [0.4,0.45,1.0]).
8. **Dark canopy below the lookout** (`c15`, `c16`).
   - **Change:** 160 canopygrove crowns at 90-520 m below the lookout, darkened.
   - **Result:** foreground 0.185 → 0.145 (ref 0.097), but from 300 m above they read as boulder heaps, not tree crowns.
   - **Decision:** REVERT. The reference's foreground is seen at canopy height; this camera is not.
9. **Asset checkpoint B merged** (1fb67bc6, merge 6c8abef1).
   - The revised default GLBs changed the scenes.
   - A fresh baseline with the new assets and no new change of mine is `b2/`; later entries compare against `b2`.
10. **Karst towers, not cones** (`c17` against `b2`).
    - **Problem:** the vista's mountains read as uniform triangles.
    - **Cause:** the summit profile was the linear cone `(1-d)^ex`.
    - **Change:** a domed crown over near-vertical walls, `(1-d^pw)^(0.35 ex)`, with pw 2.6-4 for peaks, 3-5 for spires and 1.4-2 for ranges.
    - **Result:** sheer towers with lit faces, like panel 2. Mountain luma is 0.172 → 0.162, because the walls now face away from the moon more often.
    - **Decision:** KEEP.
11. **Checkpoint C: the macro production shot** (`m1`..`m7`, review `artpass/C-macro/`).
    - **Problem:** the valley's own macro camera showed a tilted slope with sparse native fungi and orbs, an empty navy sky, and nothing of the library.
    - **Change:**
      - The camera moves to a flat stretch of the near bank, found by search (ground within 2 mm of a gentle rise over 4 m), 22 cm up, looking upstream.
      - `library.macro()` places the library at LOD 0 in the camera's frame. It uses the artist's colourway GLBs, weighted to the measured palette (mostly blue and cyan):
        - an emerald-cyan veilcap hero upper left;
        - the reliquary behind;
        - blue lacefans at the left edge;
        - blue bead colonies in the bottom-left corner and emerald ones on a glowmound bottom right;
        - a glowmat carpet;
        - bulbstalks, filigree, bellspires, chalices and lanternberries in the mid-ground;
        - a silhouette canopy 12-30 m back that closes the top of frame.
      - The riverbank's library continues beyond 9 m.
      - The native macro set is removed within 8 m; the native moss stays as the ground.
      - DOF focus 1.3 m, range 0.45; exposure -0.6; saturation 1.25.
    - **Result** (luma/sat, ours against the reference):
      - top 0.090/0.82 vs 0.120/0.72;
      - hero 0.296/0.77 vs 0.207/0.75;
      - middle 0.224/0.78 vs 0.196/0.71;
      - foreground 0.233/0.76 vs 0.178/0.80.
      - The hue histogram now spreads emerald → cyan → blue like the reference's.
    - **Open:**
      - the mid-ground is busier and brighter than the reference;
      - the ground between the organisms is bare dark lumps, where the reference has a mossy carpet;
      - a pale blue haze of far lacefans and filigree sits behind the reliquary;
      - there is little violet or gold.
    - **Decision:** KEEP.
