# CHRONOS, The Devourer: Gate D1c, the eyes in the skull and a readable maw

The owner on D1b: "it's kinda looking cool. Give it one more pass; the eyes are still not in the right position."
Three-quarter stays the master composition. Media is in `~/Desktop/av-gen-review/104-chronos/devourer-d1c/`. There is
no engine change; this is `build_scan_creature.py` only, and no ADR.

## 1. The eyes: measured, re-seated, lidded

**What was wrong.** D1b's `seat_eye` took "the deepest hit near the landmark" as the socket. Seen from the front,
though, the deepest point near an eye is the side of the head. So it slid the left eye 45 mm outward onto the temple,
and left both balls 6-15 mm proud of the surface.

A frontal depth map of the shaped head (`devourer-d1c-orbit-measurement.png`) shows the real layout. The red circles
there are D1b's eye positions:
- Blay sculpted the eyeballs under the brow, at x = -0.112 and +0.026, with the nose bridge at -0.036.
- The brow stands 20-35 mm proud of the sculpted eye.
- There is no concave socket to find, because a bust's eyes are carved, not hollow.

**What D1c does** (`carve_orbits`):
- **Centres.** Each eye is centred on the measured sculpted eyeball. Both eyes now sit at the correct height and
  spacing from the nose bridge and cheekbones, from the master camera and from frontal.
- **Depth.** The ball's front is 8 mm behind the sculptor's eyeball surface, so the brow and the upper lid overhang
  and shadow its top.
- **Size.** The ball is scaled to the orbit (radius 19 mm against an opening about 44 mm wide), not the other way
  round.
- **The opening.** The lid opening is carved back to behind the ball's surface, as a wide ellipse, which keeps Goya's
  white-ringed stare. The sculpt's own lids frame it.
- **The lids.** The upper lid is thickened into a fold that stands forward and casts shadow at portrait distance.
  There is a lighter lower-lid rim.
- **No shells.** D1b's separate lid shells are gone.

The measured centres are constants in the script, and they move with any change to `proportion()` or `socket_warp()`.

## 2. The maw

- **A wider mouth.** The slit now runs to the corners, and is taller at the centre.
- **Cheeks that fold.** The jaw's weights reach less far out (MOUTH_HALF + 0.07, against + 0.12), and the blend at the
  corners is a tight band (18 mm, against 35). So the cheek creases at the corner instead of smearing into a hanging
  sheet.
- **A smaller angle.** The devour peaks at 52° through the wider mouth, down from 66°, which stretched the chin and
  beard.
- **The head turns.** At the devour it turns 13-14° toward the three-quarter camera, so the open mouth faces it and the
  cavity's darkness shows.

## 3. The cheap fixes

- **Pores halved.** The pore relief is halved (weight 0.15 to 0.075; bump strength 0.9 to 0.75).
- **Seams fixed.**
  - The hard straight steps of light across the cheek, which were visible in D1b's eye close-up, were the scan's
    unwelded atlas seams: each side kept its own vertex normal. D1c welds the mesh.
  - The plaster trace is off. It read the scan's overlapping atlas and painted hard island edges into the albedo.

## Files

| File | What |
|---|---|
| `devourer-d1c-portrait-three-quarter.mp4` | the 24 s three-quarter clip with sound (Feline Footwear; drum stem as the beat); the devour at 8.2-9.4 s |
| `devourer-d1c-three-quarter-rest.png`, `devourer-d1c-three-quarter-devour.png` | stills from the clip at 4 s and 9.2 s |
| `devourer-d1c-before-after.png` | D1b on the left, D1c on the right: three-quarter rest, three-quarter devour, frontal |
| `devourer-d1c-eyes-before-after.png` | the eyes from the master camera, D1b on the left and D1c on the right |
| `devourer-d1c-eyes-close.png` | eye close-up |
| `devourer-d1c-frontal-rest.png` | frontal check |
| `devourer-d1c-orbit-measurement.png` | the frontal depth map and its high-pass, with D1b's misplaced eyes circled |
| `devourer-d1c-blender-*.png` | the same views from the Blender build |

## Honest critique

- **The eyes are fixed.** They sit in the skull now, with the brow and lids over them, and the stare is still wide and
  white-ringed. The far eye in three-quarter tucks behind the brow, as it should.
- **The maw reads better, but is not yet huge.** From the master camera, the devour now shows a dark opening framed by
  teeth, with the head turned into it. It is still the width of a human mouth on a giant head. If the souls need a
  bigger gate in D2-D3, the next step is to widen the mouth past the scan's lip line, a larger sculpt change.
- **The skin** is calmer in close-up, and the seams are gone.
