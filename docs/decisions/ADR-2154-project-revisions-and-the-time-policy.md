# ADR-2154: "unsaved changes" is two revision counters, and the world clock while parked is a named policy

- Status: Accepted (2026-11-25), eng/engine-api-s1b (Engine API stage S1b; 113-engine-architecture, design in
  03-phase0-api-contracts/02-session-api.md sections 3.3 and 3.4; origin 112-neon-dreams/02-ui-perf-investigation,
  fixes 7 and 8)
- Code: `app/engine.{hpp,cpp}`, `app/engine_project.cpp` (`ProjectRevisions`, `RevisionScope`, `UnrecordedChange`,
  `SavedComparison`, `Engine::revisions`, `bumpProjectRevision`, `compareWithSaved`, `projectDirtyOnDemand`,
  `TimePolicy`, `setTimePolicy`, `effectiveTimePolicy`, `stepFrames`, `Engine::tick`; `sampleProjectDirty`,
  `projectDirty` and `projectDirtyCached` removed), `app/edit_system.{hpp,cpp}` (`attach`, the state-revision memory;
  `markSaved` and `dirty` removed), `ui/edit_history.{hpp,cpp}` (`EditHistory::setObserver`), `api/session.{hpp,cpp}`
  (bumps, restores, `Host::offeredTimePolicies`, `LocalHost::setOfferedTimePolicies`, `session.info` reports
  `revisions` and `timePolicy`), `api/session_tools.cpp` (`project.diff_saved`, `transport.get_policy`,
  `transport.set_policy`, `transport.step`), `app/application.{hpp,cpp}` (the sampler, the close gate,
  `--parked-clock`), `ui/unsaved_changes.hpp` (`TouchWindow` replaces `DirtySampleSchedule`), `ai/capabilities.cpp`
  (the `transport` domain)
- Docs: `docs/api/transactions.md` (revisions), `docs/help/reference-command-line.md` (`--parked-clock`;
  `AVGEN_DIRTY_TRACE` gone with the sampler)
- Tests: `tests/unit/test_api_revisions.cpp` (`[api][revisions]`, `[api][time]`); `test_edit_system.cpp`,
  `test_unsaved_changes.cpp`, `test_project_lifecycle.cpp` and four callers moved to the new API

## Context

ADR-440 answered "is there anything to lose?" by serialising the project and comparing it with the serialisation
taken at the last open or save, and Studio sampled that every `max(250 ms, 10x its cost)` on idle frames (not while
playing, ADR-952). On Neon Dreams (22 MB) one sample costs 185 ms: the editor's p95 frame (REPORT.md fix 7). And
while parked, `Engine::tick` free-runs `renderTime` on the wall clock, so a stopped editor costs as much as a playing
one (fix 8); that was a side effect of `RealtimeClock`, not a setting anyone could read or change.

Owner decisions (2026-11-25): cheap revision counters for normal dirty checks; an on-demand comparison may clear a
false positive, at Close and Quit in particular; never serialise on a timer to decide dirtiness; Studio keeps today's
parked behaviour (`StoppedAmbient`) by default, the policy is explicit, `StoppedFrozen` is an option.

## Decision

