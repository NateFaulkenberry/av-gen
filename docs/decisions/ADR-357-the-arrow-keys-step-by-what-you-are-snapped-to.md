# ADR-357: The arrow keys step by what you are snapped to

**Status:** Accepted
**Date:** 2026-09-19
**Follows:** ADR-102 (the transport's keyboard), ADR-182 (a probe that cannot fail), ADR-216 (song
structure), ADR-356 (the slice tool)

The owner:

> Im thinking shift + arrow keys moves a larger distance, arrow keys moves a shorter distance,
> depending on timeline view so. [...] So maybe snapping to beat: arrow left moves one beat left,
> shift + arrow left moved you 4 beats left

---

## 1. They were already bound, and bound wrong

This is not a new binding. `Application::handleTransportShortcut` has stepped the playhead with the
arrows since ADR-102 — but with a **fixed** pair: a frame plain, a beat with Shift, whatever grid the
strip was actually on.

So somebody working in beats got *frames* from the key they press most often, and somebody working
in frames got *beats* from the other one. Exactly backwards in one of the two cases. The owner's ask
is not "add arrow keys", it is "make the ones you have read the setting next to them".

## 2. The table

The step is the active snap mode's own unit; Shift is the next unit up. `seq::SnapMode` already has
exactly the four values this needs.

| snap mode | arrow | Shift + arrow |
|---|---|---|
| Beats | one beat | one bar (`seq::BakeOptions::beatsPerBar`) |
| Frames | one frame | one second |
| Markers | one marker | one **section** marker |
| Off | 1% of the visible span | 10% of the visible span |

**Bars are not a literal 4.** `beatsPerBar` is read from `seq::BakeOptions`, which is where
`snapSection`'s comment already claimed the assumption was "stated in one place" — it was stated in
two, and this would have made three. `snapSection` now reads it as well.

**Markers + Shift jumps to the next section boundary.** Marker spacing is irregular, so "four
markers along" is a distance nobody can predict: it could be four bars or four minutes. A section is
the coarser *landmark*, which is the same relationship a bar has to a beat — so somebody arriving
from Beats finds Shift meaning what it meant there. It falls back to every marker on a piece with no
sections, so the coarse key is never dead.

**Off is view-relative**, and only Off. With no grid there is no unit, so the only thing a step can
be proportional to is what is on screen: a fixed number of seconds is invisible at a four-minute view
and enormous at a ten-second one. At a 10 s view the pair is 0.1 s / 1 s; at a 240 s view it is
2.4 s / 24 s. Floored at one frame, because **turning the grid off must not turn the feature off** —
1% of a very short view rounds below a frame, and a key that moves the playhead by less than a frame
is a key that appears not to work.

Up and Down stay marker jumps at every mode. Folding them into the grid would leave the piece with
no way to jump between its landmarks whenever somebody was working in frames.

## 3. It returns a unit, not a time

`ui::arrowNudge` answers with a `NudgeUnit` and a count rather than a new second, and that is
deliberate: `Engine::beatBoundary` already walks the **analysed** beat grid where there is one and
falls back to the tempo where there is not, and a pure function handed a vector of beat times could
not reproduce that. So the decision lives where a test can read it and the walking stays where it
already works. Only the Off arm computes a time, and it is the only one that clamps for itself
(`ui::nudgedTime`); the rest clamp in `Transport::seek`.

## 4. Global, not panel-scoped

The arrows already moved the playhead from anywhere, and the playhead is one object. Scoping the
*distance* to which window had focus would make the same key travel different amounts depending on
where you last clicked, and a second handler on the Sequence panel would have to fight the existing
one for the event or move the playhead twice. What is panel-scoped is the *setting* it reads, which
is where the setting lives anyway.

Repeat, focus and text fields are unchanged: the transport's keys are tested only when
`wantsTextInput()` and `itemActive()` are both false, so typing a number into a field still works,
and the handler is deliberately outside the no-repeat block so holding an arrow walks the piece.

## 5. Controls (ADR-182)

- An arm that walks all four modes and demands four different units. **This is the feature**; a test
  that only exercised Beats would pass on an implementation that ignored the mode entirely.
- Beats: Shift is exactly `beatsPerBar` times the step, checked at 3, 4 and 7 — a test that only
  ever passed 4 could not tell reading the parameter from ignoring it.
- Frames, as the control for that: Shift is a second, which is *not* four times the step. An
  implementation that multiplied everything by four passes the Beats arm alone.
- Clamps at both ends, including a named check that the right-hand clamp does not wrap.
- An Off arm at two view widths, plus the one-frame floor.

8 cases, 68 assertions.

## 6. Not done

Logic also nudges a *selected region* with the same keys, and this panel has selection state. The
owner's words read as the playhead and only the playhead was built. Selection-nudge would be cheap
and is a follow-up, not a thing to ship unasked.
