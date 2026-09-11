# AV Gen — AI Control Plane / Prompt Director

The bootstrap brief for the AI control plane, kept in the repository so the work can be picked up
without the conversation it arrived in. Reproduced as written, tightened only where the original
repeated itself.

The goal is a native, deeply integrated natural-language interface through which a user can inspect,
modify, direct and operate the AV Gen environment. **This is not a chatbot feature.** The target is:

> "I have an AI director living inside my audiovisual engine."

not

> "I have a chatbot that can change a few settings."

Representative prompts the system should eventually serve: *"Make this scene feel like a massive
bioluminescent alien forest at night… and create a 20-second cinematic camera move toward the
village. Keep the frame rate high."* — *"At the first drop, rapidly descend through the canopy,
transition into the cavern, then reveal the giant creature exactly on the downbeat."* — *"Take this
scene as far as you can visually. You have creative freedom."*

---

## §1 First: understand the existing system

Inspect the repository comprehensively before designing anything: application and engine
architecture, renderer, scene representation, entity system, materials, shaders, lighting, cameras,
environment, terrain, water, ecology, particles, animation, navigation, audio analysis, parameters,
modulation, MIDI, OSC, sequencer, timeline, automation, serialization, undo, assets, debug UI,
settings infrastructure, threading model, main-thread and GPU-thread requirements, existing
command/console/event/scripting systems, tests and ADRs.

**Read the existing documentation before designing duplicate systems. Do not throw away existing
architecture merely because a cleaner one can be imagined.** The engine is built around
`audio → analysis → signals → parameters → modulation → scene data → GPU → renderer`; the control
plane integrates into that pipeline rather than bypassing it.

## §2 Core architectural principle

**THE MODEL MUST NEVER DIRECTLY CONTROL INTERNAL C++ IMPLEMENTATION DETAILS.**

```
user prompt -> AI orchestrator -> structured tool calls -> AV Gen Tool API -> existing engine systems
```

The AI must **not**: generate or modify C++, mutate memory directly, manipulate GPU resources,
invent engine state, bypass transactions, bypass validation, bypass undo/snapshots, execute shell
commands, or assume undocumented functionality. It gets a vocabulary of safe, intentional
operations. **The Tool API is a foundational architectural layer, not a bolt-on.**

## §3 Design for a real agent

Support: request → context gathering → planning → tool execution → validation → optional iteration →
completion → rollback on failure. **Many tool calls per task.** Do not restrict the first
implementation to one tool call per prompt.

## §4–§7 Providers, local models, cloud models, settings

**§4** A provider abstraction independent of any vendor: local, OpenAI, Anthropic, Google, and a
generic OpenAI-compatible endpoint. Support conversational messages, system instructions, structured
tool definitions, tool calls and results, streaming text, usage information, cancellation, errors,
model selection, temperature or equivalent, reasoning-capable models where supported, context limits
and capability discovery. **Do not hard-code the application around one vendor.**

**§5** A lightweight local option for simple commands, inspection, parameter adjustment, explanation,
offline and privacy-sensitive use. **Check the repository and build environment before assuming an
inference framework.** If one exists, integrate it; if not, establish a clean provider boundary so a
local runtime can arrive later without restructuring. The interface must not leak runtime details.

**§6** Users configure their own credentials. Credentials must **never** be written into project
files or scene JSON, included in conversation logs or tool results, sent to another provider,
displayed in plaintext after entry, or committed to source control. **Use the native macOS secure
credential facility; do not hand-roll encryption and call it secure.**

**§7** Credentials live in the application's existing global Settings, **not** in the AI panel. Add
an AI/Providers section alongside General, Audio, Rendering, Input, Advanced. Allow configuring
providers, entering credentials (masked), selecting default provider and model, testing the
connection, enabling/disabling providers, and setting endpoints for compatible providers. Show
whether a provider is configured and whether a test succeeded. **Never expose a saved secret.**

## §8–§10 Prompt panel, task status, tool status

**§8** A dedicated panel inside the main application — not a separate window unless the architecture
forces it, and it should feel like a native creative-tool workspace rather than a web chatbot
embedded in a C++ program. Conversation, agent responses, tool activity, task status, errors,
completion summaries. Multiline prompts, keyboard send, cancellation, clear/new conversation,
continuation.

