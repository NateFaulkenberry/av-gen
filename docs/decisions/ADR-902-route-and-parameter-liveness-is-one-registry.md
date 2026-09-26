# ADR-902: Route and parameter liveness is one registry, asked at bind, at load and by `--audit-routes`

**Status:** Accepted
**Date:** 2026-09-26
**Follows:** ADR-179 (a program that asserts emission owns it), ADR-207/702 (effects and their
activation), ADR-900 (the route chain), the owner's brief §16 (configured, behavioural, meaningful)
**Found by:** the GV3 revision's audits (`reports/modulation.md` §4, §7 B; `reports/mushrooms-wind.md`
§3-4; `reports/director.md` recommendation 2), which found by reading code what had each been found by
a person looking at a frame: event routes whose attack swallowed the event, `emissiveBoost` on nodes it
could not affect, speeds whose phase is time x speed, arcs on program outputs nothing reads.
**Implemented by:** `params::liveness` -- `Registry` and its rule table, `Facts`, `BusFacts`,
`eventPassThrough`, `flatChain`, `checkSet` (`src/params/liveness.*`); `Modulator::bind` and
`firstReport` (`src/params/modulation.*`); `scene::SceneLivenessFacts`, `phaseRateTable`,
`auditProject`, `routeAuditToJson` (`src/scene/route_liveness.*`); `Engine::livenessInputs`,
`auditRoutes`, `reportRouteLiveness` (`src/app/engine.*`); `app::runRouteAuditCommand`
(`src/app/route_audit_cli.*`) and `--audit-routes` (`src/app/main.cpp`, `application.cpp`)
**Tests:** `tests/unit/test_route_liveness.cpp` (`[adr902]`): every rule with a case it must flag and a
neighbour it must not; `tests/integration/test_route_audit_cli.cpp`: the report of a project on disk

---

## Context

This codebase keeps shipping routes, tracks and parameters that bind with no warning and never reach
the output. The family is old (`docs/visual-quality.md` §8) and the GV3 audits found four more
members in one film. Each was found by a person, late, and the fix for each was local, so the next
one was found the same way.

The owner's brief asks the evaluator to tell three tiers apart: a parameter **configured** (connected
to audio), a **behavioural** response (the render visibly changes), a **meaningful** one (the change
helps). The last two need renders. The first is a question about the project, and it has an honest
answer only if "connected" means "connected to something that can reach the picture". The director
audit's recommendation 2 asks for exactly that as a refusal list for the Director's validator.

## Decision

**One registry of rules in C++, queryable on its own, applied in three places.** Each rule has a
stable id, the verdicts it can give and a one-line summary, and returns findings with the evidence in
a sentence.

