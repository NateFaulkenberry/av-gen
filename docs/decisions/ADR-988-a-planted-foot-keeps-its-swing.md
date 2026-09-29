# ADR-988: A planted foot keeps its swing

**Status:** Accepted
**Date:** 2026-09-29
**Found by:** the GV3 art pass. Round 0 found the aliens' ground layers planting each foot at its standing height
on every frame and left it to the owner (round 0's decision 2, "the aliens' walking foot barely lifts"); revision
round 1 took it up (item 6, relayed by the coordinator): raise the aliens' swing-foot clearance, fixed at the cause
**Implemented by:** `PoseLayer::keepSwing` and `PoseLayer::toeJoint` (`plantFoot`, the sole's turn and the toe
floor in `PoseLayerStack::apply`, the stride layer's standing height) in `src/scene/pose_layers.{hpp,cpp}`; the
`keepSwing` and `toe` layer keys in `src/scene/composition.cpp`; GV3's aliens in `tools/gv3/cast.py`
(`ALIEN_KEEP_SWING`)
**Tests:** `[adr988]` in `tests/unit/test_stride_warp.cpp` (the clip's foot kept in the air and its control, half
of it, the toes kept out of the ground and its control, a standing foot planted the same either way); the probes
`[.probe][alien][swing]` in `tests/unit/test_alien_locomotion.cpp`; `avgen_foot_probe`'s swing clearance

## Context

A ground-driven foot layer puts its foot on the plane under it at the height the foot stands at in the rest pose
(`plantOnPlane` with `restTipHeight`), with its sole laid on the plane (`footAlign` 1). It does that on every
frame the body is grounded -- stance, heel-off and swing alike -- because nothing in the layer knows which is which.
So the swing is flattened out of the drawing: the clip lifts the foot and the layer puts it back. It is not the
clips (the aliens' `Walking` lifts the ankle 0.124 model units and the toes 0.135, `Running` 0.336 and 0.319) and
it is not the cast's 1.94x scale (every height involved is in model units, scaled once by the node).

Measured on GV3's film, per step (a run of frames in which the drawn foot moves over the ground faster than 1.5
times the body), all five aliens: the ankle's highest was 0.265-0.267 m above the terrain against a standing
0.267, the toes' 0.038-0.042 against 0.040. On the flat, isolated, the layer drew 0.021 of the walk's 0.124 at the
ankle and 0.050 of the run's 0.336. And where the rest pose's vertical standing height meets a steep bank it is
not the height across the bank, so a planted sole went into it: frames with a foot more than 2 cm under the
terrain, 3,585 over the film (Rook 2,655, down to 0.151 m on a 42-degree bank).

Keeping the clip's lift on its own was not enough, three ways, each measured:
- **The sole.** Laid flat while the ankle rose, a foot the clip rolls onto its toes (78 degrees at heel-off) lifted
  flat, heel and toes together.
- **The body's reach solve.** ADR-544 lowers the body so a leg can reach, and a foot hanging from the root goes down
  with it -- so the clip's lift, read off the lowered foot, was eaten by the drop (Rook's right ankle held at its
  standing height at 3.9 m/s while the left was clamped).
- **Pointed toes.** Where the drawn ankle is lower than the clip's -- a stride shortened by the stride layer, a leg
  at the end of its reach, a bank -- the clip's pointed toes go into the ground: with the lift and the clip's sole,
  the frames under the terrain rose to 6,950.

## Decision

`keepSwing` on a ground foot layer (0 to 1; 0, the default, is every scene before it) keeps that share of the
clip's own foot in the air, carried onto the ground:

1. **The height.** The foot is put on the plane at its standing height plus the share of the clip's height above
   it, both measured ACROSS the plane (the standing height over the normal's height is the vertical drop, capped at
   a 60-degree slope). The lift is read in the body as the reach solve moved it, so a lowered body is not a foot
   coming down; the stride layer measures its own lift scaling there too, for a foot whose lift is kept (anywhere
   else the plant replaces the height and the stride layer is unchanged to the bit).
2. **The sole.** The foot is turned by the plane's tilt (the arc from the model's up to the normal) instead of
   having its sole laid on the plane: flat stays flat on a slope, and a heel the clip raises stays raised.
3. **The toe floor.** With a `toe` joint named (one hanging from the foot), the foot is taken back toward laid only
   as far as keeps the toe at its own standing height over the plane (a bisection between the two turns). Turning
   the foot up in the vertical plane through ankle and toe was tried first: a toe the clip points past straight down
   swung over to the back of the ankle, 0.47 m in a frame.
4. **GV3's aliens** keep all of it (`ALIEN_KEEP_SWING` 1.0, toes `toes_01.l/.r`). Nothing else changes.

## Consequences

The whole film, all five aliens, per step, before and after (metres above the terrain; standing: ankle 0.267, toes
0.040):

| | ankle, median (p10-p90) | toes, median (p10-p90) | frames >2 cm under | lowest |
|---|---|---|---|---|
| before | 0.265-0.267 (0.24-0.28) | 0.038-0.042 (-0.02-0.045) | 3,585 | -0.151 |
| after | 0.385-0.398 (0.30-0.50) | 0.091-0.098 (0.04-0.32) | **5** | -0.070 |

On the flat the drawn foot is the clip's: the walk lifts the ankle 0.24 m and the toes 0.26 m at GV3's scale, the
run 0.65 and 0.62 (the aliens do not run in this film). For a body 3.2 m tall -- 1.8 times a person, whose ankle rises
about 0.15 m walking and whose toes clear the ground by 0.10-0.15 m early in the swing -- 0.27 m at the ankle and
0.18-0.27 m at the toes would be the walk; the film's medians sit under that because most of its walking is slow and
the stride layer shortens and lowers slow steps, and its p90s reach it.

- **No slide at contact.** The plant's horizontal place is the clip's, as before: on the frames where a foot is
  down and flat, the ankle slides 3.45-11.84 mm a frame without the kept swing and 3.48-11.88 with it (+0.3 to
  +0.9%); over the whole film the ankle's path moves a median 2 mm (the reach solve's lateral answer shifting with the
  lifted targets). ADR-982's start slide is a horizontal quantity, and is unchanged with it.
- **The simulation is unchanged**, and so is GV2: its fingerprint (ADR-623's trace) is 0x7e0ba810e539e375, and every
  scene that does not name `keepSwing` is bit-identical. GV2's aliens keep their flattened swing; the same two keys
  would give them theirs.
- **The ~10 mm a frame the aliens' planted ankles slide** (their motion provider's, before and after) is found, not
  changed.

## Rejected alternatives

- **Planting only in the clip's contact spans** (letting the swing through untouched): the aliens are posed by their
  motion provider, whose clip time is not the player's; and a swing foot over uneven ground still needs the ground
  under it.
- **The lift without the sole:** the foot lifts flat -- the owner asked for feet that leave the ground, not a march.
- **Raising the lift past the clip's:** the clip is the animator's step, and restoring it already lifts the ankle's
  median step 0.12 m where it lifted none.
- **Making it the default:** it changes every walking rig in every scene and GV2's fingerprint; GV3 is the one asked for.
