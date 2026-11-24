# ADR-2151: a scene save keeps what it does not read, and writes back everything it does

- Status: Accepted (2026-11-24), eng/engine-api-s0b-serial (Engine API stage S0b, the serialization half; the owner
  ruled lossless scene serialization a foundational correctness requirement)
- Code: `scene/composition.{hpp,cpp}`: `Composition::toJson`, `Composition::fromJsonImpl` (key reading),
  `keptKeys_`, `skippedNodes_`, `CompositionNode::keptKeys`, `Composition::Look::forms`
- Tests: `tests/integration/test_scene_lossless.cpp` (`[adr2151]`; the hidden `[scene-audit]` case prints the nested
  report)

## Context

`Composition::toJson` is more than File > Save Scene (`Engine::saveComposition`). It is the scene half of the
assistant's rollback snapshot (`ai::SnapshotStore::captureDocument`), restored through `Engine::setCompositionJson`,
and a future `document.patch` (toJson, patch, setCompositionJson, re-apply parameters) would be built on it. Whatever
`toJson` drops, a save deletes from the file and a rollback deletes from the session.

Measured on 414d47790:

- `kSceneKeys` lists 36 top-level keys; `toJson` wrote 34. `lightRig` is written as `environment.lightRig`, which the
  parser also reads. `looks` (ADR-1886) was read and never written, so a save of `strange-attractor-flythrough`
  deleted its dusk looks.
- Nothing kept a key the parser does not read. ADR-278 warns about such keys and exempts `_` keys, but the warning
  was followed by deletion: 47 shipped scenes carry a top-level `_note`, and every save removed it.
- A sweep of all 277 shipped scenes (`examples/**/*.scene.json` without `_bench/`), loading each, writing it, and
  loading the result, found more:

| where | loss | scenes | done here |
|---|---|---|---|
| top level | `looks` never written | 1 | fixed: written in the form the parser reads |
| top level | unknown and `_` keys dropped | 47 (`_note`) | fixed: kept verbatim |
| `entities`, `entityProfiles` | a document with no file path (every rollback) resolved profiles against the process's folder, so the written document did not load: "cannot open entity profile '../entities/craft-lights.profile.json'" | 9 (Glowmere, tractor beam, alien-wander) | fixed: resolved through the registry, as `graph` and `materialPrograms` already were |
| `nodes[]` | a node whose asset is missing is skipped with a warning, and a save then deleted it from the file | 1 here (singularity-figures, 10 nodes without the gitignored souls); about 80 scenes name a gitignored asset | fixed: kept verbatim and written after the live nodes |
| `nodes[]._note` | dropped | 23 nodes in 9 scenes | fixed: a node's `_` keys are kept |
| `nodes[orb].material`, any `material.alphaMode` | read and drawn, written for terrains only and without `alphaMode` | 5 (the RendererQA scenes) | fixed: written whenever authored; `alphaMode` when not opaque |
| `environment.proceduralSkyBackground` | read, never written | 1 (tree-of-life-ocean-world) | fixed: written when on |
| `environment` volume block | `fogHeight`, `fogHeightFalloff`, `shadowCascades`, `volumeSteps` and the rest written only while `volumeDensity > 0`, though the surface fog reads the height layer and the shadows read the cascades without a march | 22 | fixed: written also when any value differs from the default |
| `portal.exit` | `setPortal` lifts the exit onto a generated ground; `toJson` wrote the lifted height, so each save raised it by the ground again (-7.4, then -22.8) | 1 (strange-attractor-flythrough) | fixed: written above the ground, as authored |

