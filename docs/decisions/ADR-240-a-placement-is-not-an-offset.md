# ADR-240: A placement is not an offset, a euler triple is not an orientation, a push is not a walk, and a stride speed is still a claim about a clip

**Status:** Accepted
**Date:** 2026-09-15
**Follows:** ADR-161 (rate matching), ADR-162 (the walk back onto the navigable set), ADR-204 (a
clip starts where its keys start; a facing is not an odometer), ADR-213 (an animal with one clip),
ADR-226 (the second scene with the same stride defect)

## The report

> Animals are currently sometimes **moving backward while playing a forward-walking animation**, or
> **moving sideways while displaying a forward-walking animation**. [...] Eliminate visible foot
> sliding wherever possible. [...] Aliens can still exhibit sliding/locomotion mismatches in some
> situations.

Five defects, in four subsystems, none of which the previous four passes over this exact area could
have found. Three of them are one sentence apart from work ADR-204 did and signed off, and the reason
they survived it is the same reason every time: **the four aliens ADR-204 measured are placed at a
node rotation of zero, and every farm animal is not.**

---

## 1. A euler triple is a representation, and the engine was handed the wrong one of two

A node's rotation is authored as euler degrees, stored on the node as a quaternion, and recovered as
euler degrees when its parameter is registered (`registerNodeParameters` → `eulerDegrees`). The
recovery is the textbook inverse of `glm::quat(vec3)`, which builds `Rz·Ry·Rx`, and it is correct:
it reproduces the quaternion exactly.

It is correct and it is the wrong one of the two answers. A ZYX decomposition always has exactly two
solutions — `(x, y, z)` and `(x + 180, 180 − y, z + 180)` — and the textbook form returns the one
whose middle angle lies in [−90, 90]. For a node yawed more than a quarter turn, that is the
**flipped** one:

| authored | recovered | same orientation? |
|---|---|---|
| `[0, 39.03, 0]` | `[0, 39.03, 0]` | — |
| `[0, 140.97, 0]` | `[180, 39.03, −180]` | yes, exactly |

Numerically identical, and catastrophic, because **everything downstream reads the triple by
position**. `applyOffsets` adds a body's steering to component 1 and `GroundFollower` adds a slope's
lean to components 0 and 2. In the flipped branch component 1 is `180 − yaw`, so:

> a body steering **+d** degrees is drawn turning **−d**, and the gap between where it walks and
> where it faces opens at **twice** the rate it turns.

After a quarter turn the body is drawn walking sideways; after a half turn, backwards; and every
angle in between on the way. That is the report, in both of the forms it was filed in, from one
mechanism.

Nine of Glowmere Valley 2's eighteen farm animals carry a scatter placement past 90 degrees, from
`goat-5` at −15.02 (fine) to `chick-18` at −165.46 (a near-total reversal). `vane`, the fifth
animated alien, carries 248. The four aliens ADR-204 measured carry **none at all**, which is the
whole of why its `worstYawGap < wobble` assertion passed on a build where this was live.

**The decision.** `eulerDegrees` chooses between the two solutions rather than inheriting one:
whichever is nearer to upright, measured as `|x| + |z|`. Both reproduce the quaternion exactly, so
this is a choice of representation and not an approximation — and the one it now picks is the one an
author would have written, which also means a scene that is saved and reloaded round-trips its
rotations instead of coming back as `[180, 39.03, −180]`.

The exception is gimbal lock. At a yaw of exactly ±90 the pitch and roll axes coincide and no
decomposition can separate them; `(-30, 90, 25)` and `(-55, 90, 0)` are the same orientation and both
are correct. The test states that rather than asserting past it: the triple is checked away from the
poles, and the *orientation* is checked everywhere including them.

## 2. A placement facing was added for ever instead of started from

An entity's **position** has always been absolute. `NodeBinding::anchor` is where the author put the
body and `EntityState::travel` is displacement from there, so `position()` is `anchor + travel` and
the placement places it exactly once.

