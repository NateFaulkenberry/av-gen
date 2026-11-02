# ADR-1934: The slit can be gated by identity, with a per-slot table of trail and fracture

- Status: Accepted (CHRONOS Gate D2, 2026-11-01), scene/chronos.
- Builds on:
  - ADR-410, which proposed read-time identity masks over the ADR-035 identifier target instead of per-object history;
  - ADR-1931, the ring and the sampler.
- Approach: approach A of `docs/prototypes/chronos/12-devourer-recon-plan.md` §5, which the owner approved at D0.
- Code:
  - `SlitSettings::gate` and `SlitSettings::slots` (16 slots: `trail`, `echo`, `fade`, `fracture`, `strips`,
    `angle`), with `temporal/slit/gate` and `temporal/slit/slot<k>/<leaf>`, in `src/scene/temporal_settings.{hpp,cpp}`;
  - `CompositionNode::temporalSlot` (the scene key `"temporalSlot"`, 0..16), flattened onto `Entity::temporalSlot`;
  - the slot ring, the slot table, `fs_capture_slot` and `fs_gated` in `src/rendering/temporal_effects.cpp` and
    `shaders/temporal.wgsl`;
  - `TemporalFrameInputs::identifier` / `entitySlots`, filled by `SceneRenderer` when the gate is on.
- Tests:
  - `[gpu][temporal][adr1934]` in `tests/rendering/test_temporal_gate_gpu.cpp`;
  - `[temporal][adr1934]` and `[composition][temporal][adr1934]` in `tests/unit/test_temporal_gate.cpp`.

## Context

The Devourer's souls must stretch, echo and fracture as they near the mouth, while the creature's face and the
background stay sharp. The ADR-1931 sampler applies one age map to every pixel. So it smears everything, or, with a
mask on the current frame, it smears only where a soul is now. But a trail is exactly where the soul is not.

The plan compared three ways to localise the effect:
- A, gating by identity;
- B, an isolated soul layer with its own ring;
- C, a depth ring.

A was chosen. It needs no extra scene pass, and its memory is one byte per history texel.

## Decision

1. **A node names a slot.**
   - `"temporalSlot": k` (1..16) on a scene node puts everything the node draws in slot k.
   - 0, the default, leaves it ungated.
   - The key is written only when set, so every existing scene round-trips byte for byte.
2. **A slot ring beside the colour ring.**
   - When `temporal/slit/gate` is on, an R8Uint texture array of the colour ring's size and depth holds, per history
     texel, the slot of the gated object drawn there, or 0.
   - It is written in the same frames and at the same layer as the colour capture. So the ring's write cursor, its
     validity count and its pre-roll (ADR-1932) serve both, and no second bookkeeping exists.
   - The capture point-samples the ADR-035 identifier target, because an identity has no average.
   - The identifier maps to a slot through a 64 KB table: its 16 low bits index four 8-bit slots per word. The table
     is rebuilt per frame from the entities and uploaded only when it changes.
   - Entity 0 shares identifier 0 with the cleared background, so it is never gated.
   - Turning the gate off releases the slot ring.
3. **The gated sampler replaces the time map.** History is read only where the stored texel's slot is non-zero, so the
   creature and the background can never be smeared: their past pixels are rejected. Per slot:
   - **trail (frames), echo, fade.** At a pixel the object no longer covers, the most recent stored image of it within
     `trail` persists, fading by `fade` toward its oldest end. With `echo` > 0, only the stored frames just after each
     multiple of `echo` count, so the trail breaks into discrete copies. This is Stage B (echo) and Stage C (the long
     stretch along the path).
   - **fracture, strips, angle.** Across the object, in strips perpendicular to its direction of travel on screen,
     each strip shows a random moment up to `fracture × span` back. A strip whose moment did not hold the same object
     at that pixel keeps the present. The moments are re-chosen six times a second as a function of the render time.
     This is Stage D: head, torso and limbs at different moments.
   - The trail scan visits only the layers the longest live trail can reach.
4. **The bound stays the slit's span.** No slot reads further back than `span`, so `historyFrames()`, the ring's
   sizing, the warm-up and the conformance check are unchanged.
5. **The squeeze into the mouth (Stage E) is geometry, not a sampler term.** The node's own scale and position
   (timeline-authored) narrow the soul into a ribbon and carry it into the throat, which occludes it. An address warp
   would have to know which object is at the warped address before it knows the address. The node does this exactly,
   for nothing.

## Consequences

- **Memory.** One byte per history texel: 960×540×128 layers is 66 MB on top of the colour ring.
- **Cost.** The capture adds one point-sampled pass. The sampler adds up to one u8 load per layer the longest trail
  reaches, plus three for the fracture.
- **FIR.** Like the rest of the family, it is scrub-safe, and a render of a range lands where a full render does.
- **Overlap.** A soul's trail lands over whatever is behind it, the face included. That is the look of reference
  r12, and the plan accepted it. A past soul texel hidden behind something else at capture is lost, because only
  what was visible is stored. Approach B is the fallback if that ever reads wrong.
- **Slots are by node, not by entity.** A node of several meshes shares one slot. Sixteen slots cover the Devourer's
  visible procession. Souls are recycled through slots as they are through positions.
