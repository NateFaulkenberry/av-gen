# ADR-703: Effect Library Wave 1 — owners that move, glow, leave trails and bend space

**Status:** Accepted
**Date:** 2026-09-24
**Builds on:** ADR-702 (effects are instances attached to owners). **Design:** `docs/design/effect-library/`
(catalog, shared infrastructure, rendering architecture, roadmap, performance risks).
**Implemented by:**
- `src/world/effects/{entity_fx,effect_lights,distortion_frame,history_bank,ribbon_frame,particle_emitter}.*`
- `kinds/{glow,pulse,bloom_source,trail,space_warp,particle_emitter}_effect.cpp`
- `src/rendering/{distortion_renderer,ribbon_renderer}.*`
- `shaders/{distortion,ribbon}.wgsl`, plus the FXL lanes in `pbr_shade.wgsl` and `procedural.wgsl`
- `Engine::updateEffects` and the entity-signal publishing
**Tests:**
- CPU: `[fxl]`, `[distortion]`, `[hist]`, `[signals]`, `[trail]`, `[emit]`, `[demo]`, and the conformance and registry suites
- GPU: `[effects]`, `[fxl]`, `[distortion]`, `[ribbon]`, `[emit]`
- `tests/integration/test_ufo_stack_demo.cpp` (the stacking demo and the exit criterion)

---

## Context

ADR-702 made effects instances attached to owners. Every type it shipped was a technique the engine
already had. The Effect Library research (73 effects after merging duplicates, 16 shared
primitives, six waves; see `roadmap.md`) found that the ownership model would only be *proven* by
types that need something from their owner that no World effect needs:
- its **velocity** (Space Warp);
- its **recorded path** (Trail);
- its **own surfaces** (Glow);
- a **particle system riding it** (Particle Emitter);
- and all of them **stacked on one owner** through different integrators.

Wave 1 builds those primitives with at least one effect each, so no primitive lands without a user.

## Decision

### The primitives

| Tag | What | Where it draws | Resource isolation |
|---|---|---|---|
| **SIGNALS** | `entity.<name>.{speed, velocity.xyz, acceleration, cameraDistance}` and `camera.speed`, published before routes run, from the last completed step, for subscribed owners only. The `owner.` alias in a type's default routes resolves to its owner's prefix when the routes attach. A default the owner cannot answer (a World owner has no speed) is left out; the rest attach. | — | — |
| **HIST** | `HistoryBank`: a ring per subscribed owner of the drawn transform on the step grid. Recorded after each played frame and at every seek-replay step, and carried in both ADR-700 checkpoint types. `EffectSceneQuery::nodeVelocity` is a backward difference over it. | — | one ring per owner, depth = max subscriber persistence |
| **FXL** | Per-entity material lanes. `fxA`/`fxB` sit in the object-uniform padding. A 256 B record in a storage buffer holds gain/tint, rim, pulse/travelling band and a bloom override. One record per owner, folded by fixed rules (gains multiply, rims sum). Reaches mesh entities *and* procedural nodes (the saucer is procedural). | lit pass, before fog | budget in bytes; exclusive sub-block conflicts `Dropped` with a reason |
| **LIGHTMOD** (pool half) | Up to 16 transient unshadowed lights, taken from the ecology cap (224 → 208), appended before `updateLights`. | clustered lighting | a loser is `Partial` (the glow still draws, the spill did not fit) |
| **RIBBON** | Camera-facing strips built on the CPU from point lists (Catmull-Rom). A per-frame vertex arena; minimum pixel width; core and glow profiles; additive or alpha. Declares all five pass-1 attachments. | pass 1's blended section, beside particles | 64k-vertex arena: level-of-detail first, then `Dropped` |
| **DF** | The reusable distortion framework: proxies write screen offsets (world units → screen); a scissored scene-colour copy only on frames with a producer; a depth-aware resolve with a foreground-leak mask, self-exclusion by the owner's lens plane, a three-tap chroma spread and a rim into both HDR and the emission target. | after the volumetric composite, before post | 64 proxies, lowest priority `Dropped` |
| **EMIT** | One `particleEmitter` type over the existing particle system (a `look` picks a base configuration, the rows adjust it). One `fx:<id>` system per instance, updated in place, riding its owner. | the particle system | 16 systems, `Dropped` with a reason |

