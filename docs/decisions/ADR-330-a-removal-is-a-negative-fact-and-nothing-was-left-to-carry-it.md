# ADR-330: A removal is a negative fact, and nothing was left to carry it

**Status:** Accepted
**Date:** 2026-09-18

> my changes to hero selection, removal of objects from the scene, etc

Measured on the owner's own project, `examples/world/glowmere-valley-2-multicam.json`:

```
deleted 'chicken-17': 80 -> 79 -> 80
CHECK( nodeCount(fresh) == authored - 1 )  ->  80 == 79
```

Eighty nodes, seventy-nine after the delete, **eighty again after a save and a reload**. The object
came back, so it was in every frame of every export — and an offline render builds its own `Engine`
and loads the *project document* (`startRenderFromUi` saves the project for you and then renders
that file), so the deletion never reached a deliverable at all.

Hero selection was the third instance and is fixed (ADR-276). This is the fourth, and it is the
half of the owner's report that survived.

---

## 1. The mechanism is the family's, unchanged

A project whose scene came from a file saves that scene **by reference** — `assets.scene.path` plus
a hash of bytes already on disk — and nothing but a "Save Scene As..." dialog or `--save-scene`
writes a scene file. So an edit that lands in the `Composition` rather than in a parameter lives in
the window the person is looking at and in no document any render reads.

