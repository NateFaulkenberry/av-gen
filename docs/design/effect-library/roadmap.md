# Roadmap: implementation waves

This page is part of the [Effect Library](README.md). Waves are ordered by **shared dependency**. A
wave builds the primitives that the most remaining effects need, and it ships at least one effect per
primitive, so no primitive lands without a user. This follows the repository's standing lesson that
subsystems with no users and "built but unreachable" features are its signature defect.

| Wave | Primitives | Effects | Why here |
|---|---|---|---|
| **1** | SIGNALS, scene-query growth, metadata, FXL (emission/rim/pulse), LIGHTMOD pool, HIST, RIBBON, DF, EMIT | Glow, Pulse, Bloom Source, Trail (+ Light Trail), Space Warp, Particle Emitter (Fireflies, Magic, Embers); **UFO stack demo** | Proves the Entity Effects architecture end to end on the three hardest ownership questions: an entity's *velocity* driving an effect, its *history* driving an effect, and an entity-scoped *material* term. It also proves three stages stacking on one owner. Every later wave reuses these primitives. |
| **2** | two-phase evaluator + XFORM, TRIGGER, FXL clip/displace (+ `fs_depth`), Worley F2 | Orbit, Spiral, Float, Shake (Entity), Bounce, Shockwave, Ripple (Membrane), Velocity Distortion, Dissolve, Growth, Breathing, Organic Pulsation, Motion Smear, Fresnel, Rim Light, Color Cycling, Bioluminescence, Pulsing Veins, all remaining particle presets, Stars | Once one entity can carry a term, the next dependency is *where the entity is* (XFORM) and *when events happen* (TRIGGER). FXL clip needs the depth-pass change that fixes the shadow of anything that disappears. The Glowmere organic look (bioluminescence, veins, growth) lands here because it needs only FXL. |
| **3** | BOLT, SHELL | Lightning, Arc, Electric Field, Plasma, Energy Shield, Force Field, Charge-Up, Discharge, Light Beam, Halo, Bubble, Portal (no Remote View), Reality Tear, Heat Shimmer, Gravitational Lens | SHELL and BOLT together serve about 15 effects, and all of them also need DF, RIBBON, LIGHTMOD and TRIGGER from Waves 1–2. That is why they cannot come earlier. |
| **4** | FXPOST (H1, H2), REDRAW | Radial Distortion, Scanlines, Pixelation, Dithering, Toon Edges, Chromatic Aberration (glitch + spectral lens), God Rays (Screen), Heat Haze, Aura, Afterimage (Geometry), Hologram, Energy Shield (Contour) | Camera looks partly exist already (lens CA, hueShift, FrameEcho), so this wave has the lowest *new-capability* value per unit of work. REDRAW needs pipeline variants for skinned and wind paths, which are easier once FXL has settled the object-layout work. |
| **5** | TEMPORAL (history ids, stride, **warm-up**), MEDIUM shadowed march, LOCALTIME | Echo/Smear/Ghosting (`temporalFilter`, FrameEcho migrated), Freeze-Frame, Reverse, Time-Warp Distortion, Afterimage (Image), Time Dilation, Delayed Motion, God Rays (Volumetric), Volumetric Beam (occluded) | The highest seek-determinism risk: an ADR-410 warm-up, a per-prefix time map in the timeline, and clocks in checkpoints. It comes after HIST (Wave 1) has proved that checkpointed per-entity state works for effects. |
| **6** | migrations and heavy geometry | Wind Response (ADR-360 lanes → effect), Sway, tree energy → Pulsing Veins migration, Camera Shake → Shake migration, Tendrils (instanced tube), Portal Remote View | These are content migrations with no compatibility shims (ADR-441). They are cheap to write and expensive to get right across every project file, so they wait until the effect forms are stable. Tendrils and Remote View are the most expensive new renderers. |

---

## Status (2026-09-25): Waves 1–3 shipped; the library is paused here

The owner has paused the Effect Library after Wave 3; the effort moves to the motion stack and then the
Director. This section is where to pick up.

