# Phase D — material tiers

Agent `mat`, 2026-09-13. Every number here was taken by me, in one session, under
`tools/gpu-lock.sh`, with both arms interleaved in one process (ADR-113, ADR-117). ADRs 133–138
carry the decisions; this file carries the numbers, the ordering they imply, and the things this
phase found and did not fix.

## 1. The headline

**The 8.4 ms of "opaque shading that no arm removes" is not opaque. About 5.9 ms of it — roughly
70% — is removed by shading at the flat tier.** It was invisible to Phase A and Phase B for a
structural reason ADR-118 already named: every arm before this one removed a *pass* or a
*subsystem*, and this work is per-light terms inside `evaluateLight` and per-fragment ambient work
inside `shadeSurface`. Neither is a pass.

That is the phase's result. What it does *not* mean is that 5.9 ms is available: the arm that
measured it forces the flat tier on every draw, and the picture that produces fails the §50 gate in
the near field. §4 below bounds what assignment could realize.

## 2. The measurements

Glowmere, 1280×800, realtime, release, M2 Max / Dawn-Metal, GPU lock held, one session. Session
baseline **14.02 ms GPU / 11.34–11.53 ms scene** — higher than the recorded 13.37 / 10.81, which is
why nothing below is a cross-session subtraction. Null A/B (`--ab none`, 3 pairs): **−0.93%, not a
result**, baseline blocks varying 0.93%.

| arm | Δ GPU | Δ scene | pairs | verdict |
| --- | --- | --- | --- | --- |
| **`matflat`** — every draw at the flat tier | **+5.77 ms, +40.9%** | 11.4 → 5.4 | 5 | a result |
| `contact` — no screen-space contact march (re-taken here) | +1.90 ms, +13.2% | 11.5 → 9.9 | 3 | a result, noisily |
| `matlights0` — no local lights at all | +1.31 ms, +9.3% | 11.5 → 10.1 | 3 | a result |
| **`matreduced`** — every draw at the reduced tier | **+0.79 ms, +5.6%** | 11.3 → 10.6 | 5 | a result |

Per-pair deltas and the one anomalous arm block per `matflat` run are in ADR-136, listed rather than
trimmed.

### What the decomposition says

Overlapping, not disjoint — removing all local lights also removes their contact march:

| removed by the flat rung | this session |
| --- | --- |
| the contact march, all lights | ~1.9 ms |
| the whole clustered local-light loop, 222 lights | ~1.4 ms |
| the rest: directional cascade lookup, PCSS blocker, mask read, AO sample, painterly specular | ~2.6–3.5 ms |

1. **The local-light count is not the problem.** 222 local lights, and removing every one of them is
   worth 1.4 ms of an 11.4 ms scene pass. ADR-114 said the cluster grid was not a cost; this says the
   loop it feeds is not one either. **Anyone who arrives at this scene and reaches first for the
   light count will spend their week on 12% of the problem.**
2. **Shadow *terms* are the largest thing a tier can reach, not shadow *passes*.** The shadow pass is
   0.33 ms and has been immovable through three investigations; the per-light shadow work inside the
   lit pass is several times that.
3. **The gap between the two rungs is entirely shadows**, which is why they are separate rungs: the
   reduced rung's 0.79 ms is nearly all the local-light contact march, and everything beyond it costs
   a visible shadow.

## 3. The visual gate (§50)

The canonical frame captured at frame 119 under each arm and inspected at full size.

* `matreduced` — close to the baseline over most of the frame, and **visibly changes the
  foreground**: the bioluminescent ground pools, the scene's signature, shrink or vanish where a
  froxel held more than six lights.
* `matflat` — a different picture in the near field: no contact shadows, no shading gradient on the
  ferns, no ground glow. **The distant hillside and its tree line are unchanged by eye.**
* `matlights0` — removes the scene's identity, as a bound arm should.

The last two observations together are the argument for the phase: the half of the image the flat
rung damages is the near half, and the half it leaves alone is the half assignment would give it to.

## 4. What assignment could realize, and why it is not wired

§4.9's coverage split is the bound. Authored entities — the terrain and the heroes — are **77% of
Glowmere's coverage**, and no importance rule may demote the terrain: its projected radius is the
frame, and it is what every contact shadow lands on. So the assignable share is at most the
procedural scatter's 23%, and **the realizable saving of the flat rung is at most about 1.4 ms** even
if every scattered instance qualified — which the foreground ferns and grass do not.

This is a property of Glowmere's *composition*, not of the renderer. A scene whose frame is mostly
mid-distance ecology would realize much more of the ceiling. The correct reading is "measure the
assignable share per scene".

**The experiment that settles it, and the first thing the next agent should do:** one arm applying
the flat tier to procedural draws only, entities at Full, interleaved in one session. That measures
the assignable share directly instead of estimating it, and it costs one uniform lane. It should be
run *before* the assignment machinery, which is the ordering that produced this document.

## 5. What shipped

* **The ladder** (ADR-133): `MaterialTier` Full / ReducedLights / Flat, selected by a per-draw
  uniform because ADR-118 measured a lane-varying branch running *slower* than the loop it skipped.
  Three rungs in one shader, not three pipeline variants — risk 7 is a real risk and 36 WGSL shaders
  is already a lot to keep correct.
* **The policy object** (ADR-134): `QualityPolicy::forTier` is the whole definition of a mode, it
  reconciles the two places material-tier assignment was stated, and `assertOfflineIsUncompromised()`
  is every §5.9 promise in one predicate, tested positively and negatively.
* **The selector** (`MaterialTierSelector`): pure, device-free, unit-tested — and unwired.
* **The budgets** (§18, §24): the per-subsystem budgets already lived in `QualitySettings`; this adds
  the material-tier light budgets and makes the *shadow* budget part of the tier — the flat rung's
  shadow budget is zero and the reduced rung's excludes the local contact march.
* **The render scale** (§34, ADR-137): the parameter, the tier table entry and one sizing rule.
  Not applied — see §6.

## 6. What was found and not fixed

1. **`ObjectUniforms::ids.w` is documented as `0` and is the skinned joint count** (ADR-135). Caught
   because the first implementation read it as the tier: every *skinned* draw would then have shaded
   flat in the **baseline** arm, and an A/B whose baseline is quietly wrong reports a difference that
   is real, reproducible, interleaved and about nothing. The struct comment is corrected; the missing
   lane is not added, because that is the object layout Phase E owns.
2. **Render scale cannot be applied while `tonemap.wgsl` binds its HDR input as
   `UnfilterableFloat`** (ADR-137). A mismatched size already works there — it is a nearest-neighbour
   upscale, which is a defect with a knob rather than a quality tier. Three precise steps to fix it
   are listed in the ADR; all three are outside this phase's files.
3. **`matflat` produces one anomalous arm block per run** — about halfway between the arms, once per
   run, in a different position each time. It reads as a machine hiccup, it is not attributed, and
   the median over pairs is the statistic that survives it.
4. **The `contact` arm is noisier than ADR-118 found it** (baseline spread 5.91% against 0.98%),
   because the machine was less settled by the end of the session. Its 1.9 ms here is not offered as
   a correction to ADR-118's 2.36 ms.
5. **The reduced tier's default budget of 6 barely binds**, against a measured mean froxel occupancy
   of 5.70 (§3.6). Almost all of its 0.79 ms is the local contact march rather than the cap. A
   smaller budget would save more and would damage the foreground more; nobody has calibrated where
   that trade sits, because ADR-138 says the rung is not worth wiring on this scene.
