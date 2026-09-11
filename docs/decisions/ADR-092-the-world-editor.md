# ADR-092: The world editor — a ghost under the cursor, a record of every edit

Status: Accepted

## Context

Glowmere could generate a world from a recipe, and it could place one asset where you clicked. What
it could not do was let anybody *build* anything.

The gap was not a missing feature; it was a missing contract. Before a click, the editor knew
nothing it was willing to say: not where the asset would land, not how big it would be, not whether
the ground there was a cliff or a lake. After a click, it knew nothing it was willing to take back.
`docs/world-editor.md` said so in as many words — *"There is no undo. Save before generating over
something you want."*

The brief (`docs/world-authoring-spec.md` §17–§28, §42–§45) asks for a professional 3D
world-authoring workflow, and states the bar twice:

> The artist should never guess where an asset will land.

> Do not build an editor where artists are afraid to experiment.

Those are the same requirement seen from either side of the click.

## Problem

Five things have to be true at once, and this codebase makes four of them harder than they look.

1. **The preview has to be live.** A ghost that follows the cursor has to be recomputed every frame.
   The existing picker (ADR-068) reads the identifier and linear-depth targets — exact, free per
   frame, and a *blocking GPU round trip* on the frame it runs, up to five of them. Sixty of those a
   second is not a thing that can be optimised; it is a thing that has to be asked differently.

2. **Undo has to cover everything, cheaply.** A Glowmere scene is 121 terrain chunks, a quarter of a
   million scattered instances and several thousand parameters. Snapshotting it to make one flower
   undoable costs more than the flower.

3. **Transforms live in two places.** Every scene value is a registered parameter, so a node's
   position is `nodes/<name>/position`. But `CompositionNode::transform` is what a node registers
   its parameters *from*, and the scene file wrote position and scale from it while writing rotation
   from the parameter. Three fields, two stores, one value, and they disagreed.

4. **A structural edit re-flattens the world.** `Composition::dirty_` has no granularity. Ten call
   sites set it and every one re-runs `world::scatter` over ~260k grid cells and re-meshes every
   terrain chunk. §44 names this exactly: *do not rebuild the entire scene every time the artist
   places one flower.*

5. **None of it can be checked by looking at it.** This project cannot screenshot an ImGui frame.
   Live-mode `--capture` renders the scene and not the UI. An editor whose behaviour can only be
   verified by a human at the machine is an editor that is verified once.

## Alternatives considered

**A. Draw the editor into the scene.** Gizmos and ghosts as real geometry with real materials.
Depth-correct, and it is what a game engine does.

**B. Draw the editor over the frame, in ImGui's draw list.** The overlay projects world points to
canvas pixels itself and draws lines, rings and boxes into the canvas window.

**C. Undo by snapshot.** Copy the composition before each edit.

**D. Undo by command record.** Each edit is a list of what changed: parameter base values, node
parents, and the nodes that entered or left.

**E. Groups as a side table.** A name-to-names map beside the scene.

**F. Groups as node parenting.** A `NodeKind::Group` — an empty transform — with the members
parented to it.

## Decision

**B, D and F.** Plus a memoised terrain, and a hard line between the editor's decisions and its
pixels.

### The editor is drawn over the world, not in it

`ui::viewport_overlay` draws the ghost, the selection outlines, the gizmo and the drag readout into
the canvas window's ImGui draw list. Nothing the editor draws is scene geometry, so nothing it draws
can reach an offline render — the renderer reloads the project from file and draws the scene, and a
gizmo that lived in the scene would be a gizmo that could end up in a take. It also needs no
renderer or shader change, which matters while the terrain, water and navigation passes hold those
files.

The cost is that the overlay is not depth-tested. For a gizmo that is what every tool does. For the
ghost it is the right answer too: a preview you cannot see because a leaf is in front of it has
failed at its one job.

### The ghost asks the world, not the picture

