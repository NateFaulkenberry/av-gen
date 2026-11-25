# ADR-2150: the engine publishes its contracts, generated from the code that decides them

- Status: Accepted (2026-11-24), eng/engine-api (Engine API stage S0a; 113-engine-architecture)
- Code: `api/version.{hpp,cpp}` (`kEngineVersion`, `kApiLevel`, `version()`, `versionLine()`),
  `api/describe.{hpp,cpp}` (`toolCatalogue`, `errorCatalogue`, `apiBaseline`, `breakingChanges`, the Markdown
  writers, `runDescribeCommand`), `app/main.cpp` (`--version`, `--describe`, before `parseArgs`),
  `ai/tool_api.{hpp,cpp}` (`ExecutionClass`, `ToolAnnotations::executionClass`/`permissions`,
  `ToolDefinition::since`, `toolErrorCodeMeaning`, `kAllToolErrorCodes`, the registration checks),
  `ai/engine_tools.cpp` and `ai/director_tools.cpp` (each tool's stated class and permissions),
  `ai/capabilities.cpp` (`version`; a domain is `supported`), `src/CMakeLists.txt` (the build-info header reaches
  `avgen_core`), `tools/gen_api.sh`
- Artefacts: `schemas/tool-catalogue.json`, `schemas/tool-catalogue-baseline.json`,
  `schemas/tools-without-output-schema.json` (authored), `docs/api/tools.md`, `docs/api/errors.md`
- Tests: `tests/unit/test_api_catalogue.cpp` (`[api]`)

## Context

An agent outside the repository has to learn what the engine can do from the engine. There was nothing to learn it
from:

- no published tool catalogue;
- `docs/ai-control-plane.md` kept a hand list of 34 tools while the build had 76;
- no version identity. The project stamp said 0.1.0, the window title said 0.2, the start-up log said 0.1.0, and
  the revision lived only in a header the executable alone could see;
- no error catalogue;
- no way to tell, from the metadata, how a call should be scheduled or what a session must allow before it runs.

The owner's direction (`04-implementation/00-spec.md`) is a stable, discoverable, documented public API, generated
where possible from the authoritative C++, with drift tests.

## Decision

1. **One version identity.** `avgen::api::version()` returns `{engine, revision, branch, buildType, dirty, api,
   projectFormat, sceneFormat}`. It is the only source for:
   - `avgen --version`;
   - the window title;
   - the start-up log line;
   - the project's `app.version` stamp;
   - `capability.list`'s new `version` section.

   The engine version is 0.3.0, the first build that publishes an API. The API level is 1.
2. **`avgen --describe <tools|errors|version|baseline|all> [--out dir]`** prints the contracts. It runs before
   `parseArgs`, with no Application, window, GPU, engine or settings file. The generator lives in `avgen_core`, which
   cannot link `gpu::Context`, so being GPU-free is a property of the build. `tools/gen_api.sh` writes the
   artefacts into the repository.
3. **Every tool states how it is scheduled and what it needs.**
   - `executionClass` is one of `read | instant | rebind | structural | session | job`. It is derived from the
     annotations: read-only gives `read`, a project write `instant`, a session write `session`. The exceptions are
     stated at registration:
     - `structural`: node create, delete and parent; `world.generate`; `field.create`;
       `project.restore_snapshot`;
     - `rebind`: routes, timeline keys and tracks, sequence shots, markers and overlays;
     - `job`: the live-profile tools and the Director's record and evaluate.
   - `permissions` come from a closed set: `read`, `write`, `subprocess`, `gpu`.
   - `since` is the API level at which the tool's current contract appeared.
   - Registration refuses a stated class that disagrees with the annotations (a `read` that writes) and an unknown
     permission.
4. **The API level rises on a breaking change and only then.** `schemas/tool-catalogue-baseline.json` records, per
   tool, what a client depends on: that it exists, its required inputs, each input's type and its output
   properties. A break against it is a removal, a new required input, a changed input type or a lost output field.
   Such a break with the level unchanged fails the CPU suite, which prints what broke. Additive changes never touch
   the baseline. There are no aliases (ADR-441).
5. **Output schemas are ratcheted.** `schemas/tools-without-output-schema.json` lists the tools without one, each with
   a reason. A tool missing from the list without a schema fails. A listed tool that has gained one also fails, so
   the list can only shrink.
6. **Errors are a catalogue.** Every `ToolErrorCode` has a published meaning written for a client. A
   `static_assert` ties `kAllToolErrorCodes` to the enum.
7. **Discovery distinguishes supported from available from permitted.** The capability document's domain flag is
   renamed `supported`: this build has tools for it. It used to be called `available`, but it never answered
   whether the session can use the domain. Runtime availability (a host hook, a GPU, audio) and the client's
   permission are separate per-tool answers. They arrive with the Session; they are never inferred from
   `supported`.

## Rationale

- Generating from the registry means the published catalogue is the code that runs. A second hand-kept copy is
  what went stale before.
- Byte comparison against committed files makes drift a test failure with a one-command fix, in the commit that
  caused it.
- A breaking-change check needs a baseline that does not move with every regeneration, or it could never fire.
  That is why the baseline is separate from the catalogue.
- The execution class and permissions are what the Session, the live policy and the JSON-RPC and MCP adapters
  schedule and refuse by. Stating them at registration puts the decision beside the code it describes.

## Consequences

- Changing a tool's description, schema or annotations needs `tools/gen_api.sh` in the same commit, or the suite
  fails with that command printed.
- Saved projects now stamp `app.version` 0.3.0 instead of 0.1.0. Nothing reads the stamp.
- `capability.list` domains say `supported`/`unsupportedReason` instead of `available`/`unavailableReason`. This
  is API level 1, the first published level, so there is nothing to bump.
- `avgen_core` depends on the build-info target. Its `version.cpp` recompiles when the revision moves.

## Rejected alternatives

- **A hand-written JSON catalogue checked against the registry.** Two sources of one fact; the hand list in
  `ai-control-plane.md` was exactly this, and it was 42 tools behind.
- **Detecting breaks against the committed catalogue itself.** It is regenerated with every change, so the
  comparison would be against itself and never fire.
- **An execution class on every tool, written out.** Most follow from the annotations. Stating only the exceptions
  keeps the decision visible, and a mismatch is refused at registration.

## Revisit triggers

- The Session (stage S1) adds tools (`session.*`, `transaction.*`): they join the catalogue and the ratchet must not
  grow for them without a reason.
- A second protocol constraint on tool names beyond MCP's pattern.
- The library split moves `ai/` out of `avgen_core`: the generator goes with the registry.