### What shipped

| Wave | ADR | Primitives | Types (kind number) |
|---|---|---|---|
| — | 702 | the instance model: owners, stacks, render stages, one evaluator | Comet 0, Aurora 1, Vortex 2, Meteor Shower 3, Volumetric Fog 4, Tornado 5, Ground Pulse 6, Travel Beam 7 |
| 1 | 703 | SIGNALS + `owner.` routes, HIST, FXL (emission, rim, pulse), LIGHTMOD pool (16), RIBBON, DF, EMIT | Glow 8, Pulse 9, Bloom Source 10, Trail 11, Space Warp 12, Particle Emitter 13 |
| 2 | 716 | two-phase evaluator + XFORM, TRIGGER, FXL clip / displace / pattern in every pass, Worley F2, DF Disc / Shock / Ripple / Wake | Orbit 14, Spiral 15, Float 16, Shake 17, Bounce 18, Shockwave 19, Ripple 20, Dissolve 21, Growth 22, Breathing 23, Organic Pulsation 24, Bioluminescence 25, Pulsing Veins 26, Fresnel 27, Rim Light 28, Color Cycling 29, Velocity Distortion 30, Motion Smear 31, Stars 32; 20 more Particle Emitter presets |
| 3 | 719 | BOLT, SHELL (9 shading kinds), DF Facing / Cylinder / Shimmer / Lens | Lightning 33, Arc 34, Electric Field 35, Discharge 36, Plasma 37, Energy Shield 38, Force Field 39, Charge-Up 40, Heat Shimmer 41, Gravitational Lens 42, Light Beam 43, Halo 44, Bubble 45, Portal 46, Reality Tear 47 |

Kind numbers are explicit and append-only (`src/world/effects/effect_kind.hpp`); the next free kind is 48.
Every primitive is byte-identical with no instance, proven against the pre-change binary.

### What is left