`ui::world_probe` marches a ray against `world::WorldMap::sample` — a closed-form height function,
not a mesh — so "what is under the cursor" costs a few dozen evaluations and no device. That is
deliberately the *same* query the terrain mesher, the scatter pass and `entity::findPlacement` use;
spec §3 forbids a second piece of terrain logic per consumer, and the fastest way to acquire one is
to write a private raycast because the shared query was inconvenient. When the terrain pass lands
its consolidated surface (`heightAt`/`slopeAt`/`isWater`/`isOccupied`/`nearestValidPoint`),
`GroundSample` is what should be filled from it: every field in it is one of those queries.

The GPU picker remains the authority for a *click*. It is exact and it can see procedural instances
and terrain chunks that no node owns. One blocking read on a click is fine; sixty a second is not.

### Undo is a record of the edit

`ui::EditHistory`. A command is a list of parameter base values (before and after), node parents
(before and after), and the nodes that entered or left. Undo is the same list with the two sides
exchanged. Nothing about an operation has to be re-derived to reverse it, which is the property that
pays for the model: *"move these eleven rocks four metres east"* is undone by writing eleven vectors,
not by working out what the opposite of a drag is.

**A departing node is kept, not described.** The command owns the `CompositionNode` while it is out
of the scene. Serialising it would mean a delete-then-undo silently losing whatever the scene-file
format does not write — a loaded glTF asset, a nested child composition, a terrain's built chunks —
and losing it in the one situation where the user is most certain that nothing happened.

A drag coalesces: `beginDrag` captures the before values, the gizmo writes freely for as long as the
mouse is down, `commitDrag` reads them again and pushes one command. Sixty frames of writing, one
thing to undo — and a drag that ends where it began pushes nothing.

### A group is a node

`NodeKind::Group` is an empty transform. Parenting, the world transform, the `"parent"` field in the
scene file, and the group's own keyframeable, modulatable, serialised position all existed already;
this is the node kind that had been missing to use them. The members stay ordinary nodes you can
select and edit one at a time, which is §26's actual requirement — *"the group moves as a unit and
remains editable"*.

Grouping and ungrouping recompute each member's local transform so that **nothing moves**. A
grouping operation that shifted the objects being grouped is one nobody can use.

### Both stores are written, always

`ui::setBaseComponents` writes the parameter's base value, its final value, *and*
`CompositionNode::transform`. And `Composition::toJson` now takes position and scale from the
parameter when there is one, as it already did for rotation.

Two stores for one value is survivable only if every writer writes both. Writing only the parameter
means a node that is deleted and undone comes back where it was before it was ever moved; writing
only the transform means the project's `parameters` block overrides it on the next load. The second
of those has cost this project time twice already, from the other direction.

### The terrain is memoised

`CompositionNode::TerrainProducts` keeps the scatter clouds, the glow clusters and the chunk meshes
the last flatten produced, keyed on a hash of exactly the inputs they are a function of: the world
map, the terrain settings, the ecology, and the two ecology-light settings. A rebuild caused by
anything else reuses them. Chunk mesh ids are stored relative to the terrain's first mesh and
rebased on reuse, because the absolute ids move whenever anything flattens a mesh before the
terrain — which is exactly what placing an asset does.

Nothing has to remember to invalidate it: an edit that changes the terrain moves the key.

### The decisions are ImGui-free

`edit_history`, `world_edit`, `world_probe`, `brush`, `gizmo` and `world_editor` contain no ImGui and
are on the unit-test target. `viewport_overlay` and `world_edit_panel` are the pixels. That split is
what makes "click-drag on the X arrow moves the selection four metres east" a thing a test can
assert, in a repository where nobody can take a screenshot of it.

What the split does not reach is the *wiring* — SDL to ImGui to the canvas's hover state to the
ghost to the placement. So `--ui-script edit` drives it through the pointer, through the real event
queue, and prints what the scene did.

## Rationale

