# Pass 3: the owner's notes after Phase B (2026-11-05)

The owner reviewed the Phase B (canyon day) hand-back from the hero agent (scene/sa-hero a61ebfbf, media
`pass3-env/02-canyon-day/`, the clips in `clips-after/` and `clips-before/`).

## 1. Revert to the before look

- **Phase B is not merged.** The canyon day stays as it is on scene/strange-attractor (ea97208c): no canyonRock v2,
  no talus piles, no landmarks. scene/sa-hero keeps the work as a branch; nothing on scene/strange-attractor needs
  reverting, because Phase B was never merged into it.
- The ocean (E, final), the Moon (F, the M3f gaze) and the rebuilt night pass stay; the owner approved them.

## 2. A full render of the film as it is now

- Render the whole film with sound from scene/strange-attractor at ea97208c, with the usual command, to
  `~/Desktop/av-gen-review/101-strange-attractor/pass3-env/09-current-full/strange-attractor-pass3-current-full.mp4`,
  plus a contact sheet every 5 s.
- This is a review render of where things stand, not the end-of-pass render (that is still `10-full/`).

## 3. The camera paths: point A to point B (a standing rule from now on; it refines 54- §1)

The owner, watching the clips: "some of these camera paths look quite poor based on the clips - bad pacing, camera
looking all over the place, I'm looking for more of a linear or curved trajectory for the camera from one portal to
the next. Point A to point B should be about a straight line and it should pass by characters on the way."

- **The shape of each leg:** from the arrival portal (A) to the next portal (B), the path is about a straight line,
  or one gentle, continuous curve. No wandering, no S-bends, no detours to visit things, and no doubling back.
  - Measure it: the ratio of the path length to the straight-line distance A→B per leg (aim for under about 1.15),
    and the number of turn-direction reversals in the heading (aim for 0, at most 1 for one gentle S where terrain
    forces it).
- **Where the camera looks:** along the line, toward B. The look direction changes only as slowly as the path
  bends. "Camera looking all over the place" is the defect to remove: no aim swings, no gaze tracks that wander.
  - Measure it: the per-frame yaw and pitch rate of the look direction (report the max and p95 per leg, in °/s),
    as well as forward-vs-velocity (54- §1).
- **Pacing:** an even, steady speed along the line, eased only at the start (out of A) and the end (into B). No
  crawls, no stalls, no sudden speed-ups.
  - Measure it: speed per leg (min, max, and the ratio max/min outside the first and last second).
- **The characters:** the line is placed so it passes by the characters on the way: the Gray, the Sasquatches and
  the craft at the camp, the rover on Mars, the subs, the octopi and the base in the ocean. The camera does not turn
  to them; they are passed, near enough to be seen in frame, while the camera keeps looking toward B.
- **Kept:** the portal positions and openings, the portal animations and bursts, the dives into portals (the night
  river dive, the pyramid portal), the music sync and each leg's start and end times. Rising out of an arrival and
  diving into the next portal are still welcome; they happen at the ends of the line, not in its middle.
- **The Moon:** the owner accepted the M3f Moon gaze as a Moon-only exception to the forward-vs-velocity rule
  (60- §6). The Moon leg still gets the straight-line and pacing treatment.

## 4. Process

- First the full render (§2), so the owner can see the film as it is.
- Then a plan: for each leg, a top view of the current path against the proposed A→B line with the characters and
  portals marked, and the numbers above for each. Stop for the owner's OK on the plan before rebuilding the paths.
- After the OK: rebuild the paths leg by leg, with the regression guard (luma, near-geometry, the crossings) and
  the new measures, and render the per-leg clips with sound.
