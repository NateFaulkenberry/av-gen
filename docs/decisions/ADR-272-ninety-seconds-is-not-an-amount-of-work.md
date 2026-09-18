# ADR-272: Ninety seconds is not an amount of work

**Status:** Accepted
**Date:** 2026-09-18

A timeline click is understood as a command in 3.1 ms and answered on screen in 2,437 ms. It is not
a flatten -- the seek causes zero of those. It is `EntityWorld::seek` re-integrating every entity
from `target - 90 s` at 1/60 s, and ADR-267 priced the same call at 161.5 s with a hundred
autonomous characters and 594.7 s with two hundred and fifty. That is what turns it from an
optimisation into a blocker: the character-AI plan cannot scale past about 180 bodies until it is
bounded.

`agent/responsive-editor` already took `input -> first visual` from 2,712 ms to 8.9 ms by deferring
the re-simulation. It deliberately did not make the answer arrive sooner: `input -> final visual`
moved only 14%. This is the half it left.

---

## 1. What the ninety seconds was for

Nothing, structurally. `entity/entity.hpp` says it in as many words: "`maxSeconds` bounds the work:
seeking an hour into a piece must not stall for a minute." It is a work ceiling and it was written
in the wrong currency.

A replay costs **steps x bodies**. Ninety seconds at 1/60 is 5,400 steps, which is 8,100 body-steps
in a fifteen-body test scene, 124,200 in Glowmere's twenty-three, and 1,350,000 in the cast the
character-intelligence plan asks for. The same literal buys two orders of magnitude more work
depending on what is in the scene, and the scene is the thing the number was supposed to protect the
editor from.

It was also never a correctness boundary, and the header has always said that too: beyond it "the
simulation starts from `time - maxSeconds`, which costs a character its accumulated history". A seek
to t = 200 s has never reproduced a play to t = 200 s. Only a seek inside the window ever did.

**Replaced with `entity::SeekBudget`**: a policy ceiling in seconds *and* a price cap in body-steps,
the smaller winning. A bigger scene now keeps less history rather than taking longer. Zero means no
cap, which is what every non-live caller keeps -- an offline render wants the whole window whatever
it costs, because nobody is waiting for it.

---

## 2. What actually needs replaying

Three answers, in decreasing order of how much they were worth.

### 2.1 Not every body

`IBehavior::historySteps` says how many fixed steps, ending at the target, a behaviour needs for its
state to be the state a play would have left. `hover` is `slowNoise(ctx.time, rate, seed)` and
returns 1. `drift` returns 2, because its offset is a function of the clock like `hover`'s but the
velocity it publishes for `bank` is a backward difference against the previous step. Everything else
defaults to `kAllOfIt`, and the default is the safety: a behaviour opts out by saying so.

**On the scene that prompted this, it is worth almost nothing, and that is the finding.** Glowmere's
twenty-three entities carry `wander` sixteen times, `explore` five, `liveliness` twenty-one and
`ground` sixteen. Twenty-one of twenty-three bodies navigate, and a navigating body needs every
step. The mechanism is real and it is not the fix for this scene; it is the fix for a craft-shaped
one.

### 2.2 Not the camera's opinion

The cull test inside the loop read `viewPosition`, so a seek was a function of (time, where the
camera happened to be). The parameter is gone rather than guarded. See §4.

### 2.3 Not a cheaper step, and this is the one that was measured and refused

The per-step cost is dominated by `Navigator::sample`, which resolves to `WorldMap::sample` -- an
analytic evaluation of the world's noise functions. The baked `NavGrid` answers the same
walkability question at 0.024 us against roughly 10 us, four hundred times faster, and
`Navigator::pathClear` could be made to ask it instead.

**It would not be the same answer.** The grid answers at 4 m cell resolution what `pathClear`
answers at 0.25 m sample spacing; a walker routed by the grid takes a different line past a trunk,
and a different line is a different frame. ADR-182's rule reads the same way for performance as for
anything else: a faster seek that produces a different frame is not a faster seek. The lever is
real, it lives in `world/`, and taking it means re-deciding what a walkable metre is -- which is a
change to the simulation, not to the scrub.

---

## 3. The replay now lands on the second it was asked for

`seek` ran `now = target - span + i * dt`, so its final integrated instant was `target - dt`: one
step short of the second being asked for, against a play whose final step is `target` itself.

That one step is where ADR-267's residual **0.000022 m** came from. Counting backwards from the
target makes the replayed sequence the same sequence of instants a play from zero produces, and the
residual goes to **exactly zero**.

