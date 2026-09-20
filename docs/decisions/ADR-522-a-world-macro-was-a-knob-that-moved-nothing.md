# ADR-522: A world macro was a knob that moved nothing

- Status: Accepted (2026-09-20)
- Extends ADR-028 (graph routes), ADR-088 (entity reactions), ADR-182 (a probe that cannot fail).

## Problem

`app::WorldMacro` is a named 0..1 knob with a list of parameter targets, each remapped `min..max`
through a curve. It is exactly what a continuous cross-cutting control needs to be, it serialises,
it appears in the World panel, it takes part in cue presets — and on a freshly loaded project its
routes did not exist.

Found while building the Season Shift effect, which is one macro with 32 targets. The season
rendered to sequence hash `8b0100eb5f199e12` at `macros/season` = 0.0, 0.45, 0.75 **and** 1.0 —
the same bytes at every value of the only control the effect has.

The load log was the evidence:

```
project 'season-1.0.json' loaded: 686 parameters, 0 routes, 2 sources, ...
```

Zero routes, for a project whose macro declares 32 targets. And the same measurement on a project
nobody was suspicious of:

| project | authored routes in the file | macro targets | routes after load |
|---|---|---|---|
| `examples/machine/machine.json` | 12 | 8 (3 macros) | **12** |
| `examples/weather/season.json` | 0 | 32 (1 macro) | **0** |

## Cause

`Engine::loadProject` installs the macro routes at `applyWorldMacros()`, and then runs its
**second** `params::loadProject` pass sixty lines later. That pass replaces the authored routes:

```cpp
std::vector<ModRoute>& live = modulator.routes();
live.erase(std::remove_if(live.begin(), live.end(),
                          [](const ModRoute& r) { return !r.fromGraph && !r.fromEntity; }),
           live.end());
```

A macro route is neither `fromGraph` nor `fromEntity`, so it is erased — installed, bound, and
gone thirty lines before the log line that counts it.

The comment directly above that predicate describes this defect, in the past tense:

> clearRoutes() here used to take everything, which meant a scene's graph routes (ADR-028) and an
> entity's reactions (ADR-088) were installed when the composition attached and deleted a few
> hundred lines later by the project's own second parameter pass — bound, counted in the log, and
> then gone, which is exactly the shape of failure this codebase keeps shipping.

It was found twice, fixed twice for the two owners that were known, and the third owner was never
added to the list. That is the actual lesson: an allowlist of subsystems is a list that goes stale
silently, and every new owner is a fresh instance of the same bug.

## Decision

`ModRoute::fromMacro`, set by `WorldMacro::routes()`, third in the family and named the same way as
the other two. It is honoured in both directions:

* `loadProject`'s replacement keeps it, so the knob survives the second parameter pass.
* `saveProject` skips it, for the reason the other two are skipped: the owner rebuilds its routes on
  every load, so writing them would give a project a duplicate of every macro route each time it
  was saved, and a route the author cannot delete because the thing that owns it puts it back.

Measured after:

| project | routes after load |
|---|---|
| `examples/machine/machine.json` | 12 → **20** |
| `examples/weather/season.json` | 0 → **32** |

## Verification

Two cases in `tests/unit/test_serialization.cpp`, both able to fail:

* `loadProject keeps the routes a subsystem installed and drops the rest` — four routes in, one of
  each provenance plus an authored one; asserts all four are present *before* the load, then that
  the three owned ones survived and the authored one was replaced.
* `saveProject does not write the routes a subsystem owns` — two in, one out.

The test that would **not** have caught this is worth naming, because it existed and passed: the
season macro's 32 target paths were already checked to resolve to registered parameters
(`tests/unit/test_weather_examples.cpp`). Every path was real. Reachable is not the same as
connected, which is the same lesson §77 draws about findable, one layer down.

## Other projects this fixes

`examples/machine/machine.json` (MACHINE ENERGY, TURBULENCE, HEAT),
`examples/infinite/infinite.json` and `examples/reassembly/reassembly.json` all ship world macros
and all of them were knobs that moved nothing.
