# Director Agent: architecture and feasibility report

**Status:** Investigation only. Nothing was implemented.
**Date:** 2026-09-21
**Scope:** Should the existing LLM/AI plane be turned into an AI-powered Director Agent, and what would that take?

**Where each finding comes from:**
- **[main]**: `av-gen` at `e61778df`.
- **[AR]**: `agent/anim-research` at `69d61c60`.
- **[PD]**: `agent/anim-phase-d` at `59d7521c`, plus uncommitted work in progress in that worktree, read as it stands.
- A finding without a label holds on all three.
- Every claim below was checked against source. Where a doc and the code disagree, the code wins and the disagreement is recorded.

---

## 0. Executive summary

**Verdict: YES, but only after significant semantic and API work.**

The foundation is stronger than the brief assumes, in three places:

1. **The engine already has an in-process agent control plane**, and it is the right shape: `src/ai/`, about 9,000 lines, ADR-094. It provides:
   - 58 schema-validated tools with MCP-style annotations
   - a closed error vocabulary with recovery hints
   - an explicit agent loop with budgets
   - one transaction per task
   - snapshot rollback
   - a seam into the editor's undo history
   - five provider adapters: Anthropic, OpenAI, Gemini, OpenAI-compatible, and local llama-server
   - an in-editor panel
   - a scripted provider for deterministic tests

   Its founding rule is already the Director's: *"if the AI disappeared tomorrow, could another application invoke these as legitimate engine operations?"* (`src/ai/tool_api.hpp:6`). Nothing in it needs to be thrown away.

2. **The engine already bakes intent into ordinary editable content.** Every Director output can be expressed in structures that already exist, serialise and are editable by hand:
   - `seq::Sequence`: shots, actors with keyed paths and clip cues, markers, and `SequenceEvent`s with 12 trigger kinds and 7 action kinds.
   - `scene::CameraDirection`: authored rigs and a camera track.
   - World-effect activation windows.
   - `params::Timeline` keys.

   The event system's three tiers (baked, scheduled, live; `src/seq/events.hpp`) are exactly the determinism boundary a Director has to respect.

3. **Character control already has a Director authority tier.** `entity::Authority::Director`, `EntityWorld::direct()`, `stage::Staging`, and on [PD] a `goal` considerer that biases the autonomous decider. The "director states goals, the character realises them" contract that Phase F asks for is half built.

**What is missing is the middle, and the bottom of the Rook/Umbra example.**

- **Missing middle.** There is no layer that compiles "Rook runs to Umbra, jumps, backflips" into those native structures. The 58 tools are low-level: `parameter.set`, `sequencer.add_keyframe`, `sequence.add_shot(name,start,duration,scene)`. None of them can reach characters, staging, camera rigs, the camera track, sequence events, world effects or shot cameras (`docs/character-ai-plan.md` P10 records the staging gap).
- **Missing bottom.** The motion system cannot perform the stunt:
  - **No backflip clip exists.** The alien pack has 26 clips and 57.9 s of motion, all in place. It has `Jump_running`, `Jumping`, `Fall_loop` and `Landing`, but no flip, roll or somersault.
  - **No action can request a jump toward a target.** `Airborne::launch` is called only from the autonomous `explore` behaviour.
  - Every action `Move` decelerates to a stop at its goal.
  - Every clip state loops.
  - No clip carries event markers.
  - Camera behaviours in the baked tier can only target `seq::Actor`s, and chase offsets are constant per shot.
  - There is no slow motion or time remap of any kind; the transport pauses audio away from 1x.

**The biggest structural risk is not the LLM. It is that "trigger X at Rook's backflip peak" is a live-tier event** (ADR-091). Its time is unknowable before the simulation runs, the live-tier dispatcher has no producer [main], and a live event cannot be baked into a reproducible render.

The resolution this report recommends is the most important architectural finding: **a Director should author planned performances in the baked tier** (a `seq::Actor` with a computed path and clip cues, which is a pure function of time). There, every semantic event time (take-off, peak, landing) is computed at plan time, and cameras, effects and retiming bake to keys, just as the rest of the sequencer does. Autonomous (live-tier) characters remain for background life and for "goal" direction in the Phase F style.

**Numbers:**
- **Reuse:** roughly 60–70% of the needed infrastructure exists: tool API, loop, transactions, event model, sequence model, camera model, determinism machinery, character authority tiers. Nearly all of the *semantic* layer does not.
- **MVP:** a Director that plans a shot, camera, markers, events and parameter automation for existing capabilities, with preview and one-step undo. About **8–12 developer-weeks**.
- **Full brief:** the Rook/Umbra shot end to end, including new animation semantics, camera semantics, retiming, full undo coverage and verification. About **9–15 developer-months**, **excluding acquiring a backflip clip**, which is an asset problem, not a code problem.
- **A Laya-like fast decision model does not belong in the architecture now.** Laya is a three-day-old, Python-only enterprise text classifier from a company unrelated to the Convai NPC platform. Phase D's decider costs about 0.011 ms per character per decision and needs no model. Keep the seam, which Phase D §71 already names (`LearnedBehaviorProvider`), and design for it later.

**Do first:** make the semantic plan a first-class, serialisable, validated document (the "Director Plan"), before anything else:
- Compile it to existing native content through new tools for sequence, camera, events and effects.
- Close the undo gap for those domains.
- Build the MVP on shots that need no new motion.

---

## 1. Current architecture findings (repository archaeology)

Traced from source. Paths are relative to the repo root.

| Area | Where it actually lives | Notes |
|---|---|---|
| **Startup** | `src/app/main.cpp:11`: `parseArgs` → `Application::init` → `run()` (`application.cpp:3884`) → `runLive()` (SDL loop, :3909) or `runHeadless()` (:5525) | One binary, `avgen`. Headless flags: `--render`, `--queue`, `--capture`, `--save-project`, `--ai-prompt`, `--ai-script`, … |
| **Editor** | `src/ui/`. The shell is `editor_shell.cpp`/`editor_layout.cpp` (ADR-076). `ControlPanel` is a 4,673-line hub hosting `WorldEditor`, lights, the sequencer, and `AiPanel` | Dear ImGui. The panels mutate `Engine`/`Composition` directly (§14). |
| **Canonical state** | Not one document. It is spread across `app::Engine` members: `params_`, `modulator_`, `timeline_`, `sequence_`, `autoDirector_`, `songPlan_`, `transport_`, and `controller_` (which holds `scene::Composition`) | This matters to a Director: there is no single model to diff. |
| **Scene/entity/character** | `scene::Composition` (`src/scene/composition.hpp:541`); `entity::EntityDesc` (`src/entity/entity.hpp:95`); `EntityWorld` | Characters are entities that drive a glTF node, referenced **by string name**: `rook`, `tide`, `sage`, `ember` and `vane` in `glowmere-valley-2-multicam.scene.json`. |
| **Animation** | `scene::AnimationPlayer` (`src/scene/animation.hpp:119`), a small state machine with cross-fades; `IMotionProvider`/`MotionChain` (`src/entity/motion_provider.hpp`); `ClipMotionProvider`; `MatchMotionProvider` (wired opt-in on [AR] by ADR-623, unwired on [main]) | See §5. |
| **IK** | `src/scene/ik.hpp`: analytic `solveTwoBone`, `solveBodyCompensation`, arbitrary-chain write-back (ADR-543); pose layers in `src/scene/pose_layers.hpp` | No FABRIK, CCD or full-body IK. |
| **Locomotion** | `entity::Gait` (`gait.hpp`); `Activity` enum (`locomotion.hpp:30`); `entity/airborne.hpp` (ADR-194); [AR] `locomotion_plan.hpp` | Travel is code-driven because the clips are in place. |
| **Navigation** | `NavGrid` (`src/entity/nav_grid.hpp:186`, 4 m cells, A*); `Navigator` (`navigation.hpp:152`) | No navmesh (Recast was rejected), no RVO/ORCA. |
| **Cameras (evaluated)** | `scene::CameraRig`, `CameraShot` and `CameraDirection` (`src/scene/camera_rig.hpp`); `Composition::evaluateAuthoredCamera` (`composition.cpp:~6701`) | Per-frame; can follow and aim at **any node**, including entities. |
| **Cameras (baked)** | `seq::ShotCamera`, `CameraPreset` (10), `CameraBehavior` (Chase/Orbit/Pov) (`src/seq/sequence.hpp:108-300`); `cameraPoseFor` (`sequence.cpp:536`) | Baked to keys at install; can target only `seq::Actor`s. |
| **Auto-director (camera)** | `src/app/camera_director.hpp`, `song_director.hpp`, `cinematic.hpp` (`app::Shot`, 14 `ShotKind`s) | Writes `CameraShot`s with `Origin::Directed`. |
| **Staging director (behaviour)** | `src/stage/staging.hpp` (ADR-210; cited in code as "ADR-209", a number collision) | Actors, scenarios, beats, 13 step kinds, issued at `Authority::Director`. |
| **Timeline/sequencer** | `seq::Sequence` (`sequence.hpp:562`), baked by `Sequence::bake()` (`sequence.cpp:1313`), installed by `seq::install()`; `params::Timeline` | Stored in the **project** file under `"sequence"`. |
| **Shots** | Three unrelated types: `app::Shot` (a subject-relative move), `seq::Shot` (the edit), `scene::CameraShot` (which camera is live) | ADR-245. The most common source of wrong answers. |
| **Transitions** | `seq::TransitionKind {Cut, FadeIn, FadeOut, MatchCut}`; `scene::ShotTransition {Cut, Blend}` | No 3D crossfade. |
| **Audio analysis/sections** | `analysis::SongStructure` (`src/analysis/structure.hpp:141`), 13 `SectionFunction`s; `song::SectionTimeline` (`src/song/section_timeline.hpp:170`) | "The chorus" is resolvable by walking sections; there is no `resolveTime("chorus 2")` helper. |
| **Transport/playhead** | `app::Transport` (`src/app/transport.hpp:132`, ADR-102/103) | Two clocks: `renderTime` (free-running) and the transport position. |
| **Parameters/modulation** | `params::ParameterSet` (string paths, `ParamDesc` with ranges and flags); `params::ModRoute`; `signals::SignalBus` | About 6,100 parameters in the Glowmere multicam project. |
| **World and hero effects** | `src/world/effects.hpp` (activation Always/Window/CameraTravel/HeroFocus); `EffectSchema` registry (`src/world/world_effects/effect_registry.hpp:431`, ADR-500, 6 atmosphere kinds); heroes in `src/world/hero.hpp` | "Umbra" is the hero `umbra-cap` (a glowing mushroom). **There is no `umbra_hero` effect** in either tree. |
| **Temporal effects** | `scene::TemporalEffectKind::FrameEcho` (`src/scene/temporal_settings.hpp:35`); parameters `temporal/echo/*` | This is the nearest thing to "frame drag". It is **not** slow motion. |
| **Rendering** | `src/rendering/`, Dawn/WebGPU on Metal | Out of the Director's scope except for preview. |
| **Serialisation** | nlohmann::json. `params::saveProject`/`loadProject` (`src/params/serialization.hpp`, format v4); `Composition::toJson`/`fromJson` (scene v1) | The project overrides the scene (ADR-264). |
| **Undo/redo** | `ui::EditHistory`/`EditCommand` (`src/ui/edit_history.hpp:148,215`); `app::EditSystem` (`src/app/edit_system.hpp:153`, ADR-101) | Record-of-edit, but many panels bypass it (§14). |
| **Editor commands** | `EditAction` (9 verbs) → `EditSystem::execute` → the focused `EditContext` (only `WorldEditor`) | **There is no general command bus.** |
| **Preview/render** | `--render`/`--queue`; `Application::startRenderFromUi` (`application.cpp:2421`) **saves the project and renders that file in a separate Engine** | A preview render is a save (§15). |
| **Scripting/events** | `seq::SequenceEvent` + `EventDispatcher` (ADR-098); `entity::ActionEvent`; `Schedule`; trigger volumes (`field.<name>.enter`); OSC/MIDI (`src/control/`) | There is no general scripting language. |
| **AI/LLM** | `src/ai/*` (ADR-094); `src/ui/ai_panel.cpp`; `src/app/ai_edit_sink.*` | §2. |
| **Automation interfaces** | OSC (`/avgen/param`, `/transport`, `/state`, `/query`); `--ai-script`; `--ui-script` | None of them reaches the tool registry from outside. |

