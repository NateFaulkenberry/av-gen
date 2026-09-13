# Phase B — per-pixel cost

Agent `frag`, 2026-09-13. Every number here was taken by me, in one session per table, under
`tools/gpu-lock.sh`, with both arms interleaved in one process (ADR-113, ADR-117) unless the row
says otherwise. ADRs 117-121 carry the decisions; this file carries the numbers and the ordering
they imply.

## 1. The attribution table, re-derived (task L1)

Every share in `01-audit-and-baseline.md` §4 was a share of a **15.73 ms** scene pass in an
**18.61 ms** frame. The frame is now **13.37 ms** and the scene pass **10.88 ms**. Re-derived by
re-running the arms, not by rescaling the old numbers — three of them moved by more than the
denominator did.

Glowmere, 1280x800, realtime, release, M2 Max / Dawn-Metal, three interleaved pairs of 120 frames
per arm, GPU lock held. "Δ scene" is the scene-pass median in the baseline blocks against the arm
blocks; "verdict" is the harness's own, against a noise floor it derives from that run's baseline
spread.

| arm removed | Phase A Δ scene (of 15.73) | **now, Δ scene (of 10.88)** | Δ GPU frame | verdict |
| --- | --- | --- | --- | --- |
| **contact march** | *never separated* | **−2.36** (10.75 → 8.39) | **+2.36 ms, +17.6%** | a result |
| shadow mask | +5.8 | **+1.12** (10.68 → 11.80) | −0.79 ms, −5.9% | a result |
| mask forced to full res | — | +1.02 (10.62 → 11.67) | −0.66 ms, −4.9% | a result |
| ADR-112's range rule off | — | +0.89 (10.81 → 11.73) | −0.92 ms, −6.9% | a result |
| ao | −0.3 | −0.26 (10.81 → 10.49) | +0.59 ms, +4.4% | a result |
| volume | +0.2 | ~0 (10.75 → 10.75) | +0.59 ms, +4.4% | a result, outside the scene pass |
| particles | −0.1 | −0.13 | +0.33 ms, +2.4% | marginal |
| PCSS → PCF | — | 0.00 (10.68 → 10.68) | +0.20 ms, +1.5% | **not a result** |
| transparency | −0.1 | +0.07 | −0.20 ms, −1.5% | **not a result** |
| water | +0.5 | +0.07 | −0.07 ms, −0.5% | **not a result** |
| **aux attachment stores** | — | ~0 | −0.13 ms, −1.0% | **not a result** |

### What changed in the ordering, and what it means

1. **A new largest item appeared that Phase A could not see.** The screen-space contact march is
   2.36 ms — **22% of the scene pass**, the single largest fragment item in the renderer. Phase A
   never separated it because it is not a pass and not a subsystem: it is a loop inside
   `evaluateLight`, and no toggle reached it until ADR-117 made a `QualitySettings` field into an
   A/B arm. §27's "contact shadow optimisation" now has a number.
2. **The shadow mask's 5.8 ms is stale by a factor of five.** It saves 1.12 ms of scene pass now.
   It is still a saving and still not a deletion candidate; it is just a much smaller one, because
   ADR-112's shorter range means most distant fragments never reach the blocker search at all.
   **Do not quote 5.8 ms.**
3. **PCSS stopped being a cost.** ADR-111 named it the largest contributor to the mask's residual;
   its *time* is now inside the floor. Same cause.
4. **Everything Phase A found to be noise is still noise**, and by a wider margin: water,
   transparency and particles are all at or under 2% of a frame that is 28% smaller than the one
   they were measured in.
5. **Nothing removable accounts for the scene pass**, exactly as Phase A concluded. Subtract the
   contact march and every other arm above and 8.4 ms of opaque geometry shading remains. That is
   Phase C's problem, not Phase B's.

## 2. Bandwidth: measured, and it is not the constraint

The five-target layout was already measured **not** to be a tile-occupancy constraint (32 B/sample
against a 128 B/px tile budget), so the only question left here is bandwidth. It is now answered
directly rather than by a scaling argument.

`--ab auxstore` discards the store of the four auxiliary attachments — 24 B/px, **24.6 MB a
frame** — while the shader goes on computing and writing them, so it isolates the tile-to-memory
store and nothing else. **−0.99%: not a result**, three pairs, all three deltas identical at
−0.13 ms.

The arithmetic agrees: the scene pass moves roughly 48 B/px, about 49 MB a frame, which is on the
order of 0.1 ms — under 1% of the pass, and so under the floor by construction. Details, and the
three things rejected on this evidence (memoryless attachments, eliding the empty background pass,
dropping the target nothing reads), are in ADR-119.

**This is a result about bandwidth at 1280x800, not a general one.** Bandwidth scales with pixels;
the fragment cost that dominates here does not (§4.1). The arm exists so the question can be re-
asked rather than assumed settled.

## 3. What ADR-112 bought (the debt §3.2.2 recorded)

**6.9% of the frame**, not the 4.2% its own block-measured evidence reported. Full table and the
draw-count decomposition (LOD0 accounts for 189 → 99 shadow draws, ADR-112 for 99 → 34) in ADR-120.
The audit's §3.2.2 can stop saying the contribution is not established.

## 4. Clustered lighting: nothing done, deliberately

Already measured at zero overflow, busiest froxel 29 of 32, 56% of the grid empty, build 0.07 ms
(§3.6, ADR-114). Nothing here re-opens it and nothing here adds a local light. **Do not "optimise"
it and do not raise the cap.** Re-run `--cluster-stats` after any change that adds local lights to
Glowmere, because nothing reports an overflow at runtime.

## 5. Two optimisations implemented, measured, and reverted

Both are in ADR-118 in full. They are the most useful thing this phase produced after the
attribution table, because they say what the march is bound by:

* **Incremental clip-space march** — replaces twelve matrix-vector products per fragment per light
  with two plus a fused multiply-add each. An algebraic identity. **No measurable change**, so the
  loop is not arithmetic-bound; it is bound by twelve *dependent* texture loads.
* **Skipping the march where its answer provably cannot change the result** — bit-exact, 0 of
  1,024,000 pixels differ. **4.4% slower**, in 7 of 8 interleaved pairs run in both orders. The
  branch guarding the march was wave-uniform and this makes it lane-varying, and a lane-varying
  branch around a twelve-iteration loop with a dependent load in it costs more than the loop it
  skips. In a fragment shader, saving work per lane saves nothing unless the whole wave skips.

## 6. What Phase B leaves for whoever comes next

Ranked by the numbers above, not by appeal.

1. **The contact march, 2.36 ms.** The levers that can work change how many fragments march or how
   many reads each does. Fewer steps (`contactSteps`, already policy-exposed, already tier-scaled)
   is a quality reduction and needs its own image measurement. Marching once per pixel in a
   full-resolution screen-space pass is sized in ADR-118 at **at most ~0.9 ms** — the
   resolution-independent part of its cost — against ADR-087's measurement that a half-resolution
   contact term blackens dense ground cover, and it is not attempted here.
2. **8.4 ms of opaque geometry shading that no arm removes.** Phase C.
3. **`normalRough_` is written by every fragment and read by nothing** on the normal frame path.
   Measured at zero (ADR-119), recorded because the next person will find it and think it is free
   money.
4. **`water.wgsl` still biases its shadow term with the wave normal** (ADR-111 found this and left
   it). Unmeasurable on Glowmere — the `water` arm is inside the floor — so it needs a scene with
   more water to be worth touching.
