# ADR-128: The object cap is an allocation policy, not a constant

**Status:** Accepted
**Date:** 2026-09-13

Renderer-upgrade Phase E (`docs/renderer-upgrade/04-target-architecture.md`). The one phase whose
exit criterion is a hard number: *a scene with more than 256 visible entities renders correctly.*

## Problem

`SceneRenderer::kMaxObjects = 256` was a hard ceiling on **simultaneously visible** entities. It was
not chosen as a limit; it fell out of an allocation. `ObjectUniforms` is 272 bytes, WebGPU requires
dynamic offsets to be 256-byte aligned, so each object took a 512-byte slot, and `init()` allocated
one 128 KB buffer — 256 slots — once, for the life of the renderer.

The failure mode when it was hit was the bad kind: `log::warn("more than 256 visible entities; extra
entities skipped")` and then the extra entities simply did not draw. No error, nothing on screen to
say which entities went missing or why, and the frame completed looking plausible.

`docs/renderer-limits.md` recorded that Glowmere flattens to 278 entities and only survived because
frustum culling kept a typical camera's set to about half the cap — "the least controllable
assumption a renderer can depend on". The measurement below shows that was already optimistic:

| Content | Entities | 256-slot renderer |
|---|---|---|
| `examples/recipes/glowmere.recipe.json` | 151 | fits |
| `examples/world/glowmere-stylized.json` | 278 | fits only while culled |
| `examples/recipes/glowmere-dense.recipe.json` | 482 | over |
| `examples/recipes/glowmere-extreme.recipe.json` | 840 | **over, and firing** |

`glowmere-extreme` is not a hypothetical — it is a rung on the density ladder `tools/render_bench.py`
measures against. On the pre-change binary it logged `extra entities skipped` on **60 of 60 frames**,
and the frame it produced is missing the pale hero tree in the middle of the shot. The renderer had
been dropping a hero object out of a benchmark scene, every frame, for as long as that recipe has
existed, and the only trace was a log line.

## Decision

`kMaxObjects` is deleted. The object buffer's size is a **per-frame decision**:
`ensureObjectCapacity(n)` is called once per `render()`, after `uploadMeshes()`, with the number of
drawable entities in the scene — the most slots the two entity loops can consume between them, since
each entity is offered a slot at most once across both. It grows the buffer (doubling, floor 256,
never shrinking), reallocates the staging mirror, rebuilds the object bind group, and hands the new
buffer to `SkinningRenderer`, whose group 1 names the same buffer (ADR-086) and would otherwise be
left pointing at the replaced one.

**The ABI is deliberately unchanged.** Still one uniform buffer of 512-byte slots addressed by a
dynamic offset; still `minBindingSize = sizeof(ObjectUniforms)`. No shader moved, no pipeline layout
moved, no CPU/WGSL layout guard moved. What stopped being a compile-time constant is the buffer's
*size*, and nothing else. ADR-129 records why the storage-buffer rewrite the plan named as the
direction was not taken, and what evidence would change that.

## Consequences

- **Verified: the canonical frame did not move.** `glowmere.recipe.json`, 60 frames, 1280×720,
  headless, captured on both arms: **byte-identical** PPM. That scene has 151 entities and now
  allocates exactly the 128 KB it allocated before, which is why `kInitialObjects` is still 256 — a
  scene under the old cap must not pay a byte more than it did.
- **Verified: the frame that was broken is fixed, and visibly.** `glowmere-extreme`, same conditions:
  60/60 frames warned before, 0/60 after, and 2.28% of pixels differ — including a hero tree that
  was absent and is now drawn. This is a visible change, and it is the *point*; it is not on the
  canonical frame the gate protects.
- **Measured: no CPU cost that can be distinguished from drift.** A/B interleaved in one session,
  3 runs per arm, `glowmere.recipe.json` 90 frames at 1280×720, M2 Max / Metal / Release. The
  `objects` CPU stage (entity traversal + object uniforms + shadow-caster selection) came out at a
  median of 0.055 ms before and 0.051 ms after, with the two spreads (0.052–0.074 and 0.046–0.074)
  almost entirely overlapping. GPU p50 read 13.70 ms before and 13.30 ms after — 2.9% *in the
  change's favour*, which is not believable for a change that produces byte-identical frames, and
  is an ordering artefact: the `before` arm ran first in every pair and absorbed each pair's
  warm-up. Reported rather than claimed. The honest statement is that the added work is one pass of
  integer comparisons over the entity list and nothing measured above the noise.
- The buffer never shrinks. A camera turn that drops the visible count is about to raise it again,
  and reallocating on that is churn for no memory that matters.
- One limit remains, and it is a byte budget rather than a slot count — see ADR-130.

## Alternatives rejected

1. **Raise `kMaxObjects` to 1024.** Rejected because the scope contract asks for the cap to be
   *replaced by a mechanism, not raised* — and because it moves the same silent-drop failure to a
   different number without giving anyone a reason to believe the new one.
2. **A storage buffer indexed by `@builtin(instance_index)`.** The plan's named direction. Rejected
   for now, with evidence, in ADR-129.
3. **Sizing the buffer to `scene.entities.size()` rather than the drawable count.** Rejected: an
   entity whose mesh never reached the GPU still costs a slot under that rule, and the count is free
   to compute correctly.
