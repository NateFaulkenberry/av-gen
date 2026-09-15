# ADR-218: A body in a beam has two positions and a size, and the scenario knew about neither

**Status:** Accepted
**Date:** 2026-09-15

Two reports about the same five seconds of film:

> make sure the animal being abducted is fully in the visitor beam when it happens - right now its
> misaligned

> there are times where the UFO can move and the particle beam will start dropping from its old
> position briefly before updating to new position, so we need to fix that timing

Three defects, and only one of them is the one that was guessed at.

## What "fully in the beam" is, arithmetically

The beam is a `disc` emitter of radius `extent.x` firing straight down: `applyParameters` transforms
a particle system's `position` by the node's world matrix but **not** its `direction`, so the column
is vertical in world space however the saucer is banked, and its axis is the transformed emitter
point. The spread is 0.042, which `mix(baseDir, randomDir, spread)` turns into at most 2.5° of
divergence — about 1.3 m over the thirty metres of lift — so the *narrowest* part of the column is
the disc itself, at the top, which is exactly where the animal ends up. The emitter radius is
therefore the honest number to measure against and the conservative one.

The animal is a **scaled** node: ADR-213 put the farm at 3.6x, so what matters is its world extent
and not its GLB's. `Composition::nodeCorners` is added for this — the eight corners of every mesh a
node draws, in world space, *un-boxed*. `nodeBounds` folds them into an axis-aligned box, and the box
of a spinning 3.6x cow is up to its own diagonal wider than the cow: measured, a metre of beam.

So: **every world corner of the animal, on every frame of the lift, within the emitter's radius of
the column's axis.**

## Defect 1: the director aims at a place the saucer is not

`entity::DirectorMotion` becomes an entity's `travel`, and the behaviours' offsets — hover, drift,
bank — are folded onto the node's transform *afterwards*, on purpose (ADR-210: "a craft keeps
hovering, drifting and banking while it is being flown somewhere"). So `Entity::state().position()`
and the place the node is **drawn** are two different points, and the tractor beam is parented to the
node. Glowmere's saucer carries `drift` with a radius of 2.4 m.

Measured over 110 s of the shipped scene: the two are up to **0.922 m** apart, and the beam's axis is
a further **0.565 m** from the drawn craft — the emitter sits 2.05 m under the hull, and rotating
that offset by the saucer's 3.5°/-5° authored tilt plus its bank and hover tilt moves it sideways.

`stage::Anchor` is the fix, and it is a step field rather than a rule: `Travel` (the default, and
what every step did before) or `Visual`. The `lift` step asks for `Visual`. `Entity::visualPosition()`
is the sum, named — `EntityWorld::pointOfInterest` has always returned exactly it.

### And the loop that opens the moment you do that

A `follow` that tracks a body which is simultaneously tracking the follower is a loop, and ADR-210
already called the horizontal half of it fine. It *was* fine: what went round it each frame was the
pair's own wobble, a sinusoid whose time integral is bounded by amplitude/(2·π·rate), and the craft's
is 0.3 m at 0.45 Hz — eleven centimetres.

`Anchor::Visual` puts the behaviours' offsets into the loop instead, and the saucer's drift is 2.4 m
at 0.031 Hz: an integral of about **twelve metres**. The same structure, three orders of magnitude
apart, so the loop stops being benign exactly when a step asks for the drawn position.

`StepDesc::hold` is the anchor, the way `aboveGround` is the anchor for ADR-210's vertical version: a
`follow` that resolves its station once, on the first frame, and keeps it. During a lift there is
nothing to follow anyway — the animal is the director's to move. And `Staging::setDesc` now
**refuses** a beat where two roles take their station from each other and either measures from the
drawn position, naming both and saying to hold one. A defect that is refused at load is not a defect
anybody has to remember.

### What the alignment fix was worth, exactly

Six abductions, 110 s, the same measurement code on both arms. How far the lift put the animal's
origin off the beam's axis, per abduction:

| | chick | goat | chicken | goat | pig | cow |
|---|---|---|---|---|---|---|
| before | 1.04 | 1.37 | **1.66** | 1.08 | 1.22 | 1.07 |
| after | 0.76 | 1.02 | 1.31 | 0.94 | 1.07 | **1.42** |

Five of six improved and the worst case came down from **1.66 m to 1.42 m**. The sixth — the cow —
got 0.35 m *worse*, which is worth writing down rather than averaging away: the fix is structural
(the animal now rises up the axis of the column that is actually drawn, instead of one offset by
whatever the behaviours were doing), and structural is not the same as uniformly better. What it
buys over a long piece is that the error stops being a systematic 0.9 m that grows with the drift's
own slow phase.

It is also, plainly, not the big term. Which is the next defect.

## Defect 2: the beam is not as wide as the thing it lifts

With the alignment fixed, five of six abductions in a 110 s run were inside the column and the sixth
— a cow — still reached **6.37 m** out of a **3.60 m** beam. Before the alignment fix the same cow
reached 6.31 m out of the same 3.60 m beam, so the widening is what does this and the alignment is
not.

Because the animals are 3.6x. Across the whole cast, measured as authored — before a frame has run,
so every rotation is yaw and reach from the origin is yaw-invariant:

| | reach |
|---|---|
| chick | 0.26 m |
| chicken | 0.66 m |
| rooster | 0.76 m |
| goat | 2.01 m |
| pig | 2.45 m |
| sheep | 2.89 m |
| horse | 4.91 m |
| cow | 5.29 m |
| **bull** | **5.80 m** |