- **Verdicts.** *live*: binds, and no rule can show it failing to reach the picture (connected -- the
  configured tier; whether it visibly moves anything is the behavioural tier's question). *dead*: a rule
  shows it cannot reach the picture. *hazard*: it reaches the picture in a way that is almost certainly
  not what was meant.
- **What a rule may know comes through `Facts`.** `BusFacts` knows a bus and a parameter set -- what a
  bare `Modulator` has. `scene::SceneLivenessFacts` knows the scene: its programs, nodes, effects,
  shot spans, analysis track, sources and render rate. Facts are computed the first time a rule asks.
- **Where it is asked.**
  - `Modulator::bind` puts every route it resolves through the rules and **logs each problem once for
    the life of the modulator** (unresolved routes too, which every rebind used to log again). The
    engine lends it scene facts, except in the middle of a project load, where the scene is half-built.
  - `Engine::loadProject` ends with `reportRouteLiveness`: the whole project -- routes, tracks,
    effects -- logged once, and every dead finding added to `projectWarnings()`, except what the binds
    already report there (unresolved routes and tracks), what somebody chose (disabled), and what is
    about the session rather than the project (no audio loaded yet, live-only input).
  - `avgen --project <file> --audit-routes <out.json>` writes the whole report, headless and with no
    GPU. A validator asks `Registry::standard().checkTarget(path, component, facts)` before it proposes
    a route or a track.

### The rules

| id | applies to | verdict | how it is decided |
|---|---|---|---|
| `disabled` | route, track | dead | switched off, or the whole timeline is |
| `unknown-source` | route | dead | not a signal on the bus |
| `unknown-depth-source` | route | dead | ADR-900's depth source is not a signal on the bus |
| `unknown-target` | route, track | dead | not a registered parameter |
| `not-modulatable` | route | dead | the parameter's flags refuse modulation |
| `component-out-of-range` | route, track | dead | the component is past the parameter's last |
| `zero-amount` | route | dead | amount 0, or a depth range of [0, 0] |
| `flat-chain` | route | dead | the chain settles to one value for 17 inputs across the source's declared range (a threshold above it, a flat remap, a zero gain) |
| `silent-source` | route | dead | an `audio.*`/`music.*` source with no analysed audio (`beat.*` with no tempo either) |
| `live-only-source` | route | hazard | a `control.*` source in an offline render |
| `event-swallowed` | route | dead < 10% or gated, hazard < 50% | one event at the piece's typical strength (below) through the chain, sampled at the project's frame rate |
| `pulse-swallowed` | route | dead < 10%, hazard < 50% | a value-mode timeline's shortest pulse, fed as the frames see it, through the chain |
| `pulse-between-frames` | route | hazard (dead when all are missed at the project's rate) | a value-mode timeline's positive spans sampled on the `k / fps` grid at the project's rate and at 30 fps |
| `delay-over-ceiling` | route | hazard | `delayMs` above ADR-900's 4000 ms, which is clamped |
| `phase-rate` | route, track | hazard | the target is in the phase-rate table (below); a track only when its keys differ |
| `overridden-by-replace` | route, track | dead (hazard when the Replace has a depth source) | a later Replace route on the same component; tracks apply before routes |
| `program-has-no-emission` | route, track | dead | `material/<p>/emissionIntensity` where `<p>` writes no emission register |
| `emission-lives-in-layer` | route, track | dead | the same, where the glow is in a layer whose intensity is not a parameter |
| `program-owns-emission` | route, track | dead | `procedural/<n>/material/emissive[Color]`, `.../parts/<k>/emissiveGain|emissiveColor` on a surface whose program writes emission (ADR-179) |
| `program-not-uploaded` | route, track | dead | any parameter of a program past the GPU table's 8 slots |
| `program-unused` | route, track | dead | any parameter of a program no entity, procedural or SDF names |
| `node-emits-nothing` | route, track | dead | `nodes/<n>/emissiveBoost` where no surface of the node or its descendants emits |
| `effect-never-fires` | effect, route, track | dead | the activation cannot open: hero focus or camera travel with no matching shot span, a trigger with no event before the piece ends (the effects' own `TriggerClock`), a window outside the piece, an owner the scene does not have; routes and tracks to `fx/<id>/...` inherit it |

**The event rule after ADR-900.** An event's reach is the largest deviation from the chain's rest
after one event, over the deviation a held input at the same strength reaches. It is judged at the
piece's typical event strength -- the median of the track's onsets as the bus gives them, or of an
event-mode timeline's hits; the declared maximum when neither is known -- and a chain that answers a
full-strength event but gives none at the typical strength (a threshold between the two) is dead.
Under ADR-900's chain no attack or decay can trip it, because the chain shows every event's level in
full; what it catches is a threshold the piece's events never clear.

**`emissiveBoost` is judged by the semantics agent/emission lands in parallel**: one post-program
multiplier on everything a node emits, for every kind of node. On this branch the boost still reaches
only mesh entities, so between this merge and that one the rule under-reports: a program-lit or
procedural node that emits is called live though today's boost cannot reach it. After that merge it is
exact. A node is dead only when nothing it or any descendant draws emits: its entities' and
procedurals' programs write no emission (base or layer), their own emissive is zero or black, they are
drawn lit, and a particle node's `emissive` is 0. SDF nodes are never flagged (the rule cannot see their
emission).

### The phase-rate table

A rate whose phase is `absolute time x rate` jumps its pattern by `t x Δrate` when it moves: at 180 s a
0.01 change of a colour-cycling speed turns the hue 1.8 turns. The table (`scene::phaseRateTable`) was
built by reading every shader and every CPU use of the render clock for a time-times-parameter term,
tracing each uniform back to the parameter that fills it, and checking a sample by hand (the wind's
flutter, particle pulses, the Pulse effect's cycle, fields' `speed * t + phase`, Color Cycling's hue).
It has 82 entries: LFO rate and beatsPerCycle, noise rate, timeline scale; the wind's gust and
turbulence speeds and scales and region drift; `scene/windSpeed` (only where wind-body meshes exist:
their leaf flutter is `sin(phase + t * (5.5 + 3 * strength))`); volume noise speed; camera shake
frequency; the day-night cycle length; tree energy pulse, propagation and shimmer; water flow speed;
particle pulse, pause and turbulence rates; procedural deformer, field and SDF speeds; hover, drift and
sway rates; and 53 effect rows across comet, meteors, aurora, vortex, fog, tornado, surface waves, the
entity lanes, the motion effects, shells and distortions. Secondary scales (a wavelength, a pattern
scale) count only while their primary rate is non-zero. Entries are matched against a path's last
segments, so a nested composition's `nodes/<child>/scene/...` copy is caught.

**Checked and safe** (integrated over dt, differenced, or an amplitude): orbit and rotation speeds,
exposure and focus speeds, envelope and random times, entity bounce, spin, orbit, wander and turn rates,
particle `spawnRate` and `orbit`, the fx emitter rates, `scene/wind/flutterScale` (spatial phase only --
the audit report listed it, the code does not bear it out), `regionScale`, `gustAmount`, `turbulence`,
`windDirection`, `volumeNoiseScale`, every `phase`/`offset` row, and deformer/field/SDF frequencies.
**Not in the table because they depend on content:** a material program op whose constant multiplies
the Time input, and a shader layer input the layer multiplies by `sys.time`.

### The report (`avgen --audit-routes`)

    avgen --project <project.json> --audit-routes <out.json | -> [--fps <n>] [--log <level>]

`-` writes the JSON to stdout and the summary to stderr. `--fps` overrides the project's render rate
for the sampling rules. Exit codes: 0 written, 2 no `--project`, 3 the project did not load, 4 the file
could not be written. Schema (format `avgen-route-audit`, version 1):

    {
      "format": "avgen-route-audit", "version": 1,
      "project": string,                 // the path as given
      "tier": "configured",
      "frameRate": number,               // the rate the sampling rules used
      "durationSeconds": number,         // the piece's length (0 = unknown)
      "hasAudio": bool,
      "verdicts": {"live": string, "dead": string, "hazard": string},
      "rules": [{"id": string, "appliesTo": string, "verdicts": string, "summary": string}],
      "summary": {
        "routes" | "tracks" | "effectDefaultRoutes" | "effects": {"total", "live", "dead", "hazard": int},
        "findingsByRule": {<rule id>: int}
      },
      "routes": [{
        "index": int, "origin": "project"|"entity"|"graph"|"macro",
        "source": string, "target": string, "component": int, "op": string, "amount": number,
        "enabled": bool, "delayMs": number, "attackMs": number, "decayMs": number,
        "sourceIsEvent": bool,
        "eventPassThrough"?: number,     // event sources: reach at the judged strength, 0..1 (0 = none)
        "typicalEventStrength"?: number, // event sources whose typical strength is known
        "depthSource"?: string, "depthMin"?: number, "depthMax"?: number,
        "verdict": "live"|"dead"|"hazard", "reason": string,
        "findings": [{"rule": string, "verdict": "dead"|"hazard", "reason": string}]
      }],
      "tracks": [{"index", "target", "component", "mode", "timeBase", "keys": int, "enabled",
                  "verdict", "reason", "findings"}],
      "effectDefaultRoutes": [{<a route's fields without "origin">, "effect": string,
                               "effectType": string, "installed": bool, "verdict", "reason", "findings"}],
      "effects": [{"index", "id", "type", "owner", "activation", "enabled", "verdict", "reason", "findings"}]
    }

`reason` is the first finding with the entry's verdict, or "no rule found a way for it to miss the
picture". `index` is the entry's position in the modulator's routes, the timeline's tracks or the
project's effects. An effect default route is what the Add-Effect gesture would attach; `installed` says
whether the project already has one with the same source, target and component.

## Consequences

- **Glowmere Valley 3** (`av-gen-gv3`, read-only): 33 routes (11 project, 22 reactions), 71 tracks,
  7 effect default routes, 2 effects. **3 dead tracks**: the arcs on
  `material/glowmereFirefliesCrown/emissionIntensity` and `...Scaled/emissionIntensity`
  (`emission-lives-in-layer`: the glow is the `fireflies` layer's) and on
  `material/paintedGround2/emissionIntensity` (`program-has-no-emission`). **3 hazardous routes**
  (`pulse-between-frames`): `timeline.kick -> material/glowmere2TissueWarm/emissionIntensity` (475
  pulses of 25 ms: none missed at 60 fps, 109 missed at 30 fps) and the two `timeline.gap` dips (4
  pulses of 16.7 ms: 3 missed at 30 fps). Everything else is live. **A scratch copy of the project
  with `kick`, `gap` and `crash` switched to `"mode": "event"` has no hazard left** (33 routes live);
  the three dead arcs remain.
- **Glowmere Valley 2 multicam**: 64 routes (40 project, 24 reactions), 8 tracks, 23 effect default
  routes, 18 effects, **all live**. The audit report's GV2 findings were each answered elsewhere: its
  eleven swallowed event routes by ADR-900 (every event route now arrives in full); its `emissiveBoost` routes on the
  elder's program-lit gills and cap by the post-program boost this rule is written to; its hero pulses
  fire because the project carries 45 shot spans.
- **Every project load now logs** its dead and hazardous routes, tracks and effects once, and adds the
  dead ones (outside the binds' own reports) to `projectWarnings()`.
- **A rebind no longer logs an unresolved route again**; the bind's returned error still names every
  failure every time.
- **Cost.** A bind samples each bound route's chain: 17 settled samples, and for an event source one
  event through its attack (at most a few hundred `process` calls). The scene facts are built once per
  question. GV2 multicam's whole audit, project load included, runs in 4.6 s headless; the load is most
  of it.
- **What it does not judge.** Behaviour and meaning (the evaluator's renders); a scene file's effects
  replaced by the project's `effects` list (GV3's twelve), since the report reads the running project;
  terrain settings overridden by an authored program; the two content-dependent phase-rate cases above.
