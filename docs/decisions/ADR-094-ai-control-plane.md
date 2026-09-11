# ADR-094: The AI control plane — a tool API the engine owns, and one client of it

Status: Accepted

## Context

`docs/ai-control-plane-spec.md` asks for a natural-language interface through which a user can
inspect, modify, direct and operate AV Gen, and is emphatic about what it is not: *"The prompt
panel is the first user-facing manifestation. The underlying Tool API is the actual feature."*

AI already exists in this repository. ADR-065 governs `src/ai/inference.*`, an *image* inference
subsystem, and establishes four precedents that were hard-won and apply here unchanged: AI is
optional and an optional failure must never become a total failure; there is deliberately no
synchronous entry point, so inference cannot reach the render or audio thread even by accident; the
backend is an interface and nothing above that line names a vendor; and the built-in analytic
backend is described everywhere as analytic, never as a learned model's output.

Two constraints shaped the scope. Four other passes are reshaping terrain, water, the editor's
selection and undo, and navigation, so a broad tool surface would be built on APIs in flux. And
§26 forbids building a second undo system while the editor's is being written.

The research behind the decisions below is in `docs/research/ai-control-plane.md`, which is the
note addendum §22 asks for.

## Problem

Five things have to be true at once, and the obvious designs get three.

1. **The tool API must be the engine's semantic interface, not the AI's private one.** The test the
   brief sets: if the AI disappeared tomorrow, could another application invoke
   `scene.find_entities`, `parameter.set`, `sequencer.add_keyframe` as legitimate engine
   operations? It should.
2. **Nothing may block the render, audio, analysis or UI thread**, while every tool must touch
   engine state that the frame loop reads unlocked every frame.
3. **A whole task must undo as one action**, without building a second undo system.
4. **AV Gen must be completely functional with nothing configured.**
5. **It must actually work end to end.** This repository has seven times built a system, tested it,
   and wired it into nothing.

## Decision

### The tool API is built on `params::ParameterSet`

Not as a lowest common denominator — as the engine's actual semantic surface. A node's position,
rotation, scale, visibility, emissive boost and roughness are registered parameters. So are the
camera's pose, mode and field of view; the sky's colours, sun intensity and haze; fog, wind,
brightness and volumetrics; a light rig's per-light intensity, angle and colour temperature; every
procedural material part's tint, emissive colour and roughness. Each has a path, a type, hard and
soft ranges, a group and a label, and each is *already* keyframeable by the timeline, modulatable
by audio, and serialised.

So a parameter-level tool API reaches most of §14–§19's intent, costs no new engine abstraction,
and depends on nothing the concurrent passes are moving. Where it does not reach — creating and
deleting nodes, terrain, water, editing an individual light — the capability registry says so and
says why, rather than papering over it (§54).

### Three guards against silence

This repository's characteristic failure is a feature that is correct in every respect except that
it does nothing. Three of those failure modes are reachable from this API, and each has a guard and
a test that fails if the guard is removed:

- `params::Timeline::bind` **skips tracks naming an unknown parameter and says nothing** — four
  separate "does nothing and says nothing" bugs. `sequencer.add_keyframe` refuses such a target
  outright, and `sequencer.get_state` reports `unboundTargets()` on every read.
- A `Replace`-mode track or route **rewrites a base value every frame**, so setting it looks
  identical to setting it successfully. `parameter.set` detects that and returns the change *plus*
  a note naming what overrides it.
- `camera/position` **is ignored in orbit mode**. `camera.set` switches the mode and says it did,
  rather than writing two vectors nothing reads.

Every mutating tool reports the value the engine holds *afterwards*, read back — never the value it
was asked for.

### The provider abstraction is an interface and a registry

Exactly ADR-065's shape. `providers.cpp` is the only translation unit in AV Gen that names a
vendor; the orchestrator, the tools, the panel and the tests see only `ai::Provider`. Adding one is
a class and a table row.

