# CHRONOS, Temporal Singularity: Gate S2, feedback and chaos, the drums, polish, the profile (Tests E, F, G)

Media is in `~/Desktop/av-gen-review/104-chronos/singularity-s2/`. One ADR: **ADR-1937**, which gives the feedback a
routable centre.

## The owner's S1 notes

1. **The core's edge.** The core used to read as a hard-edged black cut-out. Now:
   - The cut is tighter, and a soft luminous halo wraps it.
   - A thin, continuous photon ring sits on the edge. Its brightness flows round it and is never broken into dashes;
     it is brighter on the beamed side.
   - A little light bleeds just inside the edge. The late lens bends all of it, so the glow is lensed.
   - The centre stays black.

   See `singularity-s2-core-before-after.png`.
2. **Figure lighting, an A/B.** A, the pale flat-hot look the owner likes, stays the default.
   - B is lit and tinted by the disk: an amber-crimson core light, a magenta-violet rim from behind, a crimson fill
     from below, and the figures' pale tinted toward rose.
   - Both are shown at the same instants in `singularity-s2-lighting-ab.png`.
   - B is `singularity-figures-b.scene.json` and `singularity-lightb.json`. Switching means pointing the project at
     that scene.
3. **The striped combs and rainbow edges** are kept as they are: no smoothing, no speed-tied echo spacing, no
   warming. The palette guard from S1 is unchanged.

## My review items

- **The burst shells.** They read as blurry cream smears. Each is now two thin rippled shells made of filaments
  (ridged noise along the shell), plus fine radial ejecta whose heads run gold and tails magenta.
- **Feedback (§5F), with a centre (ADR-1937).** The engine's feedback (an FIR over the clean ring) now zooms and turns
  about a routable centre. That centre is keyed to the core's screen position from `field.py`, so the frame pours into
  the singularity wherever it wanders:
  - 10 taps, decay 0.8;
  - zoom 0.988 per frame (falling in);
  - a clockwise turn of 0.7° per frame;
  - a slight hue walk.
  - **Effect:** everything, figures included, leaves short spiralling after-images falling inward.
- **Lens edges.** The late lens now samples mirrored at the frame's edges. Clamping had smeared the edge pixels into
  thin streaks.

## The drums (§9, Test F)

The drum stem only, through five enveloped signals, every one with an attack of at least 60 ms and a decay of at least
450 ms. Nothing is routed to brightness. The background and the lens receive the same values wherever both draw the
same field.

| Signal | From | Drives |
|---|---|---|
| drum energy | `stem.drums.level` | turbulence, white-hot highlights |
| strong transients | `stem.drums.onsetStrength` over a threshold | expansion (both layers), the tear's shear in the disk and the lens |
| rhythmic activity | `stem.drums.level`, slow | rotation (both layers) |
| low frequencies | `stem.drums.bass` | the core's radius (both layers), the lens's strength |
| high frequencies | `stem.drums.treble` | radial streaks, fine tearing |

The routes live in `build_singularity.py` (`AUDIO_SOURCES`, `AUDIO_ROUTES`) and are ordinary project routes, so they
stay configurable.

**Measured on the full render.** The drum onsets come from the stem itself: 452 onsets, about 2.1 a second. A flash is
a step in the frame's mean luminance.

| | Full render (with drums) | Test E (no drums), the control |
|---|---|---|
| Mean luminance step on frames within ±2 of an onset | 0.46/255 | |
| Mean luminance step on other frames | 0.31/255 | |
| Step ratio, on-onset to off-onset | 1.47 | 0.95 |
| Largest step anywhere | 4.3/255 (at 0.27 s, the opening, not a hit) | |
| Steps over 5/255 | none | |
| Steps over 3/255 | 3 | |
| Motion in the 6 frames after a hit, against the rest | 5.75 against 4.52 (ratio 1.27) | 0.98 |
| Overall motion | about twice Test E's | |

**Verdict.** The drums move the field without flashing it. Hits produce 27% more motion, but almost no change in
brightness. The figures are in `singularity-s2-test-f-flash.json`.

