# The Living Prism 10: Stage 6, performance and the functional tests

**Media:** `~/Desktop/av-gen-review/82-living-prism/stage6/`:
- the six stills, rendered with the song playing;
- `feline-typical-window.mp4` and `feline-heaviest-window.mp4`, with audio;
- `palette-looks-with-music.jpg`;
- `refraction-opaque-vs-0.35-vs-0.6.jpg`;
- `perf-final.txt` and `functional-tests.txt`.

**Raw records:** `perf/stage6-final.txt`, `perf/stage6-final-conditions.log` (AC on every run) and
`perf/stage6-functional.txt`.

## Conditions

| Item | Value |
|---|---|
| Machine | Apple M2 Max, AC power throughout (checked before every run) |
| Code | Release at main `c7a211f5`: honest timestamps (ADR-1600), refraction (ADR-1603), the upscaler (ADR-1607) |
| Project | `living-prism-offline.json` (the mix, analysed from the three stems) |
| Audio | `--no-audio`: the analysis drives everything, the speakers stay silent |
| Output | 1080p, 30 fps target, warm-up 5 s, measured 20 s (601 frames) |

## The windows (05 section 3)

- **Typical:** 65.70 s.
- **Heaviest:** 123.22 s.
- **The heaviest's second candidate:** 150.99 s, the synth at its brightest.

Profiling shows the second candidate is the costlier camera state: route 55.8 s, the chandelier and the geode at
close range. It becomes **the heaviest window: bars 77-86, 150.99-170.82 s**, route time 55.8-75.6 s.

## Results

**Live, `--quality auto`, 3 repetitions each:**

| Window | Ladder (601 frames) | Misses /601 | Frame p50 / p99 (ms) | GPU p50 / p95 (ms) |
|---|---|---|---|---|
| Typical | High 177, Ultra 424 (x3) | 2, 2, 2 | 33.3 / 39.5-39.9 | 29.3 / 30.4 |
| Heaviest (123 s) | High 177, Ultra 423-424 | 3, 2, 1 | 33.3 / 39.7-40.1 | 28.7 / 29.5 |
| Heaviest (151 s) | High 177, Ultra 424 / Ultra 601 / High 177, Ultra 424 | 3, 1, 2 | 33.3 / 39.5-40.4 | 29.5 / 30.6 |

**Against the thresholds (03 §11, §14):**

| Threshold | Result |
|---|---|
| T3 (Medium or better at least 90%) | **100% at High or Ultra** |
| T1 (at most 5 misses per 900) | **met**: 1-3 per 601, so 1.5-4.5 per 900 |
| T2 (p99 interval at most 40 ms) | **at the line**: 39.5-40.4 ms. Three of nine runs exceed 40.0 by at most 0.4 ms. The GPU p95 is at most 30.6 ms, so the p99 is presentation pacing (a 3-vsync frame), not the scene's cost |
| T5 (fixed cost at Emergency at most 14.7 ms) | **met**: 14.6 ms. Low is 14.8 ms |

**Headless Ultra, the effects measured independently (GPU p50, ms):**

| Window | All | Refraction off | **Refraction cost** | SSR off | **SSR cost** |
|---|---|---|---|---|---|
| Typical | 29.7 | 26.9 | **2.8** | 28.7 | **1.0** |
| Heaviest (123 s) | 29.3 | 25.1 | **4.2** | 29.1 | **0.2** |
| Heaviest (151 s) | 29.6 | 26.8 | **2.8** | 28.7 | **0.9** |

**What the costs mean:**
- **Refraction** (two nodes, the spire and the fan) costs 2.8-4.2 ms. The ladder turns it off at Low and Emergency,
  where the heroes draw as their opaque program, the Stage 3 look.
- **SSR on the pool** costs up to 1 ms, smaller than the coordinator's 2.5 ms on the blockout because the pool is now
  smaller (Stage 3). It now has its ladder lever (`waterSsrScale`).

**What Stage 6 cut** (the heaviest window, Ultra GPU 32.4 → 26.0 ms before refraction, A/B in `build/prism/ab6`):

| Cut | Saving |
|---|---|
| Haze from 12 to 8 steps | 2.4 ms |
| Light ranges cut (the formation glows to 6 m, the wash lights to 9-10 m, the passage to 12 m) and the front wash removed | 4.3 ms |

The stills before and after are near-identical.

## Functional tests (`scripts/functional_tests.py`, traces of every driver; no GPU)

| Case | Result |
|---|---|
| The three stems | every driver as designed (08) |
| **Silence** | **every signal exactly 0**, all song long |
| **A missing drum stem** | the impacts fall back to the mix's onsets: hit and front at 0.72-0.93 peak, where the stems give 1.0. Bass and synth stay on their stems |
| **A silent drum stem** (present, no signal) | the impacts are 0: a silent source claims nothing, and nothing triggers falsely |
| **Stereo only** | every driver on its fallback. The hit is selective, weighted by the onset's low register (ADR-1604) |
| **Abrupt stop** (the drums stop dead at 182.7 s) | every impact driver is 0 from that frame; nothing latches |
| **The end** (silence after 210 s) | every driver returns to rest. `section` decays from 0.68 to 0.22 by the last second, then on to 0 |
| **The palette** | six looks rendered at the same station with the music playing (`palette-looks-with-music.jpg`); the automatic change happens on Feline (Ultraviolet → Abyssal at about 60 s) |
| **The loop** | the route's seam is exact (positions identical, C1) |

**Not tested:** the CCs during playback. No MIDI device or virtual port is available to the agent (no `mido` or
`rtmidi`), so it needs the owner's controller. Soft takeover (ADR-1606) is now on main and is not yet enabled in the
project.

**Not measured:** sound-to-light latency. P1-3 is still to come.