**Doc and code drift that affects this work:**
- `docs/ai-control-plane.md` says 34 tools; there are 58.
- The comment in `transaction.hpp` says snapshots cover "the parameter domain" only. `captureDocument` (`transaction.cpp:44`) now also captures the composition, the sequence and the timeline.
- `capability.list` still marks the `entity` and `render` domains unavailable while it lists tools in both.
- "ADR-360: a render must be reproducible" is a mis-citation. ADR-360 is about the tree ignoring the wind; the rule itself is stated in `EntityWorld::seek` and cited by ADR-670/671 [PD].
- ADR-617 and ADR-618 exist only on [AR] and [PD], not on [main].
- The rule "director observes, does not dictate" exists only as spec text (Phase F §78, §89, §102 on [AR]/[PD]). **No code enforces it.**

---

## 2. Existing LLM plane

### 2.1 What it is

It is a real in-process agent that calls vendor APIs itself.

- **`ai::ControlPlane`** (`src/ai/control_plane.hpp`) is the facade. It is created in `Application::initControlPlane()` (`application.cpp:921`) in GUI and headless sessions alike, and pumped every frame (`ai_->pump()`).
- **`ToolRegistry` / `ToolDefinition` / `ToolAnnotations` / `ToolResult` / `ToolError`** (`src/ai/tool_api.hpp`):
  - The input schema is validated before the tool body runs (`validateAgainstSchema`, a deliberately small JSON Schema subset).
  - Annotations: readOnly, mutatesProject, mutatesSession, destructive, idempotent, expensive, thread requirements, undoable. These mirror MCP's tool annotations.
- **`Orchestrator` / `AgentTask` / `TaskState` / `Activity`** (`src/ai/orchestrator.hpp`):
  - An explicit state machine, `GatherContext → ModelTurn → ExecuteTools → … → Validate → Commit`.
  - Budgets: 12 iterations, 64 tool calls, 300 s.
  - Structured activity events feed the UI without scraping model prose.
  - One lazy transaction per task. Rollback happens on provider error, cancel or budget exhaustion; **not** on a single tool error, which the model is expected to recover from.
- **`MainThreadQueue`** marshals tool bodies onto the frame thread.
- **`systemPrompt()` / `ambientContext()`** (`src/ai/context.hpp`):
  - A stable, registry-generated system prompt, which can be cached.
  - An O(1) ambient-state block.
  - Everything O(n) is pulled through tools. **This is already the right answer to context explosion.**
- **Providers** (`src/ai/providers.cpp`): `AnthropicProvider` (L201, `/v1/messages`), `OpenAiProvider` (L323), `GeminiProvider` (L486), `openai-compatible`, and `local` (llama-server).
  - Credentials come from the Keychain or `AVGEN_AI_<PROVIDER>_KEY` (`src/ai/credentials.*`).
  - Transport: `http_darwin.mm` (NSURLSession).
- **`ScriptedProvider`** replays a JSON list of tool calls (`--ai-script`, `examples/ai/atmosphere.ai.json`). It is the deterministic test harness.
- **UI:** `src/ui/ai_panel.cpp` provides prompt, cancel, new conversation and "Undo this task" (L182). Settings → AI configures providers.
- **Tests:** `tests/unit/test_ai_tools.cpp` (46 cases against a real Engine), `test_ai_provider.cpp` (15), and `tests/integration/test_ai_control_plane.cpp` (13).
- **Branches:** byte-identical across [main], [AR] and [PD].

### 2.2 The 58 tools, by what a Director would need

| Domain | Tools | Director relevance |
|---|---|---|
| Introspection | `capability.list`, `capability.list_tools`, `project.get_state`, `scene.get_summary`, `scene.find_nodes`, `scene.get_node`, `entity.list`, `parameter.search/get/list_groups`, `sequence.get_state`, `sequencer.get_state`, `audio.get_analysis`, `signal.list`, `modulation.list`, `camera.get`, `environment.get`, `lighting.list`, `material.list`, `render.probe` (numbers, not an image), `performance.get_stats` | Good base. `entity.list` returns no clips, activities or capabilities. |
| Low-level mutation | `parameter.set/reset`, `sequencer.add_keyframe/remove_track`, `modulation.create/set_depth/remove`, `environment.set`, `camera.set`, `camera.frame_node`, `scene.create_node/delete_node/set_parent/set_node_transform/set_node_visible`, `field.create`, `world.generate` | Usable as "escape hatch" tools. |
| Sequence | `sequence.add_marker`, `sequence.add_shot{name,start,duration,scene}` (camera left as Inherit), `sequence.add_overlay` | **No move, trim, remove, set-camera, set-transition, add-event or add-actor.** |
| Session | `project.create/save/save_as/open`, `asset.import/import_audio`, `sequencer.set_playhead`, snapshots | |
| **Absent** | characters and entity actions, staging, camera rigs, the camera track (`CameraShot`), `SequenceEvent`s, `seq::Actor`, world effects, temporal effects, undo/redo, render-to-file, screenshots and vision, selection | |

### 2.3 Capability classification

| Capability | Status | Evidence |
|---|---|---|
| Tool calling | **Already exists** | Native per vendor (`providers.cpp` L234/393/543); tools run sequentially. |
| Structured tool input (schema) | **Already exists** | `validateAgainstSchema`; `additionalProperties:false`. |
| Structured output (response schemas) | **Partially exists** | `outputSchema` field is present but usually null; no response_format use. |
| Command abstraction | **Already exists** | `Tool{definition, execute}`. |
| Execution results and errors back to the model | **Already exists** | `ToolResult{success,value,summary,error{code,message,recovery}}`; closed `ToolErrorCode`. |
| Inspect project, timeline, cameras | **Partially exists** | Read tools exist. Characters, staging, rigs and events are not readable. |
| Modify shots | **Partially exists** | Add only, with no camera. |
| Modify characters | **Missing** | P10: nothing reaches `stage::Staging` or `EntityWorld::direct`. |
| Modify cameras | **Exists but unsuitable** | `camera.set` moves the main camera's parameters; no rigs, camera track or shot camera. |
| Modify effects | **Partially exists** | Only as raw parameters (`atmos/<kind>/<leaf>`, world-effect params); no structural add or remove. |
| Preview/render | **Partially exists** | `render.probe` gives numbers only; no frame capture or render job tool. |
| Persistent context | **Missing** | Each task starts a fresh conversation (`orchestrator.cpp:205`). |
| Capability concept | **Partially exists, stale** | `capability.list` from a hand-written `kDomains`. |
| Safety/validation | **Already exists** (argument level) | Schema, name-only project paths, content roots, read-back with conflict notes. |
| Transactions | **Already exists** | `Transaction`/`TransactionSink`/`SnapshotStore`. |
| Undo integration | **Partially exists** | `EditHistoryTransactionSink` (`src/app/ai_edit_sink.hpp:49`) records **parameter diffs only**. "Undo this task" restores a snapshot outside `EditHistory`, which leaves two undo mechanisms. |
| Dry run / plan-then-approve | **Missing** | No `WaitingForApproval` state. |
| External agent access (MCP, socket) | **Missing** | Only designed for ("a serialisation exercise", `tool_api.hpp:22`). |
| Streaming, vision, parallel reads | **Missing** | `ProviderCapabilities::streaming=false`, `vision=false`. |

---

## 3. Director Agent concept

A Director Agent is **an authoring-time collaborator** that turns cinematic intent into ordinary AV Gen content: shots, cameras, character performances, events and effect automation. The user can preview, edit by hand, undo, and render deterministically.

It is not a runtime brain. This agrees with every owner spec that touches the subject:
- Phase D §50: "Do not put an LLM in the frame loop."
- Phase E §35: "No LLM in the Core."
- Phase F §32: language may produce goals, and "must not control joints, IK, navigation internals, renderer, frame timing".
- Phase F §78/§89: "Do not automatically replace authored cinematography… the human author remains director."

The concept therefore resolves into this rule:

> **The LLM writes plans; a deterministic compiler writes content; the runtime never sees the LLM.**

---

## 4. Semantic command architecture (brief §3, §4 and §13)

### 4.1 The proposed commands, one by one

Complexity: XS < 2 days, S ≤ 1 week, M 1–3 weeks, L 3–6 weeks, XL 6–12 weeks, XXL > 12 weeks.

| Command | Existing implementation | Existing interface | Safe for an agent today? | Abstraction needed | Cx |
|---|---|---|---|---|---|
| `inspect_scene` | `scene.get_summary`, `find_nodes`, `entity.list`, heroes in the composition | Tools | Yes (read-only) | A **semantic scene digest**: named subjects (characters, heroes, landmarks), positions, tags, bounds; no parameter dump | S |
| `inspect_character` | `EntityDesc` (clips map, gait, tags, capabilities [PD]); `explainCharacter` [PD] | None for the agent | n/a | A character capability card: activities → clips, speeds, jump reach, available actions, current intent | S–M |
| `inspect_timeline` | `sequence.get_state`, `sequencer.get_state`, `SectionTimeline` | Tools | Yes | Add the camera track, events, actors and sections by name; time-resolution helpers ("1:30", "chorus 2") | S |
| `create_shot` | `Sequence` edit API (`sequence.hpp:480-560`); `sequence.add_shot` | Tool (partial) | Partially: no camera, no overlap check surfaced | Shot plus camera plus subject in one call; overlap policy | M |
| `modify_shot` | `moveShot`, `trimShotStart/End`, `splitShot`, `removeShot` | C++ only | No tool | Wrap it; addressing by shot name or id | S |
| `place_character` | `scene.set_node_transform`; entity anchors | Tool (node level) | Unsafe: moving the node of a live entity fights its simulation | "Stage mark" semantics: set the entity's anchor, or an actor key at t | M |
| `assign_animation` | `ClipCue` on `seq::Actor`; `EntityDesc::clips`; `PlayClip` event | C++ / scene JSON | No tool | Must go through **activity names**, never clip names (ADR-096, Phase D §35); validate against the capability card | M |
| `create_character_action` | `ActionDesc` sequences; `EntityWorld::direct`; `Staging`; `SequenceEvent{EntityAction}` (one action per event) | C++ / scene JSON | No tool | A **performance** type (§5.6) compiled either to baked `seq::Actor` content or to Director-tier action lists | L–XL |
| `create_camera_rig` | `CameraDirection::addCamera` | UI only (`control_panel.cpp:4034`) | No tool; not undoable | Wrap it, plus the undo record | S–M |
| `set_camera_behavior` | `CameraBehavior{Chase,Orbit,Pov}`, `CameraPreset` (10), rig follow/aim | C++ | No tool | Named semantic moves (§6.3) compiled to `ShotCamera` keys or behaviour | L |
| `trigger_effect` | `SequenceEvent{SetParameter}` (baked); world-effect `Activation::Window` | C++ | No tool | Effect catalogue (names → parameter recipes); "activate X at t for d" | M |
| `set_effect_trigger` | Trigger kinds exist; `ActionComplete` and `VolumeEnter` are **live** with no poster [main] | C++ | n/a | Semantic event names resolved at plan time (§8) | L |
| `create_transition` | `shot.in/out` (`TransitionKind`), `CameraShot.transition` | C++ | No tool | Wrap it | S |
| `move_timeline_clip` | `moveShot`, `splitActorClip`; no general clip-move | C++ | No tool | Wrap it | S–M |
| `set_parameter` | `parameter.set` | Tool | Yes | Keep as an escape hatch, with a flag to the user | – |
| `preview_shot` | Playhead seek plus loop; `--capture` headless | Partly | n/a | A tool that seeks, plays a span and returns frames | M |
| `render_preview` | `--render`, `startRenderFromUi` (which **saves the project**) | CLI/UI | Unsafe (writes the project) | A render from an in-memory session snapshot to a temp file | M |

