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
| WIP for the owner (first build) | done | `~/Desktop/av-gen-review/38-digital-mosh/wip/` |
| Art pass 1, part: ink stain, matter blocks, sky fix | done | b07c5a60 |
| Art pass 1: terrain, olive and Tanguy forms, painting palettes, travelling camera; ADR-1164 `idle` | done | 12a71635 |
| Art pass 1b/1c: palette and restraint fixes from the full-song review | done | 73ec201d, ee61637f |
| Art pass 2: Critic fixes (shimmer, wobble, beat moves, varied vantages), liquid skin, double scale, MIDI | done | 95c6b672..49cf8d28 |
| Performance: per-stage offline profile (auto, Ultra), SDF-shadow A/B, live-mode profile, two live sessions | done | `04-evaluation.md` |
| Art pass 3: warm Dream haze, simpler Pixels stratum, liquid mask inside its skin and sunk by its mean swell, Light not clipped | done | 3957ec05..(final) |
| Pass 4 (owner notes): sculpted land, altitude, raking light, contagion spreads; ADR-1165 SDF shadows at a fraction of the map | done | 982af134, bbf6ed62 |
| Pass 5: the soaring flight (ADR-1166 banking), landmarks, sand texture, in-world collapse | done | 9aef84b3..afbfb690 |
| Pass 6: the mirror tableaux (owner's correction) | tried and abandoned by the owner; reverted | 828ed6c5..c326a0f2, reverted 5f75c89e..1b32bcea |
| Pass 5b: the floating eye, circling the stain, ADR-1168 (seek replays the control layer) | done | bd930b97..089eaab8, b4323355 |
| Final renders (1080p, both tracks), Critic, evaluation, full suites | see `04-evaluation.md` | |

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

## The owner's feedback so far (2026-10-05)

- The first build was crude; the camera must travel through the world (the brief's §13), not orbit or sit still.
- Real landscape, from AV Gen's terrain system: not a flat plane.
- Palettes taken from actual Surrealist paintings, sampled from reproductions (`05-palettes.md`).
- Earlier scenes: no visible edge of the world and no unrendered black; no vortex or spinning-tower centrepiece.

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
10. A periodic trigger interrupts a running transition. Mark it `idle` (ADR-1164), or a ladder never lands.
11. Terrain cannot be deformed at runtime: its geography is not parameters, and the chunks are built once. Liquid
    land has to come from other systems.
12. A `world` block falls back to Glowmere's layers, features and biomes for any key it omits. Set them all.
13. Offline render speed: the full song at 1280x720 renders at about 10.5 fps (offline tier), so a song takes
    about 10 minutes.
14. `--live-capture` re-renders every second frame at its own size: a captured live session's frame rate is not
    evidence. Profile live with `--live-profile --mode live`, or run uncaptured.
15. Python's `time.monotonic_ns` has a per-process origin on macOS; AV Gen's live clocks are `CLOCK_UPTIME_RAW`.
16. Raymarched SDF shadows are the scene's largest cost (each caster is marched per shadow texel per cascade), and
    the shadow atlas does not shrink with LIVE AUTO's render scale.
17. A `box` field's `size` is a HALF extent, and `softness` extends past it.
18. A seek used to reset the scene states, the macros and every route on a non-pure source, so a range render framed a
    different shot from a full render. ADR-1168 replays them (offline only).
19. A transparent `blend` surface occludes raymarched SDFs behind it (found in pass 6).
