# ADR-987: A walking hoof stays where it lands

**Status:** Accepted
**Date:** 2026-09-29
**Found by:** the GV3 art pass, revision round 1 (`docs/glowmere-valley-3/art-pass/00-brief.md`): the owner, "go
ahead and foot lock 4 legged rigs" -- round 0 had measured the farm animals' hooves sliding and recorded foot
locking on quadrupeds as the fix, not done
**Implemented by:** `PoseLayer::footLockMode` (`FootLockMode::Stance`, `PoseLayer::StanceMemory`, `kStanceGap`,
`kStanceRelease`) in `src/scene/pose_layers.{hpp,cpp}`; `ContactMode::Sweep` in `src/scene/motion_analysis.{hpp,cpp}`;
`Composition::AnimationSink::driveLayers` and the `footLockMode` / `contactMode` keys in `src/scene/composition.cpp`;
`AnimationPlayer::setSpeed` (`Playing::held`) in `src/scene/animation.{hpp,cpp}`; GV3's farm animals in
`tools/gv3/cast.py` (`HOOF_LOCK`, `HOOVES`, `STROKE_SPEED`, `STROKE_RATE_MAX`, `ANIMAL_UPDATE_HZ`)
**Tests:** `[adr987]` in `tests/unit/test_stride_warp.cpp` (four cases: held in the world while the body walks and
turns, the clip's outside the span, handed back over the release, and trusted only by the next frame of the same
stance -- a seek back, a cull, the next stance and the same span begun again each start from the clip's foot),
`tests/unit/test_motion_analysis.cpp` (the sweep contact) and `tests/unit/test_skeleton.cpp` (a speed of zero holds
the clip); the probes `[.probe][farm][footlock]` in `tests/unit/test_farm_locomotion.cpp`; `avgen_foot_probe`

## Context

The farm pack ships one clip per animal, an in-place `Walk`, and nothing locked its hooves. ADR-557's lock
exists (`footLock`, the `Velocity` mode) but no scene switches it on, and on an in-place walk cycle it would count
the body's travel twice: it pushes the foot the clip poses NOW back by the travel since contact, and a walk's stance
foot is already sweeping back under the hips. Measured on the r0 film (every farm animal but the two the saucer
takes, 1,724.6 m walked, the film's own 30 Hz rig rate):

1. **The clip's stroke is uneven.** A stance hoof sweeps back six times faster at one moment than another, and each
   hoof at its own mean; rate matching can match one speed, so every hoof skated. The first 70% of each stance slid
   2.05 m per metre the body walked; horse-2 on a straight walk, 13.95 mm a frame.
2. **The walk speed the rate is matched to is the stroke's fastest moment.** ADR-240 measured each hoof at its
   lowest; over the whole backward stroke the horse's hooves go back at 1.131 model units a second, not 1.628, so a
   rate-matched horse out-walked its legs by 44%.
3. **The height contact test finds pieces of each stance.** This pack scuffs its hooves forward at their lowest in
   the swing and lifts them while they still push back, so the default test found a quarter of each stance, in
   pieces (the horse's right fore in two, the bull's left hind in none).
4. **Every stop snapped the legs to frame zero.** `AnimationPlayer::setSpeed(0)` rebased the clock to `now`, and a
   clock stored as (start, speed 0) reads its clip's first frame. The farm pack's `idleRate` is 0 (ADR-213: "a statue
   caught mid-stride"), so every stop moved the hooves to frame zero's pose in one frame -- 0.37 to 1.37 m, 16 to 20
   times per horse over the film -- and the next walk began from there. Latent since the player's first commit
   (d54b765e); nothing set a speed of 0 until ADR-213.

A first, derived form of the lock (the touchdown point the clip gives, moved back by the clip's elapsed contact
time at the walk's authored speed, turned by the turn rate over the elapsed time) was exact on a steady straight
walk and wrong everywhere else. The seam carries what the body is doing now, and a stance lasts half a second: a
bull that began to turn mid-stance had its hoof thrown 1.16 m in one frame, and one standing at the film's start
with its clip at frame zero, inside a stance that wraps the loop, had its hoof held 0.56 m from the clip's for a
walk that never happened.

## Decision

1. **A `Stance` foot lock** (`footLockMode: "stance"`). From the first posed frame of a contact span the foot stays
   at the point in the WORLD where the clip had it then, whatever the body does -- walking, turning, stopping --
   and over the span's last 30% (`kStanceRelease`) it is handed back to the clip, so it leaves the ground from where
   the clip lifts it. Outside the span the clip's swing is left alone, and only the horizontal is held, at the height
   the clip gives it.
2. **That point is remembered, and the memory cannot outlive its stance.** ADR-557 derived its anchor because a
   remembered one would survive a scrub into a timeline that abolished it. This one is trusted only by the next posed
   frame of the same stance: the same span, not begun again, later in the timeline and within `kStanceGap` (0.25 s).
   A seek, a step back, a cull or a new stance finds nothing to trust and holds from where the clip has the foot on
   that frame -- so a scrubbed frame shows the clip's own foot, and a render, which plays every frame from its first,
   holds every stance from its touchdown. `driveLayers` hands the layer which span it is in and the rig's model space
   in the world; the lock is a stance lock only with a ground plane and weight, so a body in a beam is not held.
3. **A `sweep` contact mode** (`contactMode: "sweep"` on a node's animation): a foot is in contact while it goes back
   under the body, toward the rig's -Z, whatever its height -- the stance of an in-place cycle.
4. **A speed of zero holds the clip where it is.** `Playing::held` keeps the clip seconds `setSpeed(0)` found, and the
   next non-zero speed carries on from them. Every other clock is computed exactly as before.
5. **GV3's farm animals** get a stance lock on each leg (`UpperLeg -> LowerLeg -> Hoof`), sweep contacts, a walk speed
   that is each species' mean stroke speed at the cast's 1.94x (horse 2.194, cow 3.286, bull 3.345 m/s; ceiling 1.6),
   and are posed on every frame (`updateHz` 0): at 30 Hz in a 60 fps film a held hoof is drawn with the previous
   pose on every other frame while its body moves on, one frame's travel ahead and back again.

## Consequences

Measured with `avgen_foot_probe` over the whole film, the ten farm animals the saucer does not take, the r0 film
against the r1 film (each at its own rig rate):

| | r0 | r1 |
|---|---|---|
| held part of each stance (first 70%), metres slid per metre walked | 2.048 | **0.032** |
| release part (last 30%, the hoof coming off the ground) | 0.629 | 0.636 |
| standing still, mm a frame (worst one-frame move of a standing hoof) | 16.95 (1.946 m) | **2.66 (0.206 m)** |
| at walk starts, slide before the first step per metre of body | 0.086 | **0.045** |
| horse-2, first minute: straight walk / turning walk / standing, mm a frame | 13.95 / 21.59 / 1.60 | **0.18 / 0.26 / 0.01** |

On an ideal straight walk (the probe `a horse's held hooves on a straight walk`) the held part goes from 11.95 to
0.12 mm a frame. The worst standing move left is a pivot's swing. What the release takes up is the difference
between the world and the clip's stroke at lift-off, while the hoof is lifting.

- **The simulation is unchanged:** every body travels the same distance to the millimetre. The aliens are identical
  (2,409.993 m travelled, the same foot measurements, in every run).
- **GV2's behaviour fingerprint (ADR-623's trace) is unchanged:** 0x7e0ba810e539e375. No GV2 farm animal stops in its
  first three seconds. Later in GV2, and in every scene with a farm animal whose idle freezes its walk, a stop now
  holds the pose it stopped in instead of snapping to frame zero: the frame-zero snap was a defect everywhere, and
  ADR-213 had described the frozen pose, not the snap.
- **Frame cost:** the whole-film simulation, 13,531 frames without drawing, took 6.5 s more than r0's (0.48 ms a
  frame, CPU) for 48 locked legs and twelve animals posed on every frame instead of every other. Most of it is
  `PoseLayerStack::ensureModel` re-diffing the skeleton per layer, as it does for every foot layer. A stance-locked
  layer skips ADR-551's per-foot terrain query and whole-skeleton pose (it holds the clip's height and reads no
  ground): that halved the lock's first measured cost.
- A scrubbed frame shows each hoof where the clip has it on that frame, not where the played film holds it -- the
  same class of difference as the player's own clock, which a seek extrapolates rather than replays.

## Rejected alternatives

- **ADR-557's `Velocity` lock on the walk:** counts the travel twice on an in-place cycle.
- **The derived anchor (the first form):** measured above; the seam has no record of what the body did since
  touchdown, and a turn begun mid-stance is ordinary here (the animals turn on 1.8-2.2 m circles).
- **The entity tier remembering its path for the pose tier to read back:** the touchdown instant is a clip time, and
  the clip's clock is the player's, so the entity would have had to integrate a second copy of the player's clock --
  two clocks that must agree.
- **A matched stroke speed alone:** it lowers the release slide but leaves the uneven stroke's slide (1.095 m per
  metre with it and no lock).
- **An idle clip for the pack:** an asset question (ADR-213, ADR-622), and it would not have locked a walking hoof.
