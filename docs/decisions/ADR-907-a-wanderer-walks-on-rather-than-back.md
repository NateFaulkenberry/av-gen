# ADR-907: A wanderer walks on rather than back, keeps off steep ground, and eases into its stops

**Status:** Accepted
**Date:** 2026-09-26
**Follows:** ADR-088 (behaviours), ADR-096 (gait), ADR-240 (the escape), ADR-620 (authored acceleration),
ADR-826 (the character quality analyzer), ADR-375 and ADR-387 (layers and the Inspector)
**Implemented by:** `Navigator::pickDestination(Rng&, glm::vec2, const DestinationRequest&, glm::vec2&)` and
`Navigator::routeSlopeDegrees` (`src/entity/navigation.{hpp,cpp}`); `Wander` (`src/entity/behaviors.cpp`);
`BehaviorContext::gait` (`src/entity/behavior.hpp`, set on both the play and replay paths in
`src/entity/entity.cpp`); `Gait::select`'s passthrough (`src/entity/gait.cpp`); the World panel Inspector's
character section and the `entity/` layer entry (`src/ui/world_panel.cpp`, `src/ui/ui_logic.hpp`)
**Tests:** `tests/unit/test_forward_wander.cpp` (five cases, each with a control arm); the seek case in
`tests/unit/test_decider_habits.cpp`; `tests/unit/test_character_controls_reach.cpp` (where each control is found)

## Context

The owner's revision brief (§11, §12, and the character animation assessment) names what GV3's farm
animals do wrong: they walk to a point, stop, turn round on the spot and walk back; they stand and
walk on hillsides, and horses face straight into hills. The GV3 character audit traced each of these
to a rule in `wander`, measured on the film's twelve animals over its 226 s:

- **Every destination was a uniformly random direction.** Half of all destinations lay behind the
  body. 128 of 258 stops were followed by a turn of more than 90 degrees.
- **A body past its home radius drew its next destination around home**, which is a return trip by
  construction.
- **The only slope limit was the world's 63-degree cliff rule.** cow-23, cow-12, horse-22 and horse-2
  spent 74, 62, 52 and 49% of their paths on grades over 15 degrees. 134 of 273 stops were on ground
  steeper than 12 degrees, and 30 of those (84 s) faced within 45 degrees of straight uphill.
- **Wander stopped the body dead** in one frame on arrival, while ADR-620's limiter ramps only the
  *published* speed. So the legs went on walking for about 0.4 s after every stop (cow-23 at 45.70 s).

## Decision

**1. Destinations are drawn in a cone about the way the body faces** (`headingSpread`, default 60
degrees either side), through a new `DestinationRequest` form of `pickDestination`. The old
`pickDestination(rng, from, lo, hi, out)` is a different question and is unchanged; its other callers
(tests and the abduction report) still ask it.

**2. The cone leans toward home rather than jumping to it.** The lean starts at half the home radius
and is complete at the edge. Cone and lean together never reach past 90 degrees from the body's
facing. That number comes from ADR-908, not tuning: a walk-through mover at rest pivots only while its
way is more than 90 degrees off, so a destination inside 90 degrees is walked out to on a curve from
the first step. Capping the lean at 135 degrees instead left 7% of a wanderer's turning done
standing; at 90 it is 0.2%.

**3. A leash:** a candidate farther than `homeRadius` from home is offered only if it is no farther from
home than the body already is. A body inside its territory stays in it, and a body outside never walks
further out. The old rule allowed a body to stray up to twice its home radius.

**4. Two more filters on candidates:**
- A candidate inside the body's turning circle (ADR-908) is not offered, since it could only be
  reached by a pivot.
- **A slope limit, on by default** (`maxSlope`, 12 degrees): a candidate is refused if its ground, or
  the straight walk to it, is steeper than that. The walk is sampled every 2 m from 2 m out, with the
  normal taken a metre either side (about a body length). 0 is the world's own cliff rule and nothing
  more.

