# ADR-1104: A raise that does not hold is reverted, and waits longer next time

**Status:** Accepted (live optimizer, Stage 4.3). **Date:** 2026-10-03

The brief's §4.4: restore one feature, measure, revert if unstable. After a raise the controller is on probation until
its next decision (one dwell, with the window holding only the new level's frames). Over budget there: it moves back
down one level at once (`stats().reverts`), and the hold before that same raise is tried again doubles (up to 8x). The
measured step ratio it learns from the failed raise (ADR-1085) also makes the next prediction stricter. Raises are
still one level at a time after `raiseHoldFrames`; the 0.8 raise margin is unchanged (the owner's open tuning question
is not decided here).
