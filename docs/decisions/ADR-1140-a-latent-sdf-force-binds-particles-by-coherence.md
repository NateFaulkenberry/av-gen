# ADR-1140: A latent SDF force binds particles to a named SDF by coherence

**Status:** Accepted (proto/astral-forge, the production path of THE ASTRAL FORGE, step 1)
**Date:** 2026-10-05
**Resolves:** `docs/prototypes/astral-forge/04-architecture.md` "Production path" item 1, and ADR-704's
condition for an SDF binding ("a new kind with a real binding ... gets a new ADR, not a revival").
**Implemented by:** `shaders/particles.wgsl` (`cs_latent`), `shaders/sdf_program.wgsl` (the interpreter
without includes; `shaders/sdf.wgsl` now includes it), `src/rendering/particle_renderer.{hpp,cpp}`,
`src/scene/particles.{hpp,cpp}` (`ParticleLatent`, parameters), `src/scene/particle_io.cpp` (JSON),
`src/scene/particle_latent.{hpp,cpp}` (the CPU reference), `src/scene/composition.cpp` (the load-time refusal
and the nesting prefix)
**Tests:** `tests/unit/test_particle_latent.cpp` (round trip, refusals by name, load-time reference
refusals, parameters, the binding curve, the projection); `tests/rendering/test_particle_latent_gpu.cpp`
("coherence 1 pulls the matter onto the sphere, coherence 0 leaves it where it was", "one GPU step matches
the CPU reference, binding and release alike", "deterministic across fresh renderers, and a zero-strength
latent changes no byte"); `[gpu][particles][latent]`.

## Context

The Astral Forge prototype's central finding (`04-architecture.md`, approaches A-E) is that the form must
come from the matter. A latent anatomy, an SDF that is never drawn, moves the particles. The visible
surface is made from the particles' density (ADR-1141, ADR-1142). Production particles could not do the
first half. No field could tell a particle where an SDF's surface is. ADR-704 removed
`FieldKind::SdfDistance` because it was never bound and silently returned 0. That ADR set a condition: a
future version has to bind for real, refuse what it cannot resolve, and get its own ADR. This is that ADR.

The prototype's force (`prototypes/astral-forge/shaders/sim.wgsl`) has four parts:

- a per-particle binding threshold θ. Coherence C binds a particle by `smoothstep(θ - w, θ + w, C)`.
- a damped spring toward the SDF's projection `p - d ∇d`. Its stiffness is `18 + 70 C²` and its damping
  ratio is 0.55.
- a tangential flow, which is curl noise projected into the tangent plane.
- a release impulse along the normal when coherence drops: the "violent collapse".

## Decision

**A particle system may carry a `latent` block naming an SDF object of the same scene:**

```json
"latent": {"sdf": "mask", "coherence": 0.8, "width": 0.08, "strength": 1.0, "flow": 0.0, "release": 12.0}
```

The block is optional, and every key except `sdf` defaults to the value shown.

**The force.** A new compute entry `cs_latent` is dispatched between `cs_emit` and `cs_simulate`, and only
for a system that has a latent. It adds to each live particle's velocity:

- θ = mix(w, 1 − w, fract(seed × 61)). `seed` is the particle's own random. Keeping θ inside [w, 1 − w]
  means **coherence 1 binds every particle fully and coherence 0 binds none**. The prototype's role ranges
  (θ up to 0.99) left a fifth of the matter half-bound at C = 1.
- b = smoothstep(θ − w, θ + w, C). release = max(b(C_prev) − b(C), 0), where C_prev is the coherence the
  pool's previous step used.
- proj = p − d·∇d in the SDF object's local space. ∇d comes from tetrahedral differences, 4 evaluations
  of the packed program. d is their mean. The tap distance is 1e-3 of the object's bounds diagonal.
- acc = b·(K·(proj_world − p) − 2·0.55·√K·v), with K = strength·(18 + 70 C²).
  K is capped at 0.8/dt². At that cap the semi-implicit step is stable for any frame length the renderer
  allows (dt ≤ 0.1). At 60 fps the cap never binds below strength 32.
- plus b·flow·(curl − n(n·curl)). This is the system's own turbulence curl (`turbCurl` at
  `turbulenceScale`/`turbulenceSpeed`), in the tangent plane.
- v += acc·dt. Then, if release > 0, v += release·`release`·n_world·sgn·(0.6 + 0.8 h). Here sgn is −1
  for a fifth of the particles, as in the prototype, and h is another hash of the seed.

`cs_simulate` then integrates the velocity exactly as it always has. Gravity, drag, turbulence and field
forces still act on top. The spring at C = 1 has K = 88 against a default turbulence of order 1, so bound
matter stays bound.

**The binding is real.** The SDF object is found by its flattened name in `Scene::sdfs` every frame. Its
parameters are live, so a route or the editor reshapes the latent. The object is packed with
`spatial::packSdfTree` into **the particle pool's own storage buffer**. `DisplaceField` slots are remapped
to GPU field slots exactly as `SdfRenderer` remaps them. `SdfRenderer`'s node buffer is not used. It holds
only the visible raymarch objects and reallocates, and a latent is normally invisible (`visible: false`),
which is exactly the object `SdfRenderer` skips. The program runs through `sdf.wgsl`'s interpreter, split
into `sdf_program.wgsl` so that `particles.wgsl`, which already includes `fields.wgsl`, can include it
without defining every field function twice. The shader library does not de-duplicate includes.