**5. The fall-back order when a cone comes up empty** (a river bank, the world's edge, a flank):
- first the whole circle with the slope limit still strict;
- then, with nothing gentle anywhere in reach, the gentlest navigable ground on offer, so a body put on
  a hillside walks down it rather than standing there;
- only then is the body boxed in.

**6. The pace is eased.** Wander keeps the pace it actually walks at (`pace_`, a behaviour member, so
ADR-700's checkpoints carry it). It is eased by the body's own gait: up at `accel`, down at `decel`,
the numbers the action tier has always used, now handed to behaviours as `BehaviorContext::gait`. The
walk slows into its destination:
- never faster than it could still stop from in the distance left;
- inside `arrival` metres (default 2), in proportion to what is left of them (Reynolds' arrive,
  floored at a fifth of the pace, as the action tier does).

On arrival the remaining pace runs down along the body's facing rather than being cut. The arrival has
already slowed it to almost nothing, so this is centimetres.

**7. The gait remembers a stop that happened underneath a passthrough activity.** A body held still
under React or Observe (an observe pose, a hold in a beam) now remembers Idle, not the walk it was on.
Before this, the first frame of the next eased walk read Idle below the move band, changed the gait,
and `minDwell` then refused the walk for a quarter of a second while the body accelerated (legs idle,
ground moving). A flinch that keeps walking keeps its walk, as ADR-091 requires.

**8. Where an artist finds all of this (the owner's UI reach rule).** Every knob above is a registered
parameter under `entity/<name>/wander/`, and each wander knob -- the new ones and the old -- now carries
a label that says what the viewer sees ("how far off straight ahead it wanders (deg)", "steepest ground
it walks on (deg, 0 = any)", "slows down over the last (m)", "longest pause between walks (s)"). Two
changes make them findable rather than merely reachable:
- **`entity/` joins the Intermediate layer**, beside `nodes/`. It was on no layer list, so every
  behaviour, gait and decider control of every character showed only on Advanced, and the editor opens
  on Intermediate: a fourth instance of ADR-375's "a control that silently belongs to a layer nobody is
  on".
- **The World panel Inspector gains a character section.** Clicking an alien or a cow selects the node
  it drives, whose `nodes/<n>/` prefix is a transform; the Inspector now also lists, under "How
  <name> moves and behaves", everything registered under `entity/<name>/`, headed by behaviour
  (`wander`, `gait`, `decide`) and drawn with the labels (`ui::inspectorPlace`, `ui::entityInspectorPrefix`).
  ADR-908's and ADR-909's controls appear there too.

**On defaults:** all of this is the default for `wander`, including the slope limit. The walk → stop
→ 180° → back pattern is built into the old draw, and the owner does not want it as a default.
`wander` is the background-animal behaviour, and no Character Intelligence Lab benchmark routes through
it (the lab fixtures are `explore` and `decide` bodies). **The slope limit's default is 12 degrees**
because that is the line ADR-910's analyzer calls steep, and because the owner's §11 ("avoid walking
into steep slopes") is a statement about animals in general, not about one scene: a scene whose
creatures climb says so with `maxSlope` 0 or a larger angle. The first cut of this decision left the
limit opt-in, which would have fixed GV3 only if GV3 remembered to ask.

## Consequences

- **The pattern is gone by construction, and measured.** Over 1,200 s on open ground, a default
  wanderer made 135 measured stops, **0 reversals** and 0 turns over 90 degrees. The owner's pattern
  (the whole circle with a pivot-and-go turn) made 24 reversals, and the cone alone removes them even
  from a pivot-and-go body.
- **The slope limit holds, by default too.** On a ridge world with a 47-degree flank, `maxSlope` 10 kept
  every walk under 1.6 degrees and every standing second off ground steeper than 12. With `maxSlope` 0
  the same body walked up to 47.1 degrees and stood 25.9 s on steep ground, 8.7 s of it facing uphill.
  With no key in the file (the 12-degree default) it walked on no more than 5.1 degrees and stood 0 s
  on ground over 14.
- **The pace is the gait's.** The largest measured acceleration was 3.1 m/s² against an authored 3.0
  decel (the rest is the sideways part of a curve), and there were no seconds of legs walking on the
  spot. An absurd gait (1000/1000) measured 84 m/s², which shows the gait's numbers are what is being
  read.
- **Every scene with a `wander` behaviour walks differently**, because the defaults changed:
  - farm animals in `glowmere-valley-2*`, `glowmere-atmospherics`, `tractor-beam-lab*`,
    `_pre-defects`;
  - the motion-matching labs;
  - `examples/characters/alien-wander`.

  Their routes, stop places and timings all move, and on hilly ground they keep to the gentler parts.
  ADR-910 has GV2-multicam and GV3 measured before and after, and lists the tests re-baselined with
  their evidence.
- **Scenes with no authored `accel`/`decel`** now ramp at the gait's defaults (6/8 m/s²) instead of
  starting and stopping in one frame. `wander` bodies were already replayed in full on a seek (their
  history is "all of it"), so ADR-620's replay-depth concern does not arise.
- **`BehaviorContext` gained a field.** A caller building a context by hand gets the gait's defaults
  when it leaves the field null.
- **The Parameters panel's `entity` group now shows on Intermediate** for every scene with characters.
  Its sub-groups follow the panel's existing rule (closed by default beyond eight), so a large cast is a
  list of names, not a wall.
- **`explore` is unchanged.** It keeps its own pivot model and destination draw, and ADR-333's golden
  route trace is untouched.
- **Open:** the slope limit is a `wander` setting. The decider's considerers still choose points of
  interest without one, so a GV3 alien can still be sent up a slope. The owner's complaint was about
  the animals.
