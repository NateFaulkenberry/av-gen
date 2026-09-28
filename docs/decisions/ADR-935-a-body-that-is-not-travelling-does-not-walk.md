# ADR-935: A body that is not travelling does not walk

**Status:** Accepted
**Date:** 2026-09-27
**Follows:** ADR-199 (a body playing a walk and going nowhere), ADR-619 (a rate is published per step),
ADR-620 (the gait's acceleration limit), ADR-758 and ADR-823 (a performance's speed), ADR-907 (a stop is
finished, not imposed), ADR-910 (the character quality analyzer and its stuck time), ADR-934 (a retired
body publishes no speed)
**Found by:** the GV3 revision's navfix stream (ADR-934's Consequences: "Rook's walk on the spot was
not the cows"), and this stream's traces of the films
**Implemented by:** `EntityWorld::restIfStill` and `EntityWorld::drawnAcross`
(`src/entity/entity.{hpp,cpp}`), applied on the played step (`EntityWorld::update`) and the replayed
one (the seek's step)
**Tests:** `tests/unit/test_still_bodies.cpp` (`[adr935]`: a decider's choice to stand, as GV3's rook
made it and from a walking pace; a move that gives up; a walk refused on its first step, against one
that arrives; a set piece's hold, against its carry up the beam; a scrub of the standing, the hold, the
carry and the refused walk)

## Context

`EntityState::speed` is what the gait picks a clip from (walk, run, idle), what the pose layer matches
strides to and what the decider reads as "walking an errand". It persists from step to step, and only
a mover writes it: the action tier's `move`, `wander`, `explore`, a director that names a speed. So a
step in which nothing moved the body kept the last number anybody wrote. ADR-199 met that once, in
`wander`, and fixed it there ("And *say* it is standing still"); ADR-619 met it in `turnRate` and cleared
the rate every step. Two ways it still lied, both measured on GV3:

- **It latched.** navfix's trace of gv3-cast's iteration 2: from 195.42 s rook played its walk on the
  spot for 6.5 s at (-65.6, -5.6), with no body within 12 m. Its `holdPost` "range" walked it home and
  completed; the decider chose "range" again, and inside the ring that option has no actions -- it wins
  by doing nothing, and the behaviours below (`lookAt`, `liveliness`, `ground`) move nothing. The move's
  arrival had written 0, and ADR-620's limiter made that one step of deceleration: 0.584 - 6.62/60 =
  0.474 m/s. Nothing wrote again, so the limiter held 0.474 against 0.474 and the gait showed a walk
  (above rook's 0.45 m/s `moveExit`) until a greeting moved the body 6.5 s later. ADR-910 counted it:
  rook's stuck time was 8.5 s. The same latch:
  - held a set piece's animal walking on the spot through the hold before its lift. The hold (`follow`
    with `hold` and no `rate`) owns the body and names no speed, `wander` yields to it without writing
    one, and the wander's last stride stood: GV3's E5 horse, 3.3 s at 1.61 m/s while the saucer's beam
    came on; bull-21, cow-12 and cow-23 at E3 and E4; every animal of GV2-multicam's looping abduction;
  - left a `move` that gave up "stuck" publishing its last stride's pace (it writes `speed = travel`
    and fails in the same step) for as long as the body stood. Measured on ADR-933's fixture: 1.46 m/s
    for the rest of the run.
- **It ramped where the body had stopped dead.** A move that ends on its first step -- refused
  ("unreachable"), or already there (a reaction's approach inside its radius) -- stops a walking body
  within one step, and the limiter then ran the legs down over the next half second of standing still:
  0.35-0.8 s on the spot after each such errand, several per alien per film.

One fact in all of them: the speed says the body is walking and the ground says it is not.

## Decision

**After every writer and the limiter, a body the step did not move publishes no speed.**
`EntityWorld::restIfStill` compares where the body is drawn across the ground at the end of the step
with where it was drawn at the end of the last (`drawnAcross`: the simulation's place plus the offsets
its behaviours drew it at, so a craft's `hover` and `drift`, which move the node and not the body,
count as moving it). When the two are bit-identical, the speed is 0, and the next step's limiter starts
from there.

- **Where:** after ADR-620's limiter and before the vector intent is rescaled (so `desiredVelocity`
  says the same), on both paths, taken at the same two points on both. A scrub lands on the play's
  speed and clip.
