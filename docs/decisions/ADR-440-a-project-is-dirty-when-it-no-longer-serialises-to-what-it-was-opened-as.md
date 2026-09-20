# ADR-440: A project is dirty when it no longer serialises to what it was opened as

**Status:** Accepted
**Date:** 2026-09-20

Extends ADR-019 (project system), ADR-092 (a history describes one document), ADR-182 (a probe that
cannot fail proves nothing), ADR-225/ADR-350 (a setting the application does not keep is not a
setting), ADR-386 (the per-frame parameter writeback), ADR-391 (an app save photographs the run).

*Numbered in the 440-449 range assigned to this branch by the table at the top of this directory's
README.*

## Context

The owner asked for the dialog every editor has: *"Do you want to save changes? Yes / No / Cancel"*
before a project is closed. The application had none, and it had no notion of a project being
modified at all — `grep` over `src/app` and `src/ui` for `isDirty|unsavedChanges|hasUnsaved` finds
only `SequencePanel::dirty_`, which is about that panel's own list needing a re-layout.

Seven paths discard the current project without asking: File > New, File > Open, Open Recent,
Examples, Engineering Labs, a project dropped on the window, and quit. Quit was two bare `break`
statements in the frame loop with nothing at all between the event and the exit.

## The thing that had to be measured first

The obvious basis is *a project is dirty if serialising it now differs from what is on disk*. It is
honest, it is computable on demand, and it cannot drift out of date the way a hand-maintained flag
can. This engine's saves are also known not to round-trip cleanly, so before anything was built the
round trip was measured: load an example project, change nothing, serialise, diff against the file.

**Not one project in the repository round-trips.** Differing JSON paths, and the file the save would
write beside the file it came from:

| project | differing paths | file | re-serialised |
|---|---|---|---|
| `world/glowmere-valley-2` | 347 | 610 KB | 618 KB |
| `world/glowmere-valley-2-multicam` | 279 | 675 KB | 689 KB |
| `temple/temple` | 801 | 6.5 KB | 60 KB |
| `hero/hero` | 1,069 | 1.3 KB | 67 KB |
| `chamber/chamber` | 1,576 | 3.1 KB | 113 KB |
| `organic/fungi` | 1,584 | 0.8 KB | 92 KB |
| `lab/lab` | 1,879 | 3.4 KB | 121 KB |
| `city/night-shift` | **19,798** | 33 KB | **1.6 MB** |

The cause is not lossiness. A hand-written project file records the handful of parameters somebody
changed; a save writes every parameter the live session has registered — which is why night-shift
grows forty-nine times and why the diff is dominated by `parameters/...  ADDED`. A prompt built on
"differs from the file" fires on every project, every time, and a prompt that always fires is worse
than no prompt: it trains the reflex that dismisses it, and that reflex is what loses the work.

**This also disproves a claim the code makes about itself.** `Engine::saveProject` and
`scene::nodeEditsAgainst` both state that a round trip on an untouched project is byte-stable, and
that stability is the control those functions rely on. It is true of the four keys they guard
(`autoDirector`, `cameraAimFollow`, `heroes`, `lights`, `sceneNodes`) and false of the document.
ADR-385: a stated reason is not evidence. The comments have been left in place, because what they
say about *their own keys* is correct and load-bearing; this record is the correction to the wider
reading.

### What actually moves on its own

The same measurement, playing 600 frames (20 seconds) with no input at all and re-serialising from
the *same* engine:

| project | drifting paths with no input |
|---|---|
| `glowmere-valley-2-multicam` | 18 — `heroes[0,2,3,4,5,6]/position[0..2]` — plus 2 — `parameters/particles/visitor-beam/{spawnRate,emissive}` |
| `glowmere-valley-2` | 1 — the `heroes` key appears once the herd has walked far enough to differ from the scene file |
| the other six | 0 |

Twenty paths across eight projects: the entity simulation, and ADR-386's per-frame parameter
writeback on a world effect that time-gates itself to a shot span.

## Decisions

### 1. The baseline is a serialisation, not the file

A project is dirty when the document it would be saved as now differs from the document it *would
have been saved as* at the moment it was last opened or last written. Both sides are the same
function of the same engine, so the load's own lossiness cancels out exactly, and the question being
answered is the one the dialog asks: has anything changed since this was opened?

`Engine::saveProject` was split into `projectDocument(path)` (build) and `saveProject(path)` (build
and write) so the comparison is the save rather than a second serialiser written to match it. A
serialiser written "to match" another is the defect family this codebase keeps finding — a visitor
that skipped seven struct members, an unregister table missing two fields, a test harness copying a
uniform block field by field and missing the block ADR-389 added.

### 2. There is no `bool dirty_` set by every mutation, and no list of noisy keys either

A dirty flag is the same shape as those three defects, and its failure is silent **in the direction
that loses work**: every new mutation site is one more chance to forget it.

A list of keys to ignore was considered, and it inverts the failure direction — a forgotten entry
would produce a spurious prompt rather than a lost project — but a spurious prompt is exactly the
outcome the owner's brief calls worse than having no prompt.

So the drift is absorbed **by measuring it rather than naming it.** `Engine::sampleProjectDirty`
takes one argument: whether anything touched the application since the last sample. On a sample
where nothing did, every difference is by definition the engine writing its own state, and the
baseline simply moves to the current document. No list exists to drift out of date.

It is **monotone**: once dirty, dirty until a save or a load. Otherwise a quiet second after an edit
would absorb the edit, which is the flag's failure mode reintroduced by the back door. This is what
`test_project_lifecycle.cpp`'s last control asserts — an edit made *during* playback survives 120
further frames of absorption.

