# ADR-359: The tree ignores the wind twice over, and particles are allowed to drift

- Status: Accepted (2026-09-19)
- Supersedes nothing. Builds on ADR-055 (the wind field), ADR-015/040 (GPU particles),
  ADR-091 (two-tier determinism), ADR-264 (a project's parameters over its scene),
  ADR-350 (a system the application runs and does not keep).
- Scene: `examples/treeisland/tree-of-life-floating-island.{json,scene.json}` — the COSMIC variant.

## Problem

The owner's brief opens with: the Tree of Life does not respond to the scene's wind speed or wind
direction, and those parameters already exist. Find out why before adding anything.

## What was measured

One frame at t = 8.00 s, 1920x1080, Release, `tools/gpu-lock.sh`, all four arms rendered in one
serialised batch from the same freshly built `src/avgen`. Compared by sha256 of the PNG.

| arm | change | sha256 (first 16) |
|---|---|---|
| `wind0` | `scene/windSpeed = 0.0` | `15bed2be140aaaed` |
| `wind4` | `scene/windSpeed = 4.0` (the parameter's hard max) | `15bed2be140aaaed` |
| `windon` | scene gains `"wind": {enabled: true, speed: 2.5, direction: 0.6, gustAmount: 1.2}` **and** `scene/windSpeed = 2.5` | `15bed2be140aaaed` |
| `ctl-emis` | `nodes/tree-foliage/emissiveBoost = 0.5` (control) | `d94bc5bf9ddf2215` |

The control is the probe that could have fired the other way (ADR-182): it is the same kind of
change — one entry in the project's `parameters` block — and it moves 174 821 pixels by more than
one luminance level. The render harness is sensitive to a project parameter. It is not sensitive to
the wind, at any value, with the field switched on or off.

## The two independent causes

### 1. `wind.enabled` is a gate with no UI and no writer

`wind::WindParams::active()` is `enabled && speed > 0`. `enabled` is set by
`wind::windFromJson` (`src/core/wind.cpp`) and by nothing else. `Composition` reads it from a
**top-level** scene key `"wind"` (`src/scene/composition.cpp:7659`) — not from `environment.wind`,
which is where a reader would look. This scene has no such key, so `windSetting_.enabled` is
`false` for the whole session.

Of `WindParams`' ~17 authored fields, exactly two are registered as parameters
(`src/scene/composition.cpp:3298-3300`): `scene/windSpeed` and `scene/windDirection`. `enabled` is
not among them. The comment above that registration says "Speed 0 is a genuine no-op:
`WindParams::active()` is false" — which reads the gate as *speed alone*, and is the mental slip
that let this ship.

The scene **writer** closes the loop: `composition.cpp:7147` emits the `wind` block only
`if (windSetting_.enabled)`. So from inside the application there is no sequence of actions that
turns the wind on: the value that would enable it has no control, and the file that would record it
is only written once it is already enabled. The owner's `scene/windSpeed = 1.319` in the shipped
project is a slider they dragged, saved, and reloaded, and it has never done anything.

This is ADR-350's finding again, in a system ADR-350 did not sweep: a reader without a writer, and
a setting the application cannot reach.

### 2. The Tree of Life is not eligible for the deformation anyway

`windDisplacement` / `windSampleAt` are called from exactly one place in the shader tree:
`shaders/procedural.wgsl:445-449`, the instanced **procedural scatter**. The shared mesh vertex
stage every imported asset goes through — `vs_main` in `shaders/common.wgsl:215-226` — is

```wgsl
let world = object.model * vec4<f32>(in.position, 1.0);
```

and nothing else. `pbr.wgsl` does not include `wind.wgsl`. The Tree of Life is five imported GLBs
(`tree-wood`, `tree-twigs`, `tree-tracery`, `tree-foliage`, `tree-lumens`), and the island is a
sixth; every one of them is a mesh node. **There is no vertex deformation path for mesh nodes at
all** — not a disabled one, not a mis-parameterised one. `arm windon` proves this half on its own:
with the field authored on in the scene file and the speed parameter at 2.5, the frame is still
byte-identical to wind off.

`src/scene/tree_rig.{hpp,cpp}` is a real hierarchical skeleton animator, but it binds a
`scene::TreeGraph` — a *procedurally generated* tree — and cannot be pointed at an imported GLB.

### 3. Particles do not read the wind either

`shaders/particles.wgsl` does not include `common.wgsl` and has no frame uniform bound. There is no
coupling between `wind::` and `scene::ParticleSystem`. Air reaches particles only as the system's
own curl-noise `turbulence`, or a `FieldForce`.

## Decision

1. **The wind field stays.** ADR-055's field is correct, deterministic, spatial and already
   transliterated CPU/GPU. The defect is reach, not model. Every remaining `WindParams` field is
   registered under `scene/wind/*`, the scene writer emits the block unconditionally once any field
   is non-default, and `scene/wind/enabled` becomes an ordinary parameter. A parameter that gates
   itself out of existence is not a parameter.
2. **Mesh nodes gain a wind deformation path** in `common.wgsl`'s `vs_main`, gated by a per-object
   flag so every existing scene is byte-identical. The deformation is a continuous function of
   **world position** — height above the tree's base and horizontal distance from its axis — rather
   than of mesh identity, because the tree is six meshes that touch: `tree_rig.hpp` already
   documents that bending each mesh about its own base separates them at every joint, and that is a
   property of the decomposition, not of the amounts. A field sampled in world space gives the same
   displacement to two vertices at the same point whichever mesh they belong to, so there are no
   cracks by construction, and the trunk/branch/foliage hierarchy comes out of the radial and height
   profiles rather than out of per-mesh amplitudes.
3. **Deformation stays a pure function of time** (ADR-091), as does canopy shimmer, tree energy
   propagation and vortex density. Scrubbing to a frame and finding the trunk bent the other way is
   an artifact on a hero asset.

## The determinism relaxation, for particles only

The owner, 2026-09-19, verbatim:

> "I think we can ease the scrub must exactly replay particle animations yeah? that might help the
> tree of life agent."

This is an owner decision, not a reinterpretation of ADR-091. Recorded as such. Scope and
boundaries, as set with it:

- **Particles only.** Falling leaves, tree particles and vortex particles may be stateful. Tree
  wind, shimmer, energy and vortex density may not — they are cheap to express as functions of time
  and the artifact would be visible.
- **A render must still be reproducible.** Scrub may differ from play; two renders of the same
  range may not differ from each other. That is already almost true — ADR-015 rev 2 removed every
  atomic for exactly this reason — but two facts break it today and must be fixed:
  - `FixedStepClock::seek` sets `frameIndex = 0` (`src/core/time.cpp:52-58`), and particle spawn
    randomness is `pcg3d(slot, frameIndex + seed*7919, salt)`. Spawn RNG is therefore a function of
    *frames since the render started*, not of timeline position, so a render of 60-70 s does not
    splice with the same seconds of a 0-70 s render. `FrameTime::frameNonce()` exists for precisely
    this and particles do not use it.
  - A render starting at t > 0 begins with **empty pools** (`ParticleRenderer::resetPool`), so the
    head of every partial render blooms in from nothing.
- **Seek behaviour is stated, not implicit.** Today a seek hard-resets every pool to empty
  (`SceneRenderer::resetTemporalHistory` -> `ParticleRenderer::resetAll`) and the field refills over
  roughly one particle lifetime. We keep that as the default — it costs nothing and cannot stall the
  UI, which matters because `EntityWorld::seek` already re-simulates up to 90 s per scrub click and
  is the measured cause of the app's scrub lag. A bounded, opt-in **warm-up** (a parameter, in
  frames, capped) fills the pools from the seeded state on seek and at the head of a render range,
  so an offline render is not required to accept the bloom. What we do not add is a second unbounded
  re-simulation on scrub.

## Consequences

- `scene/windSpeed`'s existing value in shipped projects starts doing something the first time the
  wind is enabled. That is the point, but it is a visible change to a file the owner edits live, so
  `scene/wind/enabled` defaults to **false** and no existing scene moves until it is turned on.
- The world-space deformation is a *proxy* hierarchy, not the tree's real topology: a branch that
  happens to hang low and near the trunk moves like a trunk. At this scale that is not readable, and
  the alternative — a skeleton — needs a `TreeGraph` the GLB does not carry.
- Soft particles are authored, serialised, uploaded into `u.turb.z` and never read by any shader
  (`shaders/particles.wgsl`). Leaves near the island will intersect it hard until that is finished.

## Revisit when

- An imported tree arrives with per-vertex branch/tier attributes or a skeleton, at which point the
  world-space proxy should give way to `tree_rig`.
- Anyone asks for scrub-exact particles again: the relaxation above is the reason they are not.
