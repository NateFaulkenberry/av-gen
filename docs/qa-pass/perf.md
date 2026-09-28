# W1: performance and the Glowmere Valley 3 investigation

Agent 1's working notes. Kept current so a successor can resume. Worktree `../av-gen-qa-perf`, branch `qa/perf`,
based on `77ea4247` (+ `f417728b`, the QA docs).

## Resume here (checkpoint 2026-09-28 ~13:50, first W1 agent stopped for a Claude Code restart)

**State of the branch.** `qa/perf` = `f417728b` + `edf62e2d` (ADR-950, tools, diagnostics) + `97925c18`
(ADR-951, needs the owner's acknowledgement) + this checkpoint commit. Full CPU suite on the ADR-951 build under
gpu-lock: 3843 cases, 3823 passed, 19 skipped, 1 "failed as expected" (the shouldfail), rc=0
(`build/qa-runs/cpu-suite.log`). GPU suite (`avgen_render_tests`) NOT yet run on the branch: owed before merge.

**Local, uncommitted, keep in place** (all gitignored or `??`; never commit):
- `examples/world/_qa-gv3-r7b{,.scene}.json`: r7b from the Desktop's `unbundled-copies/`, scene reference renamed.
  (W3 has since installed r7b as `examples/world/glowmere-valley-3.json` on `qa/coord` `3b3d323c`; identical
  apart from the song path. Switch to it once qa/coord is merged into qa/perf.)
- `examples/world/_qa-b-*.json`: the hypothesis-B arms (`tools/make_gv3_state_arms.py`; `--clean` removes them).
- `build/qa-bins/`: frozen binaries. `avgen-base-77ea4247` (main, untouched), `avgen-base-startat` (main plus
  only `--start-at`), `avgen-fix950-diag` (= `edf62e2d`), `avgen-fix950-startat` (ADR-950 plus `--start-at`, no
  probe counters), `avgen-gate951` (= `97925c18`), `avgen-3e09f9e1`, `avgen-29e6918e`.
- `build/qa-runs/`: every raw result (`*.json`, `*.log`), the batch scripts (`baseline.sh`, `hypB.sh`, `hypC.sh`,
  `hypC2.sh`, `live.sh`), `contention.log`, and `wait-quiet.sh`.

**Bisect worktrees** (all `git worktree`s of this repository, assets linked from `../av-gen`; remove with
`git worktree remove` when done):
| worktree | commit | built? | for |
|---|---|---|---|
| `../av-gen-qa-bisect-q-834` | `29e6918e` (the agent/motion merge carrying ADR-834) | yes (binary also in `build/qa-bins/avgen-29e6918e`) | step 1 of C |
| `../av-gen-qa-bisect-q-pre834` | **now `0b623b88`** (ADR-893 merge: continuous path levels, blended waters). Its old `3e09f9e1` binary is saved as `build/qa-bins/avgen-3e09f9e1` | **NO: its `build/` holds the stale 3e09f9e1 build; rebuild** | step 2 of C: the sightline cost doubling |
| `../av-gen-qa-bisect-q-0916` | **now `22ce5c3d`** (the parent of the ADR-945 ecolight merge). Was `c2f61058`, which cannot load today's multicam file | **NO: rebuild** | step 2 of C: the +2.5-3 ms scene pass |
| `../av-gen-qa-bisect-q-945` | `ad5623d2` (ADR-945 merge) | **NO: configure and build** | step 2 of C |

Build each with `cmake -S <wt> -B <wt>/build/release -G Ninja -DCMAKE_BUILD_TYPE=Release
-DCPM_SOURCE_CACHE=/Users/natefaulkenberry/Documents/GitHub/av-gen/.cache/cpm && cmake --build
<wt>/build/release --target avgen` (about 15 min each; build them one after another, and never while a timing
batch is running). Then run `build/qa-runs/hypC2.sh` (5 builds, GV2 multicam t=2 and t=27, 720p, 3 reps).

**Checklist.**
- [x] 1. Baseline, taken with W3's CPU suite running (load recorded per run). **Owed:** one clean retake of the
  720p/360p table once no other `avgen*` process is running (`ps aux | grep avgen`), i.e. `build/qa-runs/baseline.sh`
  with the output files renamed. The coordinator wants that clean absolute table for the final report.
- [x] 2. Per-shot sweep, 73 shots, 640x360, 1 repeat x 3 arms. Optional: repeat at 1280x720 on gate951 only
  (`tools/perf_sweep.py shots <gv3> --reps 1 --size 1280x720 --arm gate951=build/qa-bins/avgen-gate951`), to rank
  the residual GPU-bound shots after the fix.
- [~] 3. Attribution: the sightline is established (sample profile, r = 0.975, kill switch, gate). **Next:** for
  the residual cost on gate951, compare s66 against s27 at 720p one kill switch at a time: `--disable
  shadows|ao|volume|post|water|transparency|particles|animation` via `--extra "--disable X"` on `perf_sweep.py
  points` (for example `perf_sweep.py points "s66=<gv3>@194.706" "s27=<gv3>@85.783" --reps 3 --size 1280x720
  --arm gate=build/qa-bins/avgen-gate951 --arm noshadow="build/qa-bins/avgen-gate951 --disable shadows" ...`).
  The data so far (section 1): the residual is the scene pass, 19.3 ms against 6.5 ms.
- [x] 4. Complexity report (static). Optional: fold runtime counters in with `--bench NAME=file`.
- [x] 5. Hypothesis B: 10 arms measured; verdict below. Nothing owed.
- [~] 6. Hypothesis C: step 1 (ADR-834 merge, +20 ms engine update) is found and fixed. Step 2 (`29e6918e` ->
  `77ea4247`: +17 ms engine update, +2.5-3 ms GPU scene pass at identical draws and triangles) is **owed**: build
  the three worktrees above, run `hypC2.sh`, and attribute. Expectation to test, not assume: the engine-update
  step is ADR-893 making `WorldMap::heightUncached` (`blendedLevel`, `closestOnPath`) more expensive, and so every
  sightline; the GPU step is ADR-945's ecology lights (fewer lights, each lighting a pool at twice the range).
  Whatever is beyond the spread must be explained; the GPU step may be an accepted look change rather than a
  defect.
- [~] 7. Editor: `build/qa-runs/live.sh` finished its 36 profile runs (the numbers are in section 7). The `--ui-ab
  idle:camera` navigation part was still running at the checkpoint (`build/qa-runs/live/nav-*.txt`; nav-base-r1
  and nav-gate951-r1 are complete). **Next:** finish or re-run the nav part, and investigate `ui.build`'s p90 of
  about 36 ms, which appears in every arm, idle or playing. See section 7.
- [x] 8. Fixes: ADR-950 (`edf62e2d`) and ADR-951 (`97925c18`). Owed: `avgen_render_tests` under gpu-lock on the
  branch.

**Measurement rules as practised here.** Use `tools/perf_sweep.py`: it runs gpu-lock, arms interleaved, the camera
label as state proof, the GPU-error count, and the load and other `avgen*` processes recorded per run. Report the
median of 3 with min-max. Headless is the offline loop; `--profile-cpu` (with `--start-at`) is the live editor.

## 4. Structural complexity: GV2 multicam against GV3 r7b (static)

`python3 tools/scene_complexity_report.py examples/world/glowmere-valley-2-multicam.json examples/world/_qa-gv3-r7b.json examples/world/glowmere-valley-3.json`

The world r7b renders is GV2 multicam's world, almost unchanged:

| | GV2 multicam | GV3 r7b | GV3 committed |
|---|---:|---:|---:|
| scene nodes | 77 | 81 (+scout, scout-beam, 2 fields) | 77 |
| scatter layers / max instances | 18 / 197,100 | 18 / 197,100 | 18 / 197,100 |
| shadow-casting scatter layers | 14 | 14 | 14 |
| mesh references / unique mesh assets | 40 / 29 | 41 / 29 | 40 / 29 |
| particle capacity | 54,656 | 87,424 (+32,768 scout-beam) | 54,656 |
| entities | 19 | 21 | 19 |
| light-rig lights | 3 | 3 | 3 |
| scene effects | 12 (11 groundPulse, 1 travelBeam) | 12 (same) | 12 |
| project effects | 18 (16 groundPulse) | 18 (15 groundPulse, aurora, glow, travelBeam) | 2 |
| routes (all enabled) | 40 | 81 | 11 |
| timeline tracks / keys | 8 / 1,179 | 128 / 1,244 (84 are `cameras/*`) | 71 / 1,126 |
| cameras / shots | 3 / 3 | 74 / 73 | 41 / 40 |
| parameters | 6,646 | 6,709 | 6,022 |
| staging actors / scenarios (project) | 0 / 0 | 2 / 5 | 0 / 0 |
| directingPlans | 0 | 2 (reactivity: 60 routes; ufo: 5 set pieces) | 0 |
| hidden or disabled items | 6 | 7 | 6 |
| duplicate routes / tracks / node names | 0 / 0 / 0 | 0 / 0 / 0 | 0 / 0 / 0 |
| dead references | 0 | 0 | 0 |

Node-level diff of r7b against GV2 multicam: 44 of 77 shared nodes differ, all in small authored values (scatter
motion mass/tip amplitude, a `drop-ring` emissive field on 10 mushroom caps, hero positions); the environment adds
`fogSky`, `ecologyLightRange`, `ecologyPool*`; wind gusts are stronger. No layer's density, instance cap, view
distance or shadow flag changed.

Static reading (to be tested by measurement, not concluded from): the r7b file does not carry more world than GV2
multicam. What it adds is direction (73 shots, 84 camera tracks), modulation (81 routes, 9 timeline sources), one
32k particle pool, staging and two directing plans. No duplicated, orphaned or dead state was found by the script.

## First probe: the headless frame is 94% main-thread CPU, and most of that is one function

`avgen --headless --project examples/world/_qa-gv3-r7b.json --frames 30 --size 640x360 --fps 30 --log info
--bench-json`, base binary (`77ea4247`), under gpu-lock, 2026-09-28 09:58. **Machine was under compile load from
two other agents (load average ~140)**, so the absolute ms are not baselines; the proportions are what this probe
is for. 0 GPU errors, the scene loaded, the camera log reads `camera: s01 Nocturne: ... (shot) at 0.00 s`.

| | ms (median of 18 steady frames) |
|---|---:|
| wall (whole headless iteration) | 421.6 |
| renderer CPU frame (`cpuFrameMs`, includes 27 ms `queueWait` on the GPU) | 37.5 |
| GPU frame (timestamp queries) | 24.6 (scene 14.9, fogsky 2.4, shadow 2.3, particles 1.1, cull 0.9) |
| `Engine::update` (`cpu(scene)` on the last frame) | 398.2 |

GPU per-pass timing exists (timestamp queries, `gpuPassMedianMs` in `--bench-json`). The engine update is not
split in `--bench-json`; the `probe2` stage counters printed by the headless loop are cumulative across frames
(never cleared in the headless path), so they were read as differences: `upd.other` grew ~340 ms/frame.

**`sample <pid> 10`** on a 150-frame run (base binary, same settings): of 7,437 main-thread samples inside
`Engine::update`, **7,436 are in `Composition::publishCinematicSignals`**, and of those **~7,300 are in
`world::heroSightline`**, almost all in `WorldMap::sample` -> `heightUncached` (`closestOnPath`, `blendedLevel`)
and `moisture`.

What that code does (`src/scene/composition.cpp`, ADR-834, merged 2026-09-25 in `29e6918e`): every frame, for
every entity whose centre is in frame, it runs `heroSightline` (9 rays, a ground sample every 2 m) to publish
`character.<name>.visibility` on the bus. Cost is linear in camera-to-subject distance and in the number of
in-shot characters, which is exactly the shape "close-ups fine, wides collapse" needs. ADR-834 says the signals
are routed into nothing in any shipped scene; `grep character. examples/` finds no consumer in GV2 or GV3.

**Pathological part:** `heroSightline`'s `surfaceAt` called `WorldMap::sample(p, 0.5)`, which computes height,
a normal (4 more height evaluations), slope, water surface, altitude and moisture (a closest-point search over
every water path), and used only height and water surface.

## 2. Per-shot sweep of GV3 r7b (73 shots), 640x360

`python3 tools/perf_sweep.py shots examples/world/_qa-gv3-r7b.json --reps 1 --size 640x360 --arm base=... --arm fix=... --arm nosight=...`
(raw: `build/qa-runs/sweep-r7b-640.json`, not committed). Headless, offline clock at 30 fps, 42 frames per run from a
start second inside the shot (12 warm-up frames discarded; 30 measured), one process per (shot, arm), arms
interleaved per shot. Arms:
- **base**: `77ea4247` unmodified;
- **fix**: ADR-950 (the sightline's ground query, below);
- **nosight**: the fix binary with `AVGEN_DIAG_NO_SIGHTLINE=1` (diagnostic: visibility published as 1 without
  marching; everything else identical).

State proof: every run's camera log names the shot it was meant to be (73/73 match), 0 GPU errors on all 219 runs,
7 shots are shorter than the 1.4 s window (28, 29, 53-57) and straddle into the next shot. **Contention:** another
agent's CPU suite (`avgen_tests`) ran during part of the sweep (max 1-minute load 25.7, recorded per run). One
repeat per arm: this sweep ranks shots whose costs differ by up to 20x; the attribution below uses repeats.

**Headline:**

| | base | fix (ADR-950) | nosight |
|---|---:|---:|---:|
| median over 73 shots, wall ms | 57.7 | 41.7 | 21.0 |
| shots under 10 FPS | 20 | 13 | 0 |
| shots under 30 FPS | 58 | 49 | 0 |
| worst shot, wall ms | 389 (shot 66) | 252 | 26.8 |
| worst GPU frame, any shot | | | 17.2 ms |

**Correlation:** across the 73 shots, base `Engine::update` ms against (sightlines per frame x mean metres per
sightline) has **r = 0.975**. The slope is 0.065 ms per sightline-metre (base) and 0.038 (fix).

**Worst (base):** 66 "The veil lit" 389 ms (18.2 characters in frame at a mean 286 m); 26 "The veil at the water's
edge" 341; 24 "The valley floor lit, low and wide" 309; 37 298; 59 267; 72 253; 43 243; 03 220; 23 219; 33 192.
**Best (base):** 17 "The cairn on the spur" 19 ms (0 characters in frame); 22 21; 16 22; 35 23; 41 24; 27 24.
The owner's "close-ups over 30 FPS, wides in single digits" is this: a shot's cost follows how many characters are
in frame and how far away they are, and that is what a wide has. Shot 66 is a mushroom close-up by label; it has 18
farm animals in frame 286 m behind the subject.

Full table (ms are the run's median wall frame; GPU is the nosight arm's GPU frame; counters from the nosight arm):

| shot | t (s) | base wall ms | base FPS | fix950 wall | nosight wall | GPU ms | sightlines/frame | mean dist m | draws | tris | vis inst | clustered lights | max load | label |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| 01 | 2.0 | 186.5 | 5.4 | 111.8 | 19.2 | 10.6 | 12.9 | 205 | 272 | 430304 | 1755 | 206 | 12 | s01 Nocturne: low through the ferns toward the elder |
| 02 | 8.8 | 82.3 | 12.1 | 58.0 | 24.1 | 15.2 | 5.6 | 128 | 242 | 447733 | 1376 | 142 | 12 | s02 The elder across the pool, from the west bank |
| 03 | 12.5 | 219.7 | 4.6 | 141.6 | 20.0 | 11.7 | 11.9 | 242 | 270 | 367135 | 1407 | 206 | 11 | s03 E1: the elder, and far beyond it a beam lights over a fi |
| 04 | 16.2 | 77.2 | 13.0 | 53.7 | 20.6 | 12.3 | 7.0 | 101 | 219 | 359965 | 937 | 77 | 8 | s04 The white horse grazing under the elder's cap |
| 05 | 19.8 | 115.5 | 8.7 | 66.7 | 20.6 | 11.7 | 8.0 | 218 | 273 | 476164 | 1852 | 206 | 7 | s05 The lantern from the hollow, ferns in front |
| 06 | 23.0 | 35.3 | 28.3 | 33.4 | 23.4 | 15.5 | 2.0 | 101 | 219 | 399192 | 1993 | 55 | 6 | s06 The bloom by the river, breathing |
| 07 | 24.9 | 43.9 | 22.8 | 24.1 | 17.6 | 8.3 | 2.0 | 391 | 208 | 202612 | 337 | 64 | 5 | s07 The elder's cap from the north, its rim against the star |
| 08 | 27.2 | 91.7 | 10.9 | 53.6 | 21.0 | 12.5 | 8.2 | 162 | 286 | 426666 | 1592 | 206 | 5 | s08 E2: a shape crosses the sky over the elder |
| 09 | 30.9 | 56.9 | 17.6 | 40.6 | 21.8 | 13.0 | 3.8 | 147 | 257 | 452961 | 1683 | 163 | 4 | s09 The return: settling toward the pool and the elder |
| 10 | 34.6 | 42.5 | 23.5 | 34.3 | 20.6 | 11.9 | 3.4 | 74 | 229 | 353547 | 1211 | 120 | 4 | s10 The first wave runs through the mushrooms toward us |
| 11 | 38.3 | 47.5 | 21.1 | 37.6 | 19.9 | 10.8 | 4.7 | 76 | 247 | 371957 | 1895 | 206 | 4 | s11 Rook investigates a flickering cluster |
| 12 | 42.0 | 83.7 | 11.9 | 59.3 | 24.1 | 15.5 | 6.0 | 121 | 256 | 418618 | 2066 | 206 | 7 | s12 The herd grazing on the east meadow |
| 13 | 45.2 | 61.8 | 16.2 | 41.4 | 20.9 | 11.7 | 2.4 | 246 | 223 | 321755 | 1797 | 121 | 7 | s13 The lantern, close, flaring on the clap |
| 14 | 48.0 | 128.2 | 7.8 | 83.3 | 19.5 | 11.1 | 10.3 | 169 | 263 | 432643 | 1790 | 175 | 6 | s14 A travel down the east bank, the elder across the water |
| 15 | 53.1 | 45.8 | 21.8 | 31.9 | 17.0 | 8.5 | 6.0 | 92 | 226 | 236103 | 756 | 80 | 7 | s15 Vane crosses the east meadow, the elder beyond |
| 16 | 56.8 | 22.1 | 45.2 | 22.1 | 21.7 | 13.3 | 0.0 | 0 | 222 | 384896 | 2455 | 33 | 7 | s16 The spire on the east bank |
| 17 | 60.5 | 18.6 | 53.6 | 18.6 | 18.7 | 10.2 | 0.0 | 0 | 184 | 252205 | 443 | 1 | 8 | s17 The cairn on the spur, its spores in the air |
| 18 | 64.2 | 108.5 | 9.2 | 58.2 | 19.5 | 10.5 | 8.0 | 221 | 264 | 384207 | 1589 | 206 | 8 | s18 E3: the crane rises to reveal a column of light up the v |
| 19 | 67.3 | 30.7 | 32.6 | 20.2 | 15.4 | 6.8 | 1.0 | 387 | 179 | 179045 | 86 | 93 | 7 | s19 E3: an animal lifted into the column, far up the valley |
| 20 | 69.2 | 83.4 | 12.0 | 44.7 | 19.1 | 9.8 | 5.9 | 221 | 241 | 305904 | 1633 | 206 | 7 | s20 Tide, and the column far up the valley beyond |
| 21 | 71.0 | 28.0 | 35.7 | 18.3 | 15.0 | 7.0 | 1.0 | 331 | 174 | 160935 | 107 | 55 | 6 | s21 E3: the animal fades into the scout |
| 22 | 72.9 | 21.3 | 46.9 | 21.8 | 23.4 | 13.8 | 0.0 | 0 | 186 | 241661 | 714 | 1 | 6 | s22 The ridge mushroom high in the north-east |
| 23 | 75.2 | 218.9 | 4.6 | 125.7 | 18.1 | 9.7 | 11.9 | 282 | 268 | 310081 | 536 | 206 | 6 | s23 The first grand wide: the lit valley, far and low |
| 24 | 78.9 | 308.6 | 3.2 | 190.1 | 25.6 | 16.6 | 17.0 | 247 | 306 | 510497 | 2598 | 206 | 5 | s24 The valley floor lit, low and wide |
| 25 | 82.1 | 38.0 | 26.3 | 23.8 | 19.5 | 10.4 | 1.1 | 519 | 226 | 332540 | 755 | 94 | 10 | s25 Spores falling from the bloom |
| 26 | 83.9 | 341.0 | 2.9 | 215.7 | 19.5 | 11.5 | 15.0 | 306 | 309 | 456353 | 1841 | 206 | 10 | s26 The veil at the water's edge |
| 27 | 85.8 | 24.1 | 41.5 | 18.3 | 16.5 | 6.5 | 1.0 | 355 | 196 | 124768 | 20 | 38 | 11 | s27 Under the elder: the gold gills |
| 28 | 87.3 | 25.5 | 39.2 | 25.2 | 26.8 | 17.0 | 0.0 | 0 | 186 | 246969 | 484 | 1 | 17 | s28 The scree on the west slope, lit |
| 29 | 88.2 | 29.7 | 33.7 | 41.1 | 26.8 | 14.9 | 2.9 | 105 | 233 | 259498 | 269 | 83 | 19 | s29 The ember mushroom on the east terrace, lit |
| 30 | 90.0 | 85.7 | 11.7 | 61.0 | 23.9 | 14.2 | 8.0 | 105 | 246 | 416562 | 1394 | 108 | 17 | s30 An orbit of the elder |
| 31 | 93.7 | 92.1 | 10.9 | 65.8 | 25.0 | 15.4 | 8.0 | 113 | 264 | 432233 | 1951 | 206 | 16 | s31 Cows on the west meadow, the lantern beyond |
| 32 | 96.9 | 110.0 | 9.1 | 71.8 | 23.5 | 14.9 | 6.8 | 195 | 244 | 433551 | 1693 | 100 | 15 | s32 The bloom from across the river |
| 33 | 99.7 | 192.2 | 5.2 | 119.4 | 19.7 | 10.7 | 13.1 | 194 | 291 | 436355 | 1459 | 206 | 15 | s33 E4: the scout settles over the meadow beyond the elder |
| 34 | 104.8 | 27.8 | 36.0 | 22.6 | 20.7 | 11.4 | 1.6 | 228 | 208 | 278314 | 544 | 22 | 12 | s34 E4: a cow lifted into the column |
| 35 | 108.5 | 23.4 | 42.7 | 19.0 | 17.7 | 8.7 | 1.0 | 223 | 195 | 206090 | 278 | 1 | 13 | s35 E4: Sage watches the cow rise |
| 36 | 112.2 | 147.4 | 6.8 | 99.5 | 22.7 | 14.7 | 13.0 | 145 | 269 | 443489 | 2202 | 206 | 13 | s36 The herd on the east meadow |
| 37 | 115.8 | 297.7 | 3.4 | 195.5 | 21.2 | 12.8 | 17.0 | 224 | 324 | 559273 | 2645 | 206 | 15 | s37 The umbra, listening |
| 38 | 119.3 | 70.2 | 14.2 | 50.1 | 23.9 | 14.5 | 4.7 | 119 | 243 | 471878 | 1629 | 133 | 15 | s38 The elder's spores drifting in its gold |
| 39 | 122.5 | 48.8 | 20.5 | 39.0 | 25.2 | 16.4 | 5.0 | 68 | 257 | 338862 | 1617 | 206 | 13 | s39 Sage in the grove, among the fireflies |
| 40 | 126.9 | 57.7 | 17.3 | 42.4 | 19.9 | 10.6 | 5.0 | 99 | 259 | 304812 | 1353 | 206 | 10 | s40 The lantern from the north |
| 41 | 130.6 | 24.1 | 41.5 | 19.1 | 16.9 | 8.4 | 1.0 | 134 | 195 | 170875 | 124 | 176 | 9 | s41 The elder through a long lens from the north, from above |
| 42 | 134.3 | 83.0 | 12.1 | 51.8 | 20.2 | 11.4 | 7.1 | 153 | 258 | 385373 | 1793 | 206 | 8 | s42 Behind Vane, the aurora beyond |
| 43 | 138.0 | 242.9 | 4.1 | 147.8 | 19.1 | 9.8 | 17.1 | 207 | 316 | 504379 | 1650 | 206 | 8 | s43 The aurora over the valley |
| 44 | 141.7 | 116.6 | 8.6 | 71.6 | 18.4 | 10.0 | 11.0 | 154 | 262 | 400498 | 1250 | 137 | 12 | s44 Across the valley to the west slope, under the aurora |
| 45 | 144.9 | 50.1 | 20.0 | 38.8 | 20.4 | 12.2 | 3.0 | 146 | 233 | 343725 | 1552 | 206 | 12 | s45 The spire under the aurora, from the east |
| 46 | 146.7 | 53.2 | 18.8 | 34.1 | 18.0 | 9.3 | 3.0 | 202 | 227 | 305891 | 1418 | 153 | 10 | s46 Tide looks up |
| 47 | 150.0 | 54.2 | 18.5 | 28.7 | 16.4 | 7.6 | 3.0 | 299 | 218 | 213931 | 485 | 206 | 12 | s47 E5: locked off, the saucer comes over the rim |
| 48 | 157.4 | 48.2 | 20.7 | 27.1 | 18.1 | 9.4 | 3.0 | 254 | 231 | 240037 | 1194 | 206 | 13 | s48 E5: the saucer comes on beyond the elder's cap |
| 49 | 163.8 | 46.4 | 21.5 | 26.2 | 17.3 | 8.5 | 3.0 | 247 | 248 | 318618 | 1746 | 206 | 12 | s49 From under the elder: the saucer over its rim |
| 50 | 167.5 | 91.8 | 10.9 | 62.1 | 24.3 | 14.7 | 7.0 | 142 | 283 | 530020 | 2040 | 206 | 10 | s50 Ember on the west bank, the saucer settling over the eld |
| 51 | 170.7 | 42.0 | 23.8 | 24.0 | 20.5 | 10.4 | 2.0 | 338 | 235 | 259479 | 1274 | 199 | 13 | s51 E5: the beam lights |
| 52 | 172.6 | 42.2 | 23.7 | 26.1 | 16.8 | 8.7 | 2.0 | 320 | 236 | 271066 | 1414 | 184 | 11 | s52 E5: up the beam from beside the horse |
| 53 | 174.1 | 45.3 | 22.1 | 36.1 | 21.1 | 12.1 | 4.3 | 112 | 224 | 319354 | 932 | 145 | 12 | s53 Vane sees it |
| 54 | 175.0 | 42.6 | 23.5 | 35.7 | 23.3 | 14.4 | 2.6 | 92 | 210 | 279460 | 809 | 59 | 12 | s54 E5: the horse rises, glowing, side on |
| 55 | 175.9 | 41.0 | 24.4 | 17.3 | 11.6 | 2.8 | 2.0 | 173 | 230 | 415659 | 1376 | 86 | 12 | s55 E5: the elder beside the beam |
| 56 | 176.8 | 45.3 | 22.1 | 22.0 | 17.1 | 8.5 | 4.6 | 205 | 258 | 166670 | 4 | 183 | 11 | s56 E5: the horse under the saucer |
| 57 | 177.3 | 150.6 | 6.6 | 105.1 | 25.3 | 16.5 | 7.0 | 179 | 281 | 525478 | 1791 | 206 | 26 | s57 E5: the horse fades into the saucer |
| 58 | 178.6 | 147.7 | 6.8 | 102.1 | 24.5 | 15.7 | 9.0 | 163 | 281 | 508202 | 1750 | 206 | 21 | s58 The drop: the valley lights up, wide and fast |
| 59 | 181.8 | 266.6 | 3.8 | 168.4 | 25.9 | 15.1 | 17.1 | 205 | 313 | 527252 | 2272 | 206 | 18 | s59 E5: the saucer leaves over the north rim, the elder belo |
| 60 | 183.6 | 92.1 | 10.9 | 52.3 | 23.2 | 14.7 | 5.4 | 252 | 245 | 457540 | 2695 | 84 | 14 | s60 The umbra lit, its spores in the air |
| 61 | 185.5 | 26.0 | 38.5 | 21.9 | 24.4 | 16.6 | 0.0 | 0 | 198 | 294356 | 620 | 1 | 13 | s61 The ember mushroom on the east terrace, lit |
| 62 | 187.3 | 26.1 | 38.3 | 24.4 | 24.6 | 16.1 | 0.0 | 0 | 195 | 318450 | 598 | 1 | 12 | s62 The cairn lit |
| 63 | 189.2 | 57.4 | 17.4 | 41.7 | 24.7 | 15.7 | 5.3 | 126 | 231 | 373191 | 1964 | 81 | 11 | s63 Ember looks up |
| 64 | 191.0 | 67.8 | 14.7 | 42.2 | 24.7 | 15.9 | 2.9 | 262 | 246 | 473736 | 2552 | 71 | 11 | s64 The spire lit, from the north-west |
| 65 | 192.9 | 93.3 | 10.7 | 76.8 | 23.9 | 14.8 | 6.4 | 142 | 257 | 397058 | 2318 | 206 | 11 | s65 The horses in the rebuilt light |
| 66 | 194.7 | 389.4 | 2.6 | 251.7 | 23.0 | 15.0 | 18.2 | 286 | 326 | 525410 | 2293 | 206 | 10 | s66 The veil lit |
| 67 | 196.6 | 23.4 | 42.8 | 25.1 | 23.5 | 14.2 | 0.0 | 0 | 178 | 261785 | 453 | 1 | 10 | s67 The scree lit on the west slope |
| 68 | 198.4 | 94.5 | 10.6 | 73.1 | 24.9 | 16.6 | 8.0 | 110 | 258 | 455186 | 1815 | 134 | 9 | s68 A wave of light through the mushrooms, from the east |
| 69 | 200.8 | 124.7 | 8.0 | 71.6 | 25.8 | 16.4 | 8.7 | 191 | 279 | 431823 | 1707 | 206 | 8 | s69 Crane up past the elder's cap and out over the valley |
| 70 | 204.5 | 25.9 | 38.7 | 26.4 | 24.8 | 15.8 | 1.6 | 39 | 243 | 428049 | 1856 | 183 | 8 | s70 Tide walks the north end toward the lit spire |
| 71 | 209.1 | 75.2 | 13.3 | 48.3 | 25.7 | 17.2 | 6.5 | 125 | 270 | 407640 | 1879 | 206 | 8 | s71 The valley rebuilt, from high on the north-east slope |
| 72 | 216.5 | 253.0 | 4.0 | 160.5 | 21.7 | 13.4 | 13.5 | 276 | 290 | 349898 | 820 | 206 | 8 | s72 The last wide: the elder and the valley rebuilt |
| 73 | 222.9 | 50.2 | 19.9 | 36.7 | 24.8 | 15.2 | 2.9 | 147 | 237 | 356962 | 998 | 133 | 9 | s73 The elder alone, then black |

## 1. Baseline: GV3 r7b, GV3 committed, GV2 multicam, grove (1280x720 and 640x360)

### Clean retake (the absolute table for the final report)

`build/qa-runs/baseline.sh`, 2026-09-28 14:05-14:39, unchanged apart from the output names (raw:
`build/qa-runs/baseline-{1280x720,640x360}.json`; the contended first take is kept as
`baseline-contended-*`). **No other `avgen*` process ran during any of the 216 runs** (the "others" column), max
1-minute load 7.5 (the astronaut agent's Blender was idle). 0 GPU errors, rc 0 and 0 `[error]` lines on every
run; every run's camera log names the intended shot (GV2 multicam's t=9 and grove log none, as before). Columns:
wall = median of the three runs' p50 (min-max); p95/p99 are the median run's; GPU, renderer CPU (includes
`queueWait`), engine update (`Engine::update`, last frame) and the scene pass are medians over the three runs;
counters are exact.

**1280x720**

| point | arm | wall p50 (min-max) | p95 | p99 | FPS | GPU p50 | renderer CPU | engine update | scene pass | draws | tris | lights | max load | others | gpuErr |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| r7b-s66-wide | base | 376.2 (359.9-376.5) | 385.1 | 386.1 | 2.7 | 28.9 | 34.4 | 347.7 | 20.8 | 326 | 613593 | 206 | 4.5 | - | 0 |
| r7b-s66-wide | fix950 | 235.4 (232.9-242.1) | 246.5 | 246.8 | 4.2 | 29.9 | 35.2 | 202.0 | 20.9 | 326 | 613593 | 206 | 6.9 | - | 0 |
| r7b-s66-wide | gate951 | 32.2 (31.9-32.5) | 35.0 | 35.7 | 31.1 | 23.5 | 28.7 | 3.1 | 18.6 | 326 | 613593 | 206 | 5.8 | - | 0 |
| r7b-s24-wide | base | 296.5 (289.8-305.6) | 315.3 | 316.7 | 3.4 | 27.7 | 33.0 | 261.0 | 19.3 | 306 | 584653 | 206 | 5.5 | - | 0 |
| r7b-s24-wide | fix950 | 185.5 (183.1-190.5) | 198.4 | 202.7 | 5.4 | 28.6 | 33.7 | 148.9 | 20.1 | 306 | 584653 | 206 | 5.3 | - | 0 |
| r7b-s24-wide | gate951 | 30.8 (30.7-31.8) | 37.0 | 42.7 | 32.5 | 22.8 | 27.5 | 2.9 | 17.9 | 306 | 584653 | 206 | 5.0 | - | 0 |
| r7b-s26 | base | 334.0 (333.9-334.4) | 364.4 | 371.6 | 3.0 | 27.7 | 33.2 | 331.4 | 19.1 | 302 | 559622 | 206 | 6.2 | - | 0 |
| r7b-s26 | fix950 | 214.5 (205.4-214.6) | 233.7 | 238.1 | 4.7 | 27.2 | 32.5 | 195.7 | 19.1 | 302 | 559622 | 206 | 5.7 | - | 0 |
| r7b-s26 | gate951 | 31.7 (30.3-31.8) | 34.0 | 36.0 | 31.6 | 22.2 | 27.8 | 3.5 | 17.4 | 302 | 559622 | 206 | 7.5 | - | 0 |
| r7b-s27-close | base | 23.8 (23.3-24.5) | 41.0 | 47.6 | 42.0 | 7.7 | 12.5 | 9.3 | 3.3 | 196 | 135415 | 38 | 7.1 | - | 0 |
| r7b-s27-close | fix950 | 18.9 (18.0-19.1) | 21.8 | 22.4 | 52.9 | 7.8 | 12.4 | 5.5 | 3.4 | 196 | 135415 | 38 | 6.4 | - | 0 |
| r7b-s27-close | gate951 | 15.8 (15.7-16.3) | 16.5 | 17.2 | 63.3 | 7.7 | 12.3 | 3.2 | 3.4 | 196 | 135415 | 38 | 6.4 | - | 0 |
| r7b-s17-empty | base | 26.5 (26.4-26.6) | 27.9 | 28.9 | 37.7 | 18.5 | 23.1 | 3.2 | 14.4 | 171 | 328615 | 1 | 5.6 | - | 0 |
| r7b-s17-empty | fix950 | 26.8 (26.7-26.8) | 31.5 | 33.5 | 37.4 | 18.7 | 23.1 | 3.3 | 14.4 | 171 | 328615 | 1 | 5.4 | - | 0 |
| r7b-s17-empty | gate951 | 26.5 (25.8-27.2) | 27.0 | 28.2 | 37.7 | 18.5 | 23.1 | 3.3 | 14.5 | 171 | 328615 | 1 | 5.0 | - | 0 |
| gv3c-s14-wide | base | 233.8 (218.6-242.7) | 246.3 | 246.4 | 4.3 | 21.7 | 27.2 | 203.0 | 14.7 | 255 | 382525 | 95 | 4.7 | - | 0 |
| gv3c-s14-wide | fix950 | 137.9 (134.3-141.8) | 143.9 | 145.3 | 7.3 | 19.6 | 24.4 | 112.3 | 14.9 | 255 | 382525 | 95 | 4.0 | - | 0 |
| gv3c-s14-wide | gate951 | 23.5 (23.0-24.5) | 25.8 | 26.9 | 42.5 | 16.4 | 21.1 | 2.2 | 12.4 | 255 | 382525 | 95 | 3.8 | - | 0 |
| gv3c-s16-close | base | 14.8 (14.8-15.0) | 20.5 | 20.8 | 67.5 | 7.5 | 12.2 | 7.8 | 3.6 | 207 | 134202 | 87 | 4.1 | - | 0 |
| gv3c-s16-close | fix950 | 15.1 (14.6-15.4) | 17.3 | 17.7 | 66.4 | 7.5 | 12.3 | 4.1 | 3.5 | 207 | 134202 | 87 | 4.0 | - | 0 |
| gv3c-s16-close | gate951 | 14.8 (14.8-14.9) | 16.2 | 18.4 | 67.4 | 7.4 | 12.1 | 2.6 | 3.5 | 207 | 134202 | 87 | 4.0 | - | 0 |
| gv3c-s01 | base | 205.2 (193.1-209.4) | 215.1 | 215.2 | 4.9 | 21.9 | 26.9 | 174.1 | 14.5 | 273 | 506089 | 160 | 5.0 | - | 0 |
| gv3c-s01 | fix950 | 134.0 (128.4-134.0) | 141.8 | 146.7 | 7.5 | 22.0 | 26.9 | 104.7 | 14.5 | 273 | 506089 | 160 | 4.5 | - | 0 |
| gv3c-s01 | gate951 | 25.0 (24.6-25.5) | 27.7 | 28.4 | 40.0 | 17.2 | 22.1 | 2.8 | 12.6 | 273 | 506089 | 160 | 4.6 | - | 0 |
| gv2mc-wide | base | 64.2 (61.8-66.2) | 71.4 | 71.8 | 15.6 | 19.2 | 24.3 | 39.9 | 14.9 | 258 | 447125 | 115 | 4.8 | - | 0 |
| gv2mc-wide | fix950 | 38.4 (37.5-38.4) | 39.6 | 42.7 | 26.1 | 19.1 | 23.8 | 14.8 | 14.9 | 258 | 447125 | 115 | 4.5 | - | 0 |
| gv2mc-wide | gate951 | 27.7 (27.1-28.2) | 29.1 | 30.1 | 36.2 | 19.1 | 24.0 | 3.4 | 14.9 | 258 | 447125 | 115 | 4.0 | - | 0 |
| gv2mc-hero | base | 50.3 (49.0-50.9) | 55.1 | 76.5 | 19.9 | 22.4 | 27.5 | 22.4 | 17.6 | 189 | 276851 | 171 | 4.0 | - | 0 |
| gv2mc-hero | fix950 | 37.4 (37.1-37.9) | 39.1 | 40.5 | 26.8 | 22.3 | 27.0 | 10.1 | 17.4 | 189 | 276851 | 171 | 4.1 | - | 0 |
| gv2mc-hero | gate951 | 31.0 (30.4-31.3) | 35.8 | 36.5 | 32.3 | 22.5 | 27.4 | 3.0 | 17.6 | 189 | 276851 | 171 | 4.0 | - | 0 |
| gv2mc-return | base | 64.1 (62.9-65.1) | 72.4 | 72.7 | 15.6 | 19.5 | 24.3 | 39.8 | 15.3 | 254 | 434840 | 88 | 4.0 | - | 0 |
| gv2mc-return | fix950 | 38.5 (38.3-38.5) | 41.3 | 42.4 | 26.0 | 19.3 | 24.2 | 14.3 | 15.2 | 254 | 434840 | 88 | 3.7 | - | 0 |
| gv2mc-return | gate951 | 27.7 (27.7-28.2) | 30.2 | 31.0 | 36.1 | 19.2 | 24.0 | 3.4 | 15.1 | 254 | 434840 | 88 | 4.3 | - | 0 |
| grove | base | 13.2 (13.2-13.5) | 13.9 | 13.9 | 75.7 | 10.9 | 12.8 | 0.4 | 4.9 | 43 | 29408 | 0 | 4.4 | - | 0 |
| grove | fix950 | 13.5 (13.2-14.2) | 14.3 | 14.5 | 73.8 | 11.2 | 13.1 | 0.4 | 4.9 | 43 | 29408 | 0 | 4.4 | - | 0 |
| grove | gate951 | 13.2 (13.1-13.2) | 14.5 | 15.0 | 75.9 | 10.9 | 12.8 | 0.4 | 4.9 | 43 | 29408 | 0 | 4.5 | - | 0 |

**640x360**

| point | arm | wall p50 (min-max) | p95 | p99 | FPS | GPU p50 | renderer CPU | engine update | scene pass | draws | tris | lights | max load | others | gpuErr |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| r7b-s66-wide | base | 365.9 (362.7-366.1) | 369.1 | 369.2 | 2.7 | 20.9 | 26.2 | 351.1 | 14.2 | 326 | 525410 | 206 | 3.7 | - | 0 |
| r7b-s66-wide | fix950 | 229.1 (225.4-230.9) | 234.2 | 235.2 | 4.4 | 20.5 | 25.6 | 205.1 | 13.5 | 326 | 525410 | 206 | 3.9 | - | 0 |
| r7b-s66-wide | gate951 | 23.1 (23.0-23.2) | 27.9 | 28.1 | 43.3 | 15.0 | 19.7 | 2.9 | 11.7 | 326 | 525410 | 206 | 3.8 | - | 0 |
| r7b-s24-wide | base | 291.0 (289.6-293.4) | 297.7 | 298.3 | 3.4 | 20.1 | 25.3 | 262.7 | 13.2 | 306 | 510497 | 206 | 3.7 | - | 0 |
| r7b-s24-wide | fix950 | 177.6 (176.2-181.9) | 183.2 | 184.1 | 5.6 | 18.7 | 24.0 | 152.6 | 13.1 | 306 | 510497 | 206 | 3.1 | - | 0 |
| r7b-s24-wide | gate951 | 23.1 (22.7-23.3) | 24.6 | 25.4 | 43.3 | 15.1 | 19.9 | 2.8 | 11.8 | 306 | 510497 | 206 | 2.9 | - | 0 |
| r7b-s26 | base | 318.6 (314.8-319.8) | 359.1 | 363.2 | 3.1 | 16.3 | 21.8 | 337.3 | 9.8 | 309 | 456353 | 206 | 2.7 | - | 0 |
| r7b-s26 | fix950 | 200.6 (200.1-201.0) | 217.9 | 223.4 | 5.0 | 16.2 | 21.2 | 198.0 | 10.0 | 309 | 456353 | 206 | 2.7 | - | 0 |
| r7b-s26 | gate951 | 19.1 (18.6-19.2) | 20.5 | 21.0 | 52.4 | 11.1 | 15.9 | 2.9 | 8.0 | 309 | 456353 | 206 | 3.4 | - | 0 |
| r7b-s27-close | base | 22.7 (22.1-23.6) | 35.5 | 36.4 | 44.1 | 6.0 | 10.6 | 9.3 | 2.6 | 196 | 124768 | 38 | 3.3 | - | 0 |
| r7b-s27-close | fix950 | 16.0 (15.7-16.0) | 21.7 | 22.6 | 62.6 | 5.2 | 9.9 | 5.1 | 2.4 | 196 | 124768 | 38 | 3.2 | - | 0 |
| r7b-s27-close | gate951 | 13.4 (13.3-13.4) | 14.9 | 15.1 | 74.6 | 5.2 | 9.9 | 3.4 | 2.3 | 196 | 124768 | 38 | 3.0 | - | 0 |
| r7b-s17-empty | base | 18.2 (18.1-18.4) | 19.4 | 19.7 | 54.9 | 10.2 | 14.8 | 3.3 | 7.5 | 184 | 252205 | 1 | 2.9 | - | 0 |
| r7b-s17-empty | fix950 | 18.3 (18.2-18.5) | 19.2 | 19.6 | 54.6 | 10.4 | 14.9 | 3.3 | 7.5 | 184 | 252205 | 1 | 2.8 | - | 0 |
| r7b-s17-empty | gate951 | 18.3 (18.3-18.4) | 19.5 | 19.8 | 54.7 | 10.3 | 14.9 | 3.3 | 7.5 | 184 | 252205 | 1 | 2.9 | - | 0 |
| gv3c-s14-wide | base | 224.6 (222.9-224.9) | 227.6 | 227.8 | 4.5 | 13.7 | 18.8 | 205.0 | 9.0 | 265 | 312549 | 95 | 3.1 | - | 0 |
| gv3c-s14-wide | fix950 | 132.9 (130.4-133.3) | 134.3 | 140.7 | 7.5 | 14.5 | 19.6 | 113.8 | 9.8 | 265 | 312549 | 95 | 3.0 | - | 0 |
| gv3c-s14-wide | gate951 | 15.5 (15.3-15.5) | 16.7 | 16.8 | 64.5 | 8.7 | 13.3 | 2.0 | 6.6 | 265 | 312549 | 95 | 3.2 | - | 0 |
| gv3c-s16-close | base | 12.4 (12.4-12.4) | 17.3 | 17.7 | 80.7 | 5.3 | 9.8 | 7.9 | 3.0 | 207 | 125272 | 87 | 3.8 | - | 0 |
| gv3c-s16-close | fix950 | 12.4 (12.4-12.7) | 13.8 | 14.5 | 80.5 | 5.1 | 9.8 | 4.3 | 2.7 | 207 | 125272 | 87 | 3.4 | - | 0 |
| gv3c-s16-close | gate951 | 12.3 (12.3-12.4) | 13.0 | 14.2 | 81.0 | 5.2 | 9.8 | 2.6 | 2.9 | 207 | 125272 | 87 | 3.3 | - | 0 |
| gv3c-s01 | base | 198.0 (197.6-199.6) | 206.5 | 208.8 | 5.1 | 14.7 | 19.9 | 174.1 | 9.6 | 275 | 438027 | 160 | 3.0 | - | 0 |
| gv3c-s01 | fix950 | 125.4 (125.3-125.4) | 134.2 | 134.9 | 8.0 | 15.2 | 20.2 | 105.5 | 9.6 | 275 | 438027 | 160 | 2.8 | - | 0 |
| gv3c-s01 | gate951 | 17.5 (17.0-17.9) | 19.1 | 19.1 | 57.1 | 10.2 | 15.0 | 2.3 | 7.5 | 275 | 438027 | 160 | 4.1 | - | 0 |
| gv2mc-wide | base | 56.1 (55.7-56.1) | 62.5 | 63.1 | 17.8 | 10.9 | 16.1 | 39.8 | 8.8 | 265 | 371713 | 115 | 3.6 | - | 0 |
| gv2mc-wide | fix950 | 29.7 (29.1-30.0) | 30.5 | 31.6 | 33.7 | 10.7 | 15.4 | 14.0 | 8.5 | 265 | 371713 | 115 | 3.5 | - | 0 |
| gv2mc-wide | gate951 | 18.9 (18.5-19.0) | 20.3 | 21.9 | 52.8 | 10.7 | 15.5 | 3.2 | 8.5 | 265 | 371713 | 115 | 3.3 | - | 0 |
| gv2mc-hero | base | 37.4 (37.3-37.4) | 39.6 | 39.6 | 26.8 | 10.5 | 15.2 | 21.7 | 7.9 | 203 | 206189 | 171 | 3.1 | - | 0 |
| gv2mc-hero | fix950 | 25.3 (25.3-25.5) | 27.3 | 27.7 | 39.5 | 10.5 | 15.1 | 9.8 | 7.8 | 203 | 206189 | 171 | 2.9 | - | 0 |
| gv2mc-hero | gate951 | 18.5 (18.3-19.3) | 20.3 | 20.5 | 54.1 | 10.6 | 15.1 | 3.2 | 8.0 | 203 | 206189 | 171 | 2.7 | - | 0 |
| gv2mc-return | base | 55.8 (55.2-56.3) | 63.2 | 63.2 | 17.9 | 11.3 | 16.7 | 39.0 | 9.2 | 263 | 363812 | 88 | 3.9 | - | 0 |
| gv2mc-return | fix950 | 30.0 (30.0-30.4) | 30.9 | 31.3 | 33.3 | 11.3 | 16.2 | 13.8 | 9.0 | 263 | 363812 | 88 | 3.4 | - | 0 |
| gv2mc-return | gate951 | 19.1 (19.0-19.3) | 20.3 | 20.8 | 52.4 | 11.1 | 15.7 | 3.1 | 8.8 | 263 | 363812 | 88 | 4.4 | - | 0 |
| grove | base | 7.5 (7.4-7.8) | 8.0 | 8.0 | 133.1 | 5.4 | 7.1 | 0.4 | 2.4 | 43 | 29408 | 0 | 4.0 | - | 0 |
| grove | fix950 | 7.9 (7.5-8.4) | 9.5 | 9.7 | 127.4 | 5.5 | 7.5 | 0.4 | 2.4 | 43 | 29408 | 0 | 4.0 | - | 0 |
| grove | gate951 | 7.5 (7.5-7.8) | 9.1 | 9.1 | 134.0 | 5.3 | 7.1 | 0.4 | 2.4 | 43 | 29408 | 0 | 4.0 | - | 0 |

**Reading (clean).** The contended take's shape holds and every conclusion drawn from it stands; the absolute
numbers are lower where the frame was CPU-bound.
- Base (`77ea4247`): GV3 r7b's wides are **2.7-3.4 FPS at both sizes** (engine update 261-351 ms of a 296-376 ms
  frame). The committed GV3's wides are 4.3-5.1 FPS. GV2 multicam's wides are 15.6-17.9 FPS.
- gate951 (ADR-950 + 951): the r7b wides are **31-33 FPS at 720p and 43-52 FPS at 360p**; the committed GV3's
  wides 40-43 FPS at 720p and 57-65 at 360p; GV2 multicam 32-36 / 52-54. Close-ups 63-67 FPS at 720p.
- At 720p the fixed wide is GPU-bound: s66 is 23.5 ms of GPU inside a 32.2 ms wall, of which the scene pass is
  18.6 ms, against 3.4 ms on the s27 close-up (614k against 135k triangles, 206 against 38 clustered lights).
- grove, the small scene, runs 76 FPS at 720p and 133 FPS at 360p on every arm: the engine is not slow in
  general.

### First take, with W3's CPU suite running (superseded by the clean retake above)

`build/qa-runs/baseline.sh` -> `tools/perf_sweep.py points ... --reps 3`, 2026-09-28 11:40-12:40, headless offline
clock, 42 frames (12 warm-up) from each point's second, interleaved per point across three arms (base `77ea4247`;
fix950; gate951, which is fix950 plus ADR-951). **Contention:** W3's full CPU suite (`avgen_tests`, 2 processes)
ran throughout (max 1-min load 12.0 at 720p, 7.8 at 360p), recorded per run; spreads are 1-14% except grove at
360p (18-19%). 0 GPU errors and a camera log naming the intended shot on every run (GV2 multicam's t=9 is a
free-roam camera and grove has no cut, so they log no shot label).

