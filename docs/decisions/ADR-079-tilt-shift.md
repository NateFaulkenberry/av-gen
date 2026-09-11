# ADR-079: A tilt-shift lens, as a second circle of confusion rather than a second pass

Status: accepted
Date: 2026-09-10

## Context

A tilt-shift lens swings its focal plane away from parallel with the sensor. What comes back sharp
is then a *band* across the frame at whatever angle the swing implies, with defocus growing on
either side of it. Narrow the band and steepen the falloff and the eye reads the whole frame as a
model on a table — the miniature fake — because that band of sharpness is otherwise only ever seen
through a macro lens on something small.

This engine already has a defocus filter. `fs_dof` in `shaders/post.wgsl` computes a circle of
confusion for the pixel, gathers taps on a golden-angle spiral of that radius, and weights each tap
by whether that tap's own circle actually reaches the pixel — the test that stops a sharp
foreground from haloing into a blurred background. Behind it sit `post/dof/*` and the physical lens
model of ADR-037, where the circle is `c = f² |d − s| / (N d (s − f))` in millimetres on the
sensor.

§80 asks what can be extended before anything is added. So the first question was not "where does
the tilt-shift pass go" but "what, precisely, can the existing pass not express".

The answer: nothing about the *filter*. A tilt-shift and a depth of field differ in exactly one
function — how far out of focus a point is. One asks the distance from a focus plane in metres; the
other asks the distance from a band in screen space. Taps, weights, energy and the early exit for
sharp pixels are identical, and want to stay identical: two gathers that disagree about how a blur
falls off would be visible the moment a shot used both.

## Decision

**One pass, two circle-of-confusion functions, combined by `max`.** `fs_dof` now calls
`circleOfConfusion(uv)`, which is the larger of:

- `depthCircleOfConfusion(viewDistance(uv))` — the existing ADR-037 code, unchanged, behind the
  `post/dof/enabled` flag;
- `tiltShiftCoverage(uv) * maxRadius` — a smoothstep of the distance from the band, behind
  `post/tiltShift/enabled`.

`max` rather than a sum: two reasons for a point to be out of focus are not two defocus energies to
add up, and the wider circle is the only one that can be seen anyway. Reading depth stays behind
its own flag, so a tilt-shift in a scene with no depth of field pays for no depth samples — 25 to
193 of them per pixel, which is not a rounding error.

The pass runs when either is on. It keeps the timeline stage `post/dof`, because it is still one
pass and giving it two names would only scatter the same microseconds across two rows.

**The band is screen space, not Scheimpflug.** The physically honest version tilts the focal plane
in world space, so `focusDistance` becomes a function of screen position and the existing depth
path does all the work. It was rejected, for a reason worth recording: it produces nothing at all
on a subject at roughly constant depth — a wall, a title card, a flat-on landscape — and the look
people ask for by name is the band. A depth-driven tilt is still available and always was: set the
focus distance, narrow the range, and ADR-037's lens will do it.

**Coverage.** With the centre at `c`, the rotation θ and both distances measured in fractions of
the frame *height*:

```
p        = (uv − c) · (aspect, 1)
normal   = (−sin θ, cos θ)
d        = |p · normal|
t        = clamp((d − bandWidth/2) / falloff, 0, 1)
coverage = t² (3 − 2t)
```

The aspect correction is what makes the rotation an angle *on screen*. Left in raw uv, a 45 degree
band on a 16:9 frame arrives at 28 degrees and changes width as it turns; `post/tiltShift/rotation`
would then be a number rather than an angle. Smoothstep rather than a linear ramp because the
band's edge is exactly where the eye goes looking for a seam, and a linear ramp creases there — its
slope jumps from nothing to the full falloff in one pixel.

The same formula lives twice, as `scene::tiltShiftCoverage` in `src/scene/post_settings.cpp` and as
`tiltShiftCoverage` in `shaders/post.wgsl`. That is a deliberate duplication: the shader is what
renders, and the C++ twin is the only version a unit test can interrogate at a hundred points
without a device. Both carry a comment pointing at the other.

**Parameters**, registered exactly as `post/dof/*` are, so they automate, modulate, save with the
project and appear in the Parameters panel with no further wiring:

