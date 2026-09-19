# ADR-356: Cmd+click slices, and Cmd was not the key ImGui thought it was

**Status:** Accepted
**Date:** 2026-09-19
**Follows:** ADR-089 (the sequence strip), ADR-092 (one undo stack), ADR-103 (the audio lane is a
scrub), ADR-182 (a probe that cannot fail proves nothing), ADR-216 (song structure), ADR-247 (the
section timeline)

The owner asked for one thing:

> make cmd + click into a slicing tool for cutting shot clips, audio clips, sections, etc - all
> lanes with clips or sliceable parts. Make it obey the active snapping rules

---

## 1. The operations mostly existed; the decision in front of them did not

`seq::splitShot` and `song::splitSection` were already there and already reachable from two context
menu items. What was missing was the single question a slice has to answer before any of them runs:
which lane, which block, where the cut lands once the grid has had its say, and whether that cut is
legal.

That question is now `ui::planSlice`, and it is pure. The panel supplies a pointer position and
carries out the answer. The alternative — asking it inline at the click, the way the menus asked it
inline at their enabled tests — is what had already gone wrong: **neither Split item snapped, and
neither was undoable.** Three faces of one operation (the shots menu, the sections menu, the section
inspector's "Split at playhead" button) had each grown their own idea of what a split is.

All three now plan with `planSliceAt` and perform with `performSlice`. The menu's enabled test is
literally `plan.legal()` — the same call the click makes — so an offered row and a refusing
operation cannot disagree.

Two lanes had no split at all and one now does for the first time:

| lane | operation | new? |
|---|---|---|
| Shots | `seq::splitShot` | no |
| Sections | `song::splitSection` | no |
| Audio clips | `audio::splitClip` | **yes** |
| Overlays (lyrics) | `seq::splitOverlay` | **yes** |
| Actor clip cues | `seq::splitActorClip` | **yes** |

`audio::splitClip` is the one with arithmetic worth isolating: a clip is a window onto a file, so
cutting it at a timeline second must advance `inSeconds` by the same amount it advances
`startSeconds`, or the later half plays the wrong part of the take — which sounds like a skip, not
like a cut. An open clip (`durationSeconds == 0`, "the rest of the file") stays open.

## 2. The section lane keeps its own grid, because a cut makes a thing that is dragged on it

The strip has two snaps on purpose. `snapSection` says why: *"a person dragging a section boundary
and a person dragging a shot are not necessarily asking for the same grid."*

A slice on the section lane **creates a section boundary** — the identical object, in the identical
list, that the very next gesture will drag. If the cut landed on the strip's grid and the drag that
followed it moved on the section grid, then nudging a boundary you had just placed would move it
somewhere you did not put it. So: **a cut lands on the grid the thing it creates is dragged on.**

This is not a new opinion. The section inspector's "Split at playhead" button already used
`snapSection`; the context menu's "Split here" used no snap at all. The rule makes the three agree.

The two grids also agree by default (`snapMode_` = Beats, `sectionSnap_` = beat) and only diverge
when somebody has deliberately set them apart — which is exactly when they would notice.

## 3. The audio lane is still a scrub

ADR-103 keeps moving and trimming a clip in the Audio... popup because making them drag gestures
stole the click that scrubs, and the lane's promise is that clicking a moment plays it.

**A slice does not take that click.** It is Cmd+click and a menu row, and neither is the plain left
press the promise is about. So the audio lane gains its first in-place edit without giving up
anything ADR-103 protected. Moving and trimming stay where they are.

## 4. The block is found at the pointer; the cut is tested against that block

With a coarse grid on and the pointer near a shot's end, the snapped time can land past that end and
inside the *next* shot. Cutting a block nobody pointed at is worse than refusing, so the planner
finds the block at the raw pointer position and refuses if the snapped cut leaves it.

## 5. Nothing refuses silently

`SliceRefusal` is a reason, not a bool, and each one has a line for the status strip: "nothing to
slice in this lane", "nothing under the pointer to slice", "too close to an edge: a slice there
would leave nothing". A greyed menu row and an inert Cmd+click now mean the same thing and say so.

## 6. **`io.KeySuper` is not Cmd. It is Ctrl.**

The part of this that would have shipped broken.

`ImGuiIO::AddKeyEvent` swaps Cmd and Ctrl under `ConfigMacOSXBehaviors`, which defaults on for
`__APPLE__` (imgui.cpp: *"MacOS: swap Cmd(Super) and Ctrl"*). A real Cmd press reaches ImGui as
`ImGuiMod_Super`, is swapped, and sets **`io.KeyCtrl`**. `io.KeySuper` is what a physical *Ctrl*
press sets — and ImGui then aliases Ctrl+left into a right click on its own
(*"Super+Left Click aliased into Right Click"*).

Written the obvious way, `ImGui::GetIO().KeySuper`, the slice branch fired on the gesture macOS had
already turned into a right-click and **never on Cmd+click at all**. Every unit test passed. The
menu capture looked perfect. `ai_panel.cpp` had been hedging with `KeySuper || KeyCtrl` for what is
presumably the same encounter.

What caught it was a scripted arm (`--ui-script slice`) that holds the modifier, clicks a shot, and
reports the shot count before and after in so many words — including when it did not change. It
reported *"6 shot(s) before, 6 after — THIS ARM MEASURED NOTHING"* four times across two wrong
theories before reporting *"the gesture cut a shot in two"*. ADR-182, exactly: the probe's value was
entirely in its ability to say no.

The swap runs in **both** directions, which is the trap within the trap. The rule that survives it:
*product code reads `io.KeyCtrl` to mean Cmd; a harness standing in for the backend sends
`ImGuiMod_Super` to mean Cmd.*

## 7. What was verified, and how

- `ui::planSlice` and the four split primitives: 11 unit cases, 128 assertions, a control on every
  arm — a refusal paired with a legal twin a hair further from the edge, snapping paired with
  snapping off, and the audio arm probed by mutation (dropping the `inSeconds` advance fails it).
- The menu rows: captured with `--ui-script slicemenu --capture-ui`, in both the enabled and the
  greyed state. Not clipped; the shortcut column reads "Cmd+Click".
- The gesture: `--ui-script slice`, reported above.

## 8. Not done

The owner's *"eventually this may turn into a palette of available tools"* — a Logic-style
assignable click tool with dropdowns in the transport area. Not built. A half-built palette is worse
than none, and the thing it would be a palette *of* is one tool.
