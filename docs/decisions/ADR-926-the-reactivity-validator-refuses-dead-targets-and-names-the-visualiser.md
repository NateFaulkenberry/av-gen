# ADR-926: The reactivity validator refuses a dead target and names the visualiser

**Status:** Accepted
**Date:** 2026-09-26
**Follows:** ADR-756 (validate then compile the feasible part), ADR-902 (route and parameter liveness is
one registry), ADR-900 (the route chain), ADR-925 (the reactive catalogue); amends ADR-902's rule table
**Found by:** the GV3 revision's Director audit (recommendation 2: "have the validator refuse targets
known to be dead ... and flag a plan where everything reacts to the kick"), the owner's brief sections
3, 5 and 16 ("Hero effects should not all use identical timing or amplitude"; "Avoid making everything
respond simultaneously with identical timing"), and the stream reports' traps
**Implemented by:** `directing::validateReactivity`, `routeExcursion`, `targetOwners`
(`src/directing/reactivity.*`), called by `validatePlan`; the issue codes DEAD_TARGET, ONE_SOURCE,
ONE_PHASE, OVER_SATURATED, PHASE_RATE_TRAP, ROUTE_HAZARD (`src/directing/issue.*`); the registry rules
`layer-emits-nothing` and `no-field-named` (`src/params/liveness.cpp`,
`scene::SceneLivenessFacts::deadTarget` in `src/scene/route_liveness.cpp`)
**Tests:** `tests/unit/test_reactivity_validator.cpp` (`[adr926]`): each refusal and flag with the case it
must catch and the neighbour it must let through

## Context

The route chain and the liveness registry (ADR-900, 902) made a route's reach measurable. What a
planner writes needs the same question asked before anything compiles -- and then the one no rule
asked: does the plan, as a whole, sound like the world listening, or like a visualiser?

## Decision

**Refused: a route that cannot reach the picture.** Every plan route is put through
`Registry::standard().checkRoute` with the scene's facts (plus the signals the plan's own sources will
publish, and `checkSet` against the project's routes as they will stand), and every dead finding is a
DEAD_TARGET error naming the rule: the item is blocked, not compiled, and the diff says why. An unknown
target is answered with the catalogue's nearest paths.

**Two rules added to the registry**, because the validator refuses through it and it had no rule for:

| id | applies to | verdict | how it is decided |
|---|---|---|---|
| `layer-emits-nothing` | route, track | dead | `nodes/<terrain>/scatter/<layer>/emissionGain`, `hueOffset` or `emissiveFieldAmount` on a layer with no emissive colour and no program that writes emission (ADR-905 registers the three lanes for every layer) |
| `no-field-named` | route, track | dead | a scatter layer's or a procedural node's `emissiveFieldAmount` whose owner names no field, or a field the scene does not have |

**Flagged (warnings; the item still compiles):**

| code | when | why |
|---|---|---|
| ONE_SOURCE | three or more routes, and 60% or more follow one signal | "everything pulses to the kick" reads as a visualiser; each musical layer should have its own owner |
| ONE_PHASE | three or more routes on one source reach their targets at one instant (delay + attack), or every route of the plan does | lockstep; a response that travels needs `delayMs` |
| OVER_SATURATED | an entity pulses to more than two signals -- events, scored pulses, LFOs, counting the project's routes beside the plan's -- or one route, or the routes stacked on one target, can take it past its catalogue safe range | it cannot read as answering any one layer; or the look breaks. A slow arc (section energy, a band's level, a timeline keyed by section) sets the level the pulses ride on and does not count |
| PHASE_RATE_TRAP | the liveness registry's `phase-rate` finding | moving a rate jumps the pattern by elapsed time x change |
| ROUTE_HAZARD | any other liveness hazard; a one-frame event (`music.*`) or an unsmoothed level (`audio.rms`, `audio.peak`) as a depth source; a hue driven by `audio.*`/`music.*`/`beat.*`; a multiply on an event source whose rest output is not 1 | the stream reports' traps: an event depth holds depthMin except on its frame, `audio.rms` flickers, a hue from audio reads as noise (key it by section), a bare multiply darkens its target between hits |

**A plan source must be pure in time** (LFO, noise, timeline): an envelope or a random source is
refused (NON_DETERMINISTIC), its parameters must be ones it registers (SCHEMA_INVALID with the nearest
leaf), its name must not collide with a source the plan did not make (DUPLICATE_KEY), and a route
reading any output of a refused source is BLOCKED with it.

## Consequences

- **On the glade:** routes onto the stones' glow, the shelf's light wave, a misspelt target and an
  unknown signal are refused (`layer-emits-nothing`, `no-field-named`, `unknown-target` with
  `nodes/elder-gills/emissiveBoost` suggested, `unknown-source`) and not compiled; their neighbours on the
  fungi, the fungi's wave and the elder's gills compile. Five routes on `audio.onsetLow` are flagged
  ONE_SOURCE (5 of 5); the same targets on four layers are not. Three downbeat routes at one instant are
  ONE_PHASE; at 0, 120 and 240 ms they are not. A hero pulsing to four signals is OVER_SATURATED, one
  pulsing to two is not; a +2.0 kick on the fungi's glow is past its safe range, +0.3 is not, and +0.3
  on two sources stacked is. `scene/wind/gustSpeed` is a PHASE_RATE_TRAP, `gustAmount` is not. The
  default proposal (ADR-927) raises none of these.
- **Glowmere Valley 3:** the default proposal raises no error and no ONE_SOURCE or ONE_PHASE. Beside
  GV3's existing routes it raises OVER_SATURATED on 8 heroes: every cap and tissue already pulses to the
  shared `lfo.breath` (all ten heroes in lockstep) and the `timeline.gap` dips, and the elder also to
  `timeline.kick` on its material, so one planned layer makes three. With those four shared-material
  routes removed (the gap dips kept), the proposal on the tuned scratch copy raises nothing.
- **The two new registry rules change no shipped project's audit**: no project in the repository routes
  a scatter lane or a light-wave depth yet. `--audit-routes` lists 25 rules instead of 23.
- The validator judges configuration only. Whether a route is visible, and whether it helps, stay the
  evaluator's (the owner's behavioural and meaningful tiers); ADR-927's GPU case is the behavioural
  evidence for the proposal's kinds.
