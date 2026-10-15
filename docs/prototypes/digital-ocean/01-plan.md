# Digital Ocean: Stage 0 art direction and technical plan

*Written by the art agent before any implementation, grounded in the repository as of `e3dc8e9e`. The brief is
`00-brief.md`; progress per stage is in `PROGRESS.md`.*

## 1. What Digital Alpine actually is (the reference, inspected)

- **Generator:** `tools/sonic_vfx/scenes/digital_alpine.py`, built by `python3 tools/sonic_vfx/abstract.py build
  digital_alpine` into `examples/sonic-abstract/digital-alpine.json` (project: `sonic.live`, routes, timeline,
  `sonicScene` design record, `control.midi`) and `.scene.json` (nodes, programs, environment). Regenerating it into a
  scratch folder reproduces the committed files byte for byte, so the pipeline is deterministic.
- **Look:** everything is **unlit**. The colour is painted by interpreted **material programs** (`materialPrograms`,
  ADR-030; up to 48 ops, 8 vec4 registers, 16 programs per scene). A sky dome (a sphere with a short program), a "lake
  sky" disc far below the waterline, faceted GLB ranges written by `tools/sonic_vfx/meshes.py`, and mirrored twin meshes
  for reflections. The default key light is re-authored without its shadow (`kit.Scene.unshadowed_key`).
- **Motion:** a 170 s looped camera glide (timeline tracks with `loopLength`), a world-space `wave` field deformer on
  the land (the "audio topography"), drifting noise in the water program.
- **Audio/MIDI:** routes `R(source, target, amount, chain...)` from `response.*`, `audio.*`, `notes.*` to material op
  constants (`material/<program>/op/<n>/constant/constant`), deformer amounts, particle bursts; the mod wheel is bound
  to `control.modwheel` through `control.midi.bindings`.
- **Cost lessons recorded by its author (PROGRESS-abstract.md):** a material program costs a fixed ~3 ms plus ~0.09 ms
  per op at the live floor over the pixels it covers; a casting default key costs 1-3 ms for nothing; op constants are
  clamped to +-1000 (work in decametres); a dense grid seen at grazing angles produces sub-pixel slivers.

## 2. Engine facilities found that this scene can use as they are (no engine change expected)

