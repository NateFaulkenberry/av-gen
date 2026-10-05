# ADR-1115: A warm-up never starts before the song

- Status: Accepted
- Amends ADR-397 (the shared pre-roll schedule). Found by ADR-1114's investigation
  (`docs/development/gpu-sim-seek-investigation.md` §3, fix option P0).
- Builds on ADR-360 and ADR-395 (the bounded, opt-in particle warm-up).

## Problem

`planPreRoll` stepped back `n * step` from the arriving second T, with no clamp at 0. A
`--particle-warmup 240` render opening at 2 s therefore simulated 4 s of particles, 2 s of which lay
*before the song began*. The warm-up exists to give a partial render what a render that **played**
to T would hold (ADR-360, ADR-395), and that render started at t = 0 with empty pools. The extra
pre-song seconds are a field no render ever held. That is why the investigation measured the Tree of
Life at 2 s as worse with a 240-frame warm-up than with none.

Nothing asks for particles "already running" at t = 0. The flag's help text, ADR-360, ADR-395,
ADR-521, the particle lab and the weather examples all describe the warm-up as standing in for play
from 0.

## Decision

The roll is `min(requested, T)` long: `planPreRoll` takes the whole steps that fit between 0 and T
(`floor(T / step)`, with a 1e-6 step epsilon so that T = k * step keeps its oldest frame at exactly
0). At T = 0, or less than one step in, the plan is empty. The cap, the step rule and
`arrivalFrameIndex` are unchanged.

`tests/unit/test_pre_roll.cpp`, "a pre-roll never reaches before the song starts": 240 requested at
2 s gives 120 frames, the oldest at 0. Without the clamp it gives 240. The existing cap case now
arrives at 10 s so that the cap, not the clamp, is what binds.

## Consequences

- A `--particle-warmup` render whose range opens before `n * step` seconds (4 s at 240 frames, 60 fps)
  changes. It now matches a full render's pool *counts* at the head of the range, as ADR-395 measured
  for ranges past the warm-up window. Pixels still differ for ADR-395's slot-assignment reason.
- Renders with no warm-up (the default, and the editor's only behaviour) are unaffected.
- The temporal-media consumer ADR-397 anticipated inherits the same rule: no history from before 0.
