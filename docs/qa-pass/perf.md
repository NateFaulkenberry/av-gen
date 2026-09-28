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
