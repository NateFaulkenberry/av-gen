# ADR-295: The world was sampled twice, and a grid that answers for terrain has to prove it may

**Status:** Accepted
**Date:** 2026-09-18

`docs/character-ai-plan.md` §P7 item 1 asks for the largest optimisation in the document: *steer and
validate against the grid rather than the analytic world*, because `steer` is 59.135 µs and
`pathValid` is 74.481 µs where a grid lookup is 0.024 µs. An earlier agent looked at exactly this
and wrote it down rather than doing it:

> The baked grid answers at 0.024 µs against ~10 µs, **but at 4 m cell resolution where `pathClear`
> works at 0.25 m**, so a walker routed by it takes a different line past a trunk.

That objection is correct, it is the whole problem, and this ADR is what came of taking it
seriously. The answer is in three parts, and only the first is unconditional.

---

## 1. Half the cost was the same question asked twice

Before deciding anything about resolution, the analytic query was decomposed. It had never been.

`tools/charai_probe.cpp`, section `n`, Glowmere, minima of 3, **on walkable ground** — points that
run the whole rule ladder rather than bailing at rule one, which is what the §E primitives table
sampled and why its `Navigator::sample` reads 10.3 µs where a walker's reads 15.9:

```
WorldMap::height                        1.644 µs
WorldMap::normal (4 heights)            5.941 µs
WorldMap::waterSurface                  0.097 µs
WorldMap::sample (the lot)              7.810 µs
ClearanceField::canopyHeight            8.136 µs   <-- takes its own WorldMap::sample
TerrainQuery::at                       15.923 µs   <-- 7.810 + 8.136, to the microsecond
```

`TerrainQuery::at` sampled the map, then called `canopyHeightAt`, which sampled the map again at the
same point with the same epsilon. The canopy is a function of altitude, slope and moisture, and
those three numbers come out of `WorldMap::sample` and nowhere else — so the second sample was the
first one done over.

`ClearanceField::canopyHeight` and `TerrainQuery::canopyHeightAt` now take an overload accepting a
sample the caller already holds; `TerrainQuery::at` and `ClearanceField::minimumHeight` pass theirs.
Identical arithmetic on identical inputs. Measured, same machine, same session:

| | before | after |
|---|---|---|
| `Navigator::sample` | 10.461 µs | 5.564 µs |
| `Navigator::steer` | 59.754 µs | 34.853 µs |
| `Navigator::pathValid` | 75.026 µs | 41.726 µs |
| `NavGrid` build (23,716 cells) | 244.3 ms | 148.0 ms |

**No route can move**, and that is not a claim about care taken, it is a property of the change: the
expression `canopyHeight(p)` was replaced by `canopyHeight(p, s)` where `s` is the sample
`canopyHeight(p)` would have taken. It is the only part of this ADR that is on unconditionally,
everywhere, in every world.

*A caveat the decomposition table's own control caught.* In the after-run `TerrainQuery::at` reads
5.759 µs against `WorldMap::sample`'s 6.392 µs — a composite cheaper than a part of itself, which
cannot be true. The run was taken at a load average of 8.21 and the two arms are 10% apart; the
control says so rather than letting the table read as precision it does not have.

## 2. The resolution gap is not where it was thought to be

The objection above says *trunk*. Trunks are not the problem, and the measurement says so.

`pathClear` asks two independent questions. **Solids** — trunks, rocks, heroes — are answered by
`ObstacleField::segmentBlocked`, which is a swept test against a spatial hash: exact, continuous, no
resolution at all, and **0.018 µs** for a six-metre segment. **Terrain** — bounds, slope, water,
thicket, hero — is answered by sampling the analytic world every 2.5 m at 5.6 µs a sample. The
terrain half is 99.9% of the cost, and the trunk half was never in the grid's hands and is not now.

So the grid is asked only about terrain, and only where it has room to be sure. `NavGrid` gains:

* `NavTerrain`, a per-cell flag meaning *this ground is standable whatever is standing on it*, read
  off the reject reason (`Navigator::sample` runs terrain rules first and the obstacle field last,
  so a cell that got as far as `Obstructed` passed every terrain rule).
* `terrainRoom`, a Chebyshev distance transform over that mask: how many cells of standable ground
  surround each cell. Two passes, 23,716 cells, and it is not an approximation of a Euclidean
  distance — it is the exact answer to the question the segment walk asks.