`tests/unit/test_entity_seek.cpp` requires the difference to be bitwise zero over four bodies
carrying hover, drift, bank and spin at twenty seconds, compared as *the frame after* -- one
ordinary update on top, reading the node transforms, because a seek deliberately writes nothing to
the parameter set and eight of the eleven behaviours produce only an offset the next update folds
on. A comparison of `state().position()` after a seek cannot fail for any of them, and the first
version of the probe in `tools/seek_probe.cpp` proved it by reporting 0.000000 m for eight arms at
once.

The controls beside it: a 30 Hz play, and a seek to a neighbouring second. Both must disagree.

---

## 4. The four defects, and two more

ADR-267 found four inside `EntityWorld::seek`. All four are fixed.

1. **`BehaviorContext::self` was never assigned**, so every body excluded entity 0 from crowd
   separation instead of itself.
2. **The crowd field was never rebuilt**, so a seek separated against whatever snapshot the last
   *played* frame left behind -- state surviving across the one call whose job is to remove state.
3. **The action queue and the schedule were not re-simulated at all.** ADR-091's Cinematic Action
   tier was the one tier a scrub could not reproduce.
4. **The cull test read `viewPosition`.** Removed with the parameter rather than guarded.

Two more found while fixing them:

5. **The yaw was never canonicalised**, so a replayed facing was the total a body had turned rather
   than the direction it faced, and `angleDelta` against it differs after 5,400 steps.
6. **The gait was bypassed**, so a scrub published the raw activity where a play publishes a
   hysteretic one -- a different clip on exactly the frames where the speed sits on a threshold.

And one inherited: `reset()` never restored `Entity::active_`, so a reset carried "entity 7 is not a
body" across from whatever frame last culled it.

---

## 5. Behaviour LOD: measured, and left measured

ADR-267 showed that `src/entity/action.hpp`'s claim -- that behaviour LOD "does not change the
answer" -- is measurably false of behaviours: 50.263 m over eight explorers at thirty seconds
between a camera at the origin and one 200 m away.

**This work removes it from the seek and leaves it in the play.** The verdict, stated plainly: the
claim in `action.hpp` is about the action queue, where it is true, and it is false of the behaviour
layer, where nothing claims it in that file. ADR-267 obligation 2 -- LOD selects which stages run,
never the integration step -- is a change to `EntityWorld::update`, it touches every played frame
rather than every scrubbed one, and it belongs with the fixed-step accumulator (obligation 1) that
it shares a mechanism with. Doing half of it here, in the seek, would leave the two paths disagreeing
in a third new way.

What is true after this work: **a seek no longer consults a camera at all**, so the 50 m is a
property of playback and not of scrubbing.

---

## 6. 44 textures a flatten, because a flatten happened

`Composition::rebuild` bumped `Scene::textureVersion` unconditionally, and that is the renderer's
"destroy and re-create every GPU texture" signal. The audit measured it twice and got the same
answer: 528 uploads over 12 flattens for starring a hero, 704 over 16 for a brush edit -- 44 a
flatten in both, because the count is a property of flattening rather than of the edit. Neither edit
touches an image.

The table genuinely is rebuilt every flatten, out of the same cached assets, so the answerable
question is not "did this function touch the table" but "is the table it produced the same table".
That is a digest: word-wise over name, dimensions, format and pixels, because the structure hashes
elsewhere in that file run over a few hundred floats and this runs over megabytes.

A 64-bit digest can collide and a collision here is a stale texture on the GPU. Keeping the previous
table alive and comparing byte for byte would remove a probability of about 2^-64 per edit at the
cost of doubling resident texture memory on every flatten, which is the wrong trade. Recorded rather
than left implied.

`textureTableDigest` is a free function so the decision can be tested on its own: one texel of one
4x4 image changes and the answer must move. Without that arm, "the version did not move" is
indistinguishable from "the version can never move", which is a worse defect than the one removed.

---

## 7. What this does not do

**No checkpointing.** ADR-267 called it "an optimisation for scrub latency, not a correctness
mechanism" and priced it behind two cheaper things; this is those two things. The reason it is not
here as well is structural: a checkpoint has to capture every behaviour's internal state, and
`IBehavior` has no snapshot surface -- eleven classes holding routes, phases, timers, smoothed
followers and a PRNG, with no mechanism that would notice a member somebody forgot to add. The
failure mode of a stale snapshot is a scrubbed frame that differs from a played one, which is the
defect the seek exists to prevent. It is worth building, and it is worth building against a
declared-state interface rather than against eleven hand-written serialisers.

**No forward-incremental replay.** The obvious cheap version -- a seek ahead of where the world
already is continues rather than restarting -- was designed and rejected, because `EntityWorld::update`
runs on every frame whether or not the transport is playing. A world paused at t for sixty frames
has integrated sixty steps of behaviour at a frozen clock, so "where the world already is" is not a
state any replay would have produced, and the reuse would be silently wrong.

**No second thread.** ADR-267's other candidate. It is the right answer for the remaining cost and
it is a separate change.
