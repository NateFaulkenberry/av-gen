# ADR-260: The beam is a column, not an axis; the animal is not its origin; and the project was overriding the scene

**Status:** Accepted
**Date:** 2026-09-17

> the animal being abducted does not line up with the visible tractor beam

The third report of the same thing, after two rounds that measured, changed a file, reported a
number and left the render exactly as it was. ADR-218 is the first of those and it is careful,
correct arithmetic about the wrong quantities. This one is about the quantities.

Four defects. Two of them are why the picture is wrong. One of them is why the previous two fixes
never reached the owner at all, and it is the most important thing in this document.

---

## 0. The fix that was never in the render

A project's `parameters` block is applied **over** the values its scene registers
(`params::loadDocument`). `examples/world/glowmere-valley-2.json` and its two siblings were
carrying

```
"staging/abduction/liftHeight":  -3.4      (the scene said -7.5)
"staging/abduction/animalWobble": 0.4      (the scene said  0.0)
```

940232a changed those two numbers in all four **scene** files and touched none of the three
**projects**. The owner opens the project. `--render` is pointed at the project. So the edit was
real, the file was changed, the test that loaded the scene went green, and what rendered was
byte-for-byte the behaviour that had just been reported fixed.

No measurement of the geometry could have found this, because the geometry was being measured in
the one place the override does not reach. It is now a test — `Every Glowmere project agrees with
its scene about the abduction`, pure JSON, milliseconds — and
`tools/make_abduction_scenario.py` re-syncs the projects whenever it writes the scenes, so the two
cannot drift again by editing one.

Running that generator also found that it had itself gone stale in the other direction: the
`glow-rise` / `glow-fade` cue and its three parameters had been added to the scenes by hand and
never to the script, so re-running it would have silently deleted the animal's glow. A generator
that is not the source of truth is a generator that destroys work.

---

## 1. The beam is a column with a bottom, and the lift started below it

Every diagnostic this repository has ever pointed at the abduction — ADR-218's included, and the
first version of this one's — treated the beam as its **axis**: a vertical line, of infinite
length, and the only question asked was how far the animal was from it sideways.

The beam that is *drawn* is a column of particles with a bottom. Its reach is
`speed x lifetime`, integrated with its own gravity and drag: for the shipped emitter,
**30.1 m**.

And the saucer was holding station **34.0 m** up.

Not 23. `hoverHeight` is 23 and `cruiseClearance` is 34, and `clearance` is a *floor* applied to
every `MoveTo` and `Follow` — so on the two station-holding steps the floor won, every time, and
the parameter named "Preferred Height" had no effect on anything. The scenario had pasted the
cruise clearance onto all three steps; it was named for, and belongs to, the one that crosses two
hundred metres of forest.

Eleven metres higher than the shot asked for is eleven metres the column has to cover and does not.
Measured across the lab's 23 cases, **23 of 23** had the column stopping short of the animal. In the
render the beam is a stub hanging under the saucer with the cow standing on the ground twenty-odd
metres below it, untouched by anything. That is what "does not line up with the visible beam" looks
like, it is vertical, and no horizontal fix could ever have touched it.

`clearance` comes off `hover` and `hold`. A candidate with more than `targetClearance` (6.5 m) of
canopy over it is not a candidate, so a station at `hoverHeight` over one that *is* a candidate is
clear by construction.

### And the top of the column, while we are here

`liftHeight` was -7.5 **from the saucer's origin**, and the emitter disc is 2.05 m under that. The
tallest animal in the farm is a bull at 1.770 model units, which ADR-213's 3.6x makes 6.37 m — so
the bull's back arrived 0.9 m *above the emitter*, out of the top of the column, inside the hull.
**9 of 23** lab cases did this.