**§9** Make real work obvious. **Do not simply show "Thinking…".** Show a plan, then structured
progress against it. The user must be able to distinguish response generation, planning, tool
execution, validation, errors and completion. **Implement a structured task/activity event model
rather than having the UI scrape model text.**

**§10** Every tool invocation produces structured status: `ToolStarted`, `ToolProgress`,
`ToolCompleted`, `ToolFailed`, carrying tool name, human-readable description, timestamp, duration,
success/failure, optional progress and a result summary.

## §11–§23 The Tool API

Organise into domains. Do not expose everything at once, but design so tools can be added without
redesigning the orchestrator.

- **§12 project** — `get_state`, `get_metadata`, `create_snapshot`, `restore_snapshot`, `save`,
  `undo`, `redo`. The agent must be able to establish a rollback point before substantial work.
- **§13 scene inspection** — summary, list/find entities, get entity/component/hierarchy, selection,
  visible entities, active camera, lights, materials, environment, performance summary. **Do not
  dump enormous scene data into context. Favour queryable state and targeted questions.**
- **§14 entity** — create, delete, duplicate, rename, transform, get/set property, visibility,
  enabled, parent/unparent, select. **Do not create fake abstractions disconnected from the engine.**
- **§15 assets** — list, search, metadata, find similar, place, remove, replace, categories. The
  agent must understand identity, type, bounds, materials, scale, variants and performance
  implications. **Do not let the model hallucinate asset IDs — results carry canonical identifiers.**
- **§16 material** — list, get, set property, base colour, emission, roughness, metallic,
  transparency, assign, create. Understand HDR/emissive values. **Do not clamp merely because the UI
  historically showed SDR-like values.**
- **§17 lighting** — list, create, remove, set property/intensity/colour/range/type/shadow settings,
  link to entity. Coherent lighting passes, not isolated numbers.
- **§18 environment** — get, sky, sun, moon, atmosphere, fog, ambient, exposure, tonemapping,
  background.
- **§19 camera** — list, create, remove, set transform, look at, lens, FOV, focus, DOF, activate,
  create shot. The goal is cinematography, not coordinates.
- **§20 sequencer** *(a major part)* — state, tracks, clips (create/remove/move/resize/set property),
  keyframes, curves, interpolation, automation, parameter binding, camera shots, playhead, range.
  The agent must understand temporal relationships: *"start the camera movement four beats before the
  drop and reach the creature exactly on the drop"* requires querying tempo, markers and sequencer
  state rather than inventing seconds.
- **§21 audio / modulation** — analysis, tempo, onsets, bands, signals, parameters, modulation
  (list/create/remove/bind/depth/curve). The existing audio architecture is one of the most important
  assets available to the AI.
- **§22 world** — summary, place/remove asset, paint/modify terrain, create/modify water, fog,
  vegetation, regions, surface queries. **Do not invent a navmesh merely for AI support if the engine
  lacks one — provide capability discovery so the agent knows what actually exists.**
- **§23 performance** — frame time, GPU time, CPU time, draw calls, visible entities, particle count,
  light count, shadow cost, memory, renderer stats, profile region. The AI must treat performance as
  a first-class constraint and inspect it before answering *"make this look more impressive without
  increasing frame time."*

## §24 Capability registry

A machine-readable registry the AI queries to discover what AV Gen actually supports — scene,
materials, lighting, water, terrain, ecology, physics, navigation, sequencer, audio, modulation,
rendering, export. Each capability describes supported operations, required and optional parameters,
constraints, units, valid ranges, dependencies, whether an operation is expensive, whether it is
reversible, and whether it requires the main thread. **This registry is the source of truth used to
construct tool definitions. Do not duplicate tool descriptions in several unrelated places.**

## §25 Strict tool schemas

Every tool: stable name, description, structured parameter schema, validation, execution function,
structured result, error information, permission/safety classification, undo/transaction behaviour.
Validate entity and asset IDs, parameter names, numeric ranges, enums, timeline positions,
references and resource existence. Return structured, recoverable errors:

```
{ success: false, error: { code: "ENTITY_NOT_FOUND", message: "...", recovery: "..." } }
```

## §26 Transactions *(critical)*

Group AI modifications into coherent transactions: snapshot, execute, validate, commit or roll back.
**The user must be able to undo an entire AI task as one logical action** even if it made 73
underlying modifications. **Integrate with the existing undo architecture where it permits rather
than creating a second one.** If no suitable transaction architecture exists, implement the smallest
robust abstraction and document it.

## §27–§28 Failure and cancellation

Fail gracefully on: provider unavailable, auth failure, rate limit, malformed tool call, invalid
entity or property, renderer unavailable, unsupported operation, timeout, user cancellation, budget
exceeded, validation failure. **Never leave the project in an obviously corrupted partial state.**
Report what succeeded, what failed, why, what was rolled back and what is unchanged.

Cancellation propagates UI → orchestrator → provider → tool execution. **Do not merely stop
rendering the response while background mutations continue.** Make long operations cooperatively
cancellable, with deterministic rollback if cancellation lands mid-transaction.

## §29–§30 Context and system prompt

**Do not send the entire project on every request.** Layer it: system context (stable AV Gen
knowledge), capability context, project context (high level), task context (gathered for this task),
conversation context, and only relevant tool results. The agent queries deeper state when needed.

Maintain a system prompt describing what AV Gen is, its architecture, scene concepts, parameters,
modulation, sequencer, audio, rendering, tools, constraints, performance philosophy, transaction
behaviour and limitations. **Separate static product knowledge from dynamic state.**

## §31–§33 Agent behaviour

Behave like a capable technical and artistic director: inspect before changing, prefer coherent
multi-property changes, avoid unnecessary changes, respect explicit constraints, preserve existing
work unless told otherwise, ask questions only when genuinely necessary, use tools instead of
hallucinating, verify important results, consider performance, understand temporal and visual
relationships, summarise meaningful changes.

**Do not make the agent excessively conservative.** If the user says *"go wild"*, it should be
capable of a substantial creative pass.

**§32** Translate creative language into engine operations. *"Make it magical"* may involve palette,
emission, atmosphere, lighting, contrast, fog, camera, density, animation and audio reactivity.
*"Make it feel huge"* may involve composition, scale cues, depth layering, atmospheric perspective,
lens and lighting. Think in coordinated scene changes, not single-parameter edits.

**§33** Architect for inspect → modify → observe diagnostics → evaluate → modify again. **Where
visual inspection is not yet available, do not fake it** — inspect structured renderer and
performance state, and leave a clean extension point for image feedback.

## §34–§35 Observability and persistence

Log task ID, provider, model, request lifecycle, tool calls and durations, failures, cancellation,
usage data and transaction IDs. **Never log credentials.**

Conversation history belongs to a project or session; credentials belong to global settings.
**Never mix those storage domains.**

## §36–§38 Security, authority, performance

Treat AI-generated operations as untrusted input; **the tool layer is the security boundary.** No
arbitrary shell commands, filesystem writes, network requests, code execution, plugin execution or
GPU operations unless a separately designed capability is introduced later.

The user always has final control: cancel, undo, inspect activity, retry, start a new task, change
provider, disable AI. Consider confirmation for major destructive operations; **do not force
confirmation for every trivial change.** The goal is creative flow, not bureaucratic friction.

**The AI subsystem must not degrade normal operation when idle, and must never block the render
thread, the audio thread, real-time analysis or the UI thread.** Networking and inference are
asynchronous; main-thread-required mutation is marshalled correctly.

## §39–§40 Token efficiency and taxonomy

Prefer compact structured results to prose. Support truncation and pagination. Use a consistent
namespace: `scene.*`, `asset.*`, `material.*`, `lighting.*`, `camera.*`, `environment.*`, `world.*`,
`sequencer.*`, `audio.*`, `signal.*`, `parameter.*`, `modulation.*`, `performance.*`, `project.*`.

## §41 First implementation scope

