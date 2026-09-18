# ADR-335 — The cast goes back up to the trees, and half the ladder stops being a canopy

**Status:** Accepted
**Date:** 2026-09-18
**Follows:** ADR-334 (the cast to 1.0 and the trees to 8/8/6.5, which this partially reverses),
ADR-213 (the 3.6x), ADR-262 / ADR-271 (the beam and what it contains), ADR-264 (a project is
applied over its scene), ADR-182 (a probe that cannot fail), ADR-204 / ADR-226 (a stride speed is
a claim about a clip)

## Context

ADR-334 shipped at lunchtime. The owner looked at it and said:

> "we need to scale the aliens and farm animals back up to the size they were relative to the
> vegetation, trees, mushrooms, etc. they are too small now that the world scale is corrected."

That sentence has two readings and they differ by a factor of 1.85, because ADR-334 moved two
things and not one. The cast came down 3.6x; the *trees* also came down about 0.54x; the ten hero
fungi and every undergrowth layer did not move at all. So "the size they were relative to the
vegetation" is:

| reading | what "relative to" is measured against | multiplier |
|---|---|---:|
| restore the former ratio to the **tree line** | `pines` 21 m -> 11.2 m | **1.94x** |
| restore the former ratio to the **untouched flora** | `fan-plants`, the ten fungi | 3.6x, a full revert |

The ambiguity was put to the owner with the measured ladder beside it. **They chose 1.94x, "match
the trees."** This record exists so that nobody re-derives the other one.

## The measurement

`tests/unit/test_glowmere_scale.cpp`'s `[.probe][glowmere-scale]` ladder, before and after. The
tallest body in the cast is `ember`, the ranger alien, and everything is quoted against it.

```
                          ADR-334 (1.0x)        this (1.94x)
  tallest body            ember   1.794 m       ember   3.480 m
  elder-2 (signature)     16.000  8.92 bodies   16.000  4.60 bodies
  pines (tree line)       11.200  6.24 bodies   11.200  3.22 bodies
  fan-plants (max)         5.120  2.85 bodies    5.120  1.47 bodies
  veil (smallest hero)     3.400  1.90 bodies    3.400  0.98 bodies
  boulders (max)           3.520  1.96 bodies    3.520  1.01 bodies
  a bull                   1.771  0.99 bodies    3.436  0.99 bodies
  a chicken                0.337  0.19 bodies    0.653  0.19 bodies
```

The three arms ADR-334 wrote allowed, respectively, 2.85x (no body taller than a `fan-plant`),
1.18x (every hero fungus a canopy) and 2.23x (the elder at four bodies and above the tree line).
**1.94x knowingly breaks the second one**, and what to do about that is most of this record.

## Decision

**The sixteen farm animals and the five named aliens go from 1.0 to 1.94 in all four Glowmere
valley scenes, in the scene file and in the project beside it.** Nothing else in the world moves:
the trees stay at 8 / 8 / 6.5, the ten hero fungi stay where the generator put them, and no
undergrowth layer is touched.

### Where each number comes from

A node scale is never alone, and ADR-334 enumerated the quantities that travel with it. Each one
here is derived from **the pre-ADR-334 authored value divided by the scale that world carried,
times 1.94** -- not from the current value times 1.94 -- so the rounding introduced at 1.0 does not
compound. The five aliens are divided by their own old scales (3.61, 3.601, 3.576, 3.344, 3.344),
not by 3.6.

The exception is the farm's `walkSpeed` and `runSpeed`, which are **the clip's own measured stride
speed times 1.94**, because that is the invariant `test_farm_locomotion.cpp` checks and the only
one of these numbers that has an independent measurement to be right against. The bull lands on
3.2375 against 1.6688 m/s x 1.94 = 3.23747.

| quantity | 1.0x | 1.94x | why it is a claim about the cast |
|---|---:|---:|---|
| node scale, 21 bodies | 1.0 | **1.94** | |
| `gait.walkSpeed` / `runSpeed` | | clip stride x 1.94 | ADR-204, ADR-226 |
| `runEnter`, `runExit`, `moveEnter`, `moveExit`, `accel`, `decel` | | x1.94 | m/s and m/s^2 |
| `wander.speed` / `runSpeed`, `explore.speed` / `runSpeed` | | x1.94 | m/s |
| `liftHeight` | -1.83 | **-3.5502** | beam mouth to the centre of the body it lifts (ADR-262) |
| `animalGait` | 0.361 | **0.7003** | `StepDesc::rate` is `DirectorMotion::speed`, m/s |
| `navBodyRadius` | 0.67 | **1.2998** | a body radius |
| `navWadeDepth` | 0.44 | **0.8536** | how deep this cast wades |
| multicam hero table, five aliens | | x1.94 | `height`, `radius`, `preferredCameraDistance`, `activationRadius`: the camera's stand-off (`camera_director.cpp:190`) |
| multicam `cameraShotSpans`, fifteen creature spans | | x1.94 | `subjectRadius` is half the subject's height, baked |

