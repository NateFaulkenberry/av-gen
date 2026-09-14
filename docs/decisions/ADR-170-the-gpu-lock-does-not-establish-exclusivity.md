# ADR-170: The GPU lock serialises agents, not the device, and a timing taken beside an open window is not evidence

**Status:** Accepted
**Date:** 2026-09-14
**Instruments:** ADR-150's `[.perf][representation]`

## What happened

Glowmere Valley 2's Phase 1 needed a baseline for the original scene. `[.perf][representation]` was
run under `tools/gpu-lock.sh` — the protocol followed exactly — at 1280×800, fixed t = 4.0 s, three
interleaved runs in one process session. It reported a baseline frame of **50.99 ms** and a scene
pass of **19.60 ms**.

ADR-151 reports **13.57 ms** and **10.88 ms** for that same arm.

The obvious reading is a 3.8× regression in fifteen commits. It is wrong, and the reason it is wrong
is the decision.

## The cause

`ps` during the run:

```
23.0  ./build/release/src/avgen
19.2  WindowServer
11.4  ./build/release/tests/avgen_render_tests
load averages: 6.33 11.02 15.72
```

**An interactive `avgen` window was on the GPU for the whole measurement.** The lock's own header is
honest about what it is for — two *agents* running GPU work at once corrupt each other's results,
measured on 2026-09-13 as 145 differing channels between two draws of one FrameTime. It takes a
directory-based mutex that only other agents consult. A human opening the app never asks for it, and
nothing in its design pretends otherwise.

So the run held the lock, obeyed every published rule, and measured a contended device.

## The decision

**Add one clause to the measurement protocol: the lock establishes exclusivity *among agents*, and a
GPU timing is evidence only if the device was also free of anything that did not ask for the lock.**

Concretely, and cheaply: `pgrep avgen` before a timing batch, and record the result beside the
numbers. A run that cannot say the device was quiet reports its milliseconds as a record of having
taken them, not as a measurement.

This sits alongside the rules already in force — one GPU one run; pin the resolution; never compare
timings across sessions; the first run after a build is warm-up; a probe must prove it established
the state it claims to measure. It is a special case of the last one. "The GPU is idle" is part of
the state a timing claims, and it was the part nothing checked.

## Why this is worth a record rather than a habit

Because the failure is silent and it points the wrong way. A contended run does not error; it
produces a plausible number with a plausible story attached, and the story — "something regressed" —
is the one that generates work. ADR-151's entire argument rests on differences of tenths of a
millisecond against a 2% floor. A 3.8× contamination does not perturb such an argument, it replaces
it.

The repo has been here before. `docs/renderer-forensics-report.md`'s measurement rules were each
bought with a wrong conclusion, and the shape is always the same: the instrument was fine and the
state it was pointed at was not what the experiment believed.

## What survives contention, and it is most of what Phase 1 needed

The same three runs reproduced **273,819 / 265,204 / 147,221 / 101,480** triangles across the four
arms, exactly, every time. Triangle and instance counts are computed on the CPU from the scene and
the frustum; they do not know the GPU is busy. They are comparable across sessions and they are what
`docs/glowmere-valley-2/03-baseline.md` records as the baseline.

That is also how the contamination was diagnosed rather than believed: **the structural quantities
moved 3.6% and the timings moved 280%.** Two numbers from one run disagreeing about how much changed
is what said the machine was the variable.

## The clause is necessary and not sufficient, and here is the number

Measured 2026-09-14, after this ADR had been in force for a day. Three invocations of one perf test
over one scene, GPU lock held, `pgrep avgen` clean on both sides of each, nothing else running:

| run | frame ms | triangles |
|---|---:|---:|
| 1 | 10.945 | 252,996 |
| 2 | 11.272 | 252,996 |
| 3 | 13.697 | 252,996 |

**A 25% spread with the scene held byte-identical.** Every run passed every rule this ADR states.

So the rule above is a floor, not a protocol, and the protocol is the one ADR-150 already used and
this ADR did not restate:

> **Arms must be interleaved inside one process.** A comparison between two invocations of the same
> binary has a noise floor of about 3 ms on this machine, which is larger than most effects worth
> measuring.

This was learned the way everything here is learned. A hero-cost A/B was run as two invocations —
one with the heroes, one with them hidden — and reported that **hiding six mushrooms made the frame
slower**. Interleaved inside one process, the same question answers cleanly: +0.20 ms and +0.66 ms.
The first version was not a noisy measurement of a real effect; it was not a measurement.

The confounder itself is still uninstrumented. Thermal state is the obvious candidate and nothing
records it.

## Consequence

**Glowmere Valley has no current frame-time baseline**, and the first task of its successor's Phase 2
is to take one on a quiet machine. That is a real cost of this ADR and it is the correct one: the
alternative is a budget derived from a number that was never measured.

## Verified vs assumed

**Verified:** the four arms' triangle and instance counts, three reproductions each. The `ps` output
and the load average, taken during the run. ADR-151's published figures, read from the record.

**Assumed:** that the contending `avgen` accounts for the whole 3.8×, rather than masking a real
regression underneath it. **Not established, and it cannot be from these runs.** The 3.6% triangle
growth since ADR-151 is real and unexplained, and a scene that grew could also have slowed. The
honest position is that this measurement cannot distinguish them, which is precisely why it is not
being used as one.
