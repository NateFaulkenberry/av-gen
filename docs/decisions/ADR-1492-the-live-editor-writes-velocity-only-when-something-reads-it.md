# ADR-1492: The live editor writes the velocity target only on frames something reads it

- Status: Accepted (perf/resonator-live)
- Amends: ADR-035 (the velocity target), in the live editor only.
- Code: `src/rendering/scene_renderer.{hpp,cpp}` (`setVelocityOnDemand`, `velocitySkipped`, `frame.styledGround.w`),
  `shaders/procedural.wgsl` (`skipVelocity`), `shaders/common.wgsl`, `src/app/application.cpp` (`runLive`),
  `src/app/live_profile_run.cpp` (the headless live profile renders as the editor does).
- Tests: `tests/rendering/test_procedural_gpu.cpp`, tag `[adr1492]`.

## Problem

The lit pass writes a velocity target (ADR-035). Each procedural vertex pays for it with a fourth evaluation of the
whole deformer chain, at last frame's time and with last frame's object matrix. The readers of the target are:

- motion blur;
- the aux debug views;
- a render job's `velocity` AOV;
- the tests.

The temporal history takes the view and does not read it yet (`temporal_history.cpp`).

In the live editor, motion blur is often off. The live ladder closes its gate from Medium (balanced) or Low down
(ADR-1083), and all three Resonator scenes author `post/motionBlur/amount` 0. On those frames the fourth evaluation
is a quarter of the lit pass's procedural vertex work spent on a target nobody samples. On Resonant Silk, whose 52k
vertices run eight deformers (two of them the whole resonance field), that quarter is the single largest avoidable
cost in the frame.

## Decision

1. **`SceneRenderer::setVelocityOnDemand(bool)`, off by default.** Only `Application::runLive` (the editor) and the
   headless live profile turn it on.
   - Every other renderer computes velocity on every frame, as before: an offline render, a render job (which owns
     its own renderer, AOVs included), the tests and the director stills.
2. **Per frame, on demand.** The target counts as unread when all of the following hold:
   - the motion-blur gate is closed: post off, the level's `motionBlur` false, or `motionBlurAmount × shutter/360` at
     zero, which is exactly the post chain's own condition for skipping the pass;
   - no aux debug view is shown.

   The renderer then writes `frame.styledGround.w = 1`, a lane that was always 0. `velocitySkipped()` reports it.
3. **The procedural lit pass reads that lane** before evaluating the chain. That is where ADR-1021 measured a late
   uniform read as costly. When the lane is set, the lit pass writes `prevClip = clip`, which is zero motion, and
   skips the previous-frame chain.

## Consequences

- **The picture is unchanged.** On a frame where velocity is skipped, nothing samples the target.
  - The GPU test renders the same frame with and without on-demand, and the colour hashes are equal.
  - The previous-frame chain is a pure function of last frame's time and matrices; it is never accumulated. So the
    first frame that needs velocity again is exact. The test plays four frames with the chain skipped, switches motion
    blur on, and compares that frame with a renderer that never skipped: the hashes are equal, and that frame differs
    from a sharp one, so the blur is really there.
  - On-demand changes neither seek nor play: a fresh renderer opening at a frame draws, with and without on-demand,
    the same bytes, both sharp and with motion blur open.
  - Seek and play themselves already differ. With every optimisation off, a fresh seek and sequential play to the same
    frame render different bytes, from the renderer's own frame-to-frame state. That is unrelated to this ADR, and the
    test does not claim otherwise.
- **Velocity is zero, not stale, while unread.** A reader added later that runs when motion blur is off must say so to
  the renderer, by extending the condition in `scene_renderer.cpp`. Otherwise it reads zero motion in the live editor.
  This is the cost of the decision. The condition lives next to the post chain's own gate for that reason.
- Only procedural draws skip. The mesh path's previous position is one matrix multiply, which is not worth a branch.
- **Measured.** The report has the numbers (§4).
