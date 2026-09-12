# ADR-104: A hero is declared by starring the object that is one

Status: accepted
Date: 2026-09-12

## Context

ADR-072 built `world::HeroPoint` and ADR-074 made a scene able to declare heroes in a top-level
`"heroes"` block. Both are working: Glowmere's five heroes are authored, they round-trip, and the
camera director travels between them.

Neither made a hero *declarable from the application*. The only producers were the world composer
and a text editor. The visible edge of that was the CLI message `--direct found no heroes to shoot:
declare some in the scene's "heroes" block, or generate a world` — an instruction whose only
possible next step was to close the app and open the scene file — and the Camera menu's "Direct to
Music", which was enabled whenever audio was loaded and then failed on click for the same reason.

The world editor already has the shape of the answer. Its Objects list gives every node an eye and a
padlock, the two toggles a layer list is read by, and both are one click on the row of the thing
they act on.

## Decision

### A third toggle on the row: a star

Hero designation joins visibility and locking as a per-object toggle in the World window's Objects
list, drawn as a star for the same reason the other two are drawn rather than written: the UI font is
ASCII-only, and a column of the letters V, L and H is not a thing anyone scans. Filled is a hero;
outlined is not, so the shape is present on every row and only the fill changes.

### What the hero is, is measured from the object

Designating fills a `HeroPoint` from the flattened scene rather than from a form:

- `position` is the centre of the box the object occupies, and `radius`/`height` are that box —
  half its horizontal extent, its full height — which is what those fields are defined as.
- `yaw` is the node's world rotation about Y.
- `preferredCameraDistance` is `3r + 1.5h`. Against the one set of authored heroes in the
  repository this lands within a few metres of the distances a person chose by eye (elder 49 vs 50,
  far-arch 95 vs 88); a fixed default would be wrong by an order of magnitude between a two-metre
  artefact and a forty-metre tree, which is exactly the case ADR-072 says the field exists for.
- `activationRadius` is three times that, the ratio Glowmere's authored heroes use, which satisfies
  `validate()`'s insistence that a hero activate before the camera reaches its mark.
- `importance` is **not** derived. It is a judgement about the piece rather than about the
  geometry — the biggest object in a world is routinely not its subject — so it takes the type's
  default and ties are broken by declaration order.

### The link between a hero and its nodes stays a name match

A node is a hero when any declared hero's `name`, `assembly` or `assetId` equals the node's name.
ADR-074 left this relationship as a naming convention the loader does not check, precisely because an
assembly may name something a later installer creates; this reads that convention rather than
replacing it. Designating writes both `name` and `assembly` as the node's name, so a hero made this
way is found by all three readings. Glowmere's authored heroes light up on `monument-spire`,
`far-arch`, `beacon-grove` and `visitor` without any change to the file; `elder` names an assembly of
three nodes and has no single node to sit on, which is correct and is the case that stops this from
becoming a flag on a node.

### Writing heroes sorts them by importance

`briefFromHeroes` takes the first hero as the subject and documents that heroes "arrive already
ranked by importance". Nothing enforced it for an authored list. Every write of the hero list from
the editor now stable-sorts it descending, so the contract holds by construction and equal-importance
heroes keep the order they were declared in. Glowmere's block is already descending, so this is a
no-op on the one file it could have disturbed.

### Undo carries the whole hero, not a flag

`EditCommand` gains a `HeroChange` record holding the complete `HeroPoint` either side. Undesignating
an authored hero and undoing it gives back its importance, its accent colour and its reaction
profile, rather than a fresh measurement of the node. A toggle that silently discarded somebody's
tuning would be a toggle that cannot be taken back — which is the whole premise of ADR-092's command
model.

### Both dead ends now name the button

"Direct to Music" is disabled when the world has no heroes, and says so: *nothing to travel to: star
an object in World > Objects to make it a hero, or generate a world*. The CLI message says the same.

## Rejected alternatives

- **A `hero` flag on `CompositionNode`.** ADR-074 already rejected this and the elder is still the
  reason: three nodes and a practical light, no one of which is the hero. A flag would also have to
  carry importance, stand-off and reaction profile, which is a `HeroPoint` with a different name.
- **A hero editor dialog.** Every field of a `HeroPoint` in a form is the honest maximum, and it is
  also why nobody would declare one. The toggle is the thing that was missing; the fields remain
  editable in the scene file, where the people who tune them already are.
- **Deriving `importance` from size.** Tempting, and wrong often enough to be worse than a tie: it
  would make the terrain the subject of any world whose terrain is a node.
- **Marking the composition dirty on designation.** ADR-074 is explicit that declaring a hero cannot
  change a frame. `setHeroes` still does not set `dirty_`, and nothing here rebuilds the scene.

## Consequences

- A hero designated this way is saved with the **scene**, not the project, because that is where
  ADR-074 put the block. Starring an object in a project that references a scene file requires
  saving the scene to keep it.
- `Composition::setHeroes` validates the whole set, so designation is applied through `applyEdit`
  like any other command and a set it rejects leaves nothing on the history — the toggle visibly
  does not take, rather than half-taking.
- The assembly-to-node relationship is still unchecked. This makes it *readable* from the editor,
  which is a step towards the check ADR-074 describes as a reasonable next decision.

## Revisit triggers

- A world whose heroes need distinct importances more often than not: that is the point at which
  designation needs a rank, and a drag-to-reorder hero list is the shape it should take.
- An assembly installer that creates nodes after designation, which would make the name match stale
  and force the composition to own the relationship.
