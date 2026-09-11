# Generated worlds: budgets and measured cost

Milestone 12. Measured on **Apple M2 Max**, release build, headless, 1440×900, `--tier realtime`,
300 frames at a fixed 30 Hz simulation clock, no concurrent build or GPU work. Three interleaved
runs per configuration.

`--fps 30` is the simulation cadence, not achieved rendering speed. A reciprocal of a median is not
a sustained frame rate and none is claimed here.

## Measured

| | Authored Glowmere | Generated `glowmere.recipe.json` |
|---|---:|---:|
| Wall median (3 runs) | 25.22, 25.25, 25.26 ms | 13.38, 13.39, 13.40 ms |
| Median of medians | **25.25 ms** | **13.39 ms** |
| Candidate instances | 114 289 | 9 860 |
| Visible instances | 2 323 | 595 |
| Logical triangles | 15.1 M | 1.6 M |
| Draws / indirect | 110 / 154 | 53 / 126 |
| Shadow draws | 121 | 130 |
| Shadow cascades | 2 | 3 |
| Map area | 640 × 640 m | 420 × 420 m |
| GPU errors | 0 | 0 |

GPU pass medians for the generated world (sum 11.67 ms against an 11.60 ms GPU frame — independent
medians need not sum exactly): scene 8.13, volume 2.03, shadow 0.46, cull 0.33, ao 0.33, depth 0.13,
bloom 0.13, composite 0.07, tonemap 0.07.

The run-to-run spread is 0.04 ms, so these are stable numbers rather than a single sample.

**No speed claim is made against the figure published in `stylized-glowmere.md` (31.2 ms).** That
measurement predates several changes to this scene's environment handling and was taken under a
different methodology; the two are not a controlled comparison.

## What the difference actually is

The generated world is not faster because it is better optimised. It is **sparser and smaller**:
9 860 candidate instances over 176 400 m² against 114 289 over 409 600 m², which is 0.056 against
0.279 instances per square metre — a factor of five.

Two causes of that were found and fixed while measuring, and both were defects rather than tuning:

1. **Marsh covered 0% of every generated world.** With no water features on a generated map, the
   only source of moisture is the lowland term, and its default (0.35) never reaches the 0.55 that
   `composerBiomes`'s marsh requires. The composer went on assigning ferns, fungi, shelf fungi and
   beacons to marsh, so those layers were placed and had nowhere to grow. Coverage is now marsh 11%,
   forest 39%, meadow 28%, rim 22%.

2. **Biome preference ignored size.** Every flora species was assigned forest-first, which treats
   grass like a tree. Glowmere authors its grass meadow-first (0.48 meadow, 0.34 marsh, 0.22 forest)
   and its canopy forest-first. Preference now depends on the depth band.

A remaining gap of roughly four times is **not** explained and is recorded as open. The likeliest
contributors, unverified: 22% of the generated terrain is bare rim; composed densities pass through
both an ecology weight and a band weight (each below 1) that Glowmere's authored per-biome densities
do not; and the ecological zones deliberately thin flora in two of their four kinds.

The composer now logs its terrain's biome coverage, because density is the product of a layer's
preferred density and the coverage of the biomes it grows in — so a world whose terrain is mostly
bare produces a sparse world from perfectly reasonable numbers, and the arithmetic that did it is
invisible unless something prints it.

## Budgets

These are the caps the composer applies per depth band (`tuningFor` in `world_composer.cpp`). They
are the mechanism the brief's §24 asks for, and they were already in place:

| Band | View distance | Min screen radius | Casts shadow | Max instances | Cluster scale |
|---|---:|---:|---|---:|---:|
| Foreground | 90 m | 1.4 px | no | 120 000 | 11 m |
| Midground | 220 m | 2.4 px | yes | 40 000 | 22 m |
| Background | 620 m | 1.0 px | yes | 6 000 | 48 m |

Three consequences of these numbers are load-bearing elsewhere and worth stating:

- **A hero must not stand further off than 90 m**, or the foreground the shot is composed from has
  already been culled and the frame is a large object across bare ground. The composer clamps its
  viewpoint to 82 m for exactly this reason (ADR-072).
- Foreground layers do not cast shadows. Ground cover casts shadows smaller than a shadow-map texel
  at the size it is drawn, so the cost is real and the result is not on screen.
- Background gets a *smaller* minimum screen radius than midground, deliberately: a ridge silhouette
  culled for being small leaves a hole in the horizon, whereas a fern that vanishes at sixty metres
  is a fern nobody was looking at.

## Water (ADR-099)

