# Wave 4 — wiring, not building

Written 2026-09-13 after wave 3's Phase C and Phase D results. **Both phases came back "no", and
both came back with the number that says why.** That changes what is left to do.

## What wave 3 established

- **Phase C's remaining tasks are not justified on this content (ADR-151).** The ceiling was measured
  by *deleting* each band rather than prototyping a proxy: nothing a proxy can do beats deletion,
  because a proxy still draws a silhouette and still casts a shadow, so one arm bounds every
  possible implementation. Deleting the whole impostor band costs **+1.8%** — inside the noise floor
  and the wrong sign.
- **`RepresentationSelector` is built, calibrated, and wired to nothing.** Connecting it reaches 29%
  of the deletion ceiling and halves the frame's triangles with no new code. Building C5+C6+C7 on top
  is worth **≈0.4 ms of a 13.6 ms frame**.
- **`MaterialTierSelector` is built, tested, and unwired** (ADR-135) — `ObjectUniforms` has no lane
  to carry a per-draw tier.
- **A frame-global flat tier saves 40.9% and fails the visual gate** (ADR-136). Neither shading rung
  is enabled in any tier. Assignment could realize **at most ~1.4 ms** on Glowmere (ADR-138), because
  authored entities are 77% of coverage and the terrain can never be demoted.

So the remaining value is in **connecting machinery that already exists**, and in one measurement
that decides whether the second piece is worth connecting at all.

## The ordering, and why it is not negotiable

**1. Fix the cull ladder first (ADR-152).** The ladder sizes a procedural by its raw mesh and omits
`sourceTransform`, so only three of Glowmere's thirty procedurals have a correct radius and
`elder-crown` is **8.2× too small**. Every representation decision downstream reads that radius.
Wiring a selector onto a mis-sized ladder feeds it corrupt input and produces a confident wrong
answer — the failure mode this project has paid for repeatedly.

The fix is three lines. Its *consequences* are not: it moves every scatter layer's rung selection in
a different direction and invalidates the calibration of the 28 / 11 / 4 px thresholds. It needs its
own visual gate (§50) and its own recalibration.

**ADR-152 makes a testable prediction** — that the shipped Glowmere frame does *not* change, because
its authored `minScreenRadius` is ~1.5 px and `elder-crown` has no ladder to descend. If the capture
is byte-identical, this lands as a correctness fix at zero visual cost. If it is not, it is an
image-changing change to every scattering world and belongs to whoever owns the ladder's
calibration, with art input.

**2. Run the one arm that decides Phase D's fate (ADR-138).** The flat tier applied to *procedural
draws only*, against the ~1.4 ms bound. One measurement, before any assignment machinery. If it
comes in under the 2% floor, D4 stays unwired and that is the end of it.

**3. Then wire, in this order:** `RepresentationSelector` into the procedural LOD path (the larger,
better-evidenced win), then — only if step 2 justifies it — a tier lane in `ObjectUniforms` and
`MaterialTierSelector` behind it.

## Also outstanding, and unblocked

- **Render scale (§34, ADR-137).** The parameter ships; the application does not, because
  `tonemap.wgsl` binds the HDR target as `UnfilterableFloat` with no sampler, so a scale below 1
  upscales nearest-neighbour — "a defect with a knob", and §50 rejects it. Three precise steps are
  named. All three are in the post/composition path. **Note they touch a shader, so they cannot be
  done while another agent has a GPU run in flight.**
- **`ObjectUniforms::ids.w`** is documented `0` and is actually the skinned joint count. The comment
  is fixed; the missing lane is not added.

## What is explicitly not scheduled

C5, C6 and C7 remain **tracked, not removed**, each with the number and the scene shape that would
reverse the verdict. A phase that cannot show its value is not the same as a phase that has been
deleted, and the difference has to stay visible on the board.