**Refused at load, by name.** `Composition` refuses a scene in which a latent names no node, or names a
node that is not an SDF:

```
node 'matter': latent.sdf 'nowhere' names no sdf node in this scene
```

It also refuses a latent whose tree the interpreter cannot evaluate. A compiled object may be deeper than
the interpreter's 8-entry stacks (ADR-1005), and the particle simulation has only the interpreter. The
check is `SdfTree::validate(SdfEvaluator::Interpreter)`, the same one `evaluatePacked` is bound by. A scene
that is edited live after loading can still lose its target. The renderer then logs it once and switches
the force off for that system. Nesting prefixes the reference like every other cross-reference
(`prefixFieldReferences`).

**The CPU reference.** `scene/particle_latent.cpp` holds the reference: `latentTheta`, `latentBinding`,
`latentStiffness`, `latentProject` (over `evaluatePacked`) and `latentVelocityStep`. They use the same
formulas in the same order. The GPU test runs one step on the GPU over a live, partly bound field. It
compares every particle's velocity with the reference: worst relative error < 1e-3, over > 500 moved
particles. It does the same across a 0.6 → 0.2 drop, which exercises the release. The flow term reads the
curl noise, which has no CPU mirror, so the parity test runs with flow 0.

**Parameters.** These are registered only when the block is present:

- `particles/<name>/latent/coherence` (0..1, the "summon" fader of `03-parameter-state-model.md` §6)
- `particles/<name>/latent/strength`
- `particles/<name>/latent/flow`
- `particles/<name>/latent/release`
- `particles/<name>/latent/width`

All are uniforms: no rebuild and no pipeline change, so they are safe on a fast MIDI knob.

**The last storage slot.** The particle compute layout already used **nine of the adapter's ten** storage
buffers per stage: bindings 1-7, 9 and 15. The latent program is the tenth, at binding 10. The particle
compute pass now has no storage slot left. ADR-1141's density passes therefore have a layout of their own.

## Consequences

- With the latent absent (`sdf` empty) the pass is not dispatched, the uniforms it reads stay zero, and
  `cs_simulate`'s code is untouched. A system without a latent is **byte-identical**. Measured on this
  branch's binary against a pre-change binary: `stellar-nursery.json` and `particle-vfx-lab.scene.json`
  at `--range 2:2`, 0 differing channels out of 1920×1080×4.
- A latent with strength 0, release 0 and flow 0 is dispatched and changes no byte of the pool. This was
  measured against the same system with no latent, under live turbulence, over 45 frames.
- Determinism (ADR-360's contract) is unchanged. The force is a pure function of the particle's state, the
  uniforms and time. Two fresh renderers produce identical pools. `test_particle_determinism_gpu` still
  passes. Seek remains ADR-360's relaxed seek. On a reset the first step takes C_prev = C, so a seek
  releases nothing.
- Cost (measured; table in ADR-1142): the particle compute pass for 1M particles goes from **1.05 ms to 7.01
  ms** with the example's crude-mask latent (7 packed records) at coherence 0.93. That is about 6 ms per
  million bound particles, the 4 SDF evaluations per bound particle per step that the prototype also
  measured. The prototype's staggered projection (refresh every third step)
  was **not** ported. It needs a per-particle target store, and the pool record is full (64 bytes, ADR-015).

## Rejected alternatives

- **A field kind (`FieldKind::Latent`) sampled by a field force.** A field force gets one value or vector
  at the particle. The latent needs the projection, the normal, the per-particle threshold and the
  previous coherence: a force with its own state. A field kind would have revived ADR-704's
  `SdfDistance` under another name. It would also need the SDF program bound wherever fields are
  sampled, which is every pipeline that includes `fields.wgsl`.
- **Binding `SdfRenderer`'s node buffer.** It holds only visible raymarch objects, it reallocates when the
  set grows, and it is written after the particle pass. The latent would read last frame's program, or
  nothing.
- **Folding the force into `cs_simulate`.** That would change the simulate kernel's code, and so its
  register allocation and possibly its arithmetic, for every particle system in the repository. ADR-388
  measured exactly that kind of drift. A separate entry point dispatched only when wanted leaves every
  other system's kernel alone.
- **The prototype's roles (eye 9%, mouth 7%, ...).** Roles are authored content. Production keeps one
  threshold distribution. Several systems with different latents and coherences give roles.

## Revisit triggers

- **Another storage buffer is needed in the particle compute stage.** There is none. The next one means
  merging buffers, or moving the latent program into a second bind group whose buffer the adapter limit
  still counts.
- The simulation dominates a frame with a latent at the particle counts a scene ships (more than about
  4 ms per million particles). Port the staggered projection, which needs a pool record wider than
  64 bytes.
- A latent needs a compiled-only tree. The particle module would need a compiled variant per tree, as
  ADR-1003 does for the raymarch.