## The black-frame fault, found and fixed

The first full render went black from 86.97 s to the end.
- **Bisect.** A bisect at 90 s removed, in turn, the drum routes, the feedback, the lens and the figures. Only removing
  the lens let anything show, and then only the figures: the background layer itself was black.
- **Cause.** `tearIntegral` sums `tanh(0.6·(t − t_k))`. Metal computes `tanh` through `exp(2x)`, which overflows a
  float once its argument passes 44.4. That is 87 s after the first tear (13 s), exactly where the frame went black.
  inf/inf gives NaN; the whole field went NaN, so the background was black and the lens sampled NaN everywhere.
- **Fix.** The argument is clamped to ±10, in `field.wgsl` and `field.py` alike; tanh(10) is 1 to float precision, so
  nothing earlier changes.
- **Check.** The re-rendered full song was checked on every frame, not samples: no frame is under 10/255 mean, and the
  darkest frame's mean is 42.8.

## Test G, the profile

`--live-profile --mode headless`, 1080p, Ultra, the full composition with the drums, measured over 20 s from 23 s on
an Apple M2 Max (`singularity-s2-test-g-profile.txt`, `-profile.json`, `-trace.csv`).

| | Median | P95 | P99 |
|---|---|---|---|
| Frame | 13.81 ms | 14.84 ms | 15.13 ms |
| GPU | 12.75 ms | 13.74 ms | 13.98 ms |
| CPU work | 0.44 ms | 0.79 ms | |

- **Budget.** All 1,200 measured frames were under even a 60 fps budget, so the 30 fps target is met with more than
  half of the frame to spare.
- **GPU, by pass (medians):**

  | Pass | ms |
  |---|---|
  | singularity background layer | 9.73 |
  | gated sampler | 0.87 |
  | feedback | 0.79 |
  | late lens | 0.47 |
  | scene | 0.54 |
  | bloom | 0.14 |
  | tonemap | 0.09 |
  | captures | 0.03 |

- **The background layer is the bottleneck**, at more than twice the plan's 4 ms estimate. It is also the obvious
  lever if the scene ever needs headroom: its smooth bands could render at half resolution.
- **Memory.** The colour ring is 133 MB and the slot ring 33 MB.
- **Offline.** The full song (6,315 frames) renders at 79 fps.

## Files

| File | What |
|---|---|
| `singularity-s2-full.mp4` | **the full render**: the whole song (210.5 s) with sound, the full composition with the drums |
| `singularity-s2-full-{70,95,120,150,190}s.png` | stills from it |
| `singularity-s2-test-e.mp4`, `singularity-s2-test-e-{10,31}s.png` | Test E: the full composition with no audio routes (45 s, with the track playing) |
| `singularity-s2-test-f-60-120s.mp4` | Test F: 60 s cut from the full render (the drums driving it) |
| `singularity-s2-test-f-flash.json` | Test F's flash and synchronisation metric |
| `singularity-s2-test-g-profile.txt`, `-profile.json`, `-trace.csv`, `-capture.png` | Test G: the measured profile |
| `singularity-s2-lighting-ab.png`, `singularity-s2-lighting-{a,b}-{10,22,34}s.png` | the figure-lighting A/B, at the same instants |
| `singularity-s2-core-before-after.png`, `singularity-s2-core-after-{06,13,22,40}s.png` | the core: S1 against S2 |

## Honest critique

- **Works.**
  - The whole frame is alive for the whole song, and darkness is contrast, not emptiness.
  - The core now glows rather than cuts.
  - The drums visibly push the field (expansion, tearing, lurching rotation) without one synchronised flash.
  - The feedback makes everything spiral inward with the core.
  - The combs and rainbow edges stay intact.
- **Weak.**
  - Late in the song the core wanders to the frame's edge and swells (around 150 s). It is dramatic, but for a few
    seconds it is half off screen.
  - The bursts read better, though at speed they are brief.
  - The background layer costs 9.7 ms. It fits comfortably, but it is the one place to optimise first.
