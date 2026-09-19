# Lights panel & viewport authoring — Phase 0 audit and proposed architecture

**Date:** 2026-09-19
**Branch:** `agent/lights`
**Status:** audit complete, implementation not started.

This is the Phase 0 deliverable both owner specs open with ("First-Class Lights Panel",
"Professional 3D Viewport Authoring"). It is deliberately *not* an ADR: ADR numbering has collided
six times this week, the decisions below are not all mine to make, and the precedent for a
pre-implementation audit document is `docs/offline-backend-audit.md`. The ADR should be minted when
the first decision is implemented, against the high-water mark as it stands that day.

The two specs are treated as one piece of work. They are one architecture, and the second forbids
two selection models.

---

## 0. The headline

**Far more of both specs already exists than either spec assumes, and the one thing that does not
exist is the one thing that makes a light authorable: a light has no position parameter, no UI, and
no place in a saved project.**

The engine has a rich light model, complete scene-file serialization, per-light registered
parameters, a full transform gizmo, multi-select, GPU picking, command-based undo, an editor-only
3D overlay painter, a panel registry, and light/camera visualization primitives. What it does not
have is any path by which a person can bring a light into being, move it, or keep it.

Three claims in the briefing are wrong and one is dangerous. They are in §7.

---

## 1. Where lights live today

One light type, and it is good: `scene::PunctualLight` — `src/scene/scene_types.hpp:378`. 25 fields.
Seven kinds: `Directional, Point, Spot, Rect, Disk, Tube, Sphere`. Temperature and tint in Kelvin,
per-light shadow strength/bias/softness/contact, volumetric strength, diffuse/specular-only.

It reaches the renderer through two paths:

```
Composition::authoredLights_  ──rebuild──┐
LightRig::expand             ──frame────┤
glTF KHR_lights_punctual     ──rebuild──┼──►  Scene::lights  ──packLight──►  GpuLight (128 B)
Composition::updateEcologyLights ─frame─┤                                      ↓  storage, group 0 binding 1
defaultKeyLight()            ──rebuild──┘                               shaders/shadows.wgsl:49
```

`Scene::lights` is rebuilt wholesale and never serialized; it is a derived per-frame product. The
authored list is the durable one.

**Precedence is documented and load-bearing** (`composition.hpp:692-711`): asset lights, then
authored lights, then `defaultKeyLight()` only if there is no rig *and* nothing else produced a
light, then rig lights every frame, then ecology lights capped at `kMaxEcologyLights = 224`.
Authoring one light turns the default key off. A rig coexists rather than being replaced.

Caps: **256 scene lights** (`kMaxSceneLights`), 32 per froxel, 8 in the non-clustered fallback,
**8 shadow views total** (`kMaxShadowViews`).

### Area lights are real

`shaders/lighting.wgsl:315` dispatches Rect and Disk to `shadeArea`, a genuine linearly-transformed-
cones integration with fitted LTC tables built on the CPU at startup (`buildLtcTable`,
`light_data.hpp:196`). Tube and Sphere go to `shadeRepresentative` (Karis representative point).
The CPU path tracer samples Rect over the emitter properly (`src/pathtrace/lights.cpp:86-95`).

So spec 1 §2's conditional — "if the renderer supports area lights" — resolves to **yes**, and the
spec's instruction not to fake them does not apply. But the capability boundary is not clean, and
§5 below states it honestly.

---

## 2. What already exists that the specs ask for

Listed because building any of it again would be the waste the specs are trying to prevent.

