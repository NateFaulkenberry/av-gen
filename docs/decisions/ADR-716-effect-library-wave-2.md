# ADR-716: Effect Library Wave 2 — where the owner is, when events happen, and what its surface does

**Status:** Accepted
**Date:** 2026-09-24
**Builds on:** ADR-702 (instances attached to owners), ADR-703 (Wave 1). **Design:**
`docs/design/effect-library/` (roadmap Wave 2 row, shared-infrastructure "XFORM", "TRIGGER",
"FXL", rendering-architecture §3 and §7).
**Implemented by:**
- The two-phase evaluator: `Engine::updateEffects(EffectPhase)`.
- XFORM: `world/effects/transform_frame.*`, `Composition::setEffectOffsets` / `nodeDrawnWorldTransform`.
- TRIGGER: `world/effects/effect_trigger.*` and `EffectContext::triggers`.
- FXL clip, displacement and pattern: `entity_fx.*`, `pbr_shade.wgsl`, `pbr.wgsl`, `procedural.wgsl`,
  and `worleyF1F2` in `noise.wgsl`.
- DF: the Disc shape, and the Shock, Ripple and Wake fields in `distortion.wgsl`.
- Stars: `world/effects/star_field.*`, `skybox.wgsl`.
- Twenty types, kinds 14–32, in `world/effects/kinds/`.

**Tests:**
- CPU: `[xform]`, `[trigger]`, `[shockwave]`, `[fxl]`, `[clip]`, `[displace]`, `[worley]`, `[stars]`, and the
  registry and conformance suites.
- GPU: `[xform]`, `[fxl]`, `[stars]`, `test_trigger_effects_gpu.cpp`, `test_entity_fx_surface_gpu.cpp`.

---

## Context

Wave 1 proved an owner can carry a term: its velocity, its history, its own surfaces. The next
dependencies in the roadmap were:
- *where the owner is* — a visual offset that its children and its effects follow;
- *when events happen* — an activation that fires on a beat, a marker or a near pass, and scrubs;
- *what its surface does* — clip, displace and pattern, obeyed by every pass, so that a dissolving
  owner's shadow dissolves with it.

Three slices built these in parallel with no files in common beyond the registration lines. The kind
numbers were reserved per slice in advance (motion 14–18, events 19, 20 and 30, surface 21–29 and 31),
so a type's serialised value never depended on merge order. The lead then merged them, wired
TRIGGER through, and added Stars (32).

## Decision

### The two-phase evaluator (rendering-architecture §3)

`updateEffects` stays the ONE evaluator. It is called twice per frame:
- **`BeforeScene`** runs just before `controller_->update`. It applies parameters, clears statuses and
  runs the Geometry stage. Its context carries **no scene query**, so a Geometry type cannot read a
  drawn transform that does not exist yet (or last frame's, which would lag and differ between play
  and scrub).
- **`AfterScene`** runs every other stage against the flattened scene, exactly as before.

### XFORM

A per-owner offset: translation in parent space (translations sum and commute), and rotation and
scale about a pivot in local space, composed in stack order.
- The Composition folds the offset in during its ordinary flatten (`nodeDrawnWorldTransform`). So
  children, attached lights and emitters, every effect reading the node's drawn view, and `prevModel`
  (the velocity target and motion blur) all follow it.
- `nodeWorldTransform` (the simulation, rigs and HIST) stays pre-offset. The offset is visual only: a
  bobbing saucer's AI does not perceive its own bob.
- **HIST records the pre-offset path**, because that is what keeps it exact under seek; the replay
  runs no effects. A reader that wants the *drawn* past asks `EffectSceneQuery::nodeDrawnPosition(name,
  t)`, which re-applies the offset as it was at `t` (`transformOffsetAt`: the same producers, gates and
  stacking, with no status writes). The Trail's body and `nodeVelocity` of an offset owner use it.
- Without that, a Trail on an orbiting owner drew its head on the drawn saucer and its body along the
  unmoving centre, joined by a streak. That was found by looking at the motion slice's renders.
- Types: Orbit, Spiral, Float, Shake, Bounce. Budget: 64 owners, then `Dropped` with a reason.

### TRIGGER

`Activation::Trigger` with six sources: Beat (every N, offset), Onset (threshold), MusicEvent,
TimelineMarker, Repeat (period, phase) and Proximity (entity, radius).
- **The contract is one pure function**, `TriggerClock::lastTriggers(trigger, owner, t, out)`. Consumers
  compute `age = t − t0` and keep no state.
- Proximity is evaluated on the simulation grid over the checkpointed HistoryBank, so it is exact
  under seek.
- Every builder resolves its window through the context overload of `resolveActivationWindow`. The
  events slice converted its own files; the lead converted the other six (XFORM, FXL lanes, Trail, the
  surface waves, the atmospherics).
- So a Beat-triggered Shake kicks on each beat and settles before the next. A triggered Dissolve or
  Growth runs from the event.
- An instance waiting for its first event is `Dormant`, with the reason under its badge.
- An unknown source name is refused by name in both serialisers (ADR-441).
- Live triggers are not built.

DF gained a Disc shape and three fields: Shock (an expanding refracting front), Ripple (a damped
train of rings) and Wake (a chain of segments along the drawn path). Types: Shockwave, Ripple,
Velocity Distortion. EMIT's `triggerBurst` fires on trigger edges, and twenty particle presets were
added.

### FXL clip, displacement and pattern

Lanes 6–15 of the record: clip (mode, threshold, edge), displacement (Inflate, Travelling and Smear,
summed per mode), pattern (Worley or noise), hue.
- **Every pass obeys them.** `vs_entity` and `vs_proc` add the displacement for the lit and depth-only
  pipelines alike. `fs_depth` and `fs_proc_depth` apply the clip. So the shadow of a half-dissolved
  owner has holes where the owner does, and a breathing owner has no prepass halo.