ADR-207 found it for world effects, ADR-230 for atmospheric effects ("the save kept every number of
the aurora and lost the aurora"), ADR-276 for heroes. This is the node set.

`doc["nodes"]` exists in a project document and was never a record of anything: it is read by
`Engine::bundleProject`'s asset walk, on a *scene* document, to rewrite asset paths.

## 2. Why it is harder than its three siblings

The three siblings are small lists a project can hold a copy of. A deletion is not a list. It is a
**negative fact**: there is no node left to carry it, so the only thing that can record it is the
absence itself, measured against the file. Three cases have to come out right at once, and the
choice of record is exactly what decides whether they do:

| the session did | what must reload |
| --- | --- |
| deleted a node the scene has | the node is gone |
| added a node the scene never had | the node is there, with its own numbers |
| added a node and then deleted it | nothing at all, and the file is byte-stable |

### The record is a difference, by name

**By name**, because where a node *is* already lives in the project's `parameters` block — that is
ADR-271's boundary, settled and not reopened here — and a second copy of a moved node's position
would be a second answer to one question. What the project owes is the **set**: which objects there
are. A node dragged across the valley produces no record at all, and an arm asserts that.

**A difference rather than a copy**, for two reasons. A copy makes the scene file dead for the
project that holds it: the next correction anybody makes to a shared scene — and
`glowmere-stylized.scene.json` backs three projects — would never reach it again. And a copy is what
makes the third row above go wrong: a project that wrote its live list would write eighty nodes for
a session that changed nothing in the end, and it would pass the removal and addition arms while
doing it.

### The direction that has to fail safe

`nodeEditsAgainst` returns null when the scene document cannot be read or is not an object. "I could
not read the file" is not evidence that eighty nodes were added, and recording the difference
against nothing would bake a copy of the whole world into the project — worse than the defect, and
indistinguishable from a working fix from the addition arm.

Its two siblings above it in `saveProject` choose the *opposite* direction for the same reason: an
unreadable scene means the session's effect list is recorded, because a scene this build cannot read
is not evidence that the render already has what the window has. Both are "record what you can
defend"; for a list the defensible thing is the session's copy, and for a difference it is nothing.

## 3. Why the splice happens before the parse

`Composition::loadFile` now takes the record and applies it to the scene **document**, before
`fromJsonImpl` ever sees it. That buys one invariant:

> the composition the engine ends up with is exactly the one the parser would build from a scene file
> with those edits made.

One code path for bringing a node into being — with its parent, its forward references, its nested
scene files, its assets resolved against the scene's own folder, its procedural and particle
payloads. The alternative was to extract a single-node parser out of a five-hundred-line loop, or to
move nodes between compositions, and either would be a second set of rules to keep in step with the
first.

A removal reproduces `Composition::detachNode` rather than merely dropping the entry: a child of the
removed node keeps its local transform under the grandparent. That is the editor's own semantics,
and "delete a group and its contents jump" would be a worse bug than the one this fixes. The editor
deletes whole subtrees (`ui::deleteNodes` expands through `withDescendants`), so it is the object
list's own remove button — `Engine::removeNode` — that reaches this, and an arm covers it.

The edits reach the **root** scene only. A nested scene file is another document with its own
authorship, and a project that could reach into one would be editing a file it never opened.

Nothing is written or applied for an inlined composition: `assets.scene.inline` *is*
`Composition::toJson` and already carries the node list. A difference beside it would be a second
answer, which is the same reason `heroes` and the two effect lists are not written there either.

## 4. Ordering on load

The record is applied while the scene is being built, which puts it before everything. Not taste:
the `parameters` block carries `nodes/<name>/...` values, a hero names a node, a `WorldEffect` names
a hero. Nodes are the most structural thing a project can say, so they go first, and the rest of
`loadProject`'s ordering is unchanged.

## 5. The arms

`tests/unit/test_node_project_round_trip.cpp`, each verified against main's behaviour by putting
back the one line that writes the record — three of five cases fail:

* **A — the control.** An untouched project writes no `sceneNodes` key. Without it, B would pass on
  a save that wrote the node list into every project in the repository.
* **B — removal.** Delete, save, reload: gone, and the survivors are the right ones.
  (`REQUIRE( doc.contains("sceneNodes") ) -> false` before.)
* **C — addition.** Add, save, reload: there, with its kind and its position. Broken the same way
  and fixed by the same record; the owner's "etc" was real.
* **D — added, then deleted.** No key at all. This is the arm that says what the record *is*; a copy
  of the live list passes B and C and fails here. It cannot fail against main either, by
  construction — main writes nothing — and it is in the suite to forbid an implementation rather
  than to catch the reported defect.
* **E — reparenting.** `REQUIRE( back.names().size() == 2 ) -> 3 == 2` before.
* **F — ADR-271's rule.** The scene file is byte-for-byte unchanged by any of it.
* **G — the decision alone**, on documents with no engine near them, including the unreadable-scene
  direction and the ordering of removals before additions.

`tests/integration/test_project_node_set.cpp` is the field arm on the owner's own film, and it is
worth its two project loads for something the fixture cannot cover: a real world has terrain,
ecology, a graph and nested scenes, and the thing that would break this quietly is a node the
composition holds that the scene file does not list. Such a node would be recorded as an addition by
an *untouched* save and would arrive twice on the next load. It is not: saving the eighty-node world
unedited writes no record, which is the evidence that `Composition::toJson`'s graph-node exclusion
is the whole of the exception.

## Consequences

- `src/scene/composition.{hpp,cpp}`: `nodeEditsAgainst`, `applyNodeEdits`, and a `loadFile` overload
  that takes the record.
- `src/app/engine.{hpp,cpp}`: `saveProject` writes `sceneNodes`; `loadComposition` takes the record
  and `loadProject` hands it over.
- `tests/unit/test_node_project_round_trip.cpp`, `tests/integration/test_project_node_set.cpp`.
- No shipped project changes: every one of them saves unedited to the same bytes.

## Revisit triggers

- **An added node whose asset is an absolute path.** `Composition::toJson` writes the asset as the
  composition holds it, and the editor does not rebase on add the way `saveComposition` does. A
  project moved to another machine would lose such a node's mesh, and `bundleProject` does not walk
  `sceneNodes`. Rebasing on save is the fix, and it wants a case that moves a project.
- **The World Builder, and anything else that adds nodes wholesale.** `app::WorldBuilder` composes a
  world by calling `Composition::addNode` for every ecology layer and hero it places. Save the
  *project* without saving the scene and every one of those lands in `added` — which is correct, and
  is the same defect pointing the other way if it did not, but it puts a scene-sized node list in a
  project file. The intended workflow is "Save Scene As..." after a generate, and that empties the
  record by construction. If generated worlds start arriving in projects, the answer is to make the
  generate offer to write a scene rather than to make this record smarter.
- **Anything that makes the editor write a scene file.** Then re-authoring becomes reachable and
  ADR-271's question is open again, with this record as the thing to delete rather than extend.
- **A node edit inside a nested composition.** Deliberately out of reach today. If the editor ever
  offers one, the record needs a path rather than a name, and the "one code path" argument in §3 is
  the thing to preserve while giving it one.
