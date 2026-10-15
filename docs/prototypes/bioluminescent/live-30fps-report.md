# THE RIFT LIVE at a 30 fps live target (1080p)

**Request** (the owner): "see if it can be optimized to hit 30 fps target for live playback".

**Answer: yes.** THE RIFT LIVE now holds 30 fps at 1920x1080 with the transport playing.

- **Frame time:** median 33.3 ms, P95 35.6-36.4 ms.
- **Deadline misses:** 0-0.1%.
- **Level:** the live controller settles at **Low** (scale 0.71) through the build and the drop, and at **Medium**
  (scale 0.85) in the body.

Before, it could not hold 30 fps. The controller fell to **Emergency** (scale 0.50), and the frame still took 37-40 ms,
with 29-44% of frames missing their vsync. At High, the headless GPU time halved, from 50 to 25 ms.

No change is visible in matched stills, and the offline project (`rift.json`) renders the same bytes. High is not held
live yet. §6 gives the reason, which is the GPU's clock under a 30 fps frame cap, not the scene.

Decision record: [ADR-1344](../../decisions/ADR-1344-work-the-gpu-would-discard-is-skipped-before-it-is-launched.md).
Stills and raw data: `~/Desktop/av-gen-review/48-rift-live-30fps/` (`stills/`, `data/`, which also holds the scripts).

## 1. Conditions

