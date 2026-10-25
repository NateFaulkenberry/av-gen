# ADR-1673: An unlit procedural surface that provably needs no lit shading draws through a slim fragment entry

- Status: Accepted (scene/talking-heads, 2026-10-25; the owner chose it after stop 5)
- Code:
  - `shaders/procedural.wgsl`: `fs_proc_unlit`.
  - `src/rendering/procedural_renderer.{hpp,cpp}`:
    - the eligibility per draw item;
    - the base pipelines, built lazily (`ensureUnlitPipelines`);
    - the compiled-materials variants' unlit pair;
    - the per-draw choice;
    - `ProceduralStats::unlitFastDraws`;
    - `setUnlitFastPath`.
  - `src/scene/material_program.{hpp,cpp}`: `MaterialProgram::unlitFastEligible`.
- Tests:
  - `tests/unit/test_procedural.cpp` `[adr1673]`: the eligibility whitelist. Screen-space inputs, tangent-frame,
    texture and field ops, normal, occlusion and height outputs, and enabled layers all refuse; disabled ones do not.
  - `tests/rendering/test_procedural_gpu.cpp` `[adr1673]`:
    - an eligible unlit surface takes the fast path (`unlitFastDraws > 0`, 0 with it off) and renders the same
      image to within one level per channel;
    - a lit surface, and an unlit surface whose program reads ambient occlusion, never take it, and their images are
      bit-identical with the fast path on and off.

## Problem

Talking Heads' stop-5 background is unlit. It is two large sheets with small programs, plus thin line meshes. It cost
6-9 ms a frame at 1080p on an M2 Max.

A plain unlit procedural sheet filling the frame cost about 4.5 ms with no program at all, and each program op about
0.2 ms more. The unlit branch of `shadeSurface` is short, but every procedural draw runs the one large `fs_proc`. Two
things make that expensive:
- **Unused setup.** When a program exists, the derivative-based geometry inputs, the tangent frame and the
  ambient-occlusion read are hoisted before any branch.
- **Register pressure.** The whole lit path is compiled into the same entry, and its register pressure costs
  occupancy on every fragment, lit or not.

## Decision

A second fragment entry, `fs_proc_unlit`. It is `shadeSurface`'s unlit branch and nothing else:
- the program, with the screen-space inputs left at their defaults;
- the emission lane and the instance's variation, exactly as `fs_proc` applies them;
- the waves and the fog;
- the same auxiliary targets.

The renderer draws an object with it only when every condition holds, all decided per draw on the CPU:
- **the material** is unlit, not masked, has no textures, and does not refract;
- **the object** has no effect lanes;
- **its program**, if it has one, passes `MaterialProgram::unlitFastEligible`:
  - no screen-space input (curvature, convexity, concavity, cavity, occlusion, height, normal variance,
    triplanar weights, footprint);
  - only plain arithmetic, noise and ramp ops (no textures, fields or tangent-frame ops);
  - no enabled layers;
  - no normal, occlusion or height output.

The conditions are conservative: anything not proven equal takes the full path.

**Pipelines.**
- The base module's pair is built the first time a frame has an eligible draw, so a scene without one pays nothing.
- Each compiled-materials variant (ADR-1493) builds its own unlit pair beside the lit one, on Dawn's workers.
- The fast path covers the lit pass only. The depth prepass and the shadow passes are unchanged.

## Consequences

- **Off is byte-identical.** Every draw that is not eligible takes `fs_proc` exactly as before. With the fast path
  off (`setUnlitFastPath(false)`), every draw does. Tested on a lit surface and on an ineligible unlit one.
- **An eligible draw renders the same colour.** The values are the full path's, minus the lighting it never used:
  within one level per channel in the test, with no visible difference.
- **Talking Heads**, the full scene with the background, at 1080p headless on Feline stems:

  | | Before | After |
  |---|---|---|
  | GPU p50 | 20.5-24.5 ms | 17.8 ms |
  | GPU p95 | about 36 ms | 24.7 ms |

  The scene without the background measures 14.7 ms, so the background now costs about 3 ms where it cost 6-9.
- **Not extended to glTF entities.** They use the scene renderer's own shader, so they keep the full path. The same
  split would apply there if a scene needs it.
