# Rainbow World 10: Stage 6 report (lighting, look, speed, cost)

Stage 6 of the brief: lighting and VFX refinement, the coordinator's Stage 5 notes, and the first half of the budget
work. The world is v24 of `examples/rainbow-world/build_world.py`.

Review images are in `~/Desktop/av-gen-review/74-rainbow-world/stage6/`:

| File | What it shows |
|---|---|
| `clip-pacing-24-44s.mp4` | 20 s at 1280x720: a low 30 m/s run past streakers, a 45 m/s surge over the rim, a brief hold rising under an arch |
| `01-pacing-clip-sheet.png` | The clip, every 0.5 s |
| `02-ride-stills.png` | 16 stills round the ride |
| `03-rainbow-shadow-with-without.png` | The arches' solid shadow (ADR-1547) |
| `04-leaf-lod-thinning.png` | Leaf LOD: thinned, unthinned, and Stage 5 (ADR-1548) |
| `05-five-looks.png` | The five CC 9 looks at three moments |
| `06-golden-dusk-meadows-fixed.png` | Golden Hour and Lavender Dusk with greener meadows |
| `07-sky-ground-colour-test.png` | Why the sky's ground colour stays pale |
| `08-stage5-vs-stage6.png` | Before and after |

## 1. Speed you can feel (note 1)

The Stage 5 clip opened in an ease-out. From 28 to 38 s the ride was still accelerating out of an arch hold over
open meadow, so nothing near the camera changed. Changes:

- **Streakers.** Every 13-26 m on the fast stretches, something stands 5-16 m off the ride line: a tight flower clump
  (50%), a stone (28%), or a Blossom Puff or Ridge Pine further out (22%). There are 118 round the loop. In the clip
  sheet, consecutive 0.5 s frames now differ clearly at the frame's edges (24-28 s).
- **Low and fast.** Between landmarks the ride flies at 4.5-8 m. It rises to 16 m over the 200 m before each arch,
  where it slows and the view opens.
- **Brief holds.** 12 m/s (40% of base), easing over 100 m; they were 9 m/s easing over 170 m. A lap is now 253 s at
  a 30 m/s mean, and the two laps make a 507 s loop.
- **A bug fixed.** The crossing surge was capped by the hold term (`min`). It is now a product, so the ride really
  does reach 45 m/s over the rim.

## 2. Near clouds (note 2)

- A second, broad billow octave (~150 m) gives a near mass lobes and shaded clefts.
- Only the most sun-facing lobes now reach the top stop: the ramp position is scaled to 0.86 before the ramp.
- The undersides are violet (`#8478CC`, mixed up to 70%), not the old peach.
- The stops are lower (`#4A50AC`, `#8A8ED6`, `#F4CFA4`, `#FFEAD0`, gain 0.88), so nothing reaches cream-white.

## 3. Sky and haze (notes 3 and 4)

- **The sky.** Sky haze is 0.08 (was 0.2), and there is a wider sun glow (0.32 rad). The zenith now reads deep and
  the glow shows where the sun is (`02`, t=26 and t=39).
- **The ground colour stays pale.** A deep sky below the horizon was the obvious move, and I tested it at three
  values (`07`). The sky's ground colour is also the ambient light from below, and a deep one turned every shaded
  face navy. The deep sky below is the cloud floor instead: its troughs are now each look's zenith, with its heads on
  top.
- **The milky frame.** Clip and still frames measure identical (1st, 50th and 99th percentiles of luminance agree to
  0.1). The milkiness was the look itself, with the 1st percentile at 48/255. Changes:
  - atmospheric haze 0.42 to **0.30**;
  - its tint moved from pale `#8FB4E8` to saturated `#5FA2E6`;
  - every look's haze tint saturated the same way;
  - a slight negative lift (-0.006).

## 4. Defects found and fixed

- **The rainbow arches threw solid shadows.** A dark arc lay across the meadow under every arch (`03`). The shadow
  pass draws translucent entities, and a gltf node had no way to opt out. **ADR-1547** adds `"castsShadow": false`
  on a gltf node.
- **Golden Hour and Lavender Dusk meadows were olive and mustard** (`05`). Their meadow ramps are now greener
  (`06`).

## 5. Cost (note 5)

**ADR-1548: leaf cards thin instead of collapsing.** The procedural LOD ladder deleted whole leaf cards and grew
none, so the canopy went bare. With `"thinning": true` it removes whole cards and grows the rest, as imported gltf
trees already do. The leaves now take three rungs (40 m, 85 m) and reach 150 m.

| t | No leaf LOD (Stage 5) | Thinned LOD |
|---|---|---|
| 200 | 53.5 ms | 43.1 ms |
| 30 | 36.0 ms | 33.6 ms |

Also in this stage:
- distance LOD on tree wood, canopy cores and the cloud masses;
- grass reach cut to 58 m and flower reach to 62 m;
- the rainbows out of the shadow maps.

**Now (Ultra 1080p, headless, 60 s windows, 1,800 frames each):**

| Window | p50 | p90 | p95 | p99 |
|---|---|---|---|---|
| 0-60 s | 28.6 | 35.9 | 40.2 | 50.1 |
| 120-180 s | 24.9-25.6 | 31.8-33.6 | 33.8-38.3 | 38.8-49.7 |
| 240-300 s | 28.2 | 35.4 | 38.7 | 50.0 |

The 120-180 s window was run twice. The runs differ by up to 17 ms at their worst frame (61.7 against 44.2), so the
tail of these numbers is noisy.

**The median now meets the 29.3 ms budget. P95 does not.** The worst frames cluster under Great Trees seen close
(alpha leaf-card overdraw) and in some crossings. Measured fresh at the same moments, the crossings are 24-29 ms. The
spikes there appear only in a continuous run, so they are a history effect I have not yet pinned down.

## 6. Weak points now

1. **P95 over budget** (section 5). This is Stage 8: close leaf-card overdraw, the ground program's cost, and the
   crossing spikes.
2. **Approach to a crossing** (clip 28-29.5 s): for about 1.5 s the frame is mostly sky and sun glow as the ride
   drops toward the rim.
3. **Golden Hour sunlit meadow** still leans yellow. That is a look decision, kept warm on purpose.
