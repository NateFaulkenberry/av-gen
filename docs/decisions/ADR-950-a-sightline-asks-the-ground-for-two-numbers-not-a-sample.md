# ADR-950: A sightline asks the ground for two numbers, not a whole sample

**Status:** Accepted
**Date:** 2026-09-28
**Found by:** the QA pass's Glowmere Valley 3 investigation (`docs/qa-pass/perf.md`, W1)
**Related:** ADR-834 (characters' cinematic signals), ADR-349 (camera clearance), ADR-080 (sightlines)
**Implemented by:** `surfaceAt` in `src/world/camera_clearance.cpp`
**Tests:** `tests/unit/test_sightline_surface_cost.cpp` (`[adr950]`; the benchmark is `[.perf][adr950]`)

## Context

`world::heroSightline` marches nine rays from the eye to a subject and asks where the ground is every
2 m. It asked with `WorldMap::sample(p, 0.5)`, which also derives a normal (four more height
evaluations), the slope, the altitude and the moisture (a closest-point search over every water path),
and then used only `height` and `waterSurface`.

Until 2026-09-25 that mattered only at bake time. ADR-834 then made `Composition::publishCinematicSignals`
run `heroSightline` for **every in-shot character, every frame**, to publish `character.<name>.visibility`.
Its cost is linear in the distance from the camera to each character, so it is small in a close-up and
large in a wide. On Glowmere Valley 3 a `sample` of the headless frame put 7,436 of 7,437 main-thread
samples inside `Engine::update` in `publishCinematicSignals`, almost all of them in `WorldMap::sample`.

## Decision

`surfaceAt` asks for `max(map.height(p), map.waterSurface(p))` directly. `WorldMap::sample` computes its
`height` and `waterSurface` with exactly those two calls, so every sightline answer is bit-identical.
The test pins that premise: `sample(p).height` and `sample(p).waterSurface` equal `height(p)` and
`waterSurface(p)` bit for bit on a generated terrain and on a map with two overlapping waters.

## Measured

`[.perf][adr950]`, one sightline, mean of 24 subjects, same session, old and new builds back to back
(the result digest `b27d09046eeb9d4b` is identical in both):

| distance | before | after |
|---:|---:|---:|
| 20 m | 486 us | 134 us |
| 60 m | 1,492 us | 412 us |
| 200 m | 4,481 us | 1,229 us |

The frame-level effect on Glowmere Valley 3 is in `docs/qa-pass/perf.md`.

## Not done here

The visibility signal is still computed for every in-shot character every frame, although nothing in any
shipped project reads it. ADR-951 computes it only once something asks for it.