- `worleyF1F2` in `noise.wgsl` has a CPU twin.
- The gate is `fxFlags == 0`, a uniform branch per draw.
- Types: Dissolve, Growth, Breathing, Organic Pulsation, Bioluminescence, Pulsing Veins, Fresnel, Rim
  Light, Color Cycling, Motion Smear.

**The clip's discard is last.** A `discard` in non-uniform control flow ahead of a derivative left the
surviving neighbours of a quad with undefined derivatives on Metal. One NaN from that blacked out a
whole frame's exposure. The clip therefore records its decision and discards at the end of the entry
point.

### Stars

A Sky-stage type owning what `skybox.wgsl` hard-coded:
- density;
- a power-law magnitude and a colour temperature per star;
- scintillation deepest at the horizon, with rates snapped to whole cycles per 256 s so the wrapped
  second has no seam;
- a galactic band, mottled at two scales and split by a dark rift;
- a bright sky hiding the stars behind it.

It draws on **any** sky, because adding one is the author asking for stars. One field per sky: the
first live instance draws, and another is `Dropped` naming it.

## Evidence

**Gates.** With no instance, every primitive is byte-identical to the pre-change binary:
- XFORM: `ufo-stack` t=120, sequence hash `3e7e734851024e79`.
- FXL: Glowmere t=60 `a401cddcb091d0e4`, and `ufo-stack` t=120.
- Stars: Night Shift, Glowmere Stylized and `ufo-stack`, each against the binary without it.
- TRIGGER: a dormant instance leaves an empty block, including right after a drawn frame.

**Exactness.**
- Float + Orbit on a keyed owner: play = scrub on the drawn matrices and the ribbon bytes.
- Beat and Proximity triggers: play = scrub.

**Shown able to fail.** Each case below went red under a deliberate break:
- child follows;
- XFORM gate;
- stack order;
- XFORM play = scrub;
- velocity target;
- trigger purity;
- beat and Proximity play = scrub;
- the Dormant reason;
- the unknown-source refusal;
- burst frames;
- the outward-moving ring;
- the shadow's holes (with `fs_depth`'s clip removed);
- the prepass halo;
- Worley F2 ≥ F1;
- the Trail on an orbiting owner (nearest point to the simulated centre: 0 m, against an orbit radius of 1.6 m);
- the triggered Shake;
- Stars visible (0 px on a sky-less scene).

**Looked at.** The review set is in `~/Desktop/av-gen-review/10-effect-library-wave2/`:
- Float is a clean bob. Orbit moves and banks the saucer, and its Glow, beam and Warp stay on it.
- Every surface type reads on a probe sphere. A dissolved cube's and sphere's shadows have holes.
- Bioluminescence photophores and veins on the Glowmere mushroom at night.
- The shockwave ring grows outward on a checker.
- Stars: Clear Night, Deep Space with its band, Twinkling Horizon.

## Findings not fixed here

- **The alpha-mask discard in `pbr_shade` precedes texture samples**, as the clip's did. WGSL defines
  `discard` as demoting to a helper invocation, so later derivatives are defined by the spec. Alpha-masked
  foliage has rendered correctly throughout, and moving the discard would change every foliage frame to
  fix a failure never seen. Recorded here; revisit if a masked material ever blacks out a frame.
- **The inside of a dissolving hollow owner rendered black** (fixed after the wave). It was not a
  shading result: the depth prepass never culls, so where the clip removed a front face the prepass
  kept the back face behind it, and the procedural lit pass culled that back face -- the pixel kept
  the clear colour. Mesh entities already drew a live clip two-sided (`fxTwoSided`); procedural nodes
  now do too, so the inside is shaded as a two-sided surface with the flipped normal `shadeSurface`
  already gives a back face. Chosen over a darkened edge colour because it is the owner's own
  material (its Glow and lighting included), and it holds with the edge glow at 0. Gated on the clip
  bit, so no other draw changes. GPU test `[interior]`.
- **The Energy Blast Shockwave** was a thick bright band across the frame at its peak (fixed after
  the wave, in the preset only): thickness 1.0 -> 0.4 m, edge glow 4.0 -> 1.5, chroma 0.4 -> 0.25,
  strength 1.0 -> 1.3 so the thinner band still bends. Explosion shares a heavy band (1.8 m, 2.5) and
  was left alone: a fiery front may want it; the owner's call.
- **Velocity Distortion's wake is faint** on the film's saucer. The film never moves it fast; it was
  checked with an Orbit added.
- **The stock audio routes** (the root's spin) make node paths differ between play and scrub whenever
  audio is loaded. That is not Wave 2's; noted for the seek work.
- **The demo's hidden generator and film known-defect cases carried `[effects]` and `[demo]`**, so a
  plain `[effects]` run selected them. That rewrote `ufo-stack.json` and reported the known 0.15 m
  failure to every slice. They now carry only their hidden tags (and `[adr700]`).

## Not done in Wave 2

- **XFORM:** Light and Camera owners; Orbit about another entity; Float `sync`.
- **Offsets:** a routed row is re-applied at this frame's value (exact only for unrouted rows), and a
  nested owner's parent-space translation is treated as world-space in `drawnOrigin`.
- **TRIGGER:** Live triggers; a panel editor for triggers; the Shockwave flash light; a burst repeats
  on a paused frame.
- **FXL:** a Bioluminescence flare and spill; the tree-energy → Pulsing Veins migration; Fresnel
  opacity; Growth by arc length; a Rim Light sourced from a Light; displacement on skinned meshes; a
  recomputed normal under displacement; Motion Smear's previous frame uses the current smear.
- **Stars:** parallax layers (point geometry, as Glowmere's cosmos draws).
