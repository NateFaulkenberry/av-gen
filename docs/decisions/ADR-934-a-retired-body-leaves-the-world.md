# ADR-934: A retired body leaves the world

**Status:** Accepted
**Date:** 2026-09-27
**Follows:** ADR-209 and ADR-210 (the director decides first; the Director tier), ADR-270 and ADR-290
(the crowd and the sense stage, taken as snapshots each step), ADR-385 (what a retire puts back),
ADR-700 (checkpoints), ADR-928 to 930 (set pieces, whose `retire` steps these are)
**Found by:** the GV3 revision's gv3-cast stream, on iteration 2's whole-film trace (the coordinator's
brief for this stream, item 4)
**Implemented by:** `EntityWorld::retire`, `Entity::retired` (`src/entity/entity.{hpp,cpp}`), and the
places a retired body is left out of:
- the crowd builds and every step, played (`EntityWorld::update`) and replayed (the seek's step), in
  `entity.cpp`;
- `EntityWorld::pointOfInterest`;
- the fields pass's broad phase;
- `GridPerception::perceive` (`src/entity/perception.cpp`);
- `PerceptMemory::merge`'s world argument (`src/entity/decision.{hpp,cpp}`, called from `Decide` in
  `src/entity/behaviors.cpp`);
- `CharacterQualityRecorder::record` (`src/entity/character_quality.cpp`).