*As authored* is load-bearing, and it is what makes the table a table: each species reads the same
for both of its instances, which a number sampled mid-run does not. The farm carries
`{"kind": "ground", "slopeAlign": 0.55}`, so an animal standing on a hillside is pitched into it, and
a pitched 6.3 m-tall horse throws its box corners out by metres — sampled mid-run, `horse-2` reads
6.03 m against its authored 4.91 m, and `bull-10` reads **7.22 m** against its authored 5.80 m. That
is a fact about standing on a hill rather than about hanging in a beam: measured, a lifted animal's
reach matches its authored one to within a few centimetres (the cow's lift reads 5.42 m against
5.29 m), because a director's hold is what it is doing instead of standing. Sizing the beam on the
standing number would have meant an 8.6 m radius on a 7.99 m saucer.

A bull is 9.5 m long and the saucer is 16 m across. There is no alignment that puts a 9.5 m body
inside a 7.2 m circle, and this is the whole of why the report says "misaligned" and the fix is
mostly not about alignment: the beam was authored for animals that were 3.6 times smaller, and
ADR-213 changed them without re-measuring it.

`extent` goes to **7.8 m** — the widest reach as authored (5.80 m) plus the residual misalignment the
run measured (1.42 m), which is 7.22 m, with the rest as margin. That is very nearly the saucer's own 7.99 m radius, and the consequence is worth
stating rather than discovering: **the beam is now as wide as the craft.** Given what it is lifting,
a narrower one is the thing that looks wrong.

Coverage is restored twice rather than once, because the disc's area went up 4.7x and a beam that
keeps its spawn rate over 4.7x the volume is a haze: 2.1x the rate (2900 → 6100/s, with the node's
`capacity` grown from 16,384 to 32,768 so the pool can hold 6100/s for a full 5 s lifetime) and 1.49x
the mote (`sizeStart` 0.44 → 0.656). `animalWobble` comes down from 0.85 to 0.40 at the same time:
the sway is deliberate comedy and it is also, directly, beam width.

## Defect 3: hiding a particle system freezes it, it does not clear it

Both candidates offered for the second report were reasonable and one of them is measured to be
wrong.

**A one-frame lag between the director moving the craft and the emitter's world position being
recomputed is not happening.** Each frame, the emitter's horizontal step was compared against the
craft's step *this* frame and the craft's step *last* frame; summed over 110 s, same-frame
disagreement was **28.7** and one-frame-lagged **51.3** (and after the fixes, 5.0 against 13.4). The order in `Engine::update` is
`updateBehaviour` (staging, then the entity pass, which writes the node's position finals) and then
`controller_->update`, whose `applyParameters` reads those finals — so it is already same-frame, and
`nodeWorldTransform` walks the parent chain live rather than caching.

What *is* happening is sharper than "particles persist in world space". `ParticleRenderer::update`
does

```cpp
if (pool.needsReset) { resetPool(pool); }
if (!sys.enabled) { continue; }
```

— so a hidden system is skipped **before** the simulate pass. Its particles do not age and they are
not cleared; they are frozen. The scenario hid the beam 0.3 s after dimming it, while the pool still
held roughly four thousand live particles, and the next `show` was 11 s and up to 205 m later. Every
one of those particles resumed, with its full remaining life, at the last abduction's site. That is
"the beam starts dropping from its old position before updating to the new one", exactly.

Measured, before: at all five shows that followed an abduction, the system had been enabled for
**0.00 s** since it last emitted, having moved **31.9, 204.6, 45.6, 59.9 and 31.9 m** while hidden.

The fix is in the scenario rather than in the renderer, and is better there: a particle ages while
its system is enabled, so *stop emitting, stay enabled long enough for the pool to empty, and only
then hide*. `beamRestRate` goes to 0 (it was 1050 — the beam never stopped emitting at all), a
`beamDrainSeconds` parameter is added at 5.0 s — the emitter's own `lifetimeMax` — and the `hide`
moves from the end of `depart` to the start of the next `approach`, where it runs beside a cue that
takes at least 7.8 s. A beat ends when all of its cues do, so the sequence keeps exactly the timing
it had, and if anybody ever shortens the approach below the drain the beat lengthens rather than the
hide being skipped. After: **7.37 s** of enabled, empty time at every show — comfortably over the emitter's 5.0 s
`lifetimeMax`, so the pool is provably empty before the beam is hidden at all.

A renderer-side reset on the disabled→enabled edge would fix this for every scene rather than for
this one, and is recorded here as the follow-up it is; it was not taken now because
`src/rendering/particle_renderer.cpp` is being edited concurrently and because the scenario-level fix
is correct without it.

## Consequences

- `src/entity/entity.hpp`: `Entity::visualPosition()`.
- `src/stage/staging.{hpp,cpp}`, `src/stage/stage_json.cpp`: `Anchor` (`"anchor": "travel"|"visual"`,
  default `travel`), `StepDesc::hold` (`"hold": true`, default false), and `visualCycle` in
  `setDesc`. Both default to what existed, so every scenario that does not ask is unchanged.
- `src/scene/composition.{hpp,cpp}`: `nodeCorners`.
- `tools/make_abduction_scenario.py` and the scene it writes: the two step fields, three parameter
  changes, one new parameter, the moved `hide`, and the beam node's `extent`, `capacity`,
  `spawnRate`, `sizeStart` and `sizeEnd`. Re-fingerprinted.
- `tests/unit/test_abduction_alignment.cpp`: the two regression tests and the probe that produced
  every number above.

## Revisit triggers

- Anything that changes the farm's scale again. The beam is sized from a measurement of the cast, and
  the measurement is in the test — it will fail rather than go quiet.
- A renderer that clears a pool when a system is re-enabled. `beamDrainSeconds` becomes unnecessary
  then, and the `hide` can go back where it was.
- A second scenario wanting `Anchor::Visual` on both ends of a pair. `setDesc` refuses it today;
  making it safe would mean damping rather than refusing, which is a different decision.
