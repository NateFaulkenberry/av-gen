# The AI control plane's missing verbs

**All eight items are done and on `main`.** The plane went from 35 tools to 54, and from *no verb
that creates anything* to one for each of project, world, media, shot, overlay, field and — the
last and hardest — the objects in the scene itself.

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

## Item 3, and the thing that made it possible

**`scene.create_node · delete_node · set_parent`.** A glTF asset by file, or an empty Group to
parent things to. The asset is resolved against the readable folders, so a model that cannot be
reached is refused here rather than becoming a node that renders nothing.

The obstacle was never the tools. It was that **a rollback could not undo them**: `SnapshotStore`
captured `params::saveProject` — parameters, routes, presets, timeline — and a created node is none
of those. A transaction would have restored a task's numbers and left the object it made standing in
the scene.

The earlier draft of this document offered three ways round that, all of them about reconciling two
undo mechanisms. The answer turned out to be simpler and is worth recording because the three
options were all worse: **make the snapshot cover what the tools change.** `Composition::toJson` and
`seq::Sequence::toJson` already round-trip exactly, and the composition document carries entities and
fields with it, so the snapshot now captures the scene graph and the piece alongside the parameters.
One mechanism, widened, instead of two mechanisms kept in step.

Two details that are easy to get wrong:

- **The scene graph goes back before the parameters are rebound.** Rebuilding a composition destroys
  and recreates every parameter it owns, so binding first and rebuilding second leaves every route
  and track pointing at freed parameters. The parameter half is then applied a second time over the
  restored scene, because otherwise you get the right scene with the wrong numbers in it.
- **Deleting a node has to delete its children.** `Composition::removeNode` takes one node and
  leaves them pointing at a parent that no longer exists, which makes their world transform the
  root's — an object that appears to teleport. The tool walks the subtree and removes it deepest
  first. A test pins the count.

Because the snapshot now reaches them, `sequence.add_*`, `field.create` and `world.generate` were
re-annotated from `mutatesSession` to `mutatesProject` — which was the honest answer all along and
became true rather than being asserted.

`Engine::setCompositionJson` was added for the restore: rolling back a created node means putting
the previous composition back, and going through a temporary file would make a rollback depend on
the disk.

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

Every section of it now has a verb behind it. What remains unreachable is not tooling: the three
things below are engine features that do not exist, and §8's apartment interior, §21's bathroom and
§23's traffic are the sections that depend on them.

Run it whole. The honest expectation is that the city gets *built* — nodes placed, block by block —
and that it does not yet get *laid out*, because a street grid is not something the composer can
scatter and nothing else places one.
