# Liminal Euclidean World: engineering progress (PROGRESS-eng)

*The engineering agent's running notes, kept current so a cold successor can resume. The art agent keeps
PROGRESS-art.md. Governing documents: `00-brief.md`, `01-addendum-emotion.md` (wins where they differ).
Design and research: `ENGINEERING.md`. Decisions: ADR-1040 to 1044.*

## ART PASS 2 (2026-10-01): Resume here

*The governing brief is now `02-art-pass-2.md` (the owner's; it wins). My job is its section 19 Phase 4: the
smallest reusable systems the art needs. The art agent works in the same worktree in parallel; I commit only my
own paths with `git commit -- <paths>`. ADRs 1045-1059.*

| # | system | status | ADR | tests |
|---|---|---|---|---|
| 1 | beat grid: owner-numbered bars, tempo map, pulses, authored event envelopes (BIG CLAPs, words) | **done** | 1045 | `[beatgrid]` (7 cases) |
| 2 | spatial lyric typography | next | | |
| 3 | luminous line-drawn edges | | | |
| 4 | camera breathing | **done** | 1048 | `[breath]` (2 cases) |
| 5 | object animation on beat sources | | | |
| 6 | simulation-corruption effect | | | |
| 7 | spectrum-sweep transition | | | |
| - | tint on moving figures (pass 1 gap 2) | if cheap | | |

**Resume here:** build the next undone row. Each system: code, a test, an ADR, the guide entry below, then
`git commit -- <paths>`.

### The owner's numbering (AUTHORITATIVE; the analysis numbers the count-in as bar 1)

The owner's bar N = SONG-ANALYSIS bar N+1. The owner's bar 1 beat 1 is at **2.20183486 s** (= 4 x 60/109). 109 BPM
to owner bar 74; **111 BPM from the downbeat of owner bar 75 (165.1376 s)**. Section starts in the owner's global
bars (derived from the brief's bar counts and checked against the analysis's sections):

| section | owner bars | name for `sections` |
|---|---|---|
| Intro | 1-16 | `intro: 1` |
| First release ("all you got") | 17-24 | `release: 17` |
| Verse 1 | 25-40 | `verse1: 25` |
| The pause bar | 41 | `pause: 41` |
| LET IT GO (8 bars) | 42-49 | `letgo: 42` |
| Verse 2 | 50-65 | `verse2: 50` |
| Bridge transition (FEEL IT GROW starts) | 66 | `bridgein: 66` |
| Bridge 1 | 67-74 | `bridge1: 67` |
| Bridge 2 ("is that all you", 111 BPM) | 75-82 | `bridge2: 75` |
| Bridge 3 (dance) | 83-90 | `bridge3: 83` |
| Final chorus | 91-114 | `chorus: 91` |
| Ending | 115 | `ending: 115` |

### Art agent's guide, pass 2

#### 1. The beat grid (ADR-1045): pulses and BIG CLAPs as data

Add one source to the project's `sources`:

```json
{"kind": "beatgrid", "name": "song", "settings": {
  "origin": 2.20183486, "beatsPerBar": 4,
  "tempo": [{"bar": 1, "bpm": 109}, {"bar": 75, "bpm": 111}],
  "sections": {"intro": 1, "release": 17, "verse1": 25, "pause": 41, "letgo": 42, "verse2": 50,
               "bridgein": 66, "bridge1": 67, "bridge2": 75, "bridge3": 83, "chorus": 91, "ending": 115},
  "events": [
    {"at": "release:4:4", "channel": "clap1", "release": 2},
    {"at": "release:8:4", "channel": "clap2", "attack": 0, "hold": 1, "release": 0.25, "curve": "linear"},
    {"at": "letgo:1:1", "channel": "let", "release": 0.5, "repeat": {"every": 4, "count": 8}},
    {"at": "bridge2:0:4.5", "channel": "is", "attack": 0.25, "hold": 0.25, "release": 0.5},
    {"at": "intro:5:1", "until": "release:1:1", "channel": "eighthGate", "attack": 1, "release": 1}
  ]}}
```

Signals it publishes (all 0..1, pure in time, so seek equals play):

- `grid.song.quarter` / `.eighth` / `.sixteenth` / `.half` / `.bar`: a pulse, 1 on the division and 0 just
  before the next. Its sharpness is `sources/song/pulseDecay` (fraction of the division; 0.3 default; small =
  a click, 1 = a long swell). Keyable and routable, so verse 2 can sharpen it.
- `grid.song.quarter.wave` (and every division's `.wave`): a raised cosine, 1 on the beat, 0 half way. Smooth:
  use it for camera breathing, bobbing and swells. `.phase` is the 0..1 saw (for spins that complete a turn per
  bar: route `grid.song.bar.phase` to a rotation with amount 360).
- `grid.song.<channel>`: each event channel. **A different treatment per clap = a different channel per clap**
  (`clap1` drives `palette/saturation`, `clap2` drives `camera/exposure/compensation` down for a cut to black,
  `clap3` drives an SDF node's scale...). A channel can carry many events (a word that repeats).

Event fields: `at` ("section:bar:beat" or "bar:beat"; beat 1-based and fractional; bar 0 = the bar before the
section) or `time` (seconds); `channel`; `strength` (default 1; may exceed 1); `attack` (rise *into* the
instant, so the peak lands on the beat); `hold`; `until` (a position; replaces hold); `release`; `curve` (exp
default, linear, smooth); `units` ("beats" default, or "seconds"); `repeat: {"every": beats, "count": n}`.

Wiring, with existing routes (ADR-011/900/1041):

```json
{"source": "grid.song.clap1", "target": "palette/saturation", "amount": 1.5},
{"source": "grid.song.clap2", "target": "camera/exposure/compensation", "amount": -8},
{"source": "grid.song.quarter", "target": "sdf/world/look/edge/intensity", "amount": 3,
 "depthSource": "grid.song.eighthGate"}
```

- Gate a pulse to a span of the song with an event that has `until`, used as the route's `depthSource`.
- Do not put a depth on an integrating route (ADR-1041).
- The pulses are exact on the grid; `audio.*` are the music's measured energy. Use the grid for the
  beat-locked things the brief asks for, and audio where you want the music's actual dynamics.

#### 4. Camera breathing (ADR-1048)

Parameters `camera/breath/amount` (multiplies everything; default 1), `forward` (m, + towards the subject),
`lift` (m), `side` (m), `yaw` (deg, + left), `pitch` (deg, + up), `fov` (deg added). Applied after the journey
(or any camera), roll-free, without touching the journey's distance (so swaps do not move). Drive them from the
smooth waves and key `amount` per section:

```json
{"source": "grid.song.quarter.wave", "target": "camera/breath/forward", "amount": 0.08},
{"source": "grid.song.quarter.wave", "target": "camera/breath/fov", "amount": -1.5},
{"source": "grid.song.half.wave", "target": "camera/breath/yaw", "amount": 0.6, "polarity": "bipolar"}
```

and a timeline track on `camera/breath/amount` (0 in bridge 3 and the chorus, where the brief says no camera
pulse; 1 in the intro; 0.5 in the verses). For a breath that eases rather than ticks, add
`"chain": {"springHz": 2, "springDamping": 0.7}`. Keep `forward` small near walls: the collision guard does not
see the breath.


## Where things stood after pass 1 (2026-09-30, about 20:00)

- Branch `proto/liminal-space`, worktree `/Users/natefaulkenberry/Documents/GitHub/av-gen-liminal`. Never push or
  merge. Shared worktree: commit only engineering paths with `git commit -- <paths>`; the art agent owns
  ART-RESEARCH, SONG-ANALYSIS, DIRECTOR-PLAN, `tools/liminal/`, PROGRESS-art.md.
- Build: `cmake --preset release -DCPM_SOURCE_CACHE=/Users/natefaulkenberry/Documents/GitHub/av-gen/.cache/cpm`
  (first time only), then `cmake --build --preset release -j 10`.

| step | what | status |
|---|---|---|
| 0 | ENGINEERING.md (research + feasibility) | done |
| a | vocabulary: `stairs`, `screw`, `warp`, `shell` nodes + `tools/liminal_sdf.py` (ADR-1040) | done |
| b | `spring` + `integrate` chain stages, seek-exact (ADR-1041) | done |
| c | journey camera, wrap, guard, nodes on the journey, **chapters**, look-at blend (ADR-1042) | done |
| d | project `palette` (ADR-1043) | done |
| e | surfaces (a material id per node) and node `tint` (ADR-1044); the walking figure | done |
| f | `examples/liminal/` (48 s) via `tools/make_liminal_example.py`; capture in `~/Desktop/av-gen-review/24-liminal-space/eng/` | done (`liminal-example.mp4`, 48 s, 1280x720, rendered at 18.9 fps) |
| 3 | analyzer: `tools/liminal_critic.py` (Critic inputs for an SDF project + the section 16 temporal checks) | done (see below) |

## Suites at hand-back (fd66212d code)

- `avgen_tests`: exit 0; 3,945 cases: 3,925 passed, 19 skipped, 1 failed as expected (the slope lean).
- `avgen_render_tests`: exit 0; 561 cases: 560 passed, 1 skipped.

## Test commands

```sh
cd /Users/natefaulkenberry/Documents/GitHub/av-gen-liminal
tools/gpu-lock.sh ./build/release/tests/avgen_tests "[liminal]"            # the POC's CPU cases (23+)
tools/gpu-lock.sh ./build/release/tests/avgen_render_tests "[sdf]~[.probe]" # SDF GPU parity, compiled, surfaces
tools/gpu-lock.sh ./build/release/tests/avgen_tests                         # full CPU suite: ONE FAILED line (slope lean) expected
tools/gpu-lock.sh ./build/release/tests/avgen_render_tests                  # full GPU suite
```

`"[sdf]"` alone also selects the hidden `[.probe]` shadow-map probe, which fails by design (a known defect).

## Performance (Apple M2 Max, the example, median of 138 frames, `--headless --frames 150 --bench-json`)

| view | 1920x1080 GPU | 960x540 GPU | sdf march | volume march | steps avg/max | out of steps |
|---|---|---|---|---|---|---|
| corridor (4 s) | 60.7 ms | 19.9 ms | 38.1 / 10.4 ms | 20.9 / 8.4 ms | 37.9 / 121 | 0.0 % |
| room (16 s) | 59.4 ms | 19.9 ms | 37.2 / 10.4 ms | 20.9 / 8.5 ms | 37.0 / 192 | 0.0 % |
| stair (26 s) | 60.0 ms | 19.3 ms | 37.2 / 10.0 ms | 21.0 / 8.4 ms | 36.5 / 179 | 0.0 % |

Measured with the lamps as a second SDF object (since merged into the world as a surface, which removes one
march). Offline 1280x720 renders at 16-18 fps. The volumetric march is a third of the frame: lower
`scene/volumeMaxDistance`/steps or density if the art does not need it everywhere.

## The art agent's guide

Everything below is data: `tools/liminal_sdf.py` (the vocabulary), a generator script in the pattern of
`tools/make_liminal_example.py`, and the project/scene JSON it writes. No engine code.

### Building spaces

- Helpers (metres, +Y up; a box is given by its extents): `room(interior, wall, openings, name, floor_surface)`,
  `corridor(start, end, width, height, floor_y, open_start/end, floor_surface)`, `doorway(wall, room, along,
  width, height, sill)`, `opening(extents)` (windows, cracks), `stairway(origin, steps, run, rise, width,
  direction, thickness)`, `landing(center, sx, sz)`, `platform(..., pier)`, `slab(extents)`, `union`,
  `difference`, `translate`, `rotate`, `morph`, `shell`, `surface(node, k)`.
- Name what you will animate: `name="room"` makes `sdf/<object>/node/room/size`, and a room's centre is
  `node/<name>At/translation`. Grow a room by keying both (floor = centre.y - size.y + wall/2). Openings are
  slabs too: name one to key its width.
- **Rules that bite:**
  - Keep cell content inside its screw cell; put the seam (the plane perpendicular to T through +-T/2)
    through the middle of a corridor, never past a wall's end, and overlap the pieces across it (the example's
    `hallNext`).
  - The screw's `seam` margin must exceed `epsilon x maxDistance` (0.24 by default); below it the seams
    draw as stripes in the distance. Smaller is better for a helix (its seams cross walls at the corners).
  - Cut doorways deep (the helpers do): a CSG cut is only a distance bound near its faces.
  - Do not `morph` between two rooms whose walls are far apart (moire); key the room's box instead.
  - `breathing(..., cell=T)` inside a translation screw fades the warp at the seams; keep its amount small
    (0.05-0.3 m) and its frequency low (0.05-0.1). Relax `stepScale` (0.8) with a warp.

### Surfaces (ADR-1044)

`sdf_node(name, tree, surfaces=[{"color": ...}, {"color": ...}, {"emission": [r, g, b]}...])` and
`surface(subtree, k)`. Up to 8 per object; parameters `sdf/<object>/surface/<k>/color|emission`; bind them in
the palette. Needs `compile: true` (the helper sets it). Roughness is per object.

### The journey (ADR-1042)

- Scene `camera`: `{"mode": 3, "fov": 62, "journey": {"chapters": [ {chapter}, ... ]}}`; a chapter is `name`,
  `start` (global metres), `from` (its own path's metres at `start`), `path` (floor points in cell 0; the
  camera adds `height`), `screw` (`translation`, `count`: the world's screw), `collide` (its SDF node),
  `radius`, `offset`, `yaw` (put its SDF nodes at the same position/rotation: `sdf_node(position=, yaw=)`),
  `nodes` and `lights` (shown only while the chapter is active).
- Parameters, all keyable: `camera/journey/distance` (metres; holds are equal keys), `yaw` (+ = left),
  `pitch`, `lookAhead`, `height`, `bob` and `stride` (the step, follows distance), `sway` and `swayRate` (the
  searching gaze), `radius` (0 disables the guard), `lookAt` + `lookAtWeight` (ease the gaze to a world point,
  in the journey's unwrapped frame), and `camera/fov` (the dolly zoom).
- A chapter swap is a jump in world position at a distance. Hide it in light: key `palette/value` and
  `camera/exposure/compensation` up and down around it (the example's 36-40.5 s). **Do not route audio pace
  into the distance ahead of a swap** (it moves the swap in time).
- Nodes on the journey: `"journey": {"distance": d}` on any composition node; its `position` becomes an offset
  (x right, y up, z forward); key `nodes/<name>/journey/distance`. The figure: Quaternius UAL1 (CC0,
  local-only) with `"animation": {"state": "Walk_Loop"}`, ~1.3 m/s to match the stride; add `"tint": [r,g,b]`
  and bind `nodes/figure/tint` to a palette role.
- The wrap is automatic; anything not periodic under the screw pops at it. Place lights and props in every
  covered cell with `screw_apply(point, T, count, k)` for k in about -1..4.
- Check a path: the test `Journey: every chapter of the example is clear...` in
  `tests/integration/test_liminal_journey.cpp` samples every chapter's path against the true distance; copy
  it for a new project (or point it at your scene) and run it with `"[liminal]"`.

### Continuous change (ADR-1041)

- The director owns trajectories on the timeline (smooth keys). Autonomous drift: `lfo`/`noise` sources.
- Music modulates through routes with `"chain": {"springHz": 0.35, "springDamping": 0.55, ...}` (weight: eases
  in and out) or `"integrate": true` (a rate becomes a position: a warp phase, a pace). Never put a `depthSource`
  on an integrating route. Scene states and sample-and-hold sources are not used.
- Seek equals play (bit-exact at 60 fps). Render finals at 60 fps or from 0.

### The palette (ADR-1043)

Project `"palette": {"states": [{"name", "colors": {role: [linear r, g, b]}, "scalars": {...}}], "bindings":
[{"role", "target", "component", "gain", "mode"}]}`; key `palette/position` (a float through the states),
`palette/saturation`, `palette/value`. OKLab blending. Bindings replace their targets after the routes. Good
targets: `sdf/<o>/surface/<k>/color|emission`, `scene/fogColor`, `env/sky/zenithColor|horizonColor|groundColor|sunColor`,
`lights/<id>/color`, `sdf/<o>/look/edge/color`, `nodes/<n>/tint`.

### Sky, sun, beacon, sun patch (existing features, confirmed by reading, not yet rendered here)

- Sky: the scene's `environment.sky` (ADR-036): `enabled`, `background` (draw it where the SDF misses),
  `zenithColor`, `horizonColor`, `groundColor`, `sunColor`, `sunDirection`, `useKeyLight`, `sunIntensity`,
  `sunSize`, `sunGlow`, `intensity`; the colours are parameters (`env/sky/*`). It is withheld wherever the
  architecture is closed, so a doorway or a lifted ceiling reveals it. Directional light and the sky are safe
  under a translation wrap.
- Beacon: a point light (`lights`) with `"volumetric": <strength>` scatters in the volumetric fog
  (`scene/volumeDensity` > 0, `scene/volumeLocalLights`); a surface with `emission` is the glowing doorway.
- Sun patch: a thin floor slab tagged with an emissive surface (it glows and blooms; it does not light).

### Capturing

```sh
tools/gpu-lock.sh ./build/release/src/avgen --headless --project <project.json> --render <out.mp4> \
    --range a:b --size 1280x720 --fps 30          # audio muxed from the project
tools/gpu-lock.sh ./build/release/src/avgen --headless --project <p> --frames 150 --range t: --size 1920x1080 \
    --bench-json out.json                         # GPU timing at t
./build/release/src/avgen --project <p> --audit-routes -   # every route and track: live/dead/hazard
```

### The analyzer (section 15-16, and the addendum's criteria)

`tools/liminal_critic.py` (python3 + numpy + ffmpeg; no new dependency):

- `inputs --project <p> --sections tools/liminal/all-you-got.sections.json --video <render> [--video-start s] --out <d>`
  writes Creative Critic inputs for an SDF project, which its AV Gen adapter could not describe: segments from the
  section map (bars to seconds on the song's grid, the section's `coupling` as the intended energy, so the Critic
  stops using its high-band proxy), shots from the timing sheet, the journey (chapters, the keyed walk as speeds),
  the palette timeline, the routes, and the addendum's ten questions in the intent. Then
  `cd ../creative-critic && .venv/bin/critic submit --inputs <d>/inputs.json --mode preview --wait`.
  Checked: job `job_1a0f4beb258918319` (fast mode, the 32 s example) completed with full coverage and 10 shots.
- `temporal --video <render> --audio ~/Desktop/"All You Got.wav" --sections ... [--video-start s] --out <d>` writes
  `temporal.json`/`.md`: **aggression-unanswered** (4-bar windows where the music's aggression -- loudness,
  high-band energy and noisiness together -- rises and the picture's activity does not), **snapping** and **flicker**
  (isolated change spikes, A-B-A flips, and whether they sit on audio transients), **colour-too-fast** /
  **colour-churn** (an OKLab colour move completed in under a bar away from a section boundary, or more than two
  per phrase), **breakdown-contrast** (a breakdown section not at least 15% below its neighbours in visual
  intensity). Findings are phrased like the brief's section 16 examples, and the report lists the addendum's
  emotional questions for the reviewer. `selftest` shows each check firing on its defect and quiet on its
  absence. On the 32 s example: aggression-activity correlation 0.83, 0 snaps, 0 flicker frames.

## Open (the art agent's needs list, ranked by the coordinator)

| need | status |
|---|---|
| 1 chapters | done (ADR-1042) |
| 2 fov keyable during a journey | yes (`camera/fov`) |
| 3 look-at blend | done |
| 4 several materials in a world | done (surfaces, ADR-1044) |
| 5 room growth / separating walls without the guard fighting | authoring (keyed boxes and translations); the guard only pushes when geometry comes within `radius`; set `camera/journey/radius` 0 to disable |
| 6 sky gradient and sun, withheld | existing (environment.sky); not yet rendered in an SDF scene here |
| 7 beacon with volumetrics, sun patch | existing (light `volumetric`, emissive surface); not yet rendered here |
| 8 figure bound to the palette | done (`nodes/<n>/tint`) |
| 9 motes near the camera across the wrap | open: anchor a particle emitter to the journey at the camera's distance; world-space particles will jump at a wrap, keep lifetimes short |
| 10 near-field tremble | done (`tremble()`: a warp with count 1, centred on the camera every frame; wrap it round the whole tree) |
| 11 section map to the analyzer | done (`tools/liminal_critic.py inputs` / `temporal` read `tools/liminal/all-you-got.sections.json`) |

## Known issues

- A faint jagged edge where the corridor stub meets the seam at floor level (about 28 s in the example): the
  seam guard lets a ray step up to the margin into the next cell; smaller margins trade it against seam stripes.
- The helix chapter's corners show the same at wall joints (margin 0.16 there).
- The example's look is engineering-grey on purpose; the art direction replaces palette, light and air.
- Motes across the wrap (need 9) are not built.

## Notes for a successor

- Creative Critic is a separate repo, `/Users/natefaulkenberry/Documents/GitHub/creative-critic` (Python;
  `adapters/avgen/avgen_adapter.py` is GV3-shaped and yields 0 shots for an SDF project; analyzers in
  `src/critic/analyzers/*.py`, rules in `src/critic/synthesis.py`; see the map in this file's history).
- Seek-exactness comes from ADR-901: route chain state is replayed on the 60 Hz grid. New chain stages must keep
  all their state in `ProcessorChain::State`.
- Rebuilding a test binary while a suite runs from it can kill the run; wait for the suite.