`liftHeight` is now measured from the **mouth of the beam** (the step's `to` is the beam) and is
-6.6, sized from the cast the way ADR-218 sized the beam's width from it. The test asserts it
against the measured cast rather than against the number, so re-scaling the farm fails a test
instead of going quiet.

---

## 2. A point standing in for a picture, twice

The horizontal half is smaller and it is the one everybody was looking at. It is two instances of
one mistake.

**The beam's axis is not the craft's origin.** The emitter is authored at node-local
`(0, -2.05, 0)` and the saucer is tilted — 3.5 deg / -5 deg as authored, plus `hover`'s 1.7 deg tilt
and `bank`'s 5 deg. Rotating a 2.05 m offset by that moves it sideways by up to **0.55 m**, measured.
The lift was aimed at the craft, and nothing in `stage::` could have found the axis: it is the beam
*node's* own transform, and the actor's `parts` list carried the beam's entity name for `show` /
`hide` / `set` and never its position.

**The animal's origin is not the animal.** A farm GLB's mesh is not centred on its own origin: a
cow's bind-pose box centre sits 0.223 model units behind it, which at 3.6x is **0.89 m** of world.
That vector turns with the body — and the step spins the body at 230 deg/s, so the drawn animal was
**orbiting the column on a 1.8 m circle** while the origin everybody measured sat still on the axis.

Measured in the lab, before: the worst **body-centre** offset was **1.199 m**, and the worst
**origin** offset *on the very same frames* was **0.411 m**. In the shipped film the same pair reads
**0.996 m** and **0.293 m**. The old probe's number was three times smaller than the thing the owner
was looking at. That is the whole of "the test passed and the
render did not change", and it is ADR-182's rule with a new corollary: *a probe measuring the wrong
quantity cannot fail in the way that matters.*

### The fix, and where it belongs

Not an offset added to the tractor beam. Two new answers, at the boundary where the question is
actually answerable:

* **`scene::Composition::visualPlacement(node)`** (`stage::IVisualPlacement`) — "where is this
  node's contribution to the picture centred, and where is the node itself?", read out of the
  **flattened scene**. For a particle node that is the emitter's world point; for a mesh node the
  centre of the box those meshes occupy; for anything else the node's own origin. It carries the
  parent chain, the parameter finals and whatever the asset does inside its own node, because it is
  read from the thing the renderer drew rather than re-derived beside it.
* **`stage::Anchor::Drawn`**, and its mirror **`StepDesc::place`**. `anchor` was already "which
  point of the destination"; `place` is the half that had no way of being asked — "which point *of
  the body being moved* is put there". A director writes an entity's position and an entity drives
  its node's **origin**, so every `moveTo` before this placed the origin.

The abduction's lift becomes `to: actor.beam`, `anchor: drawn`, `place: drawn`. Three fields of
JSON, no C++ that knows what a UFO is, and the correction is the asset's own model-space centre
rotated by the body's own facing — not a constant, not per-species, and different for every one of
the four yaws in the lab, which is the point.

---

## 3. What it was worth, measured

`tools/make_tractor_beam_lab.py` builds a flat plane, one saucer, the production beam and the
production scenario (imported from `make_abduction_scenario`, not retyped) with 23 animals laid out
so that each thing that could be wrong has its own row.
`examples/world/tractor-beam-lab-legacy.scene.json` is the same lab carrying the scenario **as it
was authored before this ADR** — four
fields of JSON — so before and after differ in the configuration and in nothing else, and both
numbers below come from the same build and the same measurement code.

Body-centre offset from the column's axis, over the settled part of each lift:

| | before | after |
|---|---|---|
| worst, whole cast | **1.199 m** | **0.105 m** |
| worst at the top of the lift, where the animal hangs | 1.199 m | **0.065 m** |
| four identical cows at 0/90/180/270 deg, spread between them | 0.178 m | 0.040 m |
| lifts whose column stopped short of the animal | 23 of 23 | 0 of 23 |
| lifts putting the body above the emitter disc | 9 of 23 | 0 of 23 |
| worst *origin* offset, same frames (what the old probe measured) | 0.411 m | — |

And the shipped film, ten abductions over 190 s, measured with the same code: the worst body-centre
offset at the top of a lift goes from **0.996 m to 0.086 m**, and every one of the ten improves.

The residual is understood rather than merely small. The director decides before the entity pass
writes the finals the next flattening reads, so the body-centre correction is one frame stale *in
rotation*, and the step spins the animal at 230 deg/s. One frame of that at 60 Hz is 3.8 deg of a
0.89 m radius, or **0.059 m** — and the measured worst-at-the-top across the four rotation cases is
0.058 to 0.065 m. The prediction and the measurement agree to a millimetre.

### The measurement window, which is itself a lesson

The `abduct` beat outlives its own `lift` step: the animal is retired and released while the saucer's
`hold` runs on. On those tail frames `clearDirectorMotion` has dropped the director's accumulated
spin — up to 330 degrees of it — so the body snaps back to its authored facing while its origin stays
where the correction put it, and the measured "offset" jumps to whatever the whole lift's rotation
subtends. The first version of this ADR's tables read 0.53 m instead of 0.065 for exactly that
reason, and chasing it cost an hour.

It is not a defect: `retire` hides the body in the same instant, so nothing is on screen to snap. But
it is a frame the lift does not own, and the probe now measures only while
`Entity::directorMotion().active` — while the director is actually driving the body. Stating the
window is part of stating the measurement.

The rotation row is the arm that matters most: four identical cows, aimed identically, differing
only in facing. Whatever a constant error is, it is the same for all four — so that number cannot
be made to pass by tuning an offset, which is the failure mode this ADR exists to end.

---

## 4. Two things deliberately not changed

**`ParticleSystem::direction` is not transformed by the node's world matrix.** `applyParameters`
puts `position` and `attractorPosition` through it and `extent` through its scale, and leaves
`direction` alone — so a beam authored `(0, -1, 0)` fires *world*-down however the saucer banks. As
a general rule that is a coordinate-space inconsistency inside one JSON block. As a fact about this
scene it is load-bearing: a column that tilted with the craft's 10 deg of authored tilt and bank
would miss the ground by 4 m, and the whole design assumes a vertical axis. It is left alone, named
here, and pinned by the lab's tests, which would fail if somebody "fixed" it.

**Sockets.** `Entity::socketTransform()` is the repository's authoritative semantic-position call
and it resolves through `ISkeletonQuery` — which nothing implements, and `setSkeleton` is never
called, so every socket silently returns the entity's own frame and a consumer cannot tell a real
answer from the fallback. Anchoring the beam on a socket would have been anchoring it on a
diagnostic that cannot report that it has no answer -- which is the same shape as everything else in
this document. The abduction point is a property of the *mesh box*, not of a joint, and that is
where it is read from. Recorded as a revisit trigger rather than fixed here: making `socketTransform`
mean what it says is the character layer's work, not the director's.

## Consequences

- `src/stage/staging.{hpp,cpp}`: `Anchor::Drawn`, `StepDesc::place`, `IVisualPlacement`,
  `VisualPlacement`, `StageContext::visuals`, `Staging::pointOn`, `Staging::placementOffset`, and
  `visualCycle` now treats a role's *part* as its body (`actor.beam` is parented to `actor`, and a
  loop through it is the same loop) and counts any non-`Travel` anchor.
