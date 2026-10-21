# Rainbow World 21: the plane-like camera, research and choice

The owner's second camera note (`20-camera-note.md`) asks for this: "think of it like a plane, flying, gliding,
soaring, flipping occasionally or barrel rolling ... deep research on how to do tweening to make the physics feel
right". This note covers the options, what we chose, and why.

## 1. The hard constraint

Seek must equal play (standing rule). Ruled out: a runtime follower, meaning a spring or a flight model stepped once per
frame in the engine. Its state depends on the frames before it, so a seek would have to replay the whole ride from 0
(the control replay, ADR-1168, does this for sources at a cost of seconds) or land somewhere different. It would also
be new engine code during a perf-sensitive phase.

The camera stays what it is now: a baked spline (position plus roll per control point, ADR-1541/1542), and a timeline
that maps time to arc length. All the "physics" runs **at build time**, over the whole loop at once. That means it can
also look ahead: anticipate a climb, or time a roll to land on a beat. A runtime follower can't.

## 2. The options

| Technique | What it gives | Source | Use |
|---|---|---|---|
| Gaussian smoothing (the current glide) | Fluid, but symmetric and characterless. Every change takes the same S-curve, with no weight. | - | Kept for the base low line over the ground |
| Second-order dynamics (spring-mass-damper, `y + k1 y' + k2 y'' = x + k3 x'`, parameterised by frequency f, damping zeta, response r) | Inertia: the motion lags, settles, and can overshoot. f sets how fast, zeta how much it settles (1 = critically damped, no overshoot), r how it anticipates. One model tunes altitude, bank and roll alike. | t3ssel8r, "Giving Personality to Procedural Animations using Math" (2022); Lowe, critically damped springs (Game Programming Gems 4) | **Chosen** for altitude and bank. Run forward then backward over the periodic loop: zero phase, so the plane starts climbing before the open stretch, as a pilot would, and the loop closes without a seam |
| Easing curves (sine, cubic, quintic smootherstep) | A fixed shape between two states. Quintic `6u^5 - 15u^4 + 10u^3` has zero velocity **and** zero acceleration at both ends, so a roll starts and stops without a jolt. | Penner (2001); Perlin's smootherstep (2002) | **Chosen** for the earned rolls: the roll angle rises 0 to 360 degrees over about 3.5 s, peaking near 190 deg/s, which is a gentle aileron roll |
| Aircraft model: coordinated turn | A plane in a turn banks so that lift balances gravity plus the centripetal force, `tan(phi) = v^2 kappa / g`. The bank follows from the path and the speed; it is never scheduled. | Standard flight mechanics (e.g. Anderson, *Introduction to Flight*) | **Chosen**: up = normalise(g * land_up + v^2 * lateral curvature vector), clamped to 38 degrees, then sprung. Gravity is the land's own up, so this holds on every face of the knot |
| Aircraft model: energy trade | Total energy `h + v^2 / 2g` is roughly conserved: a plane slows as it climbs and gathers speed as it dives. | Same | **Chosen**, with real g: `v^2 = V^2 - 2 g (h - mean h)`, so a 30 m soar trades about 15% speed. This replaces the climb-rate heuristic |
| Arc-length reparameterisation | Constant speed along a spline regardless of how the knots are spaced. Speed control becomes a function of distance. | Wang, Kearney and Atkinson, "Arc-length parameterized spline curves for real-time simulation" (2002) | Already in place: the timeline keys are arc length per 15 m sample, timed by the speed profile |
| Chase camera with look-ahead | A camera that looks at a point ahead on its own path. The nose follows the velocity, so climbs and dives read as pitch. | Haigh-Hutchinson, *Real-Time Cameras* (2009) | Kept: `camera/lookAhead` 90 m |
| Barrel roll vs aileron roll | An aileron roll turns about the flight path. A barrel roll corkscrews the path round a line. | Aerobatics definitions (FAA *Airplane Flying Handbook*) | The aileron roll. A corkscrew offsets the camera from its cleared line, which risks geometry; the roll alone reads clearly at this field of view |

## 3. The design

Everything is per 15 m sample of the loop, in `build_world.py`:

1. **Altitude:**
   - The ground line stays the gliding low line (3.5 m minimum).
   - The flight adds a soar on top of it. The target is high (about 30 m) over open meadow, and 0 within reach of:
     - an arch (the ride flies under its bow);
     - a rim crossing (the rim swing needs the low line);
     - every tree whose canopy the soar would enter.
   - The target goes through the spring, forward and back, at f about 1 per 260 m with zeta 1, and its slope is
     limited to about 12 degrees, so climbs are gentle and level-offs settle.
   - Tree clearance is checked in 3D against every tree's canopy envelope from the asset bounds, and the soar is cut
     wherever it would come near one. It never pushes the ride into new space beside the land.
