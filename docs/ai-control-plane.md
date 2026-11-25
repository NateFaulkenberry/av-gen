# The AI control plane

How to use the AI subsystem, and how to extend it. The decisions behind it are in
[ADR-094](decisions/ADR-094-ai-control-plane.md); the research is in
[`research/ai-control-plane.md`](research/ai-control-plane.md); the original brief is
[`ai-control-plane-spec.md`](ai-control-plane-spec.md).

The point of this subsystem is **the tool API**, not the panel. `src/ai/tool_api.hpp` and
`src/ai/engine_tools.cpp` are AV Gen's semantic interface — named, schema'd, validated operations
over real engine systems — and the AI is one client of it. Nothing in that layer names a model, a
vendor or a wire protocol, so an MCP server, a script API or a remote control surface would attach
to the same registry.

---

## Using it

### Configuring a provider

Settings → AI. Choose a provider, paste a key into the masked field, press **Store**, press **Test
connection**. The key goes to the macOS keychain and is never shown again; the row afterwards says
only *that* a credential exists and where it came from.

For a headless render or a CI job, set `AVGEN_AI_<PROVIDER>_KEY` instead — `AVGEN_AI_ANTHROPIC_KEY`,
`AVGEN_AI_OPENAI_KEY`, `AVGEN_AI_GEMINI_KEY`, `AVGEN_AI_LOCAL_KEY`. The environment is consulted
*before* the keychain, and Settings says so when a variable is in effect.

For a local model, run a server and point the **Local model** provider at it:

```
llama-server -m <model>.gguf --port 8080 --api-key "$(openssl rand -hex 16)"
```

Put that same string in the credential field. **Never** start it with `--tools`, `--agent` or
`--mcp-servers-*`: those give the server its own filesystem and shell access, entirely outside AV
Gen's tool layer, which is the security boundary.

### Asking for something

Open the **AI** panel, type, Cmd+Enter. The panel shows the plan, then each tool call with its
description, duration and outcome, then what actually changed. **Undo this task** puts the project
back to before the whole task, however many underlying modifications it made.

With nothing configured the panel says "AI assistant is not configured" and offers Settings. Every
other part of AV Gen behaves exactly as it did.

### Running it from the command line

```
avgen --ai-prompt "make this feel like a cold foggy night"        # uses the configured provider
avgen --headless --composition scene.json \
      --ai-script examples/ai/atmosphere.ai.json --save-project out.json
```

`--ai-script` swaps in a scripted provider: the model's judgement comes from a JSON file, and
everything else — context, the agent loop, main-thread dispatch, the transaction, validation — is
the real thing against the real engine. That is how the path is demonstrated and diffed without a
key or a network.

---

## Architecture

```
                      ┌──────────────── JobSystem worker ────────────────┐
 AI panel ──submit──► │ Orchestrator: gather → model → tools → validate  │
                      │        │                      │                  │
                      │        ▼                      ▼                  │
                      │   Provider (HTTPS)      api::Session::call       │
                      │                         (validate, permissions)  │
                      └─────────────────────────────┬───────────────────-┘
                                                    │  the host's MainThreadQueue, pump() once per frame
 frame thread ─────────────────────────────────────►├─► ToolRegistry ─► app::Engine
                                                    └─► Transaction / SnapshotStore / compensation log
```

Since ADR-2153 the assistant is a client of `api::Session` (`src/api/session.*`), the object every
client of the engine goes through. The Session holds the immutable tool registry (engine, Director and
session tools), the fixed permissions and the `PathPolicy` every path-taking tool resolves through, its
own `SnapshotStore`, the transaction sink, and the explicit transaction. The `ControlPlane` owns the
Session and its `LocalHost` (the engine, the queue, the hooks the application installs); the
`Orchestrator` calls `session.call` for every tool, and its task's transaction is
`transaction.begin` / `commit` / `rollback`.

| File | What it is |
|---|---|
| `ai/tool_api.*` | `ToolDefinition`, `ToolResult`, `ToolRegistry`, JSON Schema validation |
| `ai/tool_context.hpp` | what a tool is handed: engine, cancel token, performance source, change log |
| `ai/engine_tools.*` | the tools themselves |
| `ai/capabilities.*` | the §24 registry, derived from the tool registry plus authored absences |
| `ai/transaction.*` | `SnapshotStore`, `TransactionSink` (the undo seam), RAII `Transaction` |
| `ai/main_thread_queue.*` | marshalling tool bodies onto the frame thread |
| `ai/provider.*` | normalized conversation types, the `Provider` interface, the registry |
| `ai/providers.cpp` | the adapters — **the only file that names a vendor** |
| `ai/http.*`, `http_darwin.mm`, `http_portable.cpp` | the transport |
| `ai/credentials.*` | keychain, environment fallback, in-memory store for tests |
| `ai/context.*` | the system prompt and the ambient context block |
| `ai/orchestrator.*` | the agent loop state machine, `AgentTask`, `Activity` |
| `ai/control_plane.*` | the facade the application owns: providers, credentials, tasks, and the Session |
| `api/session.*`, `api/session_tools.cpp` | the Session, `LocalHost`, `session.info`, `transaction.*` (ADR-2153) |
| `api/path_policy.*` | `SessionPermissions` and the one place a client-supplied path is resolved |
| `api/compensation.*` | what a rollback does about files: the compensation log and the process-wide claims |
| `ai/scripted_provider.*` | the one permitted mock (§52) |
| `ui/ai_panel.*`, `ui/settings_panel.*` | the panels |
| `app/settings.*` | application-level settings |

