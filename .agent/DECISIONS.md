# Decisions

Compact index of decisions that shape work. Full reasoning lives in `docs/decisions/ADR-*.md`.

## Architecture
- **ADR-264** — project `parameters` are applied *over* the scene. A scene edit alone may be
  overridden. Check both layers before concluding an edit did nothing.
- **ADR-091** — two-tier determinism: scrub must equal play for the baked tier. Anything time-driven
  is a pure function of time, never an accumulator (`LfoSource::update` is the precedent).
- **ADR-351** — the path tracer is `avgen::pathtrace`, named for its technique. "Offline" was
  already taken: six ADRs use it for a WebGPU quality tier, and `app::RenderJob` renders at
  `QualityTier::Realtime`.
- **ADR-343** — one `dayPhase` drives the whole environment; it is a property of
  `scene.environment`, not a world effect. World effects (ADR-207) are *spatial* phenomena with a
  source and a propagation; day/night has neither.
- **ADR-345** — lighting and visible background are separate choices. With an HDRI bound there is no
  procedural sky cube, so drawing one needed four vec4s appended to `FrameUniforms`.

## Deliberate non-changes
- **ADR-352** — the glTF BRDF is kept faithful and *gains up to 68% of its energy at grazing
  angles*. Owner chose faithful over compensated: compensating would desync the path tracer from the
  rasteriser on every asset. A diagnostic reports it instead. **If an interior render is
  inexplicably bright, check this first.**
- **ADR-340** (retired) — Glowmere Valley 3 was cut by the owner, but its findings stand: the river
  is a wall, and the gate on hill trees is ADR-174's riparian ladder, not biome or `maxSlope`.
- The water flicker (ADR-183, ~57% reachable by no authored parameter) is deliberately unfixed.
  `water.wgsl` is not to be edited on a hypothesis; that has already cost three rounds.

## Working rules earned the hard way
- **A subsystem with no caller is not done, and no test will tell you.** Four things shipped
  unreachable in one session — a scene missing from `examples/index.json`, a cycle registering no
  parameters, a renderer with no CLI, LOD switched off in its own scenes. Tests construct the object
  directly and share the product's blind spot. Check reachability separately.
- **ADR-182** — a probe that cannot fail proves nothing. Bands, not floors. A control that comes back
  byte-identical to its arm has proved *the control did not fire*, not that the arm is inert.
- **A suite result can be absent rather than wrong, and absence looks like patience.** Four shapes
  seen in one session: a killed run prints `FAILED:` with no `with expansion:` (SIGTERM, exit 143);
  an incremental build silently omits test files a merge added (reconfigure CMake); a crash exits
  133 with no summary line; and a task can "complete" having produced **no output at all**. The
  check that settles it is not waiting longer — it is comparing the built binary's timestamp against
  the commit under test.
- **Name your test binary distinctly when other agents are running.** `pkill -f 'avgen_tests'`
  matches every worktree's binary, and it has produced a phantom 878-case regression and killed at
  least two verification builds. And **`cp` of a Mach-O invalidates its ad-hoc signature on Apple
  silicon** — the kernel SIGKILLs the copy with no output whatsoever. `codesign -s - -f` the copy.
- **A render is evidence about the binary that produced it.** `--target avgen_tests` does not build
  `src/avgen`.
- Numerical agreement is not proof of visual alignment. Every serious bug this session was invisible
  in numbers and obvious in a frame.