The scaffold that let three branches build these in parallel:
- `EffectBucket` gained `EntityLanes`, `Ribbon`, `Distortion` and `Emitter`. `isAtmosphericBucket`
  replaced the `== Surface` special cases.
- `resolve.records` lets the conformance probe ask any builder whether a live instance resolves.
- `EffectSceneQuery::nodeView` gives an owner's drawn matrix, bounds, and its entity and
  procedural ranges.
- `EffectStatus::Partial`, and a per-instance reason string shown under the card's badge.
- `description` / `performance` / `primaryCost` on every schema, all of them required by `checkRegistry`.
- Kind numbers were reserved explicitly (`Glow = 8` … `ParticleEmitter = 13`), so a type's
  serialised value never depends on merge order.

### The types

| Type | Owners | Presets | Default routes |
|---|---|---|---|
| Glow | Entity | Soft, Neon, Bioluminescent, Hot Core, Radioactive | `beat.pulse → gain` |
| Pulse | Entity | heartbeat / sine / square, travelling band | beat-synced |
| Bloom Source | Entity | — | — |
| Trail (Light Trail is a style) | Entity | UFO Wake, Light Trail, … | `owner.speed → opacity`, `audio.treble → intensity` |
| Space Warp | Entity, World | UFO, Gravitational, Magical, Portal Warp | `owner.speed → strength` (0.03 per m/s), `beat.pulse → strength` |
| Particle Emitter | Entity, World | Meadow Fireflies, Synchronous Swarm, Campfire Embers, Forge Sparks, Fairy Dust, Arcane Swirl | `audio.rms → rate`, `audio.onset → burst` |

### Why these choices (the notes worth keeping)

- **FXL is a storage-buffer index, not all-inline lanes.** Six vec4s of padding cannot hold rim,
  pattern, clip, displace and hue. Raising the object stride to 768 would cut object capacity by a
  third for every entity, including those with no effect. This uses two of the four padding vec4s
  ADR-135 left for its "Phase E"; the note in `common.wgsl` says so, and the Phase E owner must know.
- **DF's lens plane is the owner's centre plus its bounding radius**, not the centre. A flat
  saucer's far half lies behind its centre, and the first version bent the saucer's own back rim.
- **DF clamps taps at the screen edge rather than reading the IBL for sky lensing.** Glowmere's
  visible sky is a shader layer, and IBL taps would not match it.
- **A refused DF tap bisects back** to the furthest acceptable point. A fixed half-step drew a
  staircase along foreground edges.
- **Particle Emitter owns no parameters of the particle system.** Its numbers are `fx/<id>/…` only.
  The composition registers particle parameters from nodes, so no `particles/fx:*` path exists
  (tested). One authority per number.
- **Stored rows no longer allocate per access.** `storeKey` built a string for every stored row of
  every instance on every frame, which is a dozen allocations per fog bank. A per-thread reused
  buffer removes them.

## Evidence

### The UFO stack demo

`examples/effects/ufo-stack.json` is the Glowmere film with the saucer (`visitor`, a procedural
node) carrying Space Warp, Glow and Trail, plus World-owned fireflies:
- It is generated through the Add Effect menu's own path (`editEffects` + `addDefaultEffectRoutes`,
  the hidden `[.generate]` case).
- It loads with no effect warnings.
- All four instances are `Drawn` at t=120.
- Both motion routes are bound to `entity.visitor.speed`.

### Looking at the demo changed three numbers

Review sheets are in the session scratchpad under `ufo/` (before) and `ufo2/`, `ufo3/` (after). This
was the first render in which an owner both moved and carried a warp; each slice alone had not had
velocity. The three fixes:
- **Route depth.** `owner.speed → strength` was 0.8 per m/s. At 30 m/s that added 24 to a row whose
  hard maximum is 4, and rainbow fringes covered a third of the frame. It is now 0.03.
- **Rim.** The rim of a proxy stretched 2.2× along a path towards the camera drew a pale arc from the
  sky to the ground, tens of metres from the craft. UFO Warp now has no rim (Portal and Magical keep
  theirs), and its stretch is 0.6.
