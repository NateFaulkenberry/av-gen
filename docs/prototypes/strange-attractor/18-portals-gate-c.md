# Dimensional portals, Gate C: the Moon polish, portal 2, dusk and the whole 60 s

The owner approved Gate B. This gate folds in the Moon polish (the coordinator's review, which the owner agreed with)
and the two Gate B fixes, then builds the crater-floor portal, the dusk canyon and the return to 60 s.

Media is in `~/Desktop/av-gen-review/101-strange-attractor/portals-c/`; the numbers are in `05-measures.txt`. New ADRs:
1883 (portals nest; the crater-floor portal) and 1884 (the dusk world). ADR-1873 and ADR-1882 are revised.

## The Moon polish (ADR-1882 revisions)

**Boulders.** Gate B's were small white faceted pyramids.
- They are now rounded, irregular rocks: subdivided icospheres (one to three levels by size) with broad lobes, one
  or two split faces and finer lumps, and smooth normals.
- They are partly buried (a sixth to a third of their height), in the regolith's own grey.
- Sizes follow the lunar count law: many small (from 0.22 m), few large (up to 5 m). They gather on fresh ejecta,
  so the glide passes through boulder fields.
- Contact shadows are on, and the near shadow cascade is tighter.

**Ground detail.** The regolith program now shades what the 2.5 m mesh cannot carry:
- grit and lumps, by their slope along the sun;
- craterlets: Worley bowls in every size, faded out by 90 m.

Fresh craters carry irregular ejecta rays. The detail is clamped so it never blotches.

**Light.** The sun is 24° up, from the right and behind. At 9°, every crater floor on the route, the dive's
included, was in black shadow.

**The flight (34–46 s)** is reshaped:
- an establishing beat after the crossing: a rise with the Earth in the upper left;
- a fresh 160 m crater passing close beside the low glide, through its boulder field;
- the rim of an old 900 m basin as a ridge that the camera rises over at about 41 s, revealing the hero crater on
  the basin floor (earlier than Gate B's 44–46 s);
- a glide across the basin, cresting the hero's rim at 47 s, then the dive.

**The Earth.** It is 3.2° across (life: 1.9°), at 40 km, low in the upper left, gibbous.
- Gate B's "far cut" was not a renderer limit. A node's `scale` parameter is capped at 100, so a unit sphere could
  never be larger than 100 m.
- `make_earth.py` now writes a 100 m sphere.

## Gate B fixes

- **River blocks at 23.5 s.** The corruption has a `floor`: nothing at rest below it moves. The canyon sets it at the
  river's surface, so slipped river-bed blocks no longer push up through the water (`06-river-blocks/`).
- **The crossed world is not drawn.** Once the camera has crossed, the destination's renderer draws the frame and the
  world behind is skipped entirely. Gate B wasted about 25 ms a frame here; with three worlds the waste would have
  tripled.
- **The same grade on both sides.** The destination now finishes the frame with the source's post and temporal
  settings, which the engine sets on the root scene only. Without that, crossing 1 had a one-frame grade step
  (irregularity 1.46).

## Portal 2: the crater floor (ADR-1883)

**Portals nest.** The Moon is a scene file with its own `corruption` (facing down) and `portal` blocks, exactly like
the canyon's.

**Each world finds its own crossing.** A source hands its destination its camera as a pure function of time.
- The canyon crosses at 32.025 s.
- The Moon crosses at 51.830 s, on bar 27.

**The floor opening.**
- Cooler-gold lunar scan lines sweep in from about 46 s. Glyphs spread over the hero's floor.
- The floor cracks as a seam of light. It tears open to a ragged 46 × 40 m hole as the camera dives at it, with
  shards on the lips.
- Through the hole is the dusk canyon far below: violet water between maroon walls (`02-floor-portal-opening/`).

**The dive.**
- The camera crosses the floor pitched down about 40°. It emerges 150 m above the dusk river, still diving, and
  pulls out over the water.
- At 55.5 s it joins the canyon's own river run (the flythrough's second 17, at half speed, C1), so it continues
  along the river on a line already flown clear of the walls.

## The dusk world (ADR-1884)

- **The canyon** is the same canyon, from the same generation.
- **The sun** is 2.8° up, ahead of the river run.
- **The sky** is a violet-indigo zenith over a violet horizon, more indigo than pink.
- **The fills** keep the gorge from crushing: a cool sky fill and a violet bounce.

The walls read as violet and maroon stone, and the river mirrors the bright western sky (`04-dusk/`).

## Measurements (`05-measures.txt`)

| Window | Irregularity p90 | p99 | max |
|---|---|---|---|
| 0–16 s (the opening) | 0.012 | 0.044 | 0.063 (unchanged from Gate B) |
| 16–30 s (the build, the wall portal) | 0.013 | 0.027 | 0.035 |
| 30–34 s (crossing 1) | 0.036 | 0.130 | 0.152 |
| 34–46 s (the Moon) | 0.010 | 0.029 | 0.093 |
| 46–51.6 s (the floor opening) | 0.021 | 0.091 | 0.111 |
| 51.6–52.4 s (crossing 2) | 0.108 | 0.209 | 0.209 (a mean difference of only 0.64) |
| 52.4–60 s (dusk) | 0.028 | 0.181 | 0.354 (52.93 s: one frame 0.58 among 0.42–0.45, a water glint) |
| 0–60 s, whole frame | 0.016 | 0.091 | 0.354 |

Frame differences show no step at either crossing:
- crossing 1: 1.40, 1.40, 1.54, 1.83 … (the grade step of the first render was 3.20);
- crossing 2: 0.71, 0.68, 0.63, 0.64 ….

At the join, the difference settles from 0.83 to 1.04 as the camera takes the flythrough's speed.

Both suites exit 0 on the final code:
- GPU: 730 passed, 1 skipped.
- CPU with assets: 4604 passed.

## Honest weak spots

- **The dusk walls lean maroon** more than violet near the water. The sky and the water are violet-indigo.
- **The dive is quick.** The camera covers about 300 m from the hero's rim to its floor in 4.8 s. The crater is
  steep, and the crossing is held on bar 27.
- **The glyph field on the Moon's floor is wide.** It covers most of the hero's floor while the opening forms; it
  reads as the scan language, but it is denser than on the canyon wall.
- **The Earth is cropped** by the frame's left edge for a moment around 42 s, when the route turns right.
- **The join at 55.5 s** is C1, so it is smooth, but the view eases into the flythrough's own aim over 1.5 s; a
  small turn of the view is visible there.
