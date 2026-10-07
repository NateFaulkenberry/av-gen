# BIOLUMINESCENT ENVIRONMENT (The Rift): progress, resume from here

Branch `proto/bioluminescent`, worktree `../av-gen-biolum`, from main f56eaab0.

| | |
|---|---|
| Brief | `00-brief.md` (verbatim) |
| Research | `01-research.md`, `01b-reference-synthesis.md`, `01c-reference-index.md` |
| Art direction | `02-art-direction.md` |
| Architecture and the §18 assessment | `03-architecture.md` |
| Review media | `~/Desktop/av-gen-review/40-bioluminescent/cp<N>-<name>/` |
| Track | Trench, `assets/audio/trench.wav` (never commit). The drop is at 95 s |
| ADR block | 1200-1219 (1200 the ecosystem and the Environment seam; 1201 the excitable propagation grid) |
| Evaluation | `05-evaluation.md` |
| Design (music → medium → organisms, the arc, the camera, live controls) | `04-design.md` |

## State

| Step | State |
|---|---|
| Brief committed | done, 544ef65e |
| Architecture inspected; the seam chosen (03 §2) | done |
| Reference board (45 sources), synthesis, research and art direction | done |
| CP1: reference board plus eight stills from the general renderer only, measured | done |
| A/B attribution of the general renderer's cost (03 §3.1) | done, a1101882 |
| ADR-1200: the ecosystem block + EcosystemRenderer (emitters as compute-accumulated points), tests | done, 17f81a95, d640f052 |
| ADR-1201: the excitable propagation grid (helper agent, `../av-gen-biolum-prop`, merged) | done, 4e52fe8d |
| CP2: the same world with ecosystem emitters (56 vs 77.6 ms; the ecosystem itself ~1 ms) | done |
| The scene generator `build.py`: the medium, species on its channels, the arc traced on Trench | done, 4e8520af |
| CP3: the drop and the flight (owner: CP3 over-corrected, too dark and monochrome) | done |
| CP4: colour back at rest, the drop made big, CP1/CP3/CP4 at the same cameras; the Critic 0.975 and its fixes | done (the owner: move on) |
| Engine fixes: StateMachine::reset's stale clock (a second seek landed in the initial state); headless `--range a:` not seeking | done, c6b48e0e, cc615468 |
| The Environment seam as an interface (EnvironmentRenderer, EnvironmentFrame), cherry-picked by production Astral Forge | done, a8599186 |
| Performance attribution (03 §6), live optimisation, live profiles (30 fps achieved; 60 not), live-input session, offline vs live | done |
| Final full Trench render, final drop video, the Critic on the full run | done (0.98; it caught the end-of-spline aim defect, which is now fixed, and the run re-rendered) |
| Engine fix: a spline camera parked at an open spline's end lost its aim; the Rift's body flight now fits its path | done, b72866dd |
| Merge origin/main (the seam runs `{ecosystem, astral}`) | done, 9f2ed05d |
| The generated scene and meshes committed (the examples index opens THE RIFT from a clean checkout) | done, 77db67c8 |
| Both full suites | see the final report |

## How to work on it

- **The scene:** `python3 examples/bioluminescent/build.py` writes `rift.scene.json` (committed, 13 MB), `rift.json`
  (Trench) and `rift-live.json`, and a probe copy `build/biolum/rift-trace.json` for `--sonic-trace` (the arc
  without the GPU: `visual.pState` = state index / 20).
- **Stills of a time-varying medium must come from continuous play from 0**: a seek replays a grid's backlog with
  the landing parameters (ADR-1168's known limitation). Render the song to video at low resolution and pull the
  frames.

- `python3 examples/bioluminescent/cp1.py` regenerates the organisms (`meshes/`, committed) and the CP1 scenes
  (`cp1/`, gitignored).
  - Heights come from the engine (`build/release/tools/avgen_world_preview`), cached in `build/biolum/`.
- Stills: `tools/gpu-lock.sh <scratchpad>/render_stills.sh <projdir> <outdir> 1280x720 <names...>`.
  - That runs `avgen --project <p> --render <dir> --format png --range t:t` per still; output paths must be
    absolute, because they resolve against the project directory.
- Benchmark: `tools/gpu-lock.sh ./build/release/src/avgen --headless --project <p> --frames 240 --size 1920x1080
  --tier realtime --bench-json <f>`.

## Engine facts learned here

1. A scene load failure (here, an unknown `thinFilm` key) silently falls back to the default orb scene in a
   `--render`. Always look at the frame.
2. `camera/position` and `camera/target` are not parameters for a free camera, so they cannot be set from a project.
   Put the camera in the scene.
3. The generator distribution's ground is not the terrain (`scene/generator.hpp`), so CP1 places organisms
   CPU-side as `points`, which are capped at 65,536 per object.
4. Environment volume noise is `volumeNoise`, not `volumeNoiseAmount`.

5. A `--range a:b` render of a project with scene states used to land in the initial state. The cause was
   `StateMachine::reset`, which kept the last updated second, so a second seek's replay could fire no `elapsed`
   trigger. It is fixed (c6b48e0e).
6. The headless benchmark's `--range a:` restarted only the clock (fixed, cc615468). Arc-point benchmarks before
   that measured the opening.
7. Offline renders lift procedural distance culls and LOD rungs by policy (ADR-186). Use `--render-limits live`, or
   keep the bodies lean: vertex clustering makes feathery organisms blocky.
8. The volumetric march costs about 1 ms per step at 1080p; the max distance does not change it. The live project
   uses 16 steps.
9. Terrain chunks are draws and entities: 40 m chunks over a 2.4 km map were 2,224 draws and 12.5 ms of CPU. Use
   80 m chunks.
10. An open spline clamps its samples. A spline camera that reached the end used to keep its look-ahead target on
    the end point too, so the target coincided with the camera and the aim was float rounding: single frames
    pointing anywhere. The look-ahead now continues along the end tangent (b72866dd). Size a flight's paces to its
    path: `test_rift.cpp` plays the whole track and checks it.