**The small scene is `grove`**: 31 nodes, no entities, no cut; the same renderer path (terrain, scatter, clustered
lighting, post, volumetrics) at a tenth of the geometry, so it separates "the engine is slow" from "this project
is slow". It runs at 61 FPS (720p) and 94 FPS (360p) on every arm.

Draws, triangles, instances, lights and casters are exact (ADR-145) and identical across arms, which is the proof
that the arms render the same frame. "engine update" is `Engine::update` on the last frame (`cpu(scene)`);
"renderer CPU" is the renderer's CPU frame, which includes `queueWait` on the GPU. Per-pass GPU medians and every
counter are in the JSON.

**1280x720** (3 repeats; wall = median of the three runs' p50, with min-max; p95/p99 are the median run's; GPU and engine update are medians)

| point | arm | wall p50 (min-max) | p95 | p99 | FPS | GPU p50 | renderer CPU p50 | engine update | draws | shadow draws | tris | visible inst | clustered lights | shadow casters |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| GV3 r7b s66 "The veil lit" (worst wide) | base | 402.8 (399.8-406.1) | 466.9 | 474.1 | 2.5 | 37.4 | 42.9 | 366.5 | 326 | 37 | 613593 | 2415 | 206 | 60 |
| GV3 r7b s66 "The veil lit" (worst wide) | fix950 | 254.7 (253.8-259.0) | 271.0 | 277.6 | 3.9 | 34.1 | 39.8 | 217.0 | 326 | 37 | 613593 | 2415 | 206 | 60 |
| GV3 r7b s66 "The veil lit" (worst wide) | gate951 | 33.4 (32.6-34.7) | 41.5 | 42.5 | 30.0 | 24.3 | 29.6 | 3.2 | 326 | 37 | 613593 | 2415 | 206 | 60 |
| GV3 r7b s24 "valley floor, low and wide" | base | 330.5 (317.8-338.4) | 346.8 | 354.2 | 3.0 | 40.2 | 45.7 | 282.0 | 306 | 44 | 584653 | 2706 | 206 | 60 |
| GV3 r7b s24 "valley floor, low and wide" | fix950 | 201.0 (200.6-212.7) | 225.1 | 231.8 | 5.0 | 33.2 | 38.6 | 164.5 | 306 | 44 | 584653 | 2706 | 206 | 60 |
| GV3 r7b s24 "valley floor, low and wide" | gate951 | 32.2 (31.3-32.4) | 41.1 | 41.6 | 31.0 | 22.7 | 28.6 | 3.1 | 306 | 44 | 584653 | 2706 | 206 | 60 |
| GV3 r7b s26 "veil at the water's edge" | base | 359.3 (355.7-364.6) | 399.6 | 399.8 | 2.8 | 34.2 | 40.2 | 355.5 | 302 | 35 | 559622 | 1946 | 206 | 54 |
| GV3 r7b s26 "veil at the water's edge" | fix950 | 234.3 (231.8-238.8) | 286.2 | 288.5 | 4.3 | 35.8 | 41.3 | 207.7 | 302 | 35 | 559622 | 1946 | 206 | 54 |
| GV3 r7b s26 "veil at the water's edge" | gate951 | 32.1 (30.1-32.3) | 40.6 | 42.8 | 31.2 | 22.8 | 28.2 | 3.3 | 302 | 35 | 559622 | 1946 | 206 | 54 |
| GV3 r7b s27 "under the elder: gills" (close) | base | 33.0 (31.0-33.6) | 50.2 | 52.0 | 30.3 | 15.1 | 20.1 | 9.4 | 196 | 72 | 135415 | 20 | 38 | 59 |
| GV3 r7b s27 "under the elder: gills" (close) | fix950 | 26.4 (24.6-26.9) | 33.8 | 39.5 | 37.9 | 14.3 | 19.3 | 5.6 | 196 | 72 | 135415 | 20 | 38 | 59 |
| GV3 r7b s27 "under the elder: gills" (close) | gate951 | 23.4 (23.0-25.3) | 27.9 | 28.3 | 42.8 | 14.6 | 19.6 | 3.7 | 196 | 72 | 135415 | 20 | 38 | 59 |
| GV3 r7b s17 "cairn on the spur" (no characters) | base | 27.2 (27.2-28.8) | 30.4 | 31.8 | 36.7 | 18.9 | 23.7 | 3.5 | 171 | 28 | 328615 | 456 | 1 | 23 |
| GV3 r7b s17 "cairn on the spur" (no characters) | fix950 | 28.0 (26.9-29.2) | 34.0 | 36.6 | 35.7 | 19.2 | 24.0 | 3.7 | 171 | 28 | 328615 | 456 | 1 | 23 |
| GV3 r7b s17 "cairn on the spur" (no characters) | gate951 | 27.1 (26.7-28.6) | 28.5 | 29.5 | 36.9 | 18.6 | 23.3 | 3.5 | 171 | 28 | 328615 | 456 | 1 | 23 |
| GV3 committed s14 "first grand wide" | base | 248.7 (247.0-250.5) | 264.3 | 337.3 | 4.0 | 28.1 | 33.7 | 216.7 | 255 | 21 | 382525 | 561 | 95 | 42 |
| GV3 committed s14 "first grand wide" | fix950 | 149.5 (147.9-149.8) | 167.7 | 177.2 | 6.7 | 24.5 | 29.7 | 118.0 | 255 | 21 | 382525 | 561 | 95 | 42 |
| GV3 committed s14 "first grand wide" | gate951 | 24.5 (23.7-24.6) | 28.0 | 29.4 | 40.8 | 16.8 | 22.0 | 2.4 | 255 | 21 | 382525 | 561 | 95 | 42 |
| GV3 committed s16 "gills" (close) | base | 22.0 (21.9-23.4) | 29.3 | 30.2 | 45.5 | 14.4 | 19.1 | 8.3 | 207 | 70 | 134202 | 32 | 87 | 57 |
| GV3 committed s16 "gills" (close) | fix950 | 21.4 (20.7-23.6) | 27.0 | 27.4 | 46.8 | 12.9 | 17.5 | 4.5 | 207 | 70 | 134202 | 32 | 87 | 57 |
| GV3 committed s16 "gills" (close) | gate951 | 22.8 (22.0-23.0) | 27.0 | 27.1 | 43.9 | 14.8 | 19.7 | 2.7 | 207 | 70 | 134202 | 32 | 87 | 57 |
| GV3 committed s01 "Nocturne" | base | 218.0 (216.0-226.6) | 286.1 | 288.2 | 4.6 | 28.1 | 33.2 | 182.1 | 273 | 60 | 506089 | 1852 | 160 | 60 |
| GV3 committed s01 "Nocturne" | fix950 | 146.0 (145.3-148.1) | 165.3 | 180.3 | 6.8 | 28.8 | 34.5 | 109.6 | 273 | 60 | 506089 | 1852 | 160 | 60 |
| GV3 committed s01 "Nocturne" | gate951 | 25.9 (25.4-27.8) | 30.6 | 31.5 | 38.7 | 17.9 | 22.9 | 2.5 | 273 | 60 | 506089 | 1852 | 160 | 60 |
| GV2 multicam t=2 Valley Wide | base | 66.3 (64.9-66.7) | 84.3 | 85.2 | 15.1 | 19.2 | 24.7 | 41.7 | 258 | 44 | 447125 | 1042 | 115 | 43 |
| GV2 multicam t=2 Valley Wide | fix950 | 39.7 (39.5-39.8) | 49.4 | 49.6 | 25.2 | 19.8 | 24.9 | 14.8 | 258 | 44 | 447125 | 1042 | 115 | 43 |
| GV2 multicam t=2 Valley Wide | gate951 | 28.0 (27.4-28.8) | 31.4 | 37.1 | 35.8 | 19.2 | 24.2 | 3.6 | 258 | 44 | 447125 | 1042 | 115 | 43 |
| GV2 multicam t=9 Hero Free Roam | base | 50.5 (50.4-51.9) | 56.3 | 57.7 | 19.8 | 22.3 | 27.3 | 22.8 | 189 | 32 | 276851 | 1512 | 171 | 33 |
| GV2 multicam t=9 Hero Free Roam | fix950 | 38.8 (38.2-40.8) | 49.7 | 50.2 | 25.7 | 22.6 | 27.7 | 10.6 | 189 | 32 | 276851 | 1512 | 171 | 33 |
| GV2 multicam t=9 Hero Free Roam | gate951 | 31.7 (30.4-32.4) | 45.6 | 48.6 | 31.5 | 23.3 | 28.1 | 3.2 | 189 | 32 | 276851 | 1512 | 171 | 33 |
| GV2 multicam t=27 Valley Wide | base | 68.7 (64.7-69.0) | 78.2 | 82.7 | 14.6 | 20.1 | 27.2 | 40.9 | 254 | 43 | 434840 | 922 | 88 | 43 |
| GV2 multicam t=27 Valley Wide | fix950 | 39.9 (39.4-40.5) | 46.3 | 46.5 | 25.1 | 19.9 | 24.8 | 15.2 | 254 | 43 | 434840 | 922 | 88 | 43 |
| GV2 multicam t=27 Valley Wide | gate951 | 27.8 (27.2-28.2) | 32.0 | 33.9 | 36.0 | 19.3 | 24.1 | 3.5 | 254 | 43 | 434840 | 922 | 88 | 43 |
| grove (small scene) t=2 | base | 16.4 (14.4-17.5) | 17.5 | 18.3 | 60.9 | 14.0 | 15.9 | 0.4 | 43 | 0 | 29408 | 286 | 0 | 0 |
| grove (small scene) t=2 | fix950 | 16.8 (15.8-17.3) | 18.3 | 18.5 | 59.5 | 14.4 | 16.3 | 0.5 | 43 | 0 | 29408 | 286 | 0 | 0 |
| grove (small scene) t=2 | gate951 | 15.6 (14.1-17.0) | 18.9 | 20.1 | 63.9 | 13.1 | 15.1 | 0.4 | 43 | 0 | 29408 | 286 | 0 | 0 |