Measured on the same machine, release build, headless, the Glowmere project with the camera pinned
by `camera/position` / `camera/target`, 120 frames, GPU pass medians. **Four repeats per
configuration, interleaved and with the order alternated, and the minimum of the four kept**: other
agents' builds and GPU tests were running throughout, and contention can only ever make a pass look
slower, so the minimum is the honest floor. The blocked (non-interleaved) form of this measurement
was thrown away: it put whole busy stretches on one configuration and produced a "no water" scene
pass 1.2 ms *slower* than one with water in it, which cannot be true.

Two cameras, because water's cost is per water pixel and nothing else:

| camera | water's share of the frame |
|---|---:|
| `channel` — standing 4 m over the river looking along it | 7.6% |
| `surface` — a metre over the water, looking down the reach | 15.3% |

(Share measured by diffing captures with the water enabled and disabled, counting pixels that move
by more than 1/255.)

| camera | resolution | full | water, no floating layers | no water | **the surface** | **the floating layers** |
|---|---|---:|---:|---:|---:|---:|
| channel | 1440×900 (1.30 Mpx) | 16.45 | 16.38 | 17.63 | *not resolvable* | *not resolvable* |
| channel | 2880×1166 (3.36 Mpx) | 31.65 | 30.74 | 26.28 | +4.46 | +0.91 |
| surface | 1440×900 | 9.76 | 9.70 | 8.65 | **+1.05** | +0.06 |
| surface | 2880×1166 | 15.01 | 14.42 | 12.98 | **+1.44** | +0.59 |

All figures are the scene pass in milliseconds. The `surface` rows reproduced across two independent
sessions (+0.92 / +1.05 and +1.50 / +1.44), so those are the numbers to trust. The `channel` rows did
not: at 1440×900 the difference came out *negative*, and at 2880×1166 it came out three times the
per-pixel rate the `surface` rows imply. A river that is 7.6% of the frame costs something under a
millisecond, and this machine could not resolve it under the load it was carrying — which is the
honest statement, rather than picking whichever of the two runs agreed with expectation.

From the `surface` rows: **≈2.8 ns per water pixel at 3.36 Mpx** (1.44 ms over 514 k pixels), and
5.3 ns/px at 1.30 Mpx. The per-pixel rate falling as the target grows is the same slope this
renderer shows everywhere else (`renderer-2-backlog.md` records terrain at 8.30 → 2.88 ns/px over
0.32 → 5.18 Mpx), and it is why a small-window measurement over-states a fullscreen one.

**What to budget with**: a water surface filling the frame at the editor's canvas would be about
**9.4 ms**, extrapolating the per-pixel rate. Glowmere's river never comes close because it is a
seven-metre channel; a world whose shot is mostly lake would, and that is the case to measure before
authoring one rather than after.

Two costs that do *not* appear in the table:

- **The depth prepass.** A scene with water asks for it whatever else is on, because the surface's
  thickness — and so its shoreline, its transparency and its depth colour — is made from it. On
  Glowmere it is free: ambient occlusion and the shadow mask already required it, and `depth` is
  0.26 / 0.52 ms in every configuration above, water or no water. A scene with neither would pay
  that once.
- **The floating layers on the CPU.** `evaluateFloaters` for 320 instances over two bodies:
  **0.019 ms per frame** (`avgen_tests "[.water-cost]"`, which is a measurement, not an assertion).
  It was 0.70 ms before two fixes worth recording, because both are the same mistake. The first was
  `WaterBody::flowAt` calling `WaterCourse::flowAt`, `surfaceAt`, `alongAt` and `contains` in turn —
  four walks of the same polyline for four answers about one point. The second, and 98% of the cost,
  was asking `TerrainQuery::waterDepthAt` per instance per frame to keep pads off the shoals: that is
  a six-octave noise evaluation, and *how far across a reach there is water* is a property of the
  world, not of the frame. It is measured once now, into `WaterBody::wetted`.

## Not measured, and therefore not claimed

- Base-M2 behaviour, sustained thermal behaviour, and whole-shot interactive timing.
- Dedicated GPU memory. RSS is process high-water, not GPU allocation.
- Any figure at the target output resolution rather than 1440×900 (water is measured at both
  1440×900 and the editor's 2880×1166 canvas; nothing else here is).
- Water's cost at a camera where it is a thin ribbon. See the note in the water section: the machine
  could not resolve it under load, and no number is claimed.
- Temporal quality. Rendering every frame verifies execution, not the absence of shimmer, popping or
  LOD transitions in motion.
- Quality tiers remain manual. There is no adaptive performance controller.
