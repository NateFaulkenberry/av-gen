# Set pieces and the evaluator hook: design notes (2026-09-26, finished 2026-09-27)

Stream `setpieces`, ADRs 928-931, worktree `av-gen-setpieces`, branch `agent/setpieces`, from main
`0b623b88`, with `integrate/revision` (`a6598157`, reactivity ADR-924-927) merged in. Brief:
`av-gen-gv3/docs/glowmere-valley-3/revision/briefs.md`, "### setpieces". The decisions are the ADRs:

- ADR-928 set-piece templates and the staging clock (`docs/decisions/ADR-928-*.md`);
- ADR-929 a plan places set pieces, and the project keeps its staging;
- ADR-930 a set piece's moments are world events and bus events, and a seek rebuilds both;
- ADR-931 the Director evaluates in scratch, and proposes only the winner.

This file keeps the working notes behind them: the reasoning each decision rests on, in more detail
than an ADR carries.

## State

| Part | State |
|---|---|
| Staging primitives (`src/stage/staging.*`, `stage_json.cpp`) | Done, tested. `BeatDesc::startAt`, `StepDesc::component`, beat-named `startOn`/`stopOn`, the bus-id cache fix, `Staging::wrote()`, `ScenarioParam::label`, step completion within a microsecond (ADR-928) |
| Templates (`src/stage/setpiece.*`) | Done, tested (`test_setpiece_templates.cpp`) |
| `PlanSetPiece`, validator, compiler (`src/directing/*`) | Done, tested (`test_directing_setpieces.cpp`) |
| Engine: `setStaging`, `capturedStaging`, the project's `staging`, undo | Done, tested (`test_setpiece_film.cpp`: save/reload, undo/redo) |
| Bus events, the landing frame, route replay (`src/app/engine.cpp`) | Done, tested (`test_setpiece_film.cpp`, `[seek]`) |
| `avgen --plan/--plan-report`, `avgen_cast_trace --plan/--save-project` | Done; exercised on the lab and on a scratch copy of GV3 |
| UI reach: labels, `staging/` on the beginner layer, Director panel > UFO set pieces | Done, tested with the panels' own arithmetic (`test_setpiece_reach.cpp`) |
| GPU: beam colour on pixels | `tests/rendering/test_setpiece_gpu.cpp` |
| Evaluator hook (`director.evaluate`, `director.compare`, `src/app/directing_evaluate.*`) | Done, tested (`test_directing_evaluate.cpp`, `test_directing_evaluate_agent.cpp`); live smoke test in ADR-931 |

## The decisions, in working detail

**One scenario per set piece, named `setpiece/<key>`.** All autostart and wait; each hides its craft
at t = 0 (idempotent, and it keeps an instance's content independent of the set pieces before it, so
adding one does not re-fingerprint the others). Its beats are world events `setpiece/<key>/<beat>`
through the existing `Composition::raiseDirectorBeats`, which a seek's replay already runs.

**Beats.** Abduction: rest, transit (hidden hop to the entry, clocked `transitAt`), approach (show,
fly to the station), hover (`stillRoles: [actor]`, the animals gathered HERE, at arrival), beam,
lift, depart. Survey: rest, transit, approach, hover, beam, sweep (moves under a lit beam on
purpose: it lifts nothing), depart. Flyby: rest, transit, cross, depart. Moments a viewer names:
abduction approach/beam/lift/depart (default beam), survey approach/beam/sweep/depart (default beam),
flyby cross/depart (default cross).

**Time.** One moment is placed; its beat carries `startAt` (a param `<moment>At`), so it is entered on
the first frame at or after that second, in play and replay. Everything before it is scheduled
backwards with `kAnchorSlackSeconds` (0.25 s) absorbed before the clocked beat; after it follows the
authored durations. `SetPieceTimeline` gives nominal times on the frame model the beats run on (a
beat hand-off costs a frame; a `moveTo` opening its beat ends a frame early). The first version used
an absolute-time `wait`; replaced by `startAt` so the beat's world event itself is on time.

**Slots.** Parameter slots become scenario params; Structure slots (animal count, bearings,
distances, stack spacing, cruise speed) are folded into the beats. Only params some step reads are
registered (`referencedParams`), so e.g. `gatherRadius` is absent when the animals are named. Tested.

**Place.** Point (the station is fixed: good for framing) or Region (abduction only: nearest tagged
animal to the centre within the radius; the craft tracks it). Gathered animals: nearest to the
station within `gatherRadius` (query centre on the GROUND, `groundAtPlace`, because the query distance
is 3-D and the craft is 23 m up). Named animals bind by name.