**640x360** (3 repeats; wall = median of the three runs' p50, with min-max; p95/p99 are the median run's; GPU and engine update are medians)

| point | arm | wall p50 (min-max) | p95 | p99 | FPS | GPU p50 | renderer CPU p50 | engine update | draws | shadow draws | tris | visible inst | clustered lights | shadow casters |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| GV3 r7b s66 "The veil lit" (worst wide) | base | 390.3 (385.8-395.4) | 405.3 | 407.9 | 2.6 | 26.3 | 31.7 | 366.2 | 326 | 37 | 525410 | 2293 | 206 | 60 |
| GV3 r7b s66 "The veil lit" (worst wide) | fix950 | 247.4 (242.3-249.1) | 253.0 | 253.4 | 4.0 | 27.0 | 32.5 | 217.2 | 326 | 37 | 525410 | 2293 | 206 | 60 |
| GV3 r7b s66 "The veil lit" (worst wide) | gate951 | 25.0 (23.6-25.8) | 29.0 | 29.5 | 39.9 | 15.4 | 21.0 | 3.5 | 326 | 37 | 525410 | 2293 | 206 | 60 |
| GV3 r7b s24 "valley floor, low and wide" | base | 313.3 (304.8-315.2) | 321.6 | 329.8 | 3.2 | 27.5 | 32.9 | 279.9 | 306 | 44 | 510497 | 2598 | 206 | 60 |
| GV3 r7b s24 "valley floor, low and wide" | fix950 | 195.2 (192.5-195.4) | 216.6 | 231.3 | 5.1 | 26.0 | 31.3 | 161.6 | 306 | 44 | 510497 | 2598 | 206 | 60 |
| GV3 r7b s24 "valley floor, low and wide" | gate951 | 24.4 (23.8-25.1) | 27.2 | 29.7 | 41.1 | 15.8 | 20.7 | 2.9 | 306 | 44 | 510497 | 2598 | 206 | 60 |
| GV3 r7b s26 "veil at the water's edge" | base | 344.5 (336.7-344.8) | 385.5 | 391.2 | 2.9 | 22.2 | 27.9 | 354.0 | 309 | 35 | 456353 | 1841 | 206 | 54 |
| GV3 r7b s26 "veil at the water's edge" | fix950 | 217.4 (216.9-218.3) | 233.7 | 234.3 | 4.6 | 22.0 | 27.5 | 206.0 | 309 | 35 | 456353 | 1841 | 206 | 54 |
| GV3 r7b s26 "veil at the water's edge" | gate951 | 24.2 (24.0-26.7) | 27.5 | 28.2 | 41.4 | 15.9 | 20.7 | 3.0 | 309 | 35 | 456353 | 1841 | 206 | 54 |
| GV3 r7b s27 "under the elder: gills" (close) | base | 30.0 (27.7-31.4) | 42.3 | 42.5 | 33.4 | 10.0 | 14.9 | 9.9 | 196 | 72 | 124768 | 20 | 38 | 59 |
| GV3 r7b s27 "under the elder: gills" (close) | fix950 | 20.9 (20.8-21.2) | 36.8 | 42.3 | 47.9 | 8.7 | 13.4 | 5.5 | 196 | 72 | 124768 | 20 | 38 | 59 |
| GV3 r7b s27 "under the elder: gills" (close) | gate951 | 17.2 (16.7-19.1) | 23.9 | 24.6 | 58.0 | 8.5 | 13.3 | 3.7 | 196 | 72 | 124768 | 20 | 38 | 59 |
| GV3 r7b s17 "cairn on the spur" (no characters) | base | 24.7 (23.0-24.8) | 30.9 | 34.0 | 40.5 | 15.9 | 21.0 | 3.6 | 184 | 28 | 252205 | 443 | 1 | 23 |
| GV3 r7b s17 "cairn on the spur" (no characters) | fix950 | 24.1 (23.1-25.1) | 27.3 | 28.7 | 41.4 | 15.7 | 20.5 | 3.6 | 184 | 28 | 252205 | 443 | 1 | 23 |
| GV3 r7b s17 "cairn on the spur" (no characters) | gate951 | 24.1 (23.3-24.1) | 27.9 | 30.5 | 41.6 | 15.3 | 20.3 | 3.7 | 184 | 28 | 252205 | 443 | 1 | 23 |
| GV3 committed s14 "first grand wide" | base | 234.2 (233.8-237.9) | 252.7 | 354.9 | 4.3 | 15.6 | 21.0 | 216.2 | 265 | 21 | 312549 | 537 | 95 | 42 |
| GV3 committed s14 "first grand wide" | fix950 | 140.4 (138.6-144.9) | 149.1 | 168.0 | 7.1 | 16.1 | 21.4 | 118.8 | 265 | 21 | 312549 | 537 | 95 | 42 |
| GV3 committed s14 "first grand wide" | gate951 | 23.5 (22.0-24.4) | 27.5 | 28.6 | 42.6 | 15.7 | 20.9 | 2.3 | 265 | 21 | 312549 | 537 | 95 | 42 |
| GV3 committed s16 "gills" (close) | base | 17.6 (17.2-18.1) | 22.5 | 22.8 | 56.9 | 9.6 | 14.5 | 8.4 | 207 | 70 | 125272 | 32 | 87 | 57 |
| GV3 committed s16 "gills" (close) | fix950 | 17.0 (16.9-17.7) | 22.4 | 22.5 | 58.9 | 9.7 | 14.2 | 4.5 | 207 | 70 | 125272 | 32 | 87 | 57 |
| GV3 committed s16 "gills" (close) | gate951 | 16.8 (16.8-17.6) | 22.7 | 22.8 | 59.4 | 9.1 | 13.7 | 2.7 | 207 | 70 | 125272 | 32 | 87 | 57 |
| GV3 committed s01 "Nocturne" | base | 210.0 (209.5-210.5) | 226.6 | 237.1 | 4.8 | 20.0 | 25.4 | 181.4 | 275 | 60 | 438027 | 1744 | 160 | 60 |
| GV3 committed s01 "Nocturne" | fix950 | 135.3 (135.0-135.4) | 142.9 | 144.2 | 7.4 | 18.2 | 23.6 | 108.0 | 275 | 60 | 438027 | 1744 | 160 | 60 |
| GV3 committed s01 "Nocturne" | gate951 | 23.8 (23.8-24.2) | 29.6 | 30.3 | 42.0 | 16.0 | 21.0 | 2.5 | 275 | 60 | 438027 | 1744 | 160 | 60 |
| GV2 multicam t=2 Valley Wide | base | 67.3 (65.1-68.8) | 71.3 | 73.9 | 14.9 | 19.9 | 25.2 | 41.6 | 265 | 44 | 371713 | 958 | 115 | 43 |
| GV2 multicam t=2 Valley Wide | fix950 | 39.1 (38.9-39.6) | 44.8 | 46.6 | 25.6 | 18.4 | 23.7 | 14.9 | 265 | 44 | 371713 | 958 | 115 | 43 |
| GV2 multicam t=2 Valley Wide | gate951 | 25.7 (24.7-26.3) | 27.9 | 29.0 | 38.9 | 17.0 | 21.9 | 3.6 | 265 | 44 | 371713 | 958 | 115 | 43 |
| GV2 multicam t=9 Hero Free Roam | base | 50.0 (47.5-50.3) | 58.8 | 67.9 | 20.0 | 20.9 | 25.7 | 22.9 | 203 | 32 | 206189 | 1396 | 171 | 33 |
| GV2 multicam t=9 Hero Free Roam | fix950 | 32.9 (32.2-32.9) | 39.0 | 40.7 | 30.4 | 17.0 | 21.9 | 10.6 | 203 | 32 | 206189 | 1396 | 171 | 33 |
| GV2 multicam t=9 Hero Free Roam | gate951 | 26.2 (23.8-26.3) | 27.6 | 28.3 | 38.1 | 16.1 | 22.6 | 3.2 | 203 | 32 | 206189 | 1396 | 171 | 33 |
| GV2 multicam t=27 Valley Wide | base | 65.1 (63.7-67.1) | 77.4 | 81.7 | 15.4 | 18.8 | 23.7 | 41.2 | 263 | 43 | 363812 | 835 | 88 | 43 |
| GV2 multicam t=27 Valley Wide | fix950 | 38.5 (38.4-40.3) | 45.5 | 54.7 | 26.0 | 19.1 | 24.2 | 15.0 | 263 | 43 | 363812 | 835 | 88 | 43 |
| GV2 multicam t=27 Valley Wide | gate951 | 25.1 (24.1-25.5) | 28.7 | 30.0 | 39.8 | 16.0 | 21.3 | 3.6 | 263 | 43 | 363812 | 835 | 88 | 43 |
| grove (small scene) t=2 | base | 10.6 (10.6-10.7) | 13.4 | 13.6 | 94.2 | 8.3 | 10.2 | 0.5 | 43 | 0 | 29408 | 286 | 0 | 0 |
| grove (small scene) t=2 | fix950 | 10.8 (10.6-10.9) | 12.8 | 14.9 | 92.2 | 8.4 | 10.4 | 0.4 | 43 | 0 | 29408 | 286 | 0 | 0 |
| grove (small scene) t=2 | gate951 | 11.0 (10.9-11.1) | 13.1 | 14.1 | 90.9 | 8.5 | 10.6 | 0.4 | 43 | 0 | 29408 | 286 | 0 | 0 |

