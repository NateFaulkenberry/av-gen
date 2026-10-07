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
| Section 4 experiments: what costs, what a specialised path buys | next |

## How to work on it

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