- **Exact equality, not a threshold.** A body that anything moved by any amount moved; one nothing
  touched is bit-identical to where it was. A floor on the measured speed would catch a body setting
  off from rest at its gait's `accel` -- 0.08 m/s on rook's first step -- and hold it there for ever.
- **A director that names a speed keeps it.** `DirectorMotion::hasSpeed` is a staging step's `rate`:
  an animal carried up a beam has its legs going over ground it is not crossing, which is what the set
  piece asked for (ADR-758's performances are the same). A director that names none -- a hold -- is
  holding the body, and the rule applies.
- **Not a zero-length step** (`dt` 0: the first frame, a seek's first replayed step), on which nothing
  can move.

**Why here and not in each writer.** ADR-199 fixed the latch in the writer that had it, and it came
back through four others (the decider's empty option, a set piece's hold, a failed `move`, and the
limiter's ramp, which no writer controls). A central rule holds for writers that do not exist yet, as
ADR-620's limiter does. Clearing `speed` at the top of every step (ADR-619's contract for `turnRate`)
was weighed and not done: `speed` is also the pace a body *has* -- ADR-908's `move` starts a walk from
it, and 80 farm animals list `liveliness` before `wander`, so their bob reads the last step's speed --
and both would have changed for every body. This rule changes only bodies that did not move.

**What it does not do.** A body pushed by the crowd while nothing drives it moved, so a stale speed can
survive while the push lasts; and a body walking into something that holds it moves a little each step,
which ADR-910 still reads as stuck -- that is the walk's to give up (the `move`'s stuck clock), not this
rule's.

## Consequences

- **On the fixtures** (`test_still_bodies.cpp`; every case fails on the engine before this ADR, 15
  assertions -- `build/behave-runs/behave-adr935-control.log`):
  - GV3's rook, rebuilt: its walk to its post ends at 15.5 s and it stands 44 s. Before: 2,668 frames
    at 0.523 m/s with the walk shown, ADR-910 stuck 44.5 s. Now no frame with a speed or a walk.
  - The same choice to stand made half way, from a walking pace: before 3,164 frames at 3.069 m/s
    (stuck 52.7 s); now none.
  - A `move` that gives up "stuck" in ADR-933's channel: before 1,307 frames at 1.464 m/s; now none.
    A walk refused on its first step at 3.07 m/s: before 240 frames at 3.069 m/s; now none. Its
    control, a walk that arrives, brakes on its own ramp over 112 moving frames, all left alone.
  - A set piece's hold: before 179 frames at 1.0 m/s (the wander's pace); now none. Its carry keeps
    the named 0.7 m/s and the legs going.
  - Scrubs to the standing, the hold, the carry and the refused walk land on the play (position under
    1 mm, speed within 1e-5, the same gait).
