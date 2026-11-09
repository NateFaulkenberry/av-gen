# Pass 3: the camera paths, round 2, results (65-)

Built on c59accea. Media: `~/Desktop/av-gen-review/101-strange-attractor/pass3-env/13-camera-paths-round2/`.
Leg times are unchanged. The crossings were checked with stills at -0.5, -0.1, +0.1 and +0.5 s
(`crossings-half-second.png`) as well as the trace. All six are continuous, including the new night entrance at
93.46 s and the arrival into the ocean at 121.217 s.

## Dusk: no tilt

The tilt was a roll, -86 degrees decaying to 0 in about 1 s. It came from the rebuilt Moon line diving along -z while
the Moon portal's `entryUp` is +x (the Moon flight's drop). Reverting the Moon removes it. Roll at the other arrivals
was already 0 (Mars, night, ocean, dawn), so nothing else shared the cause.

| First 5 s of the leg, each second | Roll max, deg | Pitch, deg | Pitch rate max, deg/s |
| --- | --- | --- | --- |
| 47.87 | 0.11 | 1.2 to -0.8 | 3.0 |
| 48.87 | 0.08 | -0.9 to -1.8 | 2.7 |
| 49.87 | 0.00 | -1.8 to -1.8 | 0.2 |
| 50.87 | 0.00 | -1.8 to -4.1 | 8.1 |
| 51.87 | 0.00 | -4.1 to 3.1 | 14.4 |

- c59accea: roll -86 decaying to 0 in about 1 s. ea97208c: roll 0.
- Heights are now absolute, so the leg has no terrain-following pitch swings. The climb to the outer cliff starts at
  about 52 s and the look takes 35% of it (`climbLook` 0.35), which halved that pitch rate (25 to 14 deg/s) and centres
  the glyph cliff and the portal as they open.
- Cost: forward vs velocity is 29.6 deg from 55.1 to 65.9 s, over the 25-degree line. The camera looks less steeply
  than it climbs. It reads as a steady view of the cliff.

## Moon: path reverted, mound removed

- The flight is the ea97208c path and the M3f gaze (60- §6). The domes are back at 40 to 42 s, with Earth in frame.
- The mound was a small random crater's rim overprinting the portal crater's floor, a lump of about 15 m. ADR-1912
  gives a lunar hero crater a `clear` radius that flattens what overprinted its floor (125 m here). The opening
  animation, lattice and glyphs are untouched.
- Shown at the owner's angle, on c59accea's line, in `moon-owner-angle-before-after.png`, and from the restored path in
  `moon-frames.png`.

## Night: new entrance, new river portal

- Entrance (25, 5, 1205), in the far bank's rock. River portal moved to (53, 1385), in open water, same portal and
  same dive.
- Line 182 m, length/A->B 1.01, 0 reversals, yaw p95 0.0, speed 6.1 to 6.3 m/s from 96 s, 3.1 to 3.8 m over the water.
- The camp passes on the camera's left, 26.6 m off at 115.9 s. It is in frame until 108.5 s and reads from about 100 s, so
  it reads for about 9 s. It is off the frame's left edge at the closest point.
- The red line's 7 m pass is not reachable: its end is on land. The bank at x about 60 is the limit for a straight line
  that ends in water.
- Nearest mesh 18.7 m. Terrain clearance is the 3.1 m cruise until the dive.
- A black rock face sits in the right third of the frame for about 4 s after the arrival. It is the far bank's unlit
  side, close to the new entrance.
- The ocean arrival moved with the portal, with no visible step. H-4 in 57- is updated.

## Dawn: hillside out of the frame

The exit is 86 m up and only 75 m from the camp, so the camp can only be seen looking down at it. Three lines were
tried at preview quality:

- Over the beach, steep: the camp reads, but the brown ground fills the frame, which is the note itself.
- High and level: rim and river all the way, but the camp is never in frame.
- River side of the beach (kept): the look-down lands on the water. River fills most of the frame from 153 to 169 s,
  with the camp low in the frame from 152.8 to 160.6 s, then the view levels up the river to the rim at 171 s.

Measures: length/A->B 1.15, 4.8 m/s cruise, lowest 20.4 m over the ground, nearest mesh 19.4 m. It is one curve, but
with a real bend: heading turns about 45 degrees over the leg. The camp is small, and the fire barely reads in dawn
light. Pitch goes to about -47 for the dive and levels at up to 19 deg/s around 170 s.

**Owner's decision (2026-11-06):** keep the dawn as it is at 133ce870.

## Ocean

The step at 127.5-128 s is gone: the ramp runs smoothly from 6 to 49 m/s over 124 to 130 s
(`speed-night-end-ocean-start.png`). The cruise is 49.2 m/s. **It cannot sit near 37 m/s without changing the times:**
the 950 m route in the 23.6 s between arrival and the bay averages 40 m/s, before any ramp.

## Guard

| Leg | Length/A->B | Reversals | Yaw p95 | Pitch p95 | Speed max/min | Fwd vs vel max/p95 |
| --- | --- | --- | --- | --- | --- | --- |
| Canyon | 1.08 | 0 | 5.6 | 13.7 | 2.4 | 14.2 / 12.5 |
| Moon (exception) | 1.94 | 1 | 39.2 | 46.0 | 3.0 | 52.7 / 50.1 |
| Dusk | 1.11 | 1 | 16.8 | 8.2 | 3.3 | 29.6 / 29.5 |
| Mars | 1.01 | 1 | 9.1 | 5.6 | 1.4 | 15.5 / 14.1 |
| Night | 1.01 | 0 | 0.0 | 19.1 | 1.8 | 12.5 / 3.4 |
| Ocean | 1.00 | 0 | 3.1 | 6.3 | 8.6 | 3.6 / 1.6 |
| Dawn | 1.15 | 1 | 1.8 | 8.0 | 2.0 | 19.0 / 15.7 |

- Luma vs the ea97208c full render: 0 flagged half-seconds. The last 30 s are about half as bright as that render,
  because the outro now holds the canyon rather than sky.
- Near geometry: nothing over the flag (over 0.5 of the frame within 20 m for 4 s or more). The short spans are the
  portal dives at 31-32, 67.5-69.5 and 119.5-121 s (`near-geometry-whole-film.txt`).
- Missed targets: speed max/min on canyon, dusk, night, ocean and dawn is over the entry and exit eases. The dusk
  forward-vs-velocity line above.

Canyon and Mars were not changed. The Mars clip is re-rendered so its end meets the new night entrance.
