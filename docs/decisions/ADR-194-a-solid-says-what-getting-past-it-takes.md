# ADR-194: A solid says what getting past it takes; the body says what it can do about that

**Status:** Accepted
**Date:** 2026-09-14
**Extends:** ADR-093

## What was missing

`spatial::ObstacleField` had two notions of an obstacle and neither of them was traversal.

`ObstacleType` is **identity** — `Vegetation, Trunk, Rock, Structure, Creature, Custom` — and its own
comment says so: *"the type is not policy ... it is what lets a diagnostic say 'a rock' instead of
'obstacle 4471'."* `NavigationObstacle::blocking` was a bool. Between them they could say "it is a
rock and it is in the way" and nothing else.

The one thing resembling traversal was **a float on the query**: `ObstacleFilter::stepOver`, built by
`Navigator::filter()` from `NavSettings::stepOver`. That is the wrong side of the seam for half of
the question. How tall a solid is, and therefore what kind of effort it takes to get past, is a
property of the solid; how high a particular body can step is a property of the body. Putting both
in the filter meant the field could say how tall a thing was and nothing at all about what it *was*,
so every solid a body could not step over was the same solid. There was no jump class at all, and
A*'s per-cell penalty read `obstruction` and `slope` and nothing else.

And **which scatter layers become obstacles had no data source.** `entity::classifyScatterLayer`
falls through `layer.category`, then keywords in the asset's *filename*, then keywords in the
layer's *name*, and defaults to `Vegetation`; a per-class height threshold then decides. That
heuristic is honest and inspectable and it is still keywords. This project's own Glowmere scene
loads `Plant_7.gltf`, and an author with a tree in a file called that had no way to say "this one
blocks" — or, the other way, to say that a monument is scenery a character walks through.

## The seam

**Identity stays where it is. Traversal is split across the two files that already own the two
halves of the question.**

* **`spatial::Traversal { Passable, StepOver, Jumpable, Blocking }`** is a field on
  `NavigationObstacle`. It is what getting past *this solid* takes, and it is true of the solid
  whoever is asking. It replaces `blocking`, which is now a method: `Passable` is the old
  `blocking = false`.
* **`ObstacleFilter::jumpOver`** is the body's half: metres of solid this body can clear. It joins
  `bodyRadius`, `stepOver`, `footY` and `headHeight`, which were already the body's half. **It
  defaults to 0 — a body that cannot jump** — and that is what makes the whole addition inert for
  everything that existed before it.
* **`entity::traversalClass(type, height, policy)`** derives the class, and it lives in
  `entity/obstacles.hpp` because that file is already the world's policy: it is the one that knows
  a boulder from a bush. `spatial` stores the answer and honours it and invents nothing.

That is the same division ADR-093 drew — *"the field stores facts; the filter is policy, and it
belongs to the thing doing the walking"* — carried one step further, because "how hard is this to
get past" turned out to be a fact and "can I do that" turned out to be the policy.

The ladder is one function, `traversalOf`, and the order of its tests is the physical one:

1. a thing low enough to stand on is **stepped on**, whatever it is made of;
2. a thing whose foot is above the body's head is **ducked under**, whatever it is made of;
3. only then does the obstacle's own class get a say, and only to decide whether a jump is an option.

So a nominal `StepOver` still blocks a cart whose wheel clears five centimetres. **The class is what
the solid is, not a promise to every body that asks.** `relevant` — the "is this in my way" every
query reduces to — is that same function read as a bool, which is why it takes the class as an
argument rather than off the obstacle: `includeNonBlocking` has to be able to ask "and what would
this take if it *were* solid".

`ObstacleField::traversalAt` is the query surface for the distinction: the hardest thing the disc
overlaps, as a `Traversal`, with the obstacle that produced the answer. Ties go to the deepest
overlap and then to the lowest index, so the grid never decides it.

Only one thing about *identity* changes the class: a `Creature` is never `Jumpable`, because a solid
that walks away mid-vault is not one to commit to.

## The declaration

`world::ScatterLayer::navigation` is `auto | blocks | passable`, read from the scene JSON and
written back only when it is not `auto`, so a scene that never heard of the key round-trips
byte-identically. A typo is an **error**, not a silent default: an author asking for something the
world then quietly does not do is the failure this repository keeps rediscovering in its own policy
fields.

