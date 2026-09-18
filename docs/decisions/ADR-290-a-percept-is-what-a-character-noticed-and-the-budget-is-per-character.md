# ADR-290: A percept is what a character noticed, the cadence is for the replay, and the occlusion budget is per character

**Status:** Accepted
**Date:** 2026-09-18

ADR-270 decided the shape of the sense stage and left it unbuilt: `Percept`, `PerceptionSettings`
and `IPerception` were declared in `src/entity/character_ai.hpp` §2, they compiled, and nothing
included them. This is the implementation, and it is also the record of the three places where
building it contradicted the document that specified it. ADR-270's central finding — that
`world::heroSightline` is four orders of magnitude too expensive for a crowd — was re-measured here
and **holds exactly**. Three smaller things did not.

---

## 1. What was built

`src/entity/perception.{hpp,cpp}`. Two grid-backed scans a sense tick:

* a `spatial::PointGrid` over `EntityWorld::interestPoints()`, rebuilt with the list, **minus the
  entries of kind `Character`** — those are bodies, their positions in that list are wherever the
  host last recorded the landmark, and a percept built from one would report a walking character at
  where it stood when the scene loaded;
* a second `PointGrid` over every entity's **simulation** position, rebuilt each step beside the
  crowd field and for the same reason: a body that senses against a half-updated world notices
  things in an order that depends on how the entities happen to be stored.

Grid point index *is* entity index in the second, so `Percept::source` needs no translation. A
`scanIndex` spanning both scans — bodies first, then interest points — is what breaks a tie in
salience, the way `NavGrid`'s A* breaks one on cell index, so the answer cannot depend on the order
the grid visited its cells.

R1 (ADR-260) throughout: `state().position()`, never `visualPosition()`.
`tests/unit/test_entity_perception.cpp` proves it against a hovering body whose two positions are
0.257 m apart, because the lab fixture's four explorers carry no `MotionOffset` at all and an
assertion there would have passed whichever position was read — which is the same lie as an arm that
cannot fail (ADR-182).

`PerceptionSettings` is registered under `entity/<name>/perception/` — `range`, `fieldOfView`,
`proximityRange`, `capacity`, `hertz`, `occlusionTestsPerSecond`, and five `weight/<kind>` — and read
back on every tick (ADR-225). The opt-in is the scene file's `perception` key and nothing else: a
world with no perceiving body builds no grid and enters no loop.

---

## 2. The cadence is a tick index, not an accumulator

`senseTick(time, hertz, seed)` is a pure function of the instant. An accumulator drifts with the
frame rate, so a replayed sense tick would land on a different instant from the played one and a
character's working set would depend on how the frames happened to fall. A pure tick index is what
lets `EntityWorld::seek` **reconstruct** a working set rather than persist one, which is ADR-267's
D4 satisfied by construction rather than by care.

The seed contributes a **phase in [0, 1) ticks**, so a hundred bodies at 4 Hz do not all re-sense on
the same frame. It moves when a body senses and never how often, and it is visible in the numbers: a
30 s replay of 24 bodies at 4 Hz fires **2,903** sense ticks and not 2,904, because one body's phase
pushes it past the boundary of tick 0.

---

## 3. Salience deliberately excludes visibility

`salience = (weight / maxWeight) * (1 - distance / range)`. Taste times nearness, and nothing else.

Folding in `visibility` is the obvious thing to do and it is wrong. Only a round-robin subset of a
character's percepts is ever occlusion-tested; a salience that included visibility would make the
ranking — and therefore the capacity cut, and therefore *what the character knows* — depend on which
percepts happened to win the budget this tick, which depends on how many percepts it had, which
depends on how crowded the world is. That is exactly the failure ADR-270's "never drop the percept"
rule exists to prevent, arriving as `tested` flapping instead of as percepts vanishing.

So perception **reports** what was seen and how well; weighing it is the decider's job. The
normalisation by `maxWeight` rather than a clamp is the other half: clamping a weight above 1 makes
every nearby thing report exactly 1 and destroys the ordering the capacity cut depends on.

