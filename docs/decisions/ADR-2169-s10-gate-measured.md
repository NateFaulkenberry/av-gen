# ADR-2169: the S10 gate, measured: live frames within noise, offline output byte-identical, the panel and the command line render the same sequence, the editor looks the same

- Status: Accepted (2026-11-26), eng/engine-api-s10 (Engine API stage S10 gate; owner spec section 4 S10: "use measured
  before/after performance comparisons")
- Binaries: **before** = this branch's base, main `f1f62385f` (built in the S10 worktree before any change, bundle id
  `com.avgen.avgen.engineapis10`); **after** = `f2e3c4df5` (reps 1-2, the renders, captures and UI captures), the same plus
  the teardown log of `7e474b137` (reps 3-4, the contended show), and `7e474b137` (heavy reps 5-10). Both read the same shader and example trees.
- Method: three `tools/gpu-lock.sh` holds (`AVGEN_AGENT=engineapis10`), every comparison inside one hold, before and
  after interleaved; both binaries warmed on each project first (the before binary paid the cold compile: 643 s on the
  heavy project). Apple M2 Max, macOS 25G342.
- Records: `~/Desktop/av-gen-review/113-engine-architecture/04-implementation/s10/measurements/` (every log, JSON and CSV; the scripts beside it)

## Live behaviour (`--live-profile --mode live --target-fps 60 --measure 20`, the real editor loop with present, Fifo and projection)

Light: `examples/sonic-vfx/cymatic-plate.json`, 4 runs per arm (order B A, B A, A B, A B). Heavy:
`examples/digital-mosh/digital-mosh-live.json` (1,300 entities, 4,800 meshes, 2.8 M terrain triangles), 10 runs per
arm (the same order, then ABBA). Interval = time between presented frames. Means over runs.

| Project | Arm | Frames | Median ms | p95 ms | p99 ms (range over runs) | Interval sd ms | Missed vsyncs | CPU work p50 | GPU p50 |
|---|---|---|---|---|---|---|---|---|---|
| light | before | 4,803 | 16.59 | 18.54 | 19.68 (19.0-21.7) | 1.30 | 4 | 1.08 | 7.24 |
| light | after | 4,804 | 16.62 | 18.28 | 18.86 (18.8-19.0) | 1.13 | 4 | 1.06 | 7.24 |
| heavy | before | 11,995 | 16.61 | 18.52 | 21.48 (19.3-24.5) | 1.82 | 59 (0.49 %) | 4.28 | 14.06 |
| heavy | after | 11,992 | 16.63 | 18.53 | 22.22 (19.4-24.5) | 1.83 | 52 (0.43 %) | 4.22 | 14.31 |

Reading: within noise. The heavy project's p99 is bimodal in BOTH arms (each run lands near 19.5 or near 24 ms,
depending on whether its 12th-worst interval is a late frame), so its mean moves with how many runs land high: 4 of
10 before, 6 of 10 after. Frames of 23 ms or more: 146 before, 142 after. GPU p50 +0.25 ms (1.8 %), inside the
renderer's stated GPU noise floor; the frame interval does not move. Pacing (interval sd) is the same.

**The show path was live in every after run**: these projects open live input, so the show was active (`show:
started (liveInput)`) and Studio took the machine lock at the show's first frame in 0.09-0.25 ms (held by the
gpu-lock.sh ancestor).

**A show against another process's lock** (a scratch `TMPDIR` whose `avgen-gpu.lock` a live `sleep` held as
`fake-agent`; the light project, live mode): the show ran -- 1,201 frames, median 16.61, p95 18.39, p99 18.91, 1 missed
vsync, the same as the uncontended runs. Log: `show: the machine GPU lock ... is held by 'fake-agent' (pid 21153); the
show runs anyway`, then at exit `23 attempt(s), the dearest 0.33 ms, shared with 'fake-agent'`. The foreign lock was
untouched afterwards (same pid and holder).

## Offline output (`--render <dir> --range 0:1 --size 320x180 --fps 24`, 24 frames)

| Project | Sequence hash before | after | Frame files (sha256 of the 24 sha256s) |
|---|---|---|---|
| examples/cymatic-world/cymatic-world.json | `f7f2e1d7b1971c13` | `f7f2e1d7b1971c13` | identical |
| examples/rd-pins/rd-pins.json | `6425b91e16f7b9cf` | `6425b91e16f7b9cf` | identical (and again in the third hold) |
| examples/experiments/phase-wall/phase-wall.json | `a90312fd2319af10` | `a90312fd2319af10` | identical |
| the S8 PoC project (a scratch copy, render block 320x180, 24 fps, 0-1 s) | `e0f66ae2538558a3` | `e0f66ae2538558a3` | identical |

**The Render panel and the command line render the same sequence.** The PoC copy through `--render-in-app` (the Render
button, in the editor, stepped by the frame loop) and through `--render` (its own render block): `e0f66ae2538558a3`
and the same 24 files, both binaries.

## `--capture` (the probe, 04-poc-plan section 5): unchanged -- the decision is ADR-2168

Before and after, two runs each: one PNG, byte for byte. A render job's frame at the same time differs in 22.85 % of
pixels (ADR-2168).

## The editor's look (`--capture-ui`, 1440x900 points, frame 90)

Light project and the PoC copy, before and after. The panels, layout, text and widgets are pixel-identical; the
differing pixels are inside the viewport (the scene animating on the parked wall clock) and in the status bar's
timing digits (fps, ms) -- 0.17 % of the frame on the PoC project, 8.5 % on the live-animated light project, all in
those two regions (diff images in the records folder).

## CPU-only runs (ADR-2166)

| Run | Before | After |
|---|---|---|
| `--audit-routes` | no device, 0.07 s | no device, 0.07 s |
| `--pathtrace` (64x36, 1 spp) | device + pipeline cache opened, 0.73 s | **no device**, 0.47 s |
| `--headless --ai-script` (session.info) | `--frames 1`: device, renderer, 0.60 s | `--frames 0`: **no device**, 0.28 s |
| `--render` (warm caches, rd-pins) | 1.51 s | 1.64 s (see below) |

`--render` no longer builds the live renderer, so on a cold cache it no longer pays its compile (the first pass:
27-65 s before against 3.3-3.7 s after, though the before arm also filled the caches). On a warm cache it is 0.13 s
slower: its pipeline-cache hits rose from 312 to 597 -- with no live renderer holding the pipelines, the job's
warm-up renderer's pipelines are released before the real renderer asks for them, and come back from Dawn's disk
cache instead of its in-memory one [assumed cause; output identical]. `avgen serve`/`call` already ran this way.

## Not measured

- Neon Dreams (its generated scene is not in a worktree); Digital Mosh Live is the heavy project.
- A render job running beside a live show (05-live-safety 4.2's proposed check): S10 makes it impossible in-process.
