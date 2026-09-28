# ADR-952: The unsaved-changes sample waits for the film to stop

**Status:** Accepted; the owner decided it on 2026-09-28 ("skip the check during playback"), choosing it
over running the comparison off the main thread or comparing a digest.
**Date:** 2026-09-28
**Found by:** the QA pass's performance investigation (`docs/qa-pass/perf.md` section 7, W1)
**Amends:** ADR-440 (a project is dirty when it no longer serialises to what it was opened as).
**Implemented by:** `ui::DirtySampleSchedule` (`src/ui/unsaved_changes.hpp`),
`Application::sampleProjectDirtyIfIdle`, `Application::requestClose`
**Tests:** `tests/unit/test_unsaved_changes.cpp` and `tests/integration/test_project_lifecycle.cpp`
(`[adr952]`)

## Context

ADR-440 decides whether the project has unsaved changes by serialising it and comparing the result
with a baseline. Between closes it does that on idle frames, at most every `max(250 ms, 10 x its last
cost)`. On Glowmere Valley 3 (8,092 parameters) one sample costs about 36 ms on the UI thread. W1
measured it: `ui.build` p95 to p99 of 35 to 36 ms in every arm, 14 samples per paused run and 16 per
playing run, one every ~380 ms. The editor drops two frames about 2.6 times a second, playing or
paused. ADR-440 bounded the duty cycle at 10%. It did not bound the size of a single stall, and a
two-frame stall is visible in a film that is playing.

The owner chose: **skip the check during playback.**

## What playback does to the comparison (read, not guessed)

- **Timeline tracks and routes do not write the serialised values.** They modulate a parameter's
  evaluated value, and the save writes the *base*. ADR-440's 600-frame measurement found no timeline
  or route path among the paths that drifted.
- **ADR-386's per-frame writeback and the entity simulation do write serialised state.** ADR-440
  measured 20 such paths on 2026-09-20 (hero positions and two `visitor-beam` bases on GV2
  multicam). It does not exclude them by name. It **absorbs** them: on a sample where nothing touched
  the application, the baseline moves to the current document.
- **Measured again while writing this ADR:** 600 frames of GV2 multicam, and 300 frames of GV3
  from 0 s, 85.8 s and 194.7 s, now change **zero** paths of the project document. The drift that
  absorption exists for does not occur today on either film. The design below does not depend on
  that. It stays sound if the drift comes back.

## Decision

### 1. No periodic sample while the transport is playing

`DirtySampleSchedule::due` returns false on every frame where `Engine::isPlaying()` is true.
Touches are still **recorded** on those frames: an active widget, or an edit history with unsaved
commands. Only the serialisation waits.

This is equivalent to what ADR-440 did, not an approximation of it. On an untouched window, a
sample only moves the baseline over the engine's own writes. Absorbing them in sixteen samples or in
one sample at the end gives the same baseline. On a touched window, the touch is kept until the next
measurement. Because the comparison is always against the baseline, a real edit made during
playback is still in the document when that measurement happens. ADR-440's monotonicity (once
dirty, dirty until a save or a load) is unchanged.

### 2. Every discard measures on demand, mid-playback included

File > New, Open, Open Recent, Examples, Engineering Labs, a dropped project and quit all go through
`Application::requestClose`. That was already true (ADR-440 §4). `requestClose` calls
`Engine::projectDirty(touched)`, which is a fresh serialisation, never the cached answer. So a close
during playback gets a correct answer. If anything touched the application since the last sample,
a difference is reported as dirty. If nothing did, the whole playback's drift is absorbed and there
is no prompt. The only readers of `projectDirtyCached()` are the window title and the save's own
success check, which follows a save that has just reset it. Neither discards anything.

### 3. One sample on the first idle frame after playback stops

When the transport stops, the first frame with no active widget samples at once, whatever the
throttle says. After that the ordinary throttle applies. If the stop happens under a held button,
the sample waits for the release, as every sample does.

### 4. The title's marker is frozen while playing

The `•` in the window title shows the answer from before playback until the stop sample updates
it. An edit made during playback therefore shows in the title when the film stops, not while it
plays. The title is ADR-440's secondary signal. The prompt is measured on demand and is never
stale.

