# ADR-2135: a live ladder that cannot reach its target says so, once, in the log

- Status: Accepted (2026-11-24), eng/clouds-quality (Neon Dreams editor performance, fix 1; investigation
  `112-neon-dreams/02-ui-perf-investigation/REPORT.md`: "the quality ladder reaches its lowest level in about 40 s, is
  still about 9x over budget, and never says it can't reach the target")
- Extends ADR-1103 (the controller's `unsustainable()` flag, shown only as the Live panel's "LIVE TARGET UNSUSTAINABLE").
- Code: `src/app/interactive_resolution.{hpp,cpp}` (`InteractiveResolution::takeUnsustainableNotice`),
  `src/app/application.cpp` (the warning, after the controller's `note`).
- Tests: `tests/unit/test_cloud_live_quality.cpp` ("a ladder at its lowest level and still over budget says so once",
  `[adr2135]`).

## Context

ADR-1103 already knows when the controller is at the lowest level it may use and still over the budget: it sets
`unsustainable_`, and the Live panel shows a flag. Nothing else does. A headless or scripted editor run, a log a
performer sends, and the investigation's own GUI runs had a drop line for every level on the way down and then silence
-- Neon Dreams sat at Emergency at 272 ms against a 29 ms budget with no line saying the target was out of reach.

## Decision

`InteractiveResolution::takeUnsustainableNotice()` returns true exactly once per controller run: the first time
`unsustainable()` is true after a `reset` (a new ladder, the controller switched off and on). The application asks it
after every `note` and, when it answers, logs one warning:

`live quality: cannot reach the 30 fps target -- at Emergency, the lowest level, the GPU frame is 196.0 ms against a
29.3 ms budget (6.7x over); heaviest pass 'clouds' 150.2 ms. Nothing is left to lower; the editor stays at Emergency
(said once)`

The heaviest pass is the last completed frame's, its labels summed (ADR-2131's bands are several `clouds` marks).

Once and not on every verdict: the controller re-judges the bottom once per dwell (ADR-1103), so a line per verdict
would be one every half second for as long as the editor is open. A later clear and return is not said again in the
same run -- the first line already told the reader the target is out of reach at this size.

## Consequences

- The Live panel's flag is unchanged (it is the UI's; this is the log's).
- The pinned level (`--live-quality`) never says it: the controller is off, and `unsustainable()` is ADR-1103's
  automatic-only verdict.
