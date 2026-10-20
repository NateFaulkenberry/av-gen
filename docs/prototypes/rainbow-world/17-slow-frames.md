# Rainbow World 17: the slow frames

The owner, 2026-10-20:

> Investigate and fix: Rainbow World's slow frames. Typical frames are 22-24 ms, inside the 29.3 ms budget for 30
> fps. The slowest 5% run 30-35 ms at the edge crossings and rainbow holds, with unexplained 50-70 ms spikes in long
> runs.

## Method

- **Machine and settings:** M2 Max, macOS 26.7.1, Release. Live profile, headless, Ultra quality, 1920x1080, 30 fps
  fixed step. Every run is under `tools/gpu-lock.sh`.
- **The trace:** the headless profile now writes a per-frame trace (ADR-1571). It gives the piece time, the GPU span,
  the scene, depth, shadow and cull passes, the counts, and the pipeline creations since the start. A slow frame can
  therefore be placed on the ride, and two runs can be compared frame by frame.
- **Measurement data:** traces and JSON are in `build/rw2/prof/` and `build/rw2/ab/` (not committed).

## 1. The spikes are not mid-run compiles

**Two continuous 300 s runs (0-300 s)**, trace in `build/rw2/prof/long_0_300{,_b}.csv`:

| Run | p50 | p95 | p99 | max | Frames > 45 ms | Pipelines created during the 300 s |
|---|---|---|---|---|---|---|
| A | 26.0 | 37.6 | 42.7 | 66.8 | 33 | **0** (4 at start, none after) |
| B | 26.3 | 38.3 | 44.9 | 61.5 | 86 | **0** |

- **No variant was built mid-run.** Every measured frame drew with the compiled-materials variant
  (`compiledMaterials` = 1). The perf agent's hypothesis (a new material structure mid-run, so two minutes of
  interpreter frames) does not occur in Rainbow World.
  - Its structure is fixed at load. The effects added in this pass move only constants, never a program's structure
    (ADR-1493), so this stays true.
- **The spikes are timing jitter on top of expensive windows.**
  - Smoothed over 0.5 s, the two runs correlate at **0.96**: the cost of a place on the ride is reproducible.
  - Frame by frame, the residual about that smooth cost correlates at **0.12**: the spikes do not reproduce.
  - Run A's 66.8 ms frame at 259.8 s (scene 38.5 + depth 23.2, both inflated at once) is a normal frame in run B.
  - Every frame over 45 ms sits in a window whose smoothed cost is already 37-44 ms. The jitter is ±10 ms (residual
    standard deviation 2.1-2.4 ms, with tails).
  - It is consistent with the GPU being shared: another agent's avgen and an open Blender ran during these runs.
  - The cure is the same either way: lower the expensive windows' cost so the jitter no longer crosses the budget.
- **No history cost.**
  - A short run started at 40 s measured 32.2 / 39.0 (p50 / p95) over 40-50 s.
  - The continuous runs measured 32.1 / 42.3 and 31.4 over the same window.
  - Stage 8's "fresh start reads lower" was the jitter.

## 2. Where the time goes

**The expensive windows are where the camera holds under an arch** (lap A: 41.6-50.4, 77.9-87.4, 205-215 s) and
the 30-37 s crossing.

| Window | p50 | p95 | scene | depth | shadow |
|---|---|---|---|---|---|
| 40-50 s (arch hold) | 32.1 | 42.3 | 18.3 | 9.4 | 3.7 |
| 200-210 s (arch hold) | 35.1 | 43.3 | 20.0 | 9.8 | 3.4 |
| 90-100 s (fast run) | 18.3 | 22.4 | 11.1 | 2.4 | 2.9 |

**What each part of the world costs.** The A/B hid one family of nodes at a time (`nodes/<n>/visible`, the same
frames). At the 40-50 s hold, p50 / p95 and the depth pass:

| Hidden | p50 | p95 | Depth pass |
|---|---|---|---|
| nothing | 32.2-33.0 | 39.0-43.5 | 10.7-10.9 |
| **leaf cards** | **20.6** | **26.3** | **2.0** |
| trees (wood, core, leaves) | 19.7 | 29.0 | 2.0 |
| grass | 31.2 | 42.5 | 9.9 |
| flowers | 31.5 | 37.1 | 10.8 |
| clouds | 31.7 | 40.7 | 10.0 |
| rainbows | 31.7 | 42.9 | 10.9 |
| the band | 30.4 | 41.0 | 10.8 |
| rocks | 31.2 | 43.3 | 10.6 |

**The leaf cards are 12 ms of the hold** (9 of it in the depth prepass). Which cards, by changing only the leaves' LOD:

| Leaves | p50 | p95 | Look |
|---|---|---|---|
| as built (22 / 55 m levels, to 150 m) | 33.0 | 43.5 | |
| near levels sooner (10 / 30 m) | 32.7 | 44.8 | no gain: the near cards are not the cost |
| stop at 90 m | 25.8 | 31.3 | the trees at 90-150 m go to bare branches (the core is too small to stand in) |
| **a fourth, thinner level from 90 m** | **26.5** | **32.9** | canopies at 100 m patchier, still trees; nothing changes nearer |
| fourth level from 75 m, third from 45 m | 26.2 | 31.9 | the same |

- **The cost is the far cards (90-150 m):** thousands of alpha-tested cards a few pixels across.
  - Every one is tested in the prepass, and a 2x2 quad is shaded in full for a fraction of a pixel.
- **Moving the leaves out of the depth prepass does not help.** I tried it as a per-node flag and removed it again:
  the lit pass took the same 9 ms.

**Measured levers at the 200 s hold** (the profile's own A/B, `--verify-candidates`, four-level leaves in):

| Lever | Saving |
|---|---|
| render scale 0.85 | 9.2 ms (23%) |
| no material programs | 6.4 ms |
| LOD bias 2 | 3.5 ms |
| draw distance 75% | 1.1 ms |
| shadow atlas 1k | in the noise |

## 3. The fix, and what it bought

**Leaves: a fourth LOD level from 90 m** (`build_world.py`, the `*_leaves` nodes): `count` 4, `distance3` 90. It is
ADR-1548's shell thinning one level further, with no engine change.

**Full 0-300 s run, as built against four-level leaves** (`build/rw2/prof/l4_0_300.csv`):

| | p50 | p95 | p99 | max |
|---|---|---|---|---|
| As built | 26.0 | 37.6 | 42.7 | 66.8 |
| Four-level leaves | 24.6 | 35.5 | 41.5 | 59.4 |

Per 10 s window (p50 / p95):

| Window | As built | Four-level leaves |
|---|---|---|
| 40-50 | 32 / 42 | 27 / 32 |
| 30-40 | 29 / 39 | 27 / 34 |
| 200-210 | 35 / 43 | 34 / 46 |

The 40-50 s hold improved; the 200 s hold did not, because its trees are near (22-55 m), not far.

**Not yet met:** p95 is still over 29.3 ms at the 200-215 s hold and at a few other windows, at Ultra 1080p and full
render scale. What remains, and what this pass does next (section 4), is in the next revision of this report.

## 4. Next (in progress)

- Measure the live mode (`--mode live`), the owner's own measurement: the auto ladder and present included. The
  numbers above are pinned Ultra.
- Decide the live render scale or quality profile in the project's `live` block.
  - The fisheye pass already resamples the frame non-uniformly, so 0.85-0.9 may cost little to the eye.
  - It has to be checked on stills, not assumed.
- The 200 s hold: the near trees' cards and the clouds (3 ms there).
