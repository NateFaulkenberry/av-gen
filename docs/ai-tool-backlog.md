# The AI control plane's missing verbs

A handoff. Item 1 of the list below is **done and on `main`**; items 2–8 are the work.

## Why this exists

The *All You Got* bootstrap prompt asks the internal assistant to create a project, import an mp3,
build a sequence, construct a city, place a protagonist, wire audio reactivity, and save it all so
it survives a reload. Audited against the tool registry, it could do almost none of that, and the
reason was one sentence long:

> The plane had 35 tools, 15 of which mutated anything, and **not one of them created anything.**

Every verb was *change a value on something that already exists*. That is not an oversight —
ADR-094 deliberately built the surface on `params::ParameterSet`, the engine's real semantic
surface, where node transforms, camera pose, sky, fog, wind and material emission are all
registered parameters with ranges and metadata. That surface is very good at tuning. It has no way
to express *create*, because a parameter set describes what exists, not what might.

So the gap is not "a few more tools". It is a category of verb that was missing, and each item
below adds one.

## What is already done (item 1)

**`project.list` · `project.create` · `project.save` · `project.save_as` · `project.open`** —
`src/ai/engine_tools.cpp`. The plane is now 40 tools across 14 domains.

Three decisions in there that the rest of the list should follow:

- **They take a name, never a path.** A tool that took a path would let one bad argument write
  anywhere on the machine. A name resolves under `ToolContext::projectsRoot()`, which the
  application sets to `<preferences>/projects`, so the worst a wrong answer does is make a folder
  in a directory the user already owns. A name that would escape the root is **refused, not
  sanitised**: an agent that asked for `../../etc` has made a mistake worth reporting, and quietly
  rewriting it teaches it nothing.
- **They are `mutatesSession`, not `mutatesProject`, and `undoable = false`.** The snapshot domain
  is parameter state. A snapshot cannot un-write a file or put back the session that opening
  another project replaced, so claiming `mutatesProject` would promise a rollback nothing keeps.
  `ToolRegistry::add` enforces that a tool is classified; a test enforces that the classification
  is honest. Read `sessionWrite()` before annotating anything new.
- **They report what they did, read back.** `project.save` returns whether the file exists and how
  many bytes, because a save that silently wrote nothing is the failure worth catching. The test
  for this is a round trip through a *second engine* — the same engine would pass even if the file
  on disk were empty — and it is negative-controlled: make `project.save` report success without
  writing, and the reopen fails.

Tests: `tests/unit/test_ai_tools.cpp`, tag `[ai][tools][project]`. Full suite 1527, green.

## The remaining eight, in the order to do them

### 2. `asset.import` and a bounded filesystem resolve

Unlocks the prompt's §2 — "import `~/Desktop/test.mp3`". Today the agent cannot resolve a path or
load media; it can only read `hasAudio`.

Mostly a safety decision rather than much code: **which roots may an agent read**, and does import
copy into the project or reference in place. The project format already stores relative paths with
size and sha256 (see `Engine::saveProject`), so copying-in is the behaviour the format expects.
`Engine::loadAudio` does the actual work.

### 3. `scene.create_node` · `delete_node` · `set_parent`

The big one. The prompt's §6–§12, §21 and §22 all hang off it — city, apartment, bedroom, bathroom,
stage, band, the walking route.

Previously blocked on the transaction story: node creation is outside the snapshot domain, so a
rollback would leave the node behind. **The editor's undo has since landed** (ADR-092, command
records rather than snapshots), so the path is now to write a `TransactionSink` over its
compound-group API and call `ControlPlane::setTransactionSink()`. `ai::TransactionSink` is one
virtual with `begin`/`commit`/`abort`; nothing else changes — not the orchestrator, not a tool, not
a test. That was designed as the seam and this is the thing it was designed for.

Do not add node creation until that sink exists. A create tool with a snapshot-backed transaction
would report a rollback it cannot perform.

### 4. `world.generate(recipe)`