`EditSystem::dirty()`, which existed with neither a production reader nor a production writer, is
asked first because it is free. It is trusted to say yes and not to say no: it knows about the world
editor's commands and nothing about a slider, a light or a timeline.

**"Something touched the application" is not "an input event arrived", and that distinction was
bought by measurement.** The drift happens while *time advances*, which is exactly when a film is
playing — so if pointer motion counted as a touch, watching a project play with a hand on the mouse
would attribute twenty paths of simulation to the user and the prompt would fire on a project nobody
had edited. The signal is `ImGui::IsAnyItemActive()` — a slider being dragged, a field being typed
in, a menu item under the pointer — plus the edit history, which is what a canvas gizmo drag
produces and what ADR-101 routes every assistant edit through. Sampling is skipped entirely while a
widget is active, so the 31 ms serialisation can never land in the middle of a drag.

### 3a. Verified in the running application, not in the code

ADR-387: a correct value is not a reached value. Both directions were run:

- **900 frames of `glowmere-valley-2-multicam` playing, no input**: 55 samples taken (every ~317 ms,
  which is the 31 ms cost times the ten-fold throttle), **0** dirty transitions, 0 GPU errors.
- **300 frames under `--stress 7`**: the project is reported dirty after 1.0 ms of sampling, once,
  and stays dirty — the monotonicity, observed rather than asserted.

The second of those started as a failure and is why the stress driver now records a touch. It
reaches the engine directly, so no widget is ever active, so every edit it made was absorbed as
engine writeback and the first run of it reported nothing at all. `AVGEN_DIRTY_TRACE=1` prints each
sample with its attribution; it is what found this, and it is left in.

### 3. Cost, and where the check runs

A full serialisation costs **31 ms** on `glowmere-valley-2-multicam.json` (675 KB, 5,502
parameters) and under a millisecond on everything small — too much for a frame, nothing at all
beside the 2,751 ms that project takes to load.

So it runs at a close, where the answer is actually needed, and otherwise only on frames where no
input arrived, throttled to `max(250 ms, 10 x the last measured cost)`. The heaviest project in the
repository is therefore checked about every 310 ms and a small one four times a second, and neither
sample can ever land in the middle of a drag.

### 4. Every path that discards a project goes through one gate

`Application::requestClose(intent, action)` — File > New, quit, and `loadAny` for anything that
replaces rather than adds to the open project. A prompt on one path and not another is worse than
none, because it teaches that the application protects you.

"Replaces rather than adds" mirrors `Engine::loadFile`'s routing instead of restating it as a list
of extensions: audio, a scene, an environment map and a shader are added to the open project; a
project document and a world recipe replace it.

### 5. Cancel does nothing at all, and there are two of them

`ui::UnsavedChangesGate` is a four-state machine with no ImGui, no Engine and no Window in it, so
the part that is impossible to see in a screenshot is the part that is unit-tested.

Yes on a project with no path is Save As, and SDL's native dialog is asynchronous — it returns
immediately and delivers the chosen path, or an empty string for cancel, on a later frame. So "Yes"
cannot be `save(); proceed();`: there is a gap of unknown length in the middle during which the
application must neither close the project nor forget what it was about to do. That gap is the
`AwaitingSave` state, and `saveFinished(false)` — the Save As dialog dismissed, or the write itself
failing — lands in exactly the same place `answer(Cancel)` does, with the pending action dropped.

The gate holds *what* was intended and never *how* to do it; `Application` holds the continuation,
because performing a close touches the engine, the window and the panel, and a gate that could do
any of that could not be tested without all three.

### 6. The title bar shows it

A prompt is the last line of defence, not the only signal. The window title gains the project name
(it now says `Untitled` rather than keeping the last project's name after File > New, which it did)
and a `•` when there is something to lose.

## Consequences

- `Engine` grows a `nlohmann::json projectBaseline_`. On the largest project in the repository that
  is about 689 KB of resident memory, held for one project at a time.
- File > New now also clears the edit history. It never did, so undo after a New would have replayed
  the previous project's commands into the new one — ADR-092's defect on the one path ADR-092 missed.
- `EditSystem::markSaved()` and `dirty()` have production callers for the first time.
- Opening an Engineering Lab now applies its overlay profile *inside* the gated action. Left outside
  it, cancelling the prompt would still have switched the overlays and the status line, and a Cancel
  that changed something is not a Cancel.

## Rejected alternatives

- **A `bool dirty_` set at every mutation site.** Decisive reason: its failure is silent and in the
  direction that loses work, and this codebase found three hand-maintained-list defects of exactly
  that shape on 2026-09-19 alone.
- **Comparing the serialisation against the file on disk.** Decisive reason: measured above — 279 to
  19,798 differing paths on a project nobody touched, so the prompt would fire every time.
- **A hand-maintained list of keys to exclude from the comparison.** Decisive reason: a forgotten
  entry produces a prompt that fires when nothing changed, which the brief rates worse than no
  prompt. Measuring the drift needs no list.
- **Sampling every frame.** Decisive reason: 31 ms on the owner's own film is half a frame.

## Revisit triggers

- A parameter written by OSC, MIDI or the AI control plane, in a sample window where nothing else
  touched the application, is absorbed as engine writeback rather than reported as a change. No such
  writer exists on a base value today; the first one is the trigger, and the fix is to report those
  writes as touches.
- A world effect that writes its parameter bases every frame *while the transport is stopped* would
  defeat the idle absorption for as long as the user sat still. The measured writeback is time-gated
  and stops when time does; one that does not is the trigger.
- If `projectDocument` ever costs more than about 100 ms, the throttle keeps the frame safe but the
  close itself becomes a visible stall, and the comparison should move to a digest.