Its **facing** was purely additive. `state_.yaw` started at zero and `applyOffsets` wrote
`rotation.y = authored + yaw`. So a body placed at a heading — which is what a scattered animal *is*
— walked along `yaw` and was drawn along `placement + yaw`, permanently that many degrees off.

Two fields that disagree about whether a transform component is absolute or relative is exactly the
class of bug ADR-092 named, and it survived here because nothing in the repository placed a
*steering* body at a non-zero rotation until eighteen farm animals arrived by scatter.

**The decision.** `NodeBinding` carries `facing`, the node's authored world heading, read off the
composed orientation the same way `anchor` is read off the composed position. `state_.yaw` starts
there, and `applyOffsets` writes the **difference**:

```cpp
rotation.y = motion.rotation.y + (state_.yaw - facing_) * kDegrees;
```

At t = 0 that adds nothing and the body is drawn exactly where the scene file put it, which is the
half of the contract a fix that simply ignored the authored rotation would have broken. Pitch and
roll stay additive, because those really are offsets on top of whatever tilt an author authored.

## 3. ADR-162's escape walked sideways, and `wander` never had it at all

ADR-162 gave `explore` a walk back onto the navigable set: from off the walkable set `pathClear`
samples navigability *from the body's own position outward*, so every ray fails whichever way it
points and the steering fan finds nothing however wide it reaches. The fix walks the body toward the
nearest point the navigator calls navigable.

Two things were wrong with it, and one thing was missing.

**It translated the body without turning it** — and wrote `speed` and `Activity::Walk` while doing
so, telling the animation layer the body was walking. That is the one branch in the whole locomotion
path where the facing did not follow the body, and ADR-204's own decomposition mistook its steps for
pushes out of solids, which are a different mechanism making a different guarantee. It now turns onto
the way out at the behaviour's turn rate and travels along its own heading, so every metre it covers
is a metre along the way it is drawn facing.

**It re-chose the refuge every frame.** The outward spiral returns the first navigable candidate at
the smallest radius that has one, and which candidate that is flips as the body moves. That was
harmless while the escape slid the body along a vector; with travel gated on facing the way it is
going, a body that re-asked every frame turned toward a new answer every frame and covered no ground.
Measured: one sheep fell from 63.6 m of travel in ninety seconds to 24.3 m, with 745 frames of a walk
gait at a speed the rate matcher cannot represent. The refuge is now remembered until it is reached,
stops being navigable, or the body is back on the set.

**`wander` never had the escape.** ADR-162 fixed the behaviour the report was filed against and
nothing carried it across. Measured on the shipped scene, with the distance ladder off so nothing was
culled: **chick-9 stood off the navigable set for 22.28 seconds** of a ninety-second run while every
other animal's worst motionless stretch was one authored pause of three to six seconds.

The diagnosis is the part worth recording, because the report that preceded it had already refuted
the two obvious explanations — the territory is too small (raising `homeRadius` improved the worst
stretch and did not remove it) and the animal has nowhere legal to go (**all 18 animals scored
200/200 destinations** from their own anchors). Both were sound and neither was the answer, because
`pickDestination` and `steer` ask different questions: *is that spot walkable* is not *can I set off
towards it*. The probe that settled it asks the second one, and it separates the pack cleanly:

| | navigable at the stall | destinations found | directions the steering fan could take |
|---|---|---:|---:|
| every other animal | yes | 200/200 | 200/200 |
| **chick-9** | **no** | 200/200 | **0/200** |

**And the escape is gated on both conditions.** A body can be off the navigable set and still walk
perfectly well, because `pathClear` starts sampling a quarter of a metre *ahead* of the body rather
than under it. Firing on the predicate alone hijacks a body that was travelling fine — that is the
sheep above. The escape runs only where ADR-162 put it: in a give-up branch, when the body is off the
set **and** the behaviour has nothing else to offer.

## 4. The farm animals' stride speeds were still a guess, and ADR-213 said so

> "The animals' authored stride speeds were never measured against their clips. They were set from
> plausible real-animal speeds, and this ADR scaled those numbers by 3.6 rather than replacing them
> with measurements [...] That is the next honest step, and it is not this one." — ADR-213