The cheapest large win, and the reason to do it before the sequence work: **it already functions.**
`assets/city.manifest.json` and the 1,071 imported meshes are on disk, and a generated world was
verified this week — 47 scatter layers installed, trees and pedestrians placed, and the navigation
layer picked the pedestrians up as obstacles without being asked.

One tool wrapping `world::composeWorld` + `Application::generateWorldFromRecipe` turns §6–§7 from
impossible into mostly automatic. Take a recipe by name from `examples/recipes/`, or a small inline
recipe object; the validation already exists in `world::WorldRecipe::fromJson`.

**Know the limit before you promise it**: the composer places by *density* and caps a world at 64
scatter layers, which is why the shipped Glowmere manifest is 13 entries and why the city manifest
is 47. Streets and blocks are a grid problem, not a scatter problem. This tool dresses a world; it
does not lay out a city block.

### 5. `sequence.create_shot` · `set_camera` · `add_marker` · `add_actor`

§3, §4, §13 and §20. The read side landed this week as `sequence.get_state` (shots, scenes, actors,
overlays, sections, and the song's beats around a named second); this is its mirror.

**The catch worth designing around first**: editing a sequence means re-baking it, and a bake
replaces every track it owns — `sequencer.get_state` already reports those under
`ownedBySequence`. So a mutating sequence tool is not a setter; it is an edit followed by a bake
followed by a track replacement, and the transaction boundary has to cover all three or a rollback
leaves half a piece behind.

### 6. `layer.*` over `comp::LayerStack`

§5 and §19 — the lyric placeholder and the fourth-wall cursor are the same system (ADR-083). Text,
timing, position, style. `seq::OverlayCue` already describes a timed overlay and
`seq::CompositionLayerSink` already turns one into a layer, so this may be better expressed as part
of item 5 than as its own domain. Decide that before writing it.

### 7. `entity.spawn` · `set_profile` · `attach_field`

§10 (population), §17 (world reactivity) and §18 (proximity influence). The engine side exists and
none of it is reachable from a prompt: ADR-088 entities and behaviours, ADR-096 actions and
schedules, ADR-097 music-influence fields.

§18 in particular is *already built* — a field is a position, a radius, a falloff and a strength,
and it scales the depth of reactions an entity already has. The prompt asks for it as a prototype;
the engine has the finished thing and no way to ask for it.

### 8. `render.capture`

So the agent can verify instead of assert. Everything above is worth less without it: the prompt's
§26 and §30 ask the assistant to confirm its own work, and it currently has no eyes. The control
plane's capability registry already declares `vision: false` per provider, so the honest first
version is a frame probe the *agent* reads as numbers — draw counts, triangle counts, whether a
named node is on screen — rather than an image it cannot see.

## Three things no amount of tooling reaches

A plan that treats these as tool work will slip. They are engine features that do not exist.

- **Interiors are not a concept.** Buildings have no interior, entrance or room model, so §8's
  apartment and §21's bathroom have nothing to be built out of. This is Group D of the cinematic
  world brief (`docs/cinematic-world-gap-analysis.md`).
- **A city is not a scatter world.** See item 4. Road graph and lane layout are unbuilt.
- **Vehicles and traffic do not exist** at all (§23), for the same reason.

Retargeting (§9) is the happy exception: it does not exist, and it is not needed, because the
imported Quaternius characters and the 43-clip Universal Animation Library share one 66-joint rig,
and Kenney's mini-characters arrive rigged with 64 clips each.

## Practical limits to scope against

- Tools run **serially**. The metadata for parallel reads exists (`readOnly`, `requiresMainThread`)
  and the scheduler does not use it yet.
- There is **no streaming**; `onTextDelta` fires once with the whole text.
- The control plane enforces **iteration, tool-call and time budgets** from Settings. A task the
  size of the full bootstrap prompt would exhaust them long before its final section.

## How to run the prompt

Do not run it whole yet. Its §15 — the audio-reactive foundation — is the one section that works
today, end to end, through the canonical pipeline: `signal.list` → `parameter.search` →
`modulation.create`. Run that as a genuine acceptance test of the plane.

Re-run the whole prompt once items 2–5 land. That is the point at which the answer changes from
*no* to *probably*.
