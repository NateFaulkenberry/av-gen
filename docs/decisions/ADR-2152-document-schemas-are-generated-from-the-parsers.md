# ADR-2152: the document schemas are generated from the parsers' key lists, with one authored typing table

- Status: Accepted (2026-11-25), eng/engine-api-s0b-schema (Engine API stage S0b, the schema half; 113-engine-architecture)
- Code: `api/schema_types.hpp`, `api/schema_types_project.cpp` and `api/schema_types_scene.cpp` (the typing tables),
  `api/describe.{hpp,cpp}` (`projectSchema`, `sceneSchema`, the `--describe` contracts `schema:project`, `schema:scene`
  and `features`), `api/features.cpp` (`featureList`), `ai/tool_api.cpp` (`validateAgainstSchema`: `anyOf`, `const`,
  local `$ref`), `ai/capabilities.cpp` (`features`; stale domain prose), `app/engine_project.cpp`
  (`Engine::projectFileKeys`), and the enumerators beside the name functions they enumerate: `scene::allNodeKinds`,
  `allLightTypes`, `allLightRoles`, `allImportances`, `allPrimitiveKinds`, `allDistributionKinds`,
  `scene::postFileKeys`, `params::modOpNames` (and the curve, polarity, threshold and envelope names),
  `params::routeFileKeys`, `params::chainFileKeys`, `app::allRenderOutputs`, `app::renderOutputSpellings`
- Artefacts: `schemas/avgen-project.schema.json`, `schemas/avgen-scene.schema.json`
- Tests: `tests/unit/test_api_schemas.cpp` (`[api][schema]`), T1 and T2 in `tests/unit/test_api_catalogue.cpp`

## Context

ADR-2150 published the tool catalogue. The documents those tools edit, the project and the scene, had no
machine-readable form: `docs/project-format.md` is prose that nothing ties to the parser. An agent outside the
repository could learn which tools exist but not what a valid scene is.

The parsers already decide which keys exist: `kProjectKeys`, `kSceneKeys`, `kEnvironmentKeys`, `kSkyKeys`,
`kAuthoredLightKeys`, the rig's two lists, and the route and chain lists. What each key holds is decided nowhere but
in the code that reads it, wherever that code happens to need it. The owner's rule (spec section 5, "Schema
completeness"): generate what can be generated, and where authored typing is unavoidable, make missing or incomplete
typing visible and testable.

## Decision

1. **Completeness is generated, typing is one table.** Each object a key list describes is built from that list.
   Each key's type is one entry `{pointer, fragment, writtenBack, note}` in `schema_types_project.cpp` or
   `schema_types_scene.cpp`, the only authored part. The pointer is a JSON pointer into the document, with `*` for
   every element of an array (`/lights/*/castsShadow`).
2. **Every key has an entry, typed or explicitly not.** A block the Engine API PoC does not need is
   `{"x-avgen-untyped": true}`, so "not typed yet" is published, not implied by an absence. D2 holds the table and
   the key lists to a bijection. A key added to `kSceneKeys` without an entry fails with the key, its list and the
   file to edit.
