# Sonic Garden abstract direction: progress (sonic-art)

Brief: `04-brief-abstract-direction.md` (the owner's words govern). Plan: `ABSTRACT-PLAN.md`. Worktree
`/Users/natefaulkenberry/Documents/GitHub/av-gen-sonic`, branch `proto/sonic-garden`, shared with the `sonic-engineer`
(who owns `src/`, `shaders/`, `tests/` and posts ready notes in `PROGRESS-stylized-eng.md`). I own `tools/sonic_vfx/`,
`examples/sonic-garden/`, `examples/index.json` and my docs. Commit only my own paths (`git commit -- <paths>`), never
`assets/`. Push after milestones (`git push origin proto/sonic-garden`). Review media:
`~/Desktop/av-gen-review/28-sonic-abstract/`.

## Resume here (cold)

- **State (2026-10-02 18:30):** the plan is written (`ABSTRACT-PLAN.md`). Next: the review tool
  (`tools/sonic_vfx/abstract.py`), then blockouts of all eight.
- **The pinned engine:** `96bc0214` at `$S/vfx/bin-96bc0214`, through `$S/vfx/avgen.sh` (sets `AVGEN_SHADER_DIR` to
  the pin's shaders). `S=/private/tmp/claude-501/-Users-natefaulkenberry-Documents-GitHub-av-gen/fed9412c-8e5e-42c0-a62b-e703644796ad/scratchpad`.
  Never render art from `build/release` (the engineer's). Re-pin when the engineer posts a ready note: `git archive
  <sha> | tar -x -C $S/vfx/src-<sha>`, `cmake --preset release -DCPM_SOURCE_CACHE=/Users/natefaulkenberry/Documents/GitHub/av-gen/.cache/cpm`,
  build `avgen avgen_sonic_probe`, copy the binaries and `shaders/` into `$S/vfx/bin-<sha>`, point `avgen.sh` at it.
- **GPU:** every job through `tools/gpu-lock.sh`, one job per hold (shared with the Liminal art agent and the
  engineer). No `timeout` command.

## Log

- 18:00 read the brief, the engine docs (procedural geometry, materials, SDF, splines, particles, post, temporal), the
  rejected passes' stills; wrote `ABSTRACT-PLAN.md`.
