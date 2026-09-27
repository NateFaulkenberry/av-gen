# ADR-925: The reactive catalogue is generated from the engine's data, and never offers a dead target

**Status:** Accepted
**Date:** 2026-09-26
**Follows:** ADR-754 (capabilities are generated from engine data), ADR-902 (route and parameter
liveness is one registry), ADR-903 (the emission lane), ADR-905 (a scatter layer's lanes, the ecology
light), ADR-906 (a field timed from the latest event)
**Found by:** the GV3 revision's Director audit (`reports/director.md` recommendation 2: a
`ReactiveCatalog` in `CapabilityRegistry` -- emissive materials, particle systems, scatter populations,
effect fields, lights, atmosphere, wind) and its modulation and mushroom audits
**Implemented by:** `directing::ReactiveCatalog`, `ReactiveTarget`, `ReactiveHero`, `ReactiveGroup`,
`ReactiveKind`, `buildReactiveCatalog` (`src/directing/reactive_catalog.*`);
`CapabilityRegistry::reactive`/`setReactive` (`src/directing/capabilities.*`);
`app::reactiveCatalogFor`, and `sceneFactsFor` filling it (`src/app/directing_reactivity.hpp`,
`directing_context.hpp`)
**Tests:** `tests/unit/test_reactive_catalog.cpp` (`[adr925]`): every group on the glade fixture with
what moving it does; the neighbours it must not offer (a layer that emits nothing, a field-less light
wave, the terrain's own boost, a character's glow as a hero's); authored routes recorded; the water's
tears catalogued when registered and not while off

## Context

The capability registry said what characters, cameras, events and effects can do, and nothing about
what can answer the music. A planner had no list of targets, and a list somebody writes goes stale:
the GV3 audits found GV3's hand-written routes aimed at emission nothing read, at a speed whose phase
is time x speed, at a shared material that moved every hero in lockstep.

## Decision

**Generated, not listed.** Which targets exist is read from the registered parameter set, the
composition (nodes, heroes, scatter layers and their programs, fields, the light rig, staging) and the
effect list. What the engine does not carry is vocabulary -- that `emissionGain` is a glow and
`windSpeed` is motion -- so one table of path families classifies a parameter; the table names no
object of any scene, and every family has a case in the test.

**Every entry says what moving it does:**

| field | meaning |
|---|---|
| `group` | hero-emission, node-emission, material-emission, scatter-glow, scatter-hue, scatter-wave, node-wave, particles, effect, light, ecology-light, atmosphere, wind, water, water-tears |
| `kind` | luminance, hue, motion, density: what a viewer sees change |
| `level` | micro (a small, local thing: layers under 1 m, particles), meso (a hero, a cluster, a lamp layer, a wave, water), macro (the world's light, air, wind and colour) |
| `neutral` | where it rests: its base, the authored look (0 for a hue offset) |
| `safeMin`, `safeMax` | how far a route should take it, absolute: a hero's boost -40% .. +100% (the revision plan's +60%, +100% in the drop); a layer's glow -30% .. +40% and its hue +-0.08 of a turn (the emission stream: past +0.08 the teal clips); the ecology light x0.55 .. x1.45 (0.8 in a break to 2.0 at a drop on GV3's 1.4); particles' glow x0.7 .. x1.35 (GV3's own treble routes); wind x0.6 .. x1.4; a world effect x0.85 .. x1.2 |
| `op` | how a route should move it: an offset (a gain at 1, a hue) or about its base |
| `owner`, `hero` | what answers; the hero it belongs to |
| `emission`, `programLit` | how brightly it glows as authored, and whether a material program lights it -- for ranking what reads as a light |
| `scripted` | a character, something a staging scenario moves, or under one: its state is theirs |
| `drivenBy`, `plannedBy` | the sources of a person's routes already on it; the Director plan items whose routes are |
| `field`, `fieldTriggered` | a wave's field, and whether its clock starts at a musical event |

**Heroes are found from the scene, never from a naming convention.** A hero point owns the node it
names (ADR-107), every root node standing within its reach (twice its radius, or its radius plus 3 m),
and everything parented under either -- the elder's gills, its spores. A practical light owns no node
and answers for the hero it stands beside (its position read from the scene's lights, or from the rig
expanded around the subject the composition sizes it to, before the first frame). **A character is no
hero's part**: nodes an entity drives, whatever a staging scenario can move
(`stage::scenarioOwnedNodes`) and everything under them are marked `scripted`, and a hero point with
nothing of the scene's own standing for it -- a character's -- is dropped.

**Nothing dead is offered.** Every candidate goes through `liveness::Registry::standard().checkTarget`
with the scene's facts; a dead finding, or a phase rate, moves it to `excluded` with the rule's
reason, so the catalogue cannot offer what the validator (ADR-926) would refuse. The family table adds
the checks the registry cannot make from a path: a layer that emits nothing, a light-wave depth whose
owner names no field (both also rules in the registry now, ADR-926), a terrain's own boost (its layers'
lanes are the handle), a multiply of a base of 0, the water on a terrain that holds none, tears that are
off (at 0 the tear code is compiled out, so no route can open them).

**Where it lives:** `CapabilityRegistry::reactive()`, filled by `app::sceneFactsFor`, so every
director sees it: `director.inspect_capabilities` returns it under `"reactive"`, the proposer (ADR-927)
reads it, and `avgen --propose-reactivity` writes it (`catalog`).

## Consequences

- **The glade** (`tests/support/reactivity_fixture.hpp`): every group but node-emission and water-tears
  has entries -- the heroes' program-lit caps and gills (the stems plain), four scatter layers' glow and
  hue (the grass at 5% of the fungi's emission), the fungi's triggered wave and the smallest hero's own,
  the two programs with every surface drawn with each, both particle swarms (the elder's spores its
  hero's), the practical light (the elder's), the key and ambient (global), the ecology light, fog,
  wind, the pond, the aurora. The stones' three lanes are excluded ("emits nothing"), and so are the
  shelf's light wave (no field), the terrain's boost and the character's glow as a hero's.
- **Glowmere Valley 3** (a scratch copy): 10 hero mushrooms of 16 hero points (the characters and the
  saucer are not), each with three program-lit parts and a plain stem; 16 glowing scatter layers of 18
  (boulders and pebbles excluded), of which fungi (6.9), beacons (6.2), shelf fungi (4.4) and flowers
  (3.9) glow at more than a quarter of the brightest, and the trees' fireflies, ferns,
  grass and bushes at 0-4%; 14 particle systems (the saucer's beam scripted); the elder's practical light;
  the ecology light, fog, wind, water and the aurora.
- **The water's tears** are catalogued the moment the water stream registers them (tested with a
  synthetic `nodes/<terrain>/water/tears`); on this branch they do not exist, so no proposal targets
  them and no pixel proof of them is possible here.
- `director.inspect_capabilities` returns the catalogue when a host set it (every engine host does);
  the registry's JSON is unchanged when it is empty.