**Reading.**
- Base: GV3 r7b's wides are **2.5-3.0 FPS at 720p and 2.6-3.2 at 360p** - resolution-independent, because
  88-98% of the frame is `Engine::update`. The committed GV3 is the same (4.0-4.8 FPS on its wides), and so is GV2
  multicam, less severely (15 FPS on its wides against 20 on its hero shot): same world, fewer characters in its
  wides.
- Close-ups (s27, committed s16) and empty shots (s17) run at 30-57 FPS on base, because their engine update is
  3.5-9.4 ms.
- With ADR-950 + ADR-951 every GV3 point is 30-43 FPS at 720p and 40-58 FPS at 360p. At 720p a GV3 wide is then
  GPU-bound (24 ms GPU of a 33 ms wall; `queueWait` 26 ms), at 360p it is CPU-bound at ~24 ms (GPU 15 ms).
- Residual view-dependent GPU cost (720p, gate arm): the scene pass is 19.3 ms on the s66 wide against 6.5 ms on the
  s27 close-up (614k against 135k submitted triangles, 2,415 against 20 visible procedural instances, 206 against 38
  clustered lights). That is the renderer's known fragment/quad-overdraw cost of dense foliage at distance
  (`docs/renderer-upgrade/README.md`), not a defect.

## 5. Hypothesis B: remove one category of project state at a time