`Gait::footSlip` cannot make the check, for the reason ADR-204 §3 established once and for all: with
rate matching on and unsaturated it returns `speed / (authored × (speed / authored))` = 1.0 whatever
the clip contains, because the authored number is both the expected value and the thing under test.

So ADR-226's estimator — the median backward speed of a toe while that toe is in the bottom fifth of
its own height range — was pointed at the farm pack, with the expected values produced by a
pure-stdlib Python script that walks the glTF node hierarchy by hand and shares no line with this
engine.

| | authored (ADR-213) | the clip says | error |
|---|---:|---:|---:|
| bull | 2.70 | **6.008** | −55% |
| cow | 2.52 | **5.590** | −55% |
| horse | 4.86 | **5.860** | −17% |
| sheep | 3.06 | **2.482** | +23% |
| pig | 2.34 | **2.845** | −18% |
| goat | 3.60 | **2.099** | +72% |
| rooster | 2.52 | **1.790** | +41% |
| chicken | 2.34 | **2.035** | +15% |
| chick | 1.98 | **0.644** | **+207%** |

Metres per second at the 3.6× scale the scene places them. With rate matching on and inside the
clamp, the true foot slip is exactly `authored / measured` **whatever speed the body travels at** —
so the bull's feet were covering 2.2 m of stride per metre of ground and the chick's 0.33, constantly,
and every number the engine printed said 1.0.

