# ADR-2157: the scene is edited as a document; a project is validated on a private engine

- Status: Accepted (2026-11-25), eng/engine-api-s4 (Engine API stage S4; 113-engine-architecture, plan in
  03-phase0-api-contracts/02-session-api.md section 7 and 04-poc-plan.md stage S4)
- Code: `api/document_tools.cpp` (`document.get`, `document.validate`, `document.patch`, `project.validate`),
  `api/warning_codes.hpp` (the finding codes), `api/describe.cpp` (`errorCatalogue` gains `findings`;
  `errorsMarkdown` publishes them), `api/session.{hpp,cpp}` (`registerDocumentTools` in `registerAllTools`)
- Artefacts: `schemas/tool-catalogue.json`, `docs/api/tools.md`, `docs/api/errors.md` (the finding table)
- Docs: `docs/api/transactions.md` (the `document.*` rows)
- Tests: `tests/unit/test_api_documents.cpp` (`[api][documents]`, `[adr2157]`; D5 is `[examples]`; E2 is
  `[api][catalogue]`)

## Context

The PoC client has to build a scene -- a procedural floor, a key and a rim light, a tint on a node -- without
writing a file. The tool surface had a tool per object kind for a few kinds (`scene.create_node`,
`field.create`) and nothing for lights, a node's tint or anything else the scene document holds. The owner's
rule (spec section 4, S4): one coherent document-editing contract rather than a structural tool per scene
object; dry runs; overrides preserved across rebuilds; unrelated fields and forward-compatible data preserved.

The pieces existed. `Composition::toJson` is lossless since ADR-2151; `Engine::setCompositionJson` is the
assistant's rollback path; the schemas (ADR-2152) are generated from the parsers' key lists; the pinned nlohmann
has `json::patch` (RFC 6902), verified in S0b. Two facts found reading the code decided the design:

- `Engine::installComposition` (inside `setCompositionJson`) calls `params_.clear()` and
  `modulator_.clearRoutes()`. A rebuild drops every route; `SnapshotStore::applyDocument` only gets away with it
  because it reloads the routes from its document. A patch has to put them back itself.
- Re-applying the overrides blindly would undo the patch. `toJson` writes a node's transform, a light's
  intensity and the camera from their live parameters, so a patch that moves a node, applied over a session
  that had moved it earlier, would be moved back by its own override. `post` is the opposite: `toJson` writes
  the block as read, so a `post` override exists only in the parameters and must be re-applied or it is lost.

## Decision

1. **`document.patch {document: "scene", patch, dryRun?}`** applies RFC 6902 to `toJson()` and rebuilds with
   `setCompositionJson`, in the plan's order (the file header has it step by step). It is `structural`,
   `undoable` and `mutatesProject`, so it runs in the Session's transaction (implicit, or the caller's) and a
   live host refuses it during a show.
2. **Strict on the API path.** A patch is refused with `INVALID_ARGUMENTS` and `details.pointer` (and
   `details.findings`) when an operation does not apply (each operation is applied alone, so the failure names
   it), when the result has a schema error the document did not have, or when it adds a key the engine does not
   read. "Does not read" is the schema's `properties` wherever the schema types an object -- the parsers' own
   key lists -- through `$ref`s and the matching `anyOf` form; an `x-avgen-untyped` block or an untyped node
   kind is not judged, and `_` keys are always allowed. Only NEW findings count, so a document that already
   carried an unknown top-level key (kept by ADR-2151) can still be patched. Files on disk stay warn-only.
3. **Overrides: re-applied unless the patch decided the value.** The overrides
   (`params::overrideParameters` against the basis) and the routes are captured, the composition rebuilt, the
   routes reloaded, then each override whose parameter still exists and whose rebuilt value differs is
   re-applied -- if re-applying it leaves `toJson()` equal to the document the rebuild produced. Otherwise the
   document carries that value and the patch set it, so the patch wins. All overrides are tried at once first
   (one `toJson`); only when that changes the document is each decided alone. The result reports
   `overrides.{kept, yielded, dropped}`. No table of which parameter lives in which block is needed, and the rule
   is decided by the same function that defines the document. The basis is not retaken (ADR-2132), so a kept
   override is still an override at the next project save.
4. **A failure changes nothing.** `Composition::fromJson` fails before the install, so the old composition stays
   installed (the test checks the pointer and the hash). A failure inside the install (after the parameters were
   cleared) restores a snapshot captured just before it, so this holds inside an explicit transaction too, where
   a failed call does not roll the transaction back.