`Staging`'s `StepKind::Retire` (`src/stage/staging.cpp`) calls it.
**Tests:** `tests/unit/test_retired_bodies.cpp` (`[adr934]`: it neither blocks nor draws another body
and stays where it was taken, against the same film with no retire; a scrub past the retire lands
where the play did, against a scrub back before it; the quality recorder's track ends at the retire);
`tests/unit/test_abduction_poc.cpp` (the wander metric leaves retired animals out)

## Context

A set piece's `retire` step (`StepKind::Retire`) is how an abduction ends for the animal: hidden, its
claims released, never offered to a query again (`Staging::isRetired`). It cleared the body's director
motion and "put the body back under its own behaviours before hiding it". Three passes take every
active body whether anyone can see it or not:

- the crowd other bodies separate against;
- the body index the sense stage perceives;
- and every behaviour's own step.

So the animal went on living, invisibly. gv3-cast, on GV3 iteration 2's whole-film trace:

- after E4, cow-12 and cow-23 grazed on unseen in the west meadow until 226 s (cow-12 walking at
  (-63.9, -1.5) at 206 s);
- bull-10 (E3) and horse-11 (E5) did the same;
- rook played its walk clip in place for 5.25 s (205.65-210.85 s at (-66.2, -5.2)), 0.51 m/s intended
  and zero travel on flat dry ground, with the invisible cow-23 3.0 m away and cow-12 4.3 m away;
- ADR-910's stuck time for rook went from 0.4 s to 6.7 s.

This stream's trace of the same film, with every decision (`avgen_cast_trace --decisions`), shows the
invisible animals doing more than standing in the way. After their abductions, the aliens made 7
decisions about them:
- tide walked to see cow-12 at 177.5 s;
- ember headed for horse-11, across the river, at 190.1 s, which fed its pacing on the bank
  (ADR-932, ADR-933);
- sage went to cow-23 at 165.1 s and again at 178.4 s.

Nothing a scene can author works round it, because only the set piece knows the moment. The cast
trace's `atRetire` showed the other half: it is taken on the frame of the retire, after the body was
dropped back to the ground, so horse-11, 23 m up the beam, read 5.3 m.

## Decision

**`EntityWorld::retire(name)` takes a body out of the world, from the step it is called on, and the
retire step calls it.** A retired body:

- **is not simulated:** no action, behaviour, sense, gait or pose step runs for it. It is held where it
  was taken: in the beam, not dropped to the ground. It publishes no speed or turn, since a beam carried
  it up at a walking speed that would otherwise stand in its state for the rest of the film. Its node
  is still written from that state each frame, because finals are rebuilt from bases every frame and
  skipping the write would put the hidden node back on the spot the author placed it, where a camera
  aimed at it would jump;
- **is not in the crowd:** read off the retired flag, not off the `active` flag, because the director
  retires it at the top of the very step the crowd is built for;
- **is not perceived:** `GridPerception` never offers it (it stays in the body index, whose point index
  is the entity index). The retire also removes it from every current working set at once, since a
  working set is kept between sense ticks and one still holding the body would send a character toward
  the spot it was lifted from for up to a quarter of a second (measured on the fixture: 10 frames).
  A decider's percept memory forgets it rather than fading it;
- **is nowhere anyone can be sent:** `pointOfInterest` does not resolve it, so a walk to it, a face
  toward it or a post on it fails "no such target";
- **is in no field's broad phase:** it keeps the influence it was taken with, as a culled body does;
- **ends its quality track:** `CharacterQualityRecorder` leaves it out from the retire on. Before this
  ADR its numbers after an abduction were an invisible animal grazing; after it they would be an animal
  standing in a beam.

**For good, and seek-exact.** Nothing un-retires a body but `EntityWorld::reset`, which is how a seek
back before the retire gets it back.

- A seek's replay runs the director on each step before the bodies (ADR-671), so it retires the body on
  the step a play did.
- The flag is an `Entity` member, so every ADR-700 checkpoint copies it with the rest of the body.
- The crowd, the steps and the working sets are left out in both the played and the replayed step.

**Not changed:** the retire still clears the director motion, cancels the Director tier and hides the
node (ADR-385's parameter restore is untouched). Staging's own `isRetired`, the "never offered to a
query again" list, is unchanged. A retired body's `atRetire` in `avgen_cast_trace` is now where it was
taken, because nothing drops it to the ground first. The Critic's adapter works round the old value;
its note can go.

## Consequences

- **On the fixture** (a grazing cow with a body, an alien across the field that notices cows and goes
  over to them; the cow retired at 2 s):
  - after the retire, the cow moved 0.0000 m in 12 s, was in the crowd for 0 frames, was perceived for
    0 frames, and was not a point of interest;
  - the alien, which had set out to greet it, came no nearer than 14.18 m;
  - the control (no retire): the cow wandered 2.72 m, the crowd held it for 720 of 720 frames, the alien
    perceived it for 720 and walked up to within 2.46 m.
  - A scrub to 11.98 s landed the cow, retired, and the alien where the play had them (under 1 mm, yaw
    within 1e-4). A scrub back to 1 s gave the cow back.
- **The GV2 abduction's wander metric** (`test_abduction_poc.cpp`, "the animals wander their own
  territories without getting stuck") leaves out retired animals, as it already left out animals the
  director was holding. With the retire, goat-14 is held where it was taken from 15 s, and counted it
  would read as stalled for 75 s. Before, it wandered on unseen and was counted as a wanderer. Re-
  baselined: the fact changed, not the metric.
- **Scenes whose behaviour changes:** every scene with a `retire` step. The abducted animal no longer
  lives on invisibly, and the bodies near where it was taken no longer walk round it, notice it or go
  to it:
  - GV3's E3, E4 and E5 (bull-10, cow-12 and cow-23, horse-11);
  - GV2, GV2-multicam and GV2-song, `glowmere-atmospherics`, `_pre-defects`;
  - the tractor-beam labs.
- **On the films** (whole film, 226 s at 60 fps; before `ec515c8b`, after this branch):
  - **GV3** (a scratch copy of gv3-cast's iteration 2): the four animals the set pieces take travel
    nothing after they vanish, where before bull-10 walked 136.7 m, cow-23 95.9 m, cow-12 68.7 m and
    horse-11 38.7 m unseen. The aliens made 7 decisions about taken animals before and 0 after.
  - **GV2-multicam:** its abduction loops through ten animals (from 14.9 s). Each travelled 26-170 m
    after it vanished (horse-11 154.0 m from 14.9 s, cow-19 170.2 m from 33.1 s) and now travels
    nothing.
  - **Rook's walk on the spot was not the cows.** On the after run rook walks its clip in place for
    6.5 s (195.45-201.95 s) at the same spot, (-65.6, -5.6), with no body within 12 m. It is a `holdPost`
    return (`range` then `range` again): once the walk completes, rook goes on publishing about
    0.5 m/s with nothing moving it. ADR-910's stuck time for rook reads 6.7 s before and 8.5 s after. The
    cows were 3 m away by chance. The defect is recorded in this stream's report and not fixed here.
- **The quality tables for abducted animals end at the abduction.** ADR-910's tables counted their
  invisible grazing after it.