* `segmentTerrainClear(a, b, trustCells, maxRisePerMetre, maxSlope)`, which walks the cells a segment
  touches and answers **clear** or **cannot tell**. It never answers *blocked*. That asymmetry is the
  safety argument: the grid can skip work, never decide against it.
* `groundAt`, a bilinear ground for the obstacle filter's `footY`.

Three findings paid for by getting it wrong first:

**The dominant disagreement was not terrain at all.** Of 876 segments in 20,000 where the grid said
clear and the world said blocked at 40 m reach, **807 were "blocked by a solid"** — `pathClear`'s
swept test runs with the walker's foot at the segment's *start*, while its per-point tests use each
point's own ground, and over forty metres of Glowmere the ground moves by metres and the two
classify the same boulder differently. Running the point test in the fast path too, on the grid's
interpolated ground, took that term from 807 to **1**.

**Walkable ground is far steeper than the step test allows.** A cell at the walkable slope limit of
0.55 is a 63° face; 2.5 m of it rises 5 m against a step height of 1.4. So a route over ground that
is *walkable* fails the fine check on every second sample. `gridSlopeGate` gates on the slope at
which one sample spacing exactly consumes the step height — 59/255 of the walkable range — and it
halved the residual disagreements on its own.

**A margin in cells is not a margin.** `gridTrustCells` became `gridTrustMetres`, because expressed
in cells a *finer* grid silently bought a *smaller* margin, and the first finer-grid experiment
measured that instead of what it meant to.

## 3. The gap does not close, and the grid is made to say so

After all of the above, on Glowmere at 8 m of room, 20,000 segments of each shape:

```
reach  rule                grid ok  world ok   unsafe    missed   why
6      8 m of room           11802     16817       18      5033   too steep=4 no headroom=2 step=12
40     8 m of room            4167      6555       38      2426   too steep=11 no headroom=4 step=22
6      naive (control)       17615     16817      880        82   ...blocked by a solid=447 step=367
```

Eighteen in twenty thousand. And on the flat Character Intelligence Lab fixture, the same rule over
40,000 segments: **zero**, with 24 explorers walking bit-identical routes for ninety seconds —
`0.000000 m` at every horizon from one second to ninety.

Eighteen in twenty thousand is not small enough. Measured on Glowmere with the rule simply switched
on: **four of twenty-four bodies were somewhere else within one second**, 0.93 m apart, rising to
122 m by ninety seconds as the chaos compounded. A walker makes thousands of these calls a minute;
a rule wrong one time in eleven hundred is wrong several times a second across a cast.

**Making the grid finer does not fix it.** The measurement, with the margin held at 8 m so the
comparison is fair:

| cell size | cells | build | verdict |
|---|---|---|---|
| 4 m | 23,716 | 321 ms | wrong after 315 checked walks |
| 2 m | 94,864 | 886 ms | wrong after 805 |
| 1 m | 379,456 | 4,085 ms | wrong after 9,699 |

Sixteen times the cells and twelve times the build time moves the error rate by about the cell
count and does not reach zero, and it cannot: the walkable set is a threshold on an analytic field
with sub-metre content, and one of its five rules — the canopy thicket band — is not even
continuous. It steps wherever a biome weight crosses its 0.25 presence threshold, on a curve aligned
to nothing. **No lattice is sound for that at any resolution.**

So the rule is not asserted. `NavGrid::build` **tests it**, against the world, at build time:
a fixed stride over the walkable set, both segment shapes, the golden angle so the directions do not
stack, and it stops at the first line the grid and the world disagree about. `NavGrid::vouches()` is
what `Navigator::pathClear` consults, and the log says which way it went and why.

```
lab fixture (flat)   grid vouches: yes  (209 walks checked, 0 wrong)   build 0.9 ms
glowmere-valley-2    grid vouches: NO   (315 walks checked, 1 wrong)   build 321 ms
```

The stride, the angles and the reaches are fixed, so two builds of one world reach the same verdict
on any machine — a verdict that varied would make a bake irreproducible (ADR-091).

## 3a. The arm this was to be finished against

§P7's *done when* is the `explore` scaling arm of `tools/charai_probe.cpp`. It moves.

