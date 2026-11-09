# Pass 3: the A-to-B camera paths, results (61- §3, 62-, 63-)

All seven legs are rebuilt. Media: `~/Desktop/av-gen-review/101-strange-attractor/pass3-env/12-camera-paths/`.
Portals, openings, crossings and the leg times are unchanged: the three crossings that jump in the camera trace fall
on the same frames as at ea97208c (32.000, 69.667, 121.200 s), and the other three are seamless in the trace in both.

## Measures, after (the 61- §3 targets: length/A->B under 1.15, 0 reversals, yaw p95 under 10, speed max/min under 1.5)

The before numbers are re-measured with the corrected tool: yaw and pitch now use the camera's own up, because a
portal world's "up" is not the root's y. They differ slightly from the 62- table. Fwd vs vel is max / p95.

| Leg | Length/A->B | Reversals | Yaw p95 | Pitch p95 | Speed max/min | Fwd vs vel |
|---|---|---|---|---|---|---|
| canyon | 1.11 -> 1.08 | 5 -> 0 | 19.6 -> 5.6 | 13.1 -> 13.7 | 4.2 -> 2.4 | 14.2 / 12.5 |
| Moon | 1.95 -> 1.08 | 3 -> 0 | 43 -> 1.5 | 39 -> 45 (the final dive) | 3.2 -> 2.3 | 32.2 / 30.2 |
| dusk | 1.18 -> 1.14 | 2 -> 1 | 15.4 -> 21.7 | 28 -> 18.4 | 6.4 -> 3.3 | 28.6 / 6.2 |
| Mars | 1.02 -> 1.01 | 3 -> 1 | 4.9 -> 9.1 | 3.8 -> 5.6 | 1.7 -> 1.4 | 15.5 / 14.1 |
| night | 1.52 -> 1.01 | 5 -> 0 | 34.9 -> 0.1 | 12.2 -> 16.6 | 9.4 -> 8.1 | 13.1 / 4.9 |
| ocean | 1.00 -> 1.00 | 0 -> 0 | 3.4 -> 1.8 | 4.8 -> 3.5 | 23.4 -> 9.8 | 5.2 / 1.5 |
| dawn | 1.20 -> 1.09 | 1 -> 0 | 4.6 -> 1.3 | 4.0 -> 6.8 | 6.6 -> 1.6 | 18.8 / 17.7 |

**Targets missed, and why:**
- **Speed max/min on four legs (and dusk's yaw and one reversal).** The measure takes everything outside each leg's
  first and last second, so it counts each leg's arrival and its eases.
  - Canyon (2.4): the river speed is the generator's; only the slowing into the portal was changed.
  - Dusk (3.3): it brakes from 24 m/s after its crossing.
  - Night (8.1): the arrival brakes from 21 m/s and the ease into the glide reaches 6; between them the cruise is even
    at about 1.4 m/s. That is slower than the 2.5 the plan expected, because the arrival uses about 20 m of the 70.
  - Ocean (9.8): the ramp from the glide's 6 m/s to a 51 m/s cruise.
- **Dusk's yaw (21.7 p95) and one reversal:** the curve past the camp.
- **Moon's forward vs velocity (32 deg, 35.4-36.8 s):** the look stays a little lower than the shallow rise, so the
  ground and the Earth stay in frame. It is under the owner's Moon exception.
- **Moon's and night's pitch peaks** are the dives at the ends of the lines (66 deg/s on the night glide's start).

## The owner's checks (63- §4)

- **Dawn past the camp:** the closest is 15.3 m at 166.9 s, about 20 s in, not 15. The camp lies 67 m along a 260 m line
  flown evenly in 62 s, so at an even pace it cannot come sooner. The line is bowed 30 m toward the camp (the straight
  chord passed 30 m off).
- **The outro:** it ends at 58 m over the canyon's floor, with the rim against the sky in frame to the end. Luma is
  about half the sky-filled frame's from 207 s; those flags are this change, not a regression.
- **The dusk mound (0:52-1:02):** no geometry within 20 m from 54 s on, and the sky stays in frame (the look follows the
  climb again). The camera still heads for the cliff portal, so the cliff grows ahead as it should.
- **The glyph rock (1:55-1:58):** the near rock is now 16 m off at p25, with the median depth 30 m. It covers 35% of
  the frame within 20 m.
- **near_track, canyon legs:** no flags over the whole film.

## The guard

- **Clearance:**
  - canyon: 9.9 m over the terrain before the portal, 11.1 m to a mesh (the craft is hidden by day);
  - dusk: 3.4 m and 10.2 m;
  - night: 3.9 m before the dive, 8.7 m;
  - dawn: 6.2 m and 25.6 m.
- **Luma against the current full render:** the flags are 35.5-36.0 s (the Moon's frame has more sky at the rise; not
  black, luma 30) and 207-210.5 s (the outro, above).
- **The night end and the ocean start:** `speed-night-end-ocean-start.png`. The night's cruise eases up from 116.5 to
  119 s, about 2 m/s per second, into the 6 m/s glide. The ocean then climbs from 6 to 51 m/s over about 7 s
  (124-131), where it climbed from 3 to 56 m/s over about 6 s before.

## Code

- ADR-1911 (`flightSmooth`), `canyon_scale.line_points`, `pass3/path_measures.py` (yaw and pitch from the camera's up).
- The portal gaze: the Moon has climbLook 0.2, lookDown 0.06; the dawn has climbLook 0.2, lookDown 0.08. The owner's
  earlier Moon exception (M3f gaze) is replaced: the level line lets the gaze follow it.