**Why not snapshot undo (C).** The cost is proportional to the scene and the benefit is proportional
to the edit, and in this editor those differ by five orders of magnitude. A command record is more
code exactly once.

**Why not a group side table (E).** It would be a second hierarchy that the renderer, the
serialiser, the parameter system and the timeline all knew nothing about. `setParent` already builds
the world transform as parent × local.

**Why not scene-drawn gizmos (A).** Three reasons, any one of which would have been enough: it needs
renderer and shader changes that other passes own; it puts editor geometry in a scene an offline
render reads back from file; and hit-testing would then have to agree with a projection computed
somewhere else, rather than being the same projection that drew the handle.

**Why the axis drag resolves a ray and not a mouse delta.** A drag along an axis is the point on the
axis line closest to the cursor *ray*. Projecting the mouse delta onto the projected axis is the
obvious shortcut and it is wrong as soon as the axis is not parallel to the screen: on a top-down
view the green handle moves a few centimetres for a full sweep of the screen, and everybody blames
the mouse.

## Consequences

- **Measured, on `examples/recipes/glowmere.recipe.json`.** A structural edit re-flattens in
  **19.6–23.7 ms** where it took **114.9–134.5 ms** before; `engine.update`'s p99 across a scripted
  paint stroke falls from **120.3 ms to 20.4 ms**. `AVGEN_NO_TERRAIN_CACHE=1` restores the old
  behaviour so the comparison stays checkable.
- The cold flatten is unchanged (~271 ms): there is nothing to reuse the first time.
- The cache costs memory — a second copy of the terrain's chunk meshes, tens of megabytes on a
  Glowmere world. It buys back a third of a second per edit.
- A brush stroke is one undo step, however many dabs it laid down, and one `rebind()` per dab rather
  than one per plant.
- The history owns deleted nodes, so it is bounded (256 commands) and cleared when a project loads.
  A history that describes a world that is gone is worse than no history.
- `NodeKind::Group` is a scene-file format addition. A scene written by this build and opened by an
  older one fails with "unknown node kind 'group'", which is the correct outcome: the alternative is
  an arrangement that silently comes apart.
- Two modes, not §42's five. Select and Place are this pass's; Terrain and Water belong to the
  passes that own them and would be empty rooms here. Camera is not a mode at all — the navigation
  gestures work in every mode, which is what an artist expects and one fewer state to be stuck in.

## Rejected alternatives

- **A rendered thumbnail per asset in the palette** (§19). It needs an offscreen pass and a texture
  cache this pass does not own. The palette shows a category-coloured swatch whose bar height is the
  asset's real height against the tallest in view, and a ring on anything that glows — which sorts
  the palette visually and says the two things a placement gets wrong most often. It does not
  pretend to be a picture of the asset.
- **A Stamp brush mode** (§20). It needs a saved-arrangement library, which is a feature rather than
  a mode. Single, Scatter, Cluster, Landmark, Eraser and Replace ship; `PlacementMode` takes another.
- **Replacing the GPU picker with CPU box picking.** A box is not a silhouette. The CPU probe does
  box selection and the brush's collision term, where a box is the right shape; a click still gets
  the exact answer.

## Revisit triggers

- **The terrain pass lands its consolidated spatial queries.** `ui::GroundSample` should be filled
  from them; the march is the only part that should remain here.
- **A scene grows enough nodes that the brush's per-frame `nodeBounds` sweep shows up.** It is O(nodes)
  per frame while painting, against a node list that is hundreds today. A spatial index is the fix,
  and `world::scatter`'s own cloud structure is the precedent.
- **Somebody wants undo across a Generate World.** It is deliberately outside the history: a
  generate replaces the composer's own nodes wholesale, and the honest answer today is that the
  history is cleared.
- **The remaining ~20 ms flatten becomes the complaint.** The terrain is no longer in it; what is
  left is procedural regeneration and entity rebuilding, and `dirty_` still has no granularity.
