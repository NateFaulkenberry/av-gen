# ADR-2232: the lab runner and its measurement record, including unwritten timestamp slots

- Status: Accepted (2026-11-27), eng/115-labs (Parallel Engine Programme, Agent 4; Stage A report §2.5, §3.2)
- Code: `tools/labs/run.py`; `app/live_profile.{hpp,cpp}` and `app/live_profile_run.cpp`, `app/application.cpp`
  (`LiveProfileFrame::unwritten`, `LiveProfileRecord::unwrittenSlots`, the trace's `unwrittenSlots` column)
- Tests: `tests/unit/test_live_profile_unwritten.cpp` (`[adr2232]`), `tests/unit/test_live_profile.cpp` (the trace row)

## Context

Every lab and every benchmark in the programme needs the same method (60 warm-up and 200 measured frames, a fixed
quality level, interleaved repeats, GPU totals, no compile in the window, a measured noise floor) and the same record.
Agents had each written their own scripts. And one instrument fault was silently feeding budgets: on this Dawn/Metal
path a compute pass's timestamp slot comes back unwritten (the volumetrics spike: `clusters`, `vfx.inject`,
`vfx.integrate` all read 0). `FrameTimeline` already knew which slots were unwritten (`unwrittenLabels`); the live
profile reported their 0 ms as the passes' cost.

## Decision

1. **`tools/labs/run.py`** (standard library only), over public flags only:
   - `cases <lab>` renders each case through `avgen --headless --lab-case <lab>:<n> --render` and records the exit
     code (the binary's own), the sequence hash, the camera it looked through and every frame's md5;
   - `bench` and `noise` run `avgen --live-profile --mode headless` at a named quality level (`auto` is refused),
     `--warmup-frames 60`, 200 measured frames, with a discarded compile run first and the arms **interleaved** rep by
     rep; a rep that compiled a pipeline inside the window is `valid: false`;
   - the noise floor of an arm is the spread of its rep medians; a difference within twice it is no change;
   - every row is `avgen-lab-run/1` with pwd, HEAD, dirty count and load before and after, in `<out>/results.json`;
   - it refuses GPU work unless the machine lock's holder is itself or an ancestor (`src/api/gpu_lock.hpp`'s rule),
     so it runs under one `tools/gpu-lock.sh` hold per batch, and every row records the holder (`gpuLock`);
   - `suite cpu|gpu|both [--midi-protocol]` runs the test binaries the same way. **The CPU suite needs the lock too**
     (coordinator, 2026-11-27, reversing a one-day exemption): `avgen_tests` encodes video through the frameworks the
     GPU work uses, and run beside it has crashed and failed determinism falsely (docs/testing.md 29). `--no-lock-check`
     never applies to `suite`. An exit 0 with no verdict line is recorded as not a pass.
2. **Unwritten slots are reported as unwritten.** Each live-profile frame carries `unwritten` (the labels); the record
   carries `unwrittenSlots` (label, frames) in its JSON (always present, so an empty list is a statement) and a limits
   line naming them; the headless trace gains a last column `unwrittenSlots` (the count). The pass's 0 ms stays in the
   pass medians, because the intervals still partition the frame, and the limit says it is not a measurement.

## Consequences

- A compute pass is budgeted by amplification or a frame A/B, never by its own label (plan rule 8), and the record
  says so wherever it applies.
- The trace's column count grows by one; readers keyed by column name (the runner, `trace_stats.py`) are unaffected.
