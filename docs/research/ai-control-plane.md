# Research: AI control planes, provider APIs and credential storage

The note the AI control-plane brief (`docs/ai-control-plane-spec.md`, addendum §22) asks for
*before* locking the implementation. It answers nine questions: what to copy, what not to copy,
what to normalize, what stays provider-specific, which MCP concepts are useful internally, whether
local inference should be embedded or externalized, how tool metadata should be represented, how
long-running tasks should work, and how transactions interact with agent loops.

Sources were read in the order §22 sets, and the wire-level details were verified against the
current specifications in September 2026 rather than recalled. Several things that "everyone knows"
turned out to be out of date, and those are flagged.

---

## 1. What AV Gen should copy

**The loop, and only the loop.** Every provider implements the same four-beat cycle: the model asks
for a tool, the application runs it, the application returns a structured result, the model
continues. Nothing else about their APIs is common. So AV Gen implements that loop once, over its
own types, and adapters translate at the edge. This is the single most important structural
decision in the whole subsystem, and it is the one that makes a fifth provider a file rather than a
refactor.

**Action over description** (Anthropic's agentic guidance). The lesson is about the system prompt,
not the code: a model told it is a helpful assistant says *"you could increase the emission on the
plants"*; a model told it is operating an application inspects the plants, inspects the materials,
modifies the emission, verifies and reports. AV Gen's system prompt is written in the second mode
and says so explicitly.

**Structured tool annotations** (MCP). Read-only, destructive, idempotent, open-world. This
vocabulary is what makes safety, scheduling, confirmation and parallelisation *decidable* rather
than guessed.

**Honest progress** (the whole agentic literature, and `app::JobSystem` locally). Declare the
stages up front; report the stage you are in; never invent a percentage.

**Separating what the application pushes from what the model pulls** (MCP resources vs tools).
Push the handful of O(1) facts every task needs; make everything else a query.

## 2. What AV Gen should deliberately *not* copy

**Any vendor's request or response types as the internal representation.** OpenAI puts tool
arguments in a JSON *string*; Anthropic puts them in an object; Gemini puts them in an object under
a different key and has no tool role at all. Picking one as canonical makes the other two translate
twice.

**A "reasoning" or "thinking" surface in the UI.** Addendum §14 is right, and the practical reason
is stronger than the principled one: the default for `thinking.display` changed to `omitted` across
a whole model generation. A UI built on raw chain-of-thought would have silently gone blank.

**MCP's transport, handshake and JSON-RPC framing.** The *concepts* are valuable internally; the
protocol is a thing to expose later, not a thing to build the engine around (addendum §19).

**Forced tool choice.** `tool_choice: "required"` is unsupported on Ollama entirely and returns a
400 on the newest Anthropic models. `auto` is the default everywhere and is what the loop needs.

**Alias tables that map "fast" or "smart" onto specific model ids.** Such a table is stale within a
month and silently routes a user to a model they did not choose. Model identifiers stay
provider-specific and the user types one; Settings lists what the provider advertises.

**An agent framework.** The brief warns about building an elaborate theoretical framework before
proving usefulness, and the warning is well-aimed: the state machine that runs this is about 200
lines.

## 3. What is normalized, and what stays provider-specific

Normalized, because the loop cannot work without it:

| Concept | Anthropic | OpenAI | Gemini |
|---|---|---|---|
| system instruction | `system` | a `system` message | `systemInstruction` |
| assistant tool request | `tool_use` block | `tool_calls[].function` | `functionCall` part |
| tool arguments | object (`input`) | **JSON string** (`arguments`) | object (`args`) |
| tool result | `tool_result` block, user turn | `role: "tool"` message | `functionResponse` part, **user** turn |
| stop reason | `stop_reason` | `finish_reason` | `finishReason` |
| input tokens | `usage.input_tokens` | `usage.prompt_tokens` | `usageMetadata.promptTokenCount` |

Provider-specific, deliberately: model identifiers, sampling beyond temperature, reasoning and
effort configuration, caching directives, and anything beta-gated. A lowest common denominator that
hid those would make the good providers worse.

Three details that cost debugging time if assumed:

- OpenAI's `max_tokens` is **deprecated** in the current schema and rejected outright by reasoning
  models. `max_completion_tokens` is the field.
- OpenAI documents that `tool_calls[].function.arguments` **may not be valid JSON**. A loop that
  throws on a parse failure dies on the model's mistake instead of letting the model correct it.
- Several OpenAI-compatible servers report `finish_reason: "stop"` while returning tool calls.
  Trusting the field alone ends the task with the tools unrun.

## 4. Which MCP concepts are useful internally

**Tools / Resources / Prompts, distinguished by who controls invocation**: the model, the
application, the user. That distinction is worth adopting even without the protocol.

- *Tools* → `ai::ToolRegistry`. Model-controlled.
- *Resources* → the ambient context block. Application-controlled: `ai::ambientContext()` decides
  what the model gets without asking.
- *Prompts* → not implemented. Reusable user-facing workflows ("Optimize Scene", "Create Cinematic
  Shot") are a natural next step and would be a list of canned prompts plus a menu, not new
  architecture.

**The annotation vocabulary**, adopted almost verbatim: `readOnlyHint`, `destructiveHint`,
`idempotentHint`, `openWorldHint`. Two adaptations. `openWorldHint` is omitted because every tool
here is closed-world by construction — they touch the engine, not an open set of external entities
— and a field whose value is always `false` is noise. And MCP's defaults are deliberately
pessimistic (an unannotated tool reads as writing, destructive, non-idempotent) whereas AV Gen's
are optimistic; the guard against forgetting therefore lives in `ToolRegistry::add`, which refuses
a tool that is classified neither as a read nor a write.

**`isError` in the result rather than a protocol error.** MCP splits protocol failures from tool
*execution* failures precisely so the model can see the latter and self-correct. `ai::ToolResult`
does the same, and adds a `recovery` field, which is the difference between an agent that fixes a
typo in a parameter path and one that repeats it.

Worth recording: MCP's current revision is **2026-07-28**, not the 2025-06-18 the brief cites. The
annotation vocabulary is byte-identical between them; version negotiation changed substantially.

## 5. Should local inference be embedded or externalized?

**Externalized.** `llama-server` in a separate process, reached through the same
OpenAI-compatible adapter the cloud path already has.

Verified against llama.cpp master (`43f3dda6`, build b10909). The decisive facts:

1. **`common/` is not installed as headers.** The root CMake exports only `llama.h` and
   `llama-cpp.h`. Chat templating (`common/chat.h`), the Jinja engine, the tool-call parser and
   `json_schema_to_grammar` all live in `common/`, which installs as a library with no headers.
   Embedding therefore means vendoring the whole tree and linking a header-less library, or
   reimplementing Jinja and tool-call parsing. Neither is a dependency worth buying.
2. **The C API is 0.4.0 and pre-1.0**, with breaking changes every few weeks and 10–30 tagged
   builds a day. A pinned dependency that moves that fast is a maintenance commitment, not a
   feature.
3. **The renderer owns the GPU.** An embedded LLM doing Metal compute contends with the thing the
   engine exists to do, on the same device, for the same memory. Out of process, the OS schedules
   it and the worst case is a slow answer rather than a dropped frame — which is precisely ADR-065's
   realtime-budget reasoning applied to a much larger workload.
4. **Failure isolation.** A crash or an out-of-memory in inference takes down a separate process,
   not the editor with the user's unsaved project.
5. **It costs no adapter.** `llama-server` speaks OpenAI chat completions with tool calling, so
   "local" is a different endpoint and a different default, not different code.

The cost is that the user runs a server. That is real, and it is mitigated by defaulting the
endpoint to `http://127.0.0.1:8080` and offering Test connection. A later phase could supervise the
process — spawn, health-poll `GET /health`, shut down — which is a hundred lines and no new
dependency.

Wire details worth keeping, several of which contradict the upstream documentation:

- `--jinja` is now **default on** for the server; `docs/function-calling.md` still says to pass it.
- `parallel_tool_calls` now defaults to the template's capability, not `false`.
- `/props` returns `chat_template_caps` (`supports_tools`, `supports_tool_calls`,
  `supports_system_role`, …) — that is the capability probe, not the `chat_template_tool_use` the
  docs still name, which no longer exists.
- The "Loading model" 503 comes from pre-routing middleware, so *every* endpoint returns it while
  the model loads.
- Constrained output: the README documents `response_format.schema`, the code reads
  `response_format.json_schema.schema`. Use the OpenAI double nesting.
- **Security**: with no `--api-key` there is no auth on any endpoint and CORS defaults to `*` with
  credentials enabled. Always pass a random key even on localhost. And never start it with
  `--tools`, `--agent` or `--mcp-servers-*`: those give the server its own filesystem and shell
  access, entirely outside AV Gen's tool layer, which spec §36 makes the security boundary.

**Revisit when**: a single small model can be bundled under a licence we can ship, *and* Metal
contention against Dawn has been measured rather than assumed.

## 6. How tool metadata should be represented

As a struct on the tool definition, not as a parallel registry. §24 asks for a capability registry
that is "the source of truth used to construct tool definitions", and the literal reading — author
capabilities, generate tools — puts a tool's name, description and schema in a data table and its
behaviour in a function, which drift the first time somebody edits one. The intent is
single-sourcing, so the dependency is inverted: the tool definition is the source of truth and the
capability document is derived from the registry.

The half that is *not* derived is the important half. A registry built only from the tools can say
what the engine does and never what it cannot do. The authored domain records exist mostly to
report absence — terrain, water, entity creation, per-light editing — each with the reason and the
smallest engine change that would lift it. An agent that knows a thing is unavailable says so; an
agent that does not finds a plausible-looking parameter and changes something nobody asked for.

## 7. How long-running tasks should work

On the existing job system, which already had the right shape (ADR-064): a queue, workers, declared
stages, honest progress, prompt cancellation, and a snapshot a UI reads without holding anything
the worker needs.

What the AI layer adds is the **main-thread queue**. The decision of what to do happens on a
worker; the doing happens on the frame thread, because `ParameterSet`, `Modulator` and `Timeline`
are read by `Engine::update()` every frame with no lock anywhere. The same split ADR-066 made for
Generate World, for the same reason. `pump()` takes a time budget so a slow tool costs one budget
slice rather than a dropped frame, and a call from the pumping thread runs inline rather than
waiting for a pump that can never come.

Cancellation is one flag shared by the orchestrator, the HTTP request and every queued tool call,
so one press reaches all three. Work already started is allowed to finish — stopping a half-applied
engine mutation is worse than completing it and rolling the transaction back.

## 8. How transactions interact with agent loops

**One transaction per task, opened lazily, closed once.**

Not one per tool call: the user asked for one thing, and undoing "make the scene cinematic" halfway
is not an outcome anybody wants. Lazily, so an inspection-only task takes no snapshot at all and
does not clutter the rollback list.

The subtle decision is what does *not* roll back. A tool returning a structured error is
information the agent is expected to act on; rolling back the whole task because one call had a
typo in a parameter path would make the agent unable to correct itself, which is exactly the
recovery the spec's failure acceptance test asks for. Rollback is for a provider failure after
mutations, for cancellation, and for an unhandled error.

And the transaction's domain must equal the tool surface's domain, or a rollback is only
approximately a rollback. AV Gen's snapshot covers parameter values, modulation routes, presets and
the timeline — which is exactly what the tools in this pass can change. `ToolAnnotations::undoable`
is where a future tool declares that it escapes that domain, and a mutating tool that sets it false
is a tool whose changes a rollback will not reach.

---

## Reference library

- [OpenAI Responses / quickstart](https://platform.openai.com/docs/quickstart/make-your-first-api-request)
  · [OpenAPI specification](https://github.com/openai/openai-openapi)
  · [function calling](https://developers.openai.com/api/docs/guides/function-calling)
- [Anthropic agentic guidance](https://docs.anthropic.com/en/docs/build-with-claude/prompt-engineering/prompt-templates-and-variables)
  · [Messages API](https://docs.anthropic.com/en/api/messages)
- [Gemini function calling](https://ai.google.dev/gemini-api/docs/function-calling)
  · [generateContent](https://ai.google.dev/api/generate-content)
- [MCP tools](https://modelcontextprotocol.io/specification/2026-07-28/server/tools)
  · [resources](https://modelcontextprotocol.io/specification/2026-07-28/server/resources)
  · [prompts](https://modelcontextprotocol.io/specification/2026-07-28/server/prompts)
  · [versioning](https://modelcontextprotocol.io/specification/versioning)
- [llama.cpp](https://github.com/ggml-org/llama.cpp)
  · [server README](https://github.com/ggml-org/llama.cpp/blob/master/tools/server/README.md)
- [Keychain Services](https://developer.apple.com/documentation/security/keychain-services/)
  · [using the keychain to manage user secrets](https://developer.apple.com/documentation/security/using-the-keychain-to-manage-user-secrets)
- Compatible servers: [vLLM](https://docs.vllm.ai/en/latest/serving/online_serving/openai_compatible_server/)
  · [Ollama](https://docs.ollama.com/api/openai-compatibility)
  · [LM Studio](https://lmstudio.ai/docs/app/api/endpoints/openai)

## Appendix: what macOS Keychain actually buys, measured

Probed on macOS 26.6.2 (arm64) with an ad-hoc-signed CLI binary, because the answer determines what
the documentation is allowed to claim.

- **The data-protection keychain is unusable here.** `SecItemAdd` with
  `kSecUseDataProtectionKeychain = true` returns `-34018 errSecMissingEntitlement`: it needs
  `keychain-access-groups`, which needs a provisioning profile, which needs a bundle. A binary run
  out of `build/release` has none. The failure is **asymmetric** — reads against it return `-25300`
  (not found) rather than `-34018`, so probing only the read path suggests it works.
- **The file-based keychain works unsigned**: add → 0, add again → `-25299` (duplicate), update →
  0, delete → 0, delete again → `-25300`.
- **`kSecAttrAccessible` is silently ignored** on the file-based keychain.
  `kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly` returned success and reading the attributes
  back showed no accessibility attribute at all. Setting it would encode a guarantee that is not
  kept.
- **It provides no confidentiality against other local processes running as the same user.**
  `/usr/bin/security find-generic-password -w` — a binary definitively not in the item's trusted
  application list — printed the secret with no interaction.

So the honest claim, and the only one the code and the UI make: the key is not in a dotfile, in
shell history, or anywhere an accidental `git add` reaches; the user can review, rotate and delete
it in Keychain Access; and we are not shipping a hand-rolled cipher with its key beside the
ciphertext. That is a real improvement over a config file and it is the whole of it.

Mechanics worth keeping: link **both** `Security` and `CoreFoundation` (Security does not re-export
CF's symbols); no Objective-C is needed; prefer add-then-update-on-`errSecDuplicateItem`;
`kSecReturnData` alone yields a `CFDataRef` while `kSecReturnData` *plus* `kSecReturnAttributes`
yields a `CFDictionaryRef`, and casting the latter to the former crashes; and handle `-25308
errSecInteractionNotAllowed` as "headless or SSH — use the environment variable" rather than as a
fault.