`tools/make_gv3_state_arms.py examples/world/_qa-gv3-r7b.json` writes scratch copies (`_qa-b-<arm>`, never
committed), each with one category and every reference to it removed and the scene fingerprint recomputed; the
Desktop originals were only read. `build/qa-runs/hypB.sh`: 640x360, 3 interleaved repeats, the worst wide (s66)
and a close-up (s27), on the base binary (what the owner runs) and on gate951 (so a category's own cost is not
hidden under the sightline). All 120 runs: 0 GPU errors, 0 error lines, camera on the intended shot. W3's
`avgen_tests` ran throughout (max load 13.6). Arms 9 and 10 were added from W3's state audit
(`docs/qa-pass/gv3-state-audit.md` on `qa/clean`).

| arm | what was removed and why | s66 wide, base | s66, gate951 | s27 close, base | s27, gate951 | visual change |
|---|---|---:|---:|---:|---:|---|
| control | nothing (the generator's round trip) | 385.1 (382.8-389.8) | 23.4 (23.0-23.8) | 28.4 (27.8-31.8) | 17.2 (17.0-21.6) | none |
| no-groundpulse | the 15 + 11 hero-pulse `groundPulse` effects (the "15 hero pulses" suspicion) | 384.3 | 23.5 | 26.1 | 17.7 | pulses gone |
| no-effects | every effect, project and scene (aurora, glow, travel beam, pulses) | 381.5 | 22.9 | 26.6 | 16.8 | aurora and pulses gone |
| no-staging | project and scene staging and both directing plans | 410.4 | 23.6 | 23.4 | 17.5 | set pieces (UFO) gone |
| no-routes | all 81 modulation routes | 385.0 | 23.8 | 30.0 | 17.3 | no audio reactivity |
| no-automation | the 44 non-camera timeline tracks | 387.9 | 23.5 | 27.4 | 17.4 | no automation |
| no-scout | GV3's additions: scout, scout-beam, the two field nodes | 365.7 | 23.2 | 23.0 | 17.0 | scout gone; 4 fewer draws |
| **no-entities** | every EntityWorld entity (nodes still render; nothing simulated or published) | **22.4** | 22.1 | **16.3** | 16.8 | characters stand still |
| small-beams | the two hidden 32,768-particle beam pools at 256 | 384.8 | 24.7 | 28.6 | 16.9 | none (hidden) |
| eco-gv2 | GV3's ground-pool settings back to GV2's defaults (206 -> 159 clustered lights on s66) | 383.2 | 25.1 | 28.0 | 17.1 | fewer ground pools |

(median wall ms of 3; spreads 1-7% except s27 base at 3-22%.)

**Verdict on B: no project-state category costs a measurable share of the frame.** Every arm but one is inside
the control's spread on the gated binary. The one that moves the base binary is `no-entities` (385 -> 22 ms),
which removes the characters the sightline is marched to; on gate951 it is inside the spread. `no-scout` (-5%) and
`no-staging` on the close-up (-6 ms engine update) move the base binary for the same reason: fewer characters in
frame. The hidden beam pools cost nothing measurable: the renderer's `particleCapacity` counter reads 21,888 in
every arm, i.e. the two hidden 32,768 pools are not in the frame's particle work (answering W3's open question
#3). The ground pools add 47 clustered lights to the wide and nothing measurable to its GPU time at 360p.

The static report (section 4) agrees: no duplicate, orphaned or dead state, and the world is GV2 multicam's.

## 6. Hypothesis C: the same GV2 multicam workload on older builds (1280x720)

`build/qa-runs/hypC.sh`, 3 interleaved repeats, W3's CPU suite running (recorded). GV2 multicam at t=2 (Valley
Wide), t=9 (Hero Free Roam) and t=27 (Valley Wide again). There are two workloads:
- `cur`: today's `glowmere-valley-2-multicam.json`, run by every build;
- `own`: each build run on its own commit's copy of the file.

