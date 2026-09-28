# W1: performance and the Glowmere Valley 3 investigation

Agent 1's working notes. Kept current so a successor can resume. Worktree `../av-gen-qa-perf`, branch `qa/perf`,
based on `77ea4247` (+ `f417728b`, the QA docs).

## Resume here
- Status: see the checklist below; the latest section written is the last one with numbers in it.
- Scratch projects (NOT committed, never commit): `examples/world/_qa-gv3-r7b.json` + `.scene.json` are copies of
  `~/Desktop/av-gen-review/18-glowmere-valley-3/revision/integrate/project-for-review/unbundled-copies/` with only the
  project's scene reference renamed. Song: `~/Desktop/Rebuild.mp3` (absolute path in the project; never commit).
  Scratch arms for experiment 5 are `examples/world/_qa-b-*.json`.
- Machine: Apple M2 Max, 64 GB, macOS 26.7. Release build `build/release` of this worktree.
- Every timing is taken under `tools/gpu-lock.sh`, after `ps aux | grep avgen` shows nothing else on the GPU.

## Checklist
- [ ] 1. Baseline (GV3 r7b, GV3 committed, GV2 multicam, small scene; 1280x720 and 640x360)
- [ ] 2. Per-shot sweep of r7b's 73 shots
- [ ] 3. View-dependent attribution, worst wide vs best close-up
- [x] 4. Complexity report script (`tools/scene_complexity_report.py`), static part
- [ ] 5. Hypothesis B removals
- [ ] 6. Hypothesis C, historical commits
- [ ] 7. Editor / navigation cost
- [ ] 8. Fixes

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
