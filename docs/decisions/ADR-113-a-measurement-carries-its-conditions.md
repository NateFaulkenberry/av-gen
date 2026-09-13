# ADR-113: A measurement carries its conditions, and a comparison lives in one process

Status: accepted
Date: 2026-09-13

## Context

The renderer upgrade is a performance project, so every decision in it will be settled by a
measurement. The audit that opened it ([01-audit-and-baseline.md](../renderer-upgrade/01-audit-and-baseline.md))
found that the instrument was not good enough to settle them.

Three specific failures:

**The harness could not see a stutter.** It reported median, p10, p90 and min. A run of a hundred
10 ms frames with one 100 ms frame in it has a median of 10, a p90 of 10 and a p99 of 10. The one
frame the viewer would actually notice is invisible in every number the harness printed.

**Numbers had no conditions attached, and were compared anyway.** The audit found two recorded
Glowmere figures 28% apart — 23.79 ms in `docs/renderer-qa-2026-09-11.md`, 18.6 ms on the day of the
audit — and could not determine why, because neither recorded the revision, the build, the scene
version or the camera it was taken with. It recorded the gap as unresolved, which was the only
honest thing to do with it and is also a complete waste of two measurements.

**There was no protocol for an A/B.** The convention was "both arms in one session, interleaved,
three runs each", written in a document. Nothing enforced it, nothing recorded whether it had been
followed, and nothing computed whether a difference was large enough to mean anything.

## Decision

### 1. Two statistics, both named, neither standing in for the other

`Distribution` (rendering/render_stats.hpp) reports min, p10, p50, p90, p95, p99, max, mean,
variance, standard deviation, the 1% low and the 0.1% low. Two definitions are fixed here because
they are routinely confused, and confusing them changes conclusions:

- **A percentile is the nearest rank**: the element at `ceil(q*n) - 1` of the ascending sample. No
  interpolation. A frame time that no frame took is not a frame time, and at the 108-frame sample
  size this harness uses, interpolation moves p99 by more than most of what it is asked to detect.
  (The harness previously used `v[q * (n-1)]`, a *third* definition, in one place and nothing in
  the others.)
- **The "1% low" is the mean of the slowest 1% of frames**, not p99. p99 is one frame — the
  second-slowest of a hundred — and says where the tail begins. The 1% low is the average of
  everything in the tail and says how bad it is. They are always ordered `low1Percent >= p99`, and
  the gap between them is the shape of the tail. Both are reported. The JSON field is called
  `low1Percent_meanOfSlowest1Pct` so that the definition travels with the number.

At 108 measured frames the 0.1% low is exactly one frame and equals `max`. It is reported for
longer runs and documented as useless at this sample size, rather than quietly dropped.

### 2. Every record carries its conditions

`--bench-json <file>` writes the run as JSON: both clocks in full distribution, the per-pass GPU
medians, the CPU stage split, the workload (draws, submitted and logical triangles, visible and
culled instances, the LOD histogram, shadow casters, lights, particles, transient textures), and
the conditions: scene path and kind, camera **name and pose**, resolution, quality tier, engine git
revision and dirty flag, build type, backend, adapter, frame count, warm-up count, measured count,
offline frame rate, and a per-process session id.

The camera is recorded by pose and not only by name because the name does not identify the
workload: a directed camera moves, and "the Glowmere camera" covered three different views during
this investigation.

The revision is regenerated at **every build**, not at configure time. A revision captured at
configure time is whatever it was when someone last ran cmake, which is worse than no revision at
all: it invites exactly the comparison it was supposed to make checkable.

### 3. A comparison lives in one process

`--ab <phase>` runs baseline and arm **interleaved, A/B/A/B, in one process**, and differences each
pair. Pairing is the point: a machine that warms up, a cache that fills or another process that
starts halfway through is charged to both arms equally instead of to the change. The per-pair
deltas are printed, so a drift that survives pairing is visible rather than averaged into the
headline.

Cross-session comparison is **not offered**, and the session id in every record is what makes the
rule checkable by a reader of the file rather than a convention somebody has to remember.

### 4. A difference below the noise floor is not a result

The floors are 2% GPU and 4% wall — twice the within-session spread the audit measured over five
consecutive Glowmere runs (1.0% GPU, 3.0% wall). Below them, `clearsNoiseFloor` is false and the
log says "NOT A RESULT: inside the noise".

**The floor rises to whatever spread the session's own baseline blocks showed.** This is not
decoration. Measured the day this landed:

| scene | baseline spread across blocks, one session | a null A/B's reported "improvement" |
|---|---|---|
| Glowmere | 1.71% GPU, 1.63% wall | +0.34% GPU — correctly not a result |
| Constellation | **36.97% GPU**, 26.34% wall | **+15.97% GPU** — correctly not a result |

With the fixed 2% constant alone, the Constellation null A/B — the baseline compared against
*itself* — would have been certified as a 16% improvement.

### 5. `--ab none` is the null experiment, and it ships

`--ab none` makes both arms the baseline. The difference it reports is the harness measuring
itself, and it is the only honest way to state this mode's noise floor: the 2%/4% constants were
calibrated from five separate *runs*, and an interleaved within-process block is a different
measurement with a different floor. A null A/B that reports "A RESULT" is a broken harness,
whatever it says about any real arm.

## Consequences

**Constellation is not currently a usable benchmark scene, and this is new information.** Its
within-session GPU spread is 37%, and inside a single 240-frame block its p50 is 6.29 ms against a
p90 of 10.81 and a p99 of 12.98 — the per-frame cost varies by a factor of two, so the median lands
wherever the distribution's mass happens to fall. This means:

- The audit's "Discrepancy 1 — Constellation is 41% faster than the brief states" (3.60 ms measured
  against the brief's 6.09 ms) **is not supported**. Both figures, and the 10.16, 9.04, 7.80, 6.88,
  6.55, 6.42, 6.29 and 6.16 ms medians measured today, are inside one scene's own noise. The audit
  reached that conclusion from "five runs, 1% spread" — but that spread was **Glowmere's**, and it
  does not transfer.
- Anything Constellation is used to decide needs either a stabilised scene or a statistic that is
  not the median. The distribution the harness now reports is what makes the instability visible at
  all; the previous median/p10/p90 output showed a 41% difference and looked like a finding.

The 28% Glowmere gap remains unresolved and now cannot recur: a record without its conditions can
no longer be written.

The A/B mode found the audit's largest attributed effect without being told about it — `--ab
shadowmask` over two pairs reports −4.06 ms (−20.9%) whole-frame GPU, which is the pass the audit
warned not to touch.

## Alternatives considered

**Keep comparing against recorded numbers, with better bookkeeping.** Rejected: the failure is not
bookkeeping. Two of today's Constellation medians differ by 65% with identical conditions, so no
amount of recorded metadata makes a number from another session into a baseline.

**Compute the floor from a t-test on the frame samples.** Rejected for now. The frames within a
block are not independent — temporal history, caches and the scene's own time correlate them — so a
test that assumes independence would report significance from a correlated sample. The paired
block-median approach makes no distributional assumption, and the observed block spread is a direct
measurement of the quantity a test would be estimating.

**Fix Constellation instead of reporting its instability.** Deferred, and deliberately: the
instrument's job was to find out whether the scene is stable. It found that it is not. Changing the
scene in the same change that discovered this would leave nothing to check the finding against.