### 4.2 The central question

Can a Director say the following without entity IDs, clip indices, bone indices, renderer internals or shader parameters?

- `Rook.run_to(Umbra)`
- `Rook.backflip()`
- `Camera.low_angle_chase(Rook)`
- `Camera.rise_over(Rook)`
- `Effect.trigger("umbra_hero", trigger="Rook.backflip_peak")`
- `Timeline.slow_motion(0.35)`

**No. But the identifiers are already human-readable, so the missing layer is semantic, not referential.** AV Gen addresses almost everything by string name: entity `rook`, hero `umbra-cap`, parameter paths, camera slugs, shot names. There are no numeric IDs to hide, apart from `CameraId`, which has names and slugs beside it. What is missing:

| Expression | Why it fails today | Missing piece |
|---|---|---|
| `Rook.run_to(Umbra)` | `ActionDesc{Move, target=EntityRef/Point, speed}` exists, but only through C++ or scene JSON. `Move` stops at the goal. "Umbra" is a hero, not an entity target. | A **subject resolver** (character, hero or node → target), a Move "pass-through" option, and an agent-reachable performance command |
| `Rook.backflip()` | No clip, no activity, and `Activity` is a closed enum of 10 | **Asset**, plus an open activity vocabulary per character, plus one-shot playback |
| `Camera.low_angle_chase(Rook)` | `CameraBehavior::Chase` targets only a `seq::Actor`; rig `followNode` works on entities but `followOffset` is not keyframeable | **Semantic camera moves** compiled to keys, and behaviours that can target entities |
| `Camera.rise_over(Rook)` | Chase offset is constant per shot | Keyed chase offsets (`offsetStart/End`, or keys) |
| `Effect.trigger("umbra_hero", trigger="Rook.backflip_peak")` | There is no `umbra_hero` effect; world effects activate only Always/Window/CameraTravel/HeroFocus; no animation events | An **effect catalogue** with semantic names, plus **plan-time event times** |
| `Timeline.slow_motion(0.35)` | No time remap exists anywhere; `Transport::setRate` pauses audio | A **time-warp** architecture decision (§7.3) |

The precise missing layer is a **Director Plan model plus a compiler**:

- a typed, serialisable description of shots, subjects, performances, camera moves, events and effect cues, in names and seconds or beats;
- compiled deterministically into `seq::Sequence`, `CameraDirection`, `SequenceEvent`s and parameter keys;
- with a **capability registry** that says what each name can do.

### 4.3 Low-level vs semantic tools: where the boundary belongs (§13)

The engine already draws the line in the right place for runtime. Phase F §32 lists what language must not control. For tools:

- **Semantic tools (the default surface):** subjects, performances, camera moves, cues, shots, all by name and in musical or clock time. They are validated against capabilities, compiled, and produce native content.
- **Low-level tools (kept, but demoted):** `parameter.set`, `sequencer.add_keyframe`, `modulation.*`. These are necessary because AV Gen's look is parameter-driven, and there will always be requests no semantic verb covers. Each use should be flagged in the proposal ("the Director used a raw parameter") so opaque changes are visible.
- **Never exposed:** bone indices, clip indices, shader uniforms, renderer passes, joint IK targets, `MotionRequest` fields.

The boundary rule: **a tool is semantic when an animator could say it out loud and the validator can check it without running the simulation.**

---

## 5. Character and animation readiness

The worked example: *Rook runs toward the Umbra mushroom, jumps, backflips over it, and keeps running.*

### 5.1 Clips the aliens actually have (verified by parsing `assets/aliens/alien-scout.glb` read-only)

| Clip | Length (s) | Clip | Length (s) |
|---|---|---|---|
| Button_push | 1.50 | Idle | 2.00 |
| Crazy | 2.67 | Idle_turn | 4.03 |
| Dying_1_backpack | 3.33 | **Jump_running** | 1.03 |
| Dying_1_no_backpack | 3.33 | **Jumping** | 1.93 |
| Dying_forward | 4.17 | **Landing** | 1.10 |
| **Fall_loop** | 0.73 | **Running** | 0.73 |
| Fight_head_hit | 1.47 | Take_from_floor | 2.33 |
| Fight_idle | 2.23 | Take_from_table | 1.67 |
| Fight_Jab | 0.73 | Walking | 1.07 |
| Fight_leg_kick_1 | 1.63 | Walking_crouch | 1.07 |
| Fight_leg_kick_2 | 1.63 | Walking_injured | 1.67 |
| Fight_punch | 1.80 | Walking_low_grav | 4.67 |
| Floating | 6.50 | Flying_jet | 2.90 |

- 26 clips, 57.9 s. All six alien files embed the same set on one 89-joint skeleton (CC0; `assets/aliens/ATTRIBUTION.md`).
- **There is no backflip, flip, roll, somersault, vault, start/stop or turn-while-running clip.**
- All locomotion clips are **in place**: net root XZ translation is zero for Running, Walking, Jumping and Jump_running (measured by the [AR] analysis, and stated in `entity/airborne.hpp:15-18`).
- `Jumping` carries 0.87 m of vertical root range.
- No glTF animation carries markers or `extras`.
- [AR] also holds the 100STYLE corpus (CC BY 4.0, gitignored; `assets/100STYLE-ATTRIBUTION.md`). It is stylised locomotion only, with no acrobatics, and ADR-553 found that it retargets poorly onto the alien.

**Implication:** a Director asked for a backflip must say "no such motion" unless a clip is acquired. The capability registry (§10) exists to make that refusal automatic. Phase F §20–24 (offline generative motion that must pass validation and become ordinary clips) is the spec's route to rare motions like this one.

### 5.2 Feature-by-feature

| Feature | Status | Evidence |
|---|---|---|
| Locomotion (walk/run by speed) | **Exists** | `Gait`, `Activity::Walk/Run`, `playbackRate`, stride-warp layers |
| Clips and blending | **Exists (simple)** | `AnimationPlayer`: named states, cross-fade `blendIn`; `inertializeHalflife` (ADR-547); [AR] 3-slot inertialisation (ADR-613) |
| Transitions / state machine | **Partial** | `AnimationTransition{from,to,blend}`, with no conditions or graph |
| One-shot clips | **Missing** | `SkinnedRig::addDefaultStates` makes every clip loop (`animation.cpp:485-493`); a Pose is cut off by duration |
| Root motion | **Exists, opt-in, mostly unused** | `scene/root_motion.hpp` (ADR-337); the pack is in place, so travel is integrated in code |
| Procedural motion | **Exists** | Pose layers: Aim, Additive, Foot, Stride, Secondary; [AR] adds Lean and Reach |
| IK | **Two-bone plus body compensation** | `solveTwoBone`, `solveBodyCompensation`, multi-parent write-back. No arbitrary-chain solver (FABRIK/CCD) |
| Trajectory control | **Partial** | [AR] `trajectory.hpp` and `trajectory_prediction.hpp` exist and are tested but uncalled (ADR-615 "staged and dark"); [AR] `locomotion_plan.hpp` is wired |
| Jump arcs / airborne | **Exists, autonomous only** | `Airborne::launch(from,to)` is a deterministic ballistic arc, Rising → Falling → Landing, and the gait resumes a run afterwards (`locomotion.hpp:37-40`). **The only callers are in `explore`** (`behaviors.cpp:1571,1597`): a gap probe or a signal edge. Rook's `jumpRange` is 0 in the song project. |
| Contacts / foot placement | **Exists** | `detectContacts`, `PhaseTrack`; Foot layer on `GroundFollower`; [AR] packs persist contacts |
| Timing / sync with the timeline | **Exists** | Everything is keyed to the timeline second; `ClipCue` driven from its own absolute time; `EntityWorld::seek` replays at 1/60 s up to 90 s |
| Retargeting | **Offline, exists** | `scene/retarget.hpp` (ADR-548); [AR] ADR-650 retargets once per skeleton digest |
| Animation event markers / callbacks | **Missing** | No marker, notify or peak type anywhere; only `ActionEvent` (action completion) and analysis contact tracks |
| Scripted sequences | **Exists** | `ActionDesc` lists with labels, `when`/`otherwise`, `onComplete`; `Schedule`; `Staging` steps; `seq::Actor` keys, path and clip cues |
| Interruption and continuation | **Exists** | Three authority tiers; lower tiers **resume** (`resumable`), ADR-091 |
| Semantic action names | **Partial** | Eight primitive `ActionKind`s; `Activity` names; verbs as `InteractionDesc` on props; [PD] `IntentType` (13) and capabilities/affordances. There is no `run_to` or `jump_to` primitive |
| Motion matching | [AR] opt-in; [main] unwired | Rejects `Airborne` modes and samples (`match_motion_provider.cpp:151,188`), so it cannot help with the stunt |

### 5.3 Can `run → jump → backflip → land → continue running` be a high-level action sequence?

**Not today, on any branch.** The closest executable approximation:

- **Director tier:** `Move(to=near Umbra, speed=run)` → `Pose(activity="jump", duration≈1.9)` → `Move(to=beyond)`. This runs, **decelerates to a stop**, plays a looping `Jumping` **stationary** (Pose forces speed 0, `action.cpp:625-653`), then starts again from a standstill.
- **Autonomous:** `explore` with `jumpRange > 0` performs a genuine ballistic run → jump → fall → land → run, but only when it chooses to.

The six code gaps (all verified):
1. No airborne `ActionKind` (or Move option) that calls `Airborne::launch` toward a landing point. `Airborne` is private to `explore`.
2. `Move` always decelerates to zero. There is no pass-through or blend-into-next mode.
3. Clip states always loop. The pack's loop flag, which [AR] now persists, is not honoured by the player.
4. The provider chain has only Ground entries (Idle/Walk/Run). Jump/Fall/Land play only through the legacy clip path.
5. No clip events: take-off frame, peak, touchdown.
6. A timeline `EntityAction` event carries **one** action (`Engine::applySectionActions`, `engine.cpp:4023`).

The asset gap:
- There is no backflip.
- The jump clip lengths (1.93 s / 1.03 s) do not match the code arc: roughly 0.7 s airtime at apex 1.1 m and g = 18 (`JumpSettings`).

### 5.4 The better route for authored stunts: the baked tier

`seq::Actor` (`sequence.hpp:391`) drives a node from keys or a spline path plus `ClipCue`s, and `positionAt(t)` is **pure**. A Director that computes the whole performance at plan time gets several things:
- the run path to the take-off point, a ballistic arc (the same arithmetic `Airborne` uses, emitted as keys), the landing and the continuation;
- clip cues at exact seconds (`Running` → `Jump_running` → [backflip] → `Landing` → `Running`);
- **known times for every semantic event** (take-off, peak, landing);
- scrub-exact, render-reproducible motion;
- a result the user can edit as ordinary actor keys.

It gives up foot-plant adaptation, terrain awareness and the autonomous stack, and it has to hand Rook's node over from his entity. Timeline position and entity motion **add** on the same node (`Entity::fieldPosition`, `entity.cpp:1817-1830`), so the entity must be held (Director-tier Wait, or disabled) for the shot span.

**Unknown:** whether that hand-off is clean. It needs a probe. Ground clearance of computed keys can use `BakeOptions::groundHeightAt`.

### 5.5 Autonomous direction (Phase F style) for everything else

[PD] provides the "director states goals" half:
- The `goal` considerer (`decision.hpp:700`, `decision.cpp:1617-1720`) has a subject, affordance, intent, a from/until window, approach, dwell, activity, and a keyframeable weight.
- The character plans the how (Move → Face → Interact/Pose).
- It is a **bias, not a guarantee**, and one errand deep.
- `docs/reports/autonomous-character-phase-d-report.md:125` already anticipates "an optional LLM goal provider writes `goal` considerers' subjects."

That fits "Rook, investigate the mushroom during the chorus". It does not fit "backflip at 1:32.4".

### 5.6 The performance abstraction

A Director needs one type with two compilation targets:

```
Performance {
  subject: "rook"
  mode: scripted | directed | goal     // baked actor | Director-tier actions | [PD] goal considerer
  beats: [ run_to(umbra-cap, stop=false), jump(over=umbra-cap, apex=…), move("backflip"),
           land(), run(direction=forward, seconds=1.2) ]
  emits: [ jump_started, peak, landed ]  // computed times when scripted; live events otherwise
}
```

- **scripted** compiles to `seq::Actor` keys and cues (baked tier). Event times are known.
- **directed** compiles to `ActionDesc` lists at `Authority::Director` via a scheduled `SequenceEvent`. Event times are live.
- **goal** compiles to [PD] `goal` considerer entries. Event times are live, and only approximate.

---

## 6. Camera readiness

### 6.1 Inventory

| Feature | Status | Where |
|---|---|---|
| Chase | Baked, **seq actors only** | `CameraBehaviorKind::Chase`: performer-local `offset`, `lagSeconds`, `clearance` |
| Follow (live node) | Evaluated, any node | `CameraRig::followNode/followOffset/followLocal/followLagSeconds/followClearance` |
| Look-at / aim | Both architectures | `ShotCamera::lookAtActor`; `CameraRig::aimNode/aimOffset` |
| Tracking / reveal / wide / close / top-down | Baked presets | `CameraPreset` (10), stamping `app::Shot` (`ShotKind` 14, `MovementCurve {Straight, Arc, Rise, Dip}`) |
| Orbit | Baked behaviour; main camera only (integrating) | `CameraBehaviorKind::Orbit` |
| Crane / dolly | **Missing** as types | Rise curves and keys approximate them |
| Splines | Exists | `CameraPlacement::Spline` with keyframeable `splineT`, `lookAhead`, `splineOffset` |
| Keyframed cameras | Exists | `CameraKind::Keys`; rig channels `cameras/<slug>/{position,target,fov,focalLength,splineT,lookAhead,splineOffset}` (`composition.cpp:6636-6644`) |
| Cuts / multi-camera | Exists | `CameraDirection` shots; `resolveActiveCamera` (locked > event > shot > default); `ShotTransition {Cut, Blend}` |
| Focal length / DOF | Exists | Rig `focalLength`; `CompositionProfile` aperture and `focusOnSubject`; `camera/focus/*` |
| Shake | Exists, as an event | `EventActionKind::CameraShake` baked onto `camera/shake/*` |
| Constraints | Partial | Ground clearance only; no constraint stack |
| Event-aware camera | Exists (narrow) | Rig `eventScenario`/`eventBeats` claims the frame while a Staging scenario runs; ADR-217 aim hold |

### 6.2 Can "low-angle chase → rise over Rook → follow the flip → continue forward past him" be represented?

**Not as a behaviour; yes as keys.**
- `Chase`'s offset is constant for the shot.
- Rig `followOffset` is **not a parameter** (only the channels listed above are), so it cannot be keyframed.
- A world-fixed spline cannot follow a moving subject.

**If Rook is a `seq::Actor` (§5.4), the Director can compute the whole move at plan time.** For each sample *t*, the eye is `positionAt(t) + R(headingAt(t)) · offset(t)`, where `offset(t)` goes from (0, 0.4, −3) to (0, 4, 0) at the peak to (0, 1.5, +5) past him. The aim is `positionAt(t) + aimOffset`. That is emitted as `CameraKind::Keys` (`CameraKey{position,target,focalLength}`). It is exactly how `cameraPoseFor` already bakes a Chase; the only new ingredient is a time-varying offset.

A small engine extension would make this a native, editable behaviour instead of opaque keys: keyed or start/end offsets on `CameraBehavior`. That is strongly preferable, because 48 raw camera keys are "opaque generated content" (§19).

If Rook stays autonomous (live tier), the move is not bakeable. It would need a rig with `followNode=rook` and keyframeable `followOffset`, which does not exist.

### 6.3 Semantic camera vocabulary to add

The Director should be able to use named moves:
- `chase(subject, height, distance)`
- `rise_over(subject, apex_time)`
- `pass(subject, side)`
- `orbit(subject, degrees)`
- `hold(subject)`
- `reveal(hero)`

Each should compile to `ShotCamera` behaviour plus parameters, or to keys, and **each should be editable afterwards as that behaviour**. The existing `ShotKind`/`CameraPreset` names are most of this vocabulary. `docs/camera-presets.md` warns that "a preset is a stamp, not state", so the semantic move itself must be stored, or the Director's intent is lost on first edit.

### 6.4 "Director observes, does not dictate"

This is spec text (Phase F §78–79, §89, §102) with no code. It concerns the *runtime* auto-director proposing candidate shots from character events. It does **not** forbid an authoring-time Director from writing authored shots at the user's request; those are `Origin::Authored` content the user approved. The right reading for this project:
- the LLM Director **proposes**; the user accepts;
- accepted shots are authored content;
- runtime systems never let the LLM override authored cinematography.

---

## 7. Timeline readiness

### 7.1 Can Director output be ordinary, editable native content?

**Yes. This is the strongest part of the foundation.** Every target structure is serialised and editable in the Sequence panel:

| Director output | Native structure | Editable in UI | Agent tool |
|---|---|---|---|
| Shot | `seq::Shot` (non-overlapping, validated by `Sequence::validate`) | Yes | add only |
| Camera cut | `scene::CameraShot` (`Origin::Authored`) | Yes (camera track) | **none** |
| Character performance (scripted) | `seq::Actor` keys, path, `ClipCue`s | Yes (Actors lane) | **none** |
| Character performance (directed) | `SequenceEvent{EntityAction}` | Yes | **none** |
| Parameter automation | `params::Timeline` keys / `SequenceEvent{SetParameter}` | Yes | `sequencer.add_keyframe` |
| Marker | `seq::Marker` | Yes | `sequence.add_marker` |
| Transition | `shot.in/out`, `CameraShot.transition` | Yes | **none** |
| Overlay | `seq::OverlayCue` | Yes | `sequence.add_overlay` |

The bake/install model (ADR-089) is a gift here. The Director never has to generate a frame-accurate evaluator; it writes a `Sequence`, and `bake()` does the rest.

### 7.2 Gaps

- Tools are missing for shot move/trim/remove, shot camera, transitions, actors, events and the camera track. The C++ API for most of these exists (`sequence.hpp:480-560`).
- No time-string resolver ("1:30", "bar 45", "second chorus").
- **Transactionality:** `TimelineChange` in `EditCommand` snapshots the whole `seq::Sequence` before and after, which gives a cheap and correct undo for the sequence. The camera track (`CameraDirection`) and piece-level keyframes are **not** in `EditCommand` (§14).
- `seq::Shot` and `scene::CameraShot` are independent. The Director must write both coherently, and nothing validates their correspondence.

### 7.3 Slow motion

**Missing, and not a small addition.**
- Everything is keyed to the transport second.
- Audio is the master clock for a music video, and `Transport::setRate` away from 1x **pauses audio** (`transport.cpp:241,329`).
- ADR-410 names "time freeze / temporal rewind `render(w(t))`" only as a theoretical category.

A Director-grade `slow_motion(0.35)` needs an architecture decision:

1. **Scene-time warp:** a piecewise-linear `w(t)` mapping transport time to scene time for animation, entities, particles and camera, with audio unaffected. This is the "music keeps playing, the world slows" music-video look. It touches every consumer of the timeline second and the seek/replay machinery (ADR-671).
2. **Performance-local retime:** slow only the Director's scripted actor and camera keys over the span, by stretching their keys and scaling `ClipCue::speed`. This is cheap, fully baked and deterministic, but the rest of the world (wind, particles, other characters) keeps normal speed.

Option 2 is achievable inside the plan compiler. Option 1 is an engine feature, XL. Pairing option 2 with `temporal/echo/*` (FrameEcho) gives the "frame drag" look in the brief.

---

## 8. Effects and event readiness

### 8.1 What exists

- **`seq::SequenceEvent`** (`src/seq/events.hpp`, ADR-098):
  - `TriggerKind`: Time, Beat, Bar, Section, ShotStart, ShotEnd, Cue, ClipEnd, ActionComplete, InteractionComplete, VolumeEnter, VolumeExit.
  - `EventActionKind`: SetParameter, CameraShake, PlayClip, Overlay, SceneTransition, EntityAction, Notify.
  - The pure predicates `triggerIsScheduled()` and `actionIsBaked()` tell an editor (or a validator) *before running* whether an event is scrub-safe. **This is exactly the validation primitive a Director needs.**
- **Signal bus plus ModRoutes:** continuous signal → parameter; trigger volumes publish `field.<name>.enter/exit/occupancy` (ADR-097).
- **`entity::ActionEvent`:** action completion with `onComplete` names; consumed by `Staging`.
- [PD] **`WorldEvent`s:**
  - `EntityWorld::emitEvent`; sources are named `ActionEvent`s, bus signals (`EventProfile`), and Staging beats raised as `"<scenario>/<beat>"`.
  - Characters perceive them (the attention/memory mind layer).
  - [PD] ADR-671 makes a scrub replay director beats. Bus-driven and modulation state are **not** replayed.

### 8.2 What is broken or missing

- **Live-tier events have no producer on [main].** `EventDispatcher::post(TriggerSignal)` has no caller outside `seq/events.*` and tests (verified).
- `Engine::applySectionActions` applies only `EntityAction`. A fired live `SetParameter` or `CameraShake` would be dispatched and ignored.
- **No animation events.** `Rook.backflip_peak` has no source.
- **No event-triggered effect activation.** `effects.hpp:93` offers Always/Window/CameraTravel/HeroFocus only.
- **No generic "on event X do Y"** subscription.

### 8.3 Can animation events become first-class events?

Yes, in two ways that match the engine's tiers:

1. **Plan-time (baked):** for a scripted performance, the compiler knows when take-off, peak and landing happen, so it emits `Cue` markers named `rook.backflip_peak` and events triggered by those cues. All downstream effects become baked keys: effect intensity, FrameEcho, camera shake, lighting. **This needs no runtime event system at all.** It is reproducible and editable: the user can drag the marker.
2. **Runtime (live):** for autonomous characters, contact tracks, `Airborne` phase changes and `ActionEvent`s would post `TriggerSignal`s and [PD] `WorldEvent`s. Phase F §79 wants exactly these semantic events (CharacterEntered, InteractionStarted…), and they are live tier. They can only drive live consumers, and **must not be the trigger for anything that has to render reproducibly** unless the replay (ADR-671) covers it.

---

## 9. Project introspection

| Need | Status |
|---|---|
| Scene digest by named subject | Partial: `scene.get_summary`, `find_nodes`, heroes (no tool lists heroes as subjects) |
| Characters and what they can do | Missing: `entity.list` returns name, node, profile, behaviour kinds and counts only |
| Where things are (positions, bounds, distances) | Partial: `scene.get_node`, `render.probe`; nothing spatial like "distance Rook→Umbra" or "is the path clear" |
| Timeline by name and section | Partial: `sequence.get_state` (beats window) |
| What the camera sees | Numbers only (`render.probe`); no image |
| "Why is this character doing that" | [PD] `explainCharacter`, `avgen_behavior_trace --explain` (not a tool) |
| Context size | Handled by design (ambient O(1), pull O(n)); project plus scene files are about 0.7 MB for Glowmere multicam (476 KB + 237 KB), roughly 180k tokens raw. Never send them. |

---

## 10. Capability registry (§9)

**What exists that can be generated rather than hand-written:**
- `ToolDefinition` plus annotations: `capability.list_tools` is already accurate.
- `ParamDesc`: path, label, group, kind, hard/soft range, and flags (exposed, modulatable, serialized). `parameter.search` emits them.
- `EffectSchema`/`EffectField` (ADR-500): key, display name, fields with leaf paths and ranges, styles, default audio routes. There is no dump tool.
- `EntityDesc::clips` (activity → clip) and `gait` speeds; `JumpSettings`.
- [PD] `EntityDesc::capabilities` and `InteractionDesc::requires` (affordances, checked by planner and executor).
- `ActionKind`, `Activity`, `IntentType`, `TriggerKind`, `EventActionKind`, `CameraPreset`, `CameraBehaviorKind`, `ShotKind`, `TransitionKind`: all enums with `*Name`/`*FromName`.
- `triggerIsScheduled`/`actionIsBaked`, for determinism classes.
- [AR] `avgen_motion` / `motion_database_inspect`: per-clip tags, contacts, loop flags, ground speed.
- **No `inspect-db` tool exists** on any branch.