5. **`dryRun`** stops after the checks, parses the patched document on a private Offline engine with its own
   `AssetRegistry` (based where the session's is, so ids and relative paths resolve the same), and returns the
   changed paths, the parameters that would appear and disappear and the predicted hash. It installs nothing.
6. **`document.get {document}`** returns `toJson()` for the scene, `projectDocument` for the project (for the
   project's own path, or a scratch path when it was never saved; it writes nothing), and a content hash
   (`sha256:` of the canonical dump). **`document.validate {document, content}`** checks a document against the
   generated schema, then builds it on a private engine: a scene into a composition (plus its node assets), a
   project loaded from a scratch copy whose relative asset references are made absolute against the open
   project's folder.
7. **`project.validate {path?}`** loads the project on a private Offline engine (`loadProject`,
   `requireProjectScene`, `auditRoutes`, as `avgen --audit-routes` does), checks the project and its scene file
   against the schemas, resolves every asset reference, and maps what it finds to codes. With no `path` it
   validates the session's project as it is in memory, through `writeProjectCopy` into a scratch folder.
8. **One finding table**, `api/warning_codes.hpp`: `{severity, code, pointer, message, document}`, each code
   with one severity, built from an enum so nothing outside the table can be emitted. `errorCatalogue` publishes
   it beside the error codes, so `docs/api/errors.md` carries it through the S0a generator and T1 holds it to the
   code. Codes: `schema.type|required|enum|range|invalid`, `scene.unknownKey`, `scene.parse`, `scene.missing`,
   `project.parse`, `project.unknownKey`, `project.warning`, `asset.missing`, `asset.relinked`,
   `parameter.unknown`, `route.dead`, `route.hazard`. `route.dead` is a warning: a dead route is a fault in the
   piece, not in the document, and shipped examples have them.
9. **The validators are `job`s**, read-only: each builds a whole private engine on the host thread (a project
   load includes its audio analysis), so a live host refuses them during a show as it refuses the Director's
   scratch-copy evaluations.

## Rationale

- A patch over the document the engine itself writes and reads is the smallest contract that covers every
  object a scene can hold, now and as the schema grows. A tool per object is a second writer per object.
- "An override yields where re-applying it would change the patched document" is the only rule found that needs
  no per-parameter knowledge. The alternatives each need a map from parameter paths to document pointers, which
  nothing in the engine has and every new parameter would have to extend.
- Strictness on new findings only keeps the API path strict for what the client did without making a document
  the engine already accepts unpatchable.

## Consequences

- Reproduced evidence 05: the evaluation's poc-today folder holds only the fixed `column.glb` (the Blender script
  was corrected and the model re-exported), so the test rebuilds the bug with `gltf_fixture.hpp` (a column with
  no emissive material) and checks `route.dead` with rule `node-emits-nothing`; the same project with an emissive
  model reports the route live.
- D5: every shipped example project is validated. Measured here (asset links in place): 251 projects, 239 with no
  error, 12 whose only errors are `asset.missing` for gitignored files (generated `scene/` files of Aurora, the
  Rift, Musical Geography and Rainbow World; the CHRONOS souls). Those 12 are an explicit list in the test; a
  missing asset in any other project, or any other error, fails it, and the clean count has a floor of 230. The
  sweep loads every project with its audio analysis and takes about 5 minutes of wall time (10 of CPU) on a loaded
  machine: the cost of the CPU suite's largest single case.
- A rebuild restarts entity simulations and costs what a rebuild costs (620-942 ms cold on the large worlds;
  milliseconds on the PoC scene).
- `document.validate` of a project resolves the references `projectAssetRefs` names (audio, environment, scene,
  audio clips, stems); a shader layer path in a project sent as content is resolved against the scratch folder,
  so it is reported missing.

## Rejected alternatives

- **Re-apply every override after the rebuild** (what `applyDocument` does with its whole parameter half). It
  reverts a patch that moves a node the session had moved.
- **Re-apply none** (rely on `toJson` carrying the live values). It loses every `post` override.
- **A structural tool per object** (`light.create`, `node.set_tint`, ...). The owner ruled it out, and each would
  be a second writer for a key `toJson` already writes.
- **Refuse every unknown key in the patched document.** A scene carrying a kept top-level key from a later build
  could never be patched.

## Revisit triggers

- Nodes gain a key list (ADR-2151, ADR-2152): node keys are judged by the schema already; an untyped node kind
  becomes judged when its kind is typed.
- A patch is too slow on a large world: patch the live composition in place for the keys that have a setter,
  falling back to the rebuild.
- `engine.requires` load checks land: `feature.missing` joins the finding table.