Internal normalized types — `Message`, `ToolCall`, `ToolCallResult`, `Usage`, `StopReason` — because
every provider spells the same four ideas differently and picking one vendor's spelling as
canonical makes the other adapters translate twice. There is deliberately **no** model-facing tool
type: a provider is handed `const ai::ToolDefinition&` and projects the three fields a model may
see, so the registry cannot disagree with the copy sent to the model.

### Local inference is externalized, not embedded

`llama-server` in a separate process, reached by the same OpenAI-compatible adapter. Four reasons,
in order of weight: llama.cpp does not install the headers a chat client actually needs (chat
templating, the tool-call parser, `json_schema_to_grammar` all live in the un-exported `common/`);
its C API is pre-1.0 and breaks every few weeks; an embedded LLM does Metal compute on the same
device as a realtime renderer, which is ADR-065's realtime-budget argument at a much larger scale;
and a crash there would take the editor down with the user's unsaved project. It also costs no
adapter, because `llama-server` speaks OpenAI chat completions.

The cost — the user runs a server — is mitigated by a loopback default endpoint and a Test
connection button. **Revisit when** a shippable small model exists *and* Metal contention against
Dawn has been measured.

### Threading: decide on a worker, act on the frame thread

The agent loop runs on a `JobSystem` worker because it blocks on the network. Every tool body is
marshalled onto the frame thread through `ai::MainThreadQueue`, because `ParameterSet`,
`Modulator` and `Timeline` are read by `Engine::update()` every frame with no lock anywhere — the
same split ADR-066 made for Generate World.

Every tool goes through the queue, reads included: a read while the main thread writes finals is
still a race, and `ToolAnnotations::requiresMainThread` records the per-tool answer so a later
scheduler can relax it with evidence rather than optimism. `pump()` takes a time budget, so a slow
tool costs one slice rather than a frame. A call from the pumping thread runs inline rather than
waiting for a pump that cannot come.

ADR-065's structural rule is kept: nothing reachable from the render, audio or UI thread holds a
`Provider`, because the control plane only hands one out inside a job body.

### Transactions are project snapshots, and `TransactionSink` is the seam

§26 forbids a second undo system, and the editor pass is building one now. So this does two
separate things and keeps them apart.

`SnapshotStore` is the transaction *primitive*: a whole-document capture built on
`params::saveProject`/`loadProject`, which already round-trip and are already tested. It covers
parameter base values, modulation routes, presets and the timeline — **exactly** the set the tools
in this pass can change, so a rollback is complete rather than approximately complete.

`TransactionSink` is the *seam*: one virtual with `begin`/`commit`/`abort`/`available`. The default
implementation is snapshot-backed. When the editor's undo stack lands, the application installs a
sink that opens a compound undo group instead, and the orchestrator, the tools and the tests do not
change. That is the whole reconciliation: one file to write, at merge.

One transaction per task, opened lazily on the first mutating tool. A tool *error* does not roll
back — that is information the agent is expected to act on, and rolling back because one call had a
typo would make self-correction impossible.

### Credentials in the keychain, described accurately

`kSecClassGenericPassword` in the **file-based** keychain, add-then-update-on-duplicate, no
`kSecAttrAccessible` and no access group. Each of those is a measured decision recorded in the
research note: the data-protection keychain returns `errSecMissingEntitlement` for a binary run out
of a build directory, and the accessibility attributes are silently dropped on the file-based one.

`ai::ProviderConfig` has **no field for a secret**, so serialising configuration cannot leak one,
and `fromJson` rejects a document that carries a key-shaped field rather than reading it.
`AVGEN_AI_<PROVIDER>_KEY` is consulted *first*, because a headless or SSH session cannot always
read the keystore — and because it wins, `status()` reports which source is in effect so the UI
cannot claim "stored in keychain" while a stale variable is actually in use.

