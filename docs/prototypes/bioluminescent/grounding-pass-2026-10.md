# THE RIFT: grounding and camera pass (2026-10)

**Request** (the owner, 2026-10-15, five reports on THE RIFT LIVE):

1. Things are not drawn consistently on the floor, most obviously at the start of the scene. The trunks and a band of
   glowing growth seem to sit on a raised ledge that floats above a dark ground plane.
2. Some floating elements are drawn below the floor plane.
3. Plants on the floor sometimes float, and so does the river.
4. Plants on the canyon walls must also be positioned correctly.
5. The errors are worst at low camera angles. The camera should not clip through tree trunks.

The owner also asked whether this is a camera-perspective bug.

**Answer.** It is not a perspective bug. There were four separate defects, all in the generated world, and all four
are fixed in the generator (`examples/bioluminescent/`). No engine code changed. Both projects (`rift.json`,
`rift-live.json`) use the same world, so both are fixed.

- **Outside the world:** the opening shot looked at the edge of the world from outside it.
- **LOD:** the terrain's coarse levels moved the drawn ground metres away from where things stood.
- **Contact:** trunks and wall patches touched the ground at one point, not across their base.
- **River:** the river's water ended in mid-air over gullies.

The camera's whole corridor now clears the trunks by 2.5 m. THE RIFT LIVE still holds 30 fps.

Media: `~/Desktop/av-gen-review/53-rift-grounding/` (see §6).

## 1. Causes

### 1.1 The opening shot looked at the world's edge from outside it (reports 1 and 2)

The world map was 2400 m square, so the terrain covered z = -1200 to +1200. But:

- the flight runs from z = -1300 to +1300;
- the organisms are placed from z = -1400 to +1400.

So at the start the camera was 100 m outside the terrain. At the Dark stage's 3 m/s, it stayed outside for about the
first half-minute. What the owner saw:

- **The "raised ledge":** the terrain's cut edge at z = -1200, seen end-on. The lit meadows and the trunks stood on
  top of it.
- **The "dark ground plane":** the sky's ground colour, drawn below the horizon where there was no terrain.
- **The "floating elements below the floor":** 27,109 instances (15% of all of them). They were placed on the
  world's analytic heights beyond the edge, so they stood on nothing, below the ledge's level.

Compare `pairs/opening-1-s-t1.png` and `pairs/opening-12-s-t12.png`. In the before stills you can see the ledge,
including the walls' cut cross-sections at the right.

**Fix:**

- The map is now 4600 m along the canyon (`land.SIZE_Z`): the flight plus the terrain's 1000 m view distance at
  either end.
- The canyon's feature paths run to z = ±2300.
- The map's noise does not depend on its size. The probe heights inside the old extent are identical (max |Δ| = 0.0).

### 1.2 The terrain's coarse LOD levels moved the drawn ground (reports 1, 3 and 5)

The terrain drew four LOD levels: 2 m cells near the camera, then 4, 8 and 16 m cells. LOD 1 began 70 m out and
doubled from there, scaled by the lens.

The organisms were placed on the true surface, sampled on a 2 m lattice. A coarse level cuts through the canyon's
ridged rock ribs (9 m amplitude, 45 m wavelength):

- on a rib crest, the drawn ground drops below the organisms, so they stand on air;
- in a gully, the drawn ground rises above them and swallows them.

Measured at each instance, as the gap between its placement and the drawn mesh (median, then extremes):

| Kind | LOD 1 (4 m) | LOD 2 (8 m) | LOD 3 (16 m) |
|---|---|---|---|
| Sea pens | -0.09, -0.70 to +2.28 m | -0.43, -2.75 to +4.36 m | -1.32, -8.52 to +6.30 m |
| Mats | -0.04, -0.76 to +2.36 m | -0.02, -2.94 to +4.40 m | +0.11, -10.2 to +5.99 m |
| Crinoids (trunks) | -0.34, -1.06 to +0.72 m | -0.41, -3.23 to +2.07 m | -0.47, -8.56 to +5.62 m |
| Wall crust | 0.00, -1.01 to +1.48 m | +0.04, -3.44 to +4.32 m | +0.27, -8.88 to +5.49 m |

Why this is worst at low angles:

- A ground-hugging camera looks along the ground, so most of the frame's floor is far away and drawn at the coarse
  levels.
- A metre of air under a meadow shows as a dark strip under a lit band, which is the owner's "ledge" again, this time
  in mid-shot.
- The error depends on the view, because the LOD level is chosen per chunk per frame. This is why it can look like a
  camera bug.

The engine side was checked and is consistent:

- The terrain, the bodies and the ecosystem's emitters all project with the same frame uniforms (`frameUniforms`,
  `viewProj`).
- The ecosystem's occlusion reads a linear-depth target made at the HDR target's size (`scene_renderer.cpp`), which
  is the size the emitters splat at.