2. **Speed:** the existing slope term (the land's climb) times the energy trade on the soar, smoothed, times the
   crossings' surge. The base speed is set so a lap is 190 s, against 211 s before: about 11% faster, as the owner
   asked ("a little faster").
3. **Bank:**
   - The coordinated-turn up is computed from the final path's lateral curvature at the ride's own speed, clamped to
     38 degrees, and sprung.
   - It fades to the land's up over the crossings, where the rim ellipse sets the up.
4. **Earned rolls:**
   - Each roll is one 360-degree aileron roll, quintic-eased over about 3.5 s, rolling into the current turn.
   - Candidates are the apex of a soar (out of a climb) and the drop (47.85 s). At the drop the flight plan adds a
     soar, so the ride climbs on the riser and rolls on the drop.
   - At most three per lap, spaced at least 900 m apart, never within 250 m of a crossing.
   - A roll is a whole turn, so the spline's roll winding (ADR-1542) carries it.

It is all baked: the same spline and timeline as before, so seek equals play with no new engine code.

## 4. Proving it

The proof is a 25 s clip that holds one soar and one earned roll (the drop roll). It is sent before the full lap.

It is checked for:
- the clearance printouts (`CLEARANCE` to other strands, `TREES clear`);
- no dead space at the soar's apex (the meadow and the arches must still fill the frame);
- the grass draw distance (42 m since the perf pass), seen from the soar.

## 5. Revision after gate 4: more soar, no bow rolls, the twitches measured and taken out

The owner chose the more-soar variant and asked for "no barrel rolls into the rainbows" and for the "twitchy camera
movements" to be smoothed (`20-camera-note.md`). It is now the only flight; the switch is gone.

**Measured first** (`scripts/camera_motion.py`: the engine's spline camera re-evaluated frame by frame at 30 fps over the
lap -- eye at `splineT`, aim `lookAhead` metres on, up the spline normal -- and the camera's angular velocity split
into pitch, yaw and roll, with the angular jerk and the look and up turns per frame). The sources, largest first:

1. **Kinks where the face path met each rim's ellipse:** the face path rose off the face's own normal, the ellipse off
   the band's up, at different lifts (4.5-7.5 m against 11 m), and the path was not smoothed within two samples of a
   crossing (to keep it off the cliff). Between two 15 m controls the line turned 40-70 degrees; the camera's look and
   up snapped there. The worst single frame turned the up 30 degrees (914 deg/s of roll, at 163.8 s).
2. **The roll steps:** the spline interpolates roll linearly between controls, so every kink in the up the land asks
   for is a step in roll rate -- a comb of jerk spikes every 15 m.
3. **The bow rolls** (three, ~170 deg/s): removed by the owner's note.
4. **The rim swing itself:** an ellipse 40 m out and 11 m up (a ~24 m radius at its side) and a 1.45x surge round it.

**Fixes:**
- the face path's lift turns onto the band's up over the last 90 m before a crossing, and the ellipse starts and ends
  at the face path's own lift (no kink by construction);
- the swing is 44 m out and 16 m up, its two halves' heights blended, and the surge 1.15x;
- the path is smoothed one sample everywhere, one and a half through the crossings, then held 4.5 m off the band's
  cross-section by push-and-resmooth (`RIM clearance` in the build log);
- the roll is smoothed as an unwrapped angle by the zero-phase spring (one period per 320 m), so the flip over a rim is
  a long, even turn that starts before the crossing and ends after it;
- the bank is capped at 30 degrees and sprung over 400 m (a big glider);
- **found on the way: the soar flew through trees.** The 158-173 s stretch passed through a Great Tree's trunk and
  crown, and a pine stood between two controls on the line. The flight is now held 3 m clear of every crown (radius
  from the asset bounds, canopy bottom to top) and every trunk, checked every 3 m along the spline itself, not only at
  the controls, and 3.5 m over the meadow likewise; any hit pushes the nearby controls out as a smooth 90 m bump and
  the spline is rebuilt until nothing touches (`TREES along the spline`, `GROUND along the spline` in the build log);
- rolls: only on a phrase start out of a 12 m climb, 15 m up, and at least 400 m from any bow. The bows stand every
  ~500 m, so none qualifies: the flight ships with no rolls. The rim flips (the land's own) stay.

**Before / after** (whole lap, 30 fps):

| | Before (gate 4 more-soar) | After |
|---|---|---|
| Total angular rate, peak / p95 (deg/s) | 916 / 87 | 106 / 32 |
| Roll rate, peak / p95 | 915 / 84 | 103 / 21 |
| Yaw rate, peak / p95 | 74 / 30 | 44 / 16 |
| Pitch rate, peak / p95 | 58 / 29 | 33 / 22 |
| Roll jerk p95 (deg/s^3) | 6,490 | 1,900 |
| Up-vector turn per frame, peak / p95 (deg) | 30.5 / 2.8 | 3.4 / 1.0 |
| Look-direction turn per frame, peak / p95 (deg) | 2.5 / 1.3 | 1.5 / 0.9 |

The remaining peak is one crossing (the fourth quarter's, ~165 s), where the knot's own geometry is tightest.
