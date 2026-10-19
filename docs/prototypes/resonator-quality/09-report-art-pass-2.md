# Targeted Art Pass II: report

- **Brief:** [07-brief-art-pass-2.md](07-brief-art-pass-2.md) (verbatim; cut at 50,000 characters in §11).
- **Plan:** [08-plan-art-pass-2.md](08-plan-art-pass-2.md) (the findings, the debate, the proofs).
- **Branch:** `art/resonator-pass-2`. The art agent's work: §§2-6 and 8-9 and the scenes' own cost. The live path
  (§7) is the performance agent's, on `perf/resonator-live`.
- **Media:** `~/Desktop/av-gen-review/69-resonator-art-pass-2/`: `baseline/`, `proofs/`, `stage2-cymatic-world/`,
  `stage3-resonant-silk/`, `stage4-waveform-dimension/`.
- **Audio:** Feline Footwear, fed with `--audio` (the scenes carry no track); Trench (its 85-95 s breakdown is the
  quietest passage of the three tracks) and Hyperborea (the gradient reference's seconds).
- **Engine:** no engine change. The ADR range 1480-1489 was reserved and is unused.

## Summary

| | Cymatic World | Resonant Silk | Waveform Dimension |
|---|---|---|---|
| The main change | The colour pass's gradient restored inside the ten palettes; camera B, no back wall; four motion layers | The Veil (an enveloping shell of the same cloth) replaces the far drapes; translucency; music layers; the resonator baked once a frame | Translucency at the folds |
| Fields (of 16) | 15 → 15 | 14 → 16 | 13 → 13 |
| Ground / hero program ops (of 48) | 41 → 47 | 40 → 46 (+ the veil's 14) | 31 → 38 |
| Motion (15 s, contrast-normalised, max/median) | 0.28 → 0.77 px/frame, 1.38 | 1.13 px/frame, 1.60 (spikes removed: 3.0 → 1.35 on 3 s) | 1.56 px/frame, 1.31 |
| Live 1080p30, Auto | Low (0.50) → **Medium (0.71)**, 0 misses | Low (0.50) → Emergency (0.50), GPU +2.0 ms (§5) | Low (0.50) → Low (0.50), 0 misses |

## 1. The gradient reference

The reference's BEFORE column is the Cymatic World colour pass, commit `1ba14dbe` (the next commit to the scene is
"motion, Pass 1"); its generator is in git. What made the gradients, from that code (plan §1):
1. **Hue from light, not albedo.** A saturated cyan key on one flank of every ridge and a complementary fuchsia
   grazing fill, (N·L)⁴, from the opposite quarter, over dark, low-chroma albedo.
2. **Relief that shows every orientation.** Tall, wide, rounded sand ridges (2.6 m), seen steeply from close.
3. **A three-value structure.** Near-black floors, the lit ridge network, white sheen on the tops; fuchsia emission
   only on the inner walls of the hardest-ringing cells.

The scene before this pass drew hue from an albedo field by the sign of the figure's lobes, under a key 45% toward
white, on shallow lobes seen obliquely from 78 m: two flat tones per palette.

## 2. Cymatic World

**Gradient, within the palette system.**
- Each palette lights the ground with its own two-hue pair. The key is the palette's spectral role (12% toward white,
  where it was 45%). A counter hue per palette, chosen to complement its family, carries the counter light and the
  inner-wall glow: Abyssal fuchsia against ice blue (the reference's pair), Molten blue-teal against orange, Crimson
  blue-violet against red, Chlorophyll violet against green, and so on (`COUNTER` in the generator).
- The OKLab field stays as the broad regional layer, the ridges shifted toward its cool end.
- Value structure: the ambient floor 0.6 → 0.12; floors darken over the sand's 0.25-0.85; sheen on the ridge
  network; a brighter, wider crest line with an audio-independent floor (quiet passages keep a lit network).
- Relief: tall rounded tubes over flat floors (3 m, sand sharpness 1.2, no signed domes or sunk basins); a finer
  bank (longest wavelength 26 → 14 m), so a frame holds many cells, as the reference does.
- The glow lives on a band of the cells' inner walls (a remap and a triangle ramp of the sand). The one-sided step it
  replaced covered everything but the antinode cores once the ridges widened; the emission, albedo and light layer
  renders showed it.

**Back walls and dead space.** Camera B of the correction round (36 m out, 42 m up, 28 mm, about 50° down), which
the owner approved then: the frame's top edge meets the floor short of the terraces all along the arc. The bake
grids now cover ±96 m (finer cells). Verified at the arc's start, middle and far end (`cw-arc-no-wall.png`).

**Motion layers** (all functions of the clock and the audio history, so a seek lands where a play does):

| Layer | What you see | Mechanism |
|---|---|---|
| A | A broad bass wavefront travelling out from the basin's heart, lifting the figure | A Spectrum field (bass, ADR-1116) at 18 m/s in the baked relief, 0.6 m |
| A (kept) | The four families and their director, the build ringing more modes | ADR-1471, LFOs, routes |
| B | Ripples from transients riding the figure | The onset ripple rows (ADR-1409) in the relief, 0.35 m |
| B fine | The fine lattice's sand rises with the hats inside the medium net | The medium and fine readers merged into one (no vibration term) |
| C | A scan of light on each kick, crossing the basin along the ridges, then fading | A planar Onset field (45 m/s, 4 m wide) read by the program on the crest line |
| C (kept) | Light running along the ridges from where a hit landed | The ripples' amplitude on the crest |
| D | Glints on the ridge tops (the sheen, sharpened by the hats), hottest on the crest as the scan passes | Program terms on existing masks |
| E/F (kept) | The band's drift (an energy integral), the palette journey, the director | Routes |

- The snare's flash on every nodal line at once came down to a third: it produced a frame-wide step every second
  (measured).
- Budget: the excitation now comes from the colour field (|F|², no reader of its own) and medium + fine share one
  reader, which paid for the scan, the ripples and the bass front: 15 of 16 fields, 47 of 48 ops, and a compound
  holds at most four children.

**Motion, measured** (15 s, Feline 95-110 s): contrast-normalised frame-to-frame change about 2.7× the baseline's
(0.77 vs 0.28 px/frame), smooth (max/median 1.38, no spikes).

## 3. Resonant Silk

**The Veil** (plan §3.1, approach E3). One shell of the same cloth around the whole scene, so no edge shows in any
frame:
- its folds flow with the cloth (a sine along rising bands, drifting across the flow; S3's crests across the flow
  met the sphere like meridians and converged on the gather as rays, which read as a tunnel);
- the gather's bass swell lifts it as it passes (the hero's own `bassHistory` field);
- its colour is the hero's gradient roles along the same world band, dimmed, flowing at half the hero's rate;
- a glow in the shell behind the gather is the scene's light source and what backlights the hero;
- the far drapes and their meshes are gone.