- No pass uses a different projection or a stale camera.

**Fix:**

- `lodLevels: 1`: the terrain is drawn at LOD 0 (2 m) everywhere it is drawn.
- `land.Ground.height` now interpolates as the LOD 0 mesh does: the same chunk-grid vertices and the same b-c
  diagonal as `terrain.cpp`. Before, it used bilinear interpolation, which is not what is on screen.
- Cost: 0.8 ms of GPU at High, headless, at the drop. That is the A/B of `lodLevels` 4 vs 1 on the new world (§4).

### 1.3 Contact geometry: trunks and wall patches touched at one point (reports 1 and 4)

Even at LOD 0, each instance was placed by the ground height at its origin. Things with a wide base did not sit
flush:

- **Trunks:** a crinoid's holdfast ring is up to 2.6 m across. On a bank slope its downhill side stood up to
  **2.20 m** off the ground. That is the trunk rising "from the strip rather than the floor".
- **Wall crust:** a crust patch is a flat 3-8 m disc of light. On a curved wall its edges stood up to **1.89 m** in
  front of the rock.
- **Smaller kinds:** mats were up to 0.42 m off the ground, whips 0.26 m, embers 0.24 m, lanterns 0.10 m and fans
  0.07 m.

All gaps are measured along the surface normal.

**Fix (`grounding.fit`):** each kind's contact geometry is the lowest band of its mesh's vertices, or the template
points of an emitter-only patch. The fit works like this:

1. Each instance slides along its own up axis until no contact point stands more than the kind's tolerance off the
   drawn surface (0.02-0.06 m along the normal).
2. Nothing may then go deeper than the authored sink plus the kind's allowance. A trunk on a slope buries its uphill
   side (up to 4 m, which is correct for a trunk); a cushion on a hump buries its middle.
3. A crust patch or a mat that cannot lie flat at its size shrinks about its centre. Below 35% of its size it is
   dropped.

Results:

- 2,649 of 26,601 crust patches shrank, and 2 were dropped.
- 13 mats shrank.

Wall placements are judged by distance along the normal, not by vertical distance, so a 70° wall is not
under-measured. Every wall kind (fans, crust, embers) keeps its tilt toward the wall's normal, and still needs a wall
slope on both the drawn ground and the original layout ground.

### 1.4 The river's water ended in mid-air (report 3)

The river's water reaches 15 m from its course. In places the ground at that distance was lower than the water line:

- where the shelf's falloff let the rock noise through;
- in gullies beside the channel.

There, the water sheet ended over open air. On the 2 m lattice, 2,002 shoreline cells had the ground beyond them more
than 0.3 m below the water. The worst was **11.0 m**, at z = 1212, where the river ran past a gully 11 m deep.

**Fix:** a `floodplain` flat feature (`land.world`) along the river.

- It is 40 m wide, falls off at 0.5 and flattens at 0.97.
- Its level is 0.8 m above the water line.
- The river cuts its channel after it.

Now the ground beyond the water is never more than 0.008 m below the water line, and the surface steps at most
0.009 m between adjacent cells.

**Placement kept:** the floodplain flattened the floor by the river, so placement masks read from it would have
changed the meadows. The sea pens tripled (25,640 to 83,578), and the whips and lanterns doubled. So placement reads
**where things grow** (the slope and wetness masks) from the layout ground, which is the world without the
floodplain. It reads **how high things stand** from the drawn ground. Counts after the pass:

| Kind | Before | After |
|---|---|---|
| Crinoids | 291 | 291 |
| Sea pens | 25,640 | 24,373 |
| Mats | 99,170 | 98,500 |
| Fans | 12,623 | 10,970 |
| Whips | 3,532 | 3,342 |
| Lanterns | 964 | 921 |
| Crust | 28,900 | 26,601 |
| Embers | 381 | 346 |

Fans and crust also need a wall on the drawn ground, so the ones the floodplain flattened under are gone.

### 1.5 What was not a cause

- **Billboard pivots:** the ecosystem's sprites are centred on their points (`vs_sprite`, camera right/up). The points
  are the organisms' own photophores, on the bodies, so a sprite hovers by none of its height.
- **Particles:** the spores and the swarm spawn in a sphere around the flight line, and they are depth-tested
  (`CompareFunction::Less`) against the scene's depth. One spawned under the ground is hidden until it rises out.
  Nothing in the after stills draws below the ground.
- **Projection, depth and render scale:** see §1.2. The live controller's render scale (0.71 at Low) scales every
  target together.

## 2. The camera

The spline camera's eye is the flight point plus `camera/splineOffset` in the spline frame (`composition.cpp`): x is
the binormal (lateral) and y is the normal (up). The arc's stages ease that offset between nine behaviours
(`build.CAM`):