| Need | Facility | Notes |
|---|---|---|
| Ocean surface | procedural `mesh` source (a generated GLB) + **world-space `sine` deformers** (`p += dispAxis * a * sin(dot(p - c, axis) * f + phase + speed t)`) | up to 8 per node. Two sines with the same axis, one displacing up and one displacing ALONG the travel direction a quarter-cycle apart, make a **Gerstner (trochoidal) wave**: sharp crests, broad troughs. The vertex stage rebuilds normals by finite differences of the whole chain, so shading follows the waves. |
| Music written into the swell | **`spectrum` and `onset` fields** (ADR-1116) through a **`field` deformer** | a field's value at distance d is the spectrogram (or the last 8 onsets as travelling fronts) delayed by d / audioSpeed. Works on live input (the live AudioHistory ring). A `compound` field can multiply it by a sine `wave` field: a swell whose height is what the music did d/speed seconds ago. |
| A MIDI-fired hero wave | `wave` field with `waveShape: pulse` and a `trigger` (ADR-906; `signal` triggers record live bus events) | a front leaves `waveOrigin` at each pad hit and travels at `waveSpeed`; stateless, so repeated hits cannot accumulate. |
| Dolphins | **`path` deformer** along a scene `spline` (ADR-026), `deform/k/pathOffset` routable (+-1e4) | the body bends along the leap arc. Ambient leaps: a closed loop spline driven by an LFO **saw** (pure in time, seamless at the wrap). MIDI leaps: an open spline driven by an **envelope source** (linear attack), whose fast release teleports the dolphin back to its start under water, unseen. |
| Water colour, foam, glitter | material program inputs `normal`, `viewDirection`, `worldPosition`, `curvature`, `footprint`, `cameraDistance`, `field`; ops `gradient`, `smoothstep`, `remap`, `mixBy`, `fresnel`, `noise` ... | the sun glint is `r.l = 2(n.v)(n.l) - v.l`, built from `fresnel` (n.v), `gradient` (n.l, v.l). `footprint`-scaled edges keep banded colour from aliasing. |
| Sky | a dome with a short unlit program (Alpine's pattern) + the engine's procedural sky behind it | the sun disc, halo and horizon band as program terms. |
| Clouds | procedural geometry (flattened lathes / spheres) with an unlit banded program; a world-space `twist` deformer about the camera's vertical axis spins them slowly round the viewer: seamless, transport-independent drift | alternative considered: ADR-1289 sculptural clouds (a fullscreen march), kept in reserve. |
| Spray, splashes, glints | particle systems (`burst`, `spawnRate`, `collision: splash` rings) | bounded pools by construction (fixed capacity). |
| MIDI | `control.midi.bindings`: `cc` -> `signal` (then routes with smoothing), `noteEvent` -> event signal (envelopes, field triggers) | as Digital Mosh Live / THE RIFT LIVE. |
| Response sliders | `response.*` signals (kick/snare/hat/onset/low/bass/level/sustain/flux/intensity and envelopes) | the Live panel's Response sliders shape them. |

## 3. The concept

**Digital Ocean: "The Tidewriter."** A stylized, graphic ocean at the low sun, seen from a composed, nearly still
camera. Broad Gerstner swells roll in from under the sun toward the viewer; the music is *written into the sea*: what
the bass played seconds ago arrives at your feet as swell, every kick sends a front of light and lift rolling in from the
horizon, highs scatter glitter on the sun's path. Dolphins leap across the sun's reflection. A pad raises a hero wave
that rolls in and breaks into foam.

Why this and not a visualizer on water: the reactivity *is* the ocean's own medium (swell, light on water, spray), it
travels and is delayed by distance (nothing pulses in sync), and the sea keeps rolling in silence.

## 4. Palettes (shortlist; previews are rendered and compared at Stage 3 before one is chosen)

1. **Graphic golden hour (poster):** deep cobalt and ultramarine swell bodies, teal-to-seafoam lit faces, a coral and
   apricot horizon, a cream sun, cream foam. Broad colour regions.
2. **Electric twilight:** indigo and midnight water, cyan and turquoise crest light, violet and lavender clouds, a peach
   or coral sun, mint foam.
3. **Bioluminescent midnight:** near-black navy water, cyan-emerald luminous crests and wakes, a pale moon, violet haze.
4. **Surreal tropical:** saturated teal, ultramarine shadows, aqua surf, tangerine and pink sun.

Intended performance arc (to be validated): one primary palette plus one or two related ones on a single `palette`
control, read as time of day (e.g. golden hour -> twilight -> bioluminescent night), so a palette change is a sunset,
not arbitrary colour cycling.

## 5. Camera and composition (Stage 1 explores three)

- A: low and cinematic (eye 3-5 m over the water, 35 mm), the horizon high, foreground swells big.
- B: elevated (25-40 m, 28 mm), the wave pattern revealed.
- C: graphic: a strong horizon on the upper third, the low sun on a vertical third, its reflection a column down the
  frame, a long lens (50-70 mm) compressing the swell rows into bands.
- Default: a stable composed view; optional slow, subtle drift (a seamless looped track) — the brief prefers it.

## 6. Core techniques

- **Ocean mesh:** a generated GLB, a fan/strip grid in camera-centred polar coordinates covering only the view wedge,
  rings spaced geometrically (dense near, sparse far), so the vertex budget sits where it is visible and far water does
  not produce sub-pixel slivers. Far water beyond its edge is painted by the dome's below-horizon band.
- **Waves:** 3-4 Gerstner trains (pairs of sine deformers) of different wavelengths, directions and speeds; a noise
  deformer for small chop; the audio swell as a field deformer.
- **Shading:** one unlit ocean program: depth colour by facing/height, lit faces toward the sun, crest highlight from
  height + convexity, the sun glint, horizon haze by distance.
- **Sky:** dome program; graphic sun; layered clouds; distant island silhouettes.
- **Life:** dolphins (generated low-poly mesh) on path deformers; spray particles; splash rings.

## 7. Performance risks

- The ocean program covers about two thirds of the frame: the ~3 ms fixed program cost is paid there; keep it to the
  ops it needs (target <= 40).
- Vertex cost: 8 sine deformers + a compound audio field, evaluated three times per vertex (finite-difference
  normals). Keep the grid to tens of thousands of vertices.
- Transparent particles over the sun (overdraw) and bloom.
- The sky dome's program covers the rest of the frame; keep it short.
- Measured, not assumed: `tools/live_scene_profile.py` (ADR-1090) at 1080p under `tools/gpu-lock.sh`.

## 8. Checkpoints

| Stage | Deliverable | Acceptance |
|---|---|---|
| 1 | three graybox compositions, one chosen | reads as ocean + sky + sun with flat colour |
| 2 | Gerstner swell set, direction and rhythm | attractive moving water in flat shading (clip) |
| 3 | ocean program + palette previews (4), one primary | depth, contrast, sun relationship |
| 4 | sky dome, sun, clouds, islands | one coherent environment |
| 5 | foam crests, spray, glitter | detail follows the waves, no noise |
| 6 | dolphins (ambient), MIDI-fired leap, hero wave | one memorable moment |
| 7 | audio reactivity: tidewriting swell, onset fronts, highs glitter, sustain glow | different music -> different sea |
| 8 | MIDI map (CCs + pads) | performer can shape and trigger |
| 9 | animation polish, timing, variation | alive, not mechanical |
| 10 | critique rounds (contact sheets under 7 conditions), 3 fixes per round | documented improvement |
| 11 | profiling at 1080p (+ higher), degradation, suites, review_examples | measured, suites exit 0 |

Review media: `~/Desktop/av-gen-review/50-digital-ocean/`.
