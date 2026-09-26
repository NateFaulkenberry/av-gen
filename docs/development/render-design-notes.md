# Render stream (GV3 wave 2, ADRs 917-919): status at the pause, 2026-09-26

Branch `agent/render`, worktree `av-gen-render`, from main `0b623b88`. Paused on the coordinator's
instruction (usage limit). Everything below builds; the GPU side has NOT been run yet.

## Done (code, compiling, CPU-tested)
- **ADR-917, post sizes follow the frame.** `post/referenceHeight` (default 720) scales every
  pixel-sized post value by `chainHeight / referenceHeight`: bloom and halation pyramids
  (`scene::planPyramid`: weights shifted by log2(scale), fractional shifts shared between bracketing
  levels, whole-octave shifts bit-exact, finer levels weightless tent passes), anamorphic reach,
  motion-blur tiles (cap 160), the look stage's low-pass (octave descent instead of truncation).
  `PostStats` reports `pixelScale`, `anamorphicReach`, `motionBlurTile`, `motionBlurRadius`,
  `lookOctaves`; the render job logs one line. CPU tests: `tests/unit/test_post_resolution.cpp`
  (7 cases, 302 assertions, pass).
- **ADR-918, fog from the sky.** `scene/fogSky` (0..1). A 128x32 RGBA16F map of the sky's radiance
  (background via the new `shaders/sky_background.wgsl`, shared with `skybox.wgsl`, plus
  `atmosphereSkyAt`) is rendered by `shaders/fog_sky.wgsl` before the scene pass when fogSky > 0;
  `applyFog` mixes towards it. Frame group bindings 16/17; aux/mask/shadow groups bind a 1x1
  placeholder. UI: Environment panel, "Sky and fog", "Fog takes the sky's colour".
- **ADR-919, offline floors.** `QualitySettings::volumeStepFloor` 32, `textureAnisotropy` 16,
  `skyCubeFloor` 1024 at offline (others 0/8/0); each logs what it raised. UI: Render section's tier
  row shows the floors when offline is chosen.
- **World edge (report only, not built):** size is data (validated to 1e6 m; the ocean world ships
  40 km); growing GV3's world is a data change plus re-authoring, but `WorldMap::prepare`'s 97x97
  altitude survey re-ranges `altitude01`, so biomes and scatter in the core valley shift when the map
  grows. A backdrop ring was estimated at 1-2 days (terrain.cpp ring builder, composition terrain
  cache, entity after water entities) -- over the brief's "hours" bar.

## Not done
- **No GPU run yet.** Queued at the pause (their logs land in `build/`):
  `build/gpu_targeted.sh` -> `build/gpu-targeted/summary.txt` (new tests + post/sky/fog/motion/hdr
  families), `build/gv3-eval/base_batch.sh` (GV3 stills with the pre-change binary in
  `build/base/`). A full CPU suite was running: `build/cpu-suite-1.log`.
- The new GPU tests' thresholds (0.15 / 0.4 etc. in `test_post_resolution_gpu.cpp`,
  `test_fog_sky_gpu.cpp`, `test_offline_floors_gpu.cpp`) are guesses to be set from the first run.
- Expected re-baselines once the GPU suite runs: tests that render small frames with bloom at the
  default reference now get fewer levels (e.g. `test_image_look_gpu.cpp` "bloom levels" at 192x128
  asserts 2 levels; pin its `referenceHeight` to 128). `test_post_gpu.cpp`'s 128 px halo check may
  need the same.
- `build/gv3-eval/new_batch.sh` (post-change GV3 stills, identity check at the reference, fog stills,
  4K x2 two-second cost) not yet run; `build/gv3-eval/compare.py` compares a preview with a
  downsampled final.
- ADR-917/918/919 are drafts with measurements still to fill ("recorded below"); rows not yet added
  to `docs/decisions/README.md`.
- The look-stage fix's control is a throwaway build (octave loop removed) still to do.

## GV3 settings (to confirm once measured)
- `post/referenceHeight` 1080 and `post/motionBlur/maxRadius` 60: previews unchanged, finals match.
- `scene/fogSky` 1.0 (then density can come down toward the audit's 0.009-0.012).
- Final: 3840x2160, supersample 2, tier offline (floors apply).
