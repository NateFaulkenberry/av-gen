# ADR-702: Effects are instances attached to owners, in one list, evaluated by render stage

**Status:** Accepted
**Date:** 2026-09-24
**Supersedes:** the two-list shape of ADR-207 (`worldEffects`) and ADR-230 (`atmosphericEffects`);
the World Effects panel. **Keeps:** ADR-500's schema registry (promoted), ADR-562's media slots,
ADR-091's two-tier determinism, ADR-441's "convert content, no shims".
**Implemented by:** `src/world/effects/` (`effect_instance`, `effect_kind`, `effect_registry`,
`effect_stack`, `effect_params`, `effect_timing`, `kinds/*`), `src/world/wave_effect.*`,
`src/world/atmospherics.*`, `Engine::updateEffects` / `setEffects` / `editEffects`,
`Composition::effects`, `src/ui/effects_panel*`, `tools/migrate_effects.py`
**Tests:** `tests/unit/test_effect_stack.cpp`, `test_effect_conformance.cpp`,
`test_effects_panel.cpp`, `tests/integration/test_effects_project.cpp`,
`tests/rendering/test_effect_stack_gpu.cpp` (the Tornado + Aurora acceptance), and the ported
wave/atmospheric/deliverable suites.

---

## Context

The owner asked for effects to be something any scene target owns — the World, a hero, a camera, a
light — several at once, ordered, individually modulated, and for "World Effects" to stop being a
separate concept. The test case named in the brief: *World → Tornado + Aurora simultaneously*.

### What there was (audit A)

Two effect systems that shared a lifecycle and nothing else:

| | ADR-207 "world effects" | ADR-230/500 "atmospheric effects" |
|---|---|---|
| Types | camera travel beam, hero pulse (one struct, `WorldEffect`) | comet, aurora, meteor shower, vortex, fog bank, tornado (`AtmosphericEffect`, all payloads side by side) |
| Storage | `Composition::worldEffects_`, key `worldEffects` | `Composition::atmosphericEffects_`, key `atmosphericEffects` |
| Identity | display name | display name |
| Parameters | `worldfx/<name>/…`, 33 hand-named rows | `atmos/<name>/…`, schema-driven rows (ADR-500) |
| Evaluator | `Engine::updateWorldEffects` → `WorldEffectFrame` | `Engine::updateAtmosphericEffects` → `AtmosphericFrame` |
| UI | two halves of one World Effects panel | |

Neither had an **owner**. A "hero effect" did not exist as such. The Hero Pulse was one scene-wide
`WorldEffect` whose source was `focusHero` ("whichever hero the cut is holding") and whose
activation was `heroFocus`. Hero Pulse was defined in `world/effects.hpp` as a preset
(`heroGroundPulse`), not as a system. So the "Hero effect" was a use of the world-effect
infrastructure, and the world and atmospheric families were two different infrastructures.

### Why Tornado and Aurora "could not coexist" (audit B)

The brief's hypothesis was that fog and the vortex/tornado share one placed-medium slot, so enabling
one hides the other. **That limit is gone:** ADR-562 raised it to `kMaxMedia = 4` slots. And the
aurora was never in that bucket anyway. It is drawn by `atmosphere_fx.wgsl`'s curtain integrator,
not by the march. Measured on `main` (13bc030f), before any change here, on Glowmere Valley 2
multicam at t = 20 with a tornado added to `atmosphericEffects`: aurora alone changed 22.7% of
pixels; with the tornado also on, adding the aurora still changed 23.5%. The tornado contributed in
both arms too. At the data and render level they already coexisted.

What was actually wrong, and what this ADR removes, is structural:

1. **Two lists, no owner.** An effect could not be attached to anything but "the scene". A per-hero
   effect had to be faked as a scene-wide record that follows focus.
2. **Identity by display name.** `atmos/<name>/…`: renaming an effect orphaned every route, key and
   preset aimed at it, and two effects could not share a name.
3. **Fixed per-integrator capacities with silent overflow.** Comets 6, auroras 2, media 4, waves 8.
   The ninth live wave was dropped by a bare `break`, while the header claimed "with a warning".
   `AtmosphericCounts::dropped` was counted and read by nothing. The panel computed its own separate
   guess at media contention. When one effect displaced another, nothing said so where a person was
   looking. The owner's report of 2026-09-20 ("if I turn on fog I can't see the vortex") was this
   class, when the media limit was 1.
4. **Two parameter registrars, two serialisers, two evaluators, one panel with two halves.** Every
   question ("is this effect on", "what did it draw") had two answers.

