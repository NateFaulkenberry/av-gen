# ADR-1935: A shader layer stage after the temporal stage ("late")

- Status: Accepted (CHRONOS Gate S1, 2026-11-02), scene/chronos.
- Code:
  - `shaders::LayerStage::Late` and its name, `"late"`, in `src/shaders/shader_layers.{hpp,cpp}`;
  - one image-layer loop serving both the Post and Late stages, in `SceneRenderer`
    (`src/rendering/scene_renderer.cpp`).
- Tests:
  - `[gpu][temporal][adr1935]` in `tests/rendering/test_temporal_gate_gpu.cpp`;
  - `[shaders][adr1935]` in `tests/unit/test_temporal_gate.cpp`.

## Context

User shader layers had two stages:
- **Background:** drawn behind the geometry.
- **Post:** run on the HDR scene image before the temporal stage.

CHRONOS needs one gravitational warp that bends the figures, their slit-scan trails, the luminous structures and the
background together (brief §6: "one field, not unrelated filters"). A Post warp cannot do that. It runs before the
capture, so the ring stores warped colour, while ADR-1934's gated sampler reads the identifier target, which is not
warped. Trails would then be gated by where a figure was before the warp, and drawn where it was after it.

## Decision

1. **A third stage, Late.** It runs after the temporal stage, on the image the temporal effects return, and before the
   post chain. A Late layer's `inputImage` is that image. Its stage name in projects is `"late"`.
2. **One loop for both stages.** Post and Late share one loop over the same ping-pong targets. The loop always writes
   the target its `ping` names, which is never the one holding its input.
3. **The history stays clean.** The ring still captures the clean scene before any Late layer (ADR-410), so a Late
   warp never enters the history.

## Consequences

- **Warps belong in Late.** A warp or grade that must apply to temporal output goes in Late. One that the temporal
  effects should see and remember goes in Post, as before.
- **The test.** It uses a horizontal mirror, which does not commute with the gated sampler.
  - A Late mirror equals the mirrored gated frame exactly.
  - A Post mirror does not, because its trails are gated by unmirrored identities.
- **Nothing changes for existing scenes.** No existing scene uses `"late"`, and the Post and Background stages are
  unchanged.