**What is missing:**
- A per-character **capability card** listing activities, performable moves (with clip availability and duration), locomotion speeds, jump reach and interactions.
- An **effect catalogue** with semantic names ("Umbra glow", "hero pulse") that bind a hero or effect to a parameter recipe.
- A **camera move catalogue** with parameters and constraints.
- An **event catalogue**: which semantic events each performance can emit, and in which tier.
- Metadata that does not exist anywhere yet: clip semantics ("this clip is airborne, rotational, 1.2 s, with a peak at 0.55"), traversal constraints, clip-to-arc timing.

`capability.list` (`src/ai/capabilities.cpp`) should become generated from these registries. ADR-617, "documentation that cannot rot", is the precedent: a hand-written capability list has already gone stale once.

---

## 11. Laya and fast decision model research (§10)

### 11.1 What Laya actually is (primary sources checked 2026-09-21)

**The key correction first:** Laya comes from **Convai Innovations Pvt. Ltd.** (Kerala, India; founder Nandakishor M). That is **not** Convai Technologies (convai.com, San Jose), the NPC platform with the "Prompt-to-Action" system.
- Laya's site, model card and README mention no games, NPCs, animation, Unity or Unreal.
- Its stated use is enterprise classification: routing, moderation, guardrails, "the classification layer around an LLM stack".

| Property | Documented | Source |
|---|---|---|
| Type | Non-autoregressive classifier: state (text/JSON) plus typed questions in, typed answers with calibrated probabilities out, in one forward pass | https://laya.convaiinnovations.com/ |
| Outputs | `choice`, `score` (ordinal), `noul` (calibrated boolean) | site; https://huggingface.co/convaiinnovations/laya |
| Size | 421M (ModernBERT-large, 512 tokens), 322M multilingual (mmBERT-base), 421M "typed-decisions" (1024 tokens) | HF model card |
| Latency | 32.8–39.5 ms per question; 7.2 ms per question batched ×10, **on an NVIDIA T4** (vendor-measured) | HF card; https://github.com/NandhaKishorM/laya |
| Accuracy caveats | The typed-decisions base scores 0.362 without fine-tuning ("a fast base to fine-tune, not a drop-in decision engine"); ships over-confident; **degrades above about 20 options** (Banking77: 0.425) | HF card |
| Licence | Apache 2.0 open weights | HF card, GitHub |
| Runtime / format | Python only (`pip install laya`, torch plus transformers), safetensors; **no ONNX, CoreML, GGUF or C++ API** | GitHub |
| Fine-tuning | Documented (Kaggle notebook, about 4 h on 2× T4) | GitHub |
| Apple Silicon / Metal | **Not documented** | none |
| CPU latency | **Not documented** (only a 7.4 s reload figure) | none |
| Paper | **None.** The author's arXiv papers (2503.23303, 2510.01237) do not describe Laya | arXiv |
| Maturity | Released 2026-09-18, three days before this report | https://ai-tldr.dev/releases/convai-laya/ |

**Assessment:** the documentation is thin, benchmarks are vendor-only against an obscure competitor, and there is no game evidence.

**Inference, not measurement:**
- A 400M encoder at 512 tokens would plausibly run at 10–40 ms on an M-series GPU or ANE after an unverified ONNX → CoreML export.
- On the GPU it would contend with the renderer; the Neural Engine via CoreML would avoid that.
- Integrating it into a native C++ engine means either an export pipeline or an IPC sidecar process.

### 11.2 What AV Gen already has

- **[PD] Phase D decider:** utility selection at 2 Hz per character (`SelectorSettings`, `decision.hpp:67`), phase-offset per seed. It is measured at about 0.011 ms per decider per frame; 201 deciders cost 2.25 ms of entity step. It is deterministic, explainable (`explainCharacter`), needs no model, and is replayed exactly by a scrub (ADR-671).
- Laya's best case is 7 ms per batched question on a datacentre GPU. That is **three orders of magnitude slower**, nondeterministic under hardware changes unless carefully pinned, and it would break the determinism story unless its outputs are recorded.

### 11.3 Related options

