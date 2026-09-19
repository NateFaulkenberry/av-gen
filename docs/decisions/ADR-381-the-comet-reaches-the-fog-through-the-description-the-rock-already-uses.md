# ADR-381: The comet reaches the fog through the description the rock already uses

- Status: Accepted (2026-09-19)
- Phase 13. Extends ADR-230 (the comet's ground illumination), ADR-374 (the vortex must not
  scatter), ADR-379 (its spill).

## What Phase 13 already had

Most of it. The brief asks for a travelling highlight on the tree and transient illumination on the
island's underside when the comet passes; ADR-230's ground pool already does both, and this scene
already enables it — `mode: "subtle"`, intensity 1.22, radius 220. Measured: removing it changes the
frame by 2.2 luminance levels. The one thing missing was the brief's "vortex fog should receive
subtle light/color response".

## Decision

The vortex takes the comet's light from **`frame.skyGroundPoint`** — the same description the
surfaces are lit from — rather than from a second reading of where the comet is. The rock and the
fog then brighten from one account, which cannot drift, and the term costs no new plumbing.

This is not a relaxation of ADR-374's rule that this medium must not scatter the scene's lights.
That rule exists because the key at intensity 22, accumulated over a 2.6 km march, returned mean
luminance 131 of 255 with the vortex's own emission at zero. This is one small, moving,
distance-limited source, and it is gated by a parameter that is zero by default.

## Three errors, in the order I made them

**The distance was three-dimensional.** `atmosphere_ground.wgsl` measures **xz only**, and says why
in a comment: the comet is kilometres up, so what matters is how far a point is from the spot
*under* it. I used the 3D distance, the vortex sits hundreds of metres below the ground plane, every
sample fell outside a 220 m sphere, and the measured effect was **exactly zero at every setting**.

**The reach was the surface's.** The ground pool's 220 m is right for the island's rock and far too
small for a cloud kilometres across. A light that reaches the rock and stops dead at the fog beside
it is ADR-369's discontinuity one object along. `cometReach` scales it, default 6.

**And the units, for the third time in this branch.** `lit.rgb` is a surface radiance; the march
integrates over metres. Adding one to the other without a per-metre conversion lifted the whole
frame by **125 luminance levels** at `cometResponse` 0.6. ADR-374 made this mistake with density,
ADR-379 made it with the spill, and I made it again here. It now carries an explicit 0.005 and a
comment naming the other two, because three times is a pattern and the next person adding a term to
this loop should be warned by the code rather than by the ADR log.

| | comet response 0 | 0.6 (shipped) | 1.8 |
|---|---|---|---|
| frame delta | +0.000 | **+3.09** | +8.92 |

## A defect I nearly reported, and did not

The effect is as strong at t = 30 s as at t = 9.4, sixteen seconds after the comet's stated
`windowStart 4 / windowSeconds 10` closes. That looked like the ground pool failing to follow its
lifetime envelope, and ADR-230's comment claims it does follow it — a good defect to report.

It is not one. The effect's activation is `always`, and the rendered frame at t = 30 plainly shows
the comet's fragments in the sky: it is genuinely still flying, so its ground pool being lit is
correct. **Looking at the frame is what settled it**, after the number had already convinced me.

That is the second time today I have been one step from attributing a defect to another agent's area
on a plausible reading. The first was ADR-372 and the coordinator caught it; this one I caught.

## Consequences

- The response follows `skyGroundPoint`, which ADR-230 documents as **the brightest comet's** track
  point. With several comets the fog answers only the brightest, while the sky shows them all.
- Reachable as `scene/vortex/cometResponse` with a row in the Environment panel.
