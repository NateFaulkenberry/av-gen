# ADR-1103: A project minimum level, and "LIVE TARGET UNSUSTAINABLE"

**Status:** Accepted (live optimizer, Stage 4.2). **Date:** 2026-10-03

`live.minimumLevel` sets `InteractiveResolutionSettings::lowestLevel`: the controller never goes below it (a minimum
raised above the level in force moves up to it). At the minimum and still over the GPU budget at a decision, the
controller reports `unsustainable()`, and the Live panel says "LIVE TARGET UNSUSTAINABLE" with the cost and the budget
instead of degrading further; it clears at the first decision that fits. A pinned level is the user's explicit choice
and is honoured as before.
