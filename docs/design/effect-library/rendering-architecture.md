# Rendering Architecture: how the new stages plug in

This page belongs to the [Effect Library](README.md). It is a **proposal, not code**. It extends
ADR-702's one-evaluator design to the five render stages that the registry declares but nothing
implements yet: `Geometry`, `Lighting`, `Particles`, `ScreenSpace` and `PostProcess`, which are
marked "none yet" at `effect_registry.hpp:413-422`.

## 1. Today

- **`Engine::updateEffects`** (`src/app/engine.cpp:4011-4075`) is the one evaluator. Each frame it:
  - applies parameters (`applyEffectParameters`);
  - publishes fields;
  - builds one `EffectContext` (transport seconds, camera, camera velocity on the timeline, shots,
    heroes, scene query, spectrum, field bus);
  - calls two builders:
    - `buildWaveFrame` → `scene.waves` (Material, bucket `Surface`, ≤ `kMaxGpuWaves = 8`);
    - `buildAtmosphericFrame` → `scene.atmospherics` (Sky: Comet ≤ 6, Aurora ≤ 2; Volumetric:
      Medium ≤ 4).
- Both builders walk `effectOrder_`. That order is a stable sort by (stage, priority, list position),
  computed when the list changes (`effect_stack.hpp:109-113`). Each builder writes `effectStatus_`
  for the instances it owns.
- Orphan detection and the `Dropped` log follow (`engine.cpp:4047-4071`).
- **The renderer never evaluates effects.** It reads `scene.waves` and `scene.atmospherics`.
- **`EffectSceneQuery`** can answer only `nodePosition` and `nodeForward`
  (`effect_timing.hpp:123-131`). It has no velocity, no matrix, no bounds and no mesh.
- **Frame order** in `Engine::update` is:
  1. modulation routes;
  2. `controller_->update` (composition → scene), at `engine.cpp:4303`;
  3. camera, post and temporal parameters;
  4. `updateEffects`, at `engine.cpp:4355`.

  So every effect is evaluated *after* the scene's entity transforms have been built.

## 2. Target shape

```
Engine::update
 ├─ routes / modulation                          (existing)
 ├─ updateEffects(Phase::BeforeScene)            NEW — Geometry stage only
 │     buildGeometryFrame → EffectGeometryFrame  (XFORM offsets per owner node, HIST subscriptions,
 │                                                LOCALTIME rates) consumed by the Composition
 ├─ controller_->update (composition → scene)    (existing; now composes XFORM offsets, clocks)
 ├─ camera / post / temporal parameters          (existing)
 └─ updateEffects(Phase::AfterScene)             (existing entry, more builders)
       Material     buildWaveFrame          → scene.waves          (existing)
                    buildEntityFxFrame      → scene.entityFx       NEW (FXL records, reroutes, REDRAW list)
       Lighting     buildLightingFrame      → scene.lights (mods) + scene.effectLights  NEW (LIGHTMOD)
       Sky          buildAtmosphericFrame   → scene.atmospherics   (existing) + stars block NEW
       Volumetric   buildAtmosphericFrame   (existing media slots)
       Particles    buildParticleFrame      → scene.particles (+ fx:<id> systems)       NEW (EMIT)
       (drawn geometry) buildShellFrame / buildRibbonFrame → scene.shells, scene.ribbons NEW
       ScreenSpace  buildDistortionFrame    → scene.distortion     NEW (DF)
                    buildTemporalFilterFrame→ scene.temporal (filters)                  NEW (TEMPORAL)
       PostProcess  buildPostFxFrame        → scene.postFx {H1, H2 pass lists}          NEW (FXPOST)
```

One evaluator, two call sites. `effectOrder_` is already sorted by stage, and Geometry is stage 0. So
`Phase::BeforeScene` walks the Geometry prefix of `effectOrder_`, and `Phase::AfterScene` walks the
rest. No instance is evaluated twice, and there is still exactly one `EffectContext` builder, which
is called twice with the phase set.

The renderer gains **readers**, not evaluators. Each new frame block is plain data, the same way
`AtmosphericFrame` is: GPU-ready arrays, counts, and a `dropped` count.

## 3. Why the Geometry stage must run before the scene is built

An Orbit, a Float or a Shake must move the owner's *children and attachments*. Examples are a
lantern hanging from a floating island, an emitter parented to a UFO, or a pool light that is a
child node.

It must also move `prevModel`, which the renderer captures from `scene.entities` at
`scene_renderer.cpp:2833-2835`.

Patching `scene.entities` after the flatten would move only the flattened entity. Its children would
stay behind, and every attachment would need a second, effect-aware traversal. That traversal is the
"two hooks, one question" shape that ADR-580 §68 removed.

So the Geometry builder runs first and hands the Composition a per-node offset. The Composition
composes the offset into the node's local transform during its ordinary traversal.

- **Inputs the builder may read before the scene exists:**
  - this frame's parameter finals, since routes have already run;
  - the last completed simulation step's entity state (position, velocity, HIST);
  - pure functions of t.