It is an override, not a replacement. `auto` leaves the heuristic and the height thresholds exactly
in charge. `passable` contributes nothing however tall the layer grew. `blocks` contributes every
instance however short, classed `Blocking` — the author overruled the heuristic once and it does not
get a second say — **but it still does not make a ten-centimetre kerb into a wall**, because step 1
of the ladder above is a body's own legs and a scene file does not get to legislate those. That
consequence is deliberate and it is the one place `blocks` means less than it sounds like.

`navigation` is deliberately **absent from `ScatterLayer::structuralHash`**, for the reason `motion`
is and one more: that hash is xored into the layer's `scatterHash`, so putting it in would
re-randomise every instance's variation in the whole world the first time an author marked one layer
`blocks`. It is not staleness — `Composition::rebuild` builds the obstacle field from the (reused)
point clouds every time, so the placements survive the edit and the solids are recomputed from them.

## A* pays for a vault

A jumpable obstacle is not free to cross, and the old penalty would have priced it at zero: a body
that could jump would open cells the obstruction total had closed, and then walk through them at the
cost of open ground — a character hurdling a field of boulders to save two metres.

`NavCell` gained a `vault` byte, in the byte that was `pad`, next to `obstruction`. The grid build
asks `traversalFor` per overlapping solid and sends only the ones **this body actually clears by
jumping** to `vault`; everything else, *including the ones it merely steps over, which this sum has
always counted*, stays in `obstruction`. `NavPathCost::vaultPenalty` is 1.5, half of
`obstructionPenalty`: the cell is passable, so it is cheaper than a wall, and it is not open ground,
so it is not free.

**Half is a starting point and not a measurement**, and it cannot be one yet: nothing in this
project can jump, so `vault` is zero in every cell of every world it ships and the term contributes
exactly nothing. That is also the proof that no existing path moved.

## Measured

Glowmere, `examples/world/glowmere-stylized.scene.json`, before and after, identical in both:

| layer | obstacles | type |
|---|---:|---|
| canopy | 721 | trunk |
| pines | 395 | trunk |
| deadwood | 113 | trunk |
| fan-plants | 608 | vegetation |
| boulders | 513 | rock |
| **total** | **2355** (2355 blocking) | indexed at 29.9 m cells |

The count was the weaker control. The stronger one is the **nav grid**, which is built from the
obstacle field and is where a changed traversal class would show: `154x154 at 4.0 m (23716 cells,
21743 walkable, 218 water, 838 blocked), 24 regions (largest 21694 cells), 172 shore and 67 vista
points` — byte-identical before and after. Glowmere records 513 rocks and a good many of them stand
between 0.9 m (`rockMinHeight`) and 1.2 m (`jumpOverHeight`), so they are now born `Jumpable`; the
grid says, in a number, that nothing in this world can do anything about that.

## Rejected

**Deriving the class in `spatial`.** It would need the height thresholds, which are world policy —
a miniature and a giant disagree about a metre — and `spatial` is the file that includes glm and
nothing else so that anything in the project may hold one.

**Keeping `blocking` alongside `traversal`.** Two fields that can disagree about the same thing, and
whichever one a given query happened to read would be the bug. `blocking()` is a method over the
class.

**Letting `blocks` bypass the step-over test.** It would make the scene file the authority on a
body's legs, which is the seam this record exists to draw, and it would mean one field could no
longer serve a deer and a cart.

**Recording a `passable` layer as `Passable` obstacles** so `includeNonBlocking` queries could find
them. It is a real use — "walk to the glowing plant" — and it costs a hundred thousand cylinders on
a layer whose declaration was "this is not navigation's business". The use wants its own key.

**A `jumpOver` scene key.** A body's abilities belong with `bodyRadius` and `headroom` on the walk
behaviour, where a character already declares its proportions. Putting one there adds a parameter to
every project in the repository, which is a change that wants its own proof rather than a free ride
on this one. `NavSettings::jumpOver` is settable through `Navigator::setSettings`, which the
behaviour layer already calls.

## Revisit triggers

* Anything in this project gains a jump. `vaultPenalty` then has content to be measured against, and
  `NavCell::vault` stops being zero.
* A second author needs `passable` layers to remain queryable as non-solids.
* `traversalClass` grows a third identity exception. Two is a table; three means identity is
  deciding traversal after all and the split needs rereading.
