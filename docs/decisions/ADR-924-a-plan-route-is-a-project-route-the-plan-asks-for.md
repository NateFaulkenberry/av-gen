# ADR-924: A plan route is a project route the plan asks for, and a plan source the signal it needs

**Status:** Accepted
**Date:** 2026-09-26
**Follows:** ADR-755 (the Director Plan), ADR-756 (validate then compile), ADR-752 (one undo for every
domain the Director writes), ADR-900 (the route chain: `delayMs`, `depthSource`), ADR-901 (a seek
replays the project's routes)
**Found by:** the GV3 revision's Director audit (`reports/director.md` recommendation 2: "the Plan
has no route item ... no director writes modulation routes"), and the owner's brief sections 3-5 and 16
**Implemented by:** `directing::PlanRoute`, `PlanSource`, `planRouteToJson`/`planRouteFromJson`,
`planSourceToJson`/`planSourceFromJson` (`src/directing/plan_route.*`); `Plan::routes`/`sources`,
`ContentDomain::ModRoute`/`ModSource` (`src/directing/plan.*`); `compileReactivity`,
`reactivityContent`, `removeReactivityContent` (`src/directing/reactivity.*`); `Staging::routes`/
`sources` (`src/directing/scene_facts.hpp`); `params::ModRoute::planItem` (`src/params/modulation.hpp`,
`serialization.cpp`); `app::installReactivity`, `authoredRoutes` (`src/app/directing_reactivity.hpp`);
`app::sourcesDocument`/`setSourcesDocument` (`src/app/source_document.hpp`); the rack in
`EditCapture` and `AutomationChange` (`src/app/edit_capture.*`, `src/ui/edit_history.*`);
`ui::routePlanNote` (`src/ui/route_row_logic.hpp`); `ui::reactivityRows`, `reactivityHeading`,
`planItemRows` (`src/ui/director_panel_logic.*`); the Modulation panel's route rows
(`src/ui/control_panel.cpp`) and the Director panel's plans section (`src/ui/director_panel.cpp`)
**Tests:** `tests/unit/test_reactivity_plan.cpp` (`[adr924]`: round trip byte for byte, a plan without
routes keeps its bytes, the reader's refusals and warnings); `tests/integration/test_reactivity_engine.cpp`
(one undo, revision in place, hand edits kept across two revisions, seek-exact, save and reload, the
panels' logic); `tests/integration/test_reactivity_cli.cpp` (installed by plain JSON edits)

## Context

A director -- the owner, the LLM Director Agent, a script -- could plan shots, markers, performances,
cues and retimes, and nothing that responds to the music. Reactivity lived only in hand-written
`routes`, so GV3's first pass configured 24 route-target pairs and the evaluator saw one in the pixels.
A reactivity planner needs a plan item that becomes a route, and routes need the same provenance,
revision, undo and seek guarantees every other piece of content a plan makes already has.

## Decision

**A `PlanRoute` carries a project route verbatim**: `route` is exactly an entry of a project's
`routes` array (`params::routeToJson`) -- source, target, component, amount, op, polarity, enabled,
the chain with its `delayMs` first, and `depthSource`/`depthMin`/`depthMax` -- beside what a person
reads: `level` (micro, meso, macro), `group` (the catalogue group of its target, ADR-925), `owner`
(what answers), `layer` (the musical layer, in words) and `reason`. One definition of a route: a
generator installs it by copying, and the reader is the project's own `routeFromJson`, with unknown
route and chain fields reported (SCHEMA_UNKNOWN_FIELD, with the nearest real name -- "delay" suggests
"delayMs").

**A `PlanSource` is a source the plan makes** because a route needs a signal the bus does not carry:
kind, name, settings exactly as the rack writes them, and the values of the parameters it registers
(`beatSync`, `beatsPerCycle`). Only pure-in-time kinds -- LFO, noise, timeline -- so every compiled
route is replayed by a seek (ADR-901); an envelope or a random source is refused (ADR-926).

**Compiled to ordinary content.** A route is appended to the staged route list with
`ModRoute::planItem = "<planId>/<key>"`; a source becomes an entry of the staged rack with every
parameter it registers. Both are recorded in `produced` (`modulation.route` by planItem,
`modulation.source` by signal) and fingerprinted, so:

- a **revision** removes its previous routes and sources and compiles the new ones in their place;
- content a person **edited by hand** since is kept exactly as they left it (HAND_EDITED), and keeps
  the fingerprint the plan recorded -- see Consequences;
- the **install** (`installCompilation`) replaces the authored route list with the staged one (graph,
  entity and macro routes kept) and makes the rack match the staged document (only what differs is
  touched), then rebinds once.

`ModRoute::planItem` is saved with the route only when set, so every existing project keeps its bytes.
It changes nothing a route does.

**One undo.** `EditCapture` records the rack as `app::sourcesDocument` -- each source's kind, name,
settings and parameter bases -- beside the routes it already recorded, and `applyEdit` restores it
before the routes, so undoing a plan takes back its sources with the routes that read them.

**Seek-exact by construction:** every source a compiled route can read is on the replay's bus or pure
in time (the analysed audio, `section.*`, `beat.*`, `music.*`, the plan's LFO and timeline), and ADR-901
replays their chains. Measured below.

### Where an artist finds it

| What | Where |
|---|---|
| a planned route | **Modulation panel -> Routes tab**, an ordinary route row, edited there like any other, marked **`[plan: <key>]`** beside its `source -> target` header; hovering says which plan made it, its level, what answers which layer, and why |
| a planned source | **Modulation panel -> Sources tab**, an ordinary source named for what it moves (`two-bar-breath`, `glowing-plants-hue-by-section`); its parameters in the **Parameters panel -> sources -> `<name>`** |
| a proposal's route and source items | **Director panel -> the plan**: one row per item (`route micro hats: audio.onsetHigh -> ...`, `source ...`), marked ok / warning / blocked with the validator's findings, and the diff line each compiles to |
| a project's installed plan | **Director panel -> Plans in this project -> Reactivity: N routes: micro a, meso b, macro c**: each item's layer and route, its reason on hover, and "(edited by hand)" or "(not in the project)" when the route is no longer as made. Drawn with or without the assistant |

## Consequences

- **Measured on the glade fixture** (`test_reactivity_engine.cpp`): every route and source of the default
  proposal installs as one command; undo restores the route list and the rack document byte for byte
  and removes the plan; redo restores them; every `produced` fingerprint matches the installed content.
  Proposed again, the plan is revision 2 with no `+` line and the same route count. A seek to 20.3,
  27.0 and 45.2 s lands every planned route's target exactly where a play from zero does (`==`, 60 fps),
  with more targets away from their bases than there are targets. A save and a reload keep the plan,
  every route's `planItem` and both sources.
- **A kept hand edit keeps its recorded fingerprint** (`installCompilation`). Before, the install
  re-took every produced item's fingerprint from the installed content, so a revision that protected a
  person's edit adopted it as the plan's own, and the revision after next overwrote it. The test edits
  the fungi's kick route to 0.12, revises twice, and finds 0.12 both times; without the change the
  second revision writes 0.30 back. This held for every content domain (shots, markers, cues, effects)
  -- the existing ADR-756 test revises only once, so it could not see it.
- **The Director panel draws "Plans in this project" without the assistant.** It used to return before
  it with no AI plane; the plans are the project's.
- Existing projects are unchanged: no route carries `planItem`, no plan carries `routes` or `sources`,
  and a plan without them writes exactly the JSON it wrote before (checked).