**Do not attempt 200 tools.** Build a vertical slice: inspection (project, scene, entities, search,
selection, camera, lighting, materials, sequencer, audio, performance summaries); editing (entity
transform and property, material, light, environment and camera properties, create/delete/duplicate
where engine support is stable); sequencer (create track, add clip, move clip, keyframe, parameter
binding); audio (inspect analysis, parameter changes, modulation binding); safety (snapshot,
transaction, rollback, undo, cancellation). **This alone should allow genuinely impressive prompts.**

## §42 Acceptance tests

1. **Simple parameter edit** — *"Make the moon brighter."* Inspect environment, identify the light,
   modify, commit, report.
2. **Multi-property edit** — *"Make this scene feel more cinematic."* Inspect camera, environment and
   lighting; several coherent changes in one transaction; validate; summarise.
3. **Sequencer** — *"Create a 10-second camera push toward the selected object."*
4. **Audio reactive** — *"Make the glowing plants pulse with the bass."* Inspect signals, find the
   material, create the modulation relationship, configure depth, verify the binding.
5. **Complex request** — the nighttime alien forest prompt: inspect, plan, many tool calls, one
   transaction, several systems, a camera animation, validation, a meaningful summary.
6. **Failure** — an invalid entity operation returns a structured error the agent can recover from,
   with no corruption.
7. **Cancellation** — a multi-step task is cancelled; execution stops, transaction behaviour is
   deterministic, the project stays valid, the UI reports it.

## §43–§47 UI, model selection, defaults

Native UI using existing AV Gen conventions. **Do not introduce a web stack to render a chat
interface.** The panel shows the active provider/model lightly; credential management lives in
Settings. If a local model is available it may be the default lightweight assistant. If nothing is
configured, show *"AI assistant is not configured"* with navigation to Settings → AI — **do not
crash or disable the rest of AV Gen.** Support custom OpenAI-compatible endpoints, capability-
detecting streaming, tools, structured output and model listing where possible.

## §48–§49 Future-proofing and scope discipline

Leave clean extension points for vision models, image feedback, audio understanding, multimodal and
voice input, autonomous optimisation, long-running tasks, AI macros, project instructions, personas,
agent profiles, batch operations, automated render evaluation, external agents and MCP-like tool
exposure. **Implement none of them now.**

**There is a danger of building an elaborate theoretical agent framework before proving usefulness.
Avoid that.** Deliver `prompt → model → tool calls → real mutations → transaction → validation → UI
status → result`, working extremely well. Then expand.

## §50–§52 Documentation, code quality, testing

Document the control plane, provider, tool and capability architecture; the transaction, context,
security and threading models; the task lifecycle; and how to add a provider, add a tool, test tools
and extend the orchestrator. Add ADRs for major decisions. **Document *why*, not what.**

Follow existing conventions: small cohesive components, explicit ownership, RAII, strong typing,
deterministic behaviour, testable interfaces, clear error types, async where appropriate, minimal
global state. Avoid giant manager classes, singleton soup, UI-driven business logic, vendor logic
scattered through the application, stringly typed internals, hidden mutation and blocking calls on
real-time threads.

Test the provider abstraction, tool registry, schema validation, capability discovery, tool
execution and errors, transactions, rollback, cancellation, context generation, credential storage,
provider selection, conversation state and the task state machine, plus integration tests running
representative tool sequences against a controlled fixture. **Mock the provider — deterministic
engine tests must not require a cloud model.**

## §53–§55 Strategy, real integration, autonomy

Work incrementally: inspect, map, identify integration points, document the proposed architecture,
then provider abstraction → orchestrator → tool registry → inspection tools → mutation tools →
transactions → panel → settings → first end-to-end workflow → tests → iterate. **Do not rewrite
unrelated systems.**

**§54 — do not create mock or fake AV Gen APIs to make the demo work.** The first end-to-end path
must manipulate the real engine. Where an operation cannot yet be safely exposed: document the
limitation, expose a capability saying it is unavailable, identify the smallest engine abstraction
needed, implement it if appropriate, then add the tool on top. **Do not paper over architectural
gaps.**

Maintain explicit task boundaries and sensible iteration and tool-call budgets, configurable
internally even if the UI does not expose them. **The agent should never silently make unlimited
modifications.**

## §56–§58 Creative Director mode and success