Two binaries — `main` at 9d5347c and this branch — run **alternately**, three rounds each, minima
elementwise over the three, each round itself a minimum of 3 runs of 120 frames (ADR-170). Alternating
rather than one after the other because four agents share this machine and the load average moved
between 12.5 and 23.7 during the twelve minutes this took; a before-block followed by an after-block
measures the machine as much as the change. Milliseconds per 60 Hz behaviour update for the whole
population, `glowmere-valley-2`:

| profile | 10 | 25 | 50 | 100 | 250 | 500 |
|---|---|---|---|---|---|---|
| `explore` before | 0.7542 | 4.0115 | 5.2484 | 15.7533 | 42.1765 | 75.5423 |
| `explore` **after** | **0.4501** | **2.2108** | **2.9821** | **8.7278** | **23.3028** | **42.3689** |
| | 1.68× | 1.81× | 1.76× | 1.80× | 1.81× | 1.78× |
| `wander` before | 0.2059 | 0.5595 | 1.5391 | 3.8984 | 6.8050 | 15.5175 |
| `wander` **after** | 0.1533 | 0.4179 | 1.0306 | 2.5405 | 4.8878 | 10.8002 |
| `nil` before | 0.0003 | 0.0007 | 0.0014 | 0.0033 | 0.0084 | 0.0168 |
| `nil` after | 0.0003 | 0.0007 | 0.0014 | 0.0033 | 0.0084 | 0.0167 |

**1.8× across the whole range, and every control holds.** `nil` — the harness floor, no behaviours —
is unchanged to the last digit, which is what says the win is in the behaviour and not in the loop
around it. `wander` moves 1.4×, less than `explore` and not nothing, which is what says the win is in
the *shared walkability query* and not in something `explore` does: `wander` reaches it only through
`pickDestination`. And the arm's own control, the furthest distance any body actually walked at
N = 50, is **7.26 m for `explore` and 2.14 m for `wander` in both binaries, and 0.00 m for `nil`** —
identical, which is the scaling arm quietly reporting that the simulation did not move.

Read against a 16.7 ms frame, using the slope of the upper half as §E.1 does: an `explore` population
crossed a whole frame at about **125** characters and now crosses it at about **219**. Both figures
are from this session on this loaded machine, and both are lower than a quiet machine would give;
the ratio is the part that travels.

**All of this is part 1.** Glowmere's grid refuses to vouch, so not one of these microseconds comes
from the grid.

## 4. What this means, plainly — including a number this ADR had wrong

* **Glowmere gets part 1 and nothing else.** Its walkability queries are 1.9× cheaper and every route
  is bit-identical. Its grid refuses to answer for its terrain and says so in the log.
* **`gridTrustMetres = 0` turns part 2 off entirely** and is what the probe's control arm runs.
* **A world that qualifies gets about 1.4×**, and the first draft of this ADR said 3.97×. That figure
  was Glowmere with the gate bypassed, not the lab fixture, and quoting it here was the wrong number
  in the wrong place. Re-measured on the lab, three runs, minima, load 26.6:

  ```
  analytic only   75 ms of simulation, furthest body 149.73 m
  grid-assisted   54 ms                furthest body 149.73 m   (1.39x)
  ```

**And the deflating part, which is the honest reading.** The grid buys most where the analytic query
is dearest, and the analytic query is dearest where the terrain is rich — which is exactly where the
self-check refuses. The lab's world is flat with one river and no noise layers, so its
`WorldMap::height` is nearly free and there is not much for the grid to save. **The speed-up the gate
permits is smallest in the worlds that pass it.**

That does not make the mechanism worthless — it is 1.4× of a walking cast for free and exactly zero
change to the picture, and an authored interior or a set-dressed flat stage is a real shape of scene.
But it does mean part 1 is the win in this ADR and part 2 is the groundwork, and anyone reading this
to decide where to spend the next day should spend it on `WorldMap::height` rather than on the grid.

The thing this rejects is still worth naming: an optimisation that is 4× faster and moves a character
a metre in the first second is not an optimisation, it is a content change wearing one. The engine now
holds the fast path *and* the measurement that says when it may be used, and the measurement is taken
by the build rather than by whoever last remembered to.

## 5. What would change the answer

Terrain whose walkability is smooth at the grid's scale, which is what an authored world is and what
the flat lab fixture already proves the mechanism handles. Or a canopy that is a continuous field
rather than a thresholded biome weight — that one rule is the reason no resolution is sound, and it
is a decision in `ClearanceField`, not in navigation.