**Beam colour** is three float params written with the new `component`, six parallel single-step cues
in the transit (a cue advances one step a frame), and restored at depart from the beam's authored
`colorStart`/`colorEnd` (read from `SceneFacts::parameterBases`).

**One craft, many set pieces.** Validator: per craft, in time order, `b.start < a.end +
kCraftHandoverSeconds (0.5)` is "one craft in two places"; else distance(a.exit, b.entry) /
(b.start + transitSeconds - a.end) > b's `cruiseSpeed` (80 m/s) is "travel it cannot make". Other
plans' set pieces still in the scene count. An authored scenario on the same actor: error when
autoStart, warning otherwise.

**Validator refusals** (errors block the item): unknown template, unknown craft, no `beam` part
(unless flyby), place near a subject with no place, animals that are not entities, slot unknown or
out of range, time before the film or after the piece, station outside the world, no clear air
(canopy at the station above `targetClearance` or within 2 m of the hover; a region with no clear
sample on a 9x9 grid; a named animal under canopy; a survey/flyby line through canopy), scenario name
collision. Warnings: fewer animals authored near the station than asked, `REPETITION` (new
IssueCode) for stations within 40 m or same-template framing within 15 %.

**Compile.** Scenario into `Staging::staging` (new member of `directing::Staging`), a cue marker per
moment (`setpiece/<key>/<moment>`, computed times), `ContentDomain::StagingScenario` in `produced`.
A `PlanCue` may start `on: "setpiece/<key>/<moment>"` (baked at the computed time).
`SceneFacts::staged.staging` is `Engine::capturedStaging()` (param bases folded in) so a slider edit on
a set piece is a HAND_EDITED item a revision will not overwrite. `installCompilation` installs only the
scenarios the compilation rebuilt (produced AND not blocked); every other scenario keeps its installed
form, and nothing is reinstalled when nothing changed.

**Persistence.** The project writes `staging` (the description as installed, not folded -- so
projects that merely tuned GV2's abduction stay byte-stable) when it differs from the scene file's,
and reads it before `params::loadProject`. The seventh member of ADR-207's family. ADR-264's save
rule now strips the transforms of every animal a set piece's tag query could bind.

**Bus (ADR-930).** Every scenario beat is a live-bus event `<scenario>/<beat>`, declared in `rebind`
and fired the frame after, from the entity world's event record (replayed by seeks). The frame a seek
lands on (dt = 0) carries the beats of the step before it, on the replay's 1/60 s grid; the first
version skipped dt = 0 frames and so disagreed with the play on exactly that frame. Not published into
the ADR-870 replay bus (the modulator binds reactions by live-bus index and that bus shares only the
frame-signal prefix); instead ADR-901's route replay treats the staging signals as replayable and
rebuilds them at every replayed step on its engine-shaped scratch bus, so a route with memory on a
beam is seek-exact. Staging's own `startOn` reads beats from its previous frame, which a seek replays
exactly. Pre-existing hazard found and fixed: staging cached signal ids across the live and replay
buses.

**Step completion (ADR-928).** `elapsed + 1e-10 >= duration`. The three ways a frame's instant is
computed (`i * dt` in a trace, `k / fps` in a render, `target - m * dt` in a seek's replay) differ in
the last bits (~1e-13 s), so a duration that is exactly a whole number of frames sat on a frame
boundary: the lab's second abduction (fade delay 3.5 s = 210 frames) entered `depart` a frame earlier
after a seek than in the play. Found by the film proof's world-event comparison. A first try at 1e-6
also swallowed float rounding of non-exact durations (0.8f = 0.8 + 1.2e-8) and moved every such step
a frame; the ADR-623 digest caught it.

**Clearance default (ADR-928).** The abduction's `targetClearance` is 6.5 m (GV2's). GV3's canopy field
reads 3.2 m over the whole valley floor (the tallest meadow layer), 5.4 m in scrub, 7.1-8.0 m in the
woods, so 3 m refused every animal on open ground, at plan time and at run time.

**Undo.** `ui::StagingChange` (as-installed descriptions). `Engine::setStaging` validates on a scratch
director, re-registers, rebinds, and `requestSeek`s the current second (deferred in the editor,
immediate elsewhere). `EditCapture` skips parameter bases the director wrote (`Staging::wrote`), which
that re-simulation rewrites.
