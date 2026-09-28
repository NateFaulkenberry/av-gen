# ADR-951: A character's visibility is marched only once something asks for it

**Status:** Accepted by the QA pass; **needs the owner's acknowledgement** (it changes what an unread signal
reads). Revert this one commit to restore ADR-834's always-on behaviour.
**Date:** 2026-09-28
**Found by:** the QA pass's Glowmere Valley 3 investigation (`docs/qa-pass/perf.md`, W1)
**Amends:** ADR-834 (characters' cinematic signals). **Follows:** ADR-950.
**Implemented by:** `SignalBus::sought`, `Composition::publishCinematicSignals`
**Tests:** `tests/unit/test_signal_bus.cpp` and `tests/unit/test_cinematic_signals.cpp` (`[adr951]`)

## Context

ADR-834 publishes five signals per character every frame. Four are arithmetic. The fifth, `visibility`,
runs `world::heroSightline`: nine rays marched to the character with a terrain query every 2 m, so its
cost is linear in the distance to every character in frame. On Glowmere Valley 3's 73-shot cut, the main
thread's engine update tracks (in-frame characters x metres to them) with r = 0.975, and a wide with 18
characters at ~290 m spends 360 ms of a 389 ms frame on it. ADR-950 made each sightline 3.6x cheaper,
bit-identically; that left 13 of the 73 shots under 10 FPS.

Nothing reads the signal. ADR-834's own "Not done" says it is routed into nothing in a shipped scene, and
no project under `examples/` names a `character.` signal.

## Decision

`SignalBus::find` records every name it is asked for, hit or miss, and `SignalBus::sought(name)` answers
whether anything ever has. Every consumer of a bus signal reaches it through `find` (routes' sources and
depths, entity reactions and behaviours, staging, scene states, source triggers, the route panel, the
route audit), so "sought" is "something could read it". `publishCinematicSignals` marches the sightline
only for a character whose `visibility` has been sought; otherwise it publishes 0 for it. The other four
signals are unchanged, and a route that names `character.<name>.visibility` makes it computed from its
bind on.

A miss counts because a route can bind before the producer's first frame declares the name. Sought is
sticky: once asked, always computed.

## Consequences

- No shipped project changes its pictures or its simulation: nothing reads `visibility`, and ADR-834's
  invariant (the step never reads these signals) still holds and is still tested.
- **What changes:** an unsought `visibility` reads 0 on the bus and from `Composition::cinematicSignals`.
  A tool that reads bus values by id without `find` would see 0; none exists today.
- The first frame after a route naming it is added in the live editor reads 0 once, then real values.
  These signals are live-tier (ADR-834, ADR-091) already.

## Measured

Headless, 1280x720, three interleaved repeats per arm, median wall frame (ms) with FPS; `docs/qa-pass/perf.md`
has the full tables at both resolutions and the per-shot sweep.

| point | before (77ea4247) | ADR-950 | ADR-950 + 951 |
|---|---:|---:|---:|
| GV3 r7b s66, the worst wide | 402.8 (2.5 FPS) | 254.7 | 33.4 (30.0 FPS) |
| GV3 r7b s24, low and wide | 330.5 | 201.0 | 32.2 |
| GV3 r7b s27, a close-up | 33.0 | 26.4 | 23.4 |
| GV3 r7b s17, no characters in frame | 27.2 | 28.0 | 27.1 |
| GV2 multicam, Valley Wide | 66.3 | 39.7 | 28.0 |
| grove (no entities) | 16.4 | 16.8 | 15.6 |

Draws, triangles and instances are identical in every arm. `Engine::update` on the s66 wide goes from 366 ms to
3.2 ms.