---

## 4. The occlusion budget is per character, and ADR-270 says both things

ADR-270 §3 says "the budget is per-world, served round-robin". `PerceptionSettings`'s own comment
says `occlusionTestsPerSecond` is "how many tests per second the whole world may spend on **it**" —
per character. Those are different designs and only one of them can be built.

**Per character, round-robin over that character's own percepts.** A per-world cap on top of it
would make one character's `tested` flag depend on how many other characters were competing for the
same pool — a body that tested its neighbours in an empty scene and stopped testing them in a crowd,
for no reason the scene could express. That is the same class of bug as the dropped percept, and
ADR-270 wrote the rule against it one paragraph earlier. The world's total is N × the knob, and the
knob is where an author controls it.

The schedule is a function of the tick index rather than an accumulator:

```
allowed(n) = floor(n * occlusionTestsPerSecond / tickRate)   tests in ticks [0, n)
tests this tick = allowed(tick + 1) - allowed(lastSensedTick + 1)
cursor          = allowed(lastSensedTick + 1) mod perceptCount
```

so over T seconds a body performs exactly `floor(T * occlusionTestsPerSecond)` tests — not "about
that on average" — and the cursor resumes where the last tick stopped, so every percept is tested in
turn rather than the first one forever. Measured: 24 bodies at 1 test a second for 6 s perform
**exactly 144**. The control at `occlusionTestsPerSecond = 0` performs **0**, and reports
`tested == false` on every percept of every body on every one of 360 frames.

### `lastSensedTick + 1`, and the version of this that was wrong

The first implementation priced `tick` against `tick + 1` and carried no state at all, which was
neater and was wrong: **sense ticks are not consecutive.** They skip whenever a frame is long enough
to cross two of them — a 60 Hz cadence at a 30 Hz frame rate advances the index by two — and they
skip by a whole frame of microseconds when `hertz` is zero, which is how a body says "sense every
step". In both cases the tick and the tick after it are never both sensed, so the difference between
them was bought and never spent. Measured before the fix: a crowd at `hertz = 0` and two tests a
second performed **zero** tests in three seconds where it was owed 120; at a 60 Hz cadence and a
30 Hz frame it would have spent about half.

The fix is one number, and it is one the cadence already had to keep: `Entity::lastSenseTick()`.
`EntityWorld::perceiveOne` now writes it *after* the call rather than before, so the implementation
can see the tick it is being priced against. It is state, and it is state D4 permits: `reset()`
clears it and a replay re-fires the same ticks, exactly as it does for the cadence itself.

`hertz` is capped at 60 in its parameter registration. Sensing more often than the simulation steps
is not something a cadence can mean, so the knob cannot outrun the ticks; only the frame rate can.

---

## 5. Where this corrects ADR-270

### 5.1 The 1.7 ms sightline is confirmed, and it is a property of the world function

Re-measured independently, minima of 5, load average 6.6–7.9:

```
world::heroSightline, glowmere-valley-2, 20 m      1800.6 us     (ADR-270: 1720.779)
world::heroSightline, glowmere-valley-2, 60 m      5876.0 us     (ADR-270: 5391.917)
world::heroSightline, the lab's flat fixture          8.0 us
```

ADR-270's budget argument is vindicated on the shipped world and does **not** generalise: the cost
is dominated by the per-sample world lookup along the march, so a flat fixture with no ecology is two
hundred times cheaper for the same nine rays. **A fixture cannot prove the budget is necessary.**
Anyone tuning `occlusionTestsPerSecond` is tuning it for one world.

### 5.2 The cadence *is* a budget — for the replay, which is where the latency is

ADR-270 §4 retracts the cost argument for the cadence: an isolated `spatial::PointGrid` query is
0.098 µs, so 4 Hz "is not a budget". For a **played frame** that is right — 24 bodies sensing every
frame is about 20 µs, which is nothing.

What it does not price is a **whole** sense tick, and what it does not price at all is the replay.
Measured, `EntityWorld::seek(30 s)` over 24 perceiving bodies (1,800 steps × 24 = 43,200 body-steps),
minima of 5, load average 6.6–7.9:

```
no `perception` key at all                          16.6 - 17.0 ms
declared, 60 Hz, range 0 (tick fires, scan returns)  18.0 - 18.3 ms
declared, 4 Hz, range 40 m                           20.6 - 20.8 ms
declared, 60 Hz, range 40 m                          54.5 - 54.8 ms
```

So the body-grid build and the tick bookkeeping cost about **0.025 µs a body-step**, and one
complete sense tick — two grid queries, the distance and facing filter over ~40 candidates, the
stable sort and the copy-out — costs about **0.85 µs**, nine times the isolated grid query ADR-270
priced. On a played frame that is still nothing. On a scrub it is multiplied by the number of fixed
steps in the window: a 90 s replay of 100 perceiving bodies is 540,000 body-steps, which is **459 ms
a scrub click at 60 Hz and 31 ms at 4 Hz.** ADR-273 is the ADR about where an editor's latency lives,
and it lives there.

**The cadence's two better reasons — a character that re-senses every frame reads as a machine, and
`Percept::seenAt` is meaningless if it is always now — are the right reasons and they are not the
only ones.** For the live frame ADR-270 §4 is correct. For the replay it is wrong by a factor of
fifteen.

### 5.3 `SeekBudget` cannot see any of this

ADR-273 replaced `maxSeconds` with a price cap in **body-steps**, because a replay costs steps ×
bodies. Perception does not change how many steps a replay takes; it changes what a step costs — by
up to 2.2× at 60 Hz on the numbers above. So `SeekBudget::maxBodySteps` grants a world with senses
exactly the same allowance it grants a world without them, and the ceiling it was meant to be is
2.2× higher than it thinks. Nothing is broken today, because the live editor's budget is generous
and the shipped scenes declare no senses; it is recorded here because the unit is wrong in a way
that only shows up once perception is authored on a cast, which is the next unit's problem.

---

## 6. Two mechanical changes this forced

**`character_ai.hpp` included `entity/entity.hpp`.** It now includes `entity/action.hpp` and leaves
`EntityWorld` to the forward declaration `entity/behavior.hpp` already carries — nothing in it needs
`EntityWorld` complete. `entity.hpp` has to be able to include *it*, because `EntityDesc` carries a
`PerceptionSettings`. The header is meant to change by ADR rather than by edit; this is the ADR, and
the change is an include line. Nothing it says changed.

**`InterestKind` and `InterestPoint` moved from `entity.hpp` to `behavior.hpp`**, for the same
reason: `Percept` carries an `InterestKind`. Nothing about them changed, and every name still
resolves through `entity.hpp`, which includes `behavior.hpp`.

---

## 7. What this does not do

**Nothing reads a percept.** `Entity::percepts()` is published and consumed by nothing, which is the
same shape as the four `LocomotionState` fields ADR-266 complains about — with the difference that
the consumer is the next unit on the critical path rather than a hypothetical one. Until P3 lands,
the sense stage changes no character's route by a millimetre, and the Character Intelligence Lab's
own route measurements (62.9 m / 72.4 m / 50.8 m, play-vs-seek 0.025 m at 30 s) are what hold that
claim rather than an assurance in a commit message.

**A character still cannot notice a specific tree**, for ADR-270 §5's reason: the canopy is
statistical and `spatial::ObstacleField`'s 1,238 solids carry no identity beyond `ObstacleType`.

**A percept's eye is a stand-in.** `EntityState` carries a radius and no height, so the sightline is
cast from `position + 0.9 × max(radius × 2, 1)` — the same derivation the crowd field already uses
for a body's height. ADR-274 made a real joint reachable and `socketTransform` now distinguishes a
posed joint from the entity frame; the day a character declares an `eye` socket, there is one line
to change.

**Percepts do not accumulate.** Each tick rebuilds the working set from scratch; a percept that
leaves the range is gone rather than remembered as stale. That is D4 satisfied the cheap way, and it
is the thing to revisit first if a decider needs a character to keep looking for something it lost
sight of.