| | |
|---|---|
| Machine | MacBook Pro Mac14,6, Apple M2 Max, 64 GB; macOS 26.7.1; LG 5K at 60 Hz |
| Renderer | WebGPU / Dawn / Metal, Release build |
| Baseline | `5da4a2f6` (main before this work), with the original `rift.scene.json` |
| After | `1822ae43` (branch `perf/rift-live-30fps`), with the regenerated scene. Merged with main at `47afa203` before the suites |
| Output | 1920x1080 projection window (`--live-profile --mode live`), with the real loop, present, Fifo and the frame cap |
| Target | 30 fps (`--target-fps 30`): a 33.3 ms frame and a 29.3 ms GPU budget (ADR-1080). The Live panel's Target sets it (ADR-1341, ADR-1360) |
| Strategy | `effects_first` (the project's) |
| Audio | `rift-live.json` takes live input. To play the arc deterministically, the measurements use a scratch copy with Trench as the project's audio (`assets.audio`) and the `sonic` block empty, so the file plays to the output device and drives the routes and the arc. Everything else is `rift-live.json` as committed |
| Rules | Every run under `tools/gpu-lock.sh`. Each statistic is a median or P95 over a steady 20-30 s window after a 6-10 s warm-up. A/Bs are interleaved |

The live number to trust is the projected loop's frame interval and deadline misses. GPU milliseconds in the live loop
are inflated by the GPU's clock scaling (§6). Headless GPU milliseconds are the stable unit for comparing changes.

## 2. Baseline: what dominated

**Live, auto, baseline** (`B0-*`):

| Start | Settled level | Frame median / P95 | GPU median | Deadline misses |
|---|---|---|---|---|
| 40 s | Emergency (0.50) | 39.5 / 58.5 ms | 38.7 ms | 43.8% |
| 96 s (the drop) | Emergency (0.50) | 39.0 / 56.7 ms | 38.3 ms | 36.6% |
| 150 s (the body) | Emergency (0.50) | 37.7 / 49.2 ms | 37.1 ms | 29.0% |

**Headless, pinned High, 96 s:** the GPU took **50.3 / 49.7 ms** (two runs; P95 63 ms).

- **Scene pass:** 41.8 ms.
- **Volume march:** 4.3-4.7 ms. It runs at quarter resolution with 16 steps.
- **Everything else:** under 2 ms.

**Attribution:** one subsystem removed at a time, at the drop. Data in `data/ab2-*`, `ab3-*` and `ab8-*`.

| Removed | GPU, Emergency | Reading |
|---|---|---|
| nothing | 37.0 ms | |
| the sea pens' bodies (hidden) | **18.8 ms** | half the frame |
| crinoids / mats / fans+whips+lanterns (hidden) | 37.5 / 38.7 / 38.6 ms | no saving |
| all bodies | 15.4 ms | |
| the ecosystem | 30.7 ms | |
| the 72 crown lights | 32.1 ms | |
| the terrain | 35.2 ms | |

- **The sea pens' bodies:** they were the defect. 25,640 dark, double-sided fronds were drawn to 80 m.
  - **Distance:** at 30 m the cull removed them all at no visible cost (`ab8`: `near20` matches hidden).
  - **Double-sided:** drawing them single-sided matched hidden too. The frond winding faces away, so single-sided is
    not an option. Only the distance mattered.
  - **What carries the meadow:** the seapenPolyps emitter layer gives the meadow its light, and it keeps its own 300 m.
- **The ecosystem's emit:** it launched a thread per (host, template point), 56.8 M a frame over 12 layers. The two
  crust layers alone were 34.7 M.
- **The volume march:** it evaluated the eight frame lights at every step of every pixel.

## 3. The changes, each measured

| # | Change | Before -> after | Look |
|---|---|---|---|
| 1 | **Sea pens' bodies to 30 m** (from 80), live only | High headless, 96 s, 2 runs: **51.5 / 50.5 -> 32.1 / 32.1 ms** | no visible change (§5) |
| 2 | **LOD rungs** on fans/whips (35/70 m), crinoid arms (120/240 m), stalks (150/300 m), live only | on top of 1: **32.1 -> 26.9 / 26.9 ms** | differences only at emitter occlusion edges (§5) |
| 3 | **Ecosystem host cull** (engine) | `--ab ecohostcull`, 4 pairs: **-1.11 ms** (38.99 -> 37.81; pairs -1.31, -1.05, -1.31, -1.11; floor 2%) | the same bytes (GPU test) |
| 4 | **Volume-march frame-light mask** (engine, shader) | High headless, 3 interleaved pairs: march **4.72 / 4.72 / 4.65 -> 3.87 / 3.74 / 3.80 ms**, GPU 25.49 / 25.30 / 25.36 -> 24.90 / 24.84 / 24.84 ms | the same values (GPU test) |

**1 and 2 are live-only by construction.**

- **The sea-pen cull** is a distance cull, which an offline render lifts (ADR-186).
- **The LOD rungs** stay in an offline render (ADR-191). So the scene authors them with thresholds of 0, a ladder that
  ends at LOD0, and only `rift-live.json` sets the thresholds (`procedural/<body>/lod/distance1|2`, from `build.py`'s
  `LIVE_LOD`).
- **Check:** `rift.json` rendered offline at 70 s and 150 s, before against after, differs by at most 1/255. That is
  the engine's own run-to-run floor.
- **Why the rungs work now:** `build.py`'s note said rungs made feathery organisms blocky. That was v1's grid
  clustering. Mesh rungs now come from meshoptimizer (ADR-078), and at these distances the matched stills show no
  difference.

**3 and 4 are identity transforms.**

- **Full scene:** the baseline binary against the new one, on the same scene at 40, 96 and 150 s, differs by at most
  1/255 in 0.01% of pixels. Two runs of one binary differ the same way.
- **The ecosystem test** renders a scene with hosts behind, beyond, beside and straddling the frame, with and without
  the cull. The bytes are the same, and the dispatch goes from 6 hosts to 2.
- **The volume test** fails with a mask that stops 15% short of the march's end.

**`rift-live.json`'s target.** The brief asked whether the project should state `targetFps` 30 or let the panel choose.
The coordinator's owner decision (ADR-1360, merged) settled it: the Live panel's Target is authoritative, and the
project's `live.targetFps` is gone. These measurements pass `--target-fps 30`, as the panel's 30 would.

## 4. Final numbers

**Live, auto** (`F-live-*`, after):

| Start | Settled level | Frame median / P95 / P99 | GPU median / P95 | Deadline misses |
|---|---|---|---|---|
| 40 s | Low (0.71) | 33.28 / 35.91 / 39.47 ms | 24.97 / 27.85 ms | 1 of 900 (0.1%) |
| 96 s (the drop) | Low (0.71) | 33.28 / 36.36 / 40.05 ms | 25.17 / 28.90 ms | 1 of 901 (0.1%) |
| 150 s (the body) | **Medium (0.85)** | 33.28 / 35.62 / 39.87 ms | 25.17 / 29.36 ms | 0 of 901 |

**Live, pinned, 96 s:**

| Level | Frame median / P95 | GPU median / P95 | Deadline misses |
|---|---|---|---|
| **Medium** (0.85) | 33.31 / 35.27 ms | 26.08 / 30.34 ms | **0 of 601** |
| High (1.00) | 33.67 / 38.72 ms | 31.46 / 37.36 ms | 15 of 581 (2.6%) |

**Headless, pinned High, 96 s:** GPU **24.97 / 24.90 ms** (P95 28 ms), down from 50.3 / 49.7 ms.

- **Scene pass:** 17.4 ms, down from 41.8.
- **Volume march:** 3.7 ms.

**What remains** (after, High, headless; attribution `data/ab10-*`):

- **Bodies:** about 5 ms.
- **Ecosystem:** about 5 ms. This is real splatting now, not discarded threads.
- **Crown lights:** about 4 ms, of which 2.6 is the volume's clustered local in-scatter.
- **Mats:** 0.9 ms.
- **Terrain:** about 1 ms net. Its scene-pass share moves to what it would otherwise hide.
- **Particles:** nothing.

## 5. Stills: before and after, at matched times

Files are in `~/Desktop/av-gen-review/48-rift-live-30fps/stills/`.

- **The renders:** `before-t<T>.png` and `after-t<T>.png` were rendered with `--tier realtime --render-limits live`
  at 1920x1080. Before is the baseline binary on the original scene; after is the new binary on the new scene.
- **The comparisons:** `compare-t<T>.png` shows both, the difference x6, and the most-changed region at 1:1. Times are
  40, 70, 96, 150 and 185 s.

| t | mean abs diff | pixels > 8/255 | pixels > 24/255 |
|---|---|---|---|
| 40 s | 0.34 | 0.77% | 0.21% |
| 70 s | 1.84 | 3.71% | 1.72% |
| 96 s | 0.73 | 1.55% | 0.39% |
| 150 s | 0.37 | 0.58% | 0.11% |
| 185 s | 1.31 | 2.86% | 1.35% |

**What the differences are:**

- **Emitter twinkle** at occlusion edges, where a body that has stepped down a rung hides a polyp a pixel differently.
- **The sea-pen meadow at 30-80 m:** its glow is slightly less occluded now that the dark bodies there are gone.
- **The crinoid crowns:** `crowns-t70-before-after-2x.png` crops them at 2x and cannot be told apart.

**The two pictures the audience actually sees:**

- **Before:** the Emergency picture at half resolution, at 25 fps with judder.
- **After:** Low/Medium at 0.71-0.85, at a steady 30 fps.

## 6. Remaining risks

1. **High is not held live, and the reason is the GPU clock, not the scene.** *(2026-10-15: the controller half is
   fixed by [ADR-1362](../../decisions/ADR-1362-paced-live-quality-is-decided-by-missed-frames.md). Capped, the span
   reads the clock governor, not the level, so paced decisions are taken on missed frames. See
   `docs/development/live-controller-30fps.md`.)*
   - **The clock effect:** under the 30 fps frame cap, the GPU idles part of every frame and Apple silicon lowers its
     clock. The measured GPU time inflates with it. Pinned Low, 96 s (`data/D2-*`):

     | Mode | GPU median |
     |---|---|
     | capped (live) | 21.5 ms |
     | uncapped (live) | 16.4 ms |
     | headless | 17.4 ms |

   - **Why it holds the level down:** the controller decides raises from that inflated number (ADR-1085: cost =
     min(median span, mean interval), raised at 90% of the budget). So it climbs slowly and sits a level lower than the
     hardware can hold. Pinned Medium holds with 0 misses, yet auto mostly picks Low.
   - **Why High still misses:** High itself is saturated, at 31.5 ms live against 25 ms headless. Part of that gap is
     the editor and the 5K display sharing the GPU, and part is sustained-load clocking on a laptop.
   - **Scope:** this affects every scene at the new 30 fps target, so it is a controller decision, not one for this
     scene. Two candidates are a short uncapped probe before a raise, and reading the cost from headless-equivalent
     clocks.
2. **Thermals.** This is a MacBook Pro.
   - **The symptom:** one 90-second continuous headless scan read 40-64 ms where a repeat read 31-38 ms. Everything
     else was equal, and the repeat matched the short runs.
   - **The rule applied:** A/Bs were interleaved, and single runs were not trusted.
   - **For a show:** a long show on battery or in a warm room will sit a level lower. The controller will handle it,
     at a level cost, not a frame-rate cost.
3. **The GPU is shared with other agents and the owner's editor.** Runs took the lock, but WindowServer, the 5K desktop
   and an idle Blender were present for all of them. The live overhead measured here includes them.
4. **The live P99 frame is about 40 ms.** That is one lost vsync in roughly 1,000 frames: a single repeated frame, not
   a stutter run.
5. **The sea-pen bodies at 30-80 m are gone live.** If an art pass makes them brighter (emissive bodies), revisit the
   30 m.

## 7. Suites

Both full suites were run on the merged branch (`72ba5d04`: `47afa203` plus the ADR and this report), reconfigured
and rebuilt after the merge, under the lock, one after the other.

- CPU: `tools/ci/run-suite.sh cpu-assets` -- **exit 0**, PASS: 4249 cases, 4229 passed, 0 failed, 19 skipped, 1
  expected failure (the known shouldfail), 19 m 19 s.
- GPU: `tools/ci/run-suite.sh gpu --bin build/release/tests` -- **exit 0**, PASS: 658 cases, 657 passed, 0 failed, 1
  skipped, 29 m 39 s.
- New cases: `[adr1344]` (one each in `test_ecosystem_gpu.cpp`, `test_volume_gpu.cpp` and `test_rift.cpp`).
