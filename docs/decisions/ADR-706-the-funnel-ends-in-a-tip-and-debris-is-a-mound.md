# ADR-706: The funnel ends in a tip, and the debris cloud is a mound of the funnel's own sheath

**Status:** Accepted
**Date:** 2026-09-24
**Addresses:** ADR-580 §11.2 ("the fix is a ground interaction and it is the highest-value work left
on this effect"), `docs/tornado-handoff.md` §5.1, `docs/design/effect-library/tornado-fog-production-pass.md`
§2 item 1 and §7 item 3.
**Keeps:** the skirt ruling (ADR-580 §11.1: Debris stays 0 in both Tree of Life files), ADR-580
§8.7's Detail=0 gate, ADR-421 (no control without a reader -- this adds none).
**Implemented by:** `src/core/tornado.{hpp,cpp}` (`sheath`, `supportBelow`, `kDebrisUnder`,
`evaluate`), `shaders/tornado.wgsl` (`tornadoSheath`, `tornadoSupportBelow`, `tornadoEvaluate`),
the tornado arm of `mediumBoundOf` (`shaders/volume.wgsl`) and of `world::mediumBound`
(`src/world/medium_bound.cpp`), tooltips in `tornado_effect.cpp`.
**Tests:** `tests/unit/test_tornado_foot.cpp`; the tornado case in `tests/unit/test_medium_bound.cpp`;
new positions in `tests/rendering/test_tornado_parity_gpu.cpp`; "the debris cloud reaches the frame
at the foot" and the Detail=0 gate in `tests/rendering/test_tornado_structure_gpu.cpp`.

---

## Context

The funnel's lower end was two things and neither was a shape: a density ramp across
`reach +- footSoft` at full radius, and a hard plane at `h = -0.02` below it. From any camera above
the base that is the column's lower rim seen as an ellipse -- "a column that stops". The debris
skirt that used to hide it was a radially filled disc whose density fell as the square of height
(5.5% of its mass in the upper half of its height), resting on the same plane: a pool on the floor,
which is what the Tree of Life ruling removed.

The plan's constraints: structure, not noise (the new shape must be analytic and the Detail=0 gate
must hold); reuse the condensation-shell vocabulary rather than invent a fifth term; extend the
bound twins if the extent changes; no new control unless it does something. The medium slot has no
free float either (ADR-580 §10.1), so a new control was not available anyway.

## Decision

**1. The funnel ends in a tip.** The same `foot` ramp now also closes the radius, as its square
root, so the silhouette rounds to a point over `reach +- footSoft` instead of ending in a plane at
full width. `footSoft`'s tooltip now says what it does: the length of the tip.

**2. The debris cloud is the funnel's own sheath around a mound.** The shell cross-section (Gaussian
sheath, interior fill, outer fade) is factored into `sheath(x)` and called twice: around the funnel
radius, and around a debris radius that is a mound in height -- widest at the ground by
`skirtFlare` (a linear flare), superelliptic shoulders `(1 - hd^4)^(1/4)`, thinning over its upper
two thirds, and a short rounded underside `kDebrisUnder = 0.3` of its height below the contact. It
carries the striations and the suction lobes, so it turns with the column. The four existing Debris
controls drive it; nothing was added.

**3. The support floor moved, in one expression.** `supportBelow = max(0.02, footSoft, 0.3 *
skirtHeight)` is the field's early-out, and both bound twins now call the same thing
(`world::mediumBound` calls `tornado::supportBelow` itself; the shader's `mediumBoundOf` calls
`tornadoSupportBelow`). The radial bound is unchanged: the mound is scaled so its whole outer fade
lies inside the old skirt radius.

## Two profiles rendered and rejected

- **A squared flare `(1 - hd)^2` under a round dome** came back a concave trumpet: a lamp foot on
  the cone and a hard-edged pyramid on the dust devil, which had been a soft mound before.
- **A plain `sqrt(1 - hd^2)` dome with a linear flare** still came back a cone at the dust devil's
  proportions (a 4 m camera looking at a 34 m-wide, 21 m-tall mass).

The shipped profile is the third render. Sheets: `iter/v2.png`, `iter/v3.png` in the review folder.

## Evidence

Measured on the Classic Cone, Detail 0 (`test_tornado_foot.cpp`):

| | before | after |
|---|---|---|
| funnel width 16 m below the contact (h = -0.02), against 60.5 m at h = 0.1 | 57.0 m | 22.5 m |
| densest sample just above the support floor | 0.158 (a plane) | 0.0007 |
| debris mass in the upper half of its height | 5.5% | 18.1% |

**The Detail=0 gate holds, re-run after every field edit.** `tornado-modes-a-structure` at Detail 0
renders byte-identically with all seven noise controls moved a long way: 11351383429295542053 for
both arms on the old field, 5439304885446671602 for both on this one (the frame itself changed,
because the foot did; the gate is that the noise controls cannot change it). The same controls at
Detail 0.8 move 4,194 px -- the control that the detail stack reaches the frame at all. Made to
fail by letting Detail floor at 0.05 inside `tornadoDetail`: the two hashes then differ. The structure lives in the envelope, and the detail term is
still one mean-1 multiply on it.

**By eye** (`sheet-item1-showcase-foot.png`, `sheet-item1-modes.png`, `sheet-item1-hero.png`):
- the showcase feet go from flat discs on an invisible floor to rounded, banded domes wrapped round
  the foot of the funnel; the rope and the tall column most clearly, the dust devil and wedge least
  (their skirts were already their largest feature);
- panel A still reads as a tornado on its own, with a small dome at its foot where it had a disc;
- **the hero's column now ends in a rounded, tapering funnel tip where it ended in a slanted flat cut.**
  It reads as the end of a funnel, not a sawn-off stub. It does NOT read as touchdown, and cannot:
  the Tree of Life's column stands on nothing.

## The hero keeps Debris at 0

The debris-on arms of the hero (0.5 and 0.9, `sheet-item1-hero.png`, right) come back as a
blown-out luminous bell under the column -- the "pool of light" ADR-580 §10.4 described, now with a
rounded outline. The reason ADR-580 §11.1 gave for dropping the skirt (a ground contact the scene
does not have) is unchanged by this ADR, and the hero's brightness (item 4) makes the dome read as
light rather than dust. So neither Tree of Life file changes. If the owner wants the hero to read as
touchdown, that is ADR-580 §10.4's option 2 -- give it something to touch -- which is a composition
change, and the debris is now ready for it.

## Consequences

- Every tornado with `touchdown` 1 now narrows over its last `footSoft` of height (to 0.71 of its
  radius at the contact at the default), where the density was already half-faded. Standing on
  terrain the rest of the tip is underground; standing on nothing it is the rounded end above.
- A column standing on terrain renders its debris underside and tip below the ground, where the
  depth test hides them; the bound's floor is `supportBelow * height` lower, which is a few more
  field evaluations for rays that pass under the contact, and no change in what is drawn.
- `docs/tornado-handoff.md` §5.6 (particles) named this term as the thing particles would be fed
  from. It now exists as a term worth feeding them from.
