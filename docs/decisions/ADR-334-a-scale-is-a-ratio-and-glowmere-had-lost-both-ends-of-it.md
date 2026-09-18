# ADR-334 — A scale is a ratio, and Glowmere had lost both ends of it

**Status:** Accepted
**Date:** 2026-09-18
**Follows:** ADR-213 (the 3.6x), ADR-218 / ADR-262 / ADR-271 (the beam and what it contains),
ADR-182 (a probe that cannot fail), ADR-204 / ADR-226 (a stride speed is a claim about a clip)

## Context

> "I think in v2 we ended up having to make models etc much larger to fit the scale of the world."

and, offered two coherent worlds:

> "glowmere scale - flora comes down"

ADR-213 records how the world got here, and every step in the chain was locally reasonable. The four
aliens were scaled up to sit beside a world authored large. Then the farm animals arrived at their
true metre sizes, a 1.57 m cow stood next to a 6 m alien, and the report was *"all the new animals we
added in glowmere need the same upscaling as we did to the 4 aliens, they are looking small compared
to everything"*. So the animals were scaled to the aliens, which had been scaled to the world. Each
link was measured against the previous link and none of them was measured against the ground.

## What nothing in the suite could have caught

A scale is never a number on its own. "The cow is at 1.0" passes just as happily at 3.6, provided
whoever changed the scene also changed the number the test expected -- and `test_farm_locomotion.cpp`
did exactly that, correctly, three times: it asserts `gait.walkSpeed == clipStrideSpeed x nodeScale`,
which is true at 3.6 and true at 1.0 and says nothing about whether either is right.

**An absolute size cannot fail in the way that matters.** What a viewer sees is the cow against the
mushroom, the alien against the plant, the mushroom against the tree.

## The measurement

`tests/unit/test_glowmere_scale.cpp` and its `[.probe][glowmere-scale]` ladder print every body,
every hero organism's height and gill line, and every scatter layer's largest instance, in one
sorted column of metres. On the world as it was:

```
   21.000  pines (scatter, max)            3.29 bodies
   16.000  elder-2 (hero fungus)           2.51 bodies
    9.000  bloom (hero fungus)             1.41 bodies
    6.500  lantern (hero fungus)           1.02 bodies
    6.376  bull-1 (body)                   1.00 bodies      <-- a bull
    6.200  scree (hero fungus)             0.97 bodies
    5.999  rook (body)                     0.94 bodies      <-- an alien
    5.120  fan-plants (scatter, max)       0.80 bodies
```

A bull was the size of the fourth and fifth largest of the ten signature organisms. The aliens were
taller than five of them. And `fan-plants` -- the tallest thing the terrain scatters that is *not* a
tree, and the one that is everywhere, which a tree is not -- came up to four fifths of a cow: the
whole top half of the cast was walking over the undergrowth rather than through it. That is why the
establishing shot read as a diorama, and it is visible in any frame with ground in it rather than
only in a frame that happens to contain a hero organism.

## The target relationship

Three ratios, each a `CHECK` rather than a sentence, and each with a control that must fail:

1. **The undergrowth is undergrowth.** No body in the cast is taller than the largest instance the
   `fan-plants` layer will place.
2. **Every hero fungus is a canopy the cast walks under.** Each organism's gill line -- the underside
   of the cap, read off the vertices by `mushroomAnchors`, and about 0.74 of its height for all ten
   -- clears the tallest body by a quarter of that body again. A canopy you have to duck under is a
   canopy you cannot be filmed standing under. Seven of the ten failed this; four failed it against
   the aliens.
3. **The signature organism is monumental and stands above the tree line.** At least four of the
   cast's tallest body, and a fifth again above the tallest instance any tree layer will place.

## The third arm was written wrong, and the renders said so

It first read *"between four and eight bodies -- above eight the organism stops being something a
figure has a relationship with and becomes terrain"*. That was an honest guess and it was wrong, and
replacing it rather than widening it is the point of writing this section.

Three renders of the same 72 seconds of the shipped multicam project, same build, same camera set,
only the scene data differing:

| arm | what changed | what the frames showed |
|---|---|---|
| A | nothing (bc79a51) | a 6.38 m bull standing over 5.12 m plants; from the 268 m Valley Wide the animals read as the size of the hillside trees |
| B | cast to 1.0 | every ratio to the undergrowth lands. And the elder, framed by the director at its own 49.6 m, is a modest mushroom in front of a forest taller than it is |
| C | trees to 8 / 8 / 6.5 m | the same frame, the same subject, and the mushroom holds it |

The clearest single pair is t=42 s, where the director's subject is the **veil**, the smallest of the
ten heroes, at 3.4 m. In arm B the tree beside it fills the right third of the frame and the shot's
own subject is a third its height. In arm C the tree is a tree and the mushroom is the subject.

So 8.92 alien-heights does not read as terrain. What a 16 m elder reads as, in a world whose whole
subject is mushrooms, is **a mushroom shorter than the trees**. `pines` places instances to 21 m.
The failure was never in the ratio to the cast: Glowmere's signature organisms were not the tallest
things in Glowmere.

`kSignatureMax` is deleted and `kAboveTreeLine = 1.2` takes its place. The arm keeps both controls
and they now fail on different halves -- the old world low, at 2.5 bodies; the cast-comes-down-and-
nothing-else world on the tree line, 16 m against 21.

## Decision

