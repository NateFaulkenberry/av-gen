# The AI control plane's missing verbs

A handoff. **Seven of the eight items are done and on `main`** — the plane went from 35 tools to
51, and from *no verb that creates anything* to one for each of project, world, media, shot,
overlay and field, plus a way for the assistant to check its own framing. **Item 3 is the work.**

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

## What is already done

**Item 1 — `project.list · create · save · save_as · open`.** The round trip the whole brief rests
on: a project that only exists in the process that made it is not a project.

**Item 2 — `asset.list_importable · asset.import_audio`.** Copies media into the open project and
makes it the session's track. Copied rather than referenced, because a project pointing at a file on
somebody's desktop stops working the day it moves. A file that will not decode has its copy removed
again rather than left for a save to record.

**Item 4 — `world.list_recipes · world.generate`.** Wraps the path `--generate` and the Generate
World button already take. `composeFromRecipeFile` was *extracted* from `Application` rather than
copied, so "which library does this recipe mean" keeps one answer. It reports **layers, not
instances**: composing produces the rule for each scatter and the instances only exist once terrain
is built from it.

**Items 5 and 6 — `sequence.add_marker · add_shot · add_overlay`.** The bake turned out not to be
the obstacle the first draft of this document expected: `Engine::installSequence()` is idempotent —
every track and layer the previous install owned is replaced, never stacked — so each edit can
re-install and there is no separate "commit" for an assistant to forget. A test pins that property,
because if it regressed every assistant edit would double the timeline. Overlays are how §5's lyric
placeholder and §19's cursor both get made, so item 6 folded into item 5 exactly as this document
guessed it might.

**Item 7 — `entity.list · field.list · field.create`.** §18 asks for a "proximity influence
prototype" and the engine has the finished thing; this is the ask. `field.list` reports what each
field *resolved to*, which is how an author learns whether it is scrub-exact (ADR-091) without
rendering the same frame twice.

**Item 8 — `render.probe`.** The assistant has no eyes: every provider declares `vision: false`, so
an image would be a file it cannot read. This is the frame as *numbers* — which nodes fall inside
the frustum, where each sits, how much of the height it fills — by projecting each node's bounds
through the camera. It uses all eight corners of the box rather than the centre, because a building
whose centre is behind the camera can still fill the frame, and a centre test would call it
invisible. Pure arithmetic, so it needs no renderer and works headless. A camera framing nothing
says so instead of looking fine.

All in `src/ai/engine_tools.cpp`. **50 tools across 18 domains.**

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

Tests: `tests/unit/test_ai_tools.cpp`, tags `[project]`, `[import]`, `[world]`, `[authoring]`,
`[field]`, `[probe]`. Full suite **1538, green**.

## The one that remains

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

**What I found looking at it, so the next person does not have to.** `ui::EditHistory` has no
compound-group API — no `beginGroup`/`endGroup`. It has `push(EditCommand)`, `undo`, `redo` and
`undoSize()`, plus `beginDrag`/`commitDrag` for coalescing a drag. A sink can be written over that:
record `undoSize()` at `begin`, and on `abort` undo until it is back to that mark.

The harder half is not the sink. **The AI tools do not push `EditCommand`s at all** — `parameter.set`
writes the parameter directly, and the snapshot sink is what makes that undoable. So a session with
the editor sink installed would have node creation on the undo stack and parameter edits in a
snapshot, two mechanisms for one transaction. Decide which of these before writing code:

1. Route every mutating tool through `EditHistory` and drop the snapshot sink when an editor exists.
   Cleanest end state; touches every existing tool.
2. Give the editor sink both: an undo mark *and* a snapshot, aborting both. Smaller change, two
   things to keep in step.
3. Let node CRUD refuse unless the editor sink is installed (`TransactionSink::kind()` already
   reports what is backing it), and leave parameter tools on snapshots. Smallest, and honest, but
   node creation then does not work headless.

## 8. `render.capture`

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

Much more of it will now run than when this document was written. What an assistant can do today,
in the prompt's own order: create and save a project (§1), import the track and analyse it (§2),
add markers for the story beats (§20), add shots (§13), put a lyric placeholder on the frame (§5),
generate a world (§6 in part), wire audio reactivity (§15), and put a music influence field in it
(§18). Then save, close, reopen, and check it all survived (§26).

What it still cannot do is **make an object**: no building, no bed, no stage, no protagonist. That
is item 3, and it is why the city sections remain out of reach.

So run it, but read §30 knowing which boxes cannot tick yet. The honest next acceptance test is the
prompt with §7–§12 struck out — everything else in it now has a verb.