| Spec ask | Already exists | Where |
|---|---|---|
| S1 §13 light serialization | **Complete, symmetric, 26 keys both directions** | ADR-278; `composition.cpp:735`/`:860` |
| S1 §11 light params as modulation targets | 7 of 25 fields registered under `lights/<name>/` | ADR-358; `composition.cpp:3517` |
| S1 §12 timeline animation of light props | Any registered parameter is keyable, same path namespace | `src/params/timeline.hpp` |
| S1 §15 / S2 §9 light visualization | Emitter drawing incl. Rect/Disk shapes, per-light colour | `debug_visualizer.cpp`, `drawEmitter` |
| S2 §2 camera frustum drawing | `drawFrustum`, near/far/basis | `debug_visualizer.cpp:72` |
| S2 §8 transform gizmos | Move/Rotate/Scale, axis + plane handles, correct ray-to-axis maths | `src/ui/gizmo.{hpp,cpp}`, `tests/unit/test_gizmo.cpp` |
| S2 §12 Q/W/E shortcuts | **Already bound exactly as specified** — Q Select, W Move, E Rotate, R Scale | `application.cpp:2846-2868` |
| S2 §13 world/local toggle | `WorldEditor::localSpace`, bound to X | `world_editor.hpp:172` |
| S2 §15 snapping | `GizmoSnap{move,rotate,scale}`, off by default | `gizmo.hpp:78` |
| S2 §16/§17 multi-select, shift-click, marquee | `ui::Selection` is a vector; box select exists | `world_edit.hpp:42`, `nodesInScreenRect` |
| S2 §24 focus/frame bounds | `Composition::nodeBounds` → `WorldBounds{centre,size,radius,valid}` | `composition.hpp:545` |
| S2 §27 undo incl. drag coalescing | `EditHistory::beginDrag/commitDrag/cancelDrag` | `edit_history.hpp:248` |
| S2 §20 viewport overlay toolbar | `drawPreviewToolbar`, `drawViewportHud` | `viewport_overlay.cpp:519` |
| S2 §32 helpers must not render | The ImDrawList overlay provably cannot — an offline render never runs it | `viewport_overlay.hpp:5-25` |
| S1 §1 panel registration | 19-entry registry; a new panel is 3 edits | `editor_layout.cpp:21` |

**Spec 2 §12 is already satisfied and needs no new bindings.** The spec's caution about conflicting
shortcuts is well-placed but the conflict already resolved itself in the codebase's favour.

---

## 3. What does not exist — the gap list

Ordered by how much each one blocks.

1. **A light has no position parameter.** `AuthoredLightParams` (`composition.hpp:734`) registers
   `enabled, intensity, color, azimuth, elevation, angularSize, shadowStrength` — 7 of 25 fields.
   `position`, `range`, `innerConeAngle`, `outerConeAngle`, `temperature`, `tint`, `width`,
   `height`, `radius`, `castsShadow`, `contactShadow`, `shadowBias`, `volumetricStrength`,
   `diffuseOnly`, `specularOnly`, `up` are file-only. **Until position is a parameter, no gizmo can
   move a light, because every transform in this editor is a parameter write and that is what makes
   it undoable** (`edit_history.hpp:11-13`).

2. **No UI or AI call site creates, deletes or reorders a light.** `setAuthoredLights` has exactly
   one caller: `Composition::fromJson` (`composition.cpp:8124`). Authoring a light means editing
   JSON by hand. `lighting.list` is the only AI surface and is read-only.

3. **`Engine::saveProject` writes no `lights` key — and this is the fifth instance of a defect four
   ADRs have already fixed.** ADR-207 (world effects), ADR-230 (atmospherics), ADR-276 (heroes),
   ADR-330 (node edits) all found the same thing: the composition is saved *by reference*
   (`assets.scene.path` + a hash), so anything the session changed on the Composition and did not
   write into the project lived in the window and in no document any render reads. The divergence
   block at `engine.cpp:1009-1105` handles four lists and not the fifth. **A Lights panel that calls
   `setAuthoredLights` without adding that key reproduces the defect exactly**, and
   `check_project_integrity.py` will pass on the result.

4. **No pick space for lights or cameras.** `PickSpace` is a **2-bit tag with exactly one free
   slot** (`scene_types.hpp:125`): Entity=0, Procedural=1, Sdf=2, and value 3 unused. Two new spaces
   do not fit. Widening the tag costs index bits and touches the shaders that write the identifier
   target — which is the concurrent Tree of Life agent's territory.

5. **No rename, anywhere.** `grep -n rename src/scene/composition.hpp` returns nothing;
   `world_context_menu.hpp:17-22` states outright that no `WorldEditor` method, no `world_edit.hpp`
   function and no `EditCommand` record could carry one. Spec 1 §3 and spec 2 §23 both ask for it.
   Worse: a light's parameter paths are keyed on `sanitise(name)`, so renaming re-paths every
   parameter and silently orphans any modulation route or timeline track bound to the old path.

