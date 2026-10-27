# ADR-1803: Every `AVGEN_*` variable a run had is logged, recorded in a live profile and documented

- Status: Accepted (eng/qa-pass-2, 2026-10-27)
- Code: `src/core/env_overrides.{hpp,cpp}`, `src/app/main.cpp`, `src/app/live_profile*.{hpp,cpp}`,
  `tools/live_perf_baseline.py`, `docs/help/reference-command-line.md`.
- Tests: `tests/unit/test_repo_hygiene.cpp` ("every AVGEN environment variable the source reads is documented").

## Problem

`src/` reads 39 `AVGEN_*` variables. About a dozen are A/B arms that restore an older path so a change can be measured
in one binary (`AVGEN_LEGACY_PROCGEN`, `AVGEN_NO_CONTROL_REPLAY`, `AVGEN_SYNC_PIPELINES`, ...). Each is read once into
a function-local static, nothing recorded that one was set, and the command-line reference named 14 of them (CA-004 of
the 2026-10-14 audit). One left exported in a shell turns every later profile into a measurement of a different
program, and the profile looked like any other.

## Decision

- `avgen` logs every `AVGEN_*` variable in its environment as a warning when it starts (`environment overrides:
  ...`), except `tools/gpu-lock.sh`'s own (`AVGEN_GPU_LOCK_*`, `AVGEN_AGENT`).
- A live profile records them in `conditions.environment` and prints them in its text report.
- `tools/live_perf_baseline.py write` refuses runs with any override, and runs not on AC power.
- The command-line reference lists every variable; the measurement arms have their own table. A hygiene test fails when
  `src/` reads a variable the page does not name.

## Consequences

- Nothing changes for a run with no `AVGEN_*` variable set.
- The arms stay: each is how a past optimisation's before/after is re-measured (ADR-170's single-binary rule). Retiring
  one is its owner's decision, with that measurement in hand.