### Threading

| Thread | What runs there |
|---|---|
| Frame (main) | `ControlPlane::pump()` (the Session's host queue), and therefore **every tool body**; the panels |
| JobSystem worker | the agent loop, context assembly, the HTTP request, parsing |
| `NSURLSession` queue | the HTTP completion handler only |

Nothing reachable from the render, audio or analysis thread holds a `Provider` — the control plane
hands one out only inside a job body, which is the same structural rule ADR-065 set for inference.
`pump()` takes a time budget (4 ms by default) and defers the rest to the next frame. A `run()` call
from the pumping thread executes inline rather than deadlocking.

### Security

The tool layer is the boundary. There is no filesystem access, no shell, no network, no code
execution and no GPU operation in the tool surface. Arguments are validated against the tool's own
JSON Schema before the body runs, so a tool body reads validated fields rather than defending
against untrusted JSON. A tool body that throws is caught and returned as a structured error rather
than taking the worker with it.

Credentials: keychain or environment, never a file, never a log, never a tool result, never
redisplayed. `ProviderConfig` has no field that could hold one and `fromJson` refuses a document
that carries a key-shaped field.

### Context

Two layers with different lifetimes. `systemPrompt()` is stable product knowledge plus a tool index
generated from the registry — the cacheable prefix. `ambientContext()` is the handful of O(1) facts
every task needs (scene kind, counts, playhead, tempo, warnings); anything O(n) in project size is
pulled through a tool. The whole ambient block is under about a kilobyte regardless of scene size,
and there is a test that says so.

### Paths and permissions

The Studio assistant's session (`ai::studioSessionOptions`, set up in `Application::initControlPlane`)
reads the example folders, the source assets, Desktop and Downloads; writes `<prefs>/projects`; and
reads and writes the open project's folder, whichever project is open (owner decision 1). A headless
run has no roots beyond the open project's folder. A relative path resolves against the project's
folder (or the first write root), never the working directory. A path outside the roots is
`PERMISSION_DENIED`; no project open is `FAILED_PRECONDITION`.

### Transactions

One per task, opened lazily on the first mutating tool (the session's `transaction.begin`), committed
once. The snapshot covers parameter base values, modulation routes, presets, the timeline, the
composition and the sequence. The task's calls accept irreversible tools (a save, an import), so a
task that mixes them is marked partial and its rollback says what it could not restore; files its
imports created are deleted by the compensation log unless something else still needs them.
Rollback happens on a provider failure after mutations, on cancellation, and on an unhandled error;
**not** on a tool returning an error, which is information the agent is expected to act on.
[docs/api/transactions.md](api/transactions.md) lists, per tool, what a rollback restores.

---

## Extending it

### Adding a tool

One place: `src/ai/engine_tools.cpp`. Add an `add(registry, ...)` call in the right
`register*Tools` function.

```cpp
add(registry, "camera.set_focus", "Set focus distance",
    "Set the distance the lens is focused at, in metres.",
    schema::object({{"metres", schema::number("Focus distance", 0.05, 1000.0)}}, {"metres"}),
    mutating(),
    [](const json& args, ToolContext& ctx) -> ToolResult {
        params::IParameter* p = ctx.engine().params().find("camera/focus/distance");
        if (p == nullptr) {
            return ToolResult::failure(ToolErrorCode::Unavailable,
                                       "this scene exposes no focus distance",
                                       "call parameter.search with 'focus'");
        }
        const SetOutcome outcome = setParameter(ctx.engine(), *p, {metres}, -1);
        ctx.changes().note(p->path(), valueJson(outcome.after).dump(), outcome.clamped);
        return ToolResult::ok(outcomeJson(*p, outcome), "focus set");
    });
```

Six rules the registry and the tests enforce:

1. **Classify it.** `readOnly()`, `mutating()`, or a hand-built `ToolAnnotations` with
   `mutatesSession`. `ToolRegistry::add` throws on an unclassified tool, and a test asserts it.
2. **Stay inside the snapshot domain, or say you do not.** A tool with `mutatesProject` must have
   `undoable`, and a test enforces that. If it changes something a snapshot cannot restore, extend
   `SnapshotStore::captureDocument` in the same commit.
3. **Build the summary before you move the result.** `ToolResult::ok(std::move(out), ...)` with
   `out` read in the same expression is unsequenced, and it shipped a tool that reported "0 values
   set" while setting four. There is a test comparing every summary against its own structured
   result.
4. **Report what landed, not what was asked.** Read the value back. `setParameter` /
   `outcomeJson` do this, including clamping and automation overrides.
5. **State its execution class and permissions when they are not the default** (ADR-2150). The class
   is derived (read-only is `read`, a project write `instant`, a session write `session`); wrap the
   annotations in `as(..., ExecutionClass::Structural)` for a tool that rebuilds the composition,
   `Rebind` for one that adds or removes a route, key or shot, and `Job` for seconds-to-minutes work.
   Wrap them in `needs(..., {"read", "write"})` for a tool that reads or writes files the caller
   names, `"subprocess"` for a child process, `"gpu"` for GPU work. Registration refuses a class
   that contradicts the annotations.
6. **Regenerate the catalogue.** Run `tools/gen_api.sh` and commit `schemas/` and `docs/api/`. A new
   tool without an `outputSchema` must be listed, with its reason, in
   `schemas/tools-without-output-schema.json`. A breaking change to an existing tool (removed,
   renamed, a new required input, a changed input type, a lost output field) also needs
   `avgen::api::kApiLevel` raised and `tools/gen_api.sh --baseline`.

Add the domain to `kDomains` in `capabilities.cpp` if it is new. If you *cannot* expose something,
add an `available = false` record with the reason (published as `supported: false` with an
`unsupportedReason`) and the smallest engine change that would lift it
— that is more useful to an agent than silence.

### Adding a provider

Two edits, both in `src/ai/providers.cpp`. Implement `Provider` (four methods; `HttpProvider` gives
you the transport and the error mapping), then add a `registry.add(Entry{...})` row. Nothing else
in the codebase changes.

Test it against `ScriptedHttpClient` with a recorded response body — see
`tests/unit/test_ai_provider.cpp`. Assert both directions: that the request has the shape the vendor
documents, and that the response parses into AV Gen's types. Assert that the credential is in a
header and not in the body or the URL, and that no tool annotation reaches the model.

### Testing tools

`tests/unit/test_ai_tools.cpp` — a real `app::Engine`, the real registry, a `ToolContext` (with
`Fixture::allow` standing in for a session's roots). `tests/unit/test_api_session.cpp` — the same
tools through a real `api::Session`: permissions, transactions, the compensation log, the live
policy, the threading. Nothing is mocked; §54 forbids fake engine APIs and a suite that passed against a stand-in would prove only
that the stand-in works.

`tests/integration/test_ai_control_plane.cpp` — the whole path, with a `ScriptedProvider` as the
only substitution. The pumping loop in its `Session` fixture *is* the threading proof: tools only
ever run on the thread that calls `pump()`, and a probe tool asserts it.

### Extending the orchestrator

`Orchestrator::run` is the state machine. Anything user-visible goes through `task.append(Activity)`
rather than a log line, so the panel keeps working without parsing prose. A new `ActivityKind` needs
a name in `activityKindName` and a colour in `ai_panel.cpp`.

---

## Available tools

The catalogue is generated from the registry the engine runs, so it cannot go stale:

- [docs/api/tools.md](api/tools.md): every tool, with its input schema, annotations, execution class, the
  permissions it needs and its MCP name;
- [docs/api/errors.md](api/errors.md): the error codes;
- `schemas/tool-catalogue.json`: the same catalogue as JSON.

Regenerate them with `tools/gen_api.sh` after changing a tool; `tests/unit/test_api_catalogue.cpp` fails until you
do. At run time, `capability.list_tools` and `avgen --describe tools` report the same set.

## What is not exposed, and why

`capability.list` reports these at runtime with the same reasons.

| Domain | Why not |
|---|---|
| Terrain, water, navigation | Being reworked concurrently; their parameter surfaces are in flux. Whatever the current scene registers is still visible to `parameter.search`. |
| Adding or removing a light | Authored scene lights already register `lights/<id>/*` parameters (`enabled`, `intensity`, `position`, `color`, ...), so editing one is `parameter.set`. Adding or removing one is a scene-document change, which has no tool yet. |
| Rendering to a file | An external side effect; needs a separately designed capability with explicit consent, not a tool. |
| Seeing the rendered frame | The agent inspects structured renderer and performance state. Faking visual verification would be worse than not having it. |