- **Chroma.** The chroma fringe around a horse read as a glitch, so it went from 0.2 to 0.08.
- **After:** a gentle bend of the ground and ridge behind a dashing saucer, and almost nothing while
  it hovers (0.5% of pixels at t=120).

### Each primitive on pixels

Numbers are from the slices' GPU tests, and each was shown failing when its rule was broken:
- **FXL.** Gain 4 ≈ 4× emission, pixel-matching a 4× material. That holds on procedural nodes too.
- **DF.**
  - A checker behind the warp bends; a cube in front of it does not (782 px by the pixel rule,
    296 px by the leak mask).
  - The saucer stays crisp: 5,123 px smear if self-exclusion is zeroed.
  - Turbulence is temporally coherent: mean frame-to-frame difference 0.12, against 4.5 for white
    noise.
  - Two warps superpose.
- **RIBBON.** Cut exactly at a wall's edge, and it writes emission.
- **EMIT.** Each look is visibly present (off vs on), and two runs are identical.
- **Gates.** Every primitive is byte-identical with no producer. This was checked against a
  pre-change binary for FXL (Glowmere t=60), and in-test for DF, RIBBON and EMIT.

### Stacking on one owner

Glow and Pulse on one owner both act; two owners' Glows are independent. The UFO stack draws DF,
FXL, RIBBON and HIST together on one owner.

### Exit criterion (roadmap §1.10)

`[effects][seek][determinism]`: a craft flown by a keyed track carries Space Warp, Glow and Trail
with their default routes. At 5.5 s, a play and a scrub agree:
- the ribbon strips and vertices and the lane records, bit for bit;
- the distortion proxy to 1e-5. Its strength comes through a smoothed route, and a seek re-seeds
  the modulator's filter chains (1.008299 vs 1.008298).

The test was shown failing with the replay's history recording removed.

### Cost

- **DF**, at 1080p on the Glowmere saucer (240-frame GPU p50): the far shot is not measurable; the
  close shot is offset 0.26 ms plus resolve 0.07 ms (frame 21.30 → 22.02 ms).
- **The others** are unmeasured beyond the gates. FXL is one branch per draw; RIBBON and EMIT cost
  what they draw.

## Findings not fixed here

- **Engine-level scrub divergence on the Glowmere film (pre-existing).** An `Engine` seeked to 150 s
  puts the saucer 11.6 m from where a 60 Hz `Engine` play puts it; the nearest played frame is 4.3 m
  away, half a second earlier.
  - It reproduces on the stock project with no Wave 1 effect, on `agent/entity-effects`, and on
    `main` 13bc030f with identical numbers.
  - ADR-700's evidence is composition-level, with no modulation routes ticked.
  - Kept as a hidden `[.known-defect]` case in `test_ufo_stack_demo.cpp` for the seek's owner. The
    roadmap's exit test on the film therefore cannot pass until that is fixed; it passes on an owner
    the engine's seek does reproduce.
- **The volumetric march's whole-frame perturbation** (ADR-702) belongs to the tornado/fog track.
- **Distortion misses particles and water.** They write no depth, so one in front of a warp can
  still be bent.
- **The emission and velocity targets are not warped.**

## Not done in Wave 1

- LIGHTMOD's modulate half (Pulse on a Light owner).
- `screenSize` and `onScreen` signals.
- Trail's Blade mode.
- A `checkRegistry` rule that an `owner.` default needs a non-World-only type.
- The Bloom Source "threshold bias" (it needs a post change).
- A UI caller for `renameEffectOwner`.
- The ecology-cap trade's composition-level test.

**UI.** The Effects panel draws every Wave 1 card from the registry; the Space Warp card was captured
with its tuned values. The headless UI capture of this project never finishes opening, so the card
badge was only seen reading "dormant", not "drawn". The badge was **not verified live**.

## Revisit when

- the Phase E object-layout work lands (FXL's padding);
- a wave needs ribbons or proxies past their budgets;
- the engine-level scrub divergence is fixed, at which point un-hide the film's exit test.