6. **`EditCommand` has no record for "a light entered or left the scene."** It carries params,
   parents, heroes, node add/remove, and a whole-sequence `TimelineChange`. Add/Delete Light is not
   expressible.

7. **Camera-panel edits are not undoable** and `ControlPanel::selectedCamera_`
   (`control_panel.hpp:408`) is a private `uint32_t` with **no observer anywhere**. Selecting a
   camera changes nothing in the viewport and draws no gizmo.

8. **The sequencer's selection syncs with nothing.** `SequencePanel::selection()` has no reader in
   the entire tree. Spec 2 §28's "Sequencer → Canvas" link must be built from zero.

9. **Three unrelated selections exist**: `ui::Selection` (multi, node names, on `WorldEditor`),
   `ui::WorldSelection` (single, tagged kind, on `WorldPanel`), `SequencePanel::selection_`, plus
   `selectedCamera_`. Only the first two are synced, one-directionally, once per frame.

10. **Lights and cameras are not nodes and must not become nodes.** ADR-278 rejected
    `NodeKind::Light` with evidence: `scene::NodeKind` appears at 148 sites in 16 files, each of
    which would have to decide what a node that draws no geometry means to the brush, the context
    menu, the asset browser and the graph. That decision stands and the viewport work must not
    quietly reverse it.

---

## 4. Proposed architecture

### 4.1 Where an edit lands — settled, not open

ADR-271 and ADR-278 have already answered this, and ADR-278's *first revisit trigger is this exact
task*: "**A UI control that edits a light.** Then ADR-271's boundary applies in full and the answer
is a registered parameter over the authored value, not a scene rewrite."

So:

* **Editing an existing light** → the project's `parameters` block, via a registered parameter
  seeded from the scene's authored value. This is the `particles/<name>/extent` pattern
  (`src/scene/particles.cpp:167`), and ADR-271 §2 is the cautionary tale: the control must write
  the unit it displays. Absolute values seeded from the scene, never multipliers.
* **Adding or deleting a light** → the authored list on the Composition, saved into the *project*
  as a fifth divergence key, exactly as `worldEffects`/`atmosphericEffects`/`heroes` are. The list
  is small and all value types, so it takes the "project holds a copy" shape, not `sceneNodes`'
  difference-by-name shape.
* **Nothing writes a scene file.** Scenes are shared — `glowmere-stylized.scene.json` backs two
  projects — and a re-authoring editor would change a project the user did not open and
  re-fingerprint it on every drag.

### 4.2 Registering the missing parameters

Extend `AuthoredLightParams` to cover the fields a person manipulates: `position` (vec3, **the
blocker**), `range`, `innerCone`/`outerCone` (degrees, matching the JSON spelling), `temperature`,
`tint`, `width`, `height`, `radius`, `castsShadow`, `contactShadow`, `shadowBias`,
`volumetricStrength`.

Two constraints from the existing code:

* `azimuth`/`elevation` already own direction for aimed lights, in **world** space deliberately
  (not the camera-relative frame `LightRig` uses). A rotate gizmo on a directional or spot light
  must write those two, not a quaternion, or there will be two disagreeing descriptions of aim.
* Registration is dynamic and already correct: `setAuthoredLights` unregisters against the **old**
  names first, then swaps, then re-registers. The invariant when mutating the set is
  `timeline_.unbind()` → mutate → `Engine::rebind()`, because `ModRoute::targetParam` and
  `Track::param` hold raw `IParameter*`.

### 4.3 Selection — one model, per spec 2's prohibition

Generalize `ui::Selection` from `vector<string>` to a vector of a tagged handle:

```cpp
struct SelectionRef { enum class Kind { Node, Light, Camera } kind; std::string name; };
```

`Kind::Node` keeps today's exact behaviour so nothing regresses; `primary()`, `toggle()`,
`retainOnly()` and the shift-click semantics are unchanged in shape. This is spec 2 §34's
`SelectionTarget` and it is the one refactor both specs genuinely require. `WorldSelection` already
has a `Kind` enum including `Camera`, so the vocabulary exists.

### 4.4 Picking — CPU, not the GPU id buffer

**Recommendation: pick light and camera helpers on the CPU in screen space** — project the helper's
origin through `projectPoint` (`world_probe.hpp:115`), take the nearest within a pixel radius, and
give helpers priority over geometry only when the click is within that radius.

