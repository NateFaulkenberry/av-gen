# Rainbow World 08: Stage 4 report (the world and the camera journey)

Stage 4 of the brief (§8, §10). The whole knot is built and ridden in AV Gen: a 5.7 km loop of band, a 7.4 km ride.

The generator is `examples/rainbow-world/build_world.py`, run in Blender. It uses the band's analytic surface and the
asset exporter, and writes:
- the band in 24 chunks;
- a composition with every placement;
- the live project.

The live project's ride is a saw LFO on `camera/splineT` (600 s a loop). It is a pure function of the transport, so
seek equals play.

Review images are in `~/Desktop/av-gen-review/74-rainbow-world/stage4/`:

| File | What it shows |
|---|---|
| `01-world-ride-stills.png` | 14 stills round the ride |
| `02-crossing-clip-sheet.png` | The 20 s rim-crossing clip, every 0.83 s |
| `clip-crossing-120-140s.mp4` | The rim crossing, 1280x720 |
| `clip-calm-462-477s.mp4` | A calm stretch under strands and a rainbow, 1280x720 |
| `03-knot-greybox-e6.png` | The knot chosen in grey-box |

## 1. The form, decided by grey-box (E6)

| Choice | Value | Evidence |
|---|---|---|
| Knot | (2,3) torus knot, 180 m per unit, z amplitude 1.0 (5.7 km loop) | At 220 m (E2 scale) four of eight stations were half sky. At 180 m most stations have land overhead. |
| Band | 260 m wide, 40 m thick, both faces meadow, rims built from the rock kit | Strand centrelines stay 300 m apart at their closest (measured), so the band never meets itself |
| Twist | One whole turn round the loop, carried by the spline's per-point roll and `rollTurns` (ADR-1542) | With no twist the band lay almost flat, and gravity hardly turned |
| Frames | The band is swept on AV Gen's own spline frames: `knot_path.py` mirrors `spatial/spline.cpp` | Probe: engine and Python normals agree to four decimals at nine stations |

## 2. The ride: how the clip's motion language was translated

The clip (01 §2) holds a landmark through a roll, pitches over an edge, lets a new gravity take over, resolves upright,
and pulls back through arches. Here:

- **Gravity turns with the land.** `camera/splineUp` 1 (ADR-1541): the camera's up is the band's up, so the twist
  rolls the view continuously and smoothly, with no spinning.
- **Over the edge, onto the other gravity.** Four times a loop the ride leaves the meadow, swings round the rim on an
  ellipse (54 m out, 14 m clear of the faces) and lands on the band's other face, where up is the band's down. Later
  it swings back over the opposite rim. That is the clip's 4.0-4.9 s pitch-over, roughly every 2.5 minutes. The
  ellipse's normal is the camera's up, so the roll during the crossing is the geometry's.
- **Landmarks recur.** Great Trees on great-hill shoulders, Ridge Pines on spines, Weeper groves on hollow rims, and
  eight rainbow arches, one at the middle of each long stretch on a face. The ride passes under each arch in the
  calm after a crossing.
- **The weave.** On a face the ride weaves across the band (two sines, periods divided to close round the loop),
  drawn out to the rim it is about to cross over the 220 m before the crossing.
- **Full frames.** The fisheye remap of a 106-degree camera (ADR-1540), a 70 m look-ahead, and the knot's tightness
  together keep strands in view. In the stills, 10 of 14 frames have land beyond the ride's own face.

**Determinism.** Every placement is seeded, and the camera is a pure function of `lfo.ride` (`pureInTime`). Stills at
t are rendered as one-frame offline renders at t: the frame play shows at t.

## 3. Richness (the coordinator's Stage 3 notes)

1. **Landform.** Six authored terrain movements, laid round the loop (never the same twice in a row) and mirrored at
   random:
   - layered cross ridges that recede along the ride;
   - a great hill with its hollow;
   - a valley with walls;
   - a ridge spine;
   - twin hills;
   - stepped benches.
2. **Deliberate dressing, by feature:**
   - cross ridges carry a line of trees with one gap;
   - spines carry Ridge Pines;
   - great hills carry a Great Tree on the shoulder and a rock outcrop;
   - hollows carry a flower meadow and Weepers on the far rim;
   - knolls carry an outcrop or a Blossom Puff.

   Foreground drifts sit beside the ride every ~120 m, and the land between features is left as rest.
3. **Clouds.** Placed by keep-out: never within the cloud's own reach plus 180 m of the ride, never through the land.
   The plastic wall in frame 30 is gone. The cloud program is warm on lit faces and mauve-violet in shade, and the
   gain keeps values below white. The cloud sea is now **one floor of billows** (14 km) under the whole world, so
   "down" is cloud, not a flat colour.
4. **Depth.** The atmospheric look at 0.42 over 3.2 km, tinted cool. Far strands recede in value and hue.
5. **Sky.** Deeper (zenith `#0E36B2`). Shaded faces are lifted by a stronger warm bounce from below (`lights/cloudsea`
   1.6).

## 4. Cost

The live profile at Ultra, 1920x1080, headless:

| Version | GPU median | Notes |
|---|---|---|
| v2 | 54 ms | First full knot |
| v3 | 70 ms | Richer dressing |
| v8 | **29.2 ms** (P95 33.6) | Geometry 17.0, shadows 11.3 |

The v3-to-v8 savings, in order of size:
- **Leaf cards stop at 240 m.** Beyond that the canopy core, tinted by the same program, is the tree.
- **Wood decimated** to ~2,500 triangles.
- **Rim rocks** at ~300 triangles, out of the shadow maps.
- **Grass, flowers, clouds and cards** out of the shadow maps (ADR-1545).
- **Outcrops** get a two-step mesh LOD.
- **Shadow range** cut to 240 m.

The 29.3 ms budget is met at the median but not at P95. Stage 8 has the rest; the band's shadow casting is the next
lever.

## 5. Weak points now (Stage 5-6)

1. **The crossings.** The camera passes close to rim rocks, and for several seconds the frame is half sky and cloud
   floor. The idea works (the other face arcs overhead as the roll completes), but the framing needs work: a lower,
   tighter arc, rocks thinned at the crossing stations, a rainbow or a landmark waiting on the far face.
2. **Clouds.** Warmer, but still soft and pale beside R1. The cloud floor's billows read only near the horizon.
3. **Flowers.** Visible as colour dots, but small at ride height.
4. **Weeper.** The curtain is heavy and its colour is not yet the lime of R4.
5. **The ride's rhythm.** Constant speed (12 m/s) and gentle roll in the calm. The clip's clarity, disorientation
   and resolution beats exist only at the crossings.
