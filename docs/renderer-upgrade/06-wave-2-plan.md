# Wave 2 — assignment

Written 2026-09-13, after wave 1's geometry and lab branches merged. Launches once
`agent/shadowfix` lands and full-suite validation has run at that merge point.

## What wave 1 changed about the plan

The LOD0 result (−21% GPU, −38.6% triangles) did not just buy time; it moved the target. Phase A
measured the scene pass at 15.73 ms and every share below was a share *of that*. The scene pass is
now 11.9 ms, so a fixed-cost item's share has risen and a triangle-count item's share has fallen.
**Every percentage in Phase A's attribution table must be re-derived before it is used to order work
in Phase C.** That is task L1 below, and it is deliberately cheap and first.

## Assignments

Four agents, isolated worktrees, disjoint file ownership, pre-assigned ADR ranges — the topology
that worked in wave 1, with the ADR-index conflict anticipated this time (each agent appends to its
own range and the master resolves the index).

### Agent `frag` — Phase B, per-pixel cost (ADR 116–120)

Owns `shaders/`, `src/rendering/shadow_mask*`, pass setup in `scene_renderer.cpp`.

- **B-lead: contact shadow optimisation (§27).** Phase A's largest single fragment target: removing
  the shadow mask made the scene pass **5.8 ms worse**, so this is not a deletion candidate — it is
  a *cost* candidate. Measure the mask's own cost at the new baseline first; the pre-LOD0 ~19% is
  stale.
- **B1/B2: transient attachments and a load/store audit (§21, §37).** Apple: load/store actions
  "consume the majority of your app's system bandwidth". The five-target layout was measured *not*
  to be the constraint, so the win here is bandwidth, not tile occupancy — state it that way or the
  result will be misread.
- **B4/B5: clustered-light and attachment-write probes.**
- **B3: framebuffer-fetch feasibility** — report only; WGSL's capability wall applies.

Gate: no visual change on the canonical frames. Any quality reduction must be explicit,
policy-exposed and general (§49).

### Agent `repr` — Phase C foundations (ADR 121–126)

Owns `src/rendering/lod*`, a new `importance`/`representation` unit, `src/scene/` LOD metadata.

- **C1: Glowmere's own quad-overdraw share** at the *new* triangle count. This is the number that
  says how much Phase C is worth, and it is currently unknown post-LOD0.
- **C3 `ImportanceEvaluator` and C4 `RepresentationSelector` with hysteresis.** Pure CPU, unit
  testable without the GPU — deliberately the largest block of GPU-free work in the wave, so it runs
  while other agents hold the lock.
- **C8: calibrate band thresholds** against A1's knee (3.95–7.8 px/triangle) *and* its caveat: two
  screen-filling triangles cost more than 2,048, so the target is a band of a few hundred
  px/triangle, not the fewest triangles.

**C2 — transition quality without temporal AA — is the wave's largest unknown** and stays with the
master agent, not an agent. It decides whether §28 (temporal rendering) becomes a prerequisite, and
that is a scope decision, not an implementation one.

### Agent `cap` — Phase E, lift the object cap (ADR 127–129)

Owns the object uniform layout and `test_renderer_layout_guards.cpp`.

Self-contained, measurable, and the one phase whose success criterion is a hard number rather than a
judgement: a scene with >256 visible entities renders correctly. E3 re-pins the object-slot contract
so the guard tests still mean something afterwards.

### Agent `chore` — low-effort batch (medium effort model, ADR 130+)

Docs, test retagging, limits-doc upkeep, baseline regeneration, the stale-number sweep (L1 above),
CI hygiene. Explicitly **not** given anything whose failure mode is silent.

## Ordering constraint

`frag` and `repr` both eventually touch the scene pass. `frag` lands first; `repr`'s GPU-side work
rebases onto it. Until then `repr` stays in CPU-only territory, which is most of C3/C4 anyway.

## Testing policy, unchanged

Narrow filters during development, batched full-suite validation at merge points, every GPU command
through `tools/gpu-lock.sh`. A result taken without the lock is not evidence.