**The cast returns to its authored size, the tree layers come down to meet it, and the ten hero
fungi are not touched.**

### The cast: 1.0

Sixteen farm animals from 3.6 and five named aliens from 3.344-3.61 to **1.0** -- the metres the
GLBs occupy at rest, which is a number the assets were made with rather than one anybody tuned.

A node scale is never alone. A farm animal's `gait.walkSpeed` is its clip's measured stride speed
times the node scale (ADR-204, ADR-226), so every metres-per-second and metres-per-second-squared
beside it moves with the body: `walkSpeed`, `runSpeed`, `runEnter`, `runExit`, `moveEnter`,
`moveExit`, `accel`, `decel`, and the wander behaviour's `speed` and `runSpeed`. The bull's
`walkSpeed` lands on 1.6689 against the 1.6688 m/s `test_farm_locomotion.cpp` measured off the clip
at scale 1 -- that is the check, not a coincidence.

Territories do **not** scale. `homeRadius`, `minRange` and `maxRange` are metres of world and the
world has not changed; a 1.0x animal with an 18 m home range simply has more of it.

### The trees: about 0.54x

| layer | before | after | tallest instance | why |
|---|---:|---:|---:|---|
| pines | 15.0 | **8.0** | 11.2 m | `maxScale` 1.4 is what binds the tree line |
| canopy | 14.0 | **8.0** | 10.8 m | |
| deadwood | 12.0 | **6.5** | 8.45 m | a dead tree is broken-topped and sits lower |

The tree line is therefore 11.2 m and the elder, unchanged at 16 m, is **1.43x** it. No project
carries a scatter parameter, so the scene file is the whole of the edit.

### The fungi: unchanged, and that is a finding

At t=42 the veil at 3.4 m is already modest against `boulders` the terrain places to 3.5 m. Bringing
the foot of that ladder down -- which any uniform multiplier does -- takes the smallest signature
organisms out of the signature and into the scatter. The ladder was produced by a search and its
ordering is the art; nothing in three renders asked for it to move.

### What the beam had to be told

`liftHeight` is a distance from the beam's **mouth** to the **centre of the body it lifts**
(ADR-262: `to: actor.beam`, `anchor: drawn`, `place: drawn`), so it is a claim about the cast and
scales with it: **-6.6 → -1.83**. At -6.6 a 1.77 m bull would hang nearly six metres below the
emitter disc; at -1.83 its back sits 0.94 m under the mouth, which is where a 6.38 m bull's back
sat relative to its own size.

The beam's own radius is untouched. The owner set it to 0.42 m by hand through the panel (ADR-271),
and that is an authoring decision about a world with a native-scale cast in it -- their number was
right before the world it belonged to arrived.

`navBodyRadius` 2.4 → 0.67 m and `navWadeDepth` 1.6 → 0.44 m for the same reason: 1.6 m of wade is
shin-deep on a 5.67 m cow and drowns a 1.57 m one.

### Camera framing, which had to move and did

A hero's `radius`, `height` and `preferredCameraDistance` are how far the camera stands off
(`camera_director.cpp:190`), so the five aliens that were starred as heroes in the editor carry
their stand-off down with them: 14.0 m to 3.9 m for `rook`. That is the difference between filming a
6 m creature in a landscape and filming a 1.7 m one as a character, and it is the intended half of
this change rather than a side effect.

The **baked** `cameraShotSpans` carry the subject's radius from the day the director baked them, so
they were re-sized in place -- the fifteen creature spans in the multicam project, with all
forty-one then checked against the hero table. The timings are musical
and are untouched.

The two **fixed** cameras are not re-aimed. `Valley Wide` stands 268 m off and `UFO Watch` follows
the saucer at 44.6 m, and the saucer has not changed size. The consequence is real and is reported
rather than fixed: **a native-scale cast is not legible from either.** The animals that dotted the
establishing shot are gone from it. The cast now belongs to the shots the director composes for it.

## Consequences

- `examples/world/glowmere-valley-2{,-multicam,-song}.scene.json`,
  `glowmere-atmospherics.scene.json`, and the four projects beside them. Fingerprints refreshed.
- `tools/make_glowmere_valley_2.py` is **not** changed: the hero heights it generates are the ones
  the scenes still carry.
- `tests/unit/test_glowmere_scale.cpp`.
- `glowmere-stylized` is deliberately untouched. It is not this world: one `wanderer` at 0.08x of a
  different alien, its own procedural architecture, and no farm.

## Rejected alternatives

- **Bringing the hero fungi down.** Rendered as arm B and read against arm C. A uniform multiplier
  takes the ladder's foot into the scatter; a compression that holds the foot moves the top by an
  amount no frame asked for. The elder was never what made the cast look small.
- **Leaving the flora alone and only shrinking the cast.** Rendered as arm B; it is the control the
  third arm keeps.
- **Re-widening the tractor beam.** The owner narrowed it for the world this change delivers.
- **Re-aiming the fixed cameras so the cast stays legible in them.** That is a shot decision and it
  belongs to whoever owns the shots, not to a scale pass.

## Revisit triggers

- **A second Glowmere world with a different cast**, or a hero organism authored by hand rather than
  by the generator: the ten-quantities-from-one-number derivation is the generator's.
- **Anyone wanting the cast legible in the establishing shot.** The answer is the camera, not the
  cast, and this record exists so that the next person does not re-multiply the animals.
