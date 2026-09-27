# ADR-929: A plan places set pieces, and the project keeps its staging

**Status:** Accepted
**Date:** 2026-09-26
**Follows:** ADR-755 (the Director Plan), ADR-756 (validate, then compile against a staging copy),
ADR-752 (one undo for everything the Director writes), ADR-767 (cues on watched events), ADR-207 and
its family (a session's content that the project must save because the scene is saved by reference),
ADR-264 (the save rule for animals' transforms), ADR-928 (set-piece templates)
**Found by:** the GV3 revision's Director audit (`reports/director.md` §2 "More UFO events",
recommendation 5: "let a plan place the abduction at a chosen place and time, with variation")
**Implemented by:** `directing::PlanSetPiece`, `SetPieceWhere`, `Plan::setPieces`,
`ContentDomain::StagingScenario`, `IssueCode::Repetition` (`src/directing/plan.*`, `issue.*`);
`resolveSetPiece`, `otherPlansSetPieces`, `checkSetPiecesTogether`, `setPieceOfEvent`,
`setPieceCatalog` (`src/directing/setpieces.*`); the validator and compiler arms
(`src/directing/validator.cpp`, `compiler.cpp`); `SceneFacts::canopyAt`/`worldBounds`,
`Staging::staging`, `CharacterMark::tags` (`src/directing/scene_facts.hpp`,
`src/app/directing_context.hpp`); `Engine::setStaging`, `capturedStaging`, the project's `staging`
key (`src/app/engine.*`); `ui::StagingChange` and `EditCapture` (`src/ui/edit_history.*`,
`src/app/edit_capture.*`); `app::applyPlanDocument`/`applyPlanFile` (`src/app/directing_plan_file.hpp`);
`avgen --plan`/`--plan-report` (`src/app/application.*`); `avgen_cast_trace --plan`/`--save-project`
and its `setPieces` section (`tools/cast_trace.cpp`); `ui::setPieceRows`, `editSetPiece`,
`compileSetPieceEdit`, `setPieceEditLabel` and set pieces in `planItemRows`
(`src/ui/director_panel_logic.*`); the Director panel's "UFO set pieces" section
(`src/ui/director_panel.cpp`); `setPieces` in `director.inspect_capabilities` (`src/ai/director_tools.cpp`)
**Tests:** `tests/unit/test_directing_setpieces.cpp` (`[directing][setpiece]`: parse and canonical
round trip, the reader's refusals, every validator refusal with its passing partner, repetition,
musical times, compile and revision, a hand-tuned set piece kept, cues on a moment, an authored
scenario on the craft, the herd estimate); `tests/unit/test_setpiece_film.cpp` (the proof: three
abductions, one craft, save/reload, undo/redo); `tests/unit/test_setpiece_reach.cpp` (the Director
panel's rows and edits, one undo each, the hand-tuned state, the capabilities catalogue)

## Context

The Director could plan shots, markers, performances, cues, retimes and (ADR-924) routes, and no
scenario. It saw scenarios only when checking camera precedence (`validator.cpp`). GV3's generator
wrote its one abduction as raw staging JSON and could not ask the engine where a set piece would be,
when, or whether it could happen there. And nothing a plan might have made would have survived a save:
the composition is saved by reference, so a set piece installed into it lived in the window and in no
document a render reads.

## Decision

**`PlanSetPiece`**, modelled on `PlanCue`: a key (the scenario is `setpiece/<key>`), a template, a
craft (a staging actor), a time `at` for one `moment`, a place, and variation.

```json
{"key": "e4-river", "template": "abduction", "craft": "saucer", "at": "bar 57", "moment": "beam",
 "place": {"point": [62, 22]}, "animals": [], "tag": "animal",
 "set": {"animals": 2, "hoverHeight": 30, "approachBearing": 90}, "beamColor": [1.0, 0.7, 0.3],
 "framingMetres": 60}
```

- `at` is any plan time: seconds, a clock time, a bar, a section, or a watched event (ADR-767).
- `place` is `{"point": [x, z]}`, `{"near": alias, "offset": [dx, dz]}` (a hero's or node's authored
  place), or `{"region": {"center" | "near", "radius"}}` (an abduction searches it for the nearest
  tagged animal with clear air, and the craft tracks it).
- `animals` names the animals to lift, in lift order; otherwise the `animals` slot counts and the
  nearest tagged animals to the station are gathered.
- `set` overrides template slots; `framingMetres` is declared, not compiled: the distance the set
  piece is meant to be seen from, checked for repetition and handed to whoever places the camera.

**The validator refuses** (an error blocks the item, never a substitute): an unknown template or craft;
a craft with no `beam` part for anything but a flyby; a place near a subject with no authored place;
an animal that is not an entity; an unknown slot or a value out of its range; a time before the film or
a set piece that would end after the piece; a station outside the world; **no clear air** -- canopy at
a point station above `targetClearance` or within 2 m of the hover, a region with no clear sample on a
9 x 9 grid, a named animal under canopy, a survey's sweep or a flyby's path through canopy -- asked of
the navigation layer's canopy, which is what the staging queries ask at run time; a scenario name that
collides with one this plan did not make; **one craft in two places** (per craft, in time order, a set
piece that needs the craft less than 0.5 s after the last one lets it go); **travel the craft cannot
make** (from one's exit to the next one's entry faster than the later one's `cruiseSpeed`, 80 m/s by
default); and an authored scenario that drives the same craft from the start of the film (a warning
when it only runs when started). Other plans' set pieces still in the scene count.

**It warns** when fewer tagged animals stand near the station than the set piece lifts ("animals
move, but if it finds fewer it leaves without beaming"), and -- the owner's "do not duplicate the same
abduction shot" -- with the new code **`REPETITION`** when two set pieces' stations are within 40 m, or
two of one template are framed within 15 % of the same distance. A flyby happens at no place (its
station is the middle of a crossing), so it repeats only another flyby's line: GV3's flyby crosses
the sky over the elder its centrepiece lifts beside, which is a rhyme, not the same shot.

**Compiled** into `Staging::staging` next to the authored scenarios, with a sequence marker per moment
(`setpiece/<key>/<moment>` at its nominal time) and a `produced` entry per scenario
(`staging.scenario`). A cue may start `on: "setpiece/<key>/beam"`, placed at the moment's time. The
facts' staging is `Engine::capturedStaging()` -- each scenario parameter's value taken from its base --
so a slider moved on a set piece fingerprints as the hand edit it is, and a revision keeps it.
`installCompilation` installs only the scenarios the compilation rebuilt; every other keeps its
installed form, and nothing is reinstalled when nothing changed.

**The project keeps its staging** -- the seventh member of ADR-207's family. The project writes
`staging` (the description as installed, not with bases folded in, so a project that merely tuned an
abduction's slider keeps its file) whenever it differs from the scene file's, and reads it before
`params::loadProject`, so a set piece's tuned values arrive onto registered parameters.
`Engine::setStaging` validates on a scratch director first, re-registers the scenario parameters,
re-binds the timeline and routes, declares the beats on the bus (ADR-930), and re-simulates the
current second. **Undo:** `ui::StagingChange` holds the description before and after; `EditCapture`
records it, and skips the parameter bases the director wrote (`Staging::wrote`), which the
re-simulation rewrites. An approved plan, a Director-panel edit and `avgen --plan` are each one undo.

**A headless path for a generator:**

- `avgen --project P --plan plan.json [--plan-report report.json] --save-project OUT` parses,
  compiles against the project as loaded, installs exactly as an approved proposal installs (content
  and provenance), verifies the installed content against the plan's fingerprints, and exits non-zero
  when any item could not be built. The report lists every finding, every blocked item, and each set
  piece's scenario, placed moment, nominal moments, start and end, station, entry and exit, animal
  count and hover height.
- `avgen_cast_trace --project P --plan plan.json --save-project OUT --out trace.json` does the same
  without a GPU, then traces the film, and exits 2 (after writing the trace) when any item could not
  be built. Its `setPieces` section says what each set piece actually did:
  every beat's entry time and the craft's position there, each animal bound with where it was at the
  lift and at its retirement, whether the scenario finished, and -- ADR-385's invariant -- the craft's
  fastest frame-to-frame speed and farthest drift while the beam rose and the animals were lifted.

**Where an artist finds it** (the owner's rule):

- **Director panel -> UFO set pieces** (shown with or without the assistant): one row per set piece
  of every plan in the project, in the words of the picture -- "abduction: lifts 2 animals, flown by
  saucer", "beam at 01:03.705 (bar 35)", "over (62, 22)", "comes in from the east (90 deg), hovers
  30 m up, red beam, meant to be seen from about 60 m" -- with what the validator says about it now.
  Its controls edit the plan item: **event** (the template), **placed moment**, **at (s)**, **over
  x, z (m)**, **animals lifted**, **comes in from / flies toward (deg)**, **hover height / height
  above the ground (m)**, **coloured beam** and **beam colour**, and **meant to be seen from (m)**.
  Each edit is a new revision of the plan, compiled against the project and applied as ONE undo
  labelled "UFO set piece '<key>': <what>". An edit that would block the set piece is refused with
  the validator's reason and changes nothing (a revision that blocks an item takes out what its last
  revision made and builds nothing in its place). A set piece whose sliders were moved by hand says
  "tuned by hand" and how to hand it back (Reset to default); its controls wait, because a revision
  keeps hand edits.
- **Parameters panel -> staging -> setpiece/<key>**: its knobs (ADR-928).
- **Director panel -> the proposal's plan list**: a set piece is an item row with its marks.

## Consequences

- **Every scene or project that does not use set pieces is unchanged**: no `staging` key is written
  unless the staging differs from the scene file's (compared after a round trip through the same
  parser); a plan without `setPieces` serialises to the same bytes; no scene's look or behaviour moves.
- **ADR-264's save rule covers set pieces with no change to it.** It strips the saved transforms and
  visibility of every body any scenario's queries could bind (`stage::scenarioOwnedNodes`), and a set
  piece is a scenario, so a project saved after an abduction does not photograph a lifted animal's
  mid-air pose back over its authored place.
- **The issue and content tables grew:** 27 issue codes (reactivity's six, then `REPETITION`) and 12
  content domains (`modulation.route`, `modulation.source`, then `staging.scenario`), each table now
  tied to its enumerators by a size assertion as well as its last entry.
- **Measured on the lab** (`tests/data/setpieces/setpiece-lab`, no licensed assets): the three-abduction
  plan compiles with no finding; `avgen_cast_trace --plan --save-project` traces all three with every
  beam on its second, 1, 2 and 3 animals bound and retired (at 18.05, 66.07 and 116.05 s), the craft
  finished each time (26.58, 74.60, 124.58 s), and a hold of 364-365 frames per lift with 0.41 m of
  drift -- the 0.3 m sway's reach -- and 1.11 m/s at most. Saved and reloaded, the film plays the same
  to 1e-4 m. Undo takes all three scenarios and their 60-odd parameters out; redo puts them back.
- **Validated on a scratch copy of Glowmere Valley 3** (the scene as the GV3 worktree has it, with a
  second craft `scout` added and the hand-authored `abduction` scenario taken out; nothing in the GV3
  worktree written). The five-event plan in the stream report compiles with every set piece placed
  where the music says: E1's beam goes out on bar 13 (22.632 s), E2 crosses at bar 15 + 0.3 s
  (26.636 s), E3 lifts on bar 37 (66.952 s), E4 on bar 57 (103.871 s), and E5's beam lights on bar 93
  (170.339 s) and it departs at 177.699 s -- the horse's dissolve lands on the drop (bar 97, 177.696 s)
  to a frame. The first run refused E3, E4 and E5 for clear air: the template's 3 m clearance against
  Glowmere's 3.2 m meadow canopy (ADR-928 changed the default to 6.5 m); and warned that the flyby
  repeated the centrepiece's place (the rule above). What remains is one warning a generator should
  act on: only one animal grazes within 55 m of E4's region today (the characters stream is re-homing
  the herd into that meadow). **Traced** (192 s at 60 fps): E1's beam went out at 22.633 s and E2
  crossed at 26.650 s; E3's region query bound `bull-10` far up the valley and lifted it at 66.967 s;
  E4 bound `cow-19`, found no second animal within 25 m of it at arrival, and left without beaming at
  100.9 s -- the warning, come true; E5's region tracked `horse-11` 13 m from its home, lit the beam at
  170.350 s, lifted at 172.783 s, and retired the horse at 177.717 s, a frame after the drop, with the
  craft within 0.354 m of its station throughout. Every placed moment was entered on the first frame
  at or after its time on the engine's own beat grid (ADR-896: bar 93 is 170.339 s there, where the
  revision plan's nominal grid says 170.312 s).
- **`director.inspect_capabilities` lists set pieces**: every template with its moments and slots
  (default, range, unit, words, knob or structure) and every craft with whether it has a beam, which
  the plan schema had promised and the registry did not have.
- **Not done.** The Director panel cannot add a set piece (a plan, a proposal or `--plan` does), and
  moves a point place only (a region or a `near` place is edited in the plan). The canopy check needs
  the scene's navigation layer; without one it is a warning, not a refusal.
