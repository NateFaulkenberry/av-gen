# ADR-2156: one asset identity, anchored to the project; projects by path; the import reports the engine's load

- Status: Accepted (2026-11-25), eng/engine-api-s3 (Engine API stage S3; 113-engine-architecture, plan in
  03-phase0-api-contracts/01-api-contracts.md section 3 and 04-poc-plan.md stage S3)
- Code: `api/path_policy.{hpp,cpp}` (`resolveAssetId`, `assetIdFor`), `assets/asset_catalog.{hpp,cpp}`
  (`ManifestEntry`, `AssetManifest`, `readManifest`, `projectAssetIdForManifestPath`; the catalogue's ids),
  `ai/engine_tools.cpp` (`project.create/open/save_as/list`, `asset.list/search/get/import`, `scene.create_node`;
  `projectTarget`, `importSummaryJson`, `summariseModel`, `addContract`), `api/session.cpp`
  (`referencedOutside` reads `asset://project/` strings), `api/version.hpp` (`kApiLevel` 2)
- Artefacts: `schemas/tool-catalogue.json`, `schemas/tool-catalogue-baseline.json` (rewritten at level 2),
  `schemas/tools-without-output-schema.json` (five tools leave it), `docs/api/tools.md`
- Docs: `docs/api/transactions.md` (the project and asset rows)
- Tests: `tests/unit/test_api_assets.cpp` (`[api][assets]`); `test_asset_catalog.cpp`, `test_ai_tools.cpp` and
  `test_api_session.cpp` updated for the path inputs and the catalogue's ids

## Context

Measured at 03de05086 (01-api-contracts.md section 3.1 has the long form):

- `asset.import` returned `asset://project/models/column` for `column.glb`. `AssetRegistry::resolve` maps that to
  `<base>/assets/models/column`, which does not exist. Its own duplicate branch returned the id **with** the
  extension. One tool, two spellings, and the common one resolved to nothing.
- `scene.create_node` took a path only. S1 had routed it through `PathPolicy`, which refused an `asset://` id
  with `Unsupported`, so PoC step 3 (`scene.create_node {asset: <the id step 2 returned>}`) could not work.
- The project tools took a `name` only, so an external client could not say where its project lives, and
  `project.create` made no scene: the first `scene.create_node` had no composition to go in.
- The manifest (`avgen-project-assets` v1) kept `{id, type, path, sha256}`: no size, nothing about where the
  file came from, and nothing about what the engine made of it.
- The catalogue (`catalogAssets`) invented ids nothing resolves: `asset://builtin/<path under some read root>`
  for anything outside the project (the registry reads `asset://builtin/` as relative to the scene's folder), and
  `asset://project/assets/models/x.glb` (the `assets/` doubled) for a project asset the manifest did not list.

## Decision

1. **One id, one resolver.** A project asset's id is `asset://project/<path under <projectDir>/assets, with
   extension>`, the form `AssetRegistry::assetId` produces. `PathPolicy::resolveAssetId` resolves it with
   `AssetRegistry::resolve` on a private registry anchored to the **open project's folder**, holds the canonical
   result to `<project>/assets` (so `..` and a symlink that leaves the folder are `PermissionDenied`), and then
   applies the same root test and existence check as any path. Every tool that takes a file therefore takes an id
   or a path -- a relative path resolves against the project folder (S1's rule) -- and no tool resolves anything
   itself. `PathPolicy::assetIdFor(file)` is the inverse, used by every tool that reports an id. With no project
   open an id is `FailedPrecondition`; another scheme is `Unsupported` (`asset://engine/` stays reserved).
2. **The project anchor is the project folder, and it is cheap.** The tools' ids are anchored by `PathPolicy`, not
   by the engine's registry (whose base is the scene file's folder, `Engine::loadComposition`), so no `Engine`
   change was needed. Where the two can disagree -- a project whose scene file lives in another folder --
   `scene.create_node` stores the node's asset as the id only when the engine's registry reads that id back as the
   same file (every project `project.create` makes), and as the absolute file otherwise, which `saveComposition`
   rebases. The scene file's own `asset://project/` strings keep meaning the scene folder's assets: that is the
   scene format, and is the remaining gap (Consequences).
3. **Projects by path.** `project.create {path, scene: "empty" | "none"}`, `project.open {path}` and
   `project.save_as {path}`. A bare name (no `/`, no `.json`) is `<first write root>/<name>/<name>.json` -- what
   Studio's assistant sends, so its behaviour is unchanged; a `.json` path is the project file; any other path is
   a folder whose file is `<folder>/<folder name>.json`; everything but a bare name goes through `PathPolicy`.
   `scene: "empty"` starts an empty composition (`Engine::newComposition`) and saves it as `<stem>.scene.json`
   beside the project before the project is saved, so the project names it and the engine's registry base is the
   project folder. `"none"` (the default) is the old behaviour. `project.list` lists every write root.
4. **`asset.import` returns the id and reports the engine's load.** The id (both branches) is the canonical one.
   A model is loaded after it is copied, by the engine's loader on a private `AssetRegistry` on a worker thread,
   and the call fails -- and the compensation log removes the copies -- if it does not load. `import` is
   `GltfLoadSummary` (meshes, entities, materials, textures, lights, cameras, rigs, joints, clips, bounds,
   warnings) plus what an author checks a file against: `triangles`, `entityNames`, `materialNames`, `rigDetail`
   (joint names) and `clipDetail` (name, seconds, rig). An optional `source {file, tool, toolVersion,
   exportProfile, exportSettingsSha256}` is recorded. `file` is now required.
5. **Manifest version 2**: `{id, type, path, size, sha256, source?, import?}`. Version 1 is read: each entry's id
   is what its `path` says (the extensionless id is corrected, not aliased), `size` is the file's; the next write
   is version 2. Unknown keys round-trip at both levels. A manifest that cannot be read is `FailedPrecondition`
   and is never overwritten. The manifest is read before anything is copied.
6. **The catalogue's ids are resolvable or absent.** A file under `<project>/assets` gets its canonical id;
   anything else is named by its absolute path, which every tool accepts. `type` is always the catalogue's
   vocabulary (`model`, not the manifest's `models`), so Studio's asset panel filters v1 and v2 entries alike.
   `asset.list/search/get` walk the read roots and the open project's folder when the session may read it.
7. **Output schemas** for `asset.import`, `asset.get`, `project.create`, `project.list` and `scene.create_node`;
   `scene.create_node` reports `asset {id, path}` and `bounds {valid, min, max, size}`.
8. **API level 2.** `project.create/open/save_as` lost `name` and gained a required `path`; `asset.import` gained
   a required `file`. The baseline is rewritten; those four tools are `since: 2`. No aliases (ADR-441).

## Staging

The owner's rule is "newly imported files should be staged until the transaction commits, where practical". The
import copies each file under a dot-name (`.column.glb.importing`) and renames it into place, so a half-written
copy never sits at the name an id resolves to, and the catalogue skips dot-names. **Promotion at commit was not
built**: the id `asset.import` returns has to resolve inside the same transaction (PoC step 3 makes a node from
it before anything commits), and a staged file would need every resolver, the engine's loader included, to look
in the staging area first -- a second resolution path, which is what this ADR removes. The compensation log
(ADR-2153) is the rollback: created copies are deleted, the manifest's previous bytes come back, and a file
another transaction, a saved project or the restored scene depends on is kept.

`Session::referencedOutside` (ADR-2153 rule 2) only knew paths. A project saved inside the transaction names the
node's asset by its id, so a rollback deleted the file the saved project pointed at. It now resolves an
`asset://project/` string through `AssetRegistry::resolve` against the project, scene and file folders.

## Alternatives considered

1. **Anchor the engine's registry to the project folder** (a separate project anchor in `AssetRegistry` and
   `Engine`). Rejected for S3: it changes what `asset://project/` means inside every shipped scene file whose scene
   is not in its project folder, and `Engine` is S1b's. The tools' anchor in `PathPolicy` gives the PoC and every
   created project one meaning without it.
2. **Accept the old extensionless ids.** Rejected (ADR-441): the manifest reader corrects them by path.
3. **Make `asset://builtin/` resolvable.** Rejected: it means three things today (relative to the scene folder in
   the registry, relative to a read root in the catalogue, and nothing in the tools). Outside the project a file is
   named by its path.
4. **Compute the summary from the source before copying.** Rejected: loading the copy proves the project's own
   file (and a `.gltf`'s copied dependencies) loads.

## Consequences

- An `asset.import` of a file that does not load now fails; it used to copy any bytes with a model extension.
- The summary's load blocks the host thread while it runs (it is on a worker only so the engine's registry is
  never shared): a tool body runs on the host, and the Session has no phase that runs one anywhere else (S6).
- A relative `project.open` path resolves against the open project's folder, as every relative path does; a
  second project is opened by its bare name or an absolute path.
- **Gap:** a project whose scene file lives outside the project folder. The tools still resolve ids against the
  project and nodes store the file; but a scene author's `asset://project/` string in that scene means the scene
  folder's assets.
- `asset.list` ids for non-project files changed from `asset://builtin/...` to the absolute path.

## Revisit triggers

- S6 jobs: run the import's load off the host thread entirely.
- A post-PoC project anchor in the engine's registry (01-api-contracts.md section 3.2).
- `asset://engine/` for installed engine resources.
