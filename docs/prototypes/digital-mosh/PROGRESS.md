# DIGITAL MOSH: Progress (resume from here)

Branch `proto/digital-mosh`, worktree `../av-gen-mosh` (from `gpu/productionization` 6e28eab8). Brief:
`00-brief.md`. Research: `01-research.md`. Design: `02-design.md`. Implementation notes: `03-implementation.md`.
Evaluation: `04-evaluation.md`. Review media: `~/Desktop/av-gen-review/38-digital-mosh/`.

## State

| Milestone | State | Commit |
|---|---|---|
| Brief committed | done | 3f060b9c |
| Research (surrealists, codec corruption, perception, synthesis) | done | 1f66a284 |
| Design (world, contagion, colour, audio layers, arc, camera, tiers) | done | 2c136ace |
| ADR-1160: raymarched SDF shadows from the light's view (defect fix) | done | f94337c6 |
| ADR-1161: bounded integrate; ADR-1162: material quantize op | done | 6866444c |
| ADR-1163: scalar grid ceiling; the scene's path-guard test | done | 383500e6 |
| ADR-1164: hold and elapsed state triggers; the arc, stages, audio mapping | done | ee3ac206 |
| Art pass 1 | in progress | |

## How to work on it

- **The scene is generated.** Edit `examples/digital-mosh/build.py`, then run `python3 examples/digital-mosh/build.py`.
  That writes `digital-mosh.scene.json` and three projects: Feline Footwear, Trench, and LIVE.
- **Path guard.** Run `./build/release/tests/avgen_tests "[digital-mosh]"` after every build. Presets silently ignore
  unknown parameter paths, and this test fails on any path that does not exist.
- **The arc without the GPU.** Add a probe interpret source and a `sonic` block, then run
  `avgen --headless --project <p> --sonic-trace out.csv`. The trace includes the macros, `visual.*` and
  `state.index`, so the engine runs the state machine there too. Helpers, kept in the scratchpad and not in the
  repo:
  - `mktrace.py` writes the probed projects;
  - `plot_trace.py` plots the columns (use the creative-critic venv's python; it has matplotlib);
  - `arcsim5.py` is the Python model of the arc that the parameters were tuned with.
- **Stage stills.** A project whose `states.initial` is the stage and whose triggers are empty, rendered from
  t = 0. The state machine is **not** replayed on a seek, so any render must start at 0 to show the true stage.
- **GPU.** Every render and every GPU test goes through `tools/gpu-lock.sh`. Other agents (gpuprod, astral-forge)
  share the lock and run long suites, so expect to queue.

## Engine facts learned here (each one cost time)

1. Raymarched SDF shadows were marched from the camera's eye (ADR-1160). Fixed.
2. A procedural `box` `size` is the full size and clamps at 1000. Scale with `sourceTransform` instead. The plain's
   edge was the "horizon".
3. A compound field inside a compound evaluates as 0 on the GPU (`docs/gpu-fields.md`). Keep compounds one level
   deep.
4. Scalar grid injection is unbounded. Use `ceiling` (ADR-1163).
5. Gray-Scott runs at one time unit per second of film, too slow for a song. The contagion is a scalar grid instead
   (injection + curl advection + diffusion + dissipation).
6. Interpret weights are parameters ranged 0 to 100. A negative weight clamps to 0, so use `invert` with a bias.
7. A material op's constants are clamped to ±1000. `hueShift` adds `b.x`, so point `srcB` at a zeroed register.
8. State triggers fired only on crossings, and `bar every N` counts global bars (ADR-1164 adds `hold` and
   `elapsed`).
9. The default transition is 2 s, and `current()` is the committed state, not the pending one.