- **On the films** (whole film, 226 s at 60 fps, `avgen_cast_trace --decisions all` at 20 Hz and
  `avgen_character_quality`; before `integrate/revision` `983221a9` (main plus navfix), after this
  branch at `9aadb424`, which carries ADR-936 too. GV3 is `gv3/production` `a6020f39` generated into a
  scratch tree: as generated (E5 heard as "stop and watch"), and with every alien's reaction to E5 at
  `approach` 18 ("go and see"). GV2-multicam is the frozen tracked file):

  | film | ADR-910 stuck, aliens | stuck, animals | walk shown while not moving, aliens | same, animals |
  |---|---|---|---|---|
  | GV3 as generated | 5.0 -> 0.0 s | 11.6 -> 2.0 s | 2.3 -> 0.0 s | 17.2 -> 7.9 s |
  | GV3, go and see | 4.4 -> 0.0 s | 11.6 -> 2.0 s | 2.0 -> 0.1 s | 17.2 -> 7.9 s |
  | GV3, gv3-cast's iteration 2 | 15.6 -> 0.0 s | 10.9 -> 1.3 s | 12.8 -> 0.0 s | 17.3 -> 7.8 s |
  | GV2-multicam | 8.7 -> 0.1 s | 20.2 -> 2.6 s | 6.6 -> 0.8 s | 27.1 -> 8.2 s |

  With ADR-944 on top (this branch's head, `1670f07d`), the after-columns read: GV3 as generated
  0.0 / 2.0 / 0.0 / 8.3 s, go and see 0.0 / 2.0 / 0.0 / 8.4 s, iteration 2 0.0 / 1.3 / 0.0 / 8.1 s,
  GV2-multicam 0.1 / 2.6 / 0.8 / 6.9 s.

  - **Rook's case, as the brief put it:** iteration 2 (a copy of the project navfix measured) replays it
    on the engine before this ADR exactly: 195.45-201.90 s at (-65.6, -5.6), 0.473 m/s published
    after "range -> range", the walk shown, ADR-910 stuck 8.5 s. After: rook's stuck time is 0.0 s and
    its walk on the spot 7.8 -> 0.0 s. (On `a6020f39` the valley's closure moved rook elsewhere at
    that time; its stuck time there is 0.9 -> 0.0 s.)

  - **The set pieces' holds:** on GV3 horse-11 (E5) 3.9 s of walking on the spot -> 0.5 s, bull-21
    (E3) 3.4 -> 0.5 s, cow-12 (E4) 3.2 -> 0.3 s; on GV2-multicam the abducted animals' 2.7-4.3 s each
    -> 0.3-1.8 s. What is left, here and in the animals' stuck time (GV3: bull-21 0.8 s, horse-11
    0.6 s, cow-23 0.6 s), is the first half second of each lift, where the set piece names 0.7 m/s and
    the beam raises the body faster than it crosses ground: legs going, as asked. (GV2-multicam's
    abduction predates the set-piece record the trace splits lifts out by, so its lifts are in the
    animals' column; bull-18's 1.8 s is mostly a creep at 8.7 s.)
  - **The aliens' first-step ramps and latches are gone:** every alien's stuck time is 0.0-0.1 s (GV3
    before: ember 1.7, vane 1.3, rook 0.9 s; GV2-multicam: rook 3.0, ember 2.9, vane 1.4 s).
  - The animals' other seconds of "walk shown while not moving" (0.6-1.1 s each over the film,
    unchanged before and after) are the tails of `wander`'s stops, creeping under 0.05 m/s -- a slow
    walk, not a published speed over 0.3 m/s.
- **A walk that ends mid-stride now leaves the body standing until the next decision, and the next
  walk starts from rest.** Before, the latched pace was ADR-908's "pace the body already has", so the
  next walk set off at that pace from a standstill and swung round on a circle, the legs having walked
  on the spot in between. Now ADR-908 pivots the body from rest, which is what it does for any body at
  rest, and ADR-910 counts a stop there: GV3's sage at 21.7 s and tide at 129.0 s, each a greeting that
  chased a walking alien and gave up. Standing bodies' turning is counted as turning on the spot: GV3's
  sage 27% -> 53% of its yaw (it stands and watches more on the trajectory ADR-936 gives it); the other
  aliens move by a few points either way.
- **Decisions change wherever a latched speed stood:** the selector read `speed > 0.3` as "walking an
  errand" (ADR-909's turn-back veto and its hold on the incumbent), so a body standing with a latched
  pace was refused options behind it. With ADR-936 in the same run the films' trajectories diverge
  from the first difference (the aliens' totals are in ADR-936).
- **Every scene changes where a body stood with a latched or ramping speed:** every scene with a
  `holdPost` inside its ring or an `idle` option (GV2 family, GV3, the Character Intelligence Lab's
  deciders, the autonomy demo), every set piece's hold before a lift (GV3's E3-E5, GV2's abductions,
  the tractor-beam labs), every `move` that fails or is refused while the body walks, and every body
  with an authored ramp whose mover stops it dead. Those bodies now stand, and show it.
- **ADR-910's stuck metric now reads what it was meant to:** a body asking for a walk and creeping,
  and a body carried by a set piece that names its legs' speed.
