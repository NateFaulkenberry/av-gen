# Art pass 2026-10: Cymatic Plate

The art agent, 2026-10-15, branch `art/sonic-vfx-pass`. The owner's note: "try having the plate be 2d - top down,
rather than 3d at an angle".

## The concept found

- `SCENE-CATALOG.md` §6: "Black sand on a resonating plate: every note sings its own standing-wave figure into the
  sand, and a chord is two figures at war."
  - The plate was a square of steel "seen from 60 degrees above", turned as a diamond, with a camera "high, rotating
    slowly round the plate over 120 s".
  - The figure is a material program: Chladni's f = cos(nπu)cos(mπv) − cos(mπu)cos(nπv), with sand where |f| is
    small. The routes move the mode numbers by pitch, and a chord superposes its lowest note's figure.
- `ART-RESTART-ANALYSIS.md` §6 called it "a high oblique product shot... a pure effect": a shader on a quad.
- The before stills show the plate at 60° with its corners cropped by the frame, the figure seen in perspective, and
  a warm spot pooling to one side.

## The direction: a flat picture

Chladni figures are photographed and drawn from straight above: Chladni's own plates (1787), Hans Jenny's
*Cymatics* photographs, and every lab demonstration. Seen that way the figure is the whole picture: a graphic of
bone-white sand on black, symmetric about the driver bolt. The owner's request puts the scene where its subject has
always been seen.

- **The plate** is black steel in the frame's own 16:9 proportion. It fills the frame, with a thin margin of void
  round its bevelled edge, and the driver bolt at its centre.
- **The figure.** Its cells stay square on the wide plate: both axes take the plate's depth as their unit, so the
  figure runs on past the short side's span to the long edges instead of stretching.
- **The view** is a long lens (135 mm) from 19 m straight down, which is nearly orthographic. It is all in focus (the
  depth of field is off) and breathes 4% closer and back over 64 s, so the still picture is never quite still.
- **The light** pools on the figure's centre and falls to 0.3 at the corners. It is painted in the plate's program,
  because the plate is now unlit, for two reasons:
  - a lit plate filling the frame cost 9 ms of PBR shading for what is a flat picture (by ablation: 21 → 13 ms in
    the scene pass);
  - a real overhead key lit the black steel brown with its own mirror image.
  A warm key and a cold rim still light the polished driver bolt.

## What changed

All of it in `tools/sonic_vfx/scenes/cymatic_plate.py`.

- **The plate:**
  - 3.2 × 3.2 m turned 45° → 5.69 × 3.2 m, axis-aligned;
  - the bevel is 8 → 12 mm;
  - the stand is gone (it is invisible from above);
  - the dust motes are gone (from above they read as noise on the figure).
- **The camera:**
  - the 360° orbit track (120 s) → a top-down camera with a seamless 64 s breathing track;
  - the focal length is 42 → 135 mm;
  - depth of field is off.
- **The plate is unlit.** Its program paints the pool of light and no longer outputs a roughness. The lights light
  only the bolt.
- **The sand grain** is a per-cell hash (Quantize, ADR-1162) instead of an fBm noise.
- **The particles** cover the whole plate, and the leaping sand scatters sideways (spread 0.25 → 0.7). Seen from
  above, a leap straight up toward the lens barely moves.
- **New reactivity for the view and for audio alone.**
  - The bass also swells the sand on its lines. A plate's flex is invisible from straight above, so the sand carries
    it.
  - With audio alone (no MIDI), the timbre's slow brightness sets the mode numbers. Before, an audio-only performance
    such as Trench left the figure frozen on its lowest mode. A played pitch takes over whenever it is higher
    (`max`).

## The mappings (`sonicScene.vocabulary`)

| signal | what the world does |
|---|---|
| `notes.lastPitch` | the mode numbers: low notes draw simple figures, high notes intricate ones |
| `sonic.brightness.slow` | with audio alone, the sound's brightness sets the mode (a played note takes over when it is higher) |
| `notes.lowest` (+ chord, polyphony) | a second figure from the lowest note is superposed: the figures fight |
| `notes.held` (+ sustain) | a held note sharpens the figure; silence scatters the sand |
| `response.noteEnv` | the sand flashes and leaps with the note's strength |
| `response.kick` | the whole bed leaps, scatters and settles back onto its lines |
| `response.snare` | a burst of sand from the nodes |
| `response.hat` | granular glitter |
| `response.bass` | the plate flexes and the sand swells on its lines |
| `notes.tension` | the figure jitters |

## Frame time

GPU at 1080p, realtime tier, Trench from 60 s, 600 frames, under `tools/gpu-lock.sh`.

| | p50 | p95 | scene pass |
|---|---|---|---|
| before (60° view, plate on about 60% of the frame, depth of field) | 14.9 ms | 15.9 ms | 13.5 |
| the top-down plate, lit (first attempt) | 22.3 ms | 22.9 ms | 21.2 |
| after (the top-down plate, unlit, the light painted) | 14.4 ms | 14.6 ms | 13.3 |

The scene stays inside the 60 fps budget, where it was before.

## Media

`~/Desktop/av-gen-review/49-sonic-vfx-art/cymatic-plate/`:
- `before/`: stills with Trench at 12, 62 and 112 s;
- `after/`: stills with Trench at 12, 40, 62 and 112 s, and with the full test mix and MIDI at 4, 9 and 14 s;
- `cymatic-plate-trench-50-80s.mp4` and `cymatic-plate-full-midi-0-18s.mp4` (1080p30, with audio).

## Suites (end of the four-scene pass, on 2fb8cfa5)

- `tools/ci/run-suite.sh cpu-assets` (under `tools/gpu-lock.sh`): **PASS**, exit 0. 4227 passed, 0 failed, 19
  skipped, 1 expected failure; 4247 of 4247 ran, in 19 min 25 s.
- `tools/ci/run-suite.sh gpu --bin build/release/tests` (the engine and shader change of ADR-1350): **PASS**, exit 0.
  655 passed, 0 failed, 1 skipped; 656 of 656 ran, in 30 min 54 s.
- `python3 tools/check_project_integrity.py`: exit 0.