Territories do **not** move. `homeRadius`, `minRange` and `maxRange` are metres of world and the
world has not changed. The beam's own radius is untouched: the owner set it by hand through the
panel (ADR-271) and has since said *"I want a narrow beam."* The widest animal now reaches 3.13 m
from its own origin against a 7.80 m beam, and `test_abduction_alignment.cpp` still passes with
`reach + worstOrigin <= beamRadius` at 4.03 m against 7.80.

### Forty-two speeds per project were still at 3.6x, and had been since ADR-334 merged

The suite found it, which is what a suite is for -- except that it did not, and that is the point.

Every Glowmere project carries `entity/<name>/wander/speed` and `entity/<name>/runSpeed` (and the
`explore` spelling for the five aliens): **forty-two absolute copies per project**, applied *over*
the scene by ADR-264's rule. ADR-334 moved those speeds in the scene and not in the project. So
between that merge and this one, every Glowmere animal has been walking at the speed authored for a
body 3.6 times its size -- `entity/bull-1/wander/speed` was 3.585 m/s on a 1.77 m bull -- in every
render anybody made.

Arm 4 of `test_glowmere_scale.cpp` is the arm whose entire job is "a project does not undo its
scene", and it compared `nodes/*/scale`, the procedural source scales and the hero table. It had
never been told about the speeds. It has now, with a `speedsChecked >= 8` guard so it cannot go
vacuous on a project that stops carrying them.

## Arm 2 was rewritten, not widened

ADR-334's second arm: *every* hero fungus is a canopy the cast walks under -- its gill line, read
off the vertices, clears the tallest body by a quarter of that body again. At 1.94x that is false
for five of the ten: `umbra` (gills at 3.921 m), `ridge` (3.829), `spire` (3.075), `ember` (3.074)
and `veil` (2.639), against a canopy line of 4.350 m.

`kCanopyHeadroom` is **still 1.25** and it is still the definition of a canopy. Lowering it to
squeeze the veil under would be tuning a constant until a screenshot passed. What was wrong was the
sentence, and ADR-334 has a section -- "The third arm was written wrong, and the renders said so"
-- that does exactly this to its own third arm. This is the same move.

**The cast stands inside its own ladder.** The ten hero organisms were produced by a search and
their ordering is the art (ADR-334 says so, in refusing to compress them). A ladder every rung of
which is over your head is a ceiling. The elder reads as monumental because a veil in the same
world is at the cast's own height; take that away and 16 m is just a number.

|  | canopies | grounded |
|---|---:|---:|
| 3.6x (ADR-213) | 1 | 9 |
| 1.0x (ADR-334) | 10 | 0 |
| **1.94x (this)** | **5** | **5** |

So the arm asserts `canopies >= 3` **and** `grounded >= 3`: a band, not a floor (ADR-182), and the
two halves fail on the two different worlds. Both controls are kept and both are real worlds this
repository shipped -- the second one for an afternoon, and the owner's own sentence is why it is a
control rather than a hypothesis.

Demonstrated by mutating the scene rather than argued:

```
  cast at 1.0:  CHECK( grounded >= kGroundedMin )  with expansion: 0 >= 3
  cast at 3.6:  CHECK( canopies >= kCanopiesMin )  with expansion: 1 >= 3
```

Holding the flora still, the two bounds pin the cast to **[1.749x, 2.449x]**: below 1.749 `umbra`
becomes a canopy and there are eight of them; above 2.449 `cairn` stops being one and there are
two. 1.94 sits near the middle of a window 1.4x wide, so this is not an arm sized to the answer.

Arms 1 and 3 needed nothing. The tallest body is 3.480 m against a 5.12 m `fan-plant`, and the
elder is 4.60 bodies and 1.43x the tree line. Arm 3's own headroom is what bounds this change from
above: at 2.23x the elder drops below four bodies.

## What the renders show, including where they are worse

Nine frames, three arms, same build, same camera set, only the scene data differing. Absolute
paths under `renders/castscale/` (gitignored); the arms are `at100` (ADR-334), `at194` (this) and
`bodiesonly` (a diagnostic).

**t = 42.0 s, the clearest pair.** The director's subject is `cairn-cap`, a hero fungus, so the
camera solution is *identical* in both arms and the two frames are directly comparable. At 1.0 the
two cast members in frame -- a cow and an alien, standing in the `fan-plants` band across the
middle distance -- are three- and four-pixel specks. At 1.94 the cow reads as a cow and the alien
reads as a standing figure, *in* the undergrowth rather than lost in it. **This is the owner's
request, granted, and it is visible.**

**t = 3.0 s, the 268 m `Valley Wide` establishing shot.** Softened, not fixed. A handful of pale
pixels appear where there were none; the cast is still not legible from there. ADR-334's consequence
stands and its answer is still the camera, not the cast.