| Wave | Primitives | Effects |
|---|---|---|
| **4** | FXPOST (H1 before bloom, H2 display-referred), REDRAW | Radial Distortion, Scanlines, Pixelation, Dithering, Toon Edges, Chromatic Aberration (glitch + spectral lens), God Rays (Screen), Heat Haze (and Heat Shimmer's Ground Haze), Aura, Afterimage (Geometry), Hologram, Energy Shield (Contour) |
| **5** | TEMPORAL (history ids, stride, warm-up), MEDIUM shadowed march, LOCALTIME | Echo / Smear / Ghosting (`temporalFilter`, FrameEcho migrated), Freeze-Frame, Reverse, Time-Warp Distortion, Afterimage (Image), Time Dilation, Delayed Motion, God Rays (Volumetric), Volumetric Beam (occluded) |
| **6** | migrations (ADR-441, no shims) and heavy geometry | Wind Response (ADR-360 lanes → effect), Sway, tree energy → Pulsing Veins, Camera Shake → Shake, Tendrils (instanced tube), Portal Remote View |

Also outstanding, from the waves that shipped:
- **SIGNALS: effect-published signals** (`fx.<id>.age/phase/charge/release`). Charge-Up's `chargeUpCharge()` is the pure function a publisher would call.
- **LIGHTMOD's modulate half.** Pulse on a Light owner, and a spot-shaped Light Beam pool light.
- **REDRAW** (Wave 4). It unblocks Energy Shield's Contour mode and Aura (Hull).
- **XFORM on Light and Camera owners**, and Orbit about another entity.
- **A panel editor for triggers.** Activation `trigger` has no dedicated UI yet.

### Look notes and gaps the owner has not ruled on

Each is recorded in its wave's ADR ("Findings not fixed here"):
- **Wave 3 (ADR-719):**
  - Light Beam reads as haze, not a crisp beam;
  - the blue Plasma close-up has subtle strands;
  - Heat Shimmer's Campfire is subtle;
  - Gravitational Lens's Einstein ring is faint on a smooth sky, and its horizon darkens fog in front of it;
  - Reality Tear's Crystal Crack shear barely shows;
  - Discharge sparks are ribbon streaks, not EMIT;
  - Electric Field's arcs sit on the owner's bounds, not its mesh;
  - bolt LOD depends on the camera.
- **Wave 2 (ADR-716):**
  - the alpha-mask `discard` in `pbr_shade` precedes texture samples. It is defined by the WGSL spec and has never been seen to fail, so it is recorded, not moved.
- **Resolved by the owner:**
  - Energy Blast and Explosion thinned to filaments;
  - a dissolving owner's inside shaded;
  - the Tree of Life's `state.progress` routes are the approved look (ADR-712).
- **Seek:** the UFO-stack film's play = scrub case is still hidden (`[.known-defect][adr700]`). ADR-870 narrowed it to about 0.018 m, not exact. Un-hide it when it holds to the bit.

### Where to pick up

1. Read ADR-702 → 703 → 716 → 719 in order. Then read `shared-infrastructure.md`'s FXPOST and REDRAW sections for Wave 4.
2. Run each wave the same way:
   - reserve kind numbers per slice in `effect_kind.hpp`;
   - give each slice a file set it owns;
   - add a new bucket's arms to the registry stage map, `isAtmosphericBucket`'s callers, and the conformance maps;
   - require a byte-identical gate against the pre-change binary;
   - render and look at on/off sheets of every type in a real scene (the Glowmere film, the UFO stack), not only a fixture.
3. The review sheets for Waves 1–3 are in `~/Desktop/av-gen-review/5-effect-library-wave1`, `10-effect-library-wave2` and `16-effect-library-wave3`.

## Wave 1: the concrete plan

Wave 1 is ten work packages. Each fits in one agent worktree and one branch
(`agent/fx-w1-<n>`). The merge order is **1.1 → 1.2 → 1.3 → (1.4, 1.5, 1.6 in any order) → 1.7 →
1.8 → 1.9 → 1.10**. Each package's acceptance test is its merge gate. Report **only the exit code**
of `avgen_tests` *and* of `avgen_render_tests`. GPU tests live in the second binary. A tag filter on
the first can pass having run no GPU case. See `docs/testing.md` ("Twelve ways a green suite has
lied").

### 1.1 Registry metadata and status detail

- **`src/world/effects/effect_registry.hpp`**
  - Add these to `EffectSchema`:
    - `description`
    - `requires` (bitmask)
    - `state`
    - `seekExactness`
    - `performanceClass` and `primaryCost`
    - `publishes`
    - `ownerSignals`
  - Extend `EffectBucket` with `EntityLanes`, `Distortion`, `Ribbon`, `Emitter` and `LightMod`.
    These are the Wave 1 buckets only; later buckets arrive with their wave.
  - Its exhaustive switch in `resolveAtmosphericEffects` gains arms that route to the new builders.
- **`src/world/effects/effect_registry.cpp`**
  - `checkRegistry` gains named rules:
    - every kind has a description and a performance class;
    - a `requires OwnerMesh` kind must not target Light or World;
    - an `owner.` route requires a non-World target.
- **`src/world/effects/effect_instance.hpp`**
  - Add `EffectStatus::Partial`.
- **`src/app/engine.hpp` / `engine.cpp`**
  - Add `effectStatusReason_` (a parallel vector of strings) and `effectStatusReason(id)`.
  - The drop log at `engine.cpp:4062-4071` lists every bucket's capacity.
- **Effects panel.** On this branch **no UI code reads `Engine::effectStatus`, `effectKindsFor` or
  `Engine::editEffects`.** A grep of `src/` finds callers only in `engine.cpp` and in
  `tests/rendering/test_effect_stack_gpu.cpp`. So the panel that `effect_instance.hpp:111-113` says
  reads the status is either unmerged or absent. Wave 1 must wire the status and reason into
  whatever panel lands. Otherwise `Dropped` repeats ADR-562's "a counter nobody reads".
- **Tests:** extend `tests/unit/test_effect_conformance.cpp` with the new rules (each must fail by
  name on a deliberately broken schema) and `test_effect_stack*.cpp` with the `Partial` status.

### 1.2 Entity-derived signals and the `owner.` alias

- **`src/app/engine.cpp`**
  - New `publishEntitySignals()`, called **before** routes run, from the last completed step's state.
    - For subscribed entities, declare and set `entity.<name>.{speed, velocity.x/y/z, acceleration,
      cameraDistance, screenSize, onScreen}`.
    - Also declare and set `camera.speed` and `camera.velocity.*`, from the existing
      `cameraVelocityOnTimeline()` at `engine.cpp:3846`.
  - The subscription set is recomputed in `installEffects()` (`engine.cpp:552`).
- **`src/world/effects/effect_params.hpp` / `.cpp`**
  - `defaultEffectRoutes(const EffectInstance&)` resolves `owner.` to the owner's signal prefix.
  - A World owner refuses the route by name.
- **`src/params/modulation.hpp`**
  - Add `bool fromEffectDefaults`, next to `fromGraph`, `fromEntity` and `fromMacro`.
  - The project reload keeps these routes, following the `fromEntity` path at
    `composition.cpp:1680-1690`.
- **`renameEffectOwner`** (`effect_stack.hpp:96`) triggers regeneration of the resolved routes.
- **Tests (new): `tests/unit/test_entity_signals.cpp`**
  - A scripted body's `entity.x.speed` must be equal at t = 37.5 s in play vs scrub (the ADR-700
    harness in `test_seek_checkpoints.cpp`).
  - `owner.speed` must resolve after a rename.
  - A World-owned route must be refused.
  - **Reconfigure CMake** after adding test files. An incremental build silently omits them (see "stale
    test binary").

### 1.3 Scene query growth and the HistoryBank (HIST)

- **`src/world/effects/effect_timing.hpp`**
  - `EffectSceneQuery` gains `nodeMatrix`, `nodeBounds`, `nodeVelocity` and `nodeHistory(span)`.
  - Each has a default `return false`, the same contract as `nodeForward`.
- **`src/app/engine.cpp`**
  - `CompositionEffectScene` (`engine.cpp:3801`) implements them.
- **New `src/world/effects/history_bank.hpp` / `.cpp`**
  - One ring per subscribed node, holding 40 B samples on the step grid.
  - `subscribe(node, seconds)`, `record(stepInstant)`, and `sample(node, t)` (interpolated).
- **Recording point**
  - Record where `Composition::ReplayPlacement::capture()` records each step's flattening. That point
    is `src/scene/composition.hpp:1874-1900`, and ADR-700 already checkpoints it.
  - The bank is added to the same checkpoint `capture`/`restore` hooks, so a scrub restores it.
- **Tests (new): `tests/unit/test_history_bank.cpp`**
  - A body on `glowmere-valley-2-multicam`'s `visitor` path, sampled at t = 150 s, must match between
    a full play and a scrub. That is the late-time arm where the old 90 s window failed.

### 1.4 FXL: object effect lanes (emission, rim and pulse sub-blocks only)

- **`src/rendering/scene_renderer.hpp`**
  - `ObjectUniforms` gains `fxA` and `fxB` inside the existing 96 B of padding.
  - `sizeof` becomes 448 and must stay ≤ `kObjectStride` 512. Update the static_asserts.
- **`shaders/common.wgsl`**
  - Mirror the struct.
  - Add `struct EntityFx { lanes: array<vec4<f32>,16> }`.
  - Bind it at `@group(1) @binding(1) var<storage, read> entityFx: array<EntityFx>`.
- **Bind group layouts that must match**
  - The object layout in `SceneRenderer::init()` (`scene_renderer.cpp:212-380`).
  - The skinned object layout in `skinning.cpp`.
  - SDF and procedural use their own group 1, so they are unaffected. Document it.
- **`shaders/pbr_shade.wgsl`**
  - After emissive is computed (around :312), when `fxFlags != 0`:
    - apply gain and tint;
    - apply the rim sub-block;
    - apply the pulse travelling band.
  - Write both `result.color` and `result.emission`, and raise `bloomWeight` to the override.
- **New `src/world/effects/entity_fx.hpp` / `.cpp`**
  - The `EntityFxFrame` type: records, plus a map from entity index to record index.
  - `buildEntityFxFrame`, which folds lane contributions using the rules in rendering-architecture
    §7.
- **`src/scene/scene.hpp`** gains `world::EntityFxFrame entityFx`.
- **The renderer**
  - Uploads records to a growable storage buffer, using the pattern in `ensureObjectCapacity`
    (`scene_renderer.cpp:2256`).
  - Writes `fxA` and `fxB` at object-uniform fill (`scene_renderer.cpp:3038-3100`).
- **Tests**
  - Extend `tests/unit/test_renderer_layout_guards.cpp`. The CPU/WGSL layout guard must scrape the
    new fields.
  - New `tests/rendering/test_entity_fx_gpu.cpp`:
    - one entity with gain 4 has emission-target luminance 4× the control, within tolerance;
    - `fxFlags = 0` is **byte-identical** to a build without FXL (ADR-372's proof style).

### 1.5 LIGHTMOD: the effect light pool (unshadowed only)

- **New `src/world/effects/effect_lights.hpp` / `.cpp`**
  - `buildLightingFrame` produces `scene.effectLights`: ranked, capped at `kEffectLightBudget = 16`.
  - Losers are marked `Dropped` or `Partial` with a reason.
- **`src/scene/composition.cpp`**
  - The ecology light cap (`kMaxEcologyLights` at :94; the allocation at :7671-7735) subtracts the
    effect budget.
- **`SceneRenderer::updateLights`** (`scene_renderer.cpp:2146`)
  - Appends `effectLights` after authored and ecology lights, before the froxel build.
- **Tests**
  - A scene with 224 ecology glows plus 3 Glow spills gets all 3 spills lit, and ecology loses 16.
  - 17 spills give one `Dropped` with a reason.

### 1.6 RIBBON

- **New files**
  - `src/rendering/ribbon_renderer.hpp` / `.cpp`
  - `shaders/ribbon.wgsl`
  - `src/world/effects/ribbon_frame.hpp`: points with age, width, colour, profile and blend.
- **The pipeline**
  - Declares all five pass-1 attachments (HDR, normal, velocity, emission, ids). Follow
    `particle_renderer.cpp:220-240`.
  - Additive and alpha variants.
  - Depth test on, depth write off.
  - Vertex-stage camera-facing expansion with a minimum pixel width.
- **Draw point:** in pass 1 beside `particles_->draw` (`scene_renderer.cpp:3792`).
- **Arena:** a per-frame vertex arena of 64k vertices, filled with `queue.WriteBuffer`.
- **Tests**
  - GPU: a straight ribbon at a known depth is occluded by a wall in front of it.
  - GPU: it writes emission.

### 1.7 DF: the distortion framework

- **New files**
  - `src/rendering/distortion_renderer.hpp` / `.cpp`
  - `shaders/distortion.wgsl`
- **Entry points**
  - `vs_proxy` / `fs_offset`: the ellipsoid proxy, closest-approach analytic.
  - `fs_resolve`: depth-aware, with self-exclusion by centre depth, three-tap chroma and an emission
    MRT.
- **Resources**
  - A transient `sceneCopy` from the existing transient pool (`src/gpu/transient_pool.hpp`).
  - The offset and aux targets, RGBA16F.
- **`scene_renderer.cpp`**
  - Insert after the debug pass (around :3838), before post layers.
  - Gate: `scene.distortion.count == 0` skips the whole block.
- **New `src/world/effects/distortion_frame.hpp`** and `buildDistortionFrame`.
- **Tests (GPU)**
  - A checker plane behind a warp is displaced.
  - A cube in front of the warp is **not** displaced (Sousa mask).
  - The owner itself stays crisp (self-exclusion).
  - No producer gives byte-identical output.

### 1.8 EMIT: the particle emitter type

- **New `src/world/effects/kinds/particle_emitter_effect.cpp`**
  - About 25 stored rows, with the full `ParticleSystem` in `writeExtra`/`readExtra`.
  - Styles: Fireflies, Magic Particles and Embers.
- **`buildParticleFrame`**
  - Appends `fx:<id>` systems to `scene.particles`, with a stable name so GPU pools persist.
  - Sets position, extent and attractor from the owner.
  - Rotates `direction` by the owner's rotation. This is new in `particles.cpp`/`particle_renderer.cpp`.
- **The particle registrar must skip `fx:` systems**, so there is one authority per parameter.
- **Tests**
  - CPU: `fx/<id>/spawnRate` modulates the system.
  - CPU: no `particles/fx:*` path is registered.
  - GPU: an emitter on a moving entity follows it.

### 1.9 The Wave 1 kinds

These go in `src/world/effects/kinds/`, one file each, following the four-line recipe at
`effect_registry.hpp:44-66`:

| File | Owners | Mechanisms |
|---|---|---|
| `glow_effect.cpp` | Entity | FXL + LIGHTMOD spill |
| `pulse_effect.cpp` | Entity, Light | FXL scalar/travelling; LIGHTMOD multiply on a Light owner |
| `bloom_source_effect.cpp` | Entity | FXL bloom override |
| `trail_effect.cpp` | Entity, Light | HIST → RIBBON; styles include Light Trail |
| `space_warp_effect.cpp` | Entity, World | DF; default route `owner.speed → strength`; styles UFO, Gravitational, Magical and Portal Warp |
| `particle_emitter_effect.cpp` | from 1.8 | |

Registration:

- Append the enumerators to `EffectKind` in `effect_kind.hpp`. The list is **append-only**, because
  the numeric values reach `volume.wgsl`.
- Add them to `kEffectKinds` (size 8 → 14).
- Add declarations and references in `effect_registry.cpp`'s `builtinSchemas()`.

**Tests:** `avgen_tests "[registry],[conformance]"` names anything missing. Extend
`tests/rendering/test_effect_stack_gpu.cpp`, which already proves "several effects render together".

### 1.10 The UFO stacking demo, and the Wave 1 exit criterion

- **New `examples/effects/ufo-stack.json`**
  - Based on `examples/world/glowmere-valley-2-multicam.json`.
  - The `visitor` (saucer) entity carries `visitor-space-warp` (UFO Warp), `visitor-glow` (Neon, spill
    on) and `visitor-trail` (UFO Wake, Blade mode, anchors at the rim).
  - A Fireflies emitter is World-owned in the valley.
  - Routes: `owner.speed → strength` and `owner.speed → opacity`, both from the defaults, and
    `beat.pulse → visitor-glow/gain`.
- **New `tests/rendering/test_ufo_stack_gpu.cpp`**
  - All four instances report `Drawn`.
  - A frame at t = 150 s rendered after a scrub is identical to the same frame from a full play,
    within the renderer's existing float tolerance. This proves the HIST, velocity and signal
    determinism chain end to end.
- **Wave 1 is done when:**
  - the demo renders;
  - both test binaries exit 0;
  - `effectStatus` is honest under a forced over-capacity scene;
  - one screenshot at rest and one mid-dash have been reviewed by the owner. The UI cannot be
    verified by the implementer; see "visual bug reports".

### Wave 1 risks worth naming now

- **The ADR-135 layout claim.** `common.wgsl` says ObjectUniforms has "no free lane", which is true
  of the *declared* struct, and that "Phase E owns" the object-layout work. Package 1.4 uses the
  96 B of padding. Coordinate with whoever owns Phase E before merging, so the two layout changes do
  not collide.
- **Ecology light budget.** Reserving 16 lights reduces Glowmere's ecology lights by 16 in the
  densest views. That needs a before/after look by the owner. Bioluminescence is the look.
- **Scene-colour copy bandwidth on Metal.** The copy runs only on frames with a producer, and is
  scissored. Measure it with the existing per-pass timestamps (`volume.march` style,
  `FrameTimeline`) before claiming a cost. No numbers are asserted here.