Reported, not fixed (outside this change's files or not small):

| where | loss | file |
|---|---|---|
| `nodes[terrain].world.biomes` | `"biomes": []` is written as absent, and a world read without the key gets the default biomes, so a save gives digital-mosh's two terrains the Glowmere biomes | `src/world/world_map.cpp:1335` (`worldMapToJson`): write the empty list |
| `nodes[].<unknown>` | a node key the parser does not read is dropped (the liminal scenes' `entity` block, 2 scenes, read by their build script). There is no node key list to tell unknown from known; only `_` keys are kept | `composition.cpp` node reader; needs the schema typing table's per-kind key list |
| `nodes[].rotation`, procedural `distributionTransform`/`sourceTransform`/`grammar.rules[].pre` rotations | kept as a quaternion and written as Euler degrees, so a component moves in its sixth figure every save, and 180 can come back as -180, or (-90, 86, -90) as (90, 94, 90). Same turn, different text | node transform writer; the procedural transforms belong to `procedural.*` |
| `Engine::saveComposition` to another folder | rebases live nodes' asset paths; a kept skipped node keeps the path it was written with | `src/app/engine.cpp` (`saveComposition`) |

## Alternatives considered

1. **Keep the whole loaded document and merge the live state over it on save.** Rejected. It cannot delete: a node
   removed in session, an emptied `looks` or a light turned back to its default would come back from the loaded
   copy. ADR-2132 ran into the same problem with project keys and chose a list of owned keys for that reason.
2. **Keep unknown keys only; fix nothing else.** Rejected. The sweep shows the known keys lose more than the unknown
   ones: a whole node, the portal height and a terrain's biomes, none of which is an unknown key.
3. **Keep the keys the parser does not read, and fix each writer that drops a key it does read.** Chosen.

## Decision

1. **Kept keys.** `fromJsonImpl` stores every top-level key that is not in `kSceneKeys`, and every `_`-prefixed key,
   verbatim in `keptKeys_`. `toJson` writes them last and never over a key it wrote itself. The guard is defensive:
   every key `toJson` writes is in `kSceneKeys` (a test holds it to that), so a kept key cannot collide with one.
2. **Programmatic compositions have none.** `newComposition`, `Composition(registry, name)` and every composition built
   in code start with an empty `keptKeys_`: there was no document, so there is nothing to keep, and nothing is
   invented. A composition rebuilt from a document (`setCompositionJson`, a rollback) gets that document's kept keys.
3. **`looks`** are written as `{"name", "from", "params"}`, the shape the parser reads. Each value keeps its authored
   form (`Look::forms`): `true` comes back as `true`, a number as a number, a one-element list as a list. A look
   built in code without forms writes one component as a number and more as a list.
4. **Nodes.** A node's `_` keys are kept (`CompositionNode::keptKeys`, not copied by `cloneNodeSpec`, because a
   duplicate is a new node). A node the load skipped (`addNode` refused it, almost always a missing asset) is kept
   verbatim (`skippedNodes_`) and written after the live nodes.
5. **The nested writers** above are fixed in place: entity profiles resolve through the registry for a document with
   no path, `material` is written whenever authored (with `alphaMode`), `proceduralSkyBackground` when on, the volume
   block whenever it is on or differs from `scene::Environment{}`, and a portal exit above its ground.
6. **Documented normalisations**, which the sweep allows and nothing else: a top-level `lightRig` is written as
   `environment.lightRig`; an empty list the parser reads (`"lights": []`, `"entities": []`, `"heroes": []`) is
   written as absent, which means the same; a value equal to its default is omitted, as every writer here already
   did; a node name is sanitised (`.`, `/` and space become `_`) and made unique; `environment.vortex` is migrated
   to an effect (ADR-441); numbers come back at float precision (`0.13` becomes `0.12999999523162842`, stable from
   then on).

## Rationale

The owner's rule: older builds may warn about fields they do not know, but must never silently destroy them on save.
A key this build does not read is somebody else's: a person's note, a build script's metadata, a later build's
block. The only safe thing to do with it is write it back. A key this build does read is this build's to write, so
a loss there is a writer bug, and the fix belongs in the writer (ADR-441: no shims, no translation layer).

The parameter half was checked against the code, not assumed. A node transform edited through its parameter (as
`parameter.set` does) is written by `toJson` from the live base, so `setCompositionJson` of that document keeps it.
A `post` value is not: `toJson` writes the post block the scene was read with, so `setCompositionJson` on its own
puts a post edit back, and `SnapshotStore::applyDocument` restores it by applying the parameter half again after
the composition, as it was written to. ADR-2132's basis is not retaken by `setCompositionJson`, so the moved node is
still an override in the next project save. The test pins all three.

## Consequences

- A save, a rollback and a document rebuild of every shipped scene keep every top-level key, modulo the
  normalisations above. A second round trip is stable apart from the two drifts in the reported table (rotation
  spelling and empty biomes). The sweep tolerates exactly those two shapes, by name, and prints how many it let
  through.
- Shipped scenes are not rewritten. A scene saved after this may gain keys it was already being read with: a volume
  block on a scene with the march off, an orb's material, `proceduralSkyBackground`. That is the file catching up
  with what was drawn.
- A project save (ADR-330's `sceneNodes`) compares `toJson()["nodes"]` with the scene file. A node skipped for a
  missing asset used to look deleted, so a project saved on a machine without the asset recorded its removal. It is
  now in the list and records nothing.
- The sweep loads every shipped scene twice and takes about two and a half minutes of CPU on this machine.

## Rejected alternatives

- Keeping the loaded document and merging over it (see 1 above): it cannot delete.
- Keeping unknown keys inside every known block now: each block's owner would need its own known-key list, and only
  the top level, `environment` and `environment.sky` have one (ADR-278). The schema typing table will record
  `writtenBack` per key; this ADR fixes what the sweep shows is lost today.
- Rewriting Euler degrees from the authored text when unchanged: a second copy of the rotation that has to be kept in
  step with the quaternion. The drift is reported instead.

## Revisit triggers

- The schema typing table lands: replace the hand-checked nested table with its `writtenBack` column, and give
  nodes a key list so unknown node keys can be kept like the top level's.
- `world_map.cpp` writes `"biomes": []`: remove `KnownDrift::EmptyBiomes` from the sweep.
- A node's rotation is stored as authored: remove `KnownDrift::Rotation`.
- `document.patch` is built: it must go through `toJson`/`setCompositionJson` and inherit this, not a second writer.