**Translucency** (plan §3.2, M2), as emission: the glow behind the gather passing through the sheet, a
forward-scattering lobe about the view ray toward it × the facing term, sharpened (power 2.5), gone where the cloth
turns (the folds). Panels near the gather glow in their own colour between dark fold lines. A broad, wrapped version
(S5) only flattened the cloth; contrast is what reads as light passing through. The satin is softer: no metal,
rougher, the reflection strip off, the key and lamps lower; the thin film carries the iridescence.

**Music:**
- ripples running along the wall into the gather, lifted by the mids (a planar wave summed with the snare front in
  the deformer that carried the snare: a node takes eight);
- glints at fixed places along the fold edges (a sharpened lattice in the cloth's u) on the treble;
- fold tension with the slow energy;
- the veil's swell on the bass history; the kick fronts, modes and swell kept.
- The raw treble on every fold edge at once was a frame-wide flash on each hat (single-frame spikes, max/median
  3.0); the lattice and a lower gain removed them (1.35).

**Cost**, the performance agent's two measured items, both adopted:
- `environment.depthPrepass: true`;
- the resonator baked once a frame into a 64×48×64 volume (ADR-1440) that both side compounds read, instead of each
  sampling the whole resonator per vertex. At 64³ with the 0.15 m normal step it faceted the near wall; with a
  1.6 m step it does not, and 128³ looked the same at +1.2 ms.
- The veil's cost was its vertex count (33k vertices: +9 ms; 8k: +3.4; 4.6k: +2), not its swell or its noise; it is
  96×48.

## 4. Waveform Dimension

Restrained: translucency at the folds. Where a sheet turns from the eye it is thin to the eye, and the rim light
behind the sculpture comes through there in the sheet's own colour (backlight × Fresnel² × albedo). A first version
(backlight alone) lit every camera-facing face and lifted the shadows; a narrow rim in the palette's rim role was
built and is off (barely visible, and three ops of a scene at the edge of its budget). Geometry, camera, glow, the
crimson edge line, palettes and every route are unchanged.

## 5. Live profiles

`avgen --live-profile --mode live --target-fps 30 --size 1920x1080 --start 95 --measure 20 --audio
feline-footwear.wav`, through the GPU lock, each final profile taken back to back with its baseline (the head before
the pass, restored from `0bfac1d9`): the machine is shared with the performance agent, and a profile taken hours
apart drifted by up to 3 ms (Silk's hero measured 34.9 then 32.4 ms). JSON records: `stage*/*.json`.

| Scene | Run | Auto level (scale) | Frame p50 / p95 | GPU p50 / p95 | Misses |
|---|---|---|---|---|---|
| Cymatic World | baseline | Low (0.50) | 33.3 / 37.1 | 26.8 / 27.8 | 0 / 600 |
| | final | **Medium (0.71)** | 33.4 / 35.6 | 31.1 / 33.1 | 0 / 601 |
| Resonant Silk | baseline | Low (0.50) | 33.3 / 36.2 | 31.7 / 35.5 | 0 / 601 |
| | final | Emergency (0.50) | 34.8 / 38.0 | 33.7 / 36.5 | 0 / 572 |
| Waveform Dimension | baseline | Low (0.50) | 33.5 / 36.2 | 31.8 / 33.1 | 0 / 600 |
| | final | Low (0.50) | 33.3 / 36.1 | 30.3 / 31.7 | 0 / 601 |

- **Cymatic World** holds a higher level than before: camera B removed the oblique view's ridges-behind-ridges
  overdraw, which paid for the new layers and more.
- **Waveform Dimension:** at fixed Low the polish costs 0.8 ms (30.5 → 31.3); Auto holds Low with no misses, as
  before. The rim term was cut because v2 with it cost 1.7 ms and tipped Auto to Emergency (2.8% misses).
- **Resonant Silk** is about 2 ms over its baseline on this branch alone and its frame interval sits just above the
  budget (34.8 ms) at Emergency. Fixed-Low breakdown, measured back to back: the veil about +2 ms (96×48; at 256×128
  it was +9), translucency +0.9, the hero's music layers ≈ 0, the resonator bake −2.3 against two per-vertex
  evaluations, the prepass −1. The performance agent's engine changes measured Silk at Low 27.7 → 18.7 ms with
  identical pixels, so after both branches land there is headroom; the coordinator will re-profile then.

## 6. Evidence

`~/Desktop/av-gen-review/69-resonator-art-pass-2/`:

| Folder | Files |
|---|---|
| `baseline/` | the three scenes at Feline 8/60/95/130/175 s and Trench 60/88 s; Cymatic World at the reference's Hyperborea seconds (Abyssal); `cw-reference-vs-current-*.png`; baseline profiles |
| `proofs/` | CW P1-P2, Silk S1-S3 (gate 1) |
| `stage2-cymatic-world/` | `cw-reference-before-after-hyperborea-10-48-116-134s.png` (reference / before / after); `cw-palettes.png`; `cw-arc-no-wall.png` (the arc's start, middle and far end); `cw-music.png` (loud and quiet); `cw-scan-filmstrip-feline-97-99s.png` (the scan travelling); `cw-tuning-*.png` (counter hues, quiet floor, fewer glow cells); `cw-feline-95-110.mp4` (15 s) |
| `stage3-resonant-silk/` | `silk-before-after.png` (Feline 60/130 s, Trench 88 s quiet); `silk-all.png` (high energy, palettes); `silk-motion-filmstrip-feline-96-98s.png`; `silk-feline-95-110.mp4` (15 s); `iterations/` (S1-S10, the veil's resolution, axis and crease, the bake at 64³ and 128³) |
| `stage4-waveform-dimension/` | `wd-before-after.png` (three seconds, two palettes); `wd-feline-95-105.mp4` (10 s); `rejected-v1-backlight-everywhere.png` |

On the §9 list, per scene: the sustained passage, a transient, the travelling light, the ripples, two or more
palettes, the quiet passage and a 10-15 s sequence are each in the folder above; the before/after sheets use the same
resolution, camera, palette journey and seconds as the baseline.

In `silk-all.png` the pale region at the top right of the 175 s panel is the veil's glow behind the gather seen soft
through the fog, and the smooth dark region at the bottom right of the Oil-Slick panel is the veil's lower shell below
the sail's hem (its folds are shallow there). Neither is an opening: the veil is a closed sphere about the scene.

## 7. Checks

Run once at the end on head `5671ce1f` (the scenes as shipped; this report's commit follows it), after a clean
`cmake --preset release` and a full build (981 steps, exit 0). Each process's own exit code:

| Check | Result |
|---|---|
| `tools/ci/run-suite.sh cpu-assets` (under the GPU lock) | **PASS, exit 0**: 4321 passed, 0 failed, 19 skipped, 1 expected failure (the shouldfail case); ran 4341/4341 |
| `tools/ci/run-suite.sh gpu` | **PASS, exit 0**: 671 passed, 0 failed, 1 skipped; ran 672/672 |
| `tools/review_examples.py --only` Cymatic World / Resonant Silk / Waveform Dimension | `ok 1` each, exit 0 |
| `tools/check_project_integrity.py` | **0 problems**, exit 0 |

The route audit (`avgen --audit-routes`) of each scene shows no dead route beyond the audit's own no-audio case for
`audio.*` sources (which the shipped Silk also had); every MIDI route, CC 9 / CC 10 included, is present.

## 8. Known limitations

- **Silk's live cost** (§5): over budget on this branch alone by about 1.5 ms; it relies on the performance branch.
- **Cymatic World:**
  - at the loudest, densest passages (Feline 60 s) most cells still ring and carry the glow; the threshold is fixed,
    while the colour field that feeds the excitation scales with the bass route;
  - the quiet floor keeps the network lit, but in silence the counter hue is only on the far flanks;
  - camera B trades the overhaul's horizon and ranges for a frame that is all ground (the brief's "no dead space",
    and the reference's composition).
- **Resonant Silk:**
  - the veil has no parallax beyond the camera's few-metre drift (it is 300-700 m away);
  - the seam band at the gather reads a little like a cuff;
  - the cross-wise hat sub-waves were cut to pay for the resonator bake's field (16 of 16 fields are in use);
  - the glitch accents (§5.4, optional) were not built.
- **Waveform Dimension:** the effect is deliberately small; it is clearest on the fold edges in the cooler palettes.
- **Not done:** live MIDI was not exercised in renders (MIDI routes are unchanged and the audit lists them as
  live-only); no ADR was needed, so 1480-1489 stay unused.
