# Art Pass 2, M2: Mars

M2 of `33-pass2-plan.md` §6, on the 210.5 s cut. It covers:
- the canyon's second portal, in the sheer outer cliff behind the dusk camp;
- Mars, Cydonia: mesas, haze, three pyramids;
- the pyramid portal;
- the return to the canyon at night (the leg only; M3 builds the gathering);
- the coordinator's four campfire notes.

The sequence so far runs 0-118 s. Media is in `~/Desktop/av-gen-review/101-strange-attractor/pass2-m2/`; the numbers
are in `06-measures.txt`. ADR-1895 (journey legs) and ADR-1896 (Mars terms) are new.

## Timing (bar 17 at 32.0 s, 1.9835 s a bar)

| Time | What |
|---|---|
| 0-47.87 s | M1b: the canyon by day, portal 1, the short Moon, crossing 2 into the dusk canyon |
| 47.87-59.5 s | the campfire encounter |
| 59.5-69.66 s | up from the bank and across to the outer cliff; the field gathers, the seam, the tear onto Mars |
| 69.66 s (bar 36) | crossing 3: out of a mesa's wall onto the Martian plain |
| 70-97 s | across the plain: mesas, yardangs, haze; the pyramids small, then growing against the far wall |
| 97-101.4 s | to the middle pyramid's south face: the stone fractures into glyphs, the tear onto the canyon at night |
| 101.4 s (bar 52) | crossing 4: out of the canyon's outer cliff at night |
| 101.4-118 s | down toward the camp, the embers first; the Sasquatches still at the fire |

## The engine: journey legs (ADR-1895)

The canyon's scene now has two legs: `portal` (to the Moon) and `portals` (to Mars). Each later leg names its own site
in the canyon (`entry`, `entryAxis`). One leg is active, decided by time:
- the active leg's state lives in the portal members, so the single-portal code reads it unchanged;
- each world asks its own leg for its camera, so the Moon keeps following the first leg after the second has begun;
- the second leg's crossing is searched from the first leg's return home;
- the corruption opens at the active leg's site and measures its field from the journey's camera;
- looks count the crossings across legs: `dusk` from 1, `dusk-portal` from 2 (the cliff portal's corruption sizes),
  `night` from 3.

## The outer cliff portal

The site was found on the generated terrain: the outer canyon wall behind the camp is a near-vertical face from 80 m to
122 m between lat 91 and 93 (real metres, the canyon frame). The portal is at 95 m, 76 m across the benches from the
fire.
- **The climb.** The camera rises almost straight up from the bank over the Sasquatches, then crosses the benches at
  the portal's height, the eye on the cliff from 59.4 s.
- **The opening.** The glyph field gathers on the face from about 95 m away. The seam opens at 55 m and the tear shows
  Mars through the rock.
- **The crossing** is at 69.655 s (bar 36 is 69.69).
- **Two engine faults the measure caught**, both from a later leg reading the root's camera before it is placed on the
  return path (ADR-1895):
  - the field never gathered;
  - the view through the tear came from the wrong place and popped at the crossing (a frame difference of 16.8).

  It is now continuous.

## Mars (ADR-1896; `build_mars.py`)

- **The ground.** The lunar generator with its Mars terms:
  - mesas (flat tops, terraced walls, talus aprons), with knobs around the plain and a long far wall behind the
    pyramids;
  - yardangs along the wind;
  - a few old craters;
  - pads that ease the ground level under each pyramid.
- **The look.**
  - A dusty rust-and-ochre regolith program.
  - A butterscotch sky with a darker zenith.
  - Height-fog haze over the distance.
  - An afternoon sun from behind and to the right.
- **The pyramids** (`pass2/build_pyramids.py`, original geometry) stand in the Giza arrangement: the offset diagonal,
  300, 280 and 140 m bases.
  - Each is a height field carrying the monument, its sand ramps and its ground.
  - The casing is eroded and missing in patches, showing stepped courses beneath.
  - The corners are broken back, and the two older ones carry slumped collapse scars.
  - Sand is banked against the bases, deepest to windward.
  - Each is in its own state: the great one half-cased, the middle one mostly cased, the small one stripped to its
    core.
- **The pyramid portal.** It is in the middle pyramid's south face, 32 m up, where the casing has fallen. The stone
  breaks into the glyph field, and the tear opens onto the canyon at night.
- **A fix the pyramids needed.** My first export was inside out (a normal recalculation on an open surface flipped
  every face down). The corruption's back-face shading then turned the whole pyramid dark.

## Night (the leg only)

The camera comes out of the outer cliff further down (66-116 m at lat 100) and descends toward the camp: the embers are
seen first. The `night` look:
- a near-black blue sky and a cold, faint moon (the key at 2.5 %);
- the contour lines off;
- the fire down to embers and smoke (no flames), with a low red light;
- the Sasquatches still there.

The gathering with the Gray is M3.

## The coordinator's campfire notes

1. **The stones** are dark weathered rock in three tones (albedo 3-7 %). The firelight reads on their inner faces.
2. **The flame** has readable tongues.
   - Fewer, smaller particles in a deep orange-to-yellow ramp, stretched along their motion.
   - Emission is about half what it was, so the core no longer blooms to white.
   - The light's breathing stays slow (ADR-1888 revised at M1b).
3. **The third Sasquatch** no longer stands behind the fire: the three stand either side of it and to the right, and
   all of them read through the shot.
4. **The orange streaks** on the dusk wall were the contour lines at a quarter. The dusk and night looks now set the
   contour gain to 0 (ADR-1894).

## Known weaknesses

- The Martian plain is wide and even between the mesas; the pyramids carry the composition.
- The climb from the fire spends about two seconds on dark ground before the cliff fills the frame.
- The night is a first pass (M3 builds the gathering, the embers and the moonlight properly).
- The pyramids' casing patches show grid steps up close.