- **Why this is deterministic:** those inputs are all exact under seek. HIST is checkpointed, and
  velocity is rebuilt by the replay.
- **What it is not allowed to do:** read the *rendered* transform of the current frame.

**Entity-derived signals** (`entity.<name>.speed` and so on) are published at the same point, before
routes, from the last completed step. That gives one step of latency (1/60 s), which is the same in
play and in scrub. This is the only place such a signal could be published without making routes
depend on the frame they modulate.

## 4. Where each block enters the frame

Pass order in `SceneRenderer::render` (`src/rendering/scene_renderer.cpp`):

| Existing pass (line) | New work inserted | Primitive |
|---|---|---|
| object uniforms (2808) | write `fxA/fxB` lanes; upload `entityFx` records; apply reroutes (Hologram → blended) | FXL |
| lights, shadows, froxels (2700, 3410) | `scene.effectLights` appended before `updateLights`; the effect light budget is reserved from the ecology cap | LIGHTMOD |
| particle compute (3319) | `fx:<id>` systems simulate like any other system; per-system dt scale | EMIT |
| shadow depth passes (3410) | `fs_depth` evaluates FXL clip and displacement | FXL |
| depth prepass (3517) | the same | FXL |
| pass 1 opaque (3608) | the lit shader evaluates FXL | FXL |
| pass 1 blended section (≈3790, beside particles) | RIBBON draws, SHELL draws, REDRAW draws | RIBBON / SHELL / REDRAW |
| volumetric march (3803) | shadow atlas bound in the march | MEDIUM |
| **after debug (≈3838), before post layers** | DF offset pass → HDR copy → resolve | DF |
| temporal (3877) | `temporalFilter` kernels; history-id capture; warm-up tap sets | TEMPORAL |
| built-in post chain (3901) | **H1**: Camera `PostProcess` passes before bloom | FXPOST |
| after tonemap (3949), before 2D composition (4137) | **H2**: display-referred Camera passes | FXPOST |

**Attachment compatibility.** Pass 1 writes five colour attachments: HDR, normal, velocity, emission
and ids (`kSceneTargetCount`). Every pipeline drawn inside it (RIBBON, SHELL, REDRAW, Hologram) must
declare all five targets with write masks. Particles already solve this: they write velocity and
emission, and write 0 to ids. The new pipelines follow the particle renderer's pattern
(`particle_renderer.cpp:220-240`).

**WebGPU constraints honoured** [R22]:

- A texture cannot be sampled while it is an attachment of the same pass. That is why DF copies HDR
  rather than reading it in place, and why SHELL reads the *separate* linear-depth target rather
  than the depth attachment.
- There are no geometry shaders. RIBBON expands on the CPU plus the vertex stage, and SHELL uses
  instanced canonical meshes.
- The device requests the adapter's full limits (`src/gpu/context.cpp:157-180`). Designs still stay
  inside four bind groups, following the convention 0 frame, 1 object, 2 material, 3 IBL. `entityFx`
  goes in group 1, so no fifth group is needed.

## 5. Resource isolation, capacity, and honest drops

| Primitive | Isolation model | Capacity (proposed) | On overflow |
|---|---|---|---|
| Surface waves | fixed uniform array | 8 (exists) | `Dropped` (exists) |
| Comet / Aurora / Medium | fixed slots | 6 / 2 / 4 (exist) | `Dropped` (exists) |
| FXL | batched storage buffer, one record per *affected entity*, grows like the object buffer | budgeted in MiB, like `kObjectBufferBudgetMiB` | instances over budget, and exclusive-sub-block conflicts: `Dropped` with a reason |
| DF | batched proxy storage buffer, one draw per proxy kind | 64 proxies | lowest priority `Dropped` |
| SHELL | batched instance buffer per shading kind | 128 | `Dropped` |
| RIBBON | per-frame vertex arena | 64k vertices | LOD first, then `Dropped` |
| REDRAW | draw list | 64 draws | `Dropped` |
| LIGHTMOD pool | reserved light slots | 16 (taken from the ecology cap) | lowest rank `Dropped` |
| Shadowed pool lights | shadow views | shares `kMaxShadowViews = 8` | falls back to unshadowed, `Partial` |
| EMIT | one particle system per instance (the existing pool) | particle-system budget; volume glow limited to 8 | `Dropped` / `Partial` |
| FXPOST | pass list | 8 per hook | `Dropped` |
| TEMPORAL | one ring, shared taps | 32 frames | kernels clamp taps; `Partial` while settling |

Two additions to the status model:

1. **`EffectStatus::Partial`.** A multi-stage type can lose one sub-record while the rest draws. For
   example, a Shockwave front draws but its flash light is dropped because the light budget is full.
   Today the only choice would be `Drawn` (a lie) or `Dropped` (also a lie).
2. **A per-instance reason string**, written by the builder that decided. An example: *"flash light:
   effect light budget (16) full; 3 higher-priority requests"*.
   - The panel shows it.
   - The existing edge-triggered log (`engine.cpp:4062-4071`) lists every primitive's capacity, not
     just the four it names today.
   - This keeps faith with ADR-562's lesson that a counter nobody reads is not a report.

