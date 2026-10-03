# ADR-1101: Optimize applies only ticked levers, as project ceilings, with an Undo

**Status:** Accepted (live optimizer, Stage 3). **Date:** 2026-10-03

The Performance panel's "Optimize..." opens a review of the live profiler's candidates computed from the last two
seconds (the same `optimizationCandidates` rules and the same `buildLiveProfile`), each with its ESTIMATED saving,
basis and visual risk; low-risk applicable rows start ticked. "Apply selected" turns each ticked lever into a ceiling
(`applyLeverToCeiling`) in the project's `live.overrides`; "Cancel" changes nothing; "Undo optimization" restores the
block exactly as it was. A diagnostic arm (`noprograms`) or a pass arm has no ceiling form and cannot be ticked.
Measured savings come only from `avgen --live-profile --verify-candidates`. The undo is the panel's own, not the
editor's global history.
