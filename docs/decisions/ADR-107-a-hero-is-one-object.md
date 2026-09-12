# ADR-107: A hero is one object

Status: accepted
Date: 2026-09-12
Supersedes the `assembly` half of [ADR-074](ADR-074-authored-heroes.md).

## Context

ADR-072 gave `HeroPoint` two ways to say what stands at a hero: `assetId`, a library entry the world
composer placed, and `assembly`, "the name of an authored assembly". ADR-074 used the second for
Glowmere's elder — a hero called `elder` standing for the three nodes `elder-crown`, `elder-stem` and
`elder-filaments` — and was explicit that "nothing enforces that relationship yet beyond a test that
reads the shipped file".

Nothing ever did enforce it, and nothing ever used it. Three years of consequences, all of them the
same shape:

- **No row.** ADR-104 put a star on every object in the editor. An assembly hero is not an object, so
  it had no row, could not be unstarred, and the camera kept travelling to a subject the application
  had no way to talk about.
- **No reactions.** Hero reactions are wired to `nodes/<hero name>/<suffix>`. There is no node called
  `elder`, so the elder's `reactionProfile: "organism"` — the whole point of ADR-074 — could never
  have reached anything. It has never once been installed.
- **Nothing to follow.** ADR-106 makes a hero track the object it describes. An assembly has no one
  object, so it tracked nothing.

Every consumer of a hero — the camera director, the obstacle field, the clearance field, the terrain
query — reads `position`, `radius`, `height`, `importance` and `name`. Not one of them has ever read
`assembly`.

## Decision

`HeroPoint::assembly` is removed. A hero is the scene object of the same name.

- **`validate()` requires a name and no longer requires "something to stand here".** A hero declared
  in the editor has no asset id — what stands there is a node — and demanding one was the reason the
  editor had to write a fake `assembly` to get its own heroes accepted.
- **The hero-to-node match is the name, exactly.** `heroNamesNode` was checking three fields; two of
  them could match things that were not the object.
- **`"assembly"` in a scene file is read, warned about and dropped**, so a file written before this
  still opens.
- **Glowmere's heroes are renamed to their objects**: `elder` → `elder-crown`, `monument` →
  `monument-spire`. The other three already named theirs. Positions, sizes, accents and reaction
  profiles are untouched, so the framing of the authored shot does not move — and the elder's
  reaction profile now has somewhere to land for the first time.

A hero may still outlive its object: a file can name something that is not there, and an object can
be renamed or deleted underneath one. That hero keeps working — it has a position and a size, which
is all the director needs — and the World panel's Heroes list is where it can be found and removed.
What is gone is the idea that this was ever a *design* rather than a loose end.

## Rejected alternatives

- **Keeping `assembly` and resolving it by prefix.** Making the convention real would mean the
  composition validating hero assemblies against its node list, a hero's bounds being the union of a
  prefix match, and reactions fanning out to every member. That is a feature; nobody asked for it,
  and grouping already exists as `NodeKind::Group`, which is a real parent with a real transform.
- **Group nodes as heroes.** If a hero should be three objects, make them a group and star the group:
  a group is a node, it has a name, bounds and a transform, and every rule here already applies to it
  unchanged. This is the answer to the need `assembly` was invented for.
- **Keeping the field as deprecated-but-read.** A field that is serialised and never acted on is how
  this started.

## Consequences

- `examples/world/glowmere-stylized.scene.json` gains two renamed heroes and loses five `assembly`
  keys. Nothing else in the file changes.
- Anything holding a hero by the name `elder` or `monument` — a saved focus request, a hand-written
  note — now names nothing. Both were only ever referred to from that one file.
- The elder's `organism` reaction profile can be installed for the first time. Nothing installs
  reactions for authored scenes yet (only `installWorld` does, for generated ones), so this is a door
  opened rather than a behaviour changed.

## Revisit triggers

- A hero that genuinely is several objects and cannot be a group.
