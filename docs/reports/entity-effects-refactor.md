# Entity Effects refactor: deliverables report

Branch `agent/entity-effects` (not merged, not pushed). Design and rationale: **ADR-702**
(`docs/decisions/ADR-702-effects-are-instances-attached-to-owners.md`). This report collects the
spec's §38 deliverables and links to the rest.

## Architecture audit (spec §2)

In short (ADR-702 has it in full):

- **A. Representation.** There were two systems, not one. ADR-207's `worldEffects` (surface waves:
  the camera travel beam and the hero pulse) and ADR-230/500's `atmosphericEffects` (comet, aurora,
  meteor shower, vortex, fog bank, tornado). Each had its own list, parameter prefix (`worldfx/` vs
  `atmos/`), registrar, serialiser and evaluator. Neither had an owner. Hero Pulse was a preset of
  the world-wave system: one scene-wide record with a `focusHero` source.
- **B. Tornado + Aurora.** On `main` they already coexisted at the data and render level. ADR-562
  had lifted the single medium slot to 4, and the aurora is drawn by a different integrator anyway.
  This was measured before any change. The real limitations were structural: two owner-less lists,
  identity by display name, fixed per-integrator capacities with silent overflow (a bare `break` for
  waves, and a `dropped` counter nothing read), and two of everything. One residual render
  interaction was found and is **not fixed**: turning on the march for any placed medium perturbs
  the whole frame by ≤ 7/255 per channel.
- **C. Renderer.** Waves are a per-fragment term in the lit pass (they modify material response).
  Sky effects are a far-plane draw after the opaque pass. Media are marched in the volumetric pass
  and composited depth-aware. No effect creates geometry or particles, uses compute, or is a
  post-process.
- **D. Modulation.** Every number was already a parameter reached by routes, timeline, presets, cues,
  macros and control maps. Activation comes from the director's cut. The aurora's spectrum vector is
  the one documented exception. Nothing was added or replaced.

## Files and modules

- **New:** `src/world/effects/effect_instance.{hpp,cpp}`, `effect_kind.hpp`,
  `effect_stack.{hpp,cpp}`, `effect_timing.{hpp,cpp}`, `kinds/{ground_pulse,travel_beam}_effect.cpp`,
  `kinds/wave_rows.hpp`, `src/ui/effects_panel{,_logic}.{hpp,cpp}`, `tools/migrate_effects.py`.
- **Moved/renamed:** `src/world/world_effects/` → `src/world/effects/` (effects/ → kinds/);
  `world/effects.*` → `world/wave_effect.*`; `atmospheric_params.*` → `effects/effect_params.*`;
  `shaders/world_effects.wgsl` → `wave_effects.wgsl`.
- **Removed:** `src/world/effect_params.*` (the hand-written wave registrar) and
  `src/ui/world_effects_panel.*`.
- **Changed:** `Engine` (one list, `setEffects` / `editEffects` / `effectStatus`, one
  `updateEffects`); `Composition` (`effects()`, JSON `effects`); `scene::Scene` (`waves`);
  `SceneRenderer` (wave renames only); `edit_history` (`EffectChange`); `world_panel` and
  `lights_panel` (the Effects section); every tracked scene and project (converted).

## New architecture (spec §3–§14, §28)

See ADR-702 "The model", "Render stages and ordering", "Resource isolation", "Lifecycle",
"Parameters and modulation".

## Effect data model

An `EffectInstance` has these fields:

- `id`: stable and scene-unique; it is the `fx/<id>/` parameter prefix.
- `kind`: the type, a registry schema.
- `name`
- `owner`: `{World | Entity | Camera | Light, name}`.
- `enabled`
- `order`: the position in the owner's stack.
- `style`
- `activation` and `timing`
- payloads
- `flow` and `values`

The type declares:

- targets, category, render stage and priority;
- parameter rows (range, default, page, section, format, tooltip, choices);
- presets and default audio routes;
- endpoints, anchors, the factory and the integrator bucket.

## Renderer integration

The evaluator (`Engine::updateEffects`):

1. applies the parameters;
2. builds one context;
3. walks a cached (stage, priority, stack) order;
4. lets each stage's builder write its own frame block and per-instance slots;
5. writes a per-instance `EffectStatus` (drawn / dormant / disabled / dropped / orphaned).

The renderer then executes its stages in frame order. Capacities: waves 8, comets 6, auroras 2,
media 4. Overflow is reported per instance and in the log.

## UI changes

