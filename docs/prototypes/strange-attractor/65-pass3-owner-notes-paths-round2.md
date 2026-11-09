# Pass 3: the owner's notes on the rebuilt camera paths (2026-11-06)

The owner reviewed the leg clips from c59accea (`pass3-env/12-camera-paths/`). The owner's words, verbatim:

> leg desk - absolutely none of this camera tilting thing that happens out of this portal
>
> leg moon - can we edit out the part of the crater that it sticking up like a blob where the portal opens? screen
> shot sttached - revert back to the original moon camera pathing and keep that
>
> leg night - pick a different portal entrance - it would make more more sense to come out of the other side of the
> river, fly down the river with the characters on the left hand side as we pass by, then open up a portal further
> beyhond them further down the river

"Leg desk" is read as the dusk leg. Owner images (read-only, never commit them), in
`~/Desktop/av-gen-review/101-strange-attractor/pass3-env/12-camera-paths/00-owner-refs/`:
- `moon-crater-blob.png`: the mound at the Moon portal.
- `night-new-line-red.png`: the owner's red line drawn on the night top view.

## 1. Dusk: no tilting out of the portal

- Remove the tilt out of the arrival portal entirely: the horizon is level from the first frame, with no roll and no
  pitch swing as the camera comes out. "Absolutely none."
- Report roll and pitch rate over the first 5 s of the leg, before and after. If the tilt comes from a shared
  arrival behaviour, check every other arrival for it too.

## 2. Moon

- **Revert the Moon camera to the original path** (the path at ea97208c: the M3f gaze) and keep it. The Moon leaves
  the A-to-B rule for good: this replaces 61- §3 and 62- for the Moon, and the 60- §6 exception stands again. The
  domes come back with it; confirm that.
- **Remove the mound at the portal.** The crater ground bulges up like a blob where the portal opens. Flatten that
  terrain so the portal opens in the crater floor without the raised blob. Keep the portal's opening animation, its
  lattice and its glyphs. Show it at the owner's angle and from the restored path.

## 3. Night: a new entrance, a new river portal, one line down the river

This overrides the "portals stay" rule in 61- §3 and 63- for the night leg only, at the owner's instruction.

- **The new entrance:** the far side of the river from the current bank exit (see the red line's start, above and
  left of the current A).
- **The line:** straight down the river, with the characters on the camera's left as it passes.
- **The new river portal:** further down the river, beyond the characters (the red line's end). It is the same
  portal: the same opening into the river and the same dive, which the owner likes. Only its position moves.
- Keep the leg's start and end times. Set the line length so the cruise is an even, visibly moving speed: about
  6 m/s or more. A near-parked camera is the defect being fixed. Report the length and speed.
- Height above the water, and the passing distance to the camp, are the agent's call, so the characters read in
  passing with no camera turn. Check the river and terrain along the new line and at the new portal, and that the
  camp stays in frame for long enough to be seen.
- Everything the moved portals touch moves with them: the crossing, the arrival into the ocean, the night's luma
  and glide, and H-4 in 57-. If the new river portal changes where the ocean arrival must sit, the ocean's arrival
  moves to match, with no visible step.

## 4. Open from the coordinator's review of c59accea (64-)

- **Dawn:** the brown hillside filling the frame around 155–166 s, with the camp a speck in it. Fix it with the line
  or the look, keeping the leg straight, the camp pass and the canyon-rim ending.
- **Ocean:** the speed step at 127.5–128 s (24 to 34 m/s). Smooth it. With the night now faster, re-shape the ramp.
  Report whether the cruise can sit nearer the planned 37 m/s without changing the leg's times.

## Delivery

Per-leg clips with sound for the changed legs (night, dusk, Moon, ocean, dawn) and before/after stills to
`pass3-env/13-camera-paths-round2/`, with the guard as before and the crossings checked with ±0.5 s stills. **Look at
your own clips' frames before handing back**, not only the measures. No suites, no merge.