## Measured

Live editor on `examples/world/glowmere-valley-3.json`, 1280x720, 300 frames, `--profile-cpu`,
paused at and playing from the s66 wide (194.706 s), `AVGEN_DIRTY_TRACE=1`. The script is W1's
`uibuild.sh` with a before/after arm, run under `tools/gpu-lock.sh`. Three reps, interleaved. Before
is `4360244c`; after is this change.

Values are the median of 3 runs, with the range in brackets. Times are in ms. The paused runs'
`ui.build` p95 is bimodal (1.1 or 35 ms) because it depends on whether a run took 14 or 17-18
samples out of 300 frames. So the p99 is the column to read for paused runs.

| | samples / run | `ui.build` p95 | `ui.build` p99 | FRAME p95 |
|---|---:|---:|---:|---:|
| **playing, before** | 16-18 | 35.4 [34.9-35.5] | 36.3 [35.9-36.5] | 50.4 [42.8-51.3] |
| **playing, after** | **0** | **0.86** [0.86-0.90] | **0.92** [0.91-1.00] | **17.7** [17.5-26.6] |
| paused, before | 14-18 | 1.2 [1.1-34.6] | 35.9 [35.6-36.1] | 26.7 [26.5-50.6] |
| paused, after | 14-18 | 34.8 [0.8-35.4] | 36.3 [36.1-36.5] | 50.8 [26.7-51.2] |

Every run exited 0 with 0 GPU errors, and every sample read `touched=false dirty=false`. The
`ui.build` max (84-248 ms) is the first, loading frame in every arm. **Playing, the spike is gone:**
the p95 is 0.86 ms against 35.4 ms. **Paused, nothing changed, as decided:** the p99 is 36 ms in
both arms. Raw logs are in `build/qa-runs/live/uib-{before,after}-{idle,play}-r{1,2,3}.txt`
(gitignored) in the `qa/dirty-check` worktree.

## Consequences

- Playback no longer stalls for the dirty check. A paused editor still samples every ~380 ms, and
  its `ui.build` p95 and p99 are unchanged. The owner's decision covered playback only.
- A close during playback costs one serialisation (~36 ms on GV3) at the moment of the close, and
  the one frame after a stop costs the same. Both happen at a moment of user action, not in the
  middle of the film.
- **Attribution window.** While the film plays, the window a touch is attributed over is the whole
  playback rather than ~380 ms. On a project whose engine writes serialised state during playback,
  a click during playback (including the Pause button) attributes all of that playback's writes to
  the user. It can raise a prompt on a project nobody edited. ADR-440 had the same failure, over a
  shorter window, and for continuous drift (a walking herd) the two are the same. It errs toward a
  prompt, never toward losing work. Today it is moot, because both films drift by zero paths.
- The throttle and touch window moved out of `Application` into `ui::DirtySampleSchedule`, which is
  header-only and has no ImGui, Engine or clock in it. Its behaviour is now unit-tested; before this
  change it was only described.

## Rejected alternatives

- **Serialise off the main thread.** The document is built from live engine state that the next
  frame mutates. A background serialisation needs a snapshot, and taking the snapshot is most of the
  cost. The owner did not choose it.
- **Compare a digest.** This saves the comparison and the resident baseline. It does not save the
  serialisation, which is where the 36 ms goes. ADR-440 already names it as the fix for a close that
  becomes a visible stall (its third revisit trigger).
- **Skip the check while playing and trust the cached answer at a close.** Decisive reason: that is
  the stale "clean" that loses work. An edit made mid-playback would be discarded without a prompt.

## Revisit triggers

- If the engine starts writing serialised state during playback again (ADR-440's measured drift
  returns), and users click during playback, the longer attribution window can produce prompts on
  projects nobody edited. The fix is a sample on the frame input arrives, before it is dispatched.
- If the paused editor's ~380 ms stall becomes the complaint, it is the same cost with the same
  options (a digest does not help; a snapshot or a cheaper serialiser does).
- If `Engine::isPlaying()` stops meaning "the transport is advancing" (for example, a scrub mode
  that moves time without playing), the schedule should key on whether time advanced instead.