**t = 79.8 s, the `veil` shot.** The two frames are **byte-identical**. ADR-334 named t=42 / the
veil as its clearest pair, and that was a claim about the mushroom against the trees: the veil's
shot contains no cast member at all, so it cannot say anything about this change.

**t = 17.0 / 34.0 / 48.5 s, the three shots whose subject *is* an alien. All three recompose and
all three are worse.** At 17.0 the frame becomes a blown-out wall of glow pool. At 48.5 the hero
mushroom, the saucer and the aurora that composed the 1.0 frame are out of shot. At 34.0 the alien
that was a legible white figure among the ferns is not findable.

This was worth diagnosing rather than reporting as a mood, so a third arm was rendered with the
cast at 1.94 and the multicam hero table and fifteen baked spans left at **ADR-334's** values. It
renders the 1.94 frame, not the 1.0 one (98.9% of pixels differ from the 1.0 frame, 53.9% from the
full 1.94 frame, and to the eye it is the same shot). **So the hero stand-off table is not the
lever**, and reverting it would not recover those compositions. What moved them is the bodies.

That is reported rather than fixed. Re-framing the shots the director composes for a creature is a
shot decision and it belongs to whoever owns the shots -- the same line ADR-334 drew about the fixed
cameras.

## Consequences

- `examples/world/glowmere-valley-2{,-multicam,-song}.scene.json`,
  `glowmere-atmospherics.scene.json` and the four projects beside them. Fingerprints refreshed.
- `tests/unit/test_glowmere_scale.cpp`: arm 2 replaced, arm 4 widened. 876 assertions, was 801.
- `tests/unit/test_farm_locomotion.cpp`, `test_abduction_poc.cpp` and `test_abduction_alignment.cpp`
  needed **no change**. ADR-334's commits `c7d115b` and `d8ac4a2` had already replaced every
  absolute metre in them with a quantity read off the cast, and those read correctly at 1.94 --
  which is the return on having done it that way rather than editing three constants.
- `tools/make_abduction_scenario.py` still emits `liftHeight -6.6` and `animalGait 1.30`, the 3.6x
  defaults. ADR-334 did not update it either. It is a generator for new scenarios rather than a
  regenerator of these, so re-running it has never been part of this pass, but it will hand the
  next scenario a 3.6x beam.
- `glowmere-stylized` is deliberately untouched, for the reasons ADR-334 gives.

### `glowmere-atmospherics` was edited, not regenerated, and that is a finding

`tools/make_glowmere_atmospherics.py` derives that scene and project from `glowmere-valley-2`, and
the standing rule is to regenerate rather than hand-edit. **Regenerating it now would be wrong**,
and the measurement is why:

- The project the generator would write copies `glowmere-valley-2.json`'s whole `parameters`
  table. That table has since grown **891 keys** the atmospherics project does not have, almost all
  of them `atmos/*`. They would be applied over the demonstration's own `atmosphericEffects` --
  the one thing the demonstration exists to show.
- The scene has drifted too, in twelve `slopeAlign` values and the order of the `heroes` list.

So it was edited textually, exactly as ADR-334 edited it. The drift is real and predates both
changes; closing it is its own piece of work and needs the demonstration re-watched, not a
scale pass.

## Rejected alternatives

- **3.6x, the full revert.** The other reading of the owner's sentence, and they were asked and
  chose against it. It would put a bull back over the `fan-plants` (arm 1) and the elder back to
  2.48 bodies (arm 3) -- the whole of what ADR-334 measured as wrong.
- **Lowering `kCanopyHeadroom` so all ten stay canopies.** 1.18x would do it. That is a constant
  widened until the world passed, and it would assert something the frames do not support: you
  cannot film a figure standing under a 2.639 m gill line when the figure is 3.48 m tall.
- **Bringing the hero fungi down to meet the cast again.** Rejected by ADR-334 on three renders and
  nothing here reopens it. It is also the thing that would destroy the new arm 2: it moves the foot
  of the ladder out of the cast's reach.
- **Re-tuning the beam.** The owner authored 0.42 m by hand and wants it narrow. The cast fits.
- **Reverting the hero stand-off table to recover the creature shots.** Rendered as the
  `bodiesonly` arm and it does not recover them.

## Revisit triggers

- **The owner looking at the three creature-subject shots.** They are worse and they are the half
  of this change that a viewer of the 253-second cut will actually see. The lever is the shot, not
  the scale, and the evidence that the stand-off table is not the lever is above.
- **Anyone moving the tree layers again.** 1.94 is a ratio to `pines` at 11.2 m. Move the tree line
  and this number is stale by exactly the amount you moved it.
- **Anyone moving the hero fungi.** Arm 2 is now a claim about where the cast sits in that ladder,
  so compressing the ladder falsifies it for a reason that has nothing to do with the cast.
