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
| ADR block | 1200-1219 |

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
| CP3: the propagation and the rest state, from a continuous run | in progress |

## How to work on it

- **The scene:** `python3 examples/bioluminescent/build.py` writes `rift.scene.json` (gitignored, 13 MB), `rift.json`
  (Trench) and `rift-live.json`, and a probe copy `build/biolum/rift-trace.json` for `--sonic-trace` (the arc
  without the GPU: `visual.pState` = state index / 20).
- **Stills of a time-varying medium must come from continuous play from 0**: a seek replays a grid's backlog with
  the landing parameters (ADR-1168's known limitation). Render the song to video at low resolution and pull the
  frames.

- `python3 examples/bioluminescent/cp1.py` regenerates the organisms (`meshes/`, gitignored) and the CP1 scenes
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
