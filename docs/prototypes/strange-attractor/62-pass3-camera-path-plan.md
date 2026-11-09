# Pass 3: the camera-path plan (61- §3, point A to point B)

**Status:** a plan for the owner's OK. No path has been rebuilt.

**Media** (in `~/Desktop/av-gen-review/101-strange-attractor/pass3-env/11-camera-path-plan/`):
- `path-<leg>.png` and `00-all-legs.png`: one top view per leg. The current path is red, the proposed line green,
  the characters yellow. A is the arrival and B the next portal.
- `path-measures.json`: the measures below.
- Measured on the film at ea97208c with `examples/strange-attractor/pass3/path_measures.py`.

**How the top views are drawn:**
- The Moon, Mars and the ocean are drawn in their own coordinates (the camera trace fitted to each leg's flight
  knots), so the characters sit where they are.
- The canyon, dusk, night and dawn are drawn in the canyon's coordinates.

## 1. Where the legs stand now

Rate columns are the look's yaw and pitch rate in deg/s, max / p95. "Fwd vs vel" is forward against velocity in
degrees, max / p95. Speed is in m/s, outside each leg's first and last second.

| Leg | Length / A->B | Turn reversals | Yaw rate (deg/s) | Pitch rate (deg/s) | Speed min-max | max/min | Fwd vs vel (deg) |
|---|---|---|---|---|---|---|---|
| canyon 0-32 | 767 / 690 = **1.11** | **5** | 25.5 / 19.7 | 17.8 / 13.1 | 8.8-36.7 | **4.2** | 14.3 / 9.6 |
| Moon 32-47.9 | 448 / 230 = **1.95** | 3 | **108 / 45** | 34 / 28 | 15.4-49.3 | 3.2 | 64.5 / 50.1 (owner's exception) |
| dusk 47.9-69.7 | 203 / 172 = **1.18** | 2 | 29.2 / 12.8 | **61 / 30** | 3.7-23.6 | **6.4** | 40.4 / 7.7 |
| Mars 69.7-93.5 | 925 / 911 = 1.02 | 3 | 11.2 / 5.0 | 10.3 / 3.8 | 27.2-46.8 | 1.7 | 14.2 / 13.9 |
| night 93.5-121.2 | 107 / 70 = **1.52** | **5** | **79 / 35** | **73** / 12 | 1.5-14.5 | **9.4** | 20.1 / 7.4 |
| ocean 121.2-147 | 962 / 961 = 1.00 | 0 | 3.9 / 3.4 | 7.1 / 4.8 | 2.4-56.7 | **23.4** | 2.1 / 2.0 |
| dawn 147-211 | 320 / 268 = **1.20** | 1 | 37.9 / 4.4 | 26.5 / 3.9 | 1.1-7.1 | **6.6** | 19.1 / 1.4 |

**What the numbers say, and what the owner saw:**
- **The worst legs are the night (my pass-3 loop over the river), the Moon and the dusk.** All three wander, or swing
  the look (yaw up to 79-108 deg/s, pitch up to 61-73 deg/s), and their speeds vary 6-9x.
- **The canyon's path is long but snakes.** It follows every bend of the gorge (five reversals), so the look sways.
- **Mars and the ocean are already straight.** The ocean's problem is pacing only: it leaves the night's dive at
  3 m/s and races to 57 m/s.
- **The dawn crawls** at 1-7 m/s.

## 2. The proposal, leg by leg (kept: every portal's position, opening, animation and timing; the dives; each leg's start and end times)

**The rule for every leg:**
- The line runs from A to B: straight, or one gentle curve where the terrain forces it.
- The look is toward B (ADR-1909 gaze along the line). It turns only as fast as the line bends.
- An even cruise speed, eased over the first and last 1.5-2 s.
- The characters are placed beside the line, never turned to.
- **Targets:**
  - length / A->B under 1.15;
  - 0 turn reversals (at most 1 where terrain forces it);
  - look yaw under 10 deg/s p95;
  - speed max/min under 1.5 outside the eases;
  - forward vs velocity under 25 deg (the Moon is the owner's exception).

**The legs:**

1. **Canyon (0-32 s).** One gentle curve: the current path smoothed into the gorge's single large bend (a 7 s
   average, ends pinned).
   - Checked against the terrain: it stays over the river and the gorge floor, at least 9.7 m over the ground at the
     current heights.
   - The descent from the rim into the gorge (8-14 s) stays, as the line's one dive. Cruise about 22 m/s.
   - No characters on this leg.
   - This is a change to the attractor flythrough's generated path (`attractorWorld`; smoothing its flight).
2. **Moon (32-47.9 s).** A straight line from the arrival to the crater-floor portal (230 m in 15.9 s, about
   14.5 m/s).
   - The 158 m climb is replaced by a shallow rise of 20-30 m mid-line, then the dive into the crater.
   - The Earth and the domes stay in frame from the line itself. Its direction is about toward the Earth, which is
     also why M3f's gaze looked fine.
   - With a level line the gaze can follow the path, so the owner's Moon exception may no longer be needed.
   - The domes are 1-2 km off the line, so they are seen across the basin, not passed.
3. **Dusk (47.9-69.7 s).** A straight line, from the Moon portal's exit over the river to the dusk portal's climb
   (172 m, about 8 m/s).
   - It passes the camp on its near side as it does now: the Sasquatches, the Gray and the craft about 15-25 m to
     the side, glimpsed while the look stays toward B.
   - The dive-like climb into the dusk portal stays at the end.
4. **Mars (69.7-93.5 s).** Already straight (1.02). The pacing evens out from 27-47 to a 38 m/s cruise. The rover and
   its tracks stay beside the line; the pyramid dive stays.
5. **Night (93.5-121.2 s).** A straight line from the bank's exit (66.5, 1295) to the river portal (51.5, 1367):
   70 m in 27.8 s, a steady 2.5 m/s, 4 m over the water along the river's edge.
   - It passes the camp about 15-20 m to its left: the fire, the Gray, the Sasquatches and the craft, seen ahead and
     then beside.
   - The glide into the river portal stays at its end (25 deg, 3.2 m/s; it already almost matches the cruise).
   - This replaces my pass-3 loop over the river (ratio 1.52, five reversals, yaw 79 deg/s), which fails the new
     rule. The bank's boulder that M3g crept past is about 10 m off this line and passes in about 4 s at this speed
     instead of 18.
6. **Ocean (121.2-147 s).** Already straight (1.00). The pacing changes:
   - an even cruise of about 37 m/s past the subs, the octopi and the base;
   - a longer, smooth ease out of the night's dive (3 m/s) over about 3 s;
   - an ease into the bay portal over 2 s.
   - The dive's 3.2 m/s at the crossing is fixed by the portal's continuity. If the owner wants less of a ramp, the
     night glide's speed could rise to about 6 m/s (both sides change together).
7. **Dawn (147-211 s).** A straight line from the ocean's exit portal in the canyon wall, climbing to the final view
   (268 m in 64 s, a steady 4.2 m/s).
   - It passes the camp about 10-20 m to its side in the first 15 s, as now.
   - It replaces the current path's crawl (1-7 m/s) and its one reversal.

## 3. How it will be checked (after the OK)

- **For each leg:** the new measures (as in §1), the forward-vs-velocity audit, clearance (terrain and meshes), the
  luma track against the pass-3 baseline, the near-geometry track (canyon legs), and both crossings at +-0.5 s.
- **Media:** the per-leg clips with sound and before/after stills at the shared cameras.
- **The hero agent:** its parked Phase C clearing work would need the night and dusk lines, so 57- H-4 is updated
  with the new corridor once the owner approves.
