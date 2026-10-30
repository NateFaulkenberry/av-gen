# Canyon water, pass 2: the black bands and the opening's saw-teeth

Owner's reports on `canyon-shadows2/00-…mp4`:

1. In the opening's high shot (0.3–1.5 s), a jagged, spiky fringe on the reflected bank: saw-teeth or icicles,
   changing every frame.
2. Tiny white specks near it.
3. Whether the reflected cirrus band pops.
4. Black bands flickering in the water, around 8–10 s, over the left-bank flats:
   - dark diagonal bands;
   - an every-other-pixel checkerboard;
   - a ragged bank edge with a dark rim.

The coordinator also noted a vertical seam at 16.0–16.2 s, around x 60–90.

Media is in `~/Desktop/av-gen-review/101-strange-attractor/canyon-water2/`; the numbers are in `06-measures.txt`
there.

## The black bands: one-frame checkerboards (ADR-1870)

**Found by measuring.** Two scans found the event:

- a Laplacian (checker) energy over the water;
- a dark-transient scan (water pixels more than 25 levels darker than both 2 frames before and 2 frames after).

At 0.067–0.10 s, 5.067 s and 6.267 s, for exactly one frame, the whole river becomes a checkerboard with dark bands
that follow the bed. Every earlier clip has it at the same instants.

**Not a race.** It looked history-dependent, because ranges starting at different times did or did not show it. In
fact it depends on the exact time. A single frame at t = 5.0666667 s shows it, while t = 5.06666 s does not. Four
repeated runs gave the same result.

**Bisect.** Each switch below was tried, rendering the scene with one thing changed. None of them removed it:

- SSR off;
- sky mirror off;
- cirrus off;
- volume off;
- depth prepass off;
- the pre-ADR-1868 shader.

Reflection off removed most of it.

**Stage dump of `water.wgsl`** (the same frame with each stage written straight out):

| Stage | Event | Clean |
|---|---|---|
| Normal | 1.7 | 1.7 |
| Cube reflection | 1.3 | 1.3 |
| Sky mirror | 1.3 | 1.3 |
| After SSR | 3.9 | 1.6 |
| **Alpha** | **24.0** | **2.5** |

The alpha is the water's thickness over the bed, `bed - viewDepth`, read from the linear-depth target. Over the
flats the bed is centimetres under the surface, and the target was noisy from pixel to pixel there.

**Cause.** `linear_depth.wgsl` unprojected `(ndc, depth)` through the f32 `invViewProj`, with the camera kilometres
from the origin and a far plane of 1e5 m. ADR-1807 (the sky ray) and ADR-1866 (the cascades) hit the same rounding.
Here it produced:

- errors of tens of centimetres at a few hundred metres;
- a different error in neighbouring pixels;
- a different error for every pose.

For most poses the error is smooth. For a few, it alternates pixel by pixel, and shallow water flips between
see-through and opaque in a checkerboard. The dark bed shows through in bands.

**Fix.** The target now comes from the projection's own depth mapping, `x / ((1 - depth) + y)`. The terms x and y are
formed in double on the CPU, and the pass has no matrix inverse. Every reader of the target benefits.

**Results:**

- The 5.067 s frame goes from 8.7 to 1.3 on the checker measure; its neighbours are 1.3–1.5.
- Over 46 s, dark transients go from 507 to 151 px/frame (mean) and from 5,212 to 2,495 (max).
- The test's row noise on a grazing plane 5 km out goes from 2.74 m to 0.27 m.
- The remaining dark transients are the low pass over the river (15.8–22.3 s): fast-moving reflections of the rock,
  ranked the same before and after.

**Tried and dropped:**

- Rejecting SSR hits on points below the water plane. The hits were not under it; the alpha was the cause.
- A depth-slope bias on the march's hit test. It was not needed once the depth was right.

## The saw-teeth (ADR-1871)

**Bisect** at 0.40 s:

| Variant | Teeth |
|---|---|
| SSR off | Gone, and so is the bank reflection |
| Ripples off | Gone, leaving a clean mirror |
| Sky mirror off | No change |
| Edge fade 0 | No change |

So the teeth are the march's reflection, bent by ripples.

**Cause.** From high above, the ripples are a few pixels long, but each one swings the reflected ray several of the
reflection's own pixels. Neighbouring water pixels then alternately hit the rock and miss into the sky, and that
pattern is the teeth. ADR-1868's blur softened the colour but not the hit/miss pattern.

**Fix.** The march and the sky mirror reflect about a normal whose ripple tilt is limited to 1.5× the flat mirror's
per-pixel spread. The removed tilt survives as ADR-1868's blur.

**Measure:** non-blue pixels inside the smoothed water edge, 0–2 s.

| Code | Teeth area (px/frame) | Toggles (per frame) |
|---|---|---|
| Before | 207 | 406 |
| Depth fix only | 159 | 310 |
| Both fixes | 97 | 186 |

**Specks.** These are the bright water between the teeth, and they go with the teeth. Specular off made no
difference.

**Cirrus band.** Stable.

## Not fixed

**Vertical seam, 16.0–16.2 s.** This is the SSR's screen-edge fade. The wall the water should reflect is off the left
of the frame, so those rays fall back to the sky. The seam is the same before and after. Options for the owner:

- a wider fade;
- clamping off-screen rays to the edge column, which smears in general.

## Cost and suites

- GPU median 24.23 → 24.26 ms (headless ultra, scene); the linear-depth pass 0.10 → 0.09 ms.
- Coordinator flicker metric, 46 s: p99 0.041 → 0.041, max 0.13 → 0.15. The water events are too small in area to
  move this whole-frame measure.
