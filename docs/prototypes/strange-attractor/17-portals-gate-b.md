# Dimensional portals, Gate B: portal 1, the crossing and the Moon

The owner approved Gate A ("I think it looks good"). This gate folds in the owner's and the coordinator's fixes, then
builds the portal view, the crossing at 32.0 s and the lunar flight to 44 s.

Media is in `~/Desktop/av-gen-review/101-strange-attractor/portals-b/`; the numbers are in `05-measures.txt`. New ADRs:
1879 (the portal view), 1880 (the crossing) and 1882 (the Moon). ADR-1872 and ADR-1873 are revised for the Gate A
fixes.

## Gate A fixes

**A clear centre.** The owner asked for the middle of the portal to read as a clear hole, and now as a clear window
onto the Moon.
- Shards stream along the rim and a little in past the edge, then back through the opening's plane, where the Moon
  covers them. They never cross the middle or hang there facing the camera.
- They thin out as the camera closes in. The 30–31.6 s gold storm is gone: the worst irregularity there was 0.728
  and is now 0.112.
- Nothing is drawn in front of the Moon inside the opening except rock that really stands in front of it. The pass
  pulls the plane 25 m toward the eye.

**Lips.** They are now torn slabs: strata-shaped cells of rock, 6 × 2.5 m, each pushed into or out of the wall,
drawn outward and lifted by its own amount. There is no fan and no symmetry.

**The portal's face is lit.** A point light in the opening, warm at the seam, turning moon-white as it opens, lights
the shadowed face, so the slips and echoes read there.

**Timing.** Faint scan pulses from the portal from about 16 s (the field gate is now 0.06); the glyphs arrive from
about 19 s.

**The 25 s slit** is no longer a flat glow. It is the Moon, seen through the narrow opening as it splits.

## The portal view (ADR-1879)

- The Moon is its own scene file, `strange-attractor-moon.scene.json`, written by `build_moon.py`. The canyon
  scene's `"portal"` block loads it.
- It is rendered by its own renderer from the canyon camera carried through the portal: translation plus a −24.56°
  yaw, so the arrival heading faces the lunar route.
- It is composited where the camera's ray meets the corruption's opening, the same aperture the rock tears to.
- The pass runs before the water, so water and glossy reflections read the Moon. I did not get a shot where the river
  sits in front of the opening: the camera crosses the river onto its bank before the hole is open. The reflection
  path is in place; this sequence does not show it.

## The crossing (ADR-1880)

- The flight crosses the wall's plane at 32.025 s, computed from the flight samples.
- The aperture fills the frame from about 31.5 s. At the plane, the world switch is invisible.
- Frame differences across the crossing (31.83–32.27 s): 0.68, 0.69, 0.71, 0.70, 0.71, 0.72, 0.72, 0.69, 0.75, 0.86 …
  with no step.
- After it, the lunar flight leaves with the arrival velocity (C1) and eases its view to its own look-ahead over
  1.6 s.

## The Moon (ADR-1882)

**Terrain.**
- A generated world: a 4 km tile at 2.5 m, and a 32 km skirt sunk beneath it.
- Craters follow the small-crater size-frequency law, with profiles calibrated on the **LROC NAC DTM of Linné**
  (depth 0.209 D, rim 0.037 D, bowl exponent 1.9, ejecta falling as r^-4.3 to -5.2), and degrade with age.
- A fresh 950 m hero crater lies ahead; Gate C opens the floor of the portal crater.
- The surface falls away with the Moon's curvature.

**Rocks.** About 46,000 faceted boulders, 0.3–4.5 m, gathered on fresh rims and ejecta. They give the near ground its
scale.

**Shading.**
- The regolith is a lunar-Lambert material program, Lommel-Seeliger blended by phase, with an opposition surge.
- A 9° sun from the right; crisper shadows (softness 0.15, 2.5 km of cascades).
- A weak, warm regolith bounce as the only fill. The sky is black, with no stars.

**The blue cast in the shadows, explained.** With no environment map and no sky, the shader lit surfaces with a
constant blue hemisphere (0.10, 0.12, 0.20) that ignored `environment.intensity`. A new opt-in key,
`environment.fallbackAmbient: false`, removes it, and the Moon sets it. Shadows went from RGB (17, 20, 31) to neutral.

**The Earth.** NASA's Blue Marble on a sphere 1.9° across, about 5° above the horizon to the left of the route, lit
to a gibbous phase by the sun. It hangs 6 km away at true angular size, not at its real distance. A body much past
about 11 km is cut back to a small cap: only the part nearest the camera survives. I first took that for a scale
bug and over-scaled the node; it is a far-depth limit in the renderer that I did not chase. Over the 12 s of lunar
flight the camera moves about 600 m, so the Earth drifts a few degrees against the horizon.

## Data (approved, gitignored, never committed)

Both files are in `assets/space/`, which is newly gitignored:
- `NAC_DTM_LINNECRATER.TIF` and `.LBL`: LROC, NASA/GSFC/Arizona State University, PDS. Used only to calibrate the
  generator.
- `blue-marble-5400x2700.jpg`: NASA Visible Earth, `world.topo.bathy.200412.3x5400x2700.jpg`, public domain.
  `make_earth.py` builds `earth.glb` from it.

## Measurements (`05-measures.txt`)

| Window | Irregularity p90 | p99 | max |
|---|---|---|---|
| 0–16 s (the opening) | 0.012 | 0.044 | 0.063 (9.93 s; before 0.061 at the same frame) |
| 16–30 s (the build and the opening) | 0.012 | 0.029 | 0.034 |
| 20–30 s, centre crop (the corrupted wall) | 0.018 | 0.060 | 0.070 |
| 30–31.6 s (the old gold storm, 0.728 at Gate A) | 0.036 | 0.112 | 0.112 (31.37 s) |
| 30–34 s (the crossing) | 0.026 | 0.109 | 0.112 (31.37 s) |
| 33–44 s (the Moon) | 0.013 | 0.039 | 0.058 |
| 0–44 s, whole frame | 0.013 | 0.041 | 0.112 |

Both suites exit 0 on the final code: GPU 730 passed, 1 skipped; CPU with assets 4602 passed. The first CPU run
failed one CoreMIDI device test (`MidiInput with a wildcard filter auto-connects sources that appear later`),
untouched by this gate; it passed 3 of 3 alone and the full rerun exited 0.

## Honest weak spots

- **The Earth is 6 km away, not at its real distance** (see above), so it shows a little parallax. The far cut that
  forces this is not investigated.
- **Dark blocks on the river at about 23.5 s.** These are slipped riverbed blocks pushed up through the water
  surface. They read as holes.
- **The camera's rise at 42–44 s** shows a lot of black sky while it climbs to the crater rim. That is Gate C's
  approach.
- **Crossed, the canyon still renders under the Moon.** It is wasted work, about 25 ms a frame offline.
