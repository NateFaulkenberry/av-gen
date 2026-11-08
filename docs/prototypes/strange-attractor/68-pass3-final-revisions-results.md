# Pass 3: the final revisions, results (67-)

Built on 163356db (67- as written). Media: `~/Desktop/av-gen-review/101-strange-attractor/pass3-env/15-final-revisions/`.
The Mars sand (67- §4) is the hero agent's (`hero-06-mars-sand.md`), merged from scene/sa-mars-sand 02319d1f. Leg and crossing
times are unchanged.

## §1 Colour: why the intro lost contrast, and the grade

- **The cause:** M3g §2 (9cee2700) moved the whole film from ACES to AgX, so that one tone map ran across every portal
  with no step. AgX's softer toe and shoulder took out about a quarter of the canyon's luma spread and some of its
  saturation. Luma std at 8 s: 0.18 in the M3d, M3e and M3f full renders, 0.13 in 09-, 0.11 in 14-.
- **The grade (AgX stays):** a per-world grade in the timeline (`GRADE` in `build_journey.py`): contrast (log, about
  mid grey, before the tone map), saturation, local contrast, temperature, vignette, bloom and halation. Each value is
  eased over 2 s across each crossing, as the exposure track is, so nothing steps at a portal.

| World | Contrast | Saturation | Bloom | Halation | Vignette | Local contrast | Temperature |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Canyon day | 1.38 (+0.5 EV) | 1.22 | 0.07 | 0 | 0.12 | 0.30 | 0 |
| Moon (crisp, cold) | 1.15 | 1.08 | 0.015 | 0 | 0.18 | 0 | -0.06 |
| Dusk (fire, glyphs) | 1.14 | 1.22 | 0.08 | 0.35 | 0.20 | 0.12 | 0 |
| Mars (warm veil) | 1.12 | 1.18 | 0.09 | 0 | 0.12 | 0.10 | 0.04 |
| Night (fire, craft, glyphs) | 1.14 | 1.18 | 0.08 | 0.40 | 0.22 | 0.12 | 0 |
| Ocean (unchanged) | 1.0 | 1.0 | 0.04 | 0 | 0 | 0 | 0 |
| Dawn | 1.18 | 1.20 | 0.07 | 0.15 | 0.12 | 0.15 | 0 |

**After the coordinator's review (2026-11-08):** the intro's luma std at 8 s, measured on 1280x720 stills:

| State | Luma std at 8 s | Mean | Near-black |
| --- | --- | --- | --- |
| M3f (ACES) | 0.178 | 0.230 | 0 |
| 14- (AgX, no grade) | 0.111 | 0.246 | 0 |
| First grade (contrast 1.22, local 0.18) | 0.120 | 0.197 | 0 |
| Contrast 1.5, local 0.4 | 0.136 | 0.164 | 2.6% (crushed) |
| Contrast 1.38, local 0.3, +0.25 EV | 0.137 | 0.200 | 0.1% |
| **Kept: contrast 1.38, local 0.3, +0.5 EV** | **0.144** | **0.225** | **0** |

The kept grade brings the intro's brightness back to M3f's and its spread to 0.144. The rest of the gap to 0.178 is
AgX's shoulder: more contrast from the grade only crushes the shadows. The extra exposure eases out across the Moon
crossing.

**The Moon's domes:** the magenta halo was the look's local contrast (its low-pass), not the bloom: at local contrast 0
the domes are crisp, and at bloom 0.015 with local contrast 0.08 the halo was still there. The Moon now takes no local
contrast, bloom 0.015 and contrast 1.15.

## §2 Post

- Bloom goes from 0.04 to 0.07-0.09 everywhere except the ocean, which keeps 0.04. Halation (warm, threshold 1.5)
  glows on the fire, the craft and the glyphs at dusk and night, a little at dawn.
- Per-world ambience comes from the vignette, the temperature and the local contrast in the table above.
- No grain: it would read as noise against the canyon's flat colour fields.
- `grade-and-bloom-before-after.png` separates the two at 1, 42, 58 and 105 s: no grade (as 14-), grade with the old
  bloom, and grade plus bloom and halation.

## §3 Dusk

- One straight line from the far bank's face, down the river from the camp (-8.5, 12, 1416.1), across the water and
  past the gathering 5 m to its right, into the Mars portal on the bench face behind the camp (91.55, 10, 1343.21). The
  constants are `canyon_scale.DUSK_*`. The old exit on the outer cliff, and the climb to it, are gone.
- No look to the right: the heading is constant out of the arrival (yaw rate p95 0.2 deg/s). The camp sits a little
  left of and below the frame's centre from 48 s until the camera passes it at about 65 s.
