# Dimensional portals, Gate C 2: out of a canyon wall

After the Gate C review, the owner asked for the second portal's canyon side to be part of a canyon wall, as portal
1 is: we come out of the rock, not out of the dusk sky. The coordinator added five notes. This is the polish round
before the merge.

Media is in `~/Desktop/av-gen-review/101-strange-attractor/portals-c2/`; the numbers are in `05-measures.txt`.
ADR-1883, 1884 and 1873 are revised.

## The exit site (`00-exit-candidates/`)

The candidates come from the real terrain. Along the river run, at three heights, I walked out from the river to the
first wall. Each wall point's downhill normal is the exit direction, and I measured how far that ray runs clear.
There were four sites worth a still. Each still is from the exit pose, under the final dusk light.

- **A, the west wall, facing across the river.** Close against the opposite wall: cramped.
- **B, the east wall, facing down-canyon (chosen).** 43 m up a 56-degree face. More than 200 m of open water straight
  ahead, the walls leading in, and the lit far mesas at the end: the canyon opens in front of us.
- **C, the east wall, facing up-canyon.** The most striking lit wall, but straight across the river. The flight would
  turn hard at once.
- **D, the west wall on the upper run.** Cramped, the walls in shadow.

## The Moon side, and why

The Moon side stays in the crater floor, turned:
- The floor's axis (down) maps onto the wall's (level, out of the rock).
- The dive's heading maps onto the world's up.

The camera crests the hero's rim at 45.3 s and rises over the bowl. From 50.2 s it hovers above the hole, looking
down as the field and the opening grow under it. Then it drops almost vertically into the hole, crossing at 53.810 s
on bar 28. It comes out of the wall nearly level, with the world's up.
- **No horizon jank.** Through the hole, the canyon is upright from the moment the hole opens: the view down the hole
  is the view out of the wall. The crossing changes nothing on screen (`01-crossing-2/`).
- **Why not the inner wall.** The hero's inner wall slopes at about 30 degrees, so a vertical opening there would stand
  in the slope like a window rather than a hole torn in rock. It would also give up the floor opening, Gate C's
  highlight.
- **Re-timed.** The dive is now rim at 45.3 s, hover from 50.2 s, a 3.6 s vertical drop, and the crossing on bar 28.
  It was 300 m in 4.8 s.

**Seeing out through the rock.** Until the crossing, the camera carried through the portal is behind the canyon
wall. The dusk scene is clipped at the exit's plane for the camera's passes, but not for the shadows, so it looks out
through the rock.

## Coordinator notes

1. **The first seconds after the exit.** The 3 s of flat violet water seen from above are gone. The camera leaves the
   wall level, with the canyon opening ahead: a lit wall on the left, the river leading to the lit far mesas. It then
   runs low over the water, drifting right with the river and rising slowly, to 60 s.
2. **The dusk light** (`02-dusk-before-after/`). A low sun (8 degrees) behind-left of the exit rakes the west wall and
   the far walls. Lit sandstone stands against violet shadow, and the shadows are long. The fills are cooler and lower,
   so shadow reads as violet, not mud. The sky and water stay violet-indigo.
3. **The floor's field is pulled in** (`03-moon-floor-field/`): radius 260 to 60 m, the scan rings 700 to 300 m.
   The code concentrates around the opening, with clear regolith beyond.
4. **The Earth is reframed** (`04-earth/`): ahead of the route, 3 degrees up. Its top is near the frame's edge from
   44 s, and it leaves the frame when the camera tips down to the hole.
5. **The dive is re-timed** (above). The 55.5 s join and its view turn are gone: the dusk flight is its own knots.

## Measurements (`05-measures.txt`)

| Window | Irregularity p90 | p99 | max |
|---|---|---|---|
| 0–16 s | 0.012 | 0.044 | 0.063 (unchanged) |
| 16–30 s | 0.013 | 0.027 | 0.035 |
| 30–34 s (crossing 1) | 0.033 | 0.071 | 0.091 |
| 34–47 s (the Moon) | 0.042 | 0.151 | 0.252 (43.40 s) |
| 47–53.6 s (the hover and the opening) | 0.071 | 0.296 | 0.331 (48.50 s, where the camera tips down to the hole) |
| 53.6–54.2 s (crossing 2) | 0.184 | 0.312 | 0.312 (53.83 s) |
| 54.2–60 s (dusk) | 0.020 | 0.184 | 0.250 |
| 0–60 s | 0.026 | 0.129 | 0.331 |

Frame differences:
- **Crossing 1:** 1.34, 1.34, 1.41, 1.45 … with no step.
- **Crossing 2:** 1.18, 1.23, then 0.89 on the first frame out of the wall, then 1.37, 2.22, 3.02 … as the camera
  accelerates away from the wall. There is no grade step.
- **No black frames.** A first render tipped into the black sky for 0.8 s at 48 s, as the look-ahead followed the climb
  over the rim. The look now takes only a quarter of a climb. The smallest difference from 46 to 54 s is 0.65.

Both suites exit 0 on the final code:
- GPU: 730 passed, 1 skipped.
- CPU with assets: 4605 passed.

## Honest weak spots

- **The canyon-side tear is not seen.** The camera looks out through the wall, and the rock behind the exit plane is
  clipped. The portal's torn language is carried by the Moon's lips around the view. I turned the dusk tear off: its
  throat stretched the wall's foot into dark slivers in front of the camera.
- **A dark lip at 53.0–53.4 s.** In the last half-second before the crossing, a dark rounded edge of the Moon's torn
  floor crosses the bottom of the frame.
- **Later in the dusk run (57–60 s) the near walls are in shade.** Lit sandstone is on the far mesas and the left wall's
  rim only; the first seconds out of the wall carry the strongest warm light.
- **The hover looks straight down at the field** for about 3 s (50–53 s), the opening growing in the middle. It reads,
  but it is static next to the glide before it.
