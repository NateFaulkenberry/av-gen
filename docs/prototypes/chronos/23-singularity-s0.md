# CHRONOS, Temporal Singularity: Gate S0, Test A (the singularity alone)

The plan is `22-singularity-plan.md`. Media is in `~/Desktop/av-gen-review/104-chronos/singularity-s0/`. There is no
engine change: S0 is one user Background shader layer, `examples/chronos/singularity/singularity.wgsl`, with its
scene and project written by `build_singularity.py`. No ADR.

## What is on screen

- **The field.**
  - A main mass whose centre drifts on two incommensurate Lissajous terms and whose size breathes at three rates.
  - Two eddies that orbit it eccentrically, spin the opposite way, and swell and fade on their own clocks.
  - Every structure is drawn in this field's lensed (point-lens) and frame-dragged coordinates. That includes the
    background, so the stars and nebula bend round the masses.
- **The core.** It is near black, with a noisy, breathing edge. The shadow is measured in the unlensed frame; the
  first build showed the lens's inner image of the disk inside the core, and a singularity has no inside to show.
- **The rim.** Never a clean circle: its radius wanders by about 28% with angle and time, its width varies, and it is
  broken into white-hot and gold arcs. A fainter inner arc shows on one side, as the lensed underside.
- **Three accretion bands** on a disk that tilts and changes its inclination, so the rings are elliptical and lopsided:
  - inner: clockwise and fast, white-hot to amber, running to crimson toward the shadow;
  - middle: counter-clockwise, orange, crimson and magenta;
  - outer: clockwise and slow, violet and purple.

  Each is a ridged, domain-warped spiral noise with its own pitch. The band radii dent and swell with angle.
  Beaming makes the side turning toward the camera brighter, and that side wanders.
- **Radial streaks.** Thin amber lines fall inward, faster near the core, in sectors that come and go.
- **Fragmentation.** Cells of the disk, in turning polar coordinates, are displaced in angle and radius. A few are
  always displaced; during a tear, many are.
- **The eddies' spirals.** Small magenta and violet whirlpools turning the other way, each with a darkened heart.
- **The background** fills the frame, subordinate and never empty. It has:
  - a lensed, swirled nebula in deep burgundy and purple;
  - magenta and violet filaments that follow the swirl;
  - haze round the singularity;
  - stars smeared into arcs along the flow;
  - a burgundy-to-purple base that is never dead black.
- **Time.**
  - The rotation accelerates and decelerates (an LFO plus the tears' integral).
  - At irregular moments (13, 31.5, 52, 71 s and so on), a tear: turbulence, fragmentation and expansion surge, the
    rotation lurches forward and keeps what it gained, and the singularity reforms.
  - The frame turns slowly, and pushes toward the singularity at 40, 120 and 185 s.
- **Inputs for S2's drum routes.** All of these are routable parameters (`shader/singularity/<input>`):
  - intensity, scale, expansion, rotation, turbulence, fragmentation and lensing;
  - the gain of each structure;
  - a time offset.

## Files

| File | What |
|---|---|
| `singularity-s0-test-a.mp4` | Test A: 45 s with sound (Feline Footwear; the drum stem as the beat, with no routes yet) |
| `singularity-s0-still-06s.png`, `-13s.png` (a tear), `-22s.png`, `-31s.png` (a tear), `-40s.png` (a push) | stills |
| `singularity-s0-sheet.png` | the five stills on one sheet |

## Measured

- **Speed.** The 45 s render at 1080p ran at 78 fps, including the encode, so the layer is comfortably inside the
  4 ms budget. A per-pass profile is Test G.
- **Flicker**, from the video at 240×135:
  - The mean luminance never steps more than 0.6/255 between frames: no flashing.
  - The mean frame-to-frame change is 1.9/255; it is highest (3.7-4.1) inside the two tears, by design.
  - The remaining frame-to-frame noise is mostly the film grain (0.03).
- **Brightness.** The mean luminance is 37-60/255 across the clip.

## Honest critique

- **It carries the scene with no figures.** The frame is full, dark and colourful, and it keeps moving at several
  scales at once:
  - the bands counter-rotating;
  - streaks falling;
  - eddies orbiting;
  - the whole disk tilting and breathing;
  - the tears.
- **It is still recognisably "a black hole with a disk"** in its quiet stretches. The tears are where it becomes the
  brief's collapse of time. In S1-S2 the quiet state should carry more of that: more secondary spirals, interacting
  streams, outward bursts, and the temporal feedback of §5F.
- **The fragments are a little graphic.** During a tear, their soft cell edges draw faint diamond outlines in the
  bands. Larger, rotated cells, or a smooth displacement field, would read more as torn matter.
- **The inner annulus** between the shadow and the rim reads as a dark, glossy throat. It is good, but it could hold
  more falling streaks.
