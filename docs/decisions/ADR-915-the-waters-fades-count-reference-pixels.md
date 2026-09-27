# ADR-915: The water's fades count reference pixels

**Status:** Accepted
**Date:** 2026-09-26
**Found by:** GV3's final render (its F37: the water fix did not survive the resolution change), and
the GV3 revision's water audit
**Follows:** ADR-099 (the per-layer fade and the sparkle's band-pass), ADR-914
**Implemented by:** `kWaterReferenceRows`, `refScale` and `lodFootprint` in `shaders/water.wgsl`
**Tests:** `tests/rendering/test_water_lod_gpu.cpp`, "a preview and a final at twice its resolution
fade the same water" (control: the same shader counting its own pixels, derived from the live file)

## Context

Everything on the water that could alias fades by how many pixels one cycle of it spans: each ripple
layer below one to three pixels, the foam's break-up the same way, and the sparkle outside a three- to
seventy-pixel band. The pixels were the frame's own. So a render at twice the resolution kept detail
out to twice the distance.

GV3 met this at the worst moment. Its previews render 960x540 at 2x supersampling (1080 rows
internally) and its final 1920x1080 at 2x (2160 rows). The river was tuned on previews until it read as
a smooth dark ribbon in every wide (F25); in the final the ripple texture came back across the whole far
river (F37). The fix was to author the final differently -- the frequency doubled and the amplitude
halved, 0.1 at 2.6 cycles/m became 0.05 at 5.2 -- which makes a preview of the final something the
previews are not. The same shape bites anything that fades, band-passes or levels-of-detail in screen
pixels (GV3 07-technical.md, section 7.10).

## Decision

- **The water's fades count reference pixels:** a 1080-row frame's. `refScale = max(targetSize.y /
  1080, 1)`, and every fade on the surface -- the three ripple layers, the sparkle's band-pass and the
  foam's break-up -- is measured against the footprint of a reference pixel, `footprint * refScale`.
  At or above 1080 rows every resolution fades the same world-space detail, so a preview is a preview
  of the final.
- **Below 1080 rows the frame's own pixels still set the limit.** Detail finer than a real pixel can only
  alias, so a small frame (an interactive viewport at a reduced scale, a test at 192x128) fades as it
  always did.
- **Rows, not width:** a frame's height is what a vertical field of view divides, and it is the number a
  preview and a final of one film share in proportion.
- **ADR-916's tears count the same pixels**, for their band and for the layers they compress.

Rejected:
- **Counting output pixels rather than internal ones.** The shader sees the frame it draws, and
  supersampling is how the offline renderer spends resolution. A reference in internal rows keeps 2x
  supersampling's anti-aliasing (the fade is at one to three internal pixels of a 1080-row frame, half
  an output pixel to one and a half of a 540-row preview) and still makes previews and finals agree.
- **A per-project reference.** A project could only use it to make its previews disagree with its
  finals again.

## Consequences

- **Every render taller than 1080 internal rows changes** (a 1920x1080 or larger final at 2x
  supersampling, any 4K render): its water fades nearer, where a 1080-row frame would. **Renders at or
  under 1080 rows are unchanged, bit for bit** -- GV3's 960x540 previews at 2x included: s14 and s39
  rendered before and after this change differ in no pixel.
- **GV3's final changes.** Under this rule its final-only setting (0.05 at 5.2 cycles/m) fades the
  ripples at half the distance its previews showed, so the far river of a 1080p final is smoother than
  the approved v2 final. Its approved look comes back, in the previews and the final alike, with the v2
  preview settings (0.1 at 2.6, the bass route at 0.03). Stills (`lod/` in the revision's water folder):
  s14 at 78 s and s39 at 214.6 s, at 1920x1080 with today's shader and 5.2/0.05, with this change and
  5.2/0.05, with this change and 2.6/0.1, and the 960x540 preview at 2.6/0.1.
- **Measured on the QA river** (a grazing view from the bank, 1440x1080 against 2880x2160 box-averaged
  onto the 1080-row grid): the far half of the water, relative to the near half, carries x1.08 the fine
  structure at 2160 rows with this change and x1.44 counting the frame's own pixels. The near half is the
  calibration -- nothing near the lens fades at either resolution, so its x0.83 is what the comparison
  itself does (a box-averaged frame is smoother than a point-sampled one), and it is the same in both
  arms.
- **No existing test moved.** The GPU water tests render at or below 1080 rows.
