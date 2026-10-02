# Sonic Garden art restart: progress (sonic-art)

Brief: `03-brief-art-restart.md` (the owner's words govern). Worktree `/Users/natefaulkenberry/Documents/GitHub/av-gen-sonic`,
branch `proto/sonic-garden`. Commit only my own paths (`git commit -- <paths>`), never `assets/`. Review media:
`~/Desktop/av-gen-review/27-sonic-art-restart/`.

## Resume here (cold)

- **State (2026-10-02 16:00):** step 1 (analysis) written: `ART-RESTART-ANALYSIS.md`. Engine capability survey in
  progress (terrain, water, glTF assets, effects). Next: the 16-place plan (`ART-RESTART-PLAN.md`), then blockouts in
  batches.
- **The pinned engine:** `96bc0214` (no engine change since: `git log 96bc0214..HEAD -- src shaders` is empty), at
  `$S/vfx/bin-96bc0214`, run through `$S/vfx/avgen.sh` (sets `AVGEN_SHADER_DIR` to the pin's shaders). Never render
  art from `build/release`.
  `S=/private/tmp/claude-501/-Users-natefaulkenberry-Documents-GitHub-av-gen/fed9412c-8e5e-42c0-a62b-e703644796ad/scratchpad`.
  If the scratchpad is gone, rebuild the pin: `git archive 96bc0214 | tar -x -C $S/vfx/src-96bc0214`, then
  `cmake --preset release -DCPM_SOURCE_CACHE=/Users/natefaulkenberry/Documents/GitHub/av-gen/.cache/cpm` and build
  the targets `avgen avgen_sonic_probe`; copy the binaries and `shaders/` into `$S/vfx/bin-96bc0214`.
- **The silent beauty test** is `tools/sonic_vfx/variant.py: make_silent` (routes, interpret and publish sources,
  triggered effects, live input, audio and notes removed; parameters, always-on effects and the timeline kept).

## Engine needs (for the coordinator)

(none yet)

## Log

- 15:40 read the brief, the engineering notes and the old pass; the old stills and clips reviewed (16 of 16).
- 15:50 `ART-RESTART-ANALYSIS.md`.
