# ADR-944: An errand to a body walks to where the body is

**Status:** Accepted
**Date:** 2026-09-27
**Follows:** ADR-290 (percepts of bodies), ADR-333 (the decider's considerers), Phase D §24 (`social`'s
greeting walks to the body) and Phase D's `investigate` (a body is looked at where it is), ADR-340
(`bodyRadius` and the crowd), ADR-909 (the decider's habits), ADR-936 (roam targets are reachable)
**Found by:** this stream's full CPU suite: `test_glowmere_multicam.cpp`'s "The multicam's five deciders
move, and they do not walk through each other" failed on ADR-936's branch, and probes of the scene
attributed it
**Implemented by:**
- `InterestConsiderer::consider` (its `bodies_`, and `bodyOf`) and `optionDestination`
  (`src/entity/decision.{hpp,cpp}`);
- `trailingGap`, read by the `move` verb's arrival test (`ActionQueue::update`, `src/entity/action.cpp`).

**Tests:** `tests/unit/test_body_errands.cpp` (`[adr944]`: a subject that steps into the watcher's line
and stops, against the old errand's walk to a point; a subject that walks on slowly is arrived at,
against one that walks on faster than the watcher walks; the selector's judgement of a body errand,
against a greeting's; a scrub mid-errand); `tests/unit/test_glowmere_multicam.cpp` (the walk-through
arm, now on both of its seeds)

## Context

`interest` -- GV3's roam, graze and watch -- scores the things a body has noticed, and bodies are among
them: vane's watch weighs characters 4.8, sage's graze 1.8, ember's roam 1.5. Its errand walked to a
point: the stand-off `approach` metres this side of where the thing stood when the choice was made. A
place stays put. A body does not.

GV2-multicam's scene, played as the multicam test plays it (40 Hz, no song, seed 0), on ADR-936's branch:
vane chose to watch ember at 33.3 s and set off for its point 7 m short of where ember stood. Ember then
walked across vane's line to greet someone and stopped there, 42.6 s. Vane walked on to its point and
through ember: 0.26 m apart at 45.0 s, where the test that guards against bodies walking through each
other wants more than 1.2 m (two bodies of 0.9 m). ADR-936 had not caused it -- the first difference is
at 12.8 s, where vane no longer chooses to watch tide across the river -- but moved the seed onto it.
The engine before ADR-936 does the same at the test's other seed: vane watching ember, 0.73 m. That seed
was never asserted.

`social`'s greeting met this in Phase D and walks to the body ("aimed at the point it stood on when the
choice was made, the greeting 'completed' 9.5 m short of a warden who had walked on"), and `investigate`
looks at a body where it is. `interest` was the one considerer still aiming at where a body had been.

## Decision

**An `interest` errand whose subject is a body walks to the body.**

- **The walk** is a `move` to an `EntityRef`, which re-aims its end at the body every step (a body that
  walks away is followed; one that walks into the line is met), and it is over within `approach` of the
  body -- never inside the two bodies' room, their radii summed. The look at the end of the errand is at
  the body where it is, as `investigate`'s and `social`'s are.
- **What must be reachable** (ADR-936) is the ground the body stands on now: there is no stand-off to
  move, because the walk ends wherever it has got to.
- **ADR-909's habits still hold it.** `optionDestination` judged any walk after a body as going "nowhere
  this can name", which would have exempted these errands from the loop memory and the turn-back rule
  their walks to a point were held to. An `interest` option says what its subject is (`kind`, Character)
  and where it is (`target`), so it is judged there, with the walk's own tolerance. A greeting, which
  follows a body too and says neither, is judged nowhere, as before.

**A walk after a body that is moving off arrives.** The `move` verb brakes into its goal at the fastest
speed from which it can still stop at its tolerance, v = sqrt(2 a d). Behind a goal receding at v it
therefore settles v^2 / 2a short of the tolerance, and never arrives, however slowly the goal moves.
Walks to places never met it. Walks to bodies did: on GV2-multicam with the rule above, rook held
8.005 m behind a horse it had walked to (approach 8) for 4.4 s, gliding at 0.26 m/s on its idle clip,
until its post called it home; sage stood 6.0 m from the drifting saucer and, when it flew, chased it
until the walk gave up "stuck". The arrival test now counts that gap (`trailingGap`): the goal's
recession since the last step, capped at the walker's own pace (the fastest it could follow), squared
over twice its deceleration, plus the step the goal took before the test read it.

- **Nothing else changes.** A goal that is still, or coming nearer, leaves no gap, so every walk to a
  place, and to a body standing, arrives exactly as before.
- **A body that leaves faster than the walker walks is not arrived at by leaving.** The cap is the
  walker's own pace, so the gap is at most its stopping distance at that pace and a step (0.61 m for a
  3 m/s walker at the default deceleration, 8 m/s^2), and a walker further behind than that follows
  until the stuck clock gives up, as a greeting does.