| Option | Fit for AV Gen |
|---|---|
| Utility AI (Dave Mark, IAUS; GDC 2013 https://www.gdcvault.com/play/1018040/ , GDC 2015 https://www.gdcvault.com/play/1021848/) | **Already the architecture** ([PD] considerers and ScoreFactors) |
| Behaviour trees (https://arxiv.org/abs/1709.00084; BehaviorTree.CPP) | Staging plus action lists already cover the sequencing half |
| HTN (Game AI Pro ch. 12, https://www.gameaipro.com/GameAIPro/GameAIPro_Chapter12_Exploring_HTN_Planners_through_Example.pdf) | **The best conceptual fit for the Director's compiler**: decompose "backflip over Umbra" into run_to → take-off → arc → land, with preconditions (clip exists, landing navigable) |
| GOAP (Orkin, https://alumni.media.mit.edu/~jorkin/gdc2006_orkin_jeff_fear.pdf) | Overkill; [PD] explicitly limits to one-errand plans |
| Small local LLMs (llama.cpp Metal https://github.com/ggml-org/llama.cpp, MLX https://github.com/ml-explore/mlx, GBNF grammars) | Workable as an **offline/private Director provider** (the `local` provider already exists). A 7B Q4 model generates about 36–50 tok/s on M1 Pro/M4 Pro (https://github.com/ggml-org/llama.cpp/discussions/4167). Too slow for per-character decisions |
| NVIDIA ACE (https://developer.nvidia.com/blog/deploy-the-first-on-device-small-language-model-for-improved-game-character-roleplay) | CUDA/RTX; not applicable on macOS (Phase E §3: "M2 Max is a hard constraint") |
| Convai (NPC) Prompt-to-Action (https://convai.com/blog/integrating-dynamic-npc-actions-for-game-development-with-convai), Inworld (https://docs.inworld.ai/) | Cloud-first NPC stacks; wrong layer for an offline-deterministic renderer |

### 11.4 Where a fast model could ever help

Only as a `LearnedBehaviorProvider` that replaces *option scoring* inside the utility decider (Phase D §53, §71):
- run at 1–5 Hz;
- batched across characters, one forward pass per tick;
- its outputs **recorded** into the simulation so a scrub or render replays them rather than re-inferring.

At 20 options or fewer, Laya's `choice` head matches the decider's option-count shape. Nothing in this investigation shows a quality problem with the current decider that such a model would fix.

---

## 12. Three-layer AI architecture (§11)

Proposed: LLM Director → semantic commands/world plan → fast decision model → runtime.

| Layer | Belongs here | For this codebase |
|---|---|---|
| **LLM Director** (seconds, async, authoring time) | Interpreting intent, choosing subjects, sequencing beats, choosing camera language, explaining and revising | **Needed.** The control plane exists. |
| **Semantic plan plus compiler** (ms, deterministic) | Capability checks, spatial resolution, timing, arc maths, camera key generation, emitting native content, validation | **Needed. This is the missing layer and the core of the work.** HTN-style decomposition fits. |
| **Fast decision model** (5–30 Hz) | Per-character option selection under perception | **Not needed now.** The [PD] utility decider already fills this layer at negligible cost, deterministically and explainably. |
| **Runtime** (every frame) | Motion, IK, navigation, rendering | Exists. |

**The separation is beneficial, but the third layer is already occupied by something better suited than a neural model.** The real architecture for AV Gen is:

```
LLM Director ──plan──> Director Plan ──compile──> native content (sequence, cameras, events, keys)
                                         │
                                         └──goals──> [PD] goal considerers ──> utility decider ──> runtime
```

A learned scorer can later sit *inside* the decider box behind the existing seam. It should not become a new tier.

---

## 13. Director panel concept (§12, description only)

```
┌─ Director ─────────────────────────────────────────────────────────────────┐
│ Conversation                          │ Context                             │
│  > At 1:30, 5 s shot, Rook runs to    │  Scene: glowmere-valley-2-multicam  │
│    Umbra, backflips over it …         │  Subjects: rook tide sage … umbra   │
│  ◂ Plan (1 issue):                    │  Playhead 1:28.4  ♩ 118  Chorus 2  │
│    ✓ shot "rook-umbra" 1:30–1:35      │  Capabilities used: run, jump,     │
│    ✓ camera: chase → rise → pass      │   land, chase, rise_over           │
│    ✗ "backflip": rook has no such     │  Missing: backflip (no clip)       │
│      move. Substitute jump_running?   │                                     │
├───────────────────────────────────────┴─────────────────────────────────────┤
│ Proposed changes (diff)                                                      │
│  + seq shot  rook-umbra   90.0–95.0   camera: behaviour chase(keys)          │
│  + actor     rook         path 90.0–95.0, cues Running@90 Jump_running@92.1 │
│  + marker    rook.peak    92.55                                              │
│  + event     temporal/echo/amount  ramp 92.2–92.9                            │
│  + event     world-effect "Hero Pulse" window 92.55 +1.5 s                   │
│  ! raw parameter used: temporal/echo/frames = 12                             │
├──────────────────────────────────────────────────────────────────────────────┤
│ Activity: inspect_scene ✓ resolve_subject(umbra) ✓ validate ✓ compile ✓      │
│ Preview: [▶ play span] [thumbnail strip 90–95 s]                             │
│ [Accept]  [Modify…]  [Regenerate]  [Reject]          Undo: "Director: …" ⌘Z │
└──────────────────────────────────────────────────────────────────────────────┘
```

**Can the current command and undo architecture make everything the Director creates undoable? Not yet, but without a redesign.**

The real mechanism is `ui::EditHistory` of `EditCommand` records (`src/ui/edit_history.hpp:148`). Each record holds:
- parameter before/after;
- nodes kept whole on removal;
- parent and hero changes;
- a whole-`seq::Sequence` before/after (`TimelineChange`);
- the whole light list (`LightChange`).

It covers what a Director would write in the **sequence** already. It is missing:
- `CameraDirection` (rigs and camera track);
- world and atmospheric effect lists;
- piece-level timeline keys and tracks;
- staging and entity structure;
- the 2D layer stack.

The AI sink (`EditHistoryTransactionSink`) records **parameter diffs only**, and "Undo this task" bypasses the history via snapshot restore. Two parallel undo mechanisms is a defect a Director would amplify.

The fix follows the precedent the file already sets:
- add whole-list before/after fields for `CameraDirection`, world effects and the timeline;
- make the AI sink diff those domains at `begin`/`commit`, as it already does for parameters;
- delete the snapshot-restore undo path in favour of the history.

This is about 2–4 weeks, and it also fixes undo for human edits that bypass the history today (camera add/remove/place, world-effect edits, keyframe drags).

---

## 14. Validation and safety (§14)

**How much of `request → plan → validate → preview → approve → commit` exists?**

| Stage | Status | Evidence |
|---|---|---|
| Request | Exists | Panel, CLI |
| Plan | **Partial** | The orchestrator publishes the model's first text as a "plan", but it is prose, not a typed artefact |
| Validate (argument) | Exists | JSON Schema per tool |
| Validate (semantic) | **Missing** | No reference validator (unknown subject/clip/effect), no capability check, no shot-overlap check surfaced to tools (`Sequence::validate` exists in C++), no seq-shot/camera-shot coherence, no spatial feasibility |
| Dry run | **Missing** | Tools mutate immediately |
| Preview | **Partial** | The user can play the timeline; there is no programmatic span preview or thumbnail |
| Approve | **Missing** | No `WaitingForApproval` state; mutations apply during the task |
| Commit | Exists | `Transaction` commit/abort |
| Undo | **Partial** | §13 |
| Error reporting | Exists | Closed `ToolErrorCode` plus `recovery` |

**Failure modes, and where each is caught today:**

| Failure mode | Caught today? |
|---|---|
| Invalid reference | Low-level tools return `NotFound`. Scene loads only warn on unknown keys (`json_keys::warnUnknownKeys`, ADR-278). Missing assets go to `projectWarnings_` without failing. |
| Impossible animation | Not detected. `Pose` with an unmapped activity plays whatever the fallback is. |
| Nonexistent capability | Not detected (no registry). |
| Timeline conflicts and overlapping shots | `Sequence::validate` refuses overlaps in C++. `sequence.add_shot` should surface it; **Unknown:** not verified per tool. |
| Missing rigs or effects | `NotFound` at the parameter level only. |

**Persistence risk: this is the most important safety finding.** This repo has a documented history of saves silently dropping authored state:
- **ADR-618 [AR/PD]:** `PoseLayer::weight` was parsed but never written, so "one editor save turns off stride warping and breathing on all five aliens".
- Project saves that dropped `worldEffects`, `atmosphericEffects`, heroes, lights and scene nodes until each was given a hand-added override key (`Engine::projectDocument`, `engine.cpp:1023-1121`; lights: "fifth instance of the defect", `composition.cpp:10914`).
- Camera bake keys deliberately stripped and re-baked (`engine.cpp:937-958`).
- ADR-440: no example project round-trips byte for byte.

**The Director inherits this directly, in three ways:**
1. Its content must survive save → load. Every new field (semantic camera moves, performance plans, markers) needs a write, a read and a round-trip test in the same commit.
2. **A preview render is a save** (`startRenderFromUi` writes the user's project file before rendering, `application.cpp:2421-2440`). A Director preview button that triggers a render therefore writes the project, including every drop defect. Preview must render from a temp copy.
3. Snapshots are whole-document JSON (`SnapshotStore`). `edit_history.hpp:23` itself notes that serialisation loses what the scene format does not write (loaded glTF data, built terrain). Rollback through a snapshot is therefore only as good as the serialiser.

**Required gates** (none exist):
- **Plan schema validation, then a semantic validator** covering references, capabilities, overlaps, determinism tier (`triggerIsScheduled`/`actionIsBaked`), and spatial feasibility (nav reachability, landing navigable).
- **Compile to a staging copy, then diff, then approve, then apply in one transaction.**
- **A round-trip check on commit:** serialise, reload into a scratch Engine, and compare the Director's own content. This would have caught every drop defect listed above.

---

## 15. Performance (§15)

| Loop | Rate | Cost today | Interaction risk |
|---|---|---|---|
| LLM call | seconds | Worker thread (`Orchestrator` on a job; tools marshalled via `MainThreadQueue::pump(budgetMs)`) | Low. Already async and budgeted per frame. |
| Director compile and validate | per request | ms (arithmetic, JSON) | Low |
| **Re-bake/install after a Director edit** | per commit | `seq::install()` plus possibly `Composition::rebuild()`. Starring a hero costs **1,454 ms**, of which 1,125 ms is `SceneRenderer::uploadTextures` re-creating textures because `rebuild()` bumps `textureVersion` unconditionally (docs/investigations/ui-responsiveness.md) | **Medium**, if a Director edit triggers a rebuild |
| **Seek / preview a span** | per preview | `EntityWorld::seek` re-simulates up to 90 s. About **2.5 s median per click** on Glowmere [main]; [PD] ADR-671 director replay: 362 ms at 30 s, 1,077 ms at 90 s; ADR-273: 161 s with 100 characters | **High.** Every "preview at 1:30" is a seek. Scripted (baked) performances are free to seek, one more reason to prefer them. |
| Fast model | 5–30 Hz | n/a | GPU contention with the renderer on unified memory; must be recorded for determinism |
| [PD] decider | 2 Hz per character | about 0.011 ms per character | None |
| Render | per frame | about 13.4 ms after the upgrade | Baseline |

**Conclusion:** the async LLM and authoring-time Director coexist fine with interactivity, because the control plane already does the right thing. The performance risks are the **existing** seek and rebuild costs, which a Director will trigger far more often than a human. The scrub/rebuild fixes already identified (coalesced seeks, [PD] checkpoints per Phase F §51, not bumping `textureVersion`) become Director prerequisites.

---

## 16. End-to-end walkthrough: Rook and Umbra

> *"At 1:30, create a 5-second shot where Rook runs toward the Umbra hero mushroom, jumps, performs a backflip over it, and continues running. Use a low-angle chase camera that rises over Rook during the flip and continues forward past him. Trigger a slow-motion frame-drag effect during the flip and activate the Umbra hero effect at the backflip peak."*

Scene: `examples/world/glowmere-valley-2-multicam.scene.json` [main]. `rook` is an entity driving node `rook` (`alien-scout.glb`); `umbra-cap` is `heroes[6]` at (−64.2, 8.4, 166.7) with radius 2.31 and height 5.5.

| # | Stage | What must happen | Status | Evidence |
|---|---|---|---|---|
| 1 | **Request** | Prompt enters the Director | **CURRENTLY SUPPORTED** | `AiPanel`, `ControlPlane::submit`, `--ai-prompt` |
| 2 | **Interpret** | LLM extracts time 90 s, duration 5 s, subject "Rook", target "Umbra", beats run/jump/backflip/land/run, camera chase/rise/pass, effects slow-mo, frame-drag, hero effect at peak | **PARTIALLY SUPPORTED** | Provider tool-calling exists. There is no plan schema to interpret *into*, so today the model would go straight to low-level tools. |
| 3 | **Inspect** | Resolve "Rook" → entity `rook`, node `rook`; "Umbra" → hero `umbra-cap` (also nodes `umbra-*`); positions; what is at 1:30 (section, other shots) | **PARTIALLY SUPPORTED** | `entity.list`, `scene.find_nodes("umbra")`, `scene.get_node`, `sequence.get_state`. No subject resolver or alias table: "Umbra" matches five nodes and a hero, and "Rook" is lower-case `rook`. No time parser. |
| 4 | **Capability discovery** | Rook can: run (Running, 7.39 m/s run speed in song scene), jump (Jumping / Jump_running), fall, land. **Cannot: backflip.** Umbra: hero with activation radius, reaction profile, preferred camera distance 17.1. Effects available: "Hero Pulse" (heroFocus), "Camera Travel Beam", `temporal/echo`. **No "umbra_hero" effect.** | **REQUIRES NEW IMPLEMENTATION** | Capability card and effect catalogue do not exist (§10). Today the agent would not learn that the backflip is impossible; it would attempt it. |
| 5 | **Shot plan** | `seq::Shot "rook-umbra"` 90–95 s; check overlap with existing shots; camera track cut at 90 s to a new/assigned camera | **PARTIALLY SUPPORTED** | `sequence.add_shot` exists (no camera). No tool for the `CameraShot` track; not undoable via history today. |
| 6 | **Character action plan** | Scripted performance: path from Rook's position at 90 s toward a take-off point about 2 m short of Umbra's radius; ballistic arc over a 5.5 m-tall cap; land beyond; run on. Clip cues Running → Jump_running → [backflip] → Landing → Running | **REQUIRES NEW ARCHITECTURE** (performance model and compiler) **plus NEW IMPLEMENTATION** (arc to keys, one-shot clips, entity hand-off) **plus an ASSET** (backflip) | `seq::Actor` keys and cues exist and are pure. `Airborne` arc maths exist but are private to `explore`, with `apex = 1.1`, `maxDistance = 4`. **A 5.5 m-tall mushroom cannot be cleared by any existing jump setting**, so it is a spatially impossible request as stated (fly-over means an apex above 6 m). Rook's position at 90 s is live-tier: it depends on 90 s of autonomous simulation, so the plan must read it from a seek or set a stage mark. |
| 7 | **Camera plan** | Keys: low chase (h≈0.4 m, d≈3 m behind) → rising to above Rook at peak → passing ahead of him; aim at Rook; focal 24–28 mm | **PARTIALLY SUPPORTED** | Achievable **only** as `CameraKind::Keys` computed from the actor path (§6.2). No time-varying Chase offset; rig `followOffset` not keyframeable. |
| 8 | **Effect plan** | (a) FrameEcho ramp on `temporal/echo/*` 92.2–92.9 s; (b) "slow motion ×0.35" over the flip; (c) hero effect at peak | (a) **SUPPORTED as keys** via `SequenceEvent{SetParameter}` or `sequencer.add_keyframe`; (b) **REQUIRES NEW ARCHITECTURE** (time warp, §7.3); performance-local retime is **NEW IMPLEMENTATION** in the compiler; (c) **PARTIALLY SUPPORTED**: a world-effect `Window` or a `HeroFocus` effect targeted at `umbra-cap`, or keying the hero's glow parameters; the "umbra_hero effect" must be defined, not found | `effects.hpp:93`; `temporal_settings.hpp` |
| 9 | **Event plan** | `rook.jump_started` @≈92.1, `rook.backflip_peak` @≈92.55, `rook.landed` @≈93.0 as Cue markers; events keyed to them | **PARTIALLY SUPPORTED** | Cue markers and `TriggerKind::Cue` exist and bake. Times are computable **only** in the scripted mode. There are no animation events for a live-tier Rook, and the live dispatcher has no producer. |
| 10 | **Timeline plan** | Assemble shot, camera cut, actor, markers and events into one `Sequence` change plus a `CameraDirection` change | **PARTIALLY SUPPORTED** | Structures exist; tools missing for actors, events and the camera track. |
| 11 | **Validation** | References resolve; capabilities exist (**fails on backflip and on clearance**); no shot overlap; all events baked (scrub-safe); landing navigable; camera clears terrain | **REQUIRES NEW IMPLEMENTATION** | Primitives exist (`Sequence::validate`, `triggerIsScheduled`, `actionIsBaked`, `Navigator::sample`, `groundHeightAt`), but no validator composes them. |
| 12 | **Execution** | One transaction; compile to native content; one undo entry | **PARTIALLY SUPPORTED** | `Transaction` exists; undo covers the sequence (`TimelineChange`) but not the camera track, effects or keys via the AI sink (§13). |
| 13 | **Preview** | Seek to 89 s, play 90–95 s, return thumbnails | **PARTIALLY SUPPORTED** | Manual playback works. The seek costs seconds (autonomous re-simulation). No thumbnail tool; `render.probe` gives numbers. Render-to-file saves the project first. |
| 14 | **Refinement** | "Lower the camera; hold the peak longer" → edit the plan, recompile, diff | **REQUIRES NEW ARCHITECTURE** | Needs the plan to persist as editable state linked to the content it generated; there is no persistent conversation today (`orchestrator.cpp:205`). |

**What an honest Director says today (after the MVP):**

> "Rook has no backflip. The tallest jump available clears about 1.1 m, and Umbra's cap is 5.5 m tall, so I planned a running jump *past* the mushroom rather than over it. Here is a 5 s shot at 1:30 with a keyed chase camera that rises at the jump's peak, a frame-echo ramp during the air phase, and the hero pulse on Umbra at the peak. Slow motion is not supported. Accept?"

**What it would say after the full programme** is the brief's version, with a backflip clip acquired, `apex` authored for the stunt, and performance-local retime.

---

## 17. Development effort (§17)

Complexity: XS < 2 days · S ≤ 1 wk · M 1–3 wk · L 3–6 wk · XL 6–12 wk · XXL > 12 wk. Effort is in developer-weeks for one engineer who already knows this codebase.

| WS | Current state | Gaps | Depends on | Cx | Effort | Risks |
|---|---|---|---|---|---|---|
| **A** Command/semantic API | 58 low-level tools, solid tool framework | Director Plan schema; semantic verbs; compiler framework (HTN-style decomposition) | B, C | L | 4–6 | Designing verbs too close to internals, or too vague to validate |
| **B** Introspection | Scene/param/sequence read tools | Subject resolver and aliases; spatial queries; time parser; semantic scene digest | – | M | 2–3 | Name ambiguity (5 `umbra-*` nodes plus a hero) |
| **C** Capability registry | Enums, ParamDesc, EffectSchema, clips maps, [PD] capabilities | Generated character cards, effect and camera catalogues, clip semantics metadata | B | M | 2–4 | Stale capability claims (ADR-617 risk) |
| **D** Timeline API | Full C++ edit API; 3 add-only tools | Tools for shot move/trim/remove/camera/transition, actors, events, camera track; seq↔camera-shot coherence | K | M | 2–4 | Two "shot" types confused (ADR-245) |
| **E** Character actions | Action lists, Director tier, Staging, `seq::Actor`, [PD] goals | Performance type with 3 compile modes; entity↔actor hand-off; multi-action timeline events | F, D | L–XL | 5–8 | Actor and entity fighting over a node (Unknown) |
| **F** Animation semantics | Airborne arcs (autonomous), in-place clips, contacts | Jump-to action; pass-through Move; one-shot states; airborne in the provider chain; clip markers; open activity vocabulary; **backflip asset** | Asset pipeline ([AR] ADR-650) | XL | 6–10 (+ asset) | No acrobatic motion exists; clip/arc timing mismatch |
| **G** Camera semantics | 10 presets, 3 behaviours, rigs, keys | Time-varying chase offsets; behaviours targeting entities; keyframeable follow offset; named moves stored as state | D | L | 4–6 | Presets are stamps, not state; opaque keys |
| **H** Events/triggers | 12 triggers × 7 actions; tiered model | Plan-time semantic markers; live producer for `TriggerSignal`; apply non-EntityAction live events; event-activated effects | E, F | M–L | 3–5 | Live events in renders break reproducibility |
| **I** Effect API | Parameters, world effects, EffectSchema | Effect catalogue; structural add/remove tools; "activate at t" | C, K | M | 2–3 | Effects addressed by raw parameter paths |
| **J** Agent orchestration | Loop, budgets, transactions, providers | Plan/approve state; persistent Director session linked to content; streaming; optional MCP export | A | M | 2–4 | Conversation state vs project state drift |
| **K** Validation/transactions | Schema validation, snapshot transactions, partial undo | Semantic validator; dry-run on a staging copy; undo for camera/effects/keys; round-trip gate on commit; one undo mechanism | – | L | 4–6 | Save-drop class of defects (ADR-618) |
| **L** Preview/verification | Manual playback; `render.probe` | Span preview; thumbnail capture without saving the project; optional vision loop; cheaper seeks | K | L | 3–6 | Seek cost; preview render writes the project |
| **M** UI | `AiPanel` | Director panel: plan view, diff, accept/modify/regenerate, capability sidebar | J, K | M | 3–4 | "I can't see ImGui": needs screenshot-driven verification |
| **N** Fast-model integration | None; [PD] decider fills the role | Recorded-inference `LearnedBehaviorProvider` seam; model runtime | [PD] merged | S (seam) / XL (model) | 1 (seam) / 8–12 (model) | Determinism, GPU contention, no Mac runtime for Laya |
| **O** Character runtime integration | Director tier, [PD] goals | Goal-mode compile; Phase F cinematic constraints (stay in shot, …) | E, [PD] | M–L | 3–5 | Bias ≠ guarantee; one-errand limit |
| **P** Testing | Scripted provider, 74 AI tests, determinism suites | Director benchmark harness (§24), golden plans, round-trip tests, scripted-provider plan fixtures | all | M–L | 3–5 (ongoing) | Green suite lies (docs/testing.md) |

**Totals, with explicit assumptions:**
- Assumptions:
  - one senior engineer fluent in this codebase, or two at about 1.7× throughput;
  - [AR] and [PD] merged first;
  - no new rendering features;
  - the backflip clip is acquired separately, by purchase, mocap or Phase F4 generation, and is not costed;
  - estimates include tests at this repo's standard, which is high (ADR-heavy, measured claims).
- **MVP (§23):** A (subset), B, C (subset), D, I, J (plan/approve), K (subset), M (subset), P. About **8–12 developer-weeks**.
- **Full brief without N:** sum of the ranges, about 50–80 developer-weeks. That is roughly **12–18 developer-months** for one person, or **9–15 months** calendar with two people plus coordination overhead.
- **N (a real fast model):** another 2–3 months, **not recommended now**.

Confidence is low at the top end. F (animation semantics) and the time-warp decision (§7.3) are the high-variance items.

---

## 18. Prerequisites (ranked by dependency)

**Must have:**
1. Merge [AR] and [PD] (the motion seam, the `goal` considerer, ADR-671 director replay, world events). The Director's character story depends on them.
2. **One undo mechanism covering every domain the Director writes:** sequence (done), camera direction, world effects, timeline keys. Remove the snapshot-restore undo path.
3. **Director Plan schema plus validator**, including determinism-tier checks (`triggerIsScheduled`/`actionIsBaked`).
4. Tools for sequence, camera track and events over the existing C++ API.
5. A save round-trip gate for every new field (ADR-618 lesson).
6. Preview that never writes the user's project.

**Strongly recommended:**
7. Capability registry (generated character cards, effect catalogue).
8. Subject resolver plus time parser.
9. Time-varying camera behaviour offsets, stored as behaviour state.
10. Scripted performance compilation to `seq::Actor`, plus an entity hand-off probe.
11. Seek-cost reduction (checkpoints; the known `textureVersion` rebuild fix).

**Nice to have:**
12. Jump-to action, pass-through Move, one-shot clips, clip markers.
13. Live `TriggerSignal` producers and event-activated effects.
14. MCP exposure of the registry (lets external agents, including Claude Code, drive AV Gen).
15. Vision-based preview critique.

**Later:**
16. Scene-time warp (true slow motion).
17. Phase F cinematic constraints and auto-staging.
18. Learned behaviour provider or a fast model.
19. Generated motion (F4) for rare moves like a backflip.

---

## 19. Risks, stated plainly

| Risk | Severity | Why it is real here |
|---|---|---|
| **Hallucinated capabilities** | Critical | Today nothing tells the agent Rook cannot backflip. `capability.list` is already stale. Without a generated registry, the Director will confidently produce a `Pose("backflip")` that plays a fallback clip. |
| **Impossible spatial requests** | High | A 5.5 m mushroom vs a 1.1 m apex. Nothing checks clearance, reachability or landing navigability at plan time. |
| **Undo gaps** | High | Camera track, world effects and keyframe drags bypass the history. The AI sink is parameter-only. There are two undo mechanisms. |
| **Persistence drops** | High | Five-plus historic cases of saves dropping authored state. A preview render is a save. Director content multiplies the surface. |
| **Nondeterminism** | High | Any live-tier trigger (animation events on autonomous characters, signal-driven world events) in a rendered effect breaks "a render is reproducible". Modulation is not replayed on seek even with ADR-671. |
| **Animation not expressive enough** | High | 26 in-place clips, no acrobatics, looping one-shots, Move always stops. A cinematic Director will ask for motion the asset set cannot give. |
| **APIs too low-level or coupled to internals** | High | The current tools are parameter paths. A Director built on them generates opaque parameter soup that users cannot edit meaningfully. |
| **Opaque generated content** | Medium–High | Camera moves as 48 keys; performances as hundreds of actor keys. Mitigation: store semantic behaviours as state; keep the plan linked to its output. |
| **Non-transactional timeline** | Medium | `TimelineChange` covers the sequence. The camera track and piece-level keys are outside it. Human edits push after the fact. |
| **Ambiguous entity identity** | Medium | "Umbra" = 5 nodes plus a hero. Names are lower-case slugs (`rook`). Two shot types; three "director"s (`WorldDirector` knobs, the camera auto-director, `stage::Staging`). |
| **Asset naming** | Medium | Clips are raw glTF names (`Jump_running`). Activities are a closed enum; semantic names must map per character. |
| **Missing semantic metadata** | Medium | No clip semantics (airborne, rotational, peak frame), no traversal constraints. |
| **Camera conflicts** | Medium | Locked shot > event claim > shot > default (`resolveActiveCamera`). An event-claiming rig (UFO Watch, priority 10) can override a Director shot during an abduction unless the plan sets `locked`. |
| **Action failures** | Medium | Directed Move can fail (unreachable), and a `goal` is a bias. A plan compiled to live mode may not happen. Only scripted mode guarantees it. |
| **Performance and GPU contention** | Medium | Previews trigger multi-second seeks; rebuilds cost about 1.4 s. A fast model on the GPU competes with a 13 ms render. |
| **Context explosion** | Low (by design) | Pull-based context already exists; 700 KB files must never be sent. |
| **Too many tools** | Medium | 58 today, plus Director verbs. Tool-choice quality drops with count. Group by domain and expose semantic tools first, low-level ones on request (`capability.list_tools{domain}` already supports this). |
| **AI-specific structures contaminating the engine** | Medium | The tool API was designed to avoid this (no vendor names outside `providers.cpp`). The Director Plan must be an *engine* document (usable by a human or a script), and must not live under `src/ai/`. |
| **Doc rot** | Medium | Stale docs found in this very investigation (tool counts, transaction scope, "ADR-360", "director observes"). |

---

## 20. Current LLM plane assessment: extend, restructure or replace?

**Extend and partially restructure. Do not replace.**

**Evidence it should be kept:**
- The tool API's contract (validated input, structured recoverable errors, MCP-shaped annotations, thread requirements, undoable flags; `tool_api.hpp`) is what a Director's command layer needs. It is vendor-neutral by construction.
- The orchestrator already has an explicit state machine, budgets, cancellation, lazy transactions, and structured activity for the UI (`orchestrator.hpp`).
- `TransactionSink` is a deliberate seam to the editor undo (`transaction.hpp:12-18`), and `EditHistoryTransactionSink` is already installed (`application.cpp:929-933`).
- Context strategy (a stable system prompt from the registry, O(1) ambient state, O(n) pulled) solves the 700 KB problem.
- There is a deterministic scripted provider for tests, 74 tests, and five providers including local.

**Evidence it must be restructured:**
- **The level of abstraction is wrong for a Director.** Every mutating tool writes a parameter base value, a route, a timeline track, a node or a sequence entry (`transaction.hpp:26-29`). There is no semantic layer in between.
- **No plan artefact:** the loop goes straight from model turn to mutation. It needs a Plan → Validate → Preview → Approve state before Commit (a `WaitingForApproval` state).
- **Undo is split:** parameter-only history plus snapshot restore. It needs one mechanism.
- **Capability discovery is hand-written and stale** (`capabilities.cpp` `kDomains`).
- **Conversations are not persistent** (`orchestrator.cpp:205`). Refinement ("lower the camera") needs the Director session to be linked to the content it produced.

**Target shape:** keep `src/ai/` as the agent client and tool host. Add a new engine-level module (for example `src/direct/`, a name to be chosen that avoids `WorldDirector`, `app::CameraDirector` and `stage::Staging`) owning:
- the Director Plan document, validator and compiler;
- the capability registry.

Semantic tools in `src/ai/` become thin wrappers over it.

---

## 21. Target architecture

```
                               ┌────────────────────────────────────────────┐
  user ──prompt──> Director    │ src/ai/  (exists; extend)                  │
         panel <── activity ── │  Orchestrator + states (+ Plan/Approve)    │
                               │  Providers (Anthropic/OpenAI/…/local)      │
                               │  ToolRegistry: semantic tools (new)        │
                               │               + low-level tools (exist)    │
                               └──────────────┬─────────────────────────────┘
                                              │ typed calls
                               ┌──────────────▼─────────────────────────────┐
                               │ engine-level Director module (NEW)         │
                               │  Resolver: names/aliases/time ("1:30")     │
                               │  Capability registry (generated from       │
                               │    EntityDesc, clips, EffectSchema, enums) │
                               │  Director Plan (serialisable, versioned)   │
                               │  Validator: refs, capabilities, overlaps,  │
                               │    determinism tier, spatial feasibility   │
                               │  Compiler (HTN-style decomposition)        │
                               └───┬───────────────┬──────────────┬─────────┘
                        scripted   │      directed │         goal │
                                   ▼               ▼              ▼
          ┌──────────────────────────────┐ ┌──────────────┐ ┌───────────────────┐
          │ seq::Sequence (exists)       │ │SequenceEvent │ │[PD] goal          │
          │  Shot, Actor keys/cues,      │ │ EntityAction │ │ considerers       │
          │  Markers(Cue), Events(baked) │ │ → direct()   │ │ → utility decider │
          │ scene::CameraDirection       │ │ Director tier│ │ (+ future learned │
          │  rigs, CameraShot track      │ └──────┬───────┘ │  scorer, recorded)│
          │ params::Timeline keys        │        │         └─────────┬─────────┘
          │ world-effect windows         │        │                   │
          └──────────────┬───────────────┘        │                   │
                         │ bake/install (exists)  │   live tier       │
                         ▼                        ▼                   ▼
          ┌──────────────────────────────────────────────────────────────────┐
          │ runtime (exists): transport · entities · motion/IK · camera ·    │
          │ renderer. The LLM is never here.                                 │
          └──────────────────────────────────────────────────────────────────┘
   cross-cutting: EditHistory (one undo; extend to cameras/effects/keys) ·
                  round-trip gate on commit · preview from temp copy
```

Rules:
1. The Director Plan is **engine data**, reachable from UI, scripts and a future MCP without the LLM.
2. Everything the compiler emits is **native content** a user can edit without the Director.
3. **Anything that must render reproducibly compiles to the baked tier.** Live-tier output is labelled as such in the plan and the UI.
4. The compiler, not the LLM, does geometry and timing.

---

## 22. Implementation roadmap (vertical slices)

**Slice 0: Foundations (3–4 wk).**
- Merge [AR]/[PD].
- Single undo covering camera direction, world effects and timeline keys; remove snapshot-restore undo.
- Round-trip gate helper.
- Preview-from-temp-copy.
- Fix `capability.list` to be generated.

*Exit:* every existing AI tool's effect is ⌘Z-able in one step and survives save/load.

**Slice 1: "Shots and cameras on music" (4–6 wk). This is the MVP; see §23.**
- Plan schema v1 (shots, camera cuts, camera moves from existing presets and keys, markers, parameter cues, effect windows).
- Resolver.
- Validator.
- Compiler to native content.
- Plan/approve state.
- Minimal panel with diff.

**Slice 2: Scripted performances without new motion (4–6 wk).**
- `seq::Actor` compilation for run/walk paths to subjects, with clip cues from the capability card.
- Entity hand-off probe and fix.
- Plan-time semantic markers.
- Keyed chase camera derived from the actor.

*Exit:* "Rook runs past Umbra while the camera tracks low and rises" works and is editable.

**Slice 3: Airborne and events (5–8 wk).**
- Jump arcs as compiled keys (reusing `Airborne` maths).
- One-shot clip states.
- Pass-through Move.
- Clip markers.
- Time-varying `CameraBehavior` offsets stored as state.
- Performance-local retime plus FrameEcho.

*Exit:* the Rook/Umbra shot with `Jump_running` in place of the backflip, and "slow-mo" as a local retime.

**Slice 4: Autonomous direction (4–6 wk).**
- Goal-mode compile to [PD] `goal` considerers.
- Live `TriggerSignal` producers.
- Phase F "director observes" candidate shots, surfaced as *proposals* in the panel.

**Slice 5: Verification and scale (4–6 wk).**
- Thumbnail/vision critique loop.
- Director benchmark CI.
- MCP export.
- Seek checkpoints.

**Later:**
- Scene-time warp.
- Backflip and other rare motion via acquisition or F4.
- Learned scorer.

The order is reordered from the brief's implied order (characters first) because the code says cameras and timeline are ready and characters are not.

---

## 23. Minimum Viable Director

**What it does:** takes a natural-language request about **shots, cameras, cuts, markers and effect/parameter cues at musical times**, and proposes a typed plan. The plan compiles to ordinary `seq::Shot`s, `CameraShot`s, rigs, markers and baked `SequenceEvent`s. It shows a diff, applies on approval as one undo step, and refuses capabilities that do not exist.

It deliberately does **not** move characters. It can frame them, because rig `aimNode`/`followNode` already work on entity nodes.

**What it needs:**
1. **Plan schema v1:**
   - `Shot{name, start|section, duration, camera}`
   - `CameraMove{preset|behaviour|keys, subject}`
   - `Cut{time, camera, transition}`
   - `Marker`
   - `Cue{time|marker, effect|parameter, value, ramp, hold}`
2. **Resolver:** subject names (entities, heroes, nodes), plus times ("1:30", "chorus 2", "bar 64").
3. **Capability registry v1:** cameras and presets, the effect catalogue (world effects, atmosphere kinds, `temporal/echo`, hero glow parameters), subjects.
4. **Validator:**
   - references;
   - shot overlap (`Sequence::validate`);
   - camera track coherence;
   - all events baked (`actionIsBaked`);
   - `locked` shots where event-claiming rigs would steal the frame.
5. **Compiler** to existing structures.
6. **Tools:** `director.inspect`, `director.propose_plan`, `director.validate`, `director.apply`; plus sequence/camera-track/event tools over existing C++ APIs.
7. **Orchestrator** `AwaitingApproval` state.
8. **Undo** for camera direction and events (Slice 0).
9. **Panel:** conversation, plan, diff, accept/reject.

**Estimate:** 8–12 developer-weeks including Slice 0. It would plausibly pass benchmark levels 1–6 (§24).

---

## 24. Director benchmark

| # | Request | Needs | Plausible with |
|---|---|---|---|
| 1 | "What's in this scene and who are the characters?" | Introspection | **Today** |
| 2 | "Add a marker called 'drop' at 1:30." | Time parse, marker | Today (with seconds), MVP (with "1:30") |
| 3 | "Make the fog thicker during the second chorus." | Section resolve, baked param cue | Today (partly, via `sequencer.add_keyframe` with seconds), MVP |
| 4 | "Cut to the Valley Wide camera at the start of each chorus." | Camera track tool, sections | MVP |
| 5 | "Create a 6 s establishing shot of Umbra at 0:45 that slowly pushes in." | Shot plus camera preset (Reveal/Approach) on a hero | MVP |
| 6 | "Pulse Umbra's glow on every downbeat of the bridge, and shake the camera on the last one." | Beat triggers, baked events, effect catalogue | MVP |
| 7 | "Follow Rook with a low camera from 1:10 to 1:20." | Rig followNode on an entity; fixed offset | MVP (fixed offset); Slice 3 (low-to-high move) |
| 8 | "Replace every hard cut in the verse with a 0.5 s blend." | Bulk camera-track edit | MVP |
| 9 | "At 1:30, Rook walks to Umbra and looks up at it." | Scripted or directed performance, Face | Slice 2 |
| 10 | "Have Tide and Sage meet at the river during the break." | Two performances, spatial resolve, nav | Slice 2 (scripted); Slice 4 (goal) |
| 11 | "Rook runs past Umbra; the camera chases low and rises as he passes." | Actor path plus keyed chase | Slice 2–3 |
| 12 | "Rook jumps over the log at 2:05; flash the lights at the peak." | Airborne compile, plan-time peak marker | Slice 3 |
| 13 | "Slow everything down to 35% during the jump." | Time warp | Slice 3 (local retime of the performance only); **Later** (global) |
| 14 | "During the chorus, let the aliens investigate the mushroom on their own, and cut to whoever reaches it first." | Goal compile, live events, candidate shots | Slice 4 (live, not render-reproducible unless replay covers it) |
| 15 | **The full Rook/Umbra backflip request** | Backflip asset, clearance-feasible arc, retime, keyed camera, peak events | Slice 3 **plus asset**; the fly-over as stated is spatially impossible with current jump settings |
| 16 | "Make the whole song feel more cinematic." | Open-ended multi-shot planning, critique loop | Slice 5; quality uncertain |

**Current architecture, with no new work:** levels 1–3, partially and with raw seconds, through low-level tools. The result is opaque (parameter keys, not a plan).

---

## 25. Final feasibility verdict

**YES, but significant semantic and API work is required.**

**Why:**
- The agent infrastructure (tool API, loop, transactions, providers, panel) exists and is well designed.
- The target content model (sequence, camera direction, baked events) is rich, editable and deterministic.
- The character stack has a Director authority tier and, on [PD], goal direction.
- What does not exist is the semantic plan, compiler, capability and validation layer between them, along with specific motion and camera capabilities.
- These are significant but well-bounded extensions of existing structures, not foundational rewrites.

The only area that approaches "major foundational work" is **true slow motion** (a scene-time warp), which the roadmap defers.

| Question | Answer |
|---|---|
| **How much can be reused?** | About 60–70% of the infrastructure: `src/ai/` almost entirely; `seq::Sequence`/events/bake; `CameraDirection`/rigs/presets; `ActionQueue`/Director tier/Staging; `Airborne` maths; [PD] goals and decider; `EditHistory` model; determinism machinery. Almost none of the semantic layer exists. |
| **Biggest gaps** | (1) No semantic Director Plan, compiler or validator. Tools are parameter-level. (2) No capability registry, so nothing prevents asking for a backflip that no asset contains. (3) Motion cannot perform authored stunts: no jump-to action, Move always stops, clips always loop, no clip events, no acrobatic clips. (4) Semantic events on live-tier characters cannot drive reproducible effects: the live dispatcher has no producer and animation events do not exist. The fix is plan-time events in the baked tier. (5) Undo and persistence are incomplete for camera direction, effects and keys; preview renders save the project. |
| **MVP** | A Director for shots, cameras, cuts, markers and effect cues at musical times, compiled to native content with validation, diff, approval and one-step undo, and refusing nonexistent capabilities. 8–12 developer-weeks. |
| **Total effort** | About 12–18 developer-months for one engineer (9–15 months calendar with two) for the full brief, excluding acquiring a backflip clip and a real fast model. |
| **What to do first** | Slice 0: unify undo across camera direction, effects and keys; add the round-trip gate; make preview not write the project; generate `capability.list`. Then define the Director Plan schema as an engine document before writing any semantic tool. |
| **Laya-like layer now?** | **No.** Design the seam only: Phase D §71's `LearnedBehaviorProvider`, with inference *recorded* into the simulation for scrub and render. Laya itself is an enterprise text classifier, Python only, with no Mac runtime and three days old. The [PD] utility decider already does this job at about 0.011 ms per character, deterministically. Revisit only if a measured behaviour-quality problem appears that utility scoring cannot fix. |

---

## 26. Open questions (UNKNOWN; the owner or a short probe can resolve these)

1. **Actor/entity hand-off:** does a `seq::Actor` driving `rook` while the `rook` entity runs produce a clean result if the entity is held at the Director tier, or does the additive composition (`Entity::fieldPosition`) still leak entity motion? This needs a probe.
2. **Slow motion intent:** should "slow motion" slow the world while the music plays on (scene-time warp, XL), or is slowing only the directed performance and camera acceptable (local retime, S–M)?
3. **Backflip source:** purchase or commission a clip, mocap, or Phase F4 generation? And does it need to exist on all six alien variants (clips cannot be shared across files, ADR-192), or only once per skeleton (ADR-650 [AR])?
4. **Does "umbra_hero effect" name something the owner has in mind** (a planned hero effect, or the existing "Hero Pulse" `heroFocus` world effect)? It exists nowhere in either tree.
5. **Authoritative scene for the benchmark:** `glowmere-valley-2-multicam` or `-song`? Rook's `jumpRange` and camera set differ between them.
6. **Should Director plans persist in the project file** (for refinement and provenance, Phase F §66 "GENERATED" state), or be ephemeral with only the compiled content kept?
7. **External agents:** is exposing the tool registry over MCP (so Claude Code or other agents can drive AV Gen) wanted? The design anticipates it; nothing is built.
8. **Does `sequence.add_shot` surface `Sequence::validate` overlap errors** as `Conflict`? Not verified.
9. **Provider policy:** is cloud LLM use acceptable for production authoring, or must the Director work on the `local` provider? This changes the achievable plan quality substantially.
10. **"Director observes, does not dictate":** confirm the reading in §6.4 (applies to runtime auto-direction; an authoring Director proposes and the user approves).