1. **`Engine::revisions()` = `{project, scene, savedAt}`, `dirty()` = `project != savedAt`.** Values come from one
   monotonic generator, so a bump never reproduces a value a restore could have put back. A load, File > New (and a
   rollback across them) gets a fresh clean base; a save sets `savedAt = project`. Before the first load or save there
   is no document to lose, so nothing is dirty (ADR-440's rule, kept). Opening a scene, glTF or the orb on its own, or
   a new composition (every `resetProjectBasis` site) bumps `scene`.
2. **Who bumps.** A successful `mutatesProject` Session call, once, after its implicit transaction commits; an
   explicit transaction, once at commit if a call inside it changed the project; every push, undo and redo on the
   edit history (`EditHistory::setObserver`, so a command a panel pushes straight onto the history counts too). A call
   whose own work moved the counters (a save, an open, a command Studio's sink pushed) is not bumped again. A failed
   call's implicit rollback, and `transaction.rollback`, restore the counters recorded at begin; a save inside a
   rolled-back transaction leaves it dirty, because the disk then holds the middle of the transaction. The engine's
   own writebacks call none of this.
3. **Undo back to the saved state is clean.** `EditSystem` remembers, per history state, the project revision it was
   reached at, and an undo or redo that fully applies and lands on a remembered state puts that revision back -- so it
   is clean exactly when that is what the last save (from anywhere) recorded. The memory is dropped whenever something
   bumped outside the history (`Engine::unrecordedEpoch`), because those states no longer include that change.
4. **The comparison runs on demand only.** `Engine::compareWithSaved(Report|Adopt|Absorb)`: no difference and dirty
   clears it (the edit-set-back false positive); a difference while clean is an unrecorded write, logged with its JSON
   pointer paths, then reported, adopted as an edit, or absorbed as the engine's writeback. `project.diff_saved` is the
   `Report` form. Studio's close gate calls `projectDirtyOnDemand(touched)`: `Adopt` when a widget was active since the
   last save, load or close (`TouchWindow`), `Absorb` when not -- ADR-440's attribution, applied only to what the
   counters missed. The title follows `revisions().dirty()`, an integer comparison per frame; the periodic sample is
   gone. A `--ui-script`/`--ui-ab` run ends with one `Report` comparison: the dev assertion that names unrecorded
   writers.
5. **`TimePolicy {Running, StoppedFrozen, StoppedAmbient, Stepped, Offline}`** is the engine's parked setting, chosen
   by its host. A Live engine starts at `StoppedAmbient` (the old code path, untouched); `StoppedFrozen` holds
   `renderTime` and gives `deltaTime` 0 while parked, re-anchoring the wall clock so a resume does not jump; `Stepped`
   holds even a playing transport until `stepFrames`, which plays n frames at a fixed step (deltaTime 1/fps, renderTime
   exactly base + n/fps; on an Offline engine the transport follows, on a Live one it advances when playing). An Offline
   engine starts at `Offline` and `tick` ignores the setting, so no render job changes. `Host::offeredTimePolicies` says
   what a host's loop honours; Studio offers ambient and frozen and reads `--parked-clock ambient|frozen` (default
   ambient); `transport.set_policy` refuses anything else with `FAILED_PRECONDITION`.
6. **The headless `--ai-script` host stays `Offline`, not `Stepped`.** Its session's engine is then ticked by
   `runHeadless`'s fixed-step loop; honouring `Stepped` there would stop the frames that run renders after the script,
   which changes headless output. `LocalHost` offers only the engine's own policy unless told otherwise; stage S2's
   EngineHost, which has no frame loop, is where `Stepped` belongs by default.

## Alternatives considered

1. **Keep the sampler and adapt its interval** (REPORT.md's other option). Rejected by the owner: still a timer.
2. **A dirty flag set in every engine mutator.** Rejected for the reason ADR-440 gives: every new mutation site is
   one more chance to forget it, in the direction that loses work. The bumps are at the two entry points (Session,
   edit history), and the close's comparison is the safety net for anything that bypasses both.
3. **`EditSystem` keeps its own saved-state id, and an undo to it sets `project = savedAt`.** Rejected: a save made
   through a Session call or a Save As does not tell the edit system, and a change outside the history between the
   save and the undo would be called clean. Remembering each state's revision answers both.
4. **Studio defaults to `StoppedFrozen`.** Not done: it changes what the editor shows (water, wind and rigs stop),
   and the owner chose today's behaviour as the default.

## Consequences

- **Panel writes that bypass the edit history no longer mark the title.** `ui::drawParameterValue` (the Parameters
  panel's sliders) and the other panels that call `setBaseComponent` directly (lights, effects, environment, live,
  world, control panel; see the stage report) bump no revision. The close gate still catches them (the widget touch
  makes it `Adopt`), so no work is lost at Quit, but the title's dot appears only once something recorded happens.
  Routing them through `EditHistory::beginDrag`/`commitDrag` fixes both the dot and their missing undo.
- `EditSystem` is neither copyable nor movable (its history's observer holds it).
- `session.info` gains `revisions` and `timePolicy`; transaction results gain `revisions`; the catalogue gains four
  tools. Additive, so `kApiLevel` is unchanged.
- `project.get_state` does not report revisions yet: `ai/engine_tools.cpp` is stage S3's file in this round.

## Revisit triggers

- Stage S2's EngineHost: offer `Stepped` and make it the default there; `ensureFrame` for reads that need a flattened
  frame.
- The unrecorded writers are routed through the edit history: the touch window and `Adopt` can then go.
- A second client sharing Studio's session: it should see `StoppedFrozen` (02-session-api.md 3.4).
