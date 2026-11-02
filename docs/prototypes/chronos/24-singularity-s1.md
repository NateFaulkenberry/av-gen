# CHRONOS, Temporal Singularity: Gate S1, the figures, their temporal transformation and gravitational distortion

Media is in `~/Desktop/av-gen-review/104-chronos/singularity-s1/`. Two ADRs:
- **ADR-1935**, a `late` shader layer stage after the temporal stage;
- **ADR-1936**, RGB temporal displacement per gated slot.

## The approved singularity fixes (Test A)

- **The core.** It is misshapen: an ellipse, a three-lobed dent and a five-lobed ripple, each turning on its own
  clock. It wanders the frame (±0.33 of the frame width, ±0.16 of its height), so the camera rarely frames it dead
  centre. Its size swings by about ±35% on two slow clocks, besides the breathing and the tears.
- **The rim.** The dashed rim is gone. White-hot now lives only on the crests of the inner band, flowing along its
  spiral, brighter on the beamed side.
- **The tears.** The cells are gone. A tear is a smooth shear field that drags the disk sideways and smears it. The
  late lens drags whole regions of the frame the same way, figures included.
- **The quiet state now has:**
  - **colliding streams:** tidal streams torn from each eddy, bowed and wound into the main disk, where they cross;
  - **outward bursts:** 16 broken, rippling shells thrown out of the core between the tears, with ejecta streaking
    outward;
  - **a third eddy:** it forms in the far field, drifts and dissolves.
- **Feedback (§5F).** Deferred to S2, deliberately. The engine's feedback zooms about the frame's centre, so with the
  core wandering it read as a generic tunnel. S2 should give it a centre.

Before and after is `singularity-s1-fixes-before-after.png`: S0 on the left, S1 Test A
on the right, at the same five times.

## One field (§6)

`field.wgsl` is the field: the masses, the core's shape and radius, the tears, the bursts, and the frame's turn and
zoom. `build_singularity.py` splices it into both layers, and `field.py` mirrors it line for line.
- **`singularity.wgsl`** (Background) draws every structure in the field's swirled coordinates.
- **`lens.wgsl`** (Late, ADR-1935) bends everything drawn, figures and trails included. It does three things:
  - point-lens magnification and compression round the main mass and each eddy;
  - frame dragging, the eddies twisting the other way;
  - the tear's shear.
- **The figures' trajectories** are integrated in Python through the same masses: gravity, a clockwise drag, the
  eddies' pull and twist, and a little friction. So a figure's curve, the lensing and the luminous swirl agree.

## The figures (Test B)

- **The cast.** Ten figures, from the eight MPFB souls, 14-29% of the frame's height, on planes 26-34 m from a fixed
  camera.
- **Flight.** Each is reborn just inside a frame edge in a near-circular orbit. It is pulled into a spiral, slung by
  the eddies, and swallowed at the core: it shrinks continuously over its last approach, then is reborn after a
  breath. One flung out of the frame is reborn too.
- **On screen.** Between 3 and 10 figures, 6.5 on average.
- **Orientation.** A figure aligns head-first or feet-first along its flight when fast, and tumbles when slow.
- **Light.** A core light rides the singularity, so the figures are lit hot from wherever it is, with a violet fill.

## Their temporal transformation (Test C)

Each figure is an ADR-1934 slot, and its stage is a function of its depth in the field (x core radii from the main
mass):

| x | Stage |
|---|---|
| > 9 | intact |
| 9 → 6 | echoes (a short trail broken into copies 7 frames apart) |
| 6 → 3.5 | the stretch, its colours starting to drift apart (ADR-1936) |
| 3.6 → 2 | fracture, with strips across its own direction of travel (the slot's angle is keyed from its screen velocity), and a stronger RGB split |
| < 2.2 | squeezed to a ribbon along its flight, and gone at about 1.2 |

Passing near an eddy also echoes and fractures a figure. The figures' colour fringes are kept inside the brief's
palette by the lens layer's palette guard: green never exceeds the warmer of red and blue, and a strong blue leans
violet. Test C runs the lens layer with its warp at zero, so only the guard acts.

## Gravitational distortion (Test D)

Test D is Test C with the lens on: the full S1 composition.

## Files

| File | What |
|---|---|
| `singularity-s1-test-a.mp4` | Test A after the fixes, 45 s with sound |
| `singularity-s1-test-b.mp4` | Test B: the figures in the field (no temporal transformation, no lens), 45 s with sound |
| `singularity-s1-test-c.mp4` | Test C: B plus the temporal transformation, 45 s with sound |
| `singularity-s1-test-d.mp4` | Test D: C plus the gravitational distortion of everything, 45 s with sound |
| `singularity-s1-fixes-before-after.png` | S0 (left) against S1 Test A (right), at 6, 13 (a tear), 22, 31 (a tear) and 40 s |
| `singularity-s1-test-a-{06,13,22,31,40}s.png` | Test A stills |
| `singularity-s1-test-b-{10,30}s.png`, `-c-{10,34}s.png`, `-d-{10,22,34,40}s.png` | Test B, C and D stills |

All are Feline Footwear, with the drum stem as the beat and no routes yet (routes are S2).

## Measured

- **Speed.** All four 45 s renders ran at about 58 fps at 1080p, encode included, with 0 GPU errors. A per-pass
  profile is Test G, at S2.
- **Flicker,** from the video at 240×135 (owner sensitivity):

  | Test | Largest step in mean luminance between frames |
  |---|---|
  | A | 0.7/255 |
  | B | 1.2/255 |
  | C | 1.2/255 |
  | D | 1.8/255 |

  The first cut of D stepped 6/255: a figure lit white-hot by the core light vanished in one frame at the core. It now
  shrinks away continuously, under a weaker light set further back.

## Honest critique

- **Works.**
  - The figures read as people caught in a field: curved, tumbling, at several depths.
  - Their transformation reads as a progression, from intact through echoed silhouettes and stretched ribbons to a
    sliver swallowed by the core.
  - The lens makes everything bend together, most strikingly in a tear.
  - The singularity no longer reads as a stock black hole: the core is misshapen and wanders, there is no ring, and
    the tears smear.
- **Weak.**
  - Near the core, a fast figure's echoes fan into large striped combs. It is striking, but busy. S2 could tie the echo
    spacing to speed.
  - The RGB fringes run pink, ivory and violet now, though the blues are a little cold in places.
  - Feedback (§5F) is still to come (S2).
  - The figures are lit flat-hot by one light. Rim light from the bands' colour would sit them in the scene better.
