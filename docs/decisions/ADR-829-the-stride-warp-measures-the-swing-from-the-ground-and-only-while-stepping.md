# ADR-829: The stride warp measures the swing from the ground, and only while stepping

**Status:** Accepted
**Date:** 2026-09-25
**Answers:** the owner's report on the review build: "Rook's legs snap up off the ground and his
knees twitch" at 3 s when he turns (every AI-on arm), and at 12 s with AI off
**Related:** ADR-827 (review build), ADR-826 (analyzer); the foot IK and stride layers in
`src/scene/pose_layers.cpp`
**Implemented by:**
- `PoseLayerStack` stride solve (`src/scene/pose_layers.cpp`), with a rest-pose foot height stored
  when the stride layer binds;
- the Stride driving in `Composition`'s animation sink (`src/scene/composition.cpp`).

**Tests:** `tests/unit/test_stride_warp.cpp` (`[stride][layers]`), including the film case
"Rook's feet stay on the ground through the turn at 3 s" (`[glowmere][benchmark]`).

## What the data said

It was not a yaw snap: logged per frame, Rook's heading turns at 2.34° a frame, rate-limited. The
legs came from the stride warp.

- **The warp ran on a turn-in-place clip.** The ratio is the gait's foot slip times the locomotion
  plan's `phaseStride`, the start ramp. `footSlip` returns 1 for non-stepping activities, but
  `phaseStride` did not, so at 3.217 s, still on `Idle_turn`, the ratio went 1.000 -> 0.350 in one
  frame.
- **The warp lifted planted feet.** It scaled the foot's whole excursion from the hips, including
  the vertical. A foot below the hips, scaled by 0.35 with lift, is pulled toward the hips: up. At
  3.233 s the feet rose 0.113 -> 0.236 m, the foot IK went Applied -> Clamped (it cannot pull a
  foot down that far), and the knees flicked. The same happens wherever the ramp meets a turn; at
  12 s with AI off it is the same mechanism.
- **The warp arrived at full strength.** Even once walking, the first `Walking` frame moved each
  foot 0.15 m, because the pose was still mostly the outgoing clip.

## Decision

1. **The swing is measured from the ground.** When the stride layer binds, it records the foot's
   height in the rest pose. The warp scales the forward (z) excursion from the hips by the ratio,
   leaves the sideways (x) excursion alone (the stance keeps its width), and scales the height
   *above the standing height* by the lift dial. A planted foot stays on the ground at any ratio.
   The old test "the lift scales the swing" encoded the hip-pivot semantics and was rewritten.
2. **The warp applies only while stepping.** On any activity but Walk/Run the ratio is 1.
3. **It fades in with the clip.** While the player blends into the stepping clip, the warp is
   weighted by that clip's blend weight. This is the blend the pose already has, not smoothing
   added to hide a jump: the warp follows the fraction of the pose that is a step.

## Evidence

On the multicam film, 2.8-4 s, per posed frame (the rigs pose on the 30 Hz grid):

| | worst foot height step | worst foot move, 2.8-3.5 s | foot IK |
| --- | --- | --- | --- |
| before | 0.132 m | 0.207 m | Applied -> Clamped |
| after | 0.0072 m | 0.041 m | Applied throughout |

The film case asserts < 0.03 m and < 0.06 m; it fails on the old code. Renders: review folder
12-motion-stack, `09-rook-turn-fixed-*`.
