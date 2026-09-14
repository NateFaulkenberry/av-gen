# Phase 3 — the vegetation

The brief's Phase 3 asks for habitat fields, macro-clustering, species-specific suitability, spatial
constraints and deterministic generation. The budget made it ask for one more thing: **a visibly
richer, better-composed valley at the same or better frame time**, because the original Glowmere
already sits close to the 16 ms ceiling and there is no headroom to spend.

**Result: the opening shot went from 19.99 ms to 13.44 ms — a third faster — and the frame it
renders is better composed than the one it replaced.** How that happened is the finding.

---

## 1. What was built

| | |
|---|---|
| `world::ScatterLayer::{min,max}HeightAboveWater`, `heightAboveWaterFeather` | the riparian band (ADR-174) |
| `world::scatter` two-pass thinning | `maxInstances` thins uniformly instead of truncating from −Z |
| `tools/make_glowmere_valley_2.py` `HAR_BANDS` | the ladder, 12 of 13 layers |
| `tools/make_glowmere_valley_2.py` `BUDGET` | screen-radius and view-distance floors, 10 layers |
| `tools/make_glowmere_valley_2.py` `clearings` | 10 glades: camera, heroes, the river lane, one meadow |

## 2. The riparian ladder

Every layer is banded on **height above the water table** — the ground minus the surface of the
nearest water course, extrapolated past its bank. The reasoning for the axis is ADR-174; the ladder
is:

| zone | layers | HAR band |
|---|---|---|
| bank gravel | pebbles | −1.5 – 4 m |
| wet floor | fungi, flowers, ferns, fan-plants, grass | 0.1 – 11 m |
| corridor landmarks | beacons, shelf-fungi | 0.4 – 11 m |
| transitional | bushes, canopy, deadwood | 1.5 – 46 m |
| upland silhouette | pines | 11 – 95 m |

The bands overlap on purpose and every edge is feathered, so the result is a gradient with character
rather than five stripes. What it produces is visible in `build/glowmere-valley-2/01-opening.png`:
dense groundcover on the valley floor, woodland climbing the transitional slope on the left, sparse
upland on the right shoulder.

Macro-clustering (Stage B) and association (Stage E) needed no new code — `clustering` /
`clusterScale` and `ScatterProximity` were already there and already in use, which `01-audit.md` §1.7
recorded.

## 3. The measurement that decides what Phase 3 is

Three interleaved runs, one session, GPU lock held, `pgrep avgen` clean either side (ADR-170).
1280×800, fixed t = 6.0 s.

| | opening frame | opening scene | triangles |
|---|---:|---:|---:|
| Phase 2, as inherited | 20.05 ms | 16.84 ms | 326,709 |
| **+ riparian ladder + coverage budget** | 19.99 ms | 16.71 ms | 305,964 |
| **+ negative space (10 clearings)** | **13.44 ms** | **10.81 ms** | 281,077 |

Read the middle row. **Banding thirteen layers and imposing screen-radius and view-distance floors
removed 6% of the scene's triangles and moved the frame by 0.3% — nothing at all.** Then taking big
geometry out of the *near field* removed a further 8% of triangles and took **a third off the frame**.

That is ADR-126's finding arriving from the other direction, and it is worth stating as a rule
because it inverts the intuition that a vegetation budget is about counting plants:

> **Removing small and distant instances is free and buys nothing. Removing large near ones is the
> entire budget.** A coverage budget that counts instances is measuring the wrong quantity; the
> question is always how many pixels a thing covers, and one tree at the frame edge covers more than
> ten thousand grass clumps on the far hillside.

The valley-axis view moved the other way, 11.34 → 12.78 ms, and that is **not explained**. It is
within the run-to-run spread seen elsewhere in this session (§5) and it is well inside the ceiling,
so it is recorded rather than chased.

## 4. Negative space is the composition *and* the budget

The first Phase 2 render of the opening shot had the camera standing inside a wood, glimpsing the
valley between trunks. The brief's §4.5 asks for open ground as a design feature; ADR-126 says near
coverage is the cost. **They are the same edit.**

`ScatterClearance::minHeight` is the right mechanism and its header says why: it clears the canopy
and leaves the ground growing, so the result is a glade rather than a bald patch — "clearing
everything instead produced a bald hillside with one tree on it". Ten of them: one where the opening
camera stands, one each for the Wanderer and the elder, six along the river as a lane in the canopy,
and one deliberate empty meadow on the valley floor so the rest of the frame has something to be
dense against.

The river lane is also the Auto-director's runway. Phase 6 needs a continuous path down the valley
that does not fly through trees, and it now has one that was cut for compositional reasons.

## 5. Against the budget

| | frame (median) | scene pass |
|---|---:|---:|
| Glowmere Valley 2, opening | **13.44 ms** | 10.81 ms |
| Glowmere Valley 2, valley axis | 12.78 ms | 9.63 ms |
| Glowmere (painterly), same session | 13.57 ms | 11.08 ms |

**Glowmere Valley 2 is at parity with the scene it succeeds, on a map with four times the readable
ground, and both are inside the 13–14 ms target.**

One honest caveat about the original's number. `03-baseline.md` recorded 15.93 ms and this session
recorded **13.57 ms** — matching ADR-151's published 13.566 ms exactly — and *both* runs passed the
`pgrep avgen` quiet check on both sides. So a 17% spread survives the quiet check, and ADR-170's
clause is necessary but not sufficient. Nothing here depends on the difference (every number is
inside the ceiling either way), but a comparison that turned on 17% would need the confounder found
first; thermal state is the obvious candidate and it is not instrumented.

## 6. What Phase 3 did not do, and why

- **Field-of-Neighborhood competition** (`02-research.md` §2.5) is not built. Not rejected on merit:
  the band plus the existing proximity and cluster field gave this scene the structure it needed, and
  adding a second unmeasured mechanism in the same phase would have made it impossible to say which
  one did the work. It remains the right next tool if individual spacing ever reads wrong.
- **Per-species Poisson spacing** likewise. The jittered grid's spacing is adequate at these
  densities and the measurement above says spacing is not where the frame goes.
- **Curvature-aware banks** — still outstanding from Phase 2, still a `Feature` field that does not
  exist.

## 7. Determinism

Unchanged and still asserted: placement is `noise::hashIndex(seed, cellId, channel)` throughout, the
new thinning pass uses a channel of its own, and the two-pass path is a pure function of the same
inputs. The original Glowmere's representation baseline reproduces **273,819 triangles exactly**,
which is the regression check `03-baseline.md` §6 asked for.