- **Walks that slow into a body** over `arrival` metres (Phase D §14: greetings, `investigate`) settle
  further back behind a body walking on than the braking's gap, and are unchanged by it -- except behind
  a body creeping at under a fifth of the walker's pace, where the braking is what holds them, and which
  they now reach.

**What it does not do.** Two walkers crossing each other's path at walking pace still pass as close as the
crowd push lets them (ADR-340's separation yields at `max(speed, 1)` m/s, so a 3 m/s walker overlaps a
standing body by up to about a metre), and a walker passes a standing body that is not its errand's
subject the same way. That needs mutual avoidance, and this stream measured it rather than built it
(Consequences). A `move` to a body is routed once, when it starts (ADR-932).

## Consequences

- **On the fixtures** (`test_body_errands.cpp`):
  - A subject that walks into the watcher's line and stops: the watcher chose it, came no nearer than
    4.16 m, and its walk ended 6.92 m from the subject (approach 7). The old errand -- a walk to the
    stand-off from where the subject stood -- came within 0.27 m.
  - A subject that walks on at 0.4 m/s once the watcher is 12 m from it: the walk ended 7.02 m from it
    at 8.40 s, while it walked, and the watcher stood (0.00 m in the next 2 s). Before `trailingGap` the
    watcher trailed it at 7.01 m, 5.6 s within half a metre of its approach, and gave up "stuck" at
    12.30 s. Control: a subject walking on at 4.5 m/s, faster than the watcher's 3, is not arrived at;
    the watcher came no nearer than 11.25 m.
  - The selector judges a body errand at the body (ADR-909's habits), and still judges a greeting
    nowhere.
  - A scrub at 4 s and 9 s lands on the play.
  - On the code before this ADR the body-errand cases fail: 12 assertions, with the multicam arm below
    (`build/behave-runs/behave-adr944-control.log`); and before `trailingGap`, the walk after a subject
    walking on (`behave-adr944b-control.log`).
- **The multicam test** (`test_glowmere_multicam.cpp`, 40 Hz, no song) now asserts, on both of its seeds,
  that no two bodies come within 1.0 m and no body walking an errand to another comes nearer it than their
  two radii; seed 0 keeps its 1.2 m. Nearest any two bodies came: seed 0, 0.26 m before this ADR -> 2.12 m;
  the other seed, 0.81 m (0.73 m on the engine before ADR-935) -> 1.13 m, ember brushing past vane as it
  left the glow they had both walked to -- two walkers crossing, below. The nearest any errand came to its
  subject: 6.45 m (sage, at the saucer), against 0.26 m and 0.81 m before (vane, walking through ember).
- **On the films** (ADR-935's runs: 226 s at 60 fps, the whole-film cast trace at 20 Hz and ADR-910's
  quality report; before `983221a9`, then ADR-935 and 936 at `9aadb424`, then this branch's head
  `1670f07d`, which adds this ADR). The closest any two bodies came is over the whole film, aliens and
  animals alike, leaving out a set piece's carry up the beam (`build/behave-runs/behave-closest.py`); an
  errand to a body is one whose chosen option names a body in the trace, and "nearest" is how near the
  two came while the errand held, its look included (`behave-errands.py`):

  | film | closest any two bodies came | encounters under 1.4 m | errands to a body: nearest to its subject |
  |---|---|---|---|
  | GV3 as generated | 0.76 -> 1.09 -> 1.16 m | 2 -> 2 -> 1 | 0.76 -> 2.06 -> 4.03 m |
  | GV3, go and see | 0.76 -> 1.09 -> 1.45 m | 1 -> 2 -> 0 | 0.76 -> 2.06 -> 4.03 m |
  | GV3, gv3-cast's iteration 2 | 0.89 -> 0.58 -> 0.50 m | 2 -> 3 -> 2 | 2.06 -> 2.01 -> 4.03 m |
  | GV2-multicam | 0.57 -> 0.57 -> 1.51 m | 2 -> 3 -> 0 | 1.66 -> 1.36 -> 1.82 m |

  - **The walk-throughs this ADR is about are gone from every film:** GV3's rook walking into ember, which
    stood greeting, 0.76 m at 64.80 s, on its errand to ember; GV2-multicam's vane into bull-18, 1.36 m
    at 214.85 s on ADR-936's engine. Every errand to a body now ends within its `approach` of the body
    (6-8 m on GV3, 7 m for GV2's vane). The nearest a subject and its walker came while an errand held
    is 4.03 m on GV3 and 1.82 m on GV2-multicam, both vane and bull-18 at the start of the film: vane's
    walk ended 7 m off, and the bull, wandering, walked up to it (GV3) and past it (GV2) while vane
    stood and looked -- an animal's walk, which nothing steers round a standing body (below).
  - **What is left under 1.4 m is two walkers crossing** (below): GV3 as generated, horse-2 and vane
    1.16 m at 186.40 s; iteration 2, ember and rook 0.50 m at 201.65 s, both at 3.4-3.5 m/s, and
    horse-22 and vane 0.59 m at 204.75 s.
  - **Errands to bodies end where they meant to.** GV3 as generated has 30 of them, go and see 30,
    iteration 2 29, GV2-multicam 17; the walker completes within its `approach` of the body (6-8 m on
    GV3). One fails on GV2-multicam: sage after ember, which walked away at sage's own pace, followed for
    14 s and gave up "stuck" at 175.9 s, as greetings do. Without `trailingGap` (this ADR's first
    commit, `06ad0b8e`) a walk after a moving body held at the edge of its approach instead of ending:
    GV2-multicam's rook 4.6 s behind horse-22, sage at the drifting saucer until it flew and the walk
    became a chase; on GV3 go and see, vane behind horse-2 and horse-22 for 2.3 s and 0.9 s. That
    film's turned-back stops were 29 and its A->B->A 9 (rook, on that trajectory, fell into a tug between
    its post and the cairn 86.8 m from it, which GV3's tuning allows); at the head they are 22 and 1.
  - **The films' totals, before -> head:** the aliens' failed errands GV3 as generated 17 -> 2, go and
    see 18 -> 2 (sage's greetings of a walking alien, "stuck", in both), iteration 2 21 -> 7 (six
    greetings and sage's walk after rook, all "stuck"), GV2-multicam 17 -> 3 (ember's `holdPost` walk
    "blocked" in the shallows pocket at (-53.9, 59.3) that GV3 has too, rook's reaction to the beam onto
    ground no walker can stand on, and sage after ember). Reversals / turned back / A->B->A, aliens: GV3
    as generated 5 / 22 / 4 -> 3 / 23 / 1; go and see 4 / 20 / 2 -> 3 / 22 / 1; iteration 2
    2 / 19 / 2 -> 2 / 20 / 0; GV2-multicam 6 / 32 / 6 -> 6 / 30 / 5. The longest pacing runs (ADR-933's
    measure) are 3 or under everywhere (GV3 rook's 6 on the film as generated is 1): GV3's sage, 3 stops
    over 39 s from 107 s (a flight from the E4 beam and two roams), and with "go and see" vane, 3 over
    69 s (its post calling it home after errands it completed). ADR-935's and ADR-936's results hold at
    the head: the aliens' stuck time is 0.0-0.1 s in all four films, their walk on the spot 0.0-0.8 s.
- **Two walkers crossing still pass through each other's room.** Nothing steers a walker round another
  body: ADR-340's crowd separation is all there is, and it yields at `max(speed, 1)` m/s. Of the nine
  approaches under 1.2 m across the four films on the engines before this ADR, six were two walkers
  crossing (GV3 ember and sage 1.07 m at 208.25 s; ember and tide 1.09 m at 108.80 s; iteration 2 tide
  and cow-12 0.89 m at 54.95 s, ember and cow-12 0.98 m at 88.75 s, ember and rook 0.58 m at 206.60 s,
  vane and horse-22 0.96 m at 219.65 s) and three a walker into a body standing (GV3 rook into ember,
  which this ADR fixes; tide into rook 1.14 m at 112.30 s; GV2-multicam tide into ember 0.57 m at
  95.50 s, neither's errand the other). The three at the head are all crossings. A walker that is not
  on an errand to a standing body walks into it the same way; that and crossing need mutual avoidance
  in the walk's steering, which is the next engine round's.
- **Scenes whose behaviour changes:** every `interest` decider that notices bodies (the GV2 family, GV3,
  the Character Intelligence Lab's roamers, the autonomy demo) now walks to a body where it is, stops
  within `approach` of it and looks at it; the films' trajectories part from the first such errand
  (GV2-multicam: vane's, from 1.45 s -- its first choice is bull-18). Every walk to something that moves -- a
  greeting, `investigate`, a staged walk to an entity -- now arrives behind it within `trailingGap`;
  a walk that slows into its goal over `arrival` metres only behind a body creeping at under a fifth of
  its pace.
