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
                      │   Provider (HTTPS)      MainThreadQueue          │
                      └─────────────────────────────┬───────────────────-┘
                                                    │  pump(), once per frame
 frame thread ─────────────────────────────────────►├─► ToolRegistry ─► app::Engine
                                                    └─► Transaction / SnapshotStore
```

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
| `ai/control_plane.*` | the facade the application owns |
| `ai/scripted_provider.*` | the one permitted mock (§52) |
| `ui/ai_panel.*`, `ui/settings_panel.*` | the panels |
| `app/settings.*` | application-level settings |

### Threading

| Thread | What runs there |
|---|---|
| Frame (main) | `ControlPlane::pump()`, and therefore **every tool body**; the panels |
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

### Transactions

One per task, opened lazily on the first mutating tool, committed once. The snapshot covers
parameter base values, modulation routes, presets and the timeline — exactly what the tools can
change. Rollback happens on a provider failure after mutations, on cancellation, and on an
unhandled error; **not** on a tool returning an error, which is information the agent is expected
to act on.

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

Four rules the registry and the tests enforce:

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

Add the domain to `kDomains` in `capabilities.cpp` if it is new. If you *cannot* expose something,
add an `available = false` record with the reason and the smallest engine change that would lift it
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

`tests/unit/test_ai_tools.cpp` — a real `app::Engine`, the real registry, a `ToolContext`. Nothing
is mocked; §54 forbids fake engine APIs and a suite that passed against a stand-in would prove only
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

34 tools across 13 namespaces. `capability.list_tools` is authoritative at runtime.

| Namespace | Tools |
|---|---|
| `capability` | `list`, `list_tools` |
| `project` | `get_state`, `create_snapshot`, `list_snapshots`, `restore_snapshot` |
| `parameter` | `list_groups`, `search`, `get`, `set`, `reset` |
| `scene` | `get_summary`, `find_nodes`, `get_node`, `set_node_transform`, `set_node_visible` |
| `camera` | `get`, `set`, `frame_node` |
| `environment` | `get`, `set` |
| `lighting` | `list` |
| `material` | `list` |
| `sequencer` | `get_state`, `add_keyframe`, `remove_track`, `set_playhead` |
| `audio` | `get_analysis` |
| `signal` | `list` |
| `modulation` | `list`, `create`, `set_depth`, `remove` |
| `performance` | `get_stats` |

## What is not exposed, and why

`capability.list` reports these at runtime with the same reasons.

| Domain | Why not |
|---|---|
| Creating and deleting nodes | Outside the snapshot domain, so a rollback would leave it behind. Add it once the transaction is backed by the editor's undo stack. |
| Terrain, water, navigation | Being reworked concurrently; their parameter surfaces are in flux. Whatever the current scene registers is still visible to `parameter.search`. |
| Editing an individual light | Lights are regenerated on every scene rebuild, so a direct write would be discarded. Change them through `lightrig/*`, `scene/keyLight`, `env/sky/sunIntensity`. Exposing per-light editing needs each flattened light to register parameters the way a light rig's already do. |
| Rendering to a file | An external side effect; needs a separately designed capability with explicit consent, not a tool. |
| Seeing the rendered frame | The agent inspects structured renderer and performance state. Faking visual verification would be worse than not having it. |
