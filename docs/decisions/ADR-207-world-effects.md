# ADR-207: World effects propagate through one frame-global block, not through per-object state

Status: Accepted; the rule that a cut-gated effect stops firing once the cut stops driving the camera is superseded by [ADR-582](ADR-582-taking-the-camera-back-parks-the-cut-it-never-destroys-it.md) (owner's ruling, 2026-09-21): taking the camera back parks the cut, and HeroFocus and CameraTravel effects keep following its saved schedule.

## Context / Problem

The brief asks for two things that look like two shaders and are not: a beam that travels through the
world ahead of a camera on its way to the next hero, and a ripple that spreads out from a hero
mushroom's base while the camera is holding it. Both are *spatial phenomena with a source, a
propagation geometry, a speed, an appearance, a lifetime and a wish to be modulated*, and the list
of others anybody would want next -- a UFO's energy pulse, a shockwave on a drop, a forest waking
up, an object-to-object energy link -- is the same description with different numbers.

The engine had nowhere to put that. It has fields (ADR-025), which are spatial *control signals*
sampled by deformers, effectors and particles, and which do not reach surface shading. It has
material programs (ADR-030), which are per-material and would need one authored program per asset.
It has post-processing (ADR-016), which is screen space and by construction cannot know that the
front of a wave has reached a particular fern. And it has entity reactions (ADR-088/097), which
scale what an entity already does rather than painting light across ground the entity is standing
on.

So the question was where a *world-space, view-independent, material-preserving* additive term
belongs, and what an effect has to be for a second one to cost nothing.

## Alternatives considered

1. **A per-object effect list, uploaded per draw.** Rejected: ADR-135 records that `ObjectUniforms`
   has no free lane, ADR-128/129 that object data is a uniform buffer sized by an allocation policy,
   and §20 of the brief rules out CPU iteration over every environmental object per frame. Glowmere
   submits 200+ draws and 60k procedural instances; deciding per object which effects touch it is
   the one design that cannot be afforded.
2. **A new bind group / a new storage buffer at group 0.** Rejected as invasive rather than wrong:
   every bind-group layout in `scene_renderer.cpp`, `procedural_renderer.cpp`, `sdf_renderer.cpp`,
   `shadow_renderer.cpp` and `water_renderer.cpp` would have to grow a binding, and the shadow views
   copy the frame block wholesale. The data is ~1 KB. A buffer is what you reach for when the data
   is unbounded, and this one is bounded on purpose.
3. **A screen-space pass over the depth buffer.** Rejected: §21 forbids the look it produces, and it
   cannot amplify an existing emissive material, which §10 asks for. Reconstructing world position
   from depth and painting it is also strictly more work than evaluating the same function where the
   surface is already being shaded.
4. **An effect as a `spatial::FieldSpec`.** Considered seriously -- fields already pack to the GPU,
   already animate off `tau = speed * t + phase`, and already have a slot table. Rejected because a
   field is a *scalar or vector sample*, and an effect's output is a radiance with a colour, an edge,
   a sparkle and a per-material response. Bending fields to carry that would make every consumer of
   fields pay for it.

## Decision

**A world effect is authored data, resolved on the CPU once per frame into a fixed-size, GPU-ready
record, and evaluated in the shared surface shader as an additive term.**

Four pieces, each already the shape of something the engine has:

**The data model** is `world::WorldEffect` in `src/world/effects.hpp`: a `Source`, an optional
`Target`, a `Propagation` (kind, direction mode, speed, range, front width, trail), an `Appearance`
(colour, intensity, edge colour, rainbow), a `Sparkle`, a `MaterialResponse`, an `Activation` and a
`Timing`. It round-trips as a `"worldEffects"` array in the scene file, beside `"heroes"` and for
the same reason: an effect is a thing in the world, not an instruction to the renderer.

**Resolution is a pure function of time.** `world::resolveWorldEffects(effects, context)` takes the
authored set and a `WorldEffectContext` -- the second on the transport clock, the camera's pose and
velocity, a `WorldEffectScene` that can answer "where is the node called X", the hero table, and a
span of `ShotSpan` baked from the director's `Sequence` -- and produces at most
`kMaxGpuWorldEffects` resolved records. Nothing in it reads a frame counter, a wall clock or the
previous frame's output, which is what keeps an offline render of second N byte-identical to a
realtime playthrough of second N. The camera's velocity, which a naive implementation would take as
a frame-to-frame difference, is a finite difference *in timeline seconds* over a fixed step, taken
by evaluating the baked `camera/position` track -- so it is the same vector at 30 fps and at 120.

**The transport to the GPU is `FrameUniforms`.** The block grows by one count vector and eight
144-byte records, appended after `lights` so no existing offset moves. This is the same argument
ADR-055 made for the wind field: it is frame-global because the world is, and the shadow views
inherit it with the rest of the block so nothing can disagree with itself.

**Evaluation lives in `shaders/world_effects.wgsl`, included by `pbr_shade.wgsl`.** That one include
reaches entities (`pbr.wgsl`), skinned characters (`pbr_skinned.wgsl`, which includes `pbr.wgsl`
unchanged), the procedural scatter (`procedural.wgsl`) and raymarched SDF surfaces
(`sdf_raymarch.wgsl`) -- so terrain, vegetation, rocks, mushrooms, props and characters all receive
the effect without one line of per-asset code. Water includes it separately, because it shades
through its own pipeline; see the consequences below. The result is **added** to the shaded colour and to
the emission target, never substituted for the base colour: `finalSurface = normalSurface +
propagationContribution`, which is what keeps a material looking like itself under a wave.

The loop is `for (i < frame.effectCount.x)`, and `effectCount.x` is uniform across the draw, so a
frame with no effects executes one uniform compare and nothing else.

## Rationale

The expensive decisions in a system like this are all about *where the per-object question is
asked*. Asking it on the CPU costs an iteration over the scene; asking it per draw costs a uniform
upload; asking it per fragment, from data that is already resident, costs a handful of ALU. The
third is the only one that scales to 60k instances, and it is also the only one that gets the
environment interaction §9 asks for for free -- a fragment knows its own world position, so the
wave front lands on the ground where the ground is and on a leaf where the leaf is, with no
projection, no ray query and no per-object bounds test.

Sources, targets and direction modes are resolved on the CPU rather than in the shader for the
mirror-image reason: there are at most eight of them, they involve string lookups and a hero table,
and resolving them once per frame instead of once per fragment is five orders of magnitude of
arithmetic nobody has to do.

Two propagation kinds ship. `DirectionalWave` and `RadialWave` are not a minimal pair chosen to
satisfy the brief; they are the two *distance metrics* -- a dot product along an axis, and a radius
in the ground plane -- and everything the brief lists as a future kind is one of those two with a
different mask. A ring is a radial wave with a short trail; a shockwave is a radial wave with a fast
speed and a hard edge; a beam is a directional wave with a lateral falloff; a cone is a directional
wave whose lateral falloff grows with distance. The extension point is therefore *the mask*, and it
is one function in one WGSL file.

## Consequences

* A ninth simultaneous effect is dropped with a warning. Eight is a cap chosen the way
  `spatial::kMaxGpuFields` chose sixteen: large enough that nothing real hits it, small enough that
  the per-fragment loop has a bound anybody can reason about.
* Material response is **three weights and a derived groundness**, not a named category per asset.
  The shader knows whether a draw is the procedural scatter (`kProceduralDraw`, which already
  exists) and it knows the surface normal; it does not know that this entity is "a rock". A named
  per-material category needs a lane in `ObjectUniforms` that ADR-135 says is not there *and* a
  category field on `scene::Entity` that nothing currently carries. Recorded as the extension point
  rather than approximated by a name prefix, which would be a control that lies at the first scene
  that names something `rock_platform`.
* Water (`water.wgsl`) has its own pipeline and so takes the term itself, in three added lines.
  This was going to be an exclusion -- water has its own shading for the reasons
  `water_renderer.hpp` gives, and ADR-183/184 record what chasing the water surface costs -- and the
  first render of the hero pulse overturned it in one frame: Glowmere's elder stands in a pool, so a
  ripple that stopped at the shoreline drew a hard straight edge across the exact shot the effect
  exists for. The addition is purely additive and touches none of water's normals, refraction or
  foam, which is why it is three lines and not a negotiation.
* The block is in `FrameUniforms`, so `sizeof(FrameUniforms)` moved and the WGSL mirror had to move
  with it. The `static_assert` written as a sum (ADR's own note in `scene_renderer.hpp`) is what
  makes that a compile error rather than a silent misread.

## Rejected alternatives

* **A general "effect graph".** The brief explicitly warns against a large abstraction layer for
  theoretical flexibility, and the listed future effects are all reachable by adding a mask, a
  source kind or an activation -- each a value in an existing enum, not a new layer.
* **Per-effect render passes.** One additional full-resolution pass per effect is exactly what §20
  rules out, and the cost would be paid whether or not the effect is on screen.

## Revisit triggers

* A scene wants more than eight simultaneous effects, or wants effects to be *spatially* culled.
  Both point at the same answer -- a storage buffer and a per-cluster effect list, keyed off the
  froxel grid ADR-033 already builds.
* `scene::Entity` gains a semantic category (from a semantic-asset import, ADR-060), at which point
  the per-material response table in §10 of the brief becomes implementable and this ADR's
  three-weight approximation should be replaced rather than extended.
* The per-fragment cost measured in ADR-208 grows past the band recorded there on content with
  heavier overdraw than Glowmere.