Builds:
- `s0916` = `c2f61058` (2026-09-16). It has no `--range`, so its runs all start at t=0, and it refuses today's
  file (`parameter 'particles/bloom-spores/extent' expects a number`). Its `own` rows are a different world (322k
  against 448k triangles) at a different second, so they are context only, not a comparison;
- `pre834` = `3e09f9e1` (the 09-25 main just before the agent/motion merge);
- `m834` = `29e6918e` (that merge, carrying ADR-834);
- `now` = `77ea4247`.

| point | build | wall p50 (min-max) | FPS | GPU p50 | scene pass | engine update | draws | tris | clustered lights |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| cur-t2 | s0916 | refused to load the current file | | | | | | | |
| cur-t2 | pre834 | 25.6 (25.5-26.5) | 39.1 | 17.2 | 13.0 | 3.2 | 259 | 447907 | 206 |
| cur-t2 | m834 | 47.4 (45.7-48.2) | 21.1 | 17.9 | 13.3 | 23.6 | 259 | 447907 | 206 |
| cur-t2 | now | 65.7 (65.2-67.4) | 15.2 | 20.0 | 15.5 | 40.6 | 258 | 447125 | 115 |
| own-t2 | s0916 | 19.2 (18.6-21.9) | 52.0 | 13.2 | 8.3 | 2.5 | 262 | 322072 | 222 |
| own-t2 | pre834 | 26.1 (25.9-28.1) | 38.3 | 17.7 | 13.5 | 3.2 | 259 | 447907 | 206 |
| own-t2 | m834 | 46.1 (45.9-47.2) | 21.7 | 17.6 | 13.2 | 23.1 | 259 | 447907 | 206 |
| own-t2 | now | 66.3 (66.0-69.1) | 15.1 | 20.4 | 16.1 | 41.3 | 258 | 447125 | 115 |
| cur-t9 | s0916 | refused to load the current file | | | | | | | |
| cur-t9 | pre834 | 27.0 (26.9-27.1) | 37.0 | 19.2 | 14.2 | 3.0 | 189 | 275607 | 206 |
| cur-t9 | m834 | 38.3 (38.2-38.4) | 26.1 | 19.5 | 14.5 | 13.6 | 189 | 275607 | 206 |
| cur-t9 | now | 49.9 (49.7-50.3) | 20.1 | 22.7 | 17.7 | 22.7 | 189 | 276851 | 171 |
| own-t9 | s0916 | 19.1 (18.9-21.4) | 52.5 | 12.5 | 7.9 | 2.6 | 262 | 322072 | 222 |
| own-t9 | pre834 | 28.0 (27.3-28.9) | 35.7 | 19.6 | 14.5 | 3.6 | 189 | 275607 | 206 |
| own-t9 | m834 | 38.5 (38.3-38.8) | 26.0 | 19.3 | 14.4 | 14.3 | 189 | 275607 | 206 |
| own-t9 | now | 49.9 (48.9-50.6) | 20.0 | 22.2 | 17.2 | 22.4 | 189 | 276851 | 171 |
| cur-t27 | s0916 | refused to load the current file | | | | | | | |
| cur-t27 | pre834 | 25.5 (24.4-25.6) | 39.2 | 17.0 | 12.7 | 3.3 | 254 | 434653 | 206 |
| cur-t27 | m834 | 45.2 (44.8-48.9) | 22.1 | 17.3 | 13.1 | 22.9 | 254 | 434653 | 206 |
| cur-t27 | now | 66.0 (63.9-69.2) | 15.2 | 20.5 | 15.9 | 40.1 | 254 | 434840 | 88 |
| own-t27 | s0916 | 19.1 (18.8-19.7) | 52.3 | 12.5 | 8.0 | 2.8 | 262 | 322072 | 222 |
| own-t27 | pre834 | 25.4 (24.7-25.6) | 39.3 | 17.2 | 12.7 | 3.2 | 254 | 434653 | 206 |
| own-t27 | m834 | 44.3 (44.3-45.0) | 22.6 | 17.0 | 12.7 | 22.3 | 254 | 434653 | 206 |
| own-t27 | now | 65.5 (64.4-66.3) | 15.3 | 20.1 | 15.5 | 39.7 | 254 | 434840 | 88 |

