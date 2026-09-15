# ADR-205: What world effects cost, and the cross-binary number that is not evidence

Status: Accepted

## Context / Problem

ADR-204 put a loop over up to eight propagating waves inside `pbr_shade.wgsl`, which is the fragment
shader every entity, every procedural instance, every skinned character and every raymarched SDF
surface in the engine goes through, plus a copy in `water.wgsl`. §20 of the brief says to measure
that rather than assume it, and names the five numbers it wants: a baseline, the system disabled,
each effect alone, and both together.

Getting those five numbers honestly turned out to need two instruments and one discarded result.

## Alternatives considered

1. **Time the shipped scene with the directed camera.** Rejected: the two shipped effects are gated
   on the cut, so which of them is live depends on where the playhead is, and an arm whose effect is
   off screen for half the frames measures half an effect. ADR-182's rule applies -- an arm has to
   prove it established the state it claims to measure.
2. **One process per arm, compared across runs.** Rejected outright by ADR-113: a number from a
   previous run of this program is not a baseline.
3. **`tools/render_bench.py` over the density ladder.** The wrong instrument: it varies the *world*,
   and what is varying here is one term in one shader on one world.

## Decision

**Four arms, generated; two instruments; the cross-binary number reported and not used.**

The arms are `tools/make_fx_bench_scenes.py`, which writes the shipped Glowmere scene four times with
the effects pinned to `window` activation over the whole timeline and to the scene's own static
camera: no effects, the beam alone, the pulse alone, both. Pinning is what makes each arm
non-vacuous, and the check is a pixel diff against the no-effects arm before any timing is taken:

| arm | pixels changed against `_fxnone`, 640x360 at t = 1.5 s |
|---|---|
| beam | 69 197 of 230 400 (30.0%), max channel delta 211 |
| pulse | 66 772 of 230 400 (29.0%), max channel delta 197 |
| both | 109 204 of 230 400 (47.4%), max channel delta 212 |

**The instrument that decides is `--ab worldeffects`** -- the in-process A/B/A/B of ADR-113, three
pairs, 2880x1800, 80 frames at 30 fps, one arm against the same binary with the frame block reporting
zero effects. All four runs reported "the machine held still".

| scene | baseline (effects live) | arm (loop off) | delta | verdict at the 2% GPU floor |
|---|---|---|---|---|
| both | 37.95 ms | 37.09 ms | **+0.85 ms (+2.25%)** | a result |
| beam alone | 37.62 ms | 37.16 ms | +0.46 ms (+1.22%) | inside the noise |
| pulse alone | 37.55 ms | 37.16 ms | +0.39 ms (+1.05%) | inside the noise |
| none | 37.16 ms | 37.16 ms | +0.00 ms | **vacuous, and that is the finding** |

The per-pass breakdown puts all of it in one place: with both effects live the `scene` pass is
30.74 ms and with the loop off it is 30.08 ms, and every other pass median is identical to the
hundredth of a millisecond. That is the term being where ADR-204 said it was.

**The `none` arm is vacuous and is reported as vacuous.** With no effects declared the toggle changes
nothing -- the tool's own drift check calls the run VOID, the per-pair deltas are `+0.00 / -0.07 /
+0.00`, and a render with `--disable worldeffects` of the *both* scene is byte-identical to the
no-effects scene (0 of 230 400 pixels differ). That is not a null result dressed up: it is the
statement that the off switch is complete and that a scene which declares no effects pays nothing.

**The second instrument is a counterbalanced interleaved pass** over all four arms plus the
pre-change binary, five rounds, each configuration taking each position in the round exactly once.
Its medians agree with the A/B: 37.16 (none), 37.55-37.62 (pulse), 37.62-37.68 (beam), 37.88-38.01
(both), with a within-configuration spread of ±0.07 ms.

## Rationale, and the number that was thrown away

The first interleaved pass ran the five configurations in a fixed order with the pre-change binary
last in every round. It came back saying the **pre-change** binary was 1.44 ms *slower* -- 38.60 ms
against 37.16 -- on identical content. The obvious explanation was thermal position: last slot,
hottest machine, five times out of five.

It was not. Counterbalancing the order changed nothing: 38.54-38.67 across all five positions. The
two binaries were then rendered to disk and compared, and their images agree to **one pixel in
518 400, by 10 of 255** -- a float reassociation at a silhouette, not a rendering difference. So a
binary doing strictly more work, producing the same image, measured 3.7% faster, which is above this
repo's own 2% GPU floor and in the wrong direction.

The conclusion is not "world effects made Glowmere faster". It is that **a cross-binary comparison is
not an attribution**: two builds differ in code layout and in the WGSL their pipelines were compiled
from, and the Metal compiler's scheduling of a 30 ms fragment shader is free to move by a per cent
either way for reasons that have nothing to do with the change. ADR-113 refuses cross-*session*
comparison for a weaker version of this reason; cross-*binary* is worse. The number is recorded here
because it was measured and because the next person to run that comparison should know what it does,
not because it says anything about the cost of anything.

What the in-process arm says is the answer: **two live effects cost 0.85 ms of a 37 ms frame, one
costs about 0.4 ms which is at the instrument's floor, and none costs nothing at all.**

## Consequences

* The per-effect cost is roughly linear and small enough that the eight-effect cap is a limit on
  authoring rather than on performance: eight live effects would extrapolate to ~3 ms, and no scene
  runs eight.
* A single effect cannot be attributed on this content with this instrument -- 0.4 ms is under the
  2% floor. Attributing one would need either heavier content or a fragment-density pass (ADR-115),
  and neither is worth building for a number that is already known to be small.
* The arms are generated, not committed. Four near-copies of a 128 KB scene is half a megabyte of
  duplicate in the repository in exchange for one script, which is the trade
  `tools/make_bench_scenes.py` already made.

## Rejected alternatives

* **Reporting the cross-binary delta as the "baseline" row §24 asks for.** It would have been a
  flattering number and it would have been wrong. The row is reported with what it actually is.
* **Chasing the 1.44 ms.** It is a compiler scheduling difference on a frame nobody is trying to
  speed up, in the favourable direction, and finding its cause would not change a decision.

## Revisit triggers

* Content with materially heavier overdraw than Glowmere, where a term added to every fragment costs
  more than it does here.
* An effect count above two in a shipped scene, since only the linearity of the two measured points
  supports extrapolating.
* A per-cluster effect list (ADR-204's own revisit trigger), which would change the cost model from
  "every fragment, every effect" to "every fragment, the effects near it".
