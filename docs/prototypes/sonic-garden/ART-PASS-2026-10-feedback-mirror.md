# Art pass 2026-10: Feedback Mirror

The art agent, 2026-10-15, branch `art/sonic-vfx-pass`. The owner's note: "same" as Ember Forest: it feels
incomplete; find the concept, establish a stronger direction, do an art pass.

## The concept found

- `SCENE-CATALOG.md` §15: "A single glyph of light inside a video feedback loop: the music turns, zooms and recolours
  the loop, so the world is the instrument's own echo." It specifies the following:
  - one luminous glyph off centre;
  - its echoes receding into a logarithmic spiral (Crutchfield);
  - negative space;
  - deep violet ground, gold glyph, and a hue walk controlled by the feedback's own hue step.
- The instrument: sustain the decay, melody the rotation, bass the zoom, kick a zoom punch, snare a channel swap,
  chord the hue step.
- `ART-RESTART-ANALYSIS.md` §9: "A sigil inside video feedback... Nothing. It is a 2D graphic... A purple void...
  A VJ loop... Exactly the brief's 'effect as environment'."

The before stills show why it felt incomplete. The loop had nothing to recur except a small sigil, so its echoes were
a short comet tail. Nine tenths of the frame was empty violet.

## Research

- **Video feedback** is a camera pointed at the monitor that shows it. James Crutchfield, "Space-time dynamics in
  video feedback" (Physica D, 1984), studied it as a dynamical system: zoom, rotation and the monitor's response give
  spirals, nested frames and self-sustaining patterns.
- **The iconic image** is the "howlround": the BBC's Doctor Who title sequences of 1963–73 (Bernard Lodge), made by
  feeding a camera its own monitor. It is a corridor of nested screens twisting away to a vanishing point.
- **The art lineage** runs through Nam June Paik's and Steina and Woody Vasulka's camera-monitor loops. Its modern
  descendants are MilkDrop's zoom-and-turn tunnels.
- **The defining visual fact:** the frame itself recurs. The screen's edge is what makes a corridor out of the loop.
  A loop that never sees its own frame only smears.

## The art direction

**"Howlround."** We are the camera looking at our own monitor.
- **The frame** is the scene's second subject: a glowing monitor bezel, with gold viewfinder brackets, just inside the
  frame's edge.
- **The loop** (ADR-1066's feedback FIR, 24 taps) carries the frame inward as a corridor of nested screens. Each is a
  step smaller, turned, and a step further round the hue circle: gold, then rose, violet and cyan toward the
  vanishing point.
- **The glyph** (ring, triangle, beads, core) burns on the right third. Its recent past spirals down the corridor, so a
  melody leaves its contour in the echoes.
- **The glass** is CRT glass: a blue-black ground, scanlines, bloom, grain.

## What changed

All of it in `tools/sonic_vfx/scenes/feedback_mirror.py`.

- **New: the bezel.** Four bars and eight bracket arms, unlit procedural boxes (rasterised, nearly free) at 7–10
  units of emission, so the decaying echoes stay bright.
- **The loop.**
  - Zoom 0.972 → 0.93, so the corridor reaches the vanishing point in 24 taps.
  - Decay 0.84 → 0.9 and amount 1.0 → 1.6, so the corridor is solid, not ghostly.
  - Rotate 3.0 → 1.6, a gentle twist that the music adds to.
  - Hue step −0.004 → −0.026, so the colour walk shows.
  - The zoom breathes 0.93–0.915 over 24 s on a seamless loop track.
- **The glyph** is a little larger (ring 0.82 → 0.95 m), with thicker strokes for the echoes.
- **The glass:** scanlines (`post/display/scanlines` 0.22), and a blue-black ground instead of violet.
- **Audio-only performances** now move the corridor too. The interval and chord drivers need MIDI, so with only audio
  (Trench) the old loop barely moved:
  - the bass now also twists the corridor;
  - `sonic.brightness` also walks its hue.

## The mappings (`sonicScene.vocabulary`)

| signal | what the world does |
|---|---|
| `notes.lastPitch` | the triangle turns to the pitch (a spring), so a melody draws its contour into the echoes |
| `notes.interval` | a rising step twists the corridor one way, a falling one the other |
| `response.sustain` | the echoes last longer: the corridor reaches further |
| `response.bass` | the corridor deepens and twists: each screen falls further in, turned further |
| `response.kick` | a zoom punch: the corridor jumps inward; the bezel and the core flash |
| `response.snare` | an RGB split across the frame |
| `response.hat` | grain, and a flicker in the scanlines |
| `notes.polyphony` (+ tension) | the hue step: a chord colours the corridor faster round the circle |
| `sonic.brightness` | a bright timbre also walks the colour further round |
| `notes.lastVelocity` | the glyph's brightness on each note |
| silence | the corridor breathes slowly and the inner ring turns: a quiet gold tunnel |

## Frame time

GPU at 1080p, realtime tier, Trench from 60 s, 600 frames, under `tools/gpu-lock.sh`.

| | p50 | p95 | costliest passes |
|---|---|---|---|
| before | 6.9 ms | 10.0 ms | sdf 3.1, feedback 2.2 |
| after | 5.6 ms | 9.9 ms | sdf 2.7, feedback 1.3 |

The scene is well inside the 60 fps budget, as before. The bezel is rasterised unlit boxes and costs nothing
measurable; a full-frame SDF would have marched every pixel. Run to run, the two are within noise of each other.

## Media

`~/Desktop/av-gen-review/49-sonic-vfx-art/feedback-mirror/`:
- `before/`: stills with Trench at 12, 62 and 112 s;
- `after/`: stills with Trench at 12, 62 and 112 s, and with the full test mix and MIDI at 4, 9 and 14 s;
- `feedback-mirror-trench-50-80s.mp4` and `feedback-mirror-full-midi-0-18s.mp4` (1080p30, with audio).

## Open

- The loop is an exact FIR over the last 24 clean frames, not an infinite recursion. The corridor therefore ends
  about 24 screens in, at about a fifth of the frame. That is deep enough to read as infinite with the vanishing point
  this small.

## Suites (end of the four-scene pass, on 2fb8cfa5)

- `tools/ci/run-suite.sh cpu-assets` (under `tools/gpu-lock.sh`): **PASS**, exit 0. 4227 passed, 0 failed, 19
  skipped, 1 expected failure; 4247 of 4247 ran, in 19 min 25 s.
- `tools/ci/run-suite.sh gpu --bin build/release/tests` (the engine and shader change of ADR-1350): **PASS**, exit 0.
  655 passed, 0 failed, 1 skipped; 656 of 656 ran, in 30 min 54 s.
- `python3 tools/check_project_integrity.py`: exit 0.
