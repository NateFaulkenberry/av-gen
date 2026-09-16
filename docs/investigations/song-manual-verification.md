# Verification report: the song direction workflow, as built

**Date:** 2026-09-16
**Purpose:** the engineering half of `docs/song-direction-manual.md`. Every claim in that manual is
either verified here or absent from it.

**Method.** Each row was checked by reading the implementation and, where a test or a run could
settle it, by running one. "Verified" below means a test exercises it or I ran it; "implemented"
means I read the code path end to end but did not execute it; "not implemented" means I looked and it
is not there.

---

## Verification matrix

| # | Scenario | Expected | Status | Evidence |
|---|---|---|---|---|
| 1 | Import audio | audio appears on the timeline | implemented | `SequencePanel::drawImportPopup`, `onOpenAudio` |
| 2 | Analyze song | sections generated | **verified** | `analysis::detectStructure`; `test_song_beginner_path.cpp` |
| 3 | Sections become a film | authored timeline exists | **verified** | `song::timelineFromStructure`; same test |
| 4 | Sections shown in the lane | blocks with names | **verified by capture** | Sections lane populated: Verse, Verse 2, Chorus … |
| 5 | Song Mode with no performer rules | full camera sequence | **verified** | `test_song_beginner_path.cpp`, 86 assertions |
| 6 | More than one camera used | ≥2 distinct cameras | **verified** | same test asserts `used.size() > 1` |
| 7 | Add a shot | a new shot appears | implemented | "Add Shot" button; "Add shot at end" |
| 8 | Move a shot | start moves, duration kept | **verified** | `seq::moveShot`; `test_shot_editing.cpp` |
| 9 | Trim a shot | one edge moves, the other holds | **verified** | `seq::trimShotStart`/`trimShotEnd`; the end is passed in, not recomputed |
| 10 | Split a shot | two shots, durations sum to the original | **verified** | `seq::splitShot`; refused near either edge |
| 11 | Duplicate a shot | a copy, after the original in time | **verified** | `seq::duplicateShot` |
| 12 | Delete a shot | removed, others untouched | **verified** | `seq::removeShot` |
| 13 | **Shot crosses a section boundary** | allowed, section unchanged | **verified by construction** | `seq::Shot` has **no section field**; nothing validates a shot against a section |
| 14 | Many shots in one section | allowed | **verified by construction** | same — sections do not own shots |
| 15 | Overlapping shots | first in array order wins | implemented | `Sequence::shotAt` returns the first containing span |
| 16 | Camera per shot | shots can differ | implemented | `Shot::camera` is a `ShotCamera` per shot |
| 17 | Keyframe a camera | position/target/lens over a shot | implemented | `CameraKind::Keys`, `CameraKey{position,target,focalLength,interp}` |
| 18 | Keys survive moving a shot | yes | implemented | key times are **relative to the shot start** |
| 19 | Look-at an actor | camera tracks a performer | implemented | `ShotCamera::lookAtActor`, `lookAtHeight`, `lookAtWeight` |
| 20 | Camera presets | useful starting configurations | implemented | `cameraFromPreset`: Isometric, Follow, Wide, Close, TopDown, Tracking, Reveal |
| 21 | **Re-directing keeps authored shots** | only generated shots replaced | **verified by code** | `CameraShot::Origin{Authored,Directed}`; `erase_if(origin == Directed)` |
| 22 | Transitions | cut, dip, match cut | implemented | `Shot::in`/`out`, `TransitionKind` |
| 23 | Per-shot parameter automation | arbitrary tracks inside a shot | implemented | `Shot::tracks`, times relative to shot start |
| 24 | Performer actions independent of Song Mode | Song Mode unaffected | **verified** | `test_song_beginner_path.cpp` clears the table first |
| 25 | Performer table round-trips | saved and reloaded | **verified** | `test_section_actions.cpp` |
| 26 | Section type change reaches the director | different treatment | **verified** | `test_song_plan_adapter.cpp` integration case |
| 27 | Custom section type usable | e.g. "Ocean Ambience" | **verified** | `test_song_plan_adapter.cpp` `[custom]` |
| 28 | Analysis re-run preserves edits | overrides survive | implemented | `song::reanalyze`, per-field provenance; `test_song_reanalysis.cpp` |

---

## Discovered limitations

1. **No UI editor for performer rules.** The table is authored by hand in the project JSON. The
   checkbox that consumes it is correct and explains itself, but there is no way to create a rule
   from the interface. *Subsystem: UI. Smallest fix: a small list editor in the Sequence panel.*

2. **Overlapping shots resolve by array order, not by any authored precedence.** `shotAt` returns the
   first match. This is deterministic but not explained anywhere in the interface, and array order is
   not something the lane displays. *Subsystem: timeline logic / UI. Smallest fix: prevent overlap on
   drag, or show precedence.* **Not a defect** — just undocumented, and now documented.

3. **The import popup cannot be captured.** `--capture-ui` photographs the editor, but a popup needs a
   click to open and no `--ui-script` arm opens one. The performer checkbox is therefore the one
   control in this workflow whose appearance is unverified. *Subsystem: test tooling.*

4. ~~**No end-to-end test of the manual editing path.**~~ **Closed.** The six gestures were inline in
   `SequencePanel` and unreachable without ImGui, so they moved into `seq/sequence.hpp` -- matching
   how section edits have lived in the model since ADR-247 -- and the panel now calls them.
   `tests/unit/test_shot_editing.cpp` drives the same code the pointer does.

   The extraction found one thing worth recording: the panel's Duplicate places the copy after the
   original **in time**, not on top of it, and the first version of the extracted function dropped
   that. A copy on the same span would never play, because `shotAt` takes the first match -- the
   gesture would look like it had done nothing. Now asserted.

---

## Contradictions found between documentation and behaviour

* `docs/sequencer.md` described the generator as waiting on "a `SectionDirectionTable` from the
  Director decision layer". Both halves were stale: the layer exists, and the type is now
  `SectionPerformanceTable`. Corrected in the same commit as the rename.

* ADR-216 and ADR-249 name `SectionDirection` throughout. Left intact as historical records with a
  dated terminology note, rather than rewritten.

---

## Recommended fixes, in priority order

1. ~~End-to-end test of the editing path~~ -- **done**, see limitation 4.
2. **A `--ui-script` arm that opens a popup** (limitation 3). Small, and it closes the last
   unverifiable UI surface in this workflow.
3. **A performer-rule editor** (limitation 1). Real but not blocking: the system works, and the
   manual documents the JSON route.
4. **Decide the overlap rule's presentation** (limitation 2). Behaviour is correct; only its
   visibility is in question.

None of these blocks the manual. All four are recorded rather than fixed, because this task was
documentation and the fixes are separate work.