## 6. Registry metadata extensions (proposal)

This is what `EffectSchema` (`effect_registry.hpp:492-559`) should gain. Every field listed has at
least one consumer, which is the repository's standing rule; see "subsystems with no users".

| Field | Type (sketch) | Consumer |
|---|---|---|
| `description` | `const char*` (one paragraph) | the panel's help, the AI tool catalogue |
| `buckets` | small set of `EffectBucket` (extended: `EntityLanes, TransformOffset, Distortion, Shell, Ribbon, Redraw, Emitter, LightMod, PostPass, TemporalFilter, StarField`) | the builders (who owns what); `checkRegistry` (a kind that fills a bucket it did not declare is a named failure, as today) |
| `secondaryStages` | stage mask | `effectEvaluationOrder` for multi-stage types |
| `requires` | resource bitmask: `SceneColorCopy, LinearDepth, Normals, Ids, Velocity, EnvironmentCube, TemporalRing, HistoryIds, MarchShadows, OwnerMesh, OwnerSkinning, EntityHistory, LocalClock` | renderer gating (allocate the DF copy only when some live type requires it); panel warnings ("needs the depth prepass, which is off"); conformance (a type requiring `OwnerMesh` refused on a Light owner) |
| `state` | enum `Stateless, TriggerAge, TransformHistory, LocalClock, ImageHistory, ParticleState` | the seek-exactness column below; HIST / LOCALTIME subscription |
| `seekExactness` | enum `Exact, ExactAfterWarmup, Relaxed, LiveTierOnly` | the panel badge; `test_seek_checkpoints` parameterised over every `Exact` type |
| `performanceClass` | enum `VeryLow … VeryHigh` + `primaryCost` bitmask (`CPU, Vertex, Fragment, Compute, Bandwidth, Memory, ExtraPass`) | quality tiers (a `High` type is disabled on the lowest tier by rule, not by hand); the panel |
| `modulation` | list of leaves that are *recommended* route targets, plus `publishes` (signal names this type emits, e.g. `fx.<id>.charge`) | the route editor's suggestions; the bus declares published signals at install |
| `ownerSignals` | which `owner.*` signals the default routes use | `defaultEffectRoutes` resolves `owner.` to the actual owner (see parameters doc) |
| `triggerSources` | which `Activation::Trigger` sources make sense | the panel's trigger combo |
| `targets` (exists) | – | keep; the matrix in [target-capability-matrix.md](target-capability-matrix.md) is the source of truth for each type's mask |

`EffectSceneQuery` must grow in the same change, because Entity-owned effects need more than a
position. Its new methods are `nodeMatrix`, `nodeBounds`, `nodeVelocity`, `nodeHistory` (a HIST
span), `nodeClock` (τ), and `nodeMesh` (for REDRAW and surface sampling). Every one is optional, and
a scene that cannot answer returns false. That is the same contract `nodeForward` already has.

## 7. Stacking semantics within an owner

The evaluation order is ADR-702's: stage first, then priority, then stack position. Composition
*within* a primitive is fixed per primitive, so the result never depends on the order in which two
additive things were listed:

| Primitive | Rule |
|---|---|
| FXL emission gains | multiply |
| FXL added emission / rim | sum |
| FXL clip | max |
| FXL displacement | sum per mode |
| XFORM | composed in stack order; rotations are order-sensitive and that is the author's choice |
| LIGHTMOD on a light | multiply |
| DF offsets | sum; remaps last |
| FXPOST passes | stack order, which is a real image-order decision |
| TEMPORAL kernels | stack order |
| SHELL / RIBBON / REDRAW | additive blending is order-independent; alpha shells sort back-to-front by centre depth |

## 8. Worked example: the UFO stack (Space Warp + Glow + Trail)

One UFO entity with three instances, top to bottom:

1. `ufo-space-warp`
2. `ufo-glow`
3. `ufo-trail`

**Evaluation order:**

| Stage | Instance | Work |
|---|---|---|
| Material | Glow | an FXL record: gain, tint, rim |
| Lighting | Glow | a pool spill light |
| Particles | – | none |
| ScreenSpace | Space Warp | an ellipsoid DF proxy, stretched along `owner.velocity` |
| (drawn geometry) | Trail | a RIBBON from HIST |

**Frame:**

1. The UFO draws in pass 1 with its glow lanes. That writes HDR and emission.
2. The spill light lights the ground through the clusters.
3. The trail ribbon draws additively in the blended section.
4. The volume march runs.
5. DF writes the warp's offsets. The owner's centre depth self-excludes the UFO's front surface. The
   copy and resolve then bend the ground, the fog and the trail behind the UFO.
6. Post blooms the glow, the trail and the warp's rim.
7. Routes on the warp: `owner.speed → fx/ufo-space-warp/strength` and `owner.speed →
   fx/ufo-trail/opacity`. Hovering is calm. Dashing warps and streaks.

**Scrub.** HIST comes from the checkpoint, and velocity comes from the replay. The warp and the trail
at second 150 are identical whether played or scrubbed.
