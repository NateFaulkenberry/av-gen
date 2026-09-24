# ADR-711: The Tree of Life's storm self-shadows, at 4 steps and strength 0.45

**Status:** Accepted
**Date:** 2026-09-24
**Resolves:** ADR-708's "Revisit when the step redistribution lands"; plan §2 item 3.
**Depends on:** ADR-710.
**Implemented by:** `examples/treeisland/tree-of-life-floating-island.scene.json` (`environment`:
`volumeShadowSteps` 4, `volumeShadowStrength` 0.45). Both deliverables read it: the day project sets
no shadow row of its own and the night project sets no volume rows, and each renders byte-identical
to its 4 @ 0.45 arm.

## Context

ADR-708 found self-shadowing is the largest single improvement to the storm's read where the march
samples it densely, and grain on the hero at every shadow step count until 256 march steps. ADR-710
put the shipped 32 steps inside the medium. Its instruction was: re-run the hero at 4 shadow steps
and the shipped step count, and start tuning at strength 0.3-0.6.

## What the renders showed

- **The hero is clean with the shadow on.** `sheet-item2-hero.png` (before/after ADR-710, shadow off,
  4 @ 0.4, 4 @ 1.0) and `sheet-item3-strength-day.png` (off, 4 @ 0.3 / 0.45 / 0.6, 2 @ 0.45). The
  column gets a pale lit side and a shadowed side, and the helical bands read along the whole length.
  Grain at matched luminance: 0.037 of the medium at 4 @ 0.4, against 0.29 before ADR-710 (ADR-710's
  table). A faint fine grain remains in the shadowed cyan, below what the full frame shows.
- **Strength 0.3-0.6 barely differ on the hero**; the shadow saturates early in a column this thick.
  0.45 is the middle of ADR-708's range and keeps the lit side from going flat. Two shadow steps
  at 0.45 read lighter and less banded than four; four is kept.
- **The shadowed side is the storm's cyan EMISSION, not dark smoke**, as ADR-708 predicted: the hero is
  still emission-led and "too bright". That is item 4's tuning, not this one's.
- **Night:** almost no visible change from the shadow (`sheet-item3-strength-night.png`); the night
  column itself was dimmed by ADR-710 (see there).
- **Labs, not shipped** (`sheet-item3-labs.png`): 4 @ 0.45 brings back the form ADR-710's converged
  march took away -- a dark storm base, the showcase bowls' texture, the lab column's bands -- but
  crushes the lab's wall cloud nearly black. The labs are cited evidence scenes (ADR-580, ADR-707) and
  are left as they are; their re-tune belongs with ADR-710's look change.

## Cost

`volume.march` on the day project, 1920x1080, default tier: 4.1-4.4 ms without the shadow, 13.8-14.0
ms with it (ADR-710's table; GPU frame p50 ~16 -> ~25 ms). The shadow runs once per in-medium sample,
and ADR-710 put ten times as many samples in the medium. The deliverables render at the Offline tier,
where this is affordable; interactive editing of this scene pays ~10 ms. Still less than half of
ADR-708's clean-by-brute-force 256-step arm (33 ms).

## Revisit when

- The hero's emission/brightness is re-tuned (item 4): re-judge the strength with the shadowed side
  reading as smoke rather than glow.
- Interactive cost matters: 2 shadow steps halve the shadow's share and read slightly lighter.