- `src/stage/stage_json.cpp`: `"place"`; `"drawn"` / `"rendered"` now name `Drawn` rather than
  being spellings of `Visual` (no shipped scene used either word).
- `src/scene/composition.{hpp,cpp}`: `visualPlacement`, and `NodeRange::world` — the node's world
  transform **as the last flattening used it**. Recorded rather than recomputed because
  `applyParameters` reads the parameter *finals*, finals are reset to bases at the top of every
  frame, and the director runs in exactly that window: asking `nodeWorldTransform` there answers
  *the position the file was authored with*. That cost an afternoon — a wandering cow's placement
  came out 5.3 m wrong, the distance it had walked from its authored spot, while every animal that
  never left its authored spot looked perfect.
- `tools/make_abduction_scenario.py`: importable (`apply`, `main`), writes every scene named on the
  command line, re-syncs each one's project, keeps the file's existing indentation, and carries the
  glow cue and the two parameter values that had been edited into the scenes behind its back.
- `tools/refresh_scene_fingerprint.py`: takes scenes on the command line instead of knowing one.
- `tools/make_tractor_beam_lab.py`, `examples/world/tractor-beam-lab.scene.json`,
  `examples/world/tractor-beam-lab-legacy.scene.json`.
- `tests/unit/test_beam_lab.cpp`: the decomposition probe, the two regression tests, the control
  that fails, and the scene/project agreement test.
- `src/rendering/debug_draw.hpp`, `src/rendering/debug_visualizer.cpp`, `src/ui/world_panel.cpp`:
  a `beams` overlay -- every emitter's disc, its column's axis, and a ring where the column ends.
- `src/app/application.{hpp,cpp}`, `src/app/render_job.{hpp,cpp}`: `--debug-draw <list>`, and the
  overlays reaching an offline render at all. They were behind ImGui checkboxes and `RenderJob`
  built no debug geometry, so the one artefact everybody diagnoses from was the one artefact they
  could not appear in.
- `docs/project-format.md`: `anchor`'s third value and `place`.
- `docs/tractor-beam-lab.md`: how to run the lab, its cast, the coordinate-space audit, the tables.
- All four Glowmere scenes and all three projects re-generated.

## Revisit triggers

- Anything that changes the farm's scale, the emitter's `speedMin`/`lifetimeMin`, or
  `hoverHeight`. All three are asserted against measurements rather than against numbers, so they
  fail rather than go quiet.
- A renderer that transforms `ParticleSystem::direction`. The beam's verticality is assumed by the
  lift and by every test here.
- Anything that implements `ISkeletonQuery`. A real socket answer is a better abduction point than
  a mesh box for a character, and a worse one for a cow; the choice becomes available then.
- The one-frame staleness. If the director ever runs *after* the entity pass, the 0.059 m residual
  goes to zero and the tolerance can come down with it.