What the keychain buys is stated exactly, in the header, the ADR and the Settings panel: the key is
not in a file you might share or commit, and the user can rotate it where they already look. It is
**not** isolation from other local code running as the same user — that was probed, and
`/usr/bin/security` printed the secret with no interaction. Overstating it would be worse than not
using it.

## Consequences

**The tool API is usable without any AI.** The registry is built, populated and driven by tests
with no provider anywhere. An MCP server, a script API or a remote control surface would attach to
the same registry.

**A capability registry that reports absence.** Seven domains are marked unavailable with the
reason and the smallest engine change that would lift each one. That is more useful to an agent
than the twelve that are available.

**`mutatesSession` had to exist.** The invariant in `ToolRegistry::add` — every tool is classified
as a read or a write — immediately caught `sequencer.set_playhead`, which is neither: it changes
the transport, which is session state and outside the snapshot domain. Claiming `mutatesProject`
would have promised a rollback that cannot happen; claiming `readOnly` would have been a lie.

**Application-level settings now exist.** AV Gen had none; a credential is per-installation rather
than per-project, so §7 forced the smallest general version into being. A project file carries none
of it, so opening someone else's project cannot change which provider you use.

**Streaming is designed for and not implemented.** `ProviderCapabilities::streaming` says so rather
than pretending, and `CompletionRequest::onTextDelta` is honoured by calling it once with the whole
text, so a caller written against the callback works unchanged when SSE lands. §9 wants structured
progress rather than scraped model text, and that is what the panel shows, so the user-facing cost
today is small.

**A per-frame obligation of one queue check.** With no task running, `pump()` is a mutex and an
empty-deque test, measured in the `ai.pump` phase like everything else.

## Rejected alternatives

**Generate tool definitions from an authored capability table** (§24's literal reading). It puts a
tool's name, schema and description in data and its behaviour in code, and the two drift the first
time somebody edits one. The dependency is inverted instead: definitions are the source of truth,
the capability document is derived, and the *authored* half is only the domains that report absence.

**Embed llama.cpp.** See above; the decisive fact is that the headers a chat client needs are not
exported, so embedding means vendoring the tree or reimplementing Jinja.

**A diff-based transaction.** Restoring a diff requires knowing the inverse of every operation;
restoring a document requires knowing how to load one, which this engine has done since milestone
0.9. A project's parameter block is a few hundred numbers.

**A separate `LocalModelProvider` class.** Local inference is a different endpoint and a different
default, not different code. It is a separate *registry entry* so a user can choose "Local" (§5),
sharing the OpenAI-compatible adapter.

**Per-tool transactions.** The user asked for one thing. Undoing "make the scene cinematic" halfway
is not an outcome anybody wants.

**Exposing model reasoning in the panel.** Addendum §14 rules it out on principle; the practical
argument is stronger — the default for thinking display changed to `omitted` across a whole model
generation, and a UI built on it would have silently gone blank.

**Adding an HTTP library.** ADR-008 asks what a dependency buys. `NSURLSession` is the system's own
client with TLS, proxies and the user's certificate trust, and the repository already uses the
`.mm`-plus-portable-file pattern five times (MIDI, fonts, video, Syphon, text).

## Revisit triggers

- **The editor's undo lands.** Write the `TransactionSink` over it, then add node creation and
  deletion, which are only excluded because they fall outside the snapshot domain.
- **Terrain, water and navigation settle.** Their tool surfaces are named as unavailable, with
  reasons, and should be added once their parameters stop moving.
- **Streaming becomes user-visible.** SSE is a per-adapter parse behind an unchanged interface.
- **Image feedback.** `ProviderCapabilities::vision` already reports per-provider support; the
  extension point is a tool that hands the agent a rendered frame. Until then the capability
  registry says the agent cannot see the image, because faking visual verification would be worse
  than not having it.
- **A shippable local model.** Then measure Metal contention against Dawn before reconsidering
  embedding.