Reasons, in order:

1. It needs **no new `PickSpace`**, and there is only one free slot for two new kinds.
2. It touches **no shader**, so it does not collide with the concurrent agent's ownership of
   `shaders/common.wgsl`, `pbr.wgsl`, `scene_renderer.*` and `scene_types.hpp`.
3. Editor helpers must never reach a render (spec 1 §15, spec 2 §32). Writing them into the
   identifier target would put them in a pass the offline renderer runs.
4. It is what DCCs do for icon-like helpers, because an icon has no depth to test against.

The cost is that a helper behind geometry is still clickable. That is the conventional behaviour
and is the conservative trade; **flagging it as the owner's call** if they want occlusion.

### 4.5 Drawing — the ImDrawList painter, not GPU debug draw

Two overlay systems exist. Use the **`Painter`** in `viewport_overlay.cpp:107` (point, line, ring,
box, flatQuad, label-with-clamping), not `rendering::DebugDraw`.

`DebugDraw` runs inside the scene pass and is reachable by a render; the `Painter` is ImGui-side and
`viewport_overlay.hpp:5-25` states that an offline render never runs this code. That is spec 2 §32
satisfied structurally rather than by testing for absence.

The existing `drawEmitter`/`drawFrustum` geometry in `debug_visualizer.cpp` is the right *shape* and
should be ported, not reinvented — but it should be ported to the Painter, and kept visually
restrained per spec 2 §33.

Extension point: add members to `EditorVisuals` (`world_editor.hpp:70`), which already has
`HeroMarker`, `ParentLink`, `NavRoute` and `SelectedBox` as templates, populate them in
`WorldEditor::update`, paint them in `drawViewportOverlay`. Overlay output is byte-comparable
headlessly via `tools/overlay_shot.cpp`, which is the only scripted instrument that can regression-
test any of this.

### 4.6 Undo

Add a `LightChange { std::vector<AuthoredLight> before, after; }` to `EditCommand`, recorded
**whole**, on the explicit precedent of `TimelineChange` (`edit_history.hpp:117`): a record type per
gesture needs an inverse per gesture, whereas "the list was this, now it is that" reverses itself.
The authored light list is kilobytes of value types — the argument against snapshotting the
*composition* (256 terrain chunks, 250k instances) does not apply to it.

Transform edits need no new machinery: once position is a parameter, a gizmo drag is a `ParamChange`
and `beginDrag`/`commitDrag` already coalesces it into one undo step.

---

## 5. The honest capability boundary

Spec 1 §10 asks for one. Here it is, measured from the code rather than assumed:

| | Raster (clustered) | Raster (8-light fallback) | CPU path tracer |
|---|---|---|---|
| Directional | full | full, no shadows | full |
| Point / Spot | full, soft shadows | no shadows | **hard shadows** — delta position, reported `Degraded` |
| Rect / Disk | full, LTC | **absent** | full, sampled over the emitter |
| Tube / Sphere | representative point | **absent** | facing-disc approximation, "crude and honest about it" |
| Volumetric in-scatter | area lights treated as points | n/a | **unsupported** |
| `softness`, `shadowBias`, `contactShadow`, `volumetricStrength`, `diffuseOnly`, `specularOnly` | honoured | ignored | **ignored entirely** |

The path tracer already has the mechanism to say so: `pathtrace::CapabilityReport`
(`snapshot.hpp:49`) with `Support{Full,Degraded,Unsupported}`. **It is log-only —
`TraceJob::capabilities()` is exposed and read by no UI.** Surfacing it is cheap and is the honest
answer to spec 1 §10's "add diagnostics where appropriate". There is no equivalent for the raster
renderer and building one is out of scope.

---

## 6. Latent defects a Lights panel will expose

These exist now; giving users the ability to author lights is what makes them reachable.

1. **Only one cascaded directional shadow per frame, silently** (`shadow_renderer.cpp:208`). The
   moment a user can add a second shadow-casting directional — a sun and a moon — one of them casts
   nothing with no warning. Needs a diagnostic under spec 1 §23.