**Step 1, `3e09f9e1` -> `29e6918e` (ADR-834): a regression.** Engine update goes 3.2 -> 23.6 ms on the wide, with
the GPU flat, the draws identical and the triangles identical: 25.6 -> 47.4 ms wall. The profile (section "First
probe") and the kill switch attribute it to `publishCinematicSignals` -> `heroSightline`. **Fixed** by ADR-950 +
ADR-951: gate951 on this point is 28.0 ms (section 1), against pre834's 25.6.

**Step 2, `29e6918e` -> `77ea4247`: two further regressions at an identical workload. Not yet bisected.**
- Engine update 23.6 -> 40.6 ms, at the same characters. The gated build takes this back to 3.6 ms, so this step
  is also sightline cost: each sightline became roughly 1.7x dearer. The suspect is ADR-893 (`0b623b88`,
  continuous path levels and blended waters). The sample profile shows `heightUncached` dominated by
  `blendedLevel` and `closestOnPath`.
- GPU 17.2 -> 20.0 ms (scene pass 13.0 -> 15.5, +18%), with fewer clustered lights (206 -> 115/88). The suspect is
  ADR-945 (`ad5623d2`, ecology lights chosen by contribution, each lighting a pool at twice the range). Resume
  with `hypC2.sh` (see "Resume here").

## 7. The live editor (interim)

`build/qa-runs/live.sh`: the live editor, `--project <r7b> --size 1280x720 --frames 300 --profile-cpu
--adaptive-scale off --canvas-scale 1 --preview-mode workspace --start-at <s>`, idle (paused) and `--play`, the
wide s66 (194.706) and the close-up s27 (85.783). The canvas was 732x664 (0.49 Mpx). 3 repeats; 0 GPU errors. The
table gives the main-thread FRAME p50 and `engine.update` p50 (ms), median of 3.

| | base | fix950 | gate951 |
|---|---:|---:|---:|
| wide, playing: frame / engine.update | 321.8 / 290.2 (3.1 FPS) | 192.4 / 159.4 | 9.3 / 3.1 |
| wide, paused: frame / engine.update | 81.8 / 75.5 | 36.2 / 31.3 | 16.7 / 3.4 |
| close-up, playing | 30.2 / 25.6 | 13.8 / 9.2 | 8.9 / 3.6 |
| close-up, paused | 109.3 / 102.4 | 52.3 / 47.0 | 16.7 / 3.6 |

- **The editor frame is the render frame plus the same engine update.** The sightline costs the editor exactly what
  it costs headless, and **it runs while paused**: a paused wide still spends 75 ms per frame on it. This is
  "navigating is slow".
- The paused close-up is dearer than the playing one: the paused playhead holds a frame where more characters are
  in view at distance. This has not been checked further.
- gate951's frame p50 values of 8.9, 16.7 and 24.9 are vsync quantisation (present-bound); its GPU frame is 12-15 ms.
- **Open, for item 7:** `ui.build` has a p90 of about 36 ms in every arm, idle or playing, so a periodic UI-build
  spike happens in at least 10% of frames. In the base arm's playing wide it is 35 ms at p50. It is not
  explained yet.
- `--ui-ab idle:camera` (navigation), first repeat: base idle 75.6 / camera-orbit 113-127 ms; gate951 idle 16.7 /
  camera-orbit 23.7-53.4 ms. The rest of the nav repeats were still running at the checkpoint.

## Verdicts so far

- **A (scene complexity or renderer limit):**
  - It is not the collapse. With the sightline gone, every GV3 shot is 30-43 FPS at 720p and 40-58 FPS at 360p
    headless.
  - What remains is the renderer's view-dependent cost: the scene pass is 19 ms on a wide against 6.5 ms on a
    close-up at 720p, with 614k against 135k triangles.
  - The envelope: at 720p a GV3 wide is GPU-bound at about 24 ms of GPU (about 30-40 FPS).
- **B (bad project state):**
  - No. Ten removal arms were measured, and none moved the frame beyond the spread except removing the characters
    themselves.
  - The static report finds no duplicate, dead or orphaned state. The world is GV2 multicam's.
- **C (engine regression):**
  - Yes, and it is the cause. ADR-834 (`29e6918e`, 2026-09-25) added a per-frame, per-in-shot-character
    sightline whose cost is linear in distance (r = 0.975 across 73 shots).
  - A further step between `29e6918e` and `77ea4247` made each sightline about 1.7x dearer (to bisect), and a
    separate step added about +2.5-3 ms of GPU (to bisect).
- **Fixes:** on s66 at 720p, headless, 402.8 ms against 33.4 ms.
  - ADR-950 accounts for 402.8 -> 254.7 ms (-148 ms, bit-identical).
  - ADR-951 accounts for 254.7 -> 33.4 ms (-221 ms). It needs the owner's acknowledgement.
