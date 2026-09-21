# ADR-605: The incremental rebuild landed, and the win I predicted was wrong by a factor of three

**Status:** Accepted
**Date:** 2026-09-21
**Related:** ADR-603 (the profile, and the prediction), ADR-385 (a stated reason is not evidence),
`docs/testing.md` #25, Phase B §52 and §53
**Implemented by:** `src/scene/pose_layers.{hpp,cpp}` (`PoseLayerStack::ensureModel`)
**Tests:** `tests/unit/test_visual_regression.cpp` (correctness),
`tests/unit/test_motion_performance.cpp` (the win)

---

## Context

ADR-603 measured that seven full `poseToModel` walks over 90 joints were 14.3 µs of the motion
stack's 21.6 µs per character per frame — 66% — and said the correct fix was incremental rather
than hoisted, because a layer writes the pose and the next needs model space as the previous layers
left it.

It also made a prediction, deliberately stated before the work so that it could be wrong: *"an
incremental rebuild should turn most of those 14.3 µs into the cost of a subtree, and the stack's
per-character cost should approach the 4.9 µs the clip sampling alone costs."*

## Decision

**Keep `model_` valid across the layer loop, find the dirty set by diffing the pose, and record
that the prediction was wrong.**

`PoseLayerStack::ensureModel` replaces the four `poseToModel(skeleton, pose, model_)` calls. It
holds `modelPose_`, the pose `model_` was last built from; the joints that differ are exactly what a
previous layer wrote, and those plus their descendants are recomputed in the same single forward
pass `poseToModel` uses, composing the same `parent * local` in the same order.

**The dirty set is found by diffing the pose, not by consulting each layer's mask.** The mask would
be faster and it would be a *claim*. A layer that wrote outside its mask — which is exactly the
defect `docs/testing.md` #25 describes, and which has already happened once in this phase — would
silently hand the next layer a stale model space and a wrong solve, with nothing to fail. A diff
cannot be wrong about what changed, and 90 transform comparisons are an order of magnitude cheaper
than 90 matrix multiplies.

### The result, and the prediction

| | before §53 | after §53 |
|---|---|---|
| full stack, per character per frame | 21.91 µs | **14.1–14.3 µs** |
| 100 characters | 2.16 ms | **1.41 ms** |
| cost of adding one more model-space layer | 2.64 µs | **1.05 µs** (one walk is 2.10 µs) |

**35% off the stack. ADR-603 predicted it would approach 4.9 µs and it did not — it is three times
that.** The reason is one the profile could have told me and the prediction did not account for:
the first `ensureModel` of each frame follows the clip sample, which changes every joint, so a full
walk per frame is a *floor*, not something the optimization removes. Seven walks became one walk
plus six dirty-set passes, and a dirty-set pass is not free. The correct prediction would have been
"one walk plus six diffs", or about 14 µs — which is what it is.

Writing the number down beforehand is what made this visible at all. A win reported only as "35%
faster" is a success; the same win against a stated expectation is a success *and* a corrected
model of where the time goes.

## Consequences

- **§52 is the correctness proof, and it is what makes this safe.** `tests/data/motion-baseline.txt`
  records 335 named quantities in metres — joint positions, end-effector positions, contact heights,
  per-sample pose continuity — for the alien under the full seven-layer stack. The incremental
  rebuild changed **none of them**, to 0.1 mm. An optimization to a solver that cannot show its
  output is unchanged is a rewrite, not an optimization.
- The performance guard is the **marginal cost of one more model-space layer against a full walk**,
  not "the stack costs less than seven walks". The latter is true at 14.2 against 14.7 and is a 3%
  margin — a coin flip in a suite four agents run on a shared machine, and a flaky assertion is
  worse than none because someone eventually deletes it. The marginal is a difference of two minima
  and is therefore noisier than either (the first three readings were 0.64, 1.59 and 2.64 µs), so it
  is measured as the minimum of three paired runs. It then reads 1.05 µs against 2.10 across
  repeated runs: a factor of two, stable, and it inverts on a revert.
- `bind`/`rebind` writes `model_` from the **rest** pose, so it clears `modelPose_` and forces the
  next `ensureModel` to rebuild in full. Without that line a rig re-bound mid-run would solve
  against rest positions — a bug that would have shown as a character briefly snapping to T-pose
  and would not have been easy to trace.
- The next win, if one is wanted, is the six diffs. It requires knowing what each layer wrote
  without asking the layer, which is the same problem in a smaller form.
