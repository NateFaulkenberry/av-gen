# Liminal Euclidean World: engineering progress (PROGRESS-eng)

*The engineering agent's running notes, kept current so a cold successor can resume. The art agent keeps
PROGRESS-art.md. Governing documents: `00-brief.md`, `01-addendum-emotion.md` (wins where they differ).
Design and research: `ENGINEERING.md`. Decisions: ADR-1040 to 1044.*

## Where things stand (2026-09-30, about 20:00)

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
| f | `examples/liminal/` (48 s) via `tools/make_liminal_example.py`; capture in `~/Desktop/av-gen-review/24-liminal-space/eng/` | done (capture is of the 32 s version; re-render after the chapter version) |
| 3 | analyzer: SDF adapter + temporal checks | not started |

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
| 10 near-field tremble | open (small: a warp windowed by distance from the camera) |
| 11 section map to the analyzer | open (analyzer work not started) |

## Notes for a successor

- Creative Critic is a separate repo, `/Users/natefaulkenberry/Documents/GitHub/creative-critic` (Python;
  `adapters/avgen/avgen_adapter.py` is GV3-shaped and yields 0 shots for an SDF project; analyzers in
  `src/critic/analyzers/*.py`, rules in `src/critic/synthesis.py`; see the map in this file's history).
- Seek-exactness comes from ADR-901: route chain state is replayed on the 60 Hz grid. New chain stages must keep
  all their state in `ProcessorChain::State`.
- Rebuilding a test binary while a suite runs from it can kill the run; wait for the suite.