**`runSpeed` is now the same number as `walkSpeed`.** These nine ship exactly one clip and `clips`
maps `run` onto it, so there is no second clip for a second stride speed to be a claim about; a
`runSpeed` that differed would have scaled the walk cycle's playback by the ratio between them the
moment the gait ever said Run. `runEnter` is put above each animal's cruise so the walk clip the
scene names is the clip that plays (ADR-226's rule), and `rateMin` is derived as
`moveExit / walkSpeed` rather than chosen.

**The cruise speeds are re-derived** as a seeded fraction (0.40–0.65) of each animal's own measured
stride speed. That keeps the recent behaviour pass — short pauses, tight territories, no two animals
synchronised — and makes the playback rate an amble rather than an arbitrary ratio between two
numbers nobody compared.

### What the estimator cannot promise on this pack, stated rather than asserted away

The alien pack's estimate is stable to half a percent from a 10% contact threshold to a 30% one. This
pack's is not. These are five-to-fourteen-key Bezier cycles sampled onto 25–28 frames, and the
planted foot is **not held at a constant speed through its stance** — the bull's rear hoof passes
through the bottom of its arc at 5.1 units/s and plants at 0.35. Across the threshold sweep the
estimate moves by up to **1.70×** (the bull; most are inside 1.15×).

A second, independent estimator — the mean speed over each whole stance rather than the median
instant — was written and checked against the alien pack, where it agrees with ADR-226's to within
5%. On the farm pack the two agree to within 25% and mostly within 10%. So the number carries a real
uncertainty and the test says so. It is still the right number: the errors it replaces ran from
0.33× to 2.2×.

## 5. A push that was allowed to be the whole step

ADR-204 classified rather than counted, and the classification was right: a body shoved out of a rock
moves without its facing following, and that is the guarantee those mechanisms exist to make rather
than an animation fault. What it did not do is ask **how big the push was allowed to be**.

`EntityWorld::crowdSeparation` is deliberately soft — half the overlap, so "bodies yield to each
other rather than bouncing" — and then the caller clamped the result to `max(speed, 1.0) * dt`, a
*full walking step*. For a six-metre alien that is 5.6 m/s. So the separation could move the body
exactly as far in a frame as its legs did, and whenever the walk itself was slow — a turn, an
arrival, a body picking its way round a trunk — the push was the **whole** step. The character was
drawn walking forwards while travelling sideways, for as long as the overlap lasted.

Measured over ten simulated minutes of the shipped scene, with the distance ladder off so nothing was
culled, and classified by mechanism rather than assumed:

| | moving frames | backwards | its own travel | a crowd overlap | a solid |
|---|---:|---:|---:|---:|---:|
| rook | 19,720 | 5,056 (25.64%) | 0 | **5,056** | 0 |
| tide | 29,718 | 6,135 (20.64%) | 0 | **6,135** | 0 |

Every one a crowd overlap. Not one a solid, and not one the body's own travel.

The cause is the clamp, and there are two things wrong with it. `crowdSeparation` returns **half the
overlap** and the caller moved the body that whole distance in one frame, capped only at
`max(speed, 1.0) * dt`.

That is frame-rate dependent — half the overlap per frame is twice the separation speed at 120 Hz
that it is at 60 — which is exactly what ADR-161 says a behaviour's motion must never be. And it is
enormous: a fifth of a metre of overlap between two six-metre aliens produces a tenth of a metre of
push, which at 60 Hz is six metres a second, more than `rook` walks at. The cap was the only thing
making it soft, and the cap is a *walking step*.

**The decision.** Separation is a speed, and the speed says what the mechanism is for: **each body
walks out of its own half of the overlap over half a second**, integrated against the real dt, and
never faster than it walks. Deep overlaps still resolve at a walk — two bodies placed inside each
other have to get out, and be seen to — and a brush in passing becomes a nudge of a few centimetres a
second instead of a shove at cruising speed. The half-second is not free to choose: it is what
`characters make room for each other rather than standing in each other` already requires, which
starts two bodies three metres inside each other and gives them one second to get under half a metre.

The penetration resolve is left exactly as it was. "A body may not end a frame inside a solid" is a
guarantee rather than a preference, and it accounted for none of these steps anyway.

| | worst backwards step | mean | frames |
|---|---|---|---|
| rook | **0.0933 → 0.0317 m** | 0.0240 → 0.0284 | 25.6% → 28.9% |
| tide | 0.0263 → 0.0231 m | 0.0075 → 0.0018 | 20.6% → 9.4% |

`rook`'s authored walking step is 0.0933 m at 60 Hz. **The worst push was the walk, to four decimal
places.**

Only the worst separates the two builds, and that is why the assertion is stated on it. The mean
barely moves, because the old clamp only ever bit on the deep overlaps — which are exactly the frames
where a body got shoved a whole stride sideways. The *count* goes up, because a gentler push leaves
the body moving on frames where it used to be pinned; a count of backwards frames has no magnitude in
it and is not the statistic this was ever about.

### The fix that was measured and is not the fix

**Stopping a walk short of another body.** `explore`'s goal can *be* another character, and `arrive`
is 3.0 m on these four while two 2.4 m bodies need 4.8 — so the walk's destination was a spot the
separation field pushes the body straight back out of, and the two mechanisms would take turns for as
long as the character stayed. That is a defensible invariant, it was implemented, and it changed the
numbers by **nothing at all**: 5,056 and 6,135, to the digit, before and after.

Identical to the digit is not a result (ADR-162 learned that the expensive way), so it was reverted
rather than kept. It is recorded because it is the first thing the next person will think of, and now
it costs nothing to skip.

## What was measured

Glowmere Valley 2, 90 simulated seconds at 60 Hz, every entity updated every frame (the distance
ladder off — with it on, 5,393 of 5,400 frames are coarse and every reading is a measurement of the
level-of-detail ladder rather than of locomotion).

| | before | after |
|---|---:|---:|
| backwards steps the body's own travel accounts for | **5,624** | **0** |
| worst gap between drawn facing and body facing | **3.13 rad** | **0.13 rad** |
| foot slip outside 1.1×, measured against the clips | **4,506 / 4,506** | **0 / 54,917** |
| worst foot slip | **2.02×** | **1.00×** |
| clip frozen while the body covers ground | 23 | **0** |
| longest motionless stretch | **22.28 s** (chick-9, wedged) | **6.02 s** (bull-10, one authored pause of a 6.37 s maximum) |
| motionless frames | 33.7% | 34.5% |

The remaining 0.13 rad is not slack. It is the body's own authored `liveliness` sway plus the lean
`ground` gives it on a slope: a body pitched by *p* and rolled by *r* is drawn with its forward at
`atan2(cos r sin y cos p + sin r sin p, cos y cos p)`, which differs from *y* by up to
`asin(sin|t| tan|t|)` for a tilt of |t|. The test derives that bound from the lean the run actually
produced rather than writing a number down.

The five animated aliens over ten simulated minutes, nothing culled: **0** backwards steps that the
body's own travel accounts for, on every one of them, including `vane` — which was placed at 248
degrees and which no previous pass measured — and no push bigger than a third of the body's own
walking step, where the worst of them used to be a whole one.

**The probes themselves had to be fixed first, and that is a finding.** Both scene-running alien
tests pin the camera to the group and both were reading the level-of-detail ladder: the group spreads
over hundreds of metres, a body past its own `cullDistance` stops writing its node, and the node
snaps back to where the scene file put it and out again. That reads as `rook` travelling 1,766 m in
ninety seconds, in 1.68 m steps, with a 2.79 rad facing gap. `Engine::update` writes its own detail
policy onto the scene at the top of every frame, so the setting has to go through
`Engine::setDetailLimits` and not onto the composition — a scene-level poke is overwritten before
anything reads it, which is how the farm measurement spent a pass reporting 5,393 coarse frames out
of 5,400 and 5,624 "backwards" steps that were not steps at all.

## Evidence

`tests/unit/test_farm_locomotion.cpp`, six cases; two new cases in `test_alien_locomotion.cpp`'s
facing section; `vane` added to its `[.probe]` facing measurement.

Before the fixes:

* *euler degrees survive the trip through a quaternion* — fails for every yaw past 90 degrees
* *a placed facing is where a body starts, not an offset it carries for ever* — 2.62 rad gap, which
  is 150 degrees, which is exactly the placement
* *the farm animals travel the way they are drawn facing, at the speed their clips say* — 5,624
  backwards steps, foot slip outside 1.1× on every locomotor frame of the animal that had one
* *a wanderer that steps off the navigable set walks back onto it* — never freed, in thirty seconds
* *each farm animal's authored stride speed describes the clip it names* — fails for all nine
* *the animals wander their own territories without getting stuck* — 22.28 s

Two of those carry a control arm, because a probe that cannot fail is this project's own recurring
defect (ADR-182). The wedge test asserts that the steering fan really does find **0 of 16**
directions from the spot it chose, so a body without the escape would have to stand still; and the
placement test asserts that the body is drawn at its authored heading on the first frame and that it
really did turn more than three radians, so a fix that ignored the placement entirely would pass one
assertion and fail the other.

## What this does not fix

**The `minDwell` window**, again — ADR-204's and ADR-226's own standing exception. The gait holds a
state for a quarter of a second after the body's speed has left that state's band, so there is a
bounded window in which a nearly-stopped body still asks for a walk. It is 0% of locomotor frames on
this pack after the fix because `moveExit` is small, and it is still the reason the aggregate slip
bound is not stated as zero.

**`Gait::footSlip` still cannot see a `walkSpeed` that does not describe its clip.** That is ADR-204's
conclusion and it is unchanged: measuring a clip means knowing which joints are feet, which is asset
knowledge rather than engine knowledge, so the check lives in a test where the bones can be named. A
fourth pack will cost two literals and a `TEST_CASE`.

**Per-body navigation grids.** The scene builds one grid with a global `navBodyRadius` of 2.4 — right
for a six-metre alien and applied to a 0.4 m chick as well. That is why the chick was the animal that
found the wedge, and it is not why the wedge was unrecoverable. Recorded as not done, as it was
before.

**The other Glowmere scene.** `examples/world/glowmere-atmospherics.scene.json` still places both
chicks; only Valley 2 was in scope.

**No animation was added, and none was removed, because there was none to remove.** Both packs ship
their built-in takes and nothing else: every farm GLB carries exactly one clip called `Walk`, and the
aliens carry their 26. ADR-204 and ADR-213 changed clip *timing* and gait *settings*, never clips.
There has never been a generated locomotion clip in this repository, and
`every farm animal plays its own built-in Walk and nothing generated` is now the check that keeps it
that way.