- The arrival's 24 m/s eases to a 3 m/s cruise over about 4 s (it was a 2 s ramp). Speed max/min 7.5 is that ease.
- The opening's field is cut to 30 m round the portal, so the glyphs stay off the gathering; the tear starts 55 m out.
  From about 64 s the opening fills the frame and Mars is seen through it until the crossing at 69.66 s.
- Lowest 6.2 m over the terrain; the nearest mesh is sasquatch-a at 6.3 m.

## §5 Night

- **The entrance is off the rock:** out over the water on the river's far half (38, 3.5, 1212). The rock's volume is
  not in the arrival.
- **The river portal** is at the river's centre (28, 1418), beyond the camp.
- **The line:** one gentle curve, bowed toward the camp's bank, then a Moon-style rise to 16 m and a dive into the
  opening, about 46 degrees at the opening. The rise is looked at with `climbLook` 0.4, so it does not stare up at the
  sky. The ocean arrival is mapped from the dive's heading and slope.
- **The camp** is on the left, 20.1 m off at the closest, and in frame from 93.6 to 106.0 s (12.4 s; it was 15 s in
  round 2). The bow was eased later after review (control (76, 1372)), so at 99 s the line is about 2 m further from
  the left bank's rock than the first version (13.0 s in frame, 17.5 m off).
- **The opening starts sooner:** the field from 110 m and the tear from 55 m, so it opens in view during the rise.
- **The far bank's black rock (67- §7):** the rock on the right at about 95-111 s is still dark. It was not the portal
  clipping: it is the far bank's face in the moon's shadow, now 15 m or more from the line.
- Lowest 3.4 m over the water; the nearest mesh is 10.8 m (a camp shrub).

## §6 Ocean

The octopi are cut outright: the three nodes, their tracks, and the measure tool's references. Nothing else in the
ocean changed.

## §7 The cream patch at 0 s: not fixed

- It is the plateau's caprock, perfectly flat at 145.5 m, at about (-36, 1936). The terrain id AOV shows the canyon's
  own surface, not a node.
- **Albedo is not what makes it bright.** With the flats' colour forced to black in a test render, the patch stays a
  flat grey sheen. Darkening the flats 55% only turned it grey-olive. Full relief on the flats added a little texture.
  Roughness 1.0 changed nothing visible.
- So the brightness is the light it receives at a grazing view, and a material change does not remove it. A fix needs
  the plateau's geometry to change (relief on the caprock, in the generator) or the sun to move, which changes the
  canyon-day look the owner approved. Neither was done. The program is back as committed.

## Guard

Run on a full-length preview of this state (640x360, 10 fps; the 1080p final render comes after the Mars merge).

- **Luma** against the 14- full render (1f88520c): 0 flagged half-seconds (`luma-vs-14-full.txt`).
- **Crossings,** ±0.5 and ±0.1 s stills, all six: continuous (`crossings-half-second-preview.png`). Dusk to Mars
  shows Mars through the opening from about 64 s, and the crossing at 69.66 s does not step.
- **Forward vs velocity** (`forward-vs-velocity.txt`, the audit rewritten after the scratch copy was lost): every leg
  stays under 25 degrees except the Moon's approved exception, 37.30-44.33 s. Dusk max 3.8, night max 20.1.
- **Clearance** (`clearance.txt`): dusk lowest 6.2 m over the terrain, nearest mesh 6.3 m (sasquatch-a); night lowest
  3.4 m over the water, nearest mesh 10.8 m (a camp shrub).
- **Path measures** (`top-views/`, `path-measures.txt`):

| Leg | Length/A->B | Reversals | Yaw p95 | Pitch p95 | Speed max/min |
| --- | --- | --- | --- | --- | --- |
| Dusk | 1.00 | 0 | 0.2 | 1.6 | 7.5 (the 4 s arrival ease) |
| Night | 1.08 | 0 | 5.4 | 17.5 | 1.8 |

The other five legs are unchanged from 66-.

## The final render (67- §8 step 1)

`~/Desktop/av-gen-review/101-strange-attractor/pass3-env/10-full/strange-attractor-final.mp4`, rendered from 5893190b
plus the Mars merge (35bfdc8d): 1920x1080, 30 fps, supersample 2, 6316 frames, 210.53 s, AAC stereo (mean -11.1 dB,
peak 0.0 dB), 345 MB, GPU errors 0, sequence hash 2db604e8f62aa978.

- Contact sheet every 5 s: `strange-attractor-final-contact-sheet-every-5s.png`.
- Crossings at ±0.5 and ±0.1 s: `strange-attractor-final-crossings-half-second.png`. All six are continuous, including
  dusk to Mars and Mars to night with the merged sand.
- Luma against the 14- full render and against this round's guard preview: 0 flagged half-seconds in both
  (`luma-vs-14-full.txt`, `luma-vs-15-preview.txt`).
- Not fixed and left by the coordinator's call: the cream patch on the plateau at 0 s (§7).