The World Effects panel is removed. A generic `EffectsSection` sits in the World inspector
(Environment → World, a node → that entity, Camera) and the Lights panel. It has collapsible cards,
a status badge, enable, a menu (move up/down, duplicate, reset, remove), drag-reorder within an
owner, presets, Main/Advanced rows, Timing, endpoint/anchor/ground/field pickers and beat response,
plus a registry-driven "+ Add Effect" grouped by category. Structural edits go on the existing undo
stack. Frame echo moved to the Environment inspector.

UI verification is limited to these captures:

- the Environment/World Effects section, which I captured myself;
- the node (`rook`) and Camera captures, taken by the UI sub-agent.

Menus, drag and the Lights panel were **not visually verified**.

## Serialization

One `effects` array in both the scene file and the project document. Each entry is `{id, type,
name, owner, enabled, order, style, activation, timing, ground?, flow?, parameters}`. The old keys
are refused by name (ADR-441).

## Migration

`tools/migrate_effects.py` converted 45 files surgically, so unchanged members keep their bytes. It:

- converted all tracked scenes and projects;
- rewrote paths everywhere in each document;
- expanded Hero Pulse per hero, duplicating routes onto it;
- carried a scene's vortex into project lists that lacked one;
- refreshed the recorded scene hashes.

Re-running it reproduces the committed files byte for byte.

## Glowmere Valley 2 multicam

The migrated film has:

- **World:** `aurora` and `camera-travel-beam`.
- **Heroes:** each of its 16 heroes owns `<hero>-hero-pulse`, a Ground Pulse with source `owner` and
  activation `heroFocus`.
- **Routes:** the beat route onto the pulse's intensity became 16 routes.

It renders **byte-identical** to the pre-refactor binary at 16 frames over t = 2–158 s.

## Tests

**Full suites on the merged branch** (`1e508d14`, reconfigured, and a second build doing no work
before the run):

- **CPU** (`avgen_tests`): 3,192 cases — 3,175 passed, 16 skipped, 1 failed as expected (the
  deliberate `[!shouldfail]` at `test_character_lab_slopes.cpp:187`); 8,700,080 assertions; exit 0.
- **GPU** (`avgen_render_tests`): 419 cases — 418 passed, 1 skipped (texture sharing, no Syphon
  client); 524,200 assertions; exit 0.

The CPU port (on `agent/entity-effects-tests`, now merged) found and fixed four `src/` defects in
separate commits:

- a malformed wave endpoint silently became the default instead of refusing the file;
- the Sparkle toggle row was filed under a section its page never draws;
- a vortex published its flow field under its display name, not its id;
- the panel registry was sized one larger than its entries, so one panel had an empty id.

New CPU tests: `test_effect_stack.cpp` (13 cases), `test_effects_project.cpp` (4 cases, including
save→load→save on Glowmere after a frame has run), `test_effects_panel.cpp` (10 cases), and the
conformance test extended to all 8 types. Each new guard was seen failing against deliberately
broken code before being restored.

- **Unit/integration (CPU):** see the full-suite line below.
- **GPU:** `test_effect_stack_gpu.cpp`:
  - Tornado + Aurora on a fixture, shown able to fail;
  - Tornado + Aurora on the Glowmere film at t = 62, pixel counts in ADR-702;
  - two entity-owned Ground Pulses;
  - two World fog banks.
- **Ported:** the wave, comet, aurora and deliverable GPU suites.

`AVGEN_EFFECT_DUMP=<dir>` writes every arm as a PNG. I inspected them.

## Performance

About 0.02 ms per frame of CPU for the film's 18 instances (~1 µs each), from the hidden `[.perf]`
probe. GPU work is unchanged: same integrators, byte-identical frames. There are no per-frame
allocations in the evaluator; the order is cached per list change. Parameter re-registration
happens only on structural edits.

## Compromises and known limitations

- The march perturbs the whole frame when any medium is live (≤ 7/255). It is found, not fixed.
- Capacities are fixed per integrator (they are reported, not raised). `vortexGlow` takes the first
  medium only.
- No Camera- or Light-targeted type exists yet. Entity-derived signals (owner velocity on the bus)
  are designed but not built.
- Hero Pulse per hero means N edits for a global look change. There is no shared-definition mode.
- The field bus still publishes a vortex under its display name, not its id.
- Frame echo remains a scene setting rather than an effect instance.

## Recommended follow-up

1. Fix or explain the march perturbation.
2. Add a bus signal for owner kinematics (it unlocks velocity-driven effects).
3. Add a bulk-edit or shared-definition option for per-hero effects.
4. Add the Effect Library (see `docs/design/effect-library/`).
