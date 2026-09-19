# ADR-370: A leaf is a card, the canopy is measured, and the air is one description

- Status: Accepted (2026-09-19)
- Extends ADR-015/040 (GPU particles), ADR-055 (the wind field), ADR-360 (mesh wind),
  ADR-367 (soft particles, which this depends on).

*Numbered 370: main carries 360-367, 368 is the Image/Look branch's and 369 is this branch's comet
fix.*

## Problem

The brief's falling leaves, and three things standing between the existing particle system and them.

1. **Particles are round dots.** `vs_particle` builds a camera-facing quad and `fs_particle` draws a
   radial falloff. There is no texture, no atlas and no per-particle rotation. The brief is explicit
   — "Do not use generic glowing dots… Leaves should visually read as actual leaves" — and an
   additive round blob is precisely the failure it names.
2. **An emitter is a box somebody typed numbers into.** The brief asks for leaves that "originate
   from the Tree canopy rather than from a generic box emitter".
3. **Particles cannot see the wind.** `particles.wgsl` binds no frame uniform and never has, so
   there is no coupling at all between `wind::` and `scene::ParticleSystem`. A leaf drifting in
   still air while the branch it fell from bends is ADR-360's defect one object along.

## Decisions

### 1. A leaf is a card, and two rotations are enough

`ParticleShape { Round, Leaf }`. Round is the default and is what the system has always drawn.

Leaf spins the billboard basis in the view plane and **squashes the short axis** by
`abs(cos(spin * 0.7 + seed))`. The first rotation is the leaf yawing as it falls; the second is the
same card seen at an angle, going edge-on twice a revolution. That is cheaper than orienting a real
quad in three dimensions and indistinguishable at the size a leaf occupies on screen. The squash is
on the *short* axis deliberately: a leaf turning edge-on gets narrower, not shorter.

`faceLit = mix(1 - twoSided, 1, abs(edge))` is the two-sided shading the brief asks for, for one
multiply, with no normal and no light lookup.

Velocity stretch is **skipped** for leaves. A smeared leaf reads as a spark, which is the failure
mode being avoided.

The silhouette is a pointed ellipse — half-width `0.66 * cos(along * pi/2)^0.62` — with a faint
midrib, because the rib is what the eye uses to tell a leaf from a petal. Soft-edged, so the card
antialiases rather than stair-steps.

### 2. The canopy is measured, not typed

A `Particles` node may name a `canopySource` and a `canopyFrom`, and its emitter box is then derived
at bake from that node's **combined world bounds**, biased to the upper `1 - canopyFrom` of the
height so leaves do not fall out of the trunk.

The point is not the shape of the box. It is that a measured box **follows the tree** when the tree
is moved, rescaled or swapped for another asset, and a typed one silently stops describing it. The
bounds walk is factored out of ADR-360's `applyWindBodies` into `subtreeWorldBounds` and shared, so
the wind body and the leaf emitter cannot drift apart about where the tree is — they are asking the
same question and asking it twice in two ways is how two answers diverge. It uses
`Scene::meshBounds`, the caching accessor, per ADR-355.

### 3. The wind is copied into the particle uniforms, not reached through a bind group

`particles.wgsl` has no frame bind group. Adding one to get four vectors would change the layout of
every particle pipeline. Instead ADR-055's packed `WindUniforms` ride in `ParticleUniforms`, and
`cs_simulate` samples them with a transliteration of `windSampleAt` — the same expressions in the
same order, because the entire value of a shared field is that every consumer agrees about what the
air is doing at a point.

The force is `(flow * windInfluence - horizontalVelocity) * windInfluence`: a pull *toward matching
the air* rather than a shove, so a leaf accelerates until it is travelling with the wind and then
stops. That is what drag against moving air does, and it is why leaves settle into the flow instead
of being launched by it.

`windInfluence` defaults to **0**, so no existing particle system starts drifting.

## The measurement

`tests/rendering/test_leaf_particles_gpu.cpp`, 2 cases, 498 assertions, `tools/gpu-lock.sh`:

- A Leaf system differs from a Round one, and covers **fewer** lit pixels at the same `size` —
  the assertion that catches the silhouette silently falling back to the round falloff. Both arms
  are checked non-blank first, or the comparison is between two empty frames.
- `windInfluence = 0` is **byte-identical** in a gale and in dead air.
- Switched on, the same gale moves them; and reversing the **direction** at the same speed moves
  them differently again, which is a separate claim from "responds to speed" and is asserted
  separately.

In the shipped scene, at the hero camera over eight seconds:

| | pixels differing by >4 | mean abs dL |
|---|---|---|
| `windInfluence` 0 → 1.4 | 54 385 | 0.598 |
| wind direction reversed | 280 739 | 5.422 |

Direction moves far more than influence does, which is the right ordering: turning the air around
redirects every leaf *and* the tree they fell from, since both now read one field.

## What shipped, and the tuning that got there

`spawnRate 60`, `size 1.9`, `gravity -7`, `drag 0.16`, `lifetime 13-22 s`, `emissive 0.30`, alpha
blend, `softness 2.5`, `windInfluence 1.4`, `tumbleRate 2.6`, `leafAspect 0.40`, `twoSided 0.55`.

Three tunings were rendered and rejected before that one. At `spawnRate 380` / `size 3.6` the frame
is a blizzard that buries the tree — visible, certainly, and a direct violation of §23's hierarchy,
which puts the Tree first and micro-detail last. At `emissive 0.9` the leaves wash to pale cream and
stop being foliage. And leaves spawned through the *whole* crown volume read as the tree bursting
rather than shedding, which is why `canopyFrom` is 0.6 rather than the 0.42 first tried.

The legibility test that matters is not a hash. It is whether the frame reads as "a tree shedding
leaves" at a glance, and the rejected arms all changed the hash while failing that.

## Consequences

- Leaves depend on ADR-367's soft-particle fade. Without it they show a hard card edge where they
  cross the island's rim, which is the dependency flagged when that was written.
- The tumble is a screen-space fake. At a very close camera a leaf will not read as a solid object
  turning in three dimensions. The brief's camera is 219 m away and this is not that shot.
- `canopySource` names a node by string. A renamed node orphans it, and the loader warns and falls
  back to the authored box rather than failing — the same trade ADR-232 makes for parameter paths.

## Revisit when

- Anyone wants leaves at a close camera, which is the point a real oriented quad and a normal start
  to be worth their cost.
- A second system wants the canopy emitter: the bias to the upper part is currently one scalar, and
  a shell rather than a solid box would be better for anything shed from a surface.
