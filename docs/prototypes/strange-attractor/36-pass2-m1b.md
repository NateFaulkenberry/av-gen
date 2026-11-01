# Art Pass 2, M1b: the world's scale, the water spill, a short Moon, a real campfire

The owner reviewed M1 ("overall this direction is great") and asked for a revision before M2. The asks, in their
order:
- the world's scale, first;
- the portal's corruption spilling across the river again, as at Gate A;
- a shorter Moon;
- a real campfire.

The coordinator added notes on the Sasquatches, the pale fur and the dusk contour lines. Portal 1 stays in the river's
east bank wall (owner); the outer cliff faces are for the later portals.

Media is in `~/Desktop/av-gen-review/101-strange-attractor/pass2-m1b/`; the numbers are in `05-measures.txt`.
ADRs 1893 (world scale) and 1894 (contour gain); 1885-1888 stand.

## 1. The world's scale (ADR-1893)

With figures in it, the canyon was far bigger than the owner had pictured. The river was about 150 m wide, the gorge
70 m deep and the outer walls up to 150 m. A 2.3 m Sasquatch on the bank was a speck.

- **What was scaled.** The world, uniformly, by 0.4. Characters and props keep their real size. `attractorWorld.worldScale` scales the
  generated world after generation (identical shape, the flight's timing kept, so its speeds scale too).
  `canyon_scale.rescale()` scales every other length the scene and project carry:
  - the camera, the fog density, the shadow range and the cirrus;
  - the water's ripples, flow, depth fades and SSR;
  - the terrain program's world-position frequencies and depth fades;
  - the scan;
  - the portal's corruption: aperture, blocks, slip (and its audio route), glyph cells, field, and the light (as d²).
- **What it is now.** The river is about 60 m wide, the gorge about 27 m deep and the outer walls 40-60 m. The camp
  bank sits about 2.5 m over the water.
- **Why 0.4.** Cheap previews scaled the figures instead, by 1, 2, 2.5 and 3: the same picture as scaling the world.
  At 2.5 (world 0.4) a Sasquatch beside the river reads at a believable size and the walls still tower over it.
- **The scale sheet** (`01-scale/`) shows a 2.5 m Sasquatch and a 1.8 m person on the bank, before and after, near
  and wide.
- **The opening is unchanged in look.** The 0-16 s before/after against Pass 1 (`03-opening-before-after/`) is the
  same picture frame for frame by eye. PSNR is 31.5 dB (M1, at scale 1, was 52.5 dB). The residue is the water's
  ripple pattern and the scan's finest lines, now drawn in a world 0.4 the size. The Pass 1 crossings keep their times: portal 1 at 32.025 s, in its bank wall,
  scaled.

## 2. The portal's spill across the water

At Gate A the opening's corruption reached the riverbed: its blocks and glyphs rose through the water and spread
across the river toward the wall. ADR-1873's `floor` had kept the bed under the water since Gate B. The floor is
removed, so the spill is back, together with the torn aperture, the clear centre and the Moon view of Gate C.
`02-water-spill/` compares it with the Gate A clip. On a crop of the river (20-31 s) it is steadier than Gate A's:
- irregularity p99 0.039 against 0.084;
- max 0.058 against 0.130.

## 3. A short Moon (crossing 1 at 32.025 s, crossing 2 at 47.870 s, bar 25)

The long terminator crossing is gone. Of the two options I chose the second, pushing the bases out of sight and
glimpsing them as the camera rises. The scan-and-turn-around option needs the camera to turn its back on the Earth.
- **Arrival.** The camera pops out of portal 1 low over the sunlit plain, 5 m up, with the Earth ahead.
  The exit's y is a height over the Moon's ground: my first try put the camera underground for a second and a half.
- **Moon setup.** The Moon is tighter-curved (25 km), so the light falls off ahead. The domes stand in the great
  basin's night beyond its near rim, flattened a little (scale y 1.5 for 1.7-2.2). From 5 m up they are below the
  rim's line; I measured the heights on the generated ground.
- **The rise.** The camera rises to 158 m between 37 and 43 s, and the glowing domes come over the rim, across the
  basin's night.
- **The dive.** The camera dives into the portal crater on the plain, short of the rim, with Gate C 2's near-vertical
  drop along +x. The canyon through the hole is upright, and the crossing is the same shape as M1's.
- **The Earth answers the music.** `response.bass` drives the Earth node's `emissiveBoost`. The Earth glb now
  carries its own texture as a faint emissive, so the bass lifts a glow on the day side. This is a first pass for the
  owner to tune: the route is in the project.

The freed seconds go to the later worlds, crossings on bar lines (bar 17 at 32.0 s, 1.9835 s a bar):

| Crossing | Time | Bar | Into |
|---|---|---|---|
| 1 | 32.025 s | 17 | the Moon |
| 2 | 47.870 s | 25 | the canyon at dusk (the encounter) |
| 3 | 69.69 s | 36 | Mars (M2) |
| 4 | 101.4 s | 52 | the canyon at night |
| 5 | 121.2 s | 62 | the ocean |
| 6 | 147.0 s | 75 | the UFO |
| 7 | 172.8 s | 88 | the canyon at dawn, to the end at 210.5 s |

## 4. The campfire, and the Sasquatches

- **The fire.**
  - Logs in a teepee, a ring of stones and a glowing ember bed, made in Blender (`pass2/build_campfire.py`, all
    geometry made in the script, real size).
  - Flames in two particle layers: a hot core and stretched outer tongues.
  - Embers rising and drifting, and a soft smoke column.
  - The point light above the flames, breathing slowly (ADR-1888, revised).
- **A flicker the measure caught.** ADR-1888's flicker carried fast terms that ran at 6-12 Hz at the fire's rate,
  near a 30 fps render's Nyquist. The fire-lit ground jumped up to 7 % in brightness frame to frame. Only the slow
  terms remain: a breath of a few hertz.
  - The encounter's irregularity median fell from 0.226 to 0.057, and its max from 2.25 to 0.37.
- **An engine bug the fire found.** A particle emitter's `position` parameter had a hard range of ±1000 m. The camp
  is 1.35 km from the origin, so its flames were silently clamped 350 m away; that is why M1 showed no flames. The
  range is now ±100 km like a world position, with a test.
- **The encounter.**
  - The camp is on the low bank right of the dusk exit, 16 m out and 16 m along (real metres).
  - The camera comes out of the wall, drops to eye height (1.8-2.1 m over the ground) and passes about 6 m from the
    fire. It holds on the three of them from 49 to 59.5 s, then leaves down the canyon.
  - The Sasquatches stand on the fire's far side, facing it and so the camera.
- **The pale fur.** The fur cards are alpha-tested now (glTF MASK). Blender 5's exporter wrote them as blended
  whatever the material asked. Blended cards let the bright ground behind a back-lit Sasquatch show through its fur,
  which is what read pale and cottony. The palest variant's fur is also darker.
- **The contour lines at dusk** are at a quarter (ADR-1894, `attractorWorld/contourGain` in the dusk look). By day
  they are unchanged.

## Known weaknesses

- The domes are flatter than the approved proof (needed to hide them from low down).
- The flames are particles: soft, bright tongues rather than a simulated fire.
- The smoke column reads as a darker veil against the dusk sky.
- The exit from the wall still drops past the water's edge before the camp: about a second of water and rock.
- The Earth's music response is a first pass.
