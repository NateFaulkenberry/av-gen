# Set pieces and the evaluator hook: design notes at the pause (2026-09-26)

Stream `setpieces`, ADRs 928-931, worktree `av-gen-setpieces`, branch `agent/setpieces`, from main
`0b623b88`. Brief: `av-gen-gv3/docs/glowmere-valley-3/revision/briefs.md`, "### setpieces". This file
records where the work stopped and every decision already made, so the next agent can finish without
re-deriving them. No ADR is written yet; the four planned are listed at the end.

## Where it stopped

| Part | State |
|---|---|
| Staging primitives (`src/stage/staging.*`, `stage_json.cpp`) | **Done, tested.** `BeatDesc::startAt`, `StepDesc::component`, beat-named `startOn`/`stopOn`, the bus-id cache fix, `Staging::wrote()` |
| Templates (`src/stage/setpiece.*`) | **Done, tested** (`tests/unit/test_setpiece_templates.cpp`) |
| `PlanSetPiece` parse, schema, round trip (`src/directing/plan.*`) | **Done, tested** |
| Validator and compiler (`src/directing/setpieces.*`, `validator.cpp`, `compiler.cpp`) | **Done, tested** (`tests/unit/test_directing_setpieces.cpp`) |
| Engine: `setStaging`, `capturedStaging`, the project's `staging` key, staging bus events, `directingEvaluations` storage | **Compiled; exercised only through the directing tests' installs.** No engine-level test yet |
| Undo: `ui::StagingChange`, `EditCapture` | **Compiled, not tested** |
| `directing/evaluation.*` (EvaluationReport, reportFromCritic, compareEvaluations, planItemSpans) | **Compiled, not tested** |
| `--plan` / `--plan-report` (`application.*`), `app/directing_plan_file.hpp`, `avgen_cast_trace --plan/--save-project` and its `setPieces` section | **Written, NOT COMPILED** (the app and the tool were not rebuilt; they are in the second, WIP commit) |
| Evaluator hook (`director.evaluate`, `director.compare`, host hook, Critic runner) | **Not started** beyond the data types |
| UI reach (coordinator's rule, added mid-stream) | **Not started.** Plan below |
| Engine-level proof test, GPU beam-colour test | **Not started.** Plan below |
| ADRs 928-931, README rows | **Not written** |

Test status at the pause: `avgen_tests "[setpiece]"`: 23 cases, 660 assertions, all pass. A
`[stage]` + `[directing]` regression run was started and had not reported when the pause came.
The full CPU suite has not been run on this branch.

## Decisions already made (keep them unless there is a reason)

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
and fired the frame after, from the entity world's event record (replayed by seeks), so the frame
after a seek carries what a play carries. Deliberately NOT published into the ADR-870 replay bus:
the modulator binds reactions by live-bus index and that bus shares only the frame-signal prefix;
publishing there would make other reactions misread. Staging's own `startOn` reads beats from its
previous frame instead, which a seek replays exactly. Pre-existing hazard found and fixed: staging
cached signal ids across the live and replay buses.

**Undo.** `ui::StagingChange` (as-installed descriptions). `Engine::setStaging` validates on a scratch
director, re-registers, rebinds, and `requestSeek`s the current second (deferred in the editor,
immediate elsewhere). `EditCapture` skips parameter bases the director wrote (`Staging::wrote`), which
that re-simulation rewrites.

## Next steps, in order

1. Build `avgen` and `avgen_cast_trace` (`cmake --build <wt>/build/release -j 6`), fix what the WIP
   commit breaks, and run `[stage]`, `[directing]`, `[setpiece]`.
2. **UI reach** (ENGINEERING-RULES.md "UI reach"): (a) add `label` to `stage::ScenarioParam` (JSON
   "label", registered as `ParamDesc::label`) and fill it from the slot table ("hover height above the
   ground (m)", "beam at (s)"); (b) add `"staging/"` to `ui::detail::kBeginnerPrefixes` (visible in
   the picture, ADR-410's reasoning), with a test like `test_tree_energy_reach.cpp`; (c) the Director
   panel: a "UFO set pieces" section listing every set piece in the project's plans in viewer words
   (template, place, placed moment and time, animals, bearing, hover, colour, framing) with controls
   that edit the PlanSetPiece and apply a revision through `app::applyCompilation` (one undo). Put
   the decisions in `director_panel_logic` (`setPieceRows`, `editSetPiece`) with a CPU test.
3. **Engine-level proof** (`tests/unit/test_setpiece_film.cpp`, lab scene
   `tests/data/setpieces/setpiece-lab.*`): plan with west (12 s, point (-120,-60), 1 animal),
   field (60 s, (62,22), 2 animals, coloured), south (110 s, region (150,180) r 30, 3 animals); play
   0-126 s at 60 fps; assert all six animals retired and strays untouched, each beam beat entered
   within a frame of its time, the craft holding station through beam and lift (drift <= craftWobble;
   a craftWobble 0 arm gives speed exactly 0), the craft hidden between set pieces; seek to mid-lift of
   each and compare craft/animal positions and beam state with the play; world events equal in play
   and after a seek; a route from `setpiece/field/beam` reaches its target the frame after the beam,
   in play and after a seek; save/reload plays the same; undo/redo of the install.
4. **GPU**: `tests/rendering/test_setpiece_gpu.cpp`, lab scene at a beam moment, red vs default beam,
   a difference in the beam's region (under `tools/gpu-lock.sh`).
5. **Evaluator hook (ADR-931)**: `ai::EvaluationHook` in `tool_context.hpp` (a `DeferredResult`, plus
   a main-thread `settle(engine, value)` so the report is stored in `Engine::directingEvaluations()`);
   tools `director.evaluate {plan|planId, from, until, mode, label}` (compiles in scratch, never
   proposes: the autonomy policy) and `director.compare {planId, a, b}` (`compareEvaluations`). Host
   hook: scratch engine loads a project copy, installs the compilation, saves; renders the span with
   `avgen --project copy --render clip.mov --range a:b --size WxH` (command prefix configurable, e.g.
   `tools/gpu-lock.sh`); traces the cast; writes shots.json from the camera shots; runs
   `adapters/avgen/avgen_adapter.py`; runs `critic --url U submit --inputs ... --video-start a --wait
   --json --strict --no-autostart`; exit 4 = the Critic is not running (a clear error), 5 = partial
   (report kept, `partial: true`). Critic path from `--critic` / `AVGEN_CRITIC`; missing = clear
   error. Tests: stub hook; one ScriptedProvider test propose -> evaluate -> Modify -> compare; a fake
   `critic` script for the runner's parsing and exit codes. Live smoke test on a private instance:
   `CRITIC_PORT=<p> CRITIC_HOME=<scratch> critic start --daemon`, then stop it. Never touch :8765.
   Adapter changes to report (do not edit the Critic repo): it hard-codes GV3's saucer beats as
   events; it should read `setPieces` from the cast trace instead.
6. ADRs: 928 set-piece templates and the staging clock; 929 PlanSetPiece and the project's staging;
   930 staging events on the bus and in seek replay; 931 the evaluator hook. README rows. Full CPU
   suite; GPU suite if shaders or rendering changed (they have not).
