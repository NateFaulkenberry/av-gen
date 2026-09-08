# ADR-016: Post-processing chain and transient resources

- Status: Accepted (2026-09-08)
- Research: `docs/research/rendering-techniques.md` (bloom, tone mapping, DoF, motion blur, frame
  graphs), `docs/research/offline-rendering.md` §12

## Problem

Milestone 0.6 needs the built-in effects that define the "festival" look (bloom, colour grading,
lens effects, depth of field, motion blur, tone-mapping choices), driven by parameters, with the
scratch textures they need managed without per-frame allocation, and structured so the pass list
can later become a frame graph.

## Alternatives considered

1. Effects as user shader layers only (post stage).
2. A fixed built-in chain over a transient texture pool (chosen).
3. A full frame graph with declared reads/writes and automatic barriers now.
4. Per-object velocity buffers (MRT) for motion blur now.

## Decision

- `scene::PostSettings` (owned by the Engine, copied into `Scene::post` each frame) with every
  field a `post/<effect>/<field>` parameter (`registerPostParameters`), so effects are modulated,
  preset-ed and saved like everything else; the engine adds default routes (RMS → bloom
  intensity, onset → chromatic aberration).
- `rendering::PostProcessor` runs, in order: depth of field (CoC from reconstructed view
  distance, 24-tap golden-angle gather with tap-CoC weighting), camera motion blur (depth
  reprojection with the previous view-projection, neighbourhood-max velocity, 8 samples), bloom
  (soft-knee prefilter at half resolution, 13-tap downsample chain, 9-tap tent upsample chain,
  additive mix), and a composite pass (barrel/pinch distortion, chromatic aberration, white
  balance, hue rotation, log-space contrast, saturation, lift/gamma/gain). Tone mapping
  (ACES fitted, AgX, extended Reinhard, Khronos PBR Neutral, clamp), vignette and seeded film
  grain live in the output pass. Effects that are off cost nothing.
- `gpu::TransientPool` hands out scratch textures by (size, format, usage), reuses them across
  passes and frames, and frees ones unused for 60 frames. This is the resource half of a frame
  graph; pass ordering stays explicit code.
- Order in the frame: user post layers, then the built-in chain, then tone mapping.

## Rationale

- The chain covers the roadmap's post list with well-known, cheap techniques (Jimenez bloom,
  Karis-style soft threshold, standard grading maths, McGuire-style reprojection blur) that fit
  in fragment passes and the existing HDR pipeline.
- The pool removes allocation churn (about a dozen textures per frame) without imposing a graph
  API before there are enough passes to justify one.
- Depth-based motion blur needs no changes to material pipelines; per-object velocity is the
  documented next step when object motion blur is required.

## Consequences

- Motion blur only sees camera motion; spinning or moving objects do not blur.
- DoF is a single-layer gather (no separate near/far fields), so bright background bleeds
  slightly onto in-focus edges.
- Bloom runs at half resolution downwards; very small highlights can flicker.
- AgX uses the common polynomial fit; the output pass decodes it back to linear so one sRGB
  encode applies to every operator.
- Film grain is seeded from the frame index, so offline renders are reproducible.