Architect so a later Creative Director mode can take an outcome rather than instructions — *"take
this scene from amateur-looking to something usable in a professional music video"* — inspecting,
identifying weaknesses, forming a visual strategy, making coordinated changes, evaluating and
iterating. **Do not implement fake creative intelligence now; make the architecture capable of it.**

The user should not need to understand the engine. *"Make the water feel magical"* should not
require knowing which material, shader, render target, reflection parameter, lighting contribution or
exposure setting is involved — the agent discovers those through the tool API.

**Success:** a real user opens AV Gen, opens the AI panel, types a reasonably complex natural-language
request, and the application performs meaningful real modifications to the actual project.

---

## §59 Immediate task

Inspect the repository and existing documentation first. Produce an architecture assessment naming
relevant existing systems, reusable infrastructure, missing abstractions, the proposed control-plane
boundary, provider architecture, tool architecture, transaction integration, UI integration,
threading considerations, credential storage strategy and testing strategy.

**Then implement the first complete vertical slice. Do not stop at the architecture document.**
Prioritise a working, real, end-to-end AI workflow over dozens of theoretical tools.

Report: what was implemented; what changed; available tools; supported providers; what the panel
does; what Settings expose; how transactions and undo work; tests added; what remains; and any
architectural limitations discovered.

**Build this as a foundational AV Gen subsystem, not a temporary chatbot experiment.**

---

## Notes added when this was filed

**AI already exists in this repository, and ADR-065 governs it.** `src/ai/inference.{hpp,cpp}` is an
*image* inference subsystem — depth, segmentation, embedding — and the control plane is a new
subsystem beside it, not a replacement. ADR-065's precedents apply to both and were hard-won:

- **AI is optional.** A build with no backend simply has no such model and carries on. *"An optional
  failure must never become a total failure."* AV Gen must remain completely functional with no
  provider configured.
- **There is no synchronous entry point**, by design, so inference cannot be called from the render
  or audio thread even by accident. The control plane must preserve that property.
- **`InferenceBackend` is an interface and no third-party backend is bundled.** The provider
  abstraction should follow the same shape: the interface, the registry, and nothing above that line
  naming a vendor.
- **The built-in analytic backend is described everywhere as analytic, never as a learned model's
  output.** Keep that distinction; do not let a control plane blur it.