- lateral: -10 to +10 m;
- up: 2.6 to 70 m.

A MIDI pad can jump between any two stages, and two LFOs drift the eye by up to ±2 m lateral and ±1 m up. So the set
of places the eye can be at any point on the flight is:

> the convex hull of the nine offsets, grown by the drift, swept along the flight (`grounding.eye_hull`,
> `grounding.Flight`).

The hull is conservative: it includes every blend between any two behaviours.

**Before**, distance from the eye's corridor to the obstacle's surface (negative = the corridor passes through it):

| Obstacle | Clearance | Instances inside the corridor | Within 2 m |
|---|---|---|---|
| Crinoid stalks | **-0.98 m** | 46 | 75 |
| Fans | **-1.56 m** | 11 | - |
| Whip tufts | +0.44 m | - | 173 within 2 m |
| Lanterns | +2.62 m | - | - |

The worst case is the "wall" stage, lateral +10 m at 11 m up, which passes through the bank row of crinoid stalks.

**Fix:**

- **Crinoid stalks:** a stalk whose centre line, at its mesh radius, comes within 2.5 m of the corridor steps straight
  out from the flight, sideways, until it clears. It is then placed again: new height, and a new lean toward the
  river. 85 of 291 moved, by 8 m at most. The bank rows stay on the banks and their crowns still arch over the river.
- **Fans and whips:** those whose bulk is within 1 m of the corridor are dropped (6 fans, 1 whip tuft). Moving one
  off a wall would unseat it.

**After:**

