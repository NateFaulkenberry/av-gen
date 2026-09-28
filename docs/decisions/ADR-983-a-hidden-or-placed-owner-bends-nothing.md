# ADR-983: A hidden or placed owner bends nothing

**Status:** Accepted
**Date:** 2026-09-28
**Found by:** the GV3 targeted art pass, item 4 (`docs/glowmere-valley-3/art-pass/00-brief.md`): a spacetime warp
around the UFO, which "must not introduce distracting geometry/artifacts"
**Amends:** ADR-703 (Space Warp and the DF owners' drawn view), ADR-911 (placements)
**Implemented by:** `world::NodeView::visible`, `scene::Composition::nodeView`; `world::HistoryBank::velocity`,
`acceleration` and `placementStart`; `world::EffectSceneQuery::nodePlacedSince` (the engine's adapter answers it
from HIST); the Space Warp, Gravitational Lens, Heat Shimmer and Velocity Distortion producers
**Tests:** `tests/unit/test_distortion_frame.cpp`, `tests/unit/test_history_bank.cpp`, `tests/unit/test_shockwave.cpp`
(`[adr983]`)

## Context

GV3's two crafts are staging bodies. Between their set pieces they are hidden (staging's `hide` writes the node's
`visible` parameter), and each set piece takes its craft to where it appears with a hidden move a tenth of a
second long (`transitSeconds` 0.1) and then shows it there, which ADR-911 counts as a placement.

Putting a Space Warp and a Velocity Distortion's wake on the crafts showed three defects, none of them GV3's:

1. **A hidden owner still bent the view.** `NodeView` said where a node was and how big, but not whether it was
   drawn, so a warp on a hidden craft bent an empty patch of sky wherever the craft waited.
2. **A placement read as motion.** HIST differenced the newest sample against the one a grid step earlier
   whichever placement each belonged to. On the frame a craft appeared, its velocity was the hidden move divided
   by a frame, about 5,000 m/s, and a Space Warp fitted to that velocity stretched along it. (The
   `entity.<name>.speed` signal read the same number.)
3. **A wake followed the path the body never flew.** A Velocity Distortion reads its owner's drawn positions back
   over its persistence, so for a second after each appearance it laid a lens tube back along the hidden move,
   hundreds of metres across the sky.

The follow camera already respects placements (it reads its subject's history back no further than the last
change, `scene::SubjectTrail`). The effects did not.

## Decision

1. `NodeView` carries `visible`: the node and every ancestor visible, as the last flattening had them
   (`Composition` records it in the node's range in the same per-frame pass that records the drawn transform). The
   four DF types whose field surrounds the owner as it stands now (Space Warp, Gravitational Lens, Heat Shimmer and
   the Velocity Distortion's wake) draw nothing while it is hidden. Shockwave and Ripple are unchanged: a front is
   released where the owner was, and outlives what the owner does next.
2. HIST's `velocity` and `acceleration` difference only within the newest sample's placement: a body placed on the
   newest sample is at rest, and one placed a few samples ago differences over what it has done since (like a ring
   younger than a step). The engine's drawn-path velocity for an XFORM-offset owner follows the same rule.
3. `HistoryBank::placementStart` and `EffectSceneQuery::nodePlacedSince` answer where the current placement begins.
   A wake reads its path no further back than that: its segments before the placement have no length and are
   skipped, so the wake grows from where the body appeared.

## Consequences

- GV3's crafts can carry their warp: nothing bends while a craft is hidden, and nothing jumps when it appears.
- Every `entity.<name>.speed`, `.velocity.*` and `.acceleration` signal reads 0 on the frame of a placement
  instead of the placement's distance times the frame rate. The motion inside a placement is unchanged, bit for
  bit.
- A scene that shows a hidden body with a Gravitational Lens or Heat Shimmer on it now sees the field appear with
  the body rather than before it.

## Rejected alternatives

- **Suppressing the warp in GV3's data** (keys on the strength around every show and hide): it would have to track
  every set piece's timing, and it would leave the defects in the engine for the next scene to find.
- **Making `nodeDrawnPosition` fail across a placement.** Its other readers (Trail's offset correction, Discharge's
  release point) ask where the node was drawn at an earlier instant, which a placement does not change. Only a
  reader that treats the history as one continuous path needs the boundary, so it is a separate question.

## Revisit triggers

- A Trail on a body that is placed: it still draws through the placement (its own history walk, unchanged here).
