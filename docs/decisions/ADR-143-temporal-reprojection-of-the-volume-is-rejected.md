# ADR-143: Temporal reprojection of the volume is rejected, because a policy parameter already reaches its ceiling

**Status:** Accepted
**Date:** 2026-09-13
**Answers:** Phase F, task F3 — evaluate temporal reprojection of the volume
**Related:** ADR-035 (a tier scales samples and resolutions *only*), ADR-132 (temporal AA is not a
prerequisite elsewhere), ADR-139, ADR-140, ADR-141

## The question, stated so it can be answered

§28 defers temporal rendering broadly and the scope contract routes *temporal volumetrics*
specifically to Phase F. ADR-132 recently found that temporal AA is not a prerequisite for Phase C —
but that decides nothing here. Volumetric reprojection is a different technique with different
artefacts and a different payoff, and it deserves its own evidence rather than inheriting a verdict.

The technique: keep the previous frame's march result, reproject it with the camera motion, blend
the new frame's march into it, and march fewer samples (or fewer pixels) per frame because the
history carries the rest. Its payoff is the amortisation of **march work**, and nothing else.

## The evidence

**1. The march is the whole cost, so the target is right.** ADR-140: `volume.march` is 3.867 ms of
Constellation's 6.226 ms frame and 0.655 ms of Glowmere's 13.435 ms; the composite is one timestamp
tick on both. Reprojection aims at the only thing worth aiming at.

**2. But only about half the march is pixel work.** ADR-141: a sixteenth of the pixels costs
0.35–0.61 of the march, and four times the pixels costs 1.6–3.0×, never four. So roughly half the
`volume.march` interval is fixed per-pass cost that no amount of amortisation removes. **The ceiling
on a perfect reprojection — infinite history, zero resample cost, no artefacts — is therefore about
half the march**: ~2 ms of Constellation's 6.2 ms frame, and ~0.3 ms of Glowmere's 13.4.

**3. A policy parameter that already exists reaches that ceiling today.** `volumequarter` —
`volumeResolutionScale = 0.25`, four lines of tier table — measures **+40.0% whole-frame GPU on
Constellation against a 13.0% floor, four of four pairs positive**, with a march ratio of 0.35–0.61.
That is the same ~2 ms, from a parameter with no history buffer, no reprojection pass, no velocity
dependency, no artefact class that did not already exist, and no determinism surface at all.

**A technique whose best case equals what a policy default already delivers is not worth its
complexity (§56).** That is the decision, and steps 4 and 5 below are why it is not close.

**4. It would break the tier contract.** ADR-035: "a tier scales sample counts, resolutions and
history lengths only, so an offline render of a world matches the preview frame by frame except in
noise and resolution of the auxiliary passes." §5.9 forbids offline taking a temporal shortcut, so
reprojection would be a realtime-only *path* — not a realtime-only *setting*. The preview and the
final would then differ by a reconstruction, which is neither noise nor resolution, in the one pass
that covers the whole frame. Shot decisions get made on the preview.

**5. It re-opens the exact defect class that was just closed.** `SYM-TERRAIN-1` was ambient
occlusion's temporal history dropping whenever the frame index did not advance by exactly one —
right for a seek, wrong for a repeat, so re-rendering a frame produced a different picture from the
one it was re-rendering. It cost an investigation, and closing it is what lets every image
comparison in this upgrade measure the upgrade rather than compete with a defect underneath it
(§1.1). The volume's determinism today is structural and cheap: the march jitters from a hash of the
pixel and the frame index and never the wall clock, and its particle coupling is one-directional.
A second temporal history in the frame would make that a property of history bookkeeping again, and
§41 says the determinism contract is not negotiable.

## Decision

**Do not build temporal reprojection of the volume.** The scalability answer for §33 is the two
policy axes of ADR-139, measured in ADR-141.

## What would reopen this

Stated so the rejection is falsifiable rather than permanent:

- **A scene whose march is both large and pixel-proportional.** The ceiling argument rests on the
  fixed half measured in ADR-141 on these two scenes. A scene where four times the pixels genuinely
  costs four times the time has a real ceiling to reclaim, and this ADR does not apply to it.
- **A resolution scale that has already been taken as far as it visually can go.** Reprojection and
  resolution are alternatives here only while resolution still has room. Once `volumeResolutionScale`
  is at the lowest value §50 will accept and the march is still the frame's largest item, the
  remaining work is temporal or nothing.
- **A temporal AA pass arriving for another reason**, with its velocity resolve, history and
  determinism rules already paid for and already offline-exempt. Most of this ADR's cost column is
  infrastructure that would then be shared rather than new. ADR-132 says nothing is bringing it.

## Alternatives considered

**Reproject only the march and keep the composite per-frame.** This *is* the technique evaluated;
ADR-140 shows the composite is below the instrument, so there was never a variant that reprojected
the composite.

**Interleave the march over N frames spatially (a 2×2 or 4×4 checkerboard of march pixels, resolved
temporally).** Rejected on the same ceiling: it reduces pixels per frame, which ADR-141 measured as
returning half of what the pixel ratio suggests, and it does so by adding exactly the history that
point 5 argues against. `volumeResolutionScale` reduces pixels with none of that.

**Reproject only when the camera is static.** Rejected: the fog itself animates — `volumeNoiseSpeed`
scrolls the fBM, the particle glow coupling moves with the simulation — so a static camera does not
make a static volume, and a technique that is correct only when nothing moves is a technique that
fails during every shot this engine exists to render.
