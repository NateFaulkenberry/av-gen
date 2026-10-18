# Targeted Art Pass II: findings, debate and plan

- **Brief:** [07-brief-art-pass-2.md](07-brief-art-pass-2.md) (verbatim; cut at 50,000 characters in §11).
- **Branch:** `art/resonator-pass-2`. The art agent owns §§2-6 and 8-9 and the scenes' own cost; the performance
  agent owns §7 (the live path) on `perf/resonator-live`.
- **Media:** `~/Desktop/av-gen-review/69-resonator-art-pass-2/` (`baseline/`, then one folder per stage).
- **ADRs:** 1480-1489.

## 0. Baseline

Captured at head `0bfac1d9` (the brief's commit on top of `41eaca7f`), headless 960×540 through the GPU lock, each
scene's live project with the test track fed on the command line (ADR-1408), palettes on their own journey unless
stated. Feline Footwear at 8, 60, 95, 130 and 175 s; Trench at 60 and 88 s (88 s is Trench's breakdown, the quietest
passage of the three tracks); Hyperborea at 10, 48, 116 and 134 s for Cymatic World with Abyssal locked (the seconds
of the gradient reference).

| File | What it shows |
|---|---|
| `baseline/cw-sheet.png` | Cymatic World over Feline and Trench |
| `baseline/cw-reference-vs-current-hyperborea-10-48-116-134s.png` | the reference's BEFORE column beside the current scene, same seconds |
| `baseline/silk-sheet.png` | Resonant Silk |
| `baseline/wd-sheet.png` | Waveform Dimension |
| `baseline/<scene>/*.png` | the single frames |

Live profiles (1080p, 30 fps target, Auto, Feline from 95 s, 20 s measured, through the GPU lock while the
performance agent was also profiling; `baseline/base-*.json`):

| Scene | Auto level (render scale) | Frame p50 / p95 | GPU p50 / p95 | CPU p50 | Misses |
|---|---|---|---|---|---|
| Cymatic World | Low (0.50) | 33.3 / 37.1 ms | 26.8 / 27.8 ms | 5.7 ms | 0 / 600 |
| Resonant Silk | Low (0.50) | 33.3 / 37.0 ms | 30.3 / 33.6 ms | 4.8 ms | 3 / 601 (0.5%) |
| Waveform Dimension | Low (0.50) | 33.3 / 37.4 ms | 29.4 / 30.2 ms | 5.7 ms | 0 / 601 |

**What the baseline shows** (my reading of the frames, against the brief):

- **Cymatic World.**
  - The basin's back wall and terraces fill the top fifth of every frame: the brief's "back walls".
  - Each palette reads as two flat tones, one per sign of lobe (magenta and navy in Abyssal), over broad, shallow
    lobes. There are almost no highlights, and value contrast is low.
  - Motion between frames is the families morphing; nothing travels.
- **Resonant Silk.**
  - The cloth reads as glossy latex: milky white streaks from the key and the reflection strip over one hue family
    per palette.
  - The far drapes are flat-coloured ribbons with hard edges (lower right of every frame); the rest is a flat sky.
  - The quiet Trench frame and the loud Feline frames look alike.
- **Waveform Dimension.**
  - Strong, varied forms; the lighting is even, so a fold reads as bright as the face beside it, and the cut edges
    barely read. It needs exactly the brief's restrained pass.

## 1. The gradient reference (brief §4)

**The image.** `~/Desktop/av-gen-review/58-cymatic-world/shape-language/final/before-after-stills-hyperborea-10-48-116-134s.png`.
Its left column is labelled "BEFORE (colour pass, before any motion work)".

**The implementation that made it.** `docs/prototypes/cymatic-world/09-report-resonant-shape-language.md` names the
left column as "the colour pass (before any motion work)". The colour pass is
[04-report-color-lighting-pass.md](../cymatic-world/04-report-color-lighting-pass.md); its last commit is
**`1ba14dbe`** ("Cymatic World colour pass: no crystals, no walls"), and the next commit touching the scene,
`4961ad56`, is "motion, Pass 1". So the BEFORE frames are the scene at `1ba14dbe`, and its generator
(`git show 1ba14dbe:tools/cymatic_world/build.py`, `ground_program()` and `build_world()`) is the implementation.
It is available; nothing below is guessed from the filename.

**What made those gradients**, from that code and the frames:

1. **The hue came from the light, not the albedo.** The albedo was dark and low in chroma: navy stone `#081236` and
   teal grit `#0b4458`, chosen by a sand mask. The colour was painted by two coloured lights from opposite quarters:
   - a saturated cyan key (`#62ecff` × 2.3, plain N·L) from low on one side;
   - a fuchsia fill (`#c13bff` × 0.9) from the opposite quarter as a narrow grazing lobe, (N·L)⁴;
   - a weak hemisphere sky (× 0.07), so shadows stay near black;
   - a pink Fresnel rim and a cyan-white sheen, key lobe¹² on the stone only.

   Every ridge therefore has a cyan flank, a navy-to-black shadow flank and a fuchsia grazing edge, and the hue turns
   continuously around every ring of the radial figure. That is the "colour that varies across the pattern".
2. **A relief tall and round enough to show every orientation.** The relief reader was the sand ridge (not
   normalised, sharpness 0.9: wide, rounded ridges) plus a crest line and vibration, at 2.6 m, and the camera looked
   steeply down from close (30 m out, 32 m up, 30 mm). Each frame holds walls facing every direction, so the two
   lights have something to separate. Today's ridges are thin (sharpness 0.45) over shallow lobes (1.5 m, no basins),
   seen obliquely from 78 m: almost every normal points up, so the light is one value and the colour is the albedo's.
3. **A three-value structure.** Floors near black (dark albedo × a weak sky), the ridge network carrying the light,
   white sheen only on ridge tops. The negative space is what makes the colours read as saturated.
4. **Emission placed by the field.** A cyan-white nodal line (crest × excitation), and a hot magenta glow
   (`#ff3fd2`) only on the inner walls of the cells ringing hardest (excitation 0.8-2.4 × stone × a band of the sand
   field). These are the pink basins. Today the glow starts at 1.4 and saturates at 3.2, so it rarely shows.
5. **Tone mapping.** AgX chroma retention 0.9 (still set today).

**What the current scene has instead** (the 12-brief's palettes, the quality pass's OKLab field, the overhaul):
the albedo is an OKLab gradient between four palette stops, indexed by the large figure's signed F (one sign of lobe
to each end), a travelling band and a ridge shift; the key leans 45% to white and sits over a 0.6 ambient floor. That
system is what makes ten palettes and smooth palette blends possible, and it must stay (§4.1: "Do not sacrifice one
to achieve the other").

**The restoration** keeps the palette system and puts the colour pass's three mechanisms back inside it (§2.1).

## 2. Cymatic World

### 2.1 Gradient: approaches

| | Approach | For | Against | Verdict |
|---|---|---|---|---|
| G1 | Revert to the colour pass's program and three palettes | Exact | Forbidden (§4.1: no revert, no obsolete palette system); loses the ten palettes | Rejected |
| G2 | Push the albedo field harder (more chroma, more tint gain) | One-line change | The overhaul already did this (tint gain 1.6): it gives two flat tones. The reference's variation is orientation, which no albedo coordinate holds | Rejected |
| G3 | **Light-led colour within the palette system:** a palette-coloured key (the spectral role, far less white) and a counter light and hollow glow in the hue of the palette's gradient end (g3, at full chroma), so every palette carries a two-hue light pair from its own gradient; darker floors; wider, taller ridges and sunk basins so the lights find every orientation; the OKLab field kept as the broad regional layer under them | Each of the reference's mechanisms, expressed through palette roles that already blend in OKLab; broad (lobe) and fine (orientation) variation together | More relief means more overdraw (the depth prepass stays on) and steeper normals; the plate's crossing lines must survive wider ridges | **Chosen; proof P1** |

### 2.2 Back walls and dead space: approaches

The walls are visible because the overhaul's oblique camera (78 m out, 44 m up, 26 mm: about 30° down) sees past
the basin floor to its terraces, rim and the ranges, by design (the overhaul's "composition and depth").

| | Approach | For | Against | Verdict |
|---|---|---|---|---|
| D1 | Keep the oblique camera, flatten the terrain to a plain to the horizon | Depth, a horizon | A flat plane to a horizon is exactly the "flat plane as the world" and "visible edge" the owner rules out; the far figure is in cells too coarse to carry it | Rejected |
| D2 | Keep the oblique camera, a fog wall | Cheap | Fog over a back wall is "covering the problem" (§4.5) | Rejected |
| D3 | **Look into the ground (camera B of the correction round, which the owner approved: 36 m out, 42 m up, 28 mm, about 50° down), and carry the figure to the frame's edge** | The colour pass's composition (the reference's frames are all ground); the top edge meets the floor inside ~70-80 m all along the arc (ray-cast in the correction round), short of the terraces (≥ 70 m at the new basin scale) | Less sense of distance; the figure must reach the frame's top edge at every arc position (its fade, 55-95 m, and the bake extent) | **Chosen; proof P1** |

Check: ray-cast the frame's four corners along the arc and require every hit inside the figure's reach and the bake.

### 2.3 Motion layers (brief §2, §4.2-4.4)

The resonator, director, ripples and travelling light already exist. What is missing is propagation you can see,
secondary motion that rides the figure, and glimmers. Each layer below names its signal, its time constant and its
spatial footprint, and every layer is an existing engine mechanism:

| Layer | What you see | Mechanism | Signal and time constant |
|---|---|---|---|
| A primary | The families and the director (kept); a broad **bass wavefront**: a ring that lifts the figure as it travels out from the basin's heart | Spectrum field (bass, ADR-1116) laid out at ~20 m/s, smoothed, in the baked relief at ~0.5 m | The bass history itself; seconds of travel |
| B secondary | **Ripples from transients that ride the surface:** the onset ripple rows back in the geometry at low height | A ripples-only relief reader (ADR-1409) in the relief compound | Onsets; rings travel ~9 m/s, live 1.6 s |
| B fine | Fine vibration (kept), hats | The fine reader | Hats, 10/220 ms |
| C travelling light | **A scan:** a band of light crossing the basin on each kick, brightening the ridges (and the crest's glimmer) as it passes, then fading | An Onset field, planar, at ~30 m/s, read by the program, × the crest | Kick onsets; ~3 s crossing, decay ~0.6 s⁻¹ |
| C (kept) | Light running along the ridges from where a hit landed | The ripples' amplitude on the crest (Stage D of the light pass) | Onsets |
| D glimmers | **Glints fixed to the ridge network** where the scan and the ripples pass: the sheen on ridge tops, sharpened by hats, and the crest × scan product | Program terms on existing masks | Hats 0/180 ms; the scan |
| E environment | The far ground and the basin's air respond slowly: the haze colour and the band's drift (kept: the band integrates energy) | Routes | Sustain, 2.5/5 s; energy integral |
| F phrase | The family director (97-173 s LFOs, kept), the palette journey (kept), the build ringing more modes (kept); the balance of large and medium shifts with the slow energy | Routes | `sonic.energy.slow`, 4/8 s |

Coordinated event (§2.2): a kick launches the scan (light) and a ripple ring (geometry) from the same moment; the
bass of that kick becomes the travelling wavefront a beat later; the hollow glow flares where the cells already ring.

### 2.4 Budgets

- **Fields:** 15 of the GPU's 16 are in use. The scan (+1), the ripple relief (+1) and the bass wavefront (+1) are
  paid for by dropping the excitation reader and its grid (−2): the program takes the excitation from the colour
  field it already reads (|F|² of the large figure).
- **Program fields:** 4 a program: sand, colour field, travelling light, scan.
- **Ops:** 41 of 48; the light pair needs no new ops (it is palette data).
- **Grid table:** 3.3M of ~4M floats; dropping the excitation grid frees 0.26M.
- **GPU mode table** (48 rows for every resonator together): see §5; likely a defect today.

## 3. Resonant Silk

### 3.1 Environment: approaches

| | Approach | For | Against | Verdict |
|---|---|---|---|---|
| E1 | More or better far drapes | Cheap | The owner rejected ribbons (§5.1) | Rejected |
| E2 | An animated sky dome (painted bands) | Cheap, no parallax issue | A dome reads as a skybox; no folds, nothing for light to model | Rejected alone |
| E3 | **The Veil: one enveloping shell of the same cloth around the whole scene**, no edge in any frame; its folds travel in the silk's flow direction (a planar wave), the bass swell that runs out from the gather lifts it as it passes (the same field as the hero's), its colour is the hero's gradient roles along the same world band, dimmed, flowing at half the hero's rate; a glow in the shell behind the gather is the scene's light source and backlights the hero | One coherent spatial system: the foreground's flow, swell and colour logic at a lower density and brightness; animated; no ribbons, no edges; cheap (one 18k-vertex shell, unlit painted program, three deformers) | Might read as a backdrop if its folds are too faint or too regular; must stay darker than the hero | **Chosen; proof S1** |
| E4 | Volumetric light shafts or fog colour fields | Atmosphere | Washed the frame out in the overhaul; cost | Rejected |

Depth: hero wall (foreground), gather and sail (middle), the veil (background), with fog between them.

### 3.2 Material: approaches

| | Approach | For | Against | Verdict |
|---|---|---|---|---|
| M1 | Alpha blending | Real see-through | One self-overlapping 52k-vertex sheet: sorting artifacts, double the pixel cost, the form dissolves | Rejected (§5.3: prefer a stable approximation) |
| M2 | **Transmission as emission:** the glow behind the gather passing through the sheet, ∝ the viewer-facing normal toward the glow, weakened where the cloth turns (long path), in the cloth's own colour; less specular (no metal, rougher, softer bands) | Thin faces glow, folds stay deep, backlighting reveals structure; stable and cheap (+5 ops) | An approximation; must not wash out | **Chosen; proof S1** |

### 3.3 Musical vocabulary (brief §5.4-5.5)

| Layer | What you see | Mechanism | Signal |
|---|---|---|---|
| A primary | Modes, swell, kick fronts (kept); fold tension rises with the slow energy | Routes on the drift and mode gains | `sonic.energy.slow` |
| B ripples along the flow | Ripples running along the wall into the gather | A `sine` deformer along the wall's run, its amplitude the mids | Mids, 150/900 ms |
| B sub-waves | Fine crosswise waves | A `sine` deformer, 5 m, hats, small | Hats |
| B local | The snare's front (kept, now visible) | Onset field (existing) | Snares |
| C light | A kick's front as light (kept), the snare's front as a narrow highlight band | Onset fields in the program | Onsets |
| D glimmers | Glints at fixed places along the fold edges | The fold-edge mask × a uv lattice × the treble | Hats |
| E environment | The veil's swell (bass history), its folds' flow, its colour flow | §3.1 | Bass history, energy integral |
| F phrase | The palette journey (kept); the veil's glow with the slow energy | Routes | Slow energy |
| Glitch | Optional, last: a bounded slip band on rare large changes | Only if the rest lands within budget | — |

### 3.4 Budget

Silk is the scene that does not hold 30 fps today (vertex-bound at Emergency: 52k vertices × 8 deformers). The far
drapes go (−2k vertices, −2 draws), the veil comes in (18k vertices, three deformers, an unlit program), the new
ripples are `sine` deformers (no field reads), and the program pays for translucency by routing the light gains onto
the ramps (−4 ops). Fields: 14 + 2 (the veil's folds and swell) = 16.

## 4. Waveform Dimension (restrained)

Keep geometry, camera, routes and palettes. Three program terms, each small and tuned against the palettes:
1. **Backlight translucency:** the rim light (from behind the sculpture) passing through faces that turn to the eye,
   in the sheet's own colour, stronger on the inner sheets (light trapped in the nest).
2. **Edge illumination:** a Fresnel rim in the palette's rim role, only where the sheet faces the rim light, narrow.
3. **Fill in the darkest folds:** a small emission floor of the albedo so turned-away faces keep their colour.

If the scene starts to look different, back off. Budget: 31 ops, +5 at most.

## 5. Engine work (ADRs 1480-1489)

- **The GPU mode table overflows (verified: no, in the loud passages tested; see §6a).** The table holds 48 rows for all resonators together; a resonator
  that does not fit is skipped silently (`field_uniforms.cpp`). Cymatic World's large resonator has up to 24 modes
  plus 6 ripple rows, and its medium/fine resonator 22 rows, 7 of them silent (no source below u 0.3). In a loud
  passage with ripples the second resonator is dropped for those frames: the medium and fine relief vanish. If a
  debug render confirms it, the fix is to pack only the rows that sound (the samplers already skip silent rows,
  ADR-1406), with a test and an ADR.
- Nothing else is planned; any further engine change will be additive, one commit, a test and an ADR.

## 6. Profiling

`avgen --live-profile --mode live --target-fps 30 --size 1920x1080 --start 95 --measure 20 --audio feline-footwear.wav`,
through the GPU lock, Auto and fixed levels, before and after each scene. The machine is shared (the performance
agent profiles at the same time), so only back-to-back pairs are compared.

## 6a. Proofs so far (`proofs/`)

- **P1** (`cw-P1-...`): light pair + camera B + rounder, taller ridges and sunk basins, Abyssal locked, Hyperborea
  48 and 116 s. The back wall is gone; hue turns with orientation (violet, pink, blue) and the basins glow. The cells
  were too large (four across) and the pink too strong.
- **P2** (`cw-P2-...`): the bank's longest wavelength 26 → 19 m (more, smaller cells), the ridges shifted back toward
  the cool end of the gradient, the glow's threshold raised. The structure reads; the ridge network is still thinner
  and dimmer than the reference's lit carrier, so the next round widens and brightens the crest line.
- **The mode table:** a medium-scale-only sequence over loud Feline (95-98 s) never flattens, so the second resonator
  is not dropped there. No engine change; the item in §5 is closed.
- **A once-a-second step** in frame-to-frame change (×1.6-1.8) was traced to the snare's flash on every nodal line
  at once (Feline's snare falls each second): a frame-wide event of the kind §4.2 forbids. Its gain comes down and
  the snare's energy goes into travelling elements.
- **S1-S3** (`silk-S1..S3`): the veil works structurally (no edges, no ribbons; the folds read as drapery). S1 was far
  too bright; S2/S3 darker. In S3 the folds converged on the gather like rays (crests across the flow meet the sphere
  like meridians): the crests now run with the flow. The hero is still pastel and glossy: the frontal fill (the fold
  colour lifted toward white, from the camera side) flattens it; it comes down and the translucency carries the faces.

## 7. Stages and gates

1. Baseline, the finding, this plan (gate 1).
2. Cymatic World: P1 (gradient + camera), the mode-table fix, then the layers A-F; stills and one 10-15 s clip (gate 2).
3. Silk: S1 (veil + translucency), then the music layers; stills and one clip (gate 3).
4. Waveform Dimension polish; final profiles of all three (gate 4).
5. Clean reconfigure, the suites, the review and integrity checks, the report (09).