3. **Typed deeply, now, only for the PoC.**

   | block | typing | why |
   |---|---|---|
   | project `format`, `version`, `app` | typed | the envelope; `version` 1..`kProjectFormatVersion` |
   | project `parameters` | an open object of path -> value | a value's type is the registered parameter's |
   | project `routes` | typed: `route` and `chain`, every key of both lists | PoC; `op`, `polarity`, `curve`, `threshold`, `envelope` from the code's name tables |
   | project `assets` | typed: `audio`, `audioClips`, `stems`, `scene`, `environment` as `assetRef` | PoC |
   | project `render` | typed; `output` from `renderOutputSpellings` | PoC |
   | project `sceneNodes` | typed: `added` (the scene's node definition), `removed` | PoC |
   | the other 32 project keys | untyped | not needed yet |
   | scene `format`, `version`, `name`, `camera`, `nav*`, `entityProfiles` | typed | PoC |
   | scene `lights[]`, `lightRig` (top level and `environment.lightRig`) | typed: every authored-light key, a rig path or the rig and its lights | PoC |
   | scene `environment` | 66 of 70 keys typed; `dayNight`, `tileDynamics`, `bands`, `vortex` untyped | scalars and `sky`, as the plan asks |
   | scene `environment.sky` | typed but `cirrus` | PoC |
   | scene `post` | typed, generated from `scene::postFileKeys()` (109 keys) | the reader's own tables |
   | scene `nodes[]` | the keys every kind reads; `gltf` (`lod` typed), `group`, `procedural` (a `box` source, a `single` distribution, the material) | PoC; every other kind, source and distribution is untyped |
   | scene `wind` | an object, untyped | not needed yet |
   | the other 22 scene keys | untyped | not needed yet |

4. **Enums come from the code.** No list of names is written in a table. Where only a name function existed, an
   enumerator was added beside it and the `FromName` now loops over it, so the reader and the schema share one list.
   `post`'s keys come from `applyPostJson`'s own tables, moved to file scope.
5. **"Valid" means the engine reads the value as written**, without refusing it or warning about it. A value the
   parser silently ignores (a colour with four components where it reads three) is outside the type, though the file
   still loads.
6. **Unknown keys.** Both roots are `additionalProperties: true`, because unknown top-level keys are kept by design
   (ADR-2132 for projects, ADR-2151 for scenes). Every object says what it does with an unknown key in
   `x-avgen-unknownKeys`:
   - `keep`: kept and written back (the project root);
   - `warn`: warned about, kept and written back (the scene root, `post`);
   - `warn-drop`: warned about and lost on save (`environment`, `sky`, lights, the rig, routes);
   - `drop`: ignored and lost (nodes, camera, assets, render, chain).

   A key the engine refuses (`lod`) is `additionalProperties: false`.
7. **`writtenBack` is ADR-2151's audit.** False exactly for:
   - `/lightRig` (written as `environment.lightRig`);
   - `/lights`, `/entities` and `/heroes` (an empty list is omitted);
   - `/environment/vortex` (migrated to an effect);
   - `/nodes/*/<unknown>` (dropped; reported);
   - `/nodes/*/world/biomes` (empty written as absent; reported).

   The schema marks each with `x-avgen-writtenBack: false` and lists them all at the root in
   `x-avgen-notWrittenBack`. Value-level normalisations (defaults omitted, names sanitised, float precision, rotation
   spelling) keep the key and are not entries. Every project entry is written back (ADR-2132).
8. **The engine validates its own documents.** The schemas are JSON Schema 2020-12 using only what
   `ai::validateAgainstSchema` enforces, plus annotations (`$schema`, `$id`, `$defs`, `description`, `title`,
   `x-avgen-*`). The validator gained exactly three keywords:
   - `anyOf`: every form's reason is listed when none holds, and a one-form `anyOf` reports that form's own error;
   - `const`;
   - a local `$ref` (`#/$defs/...`), resolved against the root and applied beside its siblings. One that does not
     resolve, or loops, is `Internal`: the schema is at fault, not the value.

   Existing messages and codes are unchanged, and a tool schema still may not use `$ref` (T2).
9. **Feature ids are generated** (`api::featureList`):
   - `scene.<key>` (but `format`, `version` and `name`), `scene.environment.<key>`;
   - `node.<kind>`, `effect.<kind>` (the effect registry);
   - `render.<output>`, `aov.<name>` (`RenderSettings::aovNames`);
   - `assets.stems`, `assets.feeds`, `assets.audioClips`.

   `capability.list` returns them as `features`, and `avgen --describe features` prints them. They are not an
   artefact. A feature id says the parser reads the thing (ADR-2150's "supported"), never that this session can run
   it.
10. **Out of scope:** `engine.requires` load checks and `project.validate` (stages S4 and later). The schemas are
    what they will check against.

## Rationale

- The key lists are where a key comes into being, so a schema built from them cannot name a key the parser does not
  read or miss one it does. That is the half that can be generated.
- Types cannot be derived without a second parser. One table beside nothing else is the smallest authored surface,
  and D2 and D3 make every gap a failing test rather than a silent omission.
- The validator's subset was a deliberate ADR-065-style decision. Three keywords make the document schemas
  expressible: a path or an object, a format constant, one node definition used in two places. A JSON Schema library
  would be a dependency bought to check two files.

## Consequences

- D3: all 277 shipped scenes (`examples/**/*.scene.json` without `_bench/`) and all 251 shipped projects
  (`format: avgen-project`) validate, with no skip list. D3 sweeps those 528 documents in a few seconds.
- A typing table edit, a key list edit or a new enumerator value needs `tools/gen_api.sh` in the same commit. T1
  fails with that command printed.
- `capability.list` gains `features`. Its domain prose was corrected where it had gone stale (C3):
  - `light-editing` said lights would need to register parameters; authored lights register `lights/<id>/*`;
  - `entity` said node creation and deletion were not exposed because a rollback could not undo them;
  - `terrain` and `water` named tools that exist.
- `kRouteKeys` now includes `planItem`, which the reader already accepted. The route warning is unchanged.

Found while typing, reported, not changed here:
- The plan's procedural "plane" is not a primitive kind (`primitiveKindFromName` has box, cylinder, sphere, torus,
  point, procedural, tube, mesh, generated, text and fiber). The PoC floor is a flat `box`.
- `assets.scene.kind` (`orb`, `gltf`, `composition`) has no name table; `Engine::loadProject` compares literals,
  so that enum is the one list written in a table.
- `render.output: "png"` is accepted and written back as `"sequence"` (5 shipped projects use it).
- Several readers silently ignore a malformed value instead of refusing it: `environment.fogColor`, `background`,
  `styledSkyAmbient` and `styledGroundAmbient`; the sky colours; the camera's `position`, `target`, `orbitPivot` and
  a non-integer `mode`; a node's `canopy*`, `vortex*`, `energy` and `wind` members.
- A one-node scene of kind `gltf` or `scene` without an asset, or `spline` without two control points, is skipped
  at load (and kept, ADR-2151). C1 names the three.

## Rejected alternatives

- **A hand-written JSON Schema file checked against the parsers.** Two copies of the key lists. The prose format
  doc already shows how that drifts.
- **Typing every block now.** Weeks of reading 15k lines of `composition.cpp` for blocks no client edits yet. An
  explicit `x-avgen-untyped` publishes the gap at no cost and turns into a type when a client needs one.
- **`additionalProperties: false` on typed objects.** It is stricter than the engine, which warns or ignores. A
  schema stricter than the parser fails correct files. The policy is published per object instead.
- **Extending the validator to `oneOf`, `pattern`, `patternProperties` and schema-valued `additionalProperties`.**
  None is needed to state the PoC's types, and each is a constraint the engine would have to keep enforcing.

## Revisit triggers

- A client needs a block that is untyped: type it in the table and regenerate. D3 says at once whether the type
  matches what ships.
- Nodes gain a key list (ADR-2151's revisit trigger): their keys join D2, and `/nodes/*/<unknown>` stops being a
  not-written-back entry once unknown node keys are kept.
- `world_map.cpp` writes `"biomes": []`: remove `/nodes/*/world/biomes`.
- `project.validate` or `document.patch` (S4) lands: it validates with these schemas through the same validator.