| path | type | default | notes |
|---|---|---|---|
| `post/tiltShift/enabled` | bool | `false` | |
| `post/tiltShift/centre` | vec2 | `(0.5, 0.5)` | normalised, (0,0) top left; hard range −1..2 so the band may run off the frame |
| `post/tiltShift/rotation` | float | `0` | degrees; 0 is horizontal, positive reads clockwise |
| `post/tiltShift/bandWidth` | float | `0.2` | full width of the sharp band, in frame heights |
| `post/tiltShift/falloff` | float | `0.25` | distance past the edge to full defocus; hard minimum 0.001 |
| `post/tiltShift/maxRadius` | float | `8` | pixels at 720p, scaled by height/720, as `dofMaxRadius` |

A composition's own `post` block can author all six (ADR-059); a malformed `tiltShiftCentre` is an
error rather than an ignored key, because it is the one field that can be structurally wrong and a
silent default puts the band somewhere else entirely.

## Sampling: what changed, and what deliberately did not

The gather is shared, so any change to it is a change to depth of field. Two were considered.

**Tap count now follows the disc's area — but only when the band is in play.** 24 taps is what the
depth path has always used, and with the band off it is still exactly 24, so no existing render
moves. That count does not survive being asked for a miniature fake: 24 taps over a 12 pixel disc
is one sample per 12 square pixels, and neighbouring pixels averaging different samples does not
read as defocus, it reads as noise. With the band on, the count is `0.5 · coc²` — about one tap per
two square pixels — capped at 192.

The cap is a real limit and worth stating plainly. 192 taps is enough for a 20 pixel radius; above
that the disc is undersampled, and since the radius scales with frame height, an authored 32 at
2160p is 96 pixels of radius and will not hold up. A wider blur than that wants a downsampled
pyramid rather than a bigger spiral, which is a different pass and a different decision.

**A per-pixel spiral rotation was tried and removed.** Interleaved gradient noise is the standard
answer to a sparse gather's rings, and `blurJitter` was already in the file. It measured worse.
On a broadband test pattern the worst pixel-to-pixel step inside the fully defocused region was
2.6 without it and 3.1 with it, against 13.7 in the sharp original — that is, the dither was the
largest artefact left in the blurred region, not the rings it was there to hide. Rotating per pixel
turns a filter into an estimator, and an estimator with 192 samples still has visible variance.
Unrotated, every pixel applies the same irregular kernel and neighbours differ only by their
offset. The measurement is in `tests/rendering/test_tilt_shift_gpu.cpp`.

## Consequences

Off by default and visually inert: `examples/world/glowmere-stylized.json` at 640x360, frames
8.000–8.050, hashes `1d19e398a2375d67` before this change and `1d19e398a2375d67` after it.

HDR-correct. The pass sits before the tone map on scene-linear values and nothing in it clamps: the
result is a weighted mean of the taps, so a 60.0 highlight comes out of a full blur at 42.8, not at
1.0. Glowmere's emissive flora keeps its range.

Cost, measured on `glowmere-stylized` at 1920x1080, from the `post/dof` row of the frame timeline
(best of several runs; medians are destroyed by contention):

| authored radius | radius at 1080p | taps | `post/dof` |
|---|---|---|---|
| off | — | — | pass not encoded |
| 4 | 6 px | 24 | 0.52 ms |
| 8 | 12 px | 72 | 1.18 ms |
| 16 | 24 px | 192 (capped) | 3.21 ms |
| 32 | 48 px | 192 (capped) | 3.60 ms |

Linear in the tap count until the cap, then flat — which is the cap doing its job. Against a frame
that is tens of milliseconds of scene, a miniature fake costs about as much as bloom and shadows
together. That is the price of quality over speed for this effect, and the pyramid mentioned above
is where to go if it ever needs to be cheaper.

Depth of field is bit-for-bit unchanged while the band is off: the same scene with
`post/dof/enabled` and a 10 pixel radius hashes `45303cefa531209c` against both the old shader and
the new one. With both on, the depth path inherits
the denser tap count — a better image for more time, and the only way the two can share a gather
without disagreeing about how a blur falls off.
