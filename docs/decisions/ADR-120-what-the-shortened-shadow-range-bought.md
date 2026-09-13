# ADR-120: what the shortened shadow range bought, settled

Status: accepted

## Context

ADR-112 shortened the directional shadow range so the coarsest cascade's texel is 8 cm rather than
2.36 m. It reported, as an aside, that this was also faster -- 18.68 -> 17.89 ms, -4.2% -- and then
said of its own evidence:

> The two arms were run in blocks rather than interleaved run-by-run, because switching arms needs a
> rebuild. The within-arm spreads are smaller than the gap, but this is weaker evidence than a
> properly interleaved A/B and is labelled as such.

The audit's §3.2.2 then declined to credit the shadow branch with any of the wave-1 timing, because
the step that would have shown it spanned two sessions and §3.1 forbids that comparison. What was
left on the record was one deterministic counter -- shadow draws 189 -> 34 -- confounded with LOD0,
which also removed draws. The audit named the experiment that would settle it and assigned it here.

## The measurement

`--ab shadowrange` (ADR-117), which sets `shadowTexelTarget = 0` and so restores the pre-ADR-112
rule of three scene radii, interleaved baseline/arm/baseline/arm/baseline/arm in one process.

```
scene      examples/world/glowmere-stylized.scene.json
size       1280x800, realtime tier
build      cmake --preset release, revision dfbf7f5
protocol   3 pairs x 120 frames, first 12 discarded, GPU timestamp queries
machine    Apple M2 Max, Dawn/Metal, tools/gpu-lock.sh held throughout
```

| | baseline (ADR-112 on) | arm (`shadowTexelTarget = 0`) |
| --- | --- | --- |
| GPU frame median | **13.30 ms** | **14.22 ms** |
| scene pass | 10.81 / 10.68 / 10.88 | 11.73 / 11.73 / 11.67 |
| shadow pass | 0.33 / 0.33 / 0.33 | 0.33 / 0.33 / 0.33 |
| shadow draws | 34 | 99 |

**-0.92 ms, -6.90%**, against a baseline block spread of 1.48% and a 2% floor. The three per-pair
deltas are -0.92, -1.18, -0.79: same sign, same order, no pair carrying the result.

## What this settles, and what it corrects

**ADR-112 is worth 6.9% of the frame, not 4.2%.** Its own figure was taken before LOD0. The rule's
saving is roughly fixed in milliseconds -- it is distant fragments not running a PCSS blocker search
-- and LOD0 made the frame smaller, so the same saving is now a larger share of it. A stale
percentage understating a change is the mirror of the stale percentages this wave exists to
re-derive, and it is worth noticing that the error ran in the safe direction only by luck.

**Almost all of it is the scene pass.** 0.85-1.05 ms of the 0.92 ms. That is the mechanism ADR-112
predicted: with a shorter range a distant fragment falls outside the last cascade, `shadowLookup`
reports invalid, and the blocker search never runs.

**None of it is the shadow pass.** ADR-112 expected "about 0.1 ms" there from tighter frusta culling
more casters. Measured: the shadow pass is 0.33 ms in *both* arms, to the timestamp counter's own
resolution, while the draw count triples. That is the same finding §4.1 made with the depth prepass
-- a depth-only pass is not where this renderer's time goes -- and it means ADR-112's shadow-pass
claim should be read as unmeasurable rather than as measured.

**The 189 -> 34 draw reduction decomposes.** The arm renders 99 shadow draws where the shipping
configuration renders 34, in every arm block, on a tree that also has LOD0 in it. So of the
189 -> 34 that §3.2.2 recorded as "the two merges together", LOD0 accounts for 189 -> 99 and
ADR-112 for 99 -> 34. Both are deterministic counters, so neither carries session noise.

## Consequences

* §3.2.2's "the shadow work's contribution to the timing is not established by these numbers" is now
  established by different numbers, taken the way the rule requires.
* `shadowTexelTarget` is confirmed as a *performance* setting as well as a quality one, and the two
  point the same way, which is rare enough to record: shortening the range makes the near shadows
  sharper and the frame faster at the same time. The thing it costs is long-range terrain
  self-shadowing, which is what ADR-112 already says it costs.
* This does not license raising the target to go faster. The relationship past 8 cm is untested, and
  the quality it trades is the reason the setting exists.