One residual render-level interaction was measured while writing the acceptance test, and it is
**not** fixed here (see Consequences): switching the volumetric march on for any placed medium moves
up to ~70% of the frame by at most 7/255 per channel (mean 1.4), even when the medium is behind the
camera. It dims nothing and hides nothing. It is a whole-frame perturbation of the march and
composite.

### Renderer (audit C)

Effects never touched the renderer's logic. The Engine resolves them per frame into fixed-size frame
blocks the renderer copies (ADR-207's rule: "a renderer that knew about heroes would have to be given
the director"). Each family reached the picture through a different technique:

- **Surface waves** (beam, pulse): no pass of their own. They are a per-fragment term in the shared
  lit pass (`wave_effects.wgsl`, included by `pbr_shade.wgsl` and `water.wgsl`), fed from 8 records
  in `FrameUniforms`. They modify the material response of existing geometry.
- **Sky** (comet, meteor shower, aurora): a fullscreen far-plane draw in pass 1 after the opaque
  geometry (`atmosphere_fx.wgsl`), depth-tested so terrain occludes it, plus a ground-illumination
  term (`atmosphere_ground.wgsl`).
- **Placed media** (vortex, fog bank, tornado): marched by the volumetric pass
  (`volume.wgsl`, half resolution, depth-aware composite into HDR after pass 1), up to 4 slots of 16
  lanes each, dispatched per slot on `MediumSlot::kind`. The first medium also lends a surface glow
  (`frame.vortexGlow`).
- No effect creates geometry, particles or compute work, and none is a post-process. ADR-410's frame
  echo is a post-process, but it is a scene setting, not an effect (below).

### Modulation (audit D)

Every effect number was already an ordinary parameter. Routes (`ModRoute`: audio bands, beat, onsets,
LFOs and other sources, MIDI/OSC through control maps), timeline tracks, presets, cues and macros
all address parameters by path. Activation windows come from the director's cut (`ShotSpan`s) on the
transport clock. The one exception is the aurora's 16-bin spectrum vector, carried in the context
because no scalar route can carry a vector. Field-bus subscriptions (ADR-420 §68) let an effect
follow a published flow field. There was no second modulation system to remove, and none is added.

---

## Decision

### The model

```
Scene ── effects: [EffectInstance…]            (Composition::effects_, the ONE list)

EffectInstance { id, kind (the type), name, owner, enabled, order, style,
                 activation, timing, <payloads>, flow, values }
EffectOwner    { kind: World | Entity | Camera | Light, name }
EffectSchema   { key, displayName, category, targets, stage, priority,
                 fields (parameter rows), styles, routes, endpoints, anchors,
                 resolve.bucket, factory, … }           (the type registry)
```

- **Type vs. instance.** `EffectKind` + `EffectSchema` is the type. `EffectInstance` is one use of
  it, with its own id, values, enable and modulation. Two entities each with a Ground Pulse are two
  instances of one type. The GPU tests draw exactly that.
- **Ownership.** Every instance names its owner. "The World's effects" is `effectsOf(list,
  World)`. A World effect is simply an effect whose owner is the World. The list is stored grouped
  by owner in stack order, so iterating the list walks every stack top to bottom.
- **Targets.** Each type declares the owners it supports: `targets`, a mask. Tornado, Aurora,
  Comet, Meteor Shower, Fog, Vortex: World. Ground Pulse: Entity | World. Travel Beam: World |
  Camera. `effectAllowedOn` and `effectKindsFor(target)` read the mask. The Add Effect menu and
  `validateEffects` use them, so no panel carries its own table.
- **Identity.** `id` is scene-unique, never contains '/', and is minted from owner + type
  (`rook-ground-pulse`, `aurora-2`). Parameters are `fx/<id>/<leaf>`. Renaming changes no path, and
  reordering changes no path.
- **Order.** `order` is the position in the owner's stack, persisted and contiguous. The pure
  operations (`addEffect`, `removeEffect`, `moveEffect[To]`, `duplicateEffect`, `removeEffectsOf`,
  `renameEffectOwner`, `normaliseEffectOrder`, `validateEffects`) live in `effect_stack.cpp` and are
  unit-tested.
- **Payloads.** These stay side by side on the instance (ADR-230/500's convention), because a type
  declared after ADR-500 keeps its numbers in the keyed `values` store and needs no edit to a
  shared header. A new effect is one file in `kinds/`, one enumerator and two registry lines. ADR-207's
  waves became two types, **Ground Pulse** and **Travel Beam**, over one `WaveEffect` payload with one
  row table (`kinds/wave_rows.hpp`) whose leaves are the old `worldfx` leaves.

### Render stages and ordering

List order is an author's stacking decision; a frame has an order of its own. Each type therefore
declares a **`RenderStage`** — `Geometry, Material, Lighting, Sky, Volumetric, Particles,
ScreenSpace, PostProcess` — and a `priority`, and names the **bucket** (the integrator that draws
it): `Surface` (Material), `Comet`/`Aurora` (Sky), `Medium` (Volumetric). `checkRegistry` refuses a
stage that disagrees with its bucket.

`Engine::updateEffects` is the one evaluator. It follows ADR-702 §28 (*evaluate → contributions →
sort → execute*):

1. apply parameters (finals) to the live list;
2. build one `EffectContext` (transport second, camera pose and timeline velocity, shots, heroes, a
   node query, spectrum, field bus). This merges the two old contexts;
3. walk `effectOrder_`, which is stable by (stage, priority, stack position), is cached when the list
   changes, and allocates nothing per frame. Each stage's builder (`buildWaveFrame`,
   `buildAtmosphericFrame`) writes its own frame block and its own slots;
4. the renderer executes its stages in frame order: lit pass (Material), far-plane sky draw (Sky),
   march (Volumetric).

Techniques are not forced through one shader. The stages that have no integrator yet (Geometry,
Lighting, Particles, ScreenSpace, PostProcess) are declared so a type can be added without a new
architecture. What such a type needs is an integrator, which is a shader and renderer change: the
same honest cost ADR-500 recorded.

### Resource isolation

The integrators are **intentionally batched shared arrays with per-instance slots**, not one resource
that later effects overwrite: 8 wave records, 6 comet records, 2 aurora records, 4 medium slots. Two
tornadoes are two slots; two fog banks, two slots; two pulses, two records (all rendered in
`test_effect_stack_gpu.cpp`). Capacity is finite. What changed is that exceeding it is **reported
per instance**. Every builder writes an `EffectStatus` for each instance it owns: `Disabled`,
`Dormant`, `Drawn`, `Dropped` (active, but its stage's capacity was full), or `Orphaned` (its owner
is not in the scene). The Effects panel shows the badge, and the engine logs the dropped count on
change. The wave builder's silent `break` and the unread `dropped` counter are gone. Also fixed:
`mediaDropped` accumulated across frames (`+=` on a block rebuilt in place) and is now reset per
frame.

Two stated single-resource limits remain and are recorded rather than hidden: `frame.vortexGlow`
takes the **first** medium only (one sphere in the surface shader), and a particle node's vortex
attractor binds the first vortex.

### Lifecycle

- **Created:** `addEffect`/`insertEffect` via `Engine::editEffects`, plus default audio routes on a
  user add.
- **Enabled:** `fx/<id>/enabled`.
- **Evaluated:** activation window × timing envelope, on the transport clock.
- **Rendered:** a builder claims a slot.
- **Disabled:** status `Disabled`.
- **Destroyed:** removal, or `removeEffectsOf(owner)`.

Activation is ADR-207's (`always`, `window`, `cameraTravel`, `heroFocus`) and uses the director's cut.
No second timeline exists.

### Parameters and modulation

Every row is a parameter `fx/<id>/<leaf>`, registered by the Engine into a container it releases
wholesale. There are no new pointers on Composition's hand-written lists. Routes, timeline tracks,
presets, cues, macros and control bindings all work unchanged because they address paths. Shared
rows (timing, ground illumination, flow subscription) apply per type through `sharedFieldApplies`,
so a wave registers no ground-glow rows that nothing reads.

**Entity-derived modulation** (UFO velocity → Space Warp strength) is possible without a hook. The
owner's state would be published as a signal on the bus and routed onto `fx/<id>/<leaf>` like any
other source. `SourceKind::Owner` already lets a type ride its owner's position. Neither was built
here beyond the `Owner` source.

### Hero Pulse (§19)

Glowmere's pulse was one record with `focusHero` source and `heroFocus` activation. It has **no
shared state**: its window restarts with each spotlight span, and its colour and shape are authored
numbers. So each hero now carries its **own instance**, owned by that hero, with an `owner` source.
`resolveActivationWindow` fires a named-subject effect only for its own subject. Sixteen per-hero
instances therefore draw exactly what the one focus-following record drew, and each can now be
tuned, disabled or modulated alone. Measured below: byte-identical frames. The one behaviour change
is authoring, not picture: editing "the" pulse means editing each hero's, or duplicating one. A
World-owned pulse with a `focusHero` source is still legal for a scene that wants one shared look.
A route onto the old pulse became one route per hero.

### Serialization and migration

One array, `effects`, in both serialisers: `Composition::toJson` for the scene and
`Engine::projectDocument`, the latter only when the session's list differs from the scene file's.
Each entry is `{id, type, name, owner, enabled, order, style, activation, timing, ground?, flow?,
parameters:{…}}`. `parameters` holds only the instance's own type's rows. A row borrowed from
another payload (a meteor shower's `Comet`, a fog bank's `Vortex`) nests there by block name.
Order survives save → load → save (tested with a frame stepped before the save).

Per ADR-441 there is **no reader for the old keys**. `worldEffects` or `atmosphericEffects` in a
scene is a named load error; in a project it is a named warning, and the key is ignored.
`tools/migrate_effects.py` converted all 45 tracked scenes and projects once, **surgically**:
unchanged top-level members keep their bytes. It also converts paths anywhere in the document,
expands hero pulses per hero, carries a scene's vortex into a project list that lacked one
(replacing ADR-387's load-time carry-over, which is deleted), and refreshes recorded scene hashes.
The legacy `environment.vortex` migration in the scene reader is kept: two scratch scenes and
tests still use it.

### UI

The World Effects panel is removed. `EffectsSection` (`src/ui/effects_panel.*`) draws any owner's
stack from registry metadata alone, with no per-type branches: collapsible cards with enable, status
badge, menu (move up/down, duplicate, reset, remove), drag reorder within an owner, preset, Main and
Advanced rows, Timing, endpoint pickers, sky anchor, ground glow, flow field and beat response. A
"+ Add Effect" menu groups `effectKindsFor(owner)` by category. It is wired into the World
inspector (Environment → World, a node → that entity, Camera → camera) and the Lights panel (no
light types yet; it says so). Structural edits are one step each on the existing undo stack
(`EffectChange` in `EditCommand`), including the default routes an add attached. ADR-410's frame
echo moved unchanged to the Environment inspector. It is a scene post setting, not an effect instance.

---

## Evidence

- **Tornado + Aurora on Glowmere** (`[gpu][effects][acceptance][glowmere]`): the migrated film, a
  tornado added to the World via `editEffects`, t = 62. Visible-pixel counts (channel sum > 24, of
  20,736 px at 192 × 108):
  - aurora alone 4,799
  - tornado alone 4,489
  - aurora with the tornado on 4,710
  - tornado with the aurora on 4,584
  - Both are `Drawn`; one aurora and one medium are in the frame; exactly one of 16 hero pulses is
    `Drawn`, none `Orphaned`; both still drawn and moving at t = 63.
  - Arm PNGs (`AVGEN_EFFECT_DUMP`) were inspected: the curtains are in the sky and the column in the
    valley, together.
  - A first version at t = 20 passed on byte-exact counts that turned out to be the march's
    perturbation. The shot there is a close-up of a mushroom cap: the aurora moved 1 visible pixel.
    The metric and the second were fixed.
- **The same on a fixture** (`[gpu][effects][acceptance]`): shown able to fail. With the evaluator
  sabotaged to drop the medium when an aurora is live, it goes red on the medium count and the
  marched-media stat.
- **Two instances of one type:** two entity-owned Ground Pulses (1,425 / 1,313 px, each unchanged by
  the other) and two World fog banks (2,653 / 2,942 px).
- **Behaviour preserved:** Glowmere multicam rendered by the pre-refactor binary (old format) and by
  this branch (converted content) is **byte-identical at 16 frames over t = 2, 20, 26, 50, 62, 86,
  122, 158**. That span covers many focus changes, i.e. many hero-pulse handovers.
- **CPU cost** (hidden `[.perf]` probe): `Engine::update` on the film at t = 61–71 took 2.128 ms per
  frame with its 18 instances, against 2.105 ms with none. About 0.02 ms per frame, or ~1 µs per
  instance. GPU work is unchanged: the same integrators, the same records, byte-identical frames.
  The film's project stores 626 `fx/` parameter values, most of them the 16 hero pulses' 38 rows each.

---

## Consequences

- One effect architecture; "World effect" survives only as a description of an owner.
- A new type is one file plus three lines. A new **integrator** (a new stage in use) is still shader
  and renderer work. The stages are declared so that work has a place to land.
- **Not fixed here:** the march's whole-frame perturbation when any medium is live (≤ 7/255, mean
  1.4, independent of where the medium is). It is worth an owner in the volumetric area; it is why
  the coexistence tests count visible pixels.
- **Not done:** Camera- and Light-targeted types (none exist yet); entity-derived signals on the bus;
  per-instance Hero Pulse editing in bulk (a "select all pulses" or a shared-definition mode);
  per-type capacity raising.
- Content in the old format no longer loads (ADR-441). `tools/migrate_effects.py` converts a file.

## Revisit when

- a type needs more slots than its integrator has (raise the capacity, and measure it: ADR-374's
  per-medium cost);
- a type needs a stage with no integrator;
- the hero-pulse duplication becomes an authoring burden. The shared-definition option (one
  definition, many instance overrides) was considered and not built, because nothing in the scene
  needed shared *state*.