**Undo/redo is being built concurrently** by the world-authoring editor pass (§28 of
`docs/world-authoring-spec.md`). §26 forbids a second undo system. Until that lands, project
snapshots (§12's `create_snapshot` / `restore_snapshot`) are the transaction primitive, and the two
should be reconciled rather than duplicated.

---

# Addendum — research references and architectural guidance

Study these as reference material before major architectural decisions. **Do not blindly copy their
architectures** — extract the best ideas and adapt them to AV Gen's native C++23 architecture.

**§1 OpenAI Responses / tool calling** — <https://platform.openai.com/docs/quickstart>. The critical
concept is the loop: model requests a tool → application executes it → application returns a
structured result → model continues. **AV Gen implements that loop internally regardless of
provider.** Do not couple the tool layer to OpenAI's API types; define internal normalized types —
`AIMessage`, `AIToolDefinition`, `AIToolCall`, `AIToolResult`, `AITask`, `AIUsage`, `AIError` — and
have provider adapters translate.

**§2 Anthropic tool use and agentic workflows** — <https://docs.anthropic.com/en/docs/build-with-claude/prompt-engineering/prompt-templates-and-variables>.
Explicit tool-use instructions, proactive execution, multi-step workflows, adaptive reasoning,
long-running tasks, self-correction, progress reporting, parallel calls, verification, and balancing
autonomy against safety. **The design lesson: the model should act, not describe what the user could
do.** Bad: *"You could increase the emission on the plants."* Good: inspect plants → inspect
materials → modify emission → validate → report. The user asked the AI to operate the application,
so the default should be action-oriented when intent clearly implies modification.

**§3 Gemini function calling** — <https://ai.google.dev/gemini-api/docs/function-calling>. Note the
documented separation, and study sequential, parallel and compositional calling. **Independent
operations should eventually run in parallel where safe** — inspect lights, materials, camera and
performance together — while dependent ones stay sequential: inspect entity → obtain ID → modify.
**Do not blindly parallelize engine mutations.**

**§4–§6 Model Context Protocol** — <https://modelcontextprotocol.io/specification/2025-06-18/server/tools>.
The valuable distinction is **Tools** (things the model can do), **Resources** (context the
application provides: scene summary, selection, metadata, assets, timeline, audio analysis,
performance) and **Prompts** (reusable user-facing workflows: *Optimize Scene*, *Create Cinematic
Shot*, *Make Scene Audio Reactive*, *Improve Lighting*). **Do not implement the full protocol now.
Make AV Gen internally MCP-shaped** so exposing it over MCP later is straightforward — an external
agent reaching the same tool layer the AI panel uses.

Give every tool internal metadata: `name`, `description`, `inputSchema`, `outputSchema`, plus
`readOnly`, `mutatesProject`, `destructive`, `idempotent`, `expensive`, `requiresMainThread`,
`requiresRenderThread`, `requiresAudioThread`, `undoable`, `supportsCancellation`,
`supportsProgress`. The model need not see every field; **the orchestrator must**. This metadata is
the foundation for safety, scheduling, validation, parallelization, UI presentation, transaction
handling, tool selection and future MCP exposure.

**Human-in-the-loop:** make AI actions visible — *"AI is modifying 14 scene objects"*, not a scene
silently changing. But **do not confirm every harmless operation.** Classify: read-only (no
confirmation), normal mutation (automatic), destructive/high-impact (confirmation depending on an
autonomy setting), and external side effects — filesystem, network, publishing, hardware — which
**always** require explicit consent under a separate security model.

**§7–§8 llama.cpp** — <https://github.com/ggml-org/llama.cpp> and its
[server README](https://github.com/ggml-org/llama.cpp/blob/master/tools/server/README.md). An
unusually good reference: native C++, Apple Silicon focused, performance sensitive, with an
OpenAI-compatible server supporting tool calling, schema-constrained JSON, streaming and monitoring.
**Do not necessarily embed it.** Decide between `LocalModelProvider → llama.cpp` embedded and
`OpenAICompatibleProvider → localhost llama-server`; the latter keeps the runtime outside the engine
process and may be the cleaner start. **Make a reasoned decision** against startup time, memory,
distribution, packaging, Apple Silicon acceleration, model management, IPC overhead, failure
isolation and user experience. Long term: Local Fast / Local Reasoning / Cloud Fast / Cloud
Reasoning, with the user never needing to understand the runtime.

**§9 macOS credentials** — <https://developer.apple.com/documentation/security/keychain-services/>.
**Use Keychain rather than inventing encryption.** API keys, secrets and tokens live there; project
files carry only non-secret configuration (`provider`, `model`, `enabled`).

**§10–§12 AV Gen owns the abstraction.** Not OpenAI, not Anthropic, not Gemini, not MCP. The
internal Tool API is the engine's semantic interface; the AI is one client among future ones — an
MCP server, a script API, a remote/OSC API. **The test of the architecture: if the AI disappeared
tomorrow, could another application invoke `scene.find_entities`, `scene.set_property`,
`sequencer.add_keyframe`, `material.set_emission` as legitimate engine operations?** It should.

**§13 Resource model.** Distinguish state the orchestrator **provides automatically** every request —
project name, active scene, selection, current time, play state, active camera — from state the model
must **explicitly request**, such as all 300 vegetation entities. Literal URI resources are optional;
the separation is what matters.

**§14 Thinking / reasoning UX.** **Do not expose raw private chain-of-thought.** Expose structured
execution state instead — *Planning, Analyzing scene, Executing, Validating, Complete* — and
optionally the plan itself as a numbered list. This gives the user the useful part of agent
reasoning without making the architecture depend on exposing private model reasoning.

**§15 The agent loop is first-class.** `AgentTask → GatherContext → ModelTurn → ToolCalls? → (yes:
ExecuteTools → ToolResults → ModelTurn | no: FinalResponse)`. **Make the state machine explicit; do
not bury it inside a giant function.** States along the lines of: Idle, Preparing, Planning,
WaitingForModel, ExecutingTools, Validating, WaitingForApproval, Completed, Failed, Cancelled,
RollingBack.

**§16 Parallel tool execution.** Where safe, run reads concurrently — `scene.get_summary`,
`lighting.list`, `camera.list`, `performance.get_stats`, `audio.get_analysis`. Mutations need
explicit dependency semantics: `scene.create_entity` then `scene.set_property(newEntityId)` cannot
be parallelized. **The tool metadata should eventually let the scheduler reason about this.**

**§17 Long-running tasks.** Do not assume every request finishes in two seconds — large scene
analysis, procedural construction, optimization, render/evaluate/revise loops and large sequencer
creation may take ten seconds or ten minutes. **Task ID, progress, cancellation, persistence and
recovery are foundational concepts**, and the UI stays responsive throughout.

**§18 Verification is not optional.** For *"make the water reflect the moon"* the agent should not
call `material.set_reflection(0.8)` and declare success — it should inspect water, lighting and
renderer capabilities, modify, query the resulting state, verify and report. As visual inspection
arrives the architecture should allow modify → render preview → inspect → modify again. **Do not
fake visual verification before that capability exists.**

**§19 External architecture is inspiration, not a reason to rewrite AV Gen.** *This is critical.*
Do **not** replace the engine with an MCP server, replace the UI with a web application, rewrite the
renderer, introduce Python because an AI framework uses it, force an external agent framework into
the application, or restructure the project around a vendor SDK. Integrate the concepts. **The AI
layer should feel like it was designed as part of AV Gen from the beginning.**

**§20 Multimodal readiness.** Keep the architecture ready for models that accept screenshots,
rendered frames, thumbnails, scene diagrams, audio and analysis data — eventually a screenshot plus
scene state plus performance metrics plus audio state would make Creative Director mode far more
capable. **Do not implement multimodal prematurely** unless the provider architecture naturally
supports it.

**§21 External agent interoperability.** Preserve the possibility of an external agent driving AV Gen
through the same semantic interface over MCP. That is a reason to keep the internal tool API clean
and protocol-neutral.

**§22 Research order, and an architecture note before locking the implementation.** Study, in order:
OpenAI Responses/tool calling; Anthropic tool use and agentic guidance; Gemini function calling; MCP
tools/resources/prompts; llama.cpp server and tool calling; Apple Keychain. Then produce a short
internal note answering: **What should AV Gen copy? What should it deliberately not copy? What
should be normalized across providers? What stays provider-specific? Which MCP concepts are useful
internally? Should local inference be embedded or externalized? How should tool metadata be
represented? How should long-running tasks work? How should transactions interact with agent loops?**
Do this analysis *before* locking the implementation.

**§23 Reference library** — keep in the architecture documentation:
[OpenAI Responses](https://platform.openai.com/docs/quickstart/make-your-first-api-request) ·
[Anthropic agentic guidance](https://docs.anthropic.com/en/docs/build-with-claude/prompt-engineering/prompt-templates-and-variables) ·
[Gemini function calling](https://ai.google.dev/gemini-api/docs/function-calling) ·
[MCP tools](https://modelcontextprotocol.io/specification/2025-06-18/server/tools) ·
[MCP architecture](https://modelcontextprotocol.io/specification/2024-11-05/server/index) ·
[llama.cpp](https://github.com/ggml-org/llama.cpp) ·
[llama.cpp server](https://github.com/ggml-org/llama.cpp/blob/master/tools/server/README.md) ·
[Keychain Services](https://developer.apple.com/documentation/security/keychain-services/) ·
[Keychain user secrets](https://developer.apple.com/documentation/security/using-the-keychain-to-manage-user-secrets)

**§24 Final direction.** The goal is not *"add AI chat to AV Gen"*. It is to **turn AV Gen into an
AI-operable audiovisual engine** — a model that understands scenes, worlds, assets, materials,
lights, cameras, animation, the sequencer, audio, modulation, parameters, rendering, performance,
cinematography and visual style, and operates them through a structured semantic interface.

**The prompt panel is the first user-facing manifestation. The underlying Tool API is the actual
feature.** Build that foundation correctly and increasingly capable models make AV Gen progressively
more powerful without redesigning the engine each time.
