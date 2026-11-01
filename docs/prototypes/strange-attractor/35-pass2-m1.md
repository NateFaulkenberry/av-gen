# Art Pass 2, Milestone 1: the Moon at night, the first encounter

M1 of `33-pass2-plan.md` §6, re-timed to the owner's 210.5 s cut of Feline Footwear (crossings on bar lines). It covers
the Moon extension (the terminator, the shadow basin, the domes, the crater portal at the basin's edge), the return to
the canyon at dusk, and the first Sasquatch encounter at a campfire. The sequence so far runs 0-86 s.

Media is in `~/Desktop/av-gen-review/101-strange-attractor/pass2-m1/`; the numbers are in `05-measures.txt`.
ADRs: 1885 (the journey returns to its first world), 1886 (looks), 1888 (light flicker; card fur), 1884 revised.

## Timing (121 BPM, bar = 1.983 s)

| Time | What |
|---|---|
| 0-32.0 s | the canyon by day, the wall portal (Art Pass 1, unchanged) |
| 32.025 s (bar 17) | crossing 1: out onto the Moon, sunlit plain, a fresh crater beside the glide |
| 42-50 s | over the great basin's rim: the near rim's shadow is the night side, the domes across it |
| 50-62 s | across the night floor (earthshine), the eye held on the domes while the route swings east |
| 62-67.7 s | over the portal crater at the basin's edge; the glyph field, the floor opening |
| 67.700 s (bar 35) | crossing 2: out of the canyon wall at dusk (the same canyon, ADR-1885) |
| 69.4-75.0 s | down to the water, slowing past the campfire with the eye on it: three Sasquatches round it |
| 75.0-86 s | on down the dusk canyon (toward Mars, M2, planned at bar 44 = 85.5 s) |

## The Moon

- **Terminator and basin.** The Moon is more curved (`lunarWorld.curvature` 150 km, ADR-1887). The sun is 2.2 degrees
  up, so the light falls away across the route. A great old basin (2.2 km, `fresh` 0.12) puts its floor in the near
  rim's shadow: the camera crosses from day into night physically.
- **The night is lit by the Earth.** Earthshine (0.6, blue-white, from the Earth's direction) plus a high weak
  "earthglow" fill (0.09), so the basin floor's relief reads instead of going black. A grazing earthshine alone left
  the floor black in the first render.
- **The domes** keep the approved proof look (`pass2-r/02-proofs/02-crater-domes-in-the-dark.png`): three of them, in
  their own craters across the basin. Their point lights are a local warm spill (60-80 k, range 900 m). At first they
  flooded the floor brown. A fourth dome sat on the final approach and filled the frame, so it was removed.
- **The crater portal** is at the dark basin's edge, as the owner agreed. The final dive is Gate C 2's: a near-vertical
  drop along +x. The rotation portal turns that into the wall's level exit, so the canyon through the hole is upright
  and the crossing changes nothing on screen. An approach from +z, which I tried first, came through tilted and
  snapped level at the crossing.
- **The Earth** is the 100 m Blue Marble mesh at 250 m radius and 9 km distance, about 3.2 degrees across. Contact
  shadows are off: they made the blotchy dark patches on its texture in the dome proof.

## The canyon at dusk, one world (ADR-1885, ADR-1886)

The Moon's portal now leads back to the root canyon (`"world": "@root"`). The dusk scene and `build_dusk.py` are gone.
Dusk is a `looks` entry from crossing 1, written by `build_journey.py`:
- sun -130 degrees azimuth, 8 degrees up;
- the violet sky, the fog colour and the fills;
- the campfire and the Sasquatches switched on.

The canyon's scan and corruption end after its own crossing.

**A regression this caused, found and fixed.** The first full render lost the opening's whole foreground (PSNR 18 dB
against Pass 1 over 0-16 s):
- the return portal's exit-plane clip landed on the root from frame 0;
- the Moon's nested view back into the root made a second renderer mid-frame.

Both are fixed (ADR-1885), and the return test checks the clip. The opening now matches Pass 1 (PSNR 52.5 dB over
0-16 s). `03-before-after-0-16s/` shows it a frame per second.

## The first encounter

- **Site.** A flat bank 75 m down-canyon of the exit, 45 m off the river line, measured at 2.7-3.3 m round the fire.
  Three Sasquatches face the fire in a loose ring.
- **The Sasquatches** are built from the owner's download, Mat Rex's Bigfoot (CC BY 4.0, attribution in
  `assets-pass2.md`), by `pass2/build_sasquatch.py`:
  - three heights (2.35, 2.5 and 2.25 m), a fitted 20-joint armature and distance-based weights;
  - the arms posed down;
  - 24 000 alpha hair cards with the fur colour baked in;
  - a 12 s idle.
  - My own MPFB build had a flattened head and grey fur, so it was dropped.
- **The fire.** A point light (90, range 45 m, shadows) with a slow deterministic flicker (0.35 at 3.2 Hz, ADR-1888).
  There are flame, ember and smoke particles. At this distance the light's pool reads more than the flames do.
- **The shot.** The camera comes out of the wall and drops to 6 m over the water. It slows as it passes the fire about
  22 m off, abeam near 74 s, and the eye holds on the group from 69.4 to 75.0 s. The aim lets go before the camp falls
  behind: an earlier version released it later, and the view swung about 90 degrees in a second (a frame difference of
  19.5; now 10.6, eased over about 1.5 s).

## Measures (`05-measures.txt`)

The coordinator's flicker metric is on the final video. The opening, the build and both day legs are as calm as
Pass 1's. Two places stand out:
- **Crossing 2.** The dive slows into the hole and the frame difference dips at the crossing, then rises as the camera
  flies out of the wall: the same shape as Gate C 2, with no jump.
- **The campfire.** The camera is slow and the frame differences are small (about 1). The fire's breathing and the
  fur cards' shimmer then dominate the ratio, but the absolute change is small.

## Known weaknesses (honest)

- The Sasquatch facing away from the camera reads pale and cottony in the firelight (the cards are lit on both sides).
- The flames are small against the fire's light pool at 23 m.
- At dusk the canyon's gold contour lines (the Pass 1 `response.high` route) glow strongly against the dark walls. A
  look cannot quiet a routed parameter. This is left for the owner to decide.
- The bank behind the camp shows the terrain's stepped edge in the low light.
- The Grays' sexless bodies and the forehead triangle are M3/M4 work. The downloaded Grey Alien is already sexless.