2. **The ecology erase is a global prefix match** (`composition.cpp:6541`): `std::erase_if` over all
   of `scene_.lights` on the name prefix `ecology.glow.`. A user-named light matching that prefix is
   silently deleted *and shifts `authoredLightFirst_`-relative indices*, so the parameter block then
   drives the wrong light. Once users type names, this needs a guard.
3. **`kLightFlagArea` is packed and read by no shader** — dead, harmless, misleading.
4. **glTF-imported lights are never re-placed after rebuild** — only intensity and colour refresh.
   ADR-278 left this deliberately because no asset in the repo carries one.
5. **Emissive instanced geometry contributes nothing to path-traced NEE** (`snapshot.cpp:420` loops
   `snap.meshes` only), with no capability entry reporting it.

---

## 7. Where the briefing was wrong

Recorded because the briefing asked to be corrected and because three of these change the plan.

1. **"Future/current Metal hybrid path tracing" (S1 §10) and "Metal hybrid path trace" (S2 §32,
   §37) do not exist.** There is no Metal backend, no `TraceBackend` interface, no `.metal` file and
   no hybrid pass anywhere in the tree. `docs/offline-backend-audit.md` — dated today — says so at
   line 498: "there is no `TraceBackend` interface, no Metal prototype, no hybrid pass". It is a
   proposal document. **The acceptance criteria that name it cannot be satisfied and should be
   struck**, not faked. There is exactly one path tracer: CPU, over Embree.

2. **"You will likely need `composition.*` and `scene_types.hpp` for light serialization."** Light
   serialization is already complete and symmetric — 26 keys, both directions, no reader/writer
   asymmetry, verified field by field. **I do not need `scene_types.hpp` at all** under the
   recommendation in §4.4, and I need `composition.*` for parameters and list mutators rather than
   for serialization. The coordination need is real but narrower and in a different file than
   stated.

3. **"W/E/Q as Move/Rotate/Select must be checked against existing bindings before you take
   them."** Checked: they are already bound to exactly those meanings (`application.cpp:2846-2868`),
   plus R=Scale, B=Place, X=local/world. Nothing to take. One latent conflict worth knowing: **E is
   conditional** — it rotates only when the selection is non-empty and otherwise falls through to
   the File menu's "Open Environment" HDR dialog. With a light selected that becomes a real
   ambiguity.

4. **Arrow keys.** The briefing's warning is accurate, and there is a sharper version of it:
   the editor's `nudgeSelection` claims the arrows whenever the selection is non-empty, so the
   transport only ever sees them with nothing selected. Making lights selectable therefore *widens*
   the window in which arrows stop scrubbing. That is the regression the owner already reported
   once.

---

## 8. Recommended order

1. Register the missing light parameters, `position` first. Prove reachability with ADR-350's
   prescribed pair (`tests/unit/test_day_night_parameters.cpp` cases
   *"Every day/night setting is a real parameter"* + *"A saved scene keeps its day/night cycle"*,
   replicated per `test_wind_parameters.cpp`) — registration with a negative control, and
   save → load → **save**, asserting named keys.
2. The fifth project-save divergence key, with a byte-stability control on an untouched project.
3. `SelectionRef` — the one required refactor, with `Kind::Node` behaviour unchanged.
4. `LightChange` in `EditCommand`; Add/Duplicate/Delete Light.
5. The Lights panel (3 edits against the registry).
6. Viewport helpers on the `Painter`, CPU picking, selection sync both ways.
7. Cameras: give them the same `SelectionRef` treatment and fix the un-undoable camera panel.

Steps 1–2 are the ones without which everything else is unreachable or unsaved, which is this
repository's defining failure mode and is called out by both specs.

---

## 9. Open questions for the owner

* **Helper occlusion** (§4.4): should a light icon behind a mountain be clickable? Conventional
  answer is yes; the conservative implementation is yes; flagging because it is a taste call.
* **Rename** (§3.5): renaming a light re-paths its parameters and orphans its modulation routes.
  Options are (a) no rename, (b) rename with automatic route/track re-pointing, (c) give lights a
  stable id and decouple the parameter path from the display name. (c) is correct and is the
  largest; (a) is what the codebase does everywhere else today. **Recommend (a) for this pass** and
  record (c) as the revisit trigger.
* **A second shadow-casting directional** (§6.1): warn, or raise the shadow-view budget?
