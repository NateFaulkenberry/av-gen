# ADR-2031: The UBSan job builds at -O1

- Status: Accepted (eng/ci-fixes, 2026-11-12; the owner chose this over splitting the job).
- Code: the `ubsan` configure preset (`CMakePresets.json`): `CMAKE_C_FLAGS_DEBUG`/`CMAKE_CXX_FLAGS_DEBUG` = `-O1 -g`.
- Docs: `docs/development/ci.md`, Sanitizers, "UBSan: its own job".

## Context

ADR-2030 removed the lunar report that halted every UBSan shard about a third of the way through the set. With the
whole set running again, the job is near its limits:

- On the last green run (2026-10-31) the three shards took 162, 213 and 248 minutes against `run-suite.sh`'s
  330-minute shard limit. The job's limit is 355 minutes, and that includes a build of about 40 minutes.
- Since then, common cases are about 13% slower and there are about 150 new cases. The slowest shard is
  estimated at 280-300 minutes plus the build.
- A GitHub-hosted job cannot run longer than 6 hours, so the time limit cannot be raised further.

## Decision

The `ubsan` preset compiles at `-O1 -g` instead of the Debug default `-g` (`-O0`). It stays
`CMAKE_BUILD_TYPE=Debug`, so `NDEBUG` is unset:

- assertions stay on;
- the cases that key on `NDEBUG` (`kOptimised` wall-clock ceilings) behave as before;
- the test set is unchanged.

`-fno-omit-frame-pointer` (cmake/Warnings.cmake) keeps the stack traces usable.

The `asan` and `tsan` presets stay at `-O0`. ASan's five-job plan is balanced on `-O0` timings, and TSan's job is far
inside its limit.

## Alternatives

- **Split UBSan into two jobs, each with its own build and its own 6-hour limit.** This keeps `-O0`'s full coverage. It
  costs an extra build each night and a fifth macOS slot, which the ASan jobs already fill. The owner preferred
  `-O1`.
- **RelWithDebInfo.** This sets `NDEBUG`, which turns assertions off and changes which `kOptimised` cases run.

## Consequences

- The UBSan job runs roughly twice as fast. The hosted timing is proven by the next nightly, not locally.
- The accepted cost: the optimiser can fold away or move a little undefined behaviour before a check sees it, so a
  report that `-O0` would raise can occasionally be missed. The ASan job's recoverable UBSan checks at `-O0` remain
  a second net over the default set, minus the two cases its plan skips.