| Obstacle | Clearance |
|---|---|
| Crinoid stalks | **2.50 m** |
| Fans | 1.03 m |
| Whips | 1.01 m |
| Lanterns | 3.12 m |
| Ground (the eye's lowest height above it) | 3.80 m |

The flight's points, paces, stage behaviours and loop/arc timing are unchanged. `rift.json` and `rift-live.json` are
byte-identical, and `test_rift`'s "the flight does not run out of path" still passes.

The crowns are not cleared. The "canopy" and "surge" stages fly through the crinoid crowns on purpose (`build.CAM`:
"through the crinoid crowns"). The owner's request was the trunks.

## 3. The checks (so this cannot silently regress)

**In the generator.** `build.py` stops if any of these fails, and `test_rift.cpp` runs it on a stale or clean
checkout:

- **`assert_world_covers`:** the map holds every placement, and the flight plus the terrain's view distance ahead of
  either end.
- **`assert_terrain_full_resolution`:** the terrain draws one level.
- **`check` / `assert_grounded`:** for every kind and every instance, the worst contact point stands no more than the
  kind's tolerance off the drawn ground, along the normal. Nothing is deeper than the sink plus the allowance.
- **`check_free`:**
  - Comb jellies stay at least 1.5 m above the ground, because they bob 1.2 m. The actual minimum is 2.02 m.
  - Plankton sit on the water surface (0.000 m off) and above the bed.
- **`check_river` / `assert_river`:** wherever the water ends, the ground beyond it is at or above the water line
  (tolerance 0.05 m), and the surface is continuous.
- **`camera_clearance`:** stalks are at least 2.5 m from the corridor, and fans, whips and lanterns at least 1 m.

Every run of `build.py` prints the numbers. Current output:

```
crinoid  n 291    maxGap 0.010  deepest -2.335  (allowed -4.484)
seapen   n 24373  maxGap 0.011  deepest -0.101  (allowed -0.300)
mat      n 98500  maxGap 0.016  deepest -0.366  (allowed -0.384)
fan      n 10970  maxGap 0.011  deepest -0.228  (allowed -0.515)
whips    n 3342   maxGap 0.011  deepest -0.443  (allowed -0.520)
lanterns n 921    maxGap 0.011  deepest -0.131  (allowed -0.320)
crust    n 26601  maxGap 0.031  deepest -0.382  (allowed -0.410)
ember    n 346    maxGap 0.031  deepest -0.277  (allowed -0.410)
comb     minAboveGround 2.016;  plankton minAboveBed 2.121, maxOffWater 0.000
river    worstBankBelowWater 0.008, maxDepth 4.300, maxSurfaceStep 0.009
camera   crinoid 2.495, fan 1.025, lanterns 3.116, whips 1.007, ground 3.803
```

**In the engine's own terms** (`tests/integration/test_rift.cpp`, "THE RIFT: every body stands on the ground the
terrain draws and the ground covers the flight"). This test does not use the generator's probe lattice. It:

- parses `rift.scene.json`;
- builds the WorldMap with `worldMapFromJson`;
- checks `lodLevels == 1`;
- checks that every flight point ± the view distance is inside the map;
- for each body kind, checks every instance's origin against the LOD 0 triangles. It rebuilds those from
  `WorldMap::height` at the chunk grid's vertices, with `terrain.cpp`'s diagonal. No origin may stand above the
  ground: 0.05 m tolerance, 0.15 m for a cushion's dome centre and 0.2 m for a fan's tilted foot on a 70° wall. No
  origin may be deeper than its kind allows.

It fails on the old world.

## 4. Frame time

Machine: M2 Max, 1920x1080, target 30, `effects_first`. Every run was under `tools/gpu-lock.sh`.

THE RIFT LIVE has live input, so these runs used a scratch copy with Trench as its audio and the Response block kept
(`sonic.live` off), as in `live-30fps-report.md`.

**Live:** `--live-profile --mode live --quality auto`, transport playing, 10 s warm-up, 20 s measured.

| Start | Before: level, frame p50/p95, GPU p50, misses | After: level, frame p50/p95, GPU p50, misses |
|---|---|---|
| 40 s | Low 0.71, 33.3 / 34.3 ms, 25.2 ms, 0.17% | Low 0.71, 33.3 / 34.2 ms, 24.6 ms, 0.5% |
| 96 s (the drop) | Low 0.71, 33.3 / 34.2 ms, 25.2 ms, 0% | Low 0.71, 33.3 / 34.4 ms, 24.3 ms, 0% |
| 150 s | Low 0.71, 33.3 / 34.3 ms, 24.7 ms, 0.17% | Low 0.71, 33.3 / 34.3 ms, 24.4 ms, 0% |

**It still holds 30 fps.** The controller settles at the same level as before.

**Headless, pinned High, at 96 s** (two interleaved runs each):

| World | GPU p50 |
|---|---|
| before | 25.4 / 25.9 ms |
| after (`lodLevels 1`) | 30.6 / 30.4 ms |
| after, but `lodLevels 4` / 70 m | 29.7 / 29.7 ms |
| after, `lodLevels 2` / 250 m | 29.9 / 30.1 ms |
| after, `lodLevels 2` / 150 m | 29.6 / 29.9 ms |

- The terrain's single level costs about 0.8 ms.
- The other 4 ms is the world that now exists where it used to stop. At the drop, the old map's edge at z = 1200 was
  inside the 1000 m view, and nothing was drawn past it. Now the canyon continues. That is 1,009 entities instead of
  587, and 4.0 M triangles instead of 2.2 M.
- That cost was always owed. The old frame was cheaper because it was missing part of the canyon.
- Live absorbs it at the same level. High is still not held live, which was already true (`live-30fps-report.md` §6).

The ADR-1344 limits are kept and checked by `test_rift`'s ADR-1344 case:

- the live-only LOD rungs;
- the 30 m sea-pen cutoff;
- the ecosystem host cull.

The Response wiring (ADR-1361) is untouched: the projects are byte-identical.

## 5. Tests

- `python3 tools/check_project_integrity.py`: 0 problems.
- `build/release/tests/avgen_tests "[rift]"`: all tests passed (590 assertions in 9 test cases). Exit code 0.
- `tools/ci/run-suite.sh cpu-assets`: PASS, exit code 0.
  - 4,243 passed, 0 failed, 19 skipped, 1 expected failure.
  - 4,263 of 4,263 ran, in 18 m 12 s.

The GPU suite was not required. No engine or shader code changed; the one C++ change is a test in `avgen_tests`.

## 6. Media (`~/Desktop/av-gen-review/53-rift-grounding/`)

- **`pairs/`:** before | after at matched times and cameras.
  - Opening: 1, 4 and 12 s. These are the owner's start-of-scene reports.
  - Flight: 40, 96 (the drop), 150 and 200 s.
  - Low eye: 4.5 m over the flight line, 13-14 m to either side over the banks, 50° lens, bank and drift off. Right
    bank at 30 and 60 s, left bank at 20 and 110 s.
- **`diag/`:** the full-size stills behind the pairs.
- **`opening-before.mp4`, `opening-after.mp4`:** the first 14 s of THE RIFT LIVE, `--render-limits live`, on Trench.
- **`owner-report-start-of-scene.png`:** the owner's report.

All stills are `--tier realtime --render-limits live` at 1920x1080, so they show the live picture.

## 7. Files

- **`examples/bioluminescent/land.py`:** the map's extent (`SIZE_Z`), the floodplain, the mesh-exact
  `Ground.height`, and `world(floodplain=False)` for the layout ground.
- **`examples/bioluminescent/grounding.py`** (new): the seat fit, the grounding/river/world checks, and the eye
  corridor with its clearance.
- **`examples/bioluminescent/build.py`:**
  - layout vs drawn ground in `place`;
  - the corridor step-out for crinoids;
  - `seat()`;
  - `SINK`;
  - `lodLevels: 1`;
  - the asserts.
- **`tests/integration/test_rift.cpp`:** the engine-side grounding case.
