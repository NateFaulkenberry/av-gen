# ADR-891: A continuous take joins where an edit cuts

**Status:** Accepted
**Date:** 2026-09-25

Extends ADR-158 (a directed shot's aim follows its hero) and ADR-249 (Song Mode's camera track).
Leaves ADR-245's camera precedence unchanged.

## Context

Reported on `examples/world/glowmere-valley-2-multicam.json`: *"if I change it to Continuous shot in
the Auto-director I am getting edits instead of a continuous shot."*

The film was played through the Engine at 30 fps after the panel's own call (`directEngine` with
the mode changed). Every frame-to-frame discontinuity was logged. There were three sources.

1. **The director's own take snapped its aim.** `installSequence` writes one aim-follow entry per
   shot that holds a subject. `applyDirectedAim` adds that hero's walk since the cut to the aim, and
   drops it to zero when the entry changes, on the grounds that a shot boundary is a cut. In a
   continuous take a boundary is not a cut. The eye stood still and the aim turned 29.7, 41.8, 25.2,
   39.1, 38.2, 81.0 and 72.1 degrees in one frame, at 11.83, 23.67, 112.27, 153.63, 189.10, 216.57
   and 221.43 s.
2. **Song Mode's camera track survived the change of mode.** The project is saved in Song Mode.
   Once Song has directed, choosing Continuous shot re-baked the framing through `installSequence`,
   which never touches the camera track. That left Song's 11 `Directed` camera shots in place,
   all locked, so the film cut between Hero Free Roam and UFO Watch on Song's schedule.
3. **The authored track and the event camera.** Valley Wide holds 0–7 s and 26–31 s (locked,
   authored), and UFO Watch takes the frame at each of the ten abductions. Those account for 18
   camera changes.

**Not a code regression inside the window checked.** The same probe, built at `e61778df`
(2026-09-21), gives the same seven aim snaps, the same eleven Song shots left behind, and the same
authored and event cuts. Sources 1 and 2 date from `1bc30100` (aim-follow, 09-14) and `6ca29d1c`
(Song Mode's camera track, 09-16). Source 3 dates from `e08d25c3` (ADR-245, 09-16).

## Decision

* **A continuous take's boundaries are joins.** `AimFollow` gains `joinInSeconds` and
  `joinOutSeconds`. `installSequence` sets them only in `ContinuousShot`, and only where the bake
  chained the boundary (this shot starts at the last one's end pose). The breakdown that the
  continuous take deliberately cuts on stays a cut. Across a join, `applyDirectedAim` eases from the
  outgoing shot's offset to the incoming one's over up to 2 s. It never takes more than half a shot.
  Both offsets are read from where the heroes are now, so a scrub still lands on the frame a play
  does. An edited sequence, Song Mode and every project saved before this write no join and behave
  exactly as before. The fields are written to `cameraAimFollow` only when non-zero.
* **Continuous and Edited leave no Song camera shots behind.** `directEngine` removes the camera
  track's `Directed` shots after a non-Song bake. Authored shots stay. This is the same rule
  `installSongDirection` follows.
* **Precedence is unchanged.** ADR-245 is explicit. Continuous shot is "the Auto-director's own
  cutting behaviour on *its* camera. It is not the shot track saying so." `docs/auto-director.md`
  names the event camera as how a Continuous cut sees the world's events. So source 3 is left as
  designed, and it is put to the owner as a question (below).

## Measured

`tests/unit/test_continuous_shot_multicam.cpp` (`[adr891]`) plays the whole film. On main it fails
in two places: 11 `Directed` shots are left, and the take has snaps. With the fix it passes, over
3,168 frame pairs of the director's take. The largest remaining turn is a smooth 14.3° a frame, a
baked pan at 61.1 s. Control arm: with the joins disabled, the seven snaps above come back exactly.

## Open question for the owner

Should **Continuous shot own the frame**, so that authored shots and event cameras don't cut it? Or
should it keep ADR-245's order, where a locked authored shot beats an event, an event beats an
authored shot, and an authored shot beats the Auto-director's camera? On this film, the 18 camera
changes that remain are all authored or UFO Watch.
