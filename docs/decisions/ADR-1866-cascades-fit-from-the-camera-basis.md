# ADR-1866: Shadow cascades are fitted to the camera's basis, not its inverse view-projection

- Status: Proposed, scene/strange-attractor (canyon shadow-jitter investigation, 2026-10-28).
- Code:
  - `ShadowCamera` and `fitDirectionalCascade(const ShadowCamera&, …)` (`src/rendering/shadow_math.{hpp,cpp}`). Both fit
    paths share `fitSlice`.
  - `ShadowRenderer::update` takes the basis (`src/rendering/shadow_renderer.{hpp,cpp}`).
  - `SceneRenderer::updateLights` builds the basis in double from the view matrix and the half-angle tangents the
    frame block carries (ADR-1807).
- Tests: `[adr1866]` in `tests/unit/test_shadow_cascades.cpp`. A camera gliding 3 km from the origin with a 60 km far
  plane:
  - from the basis, the cascade's radius is identical every frame and matches the analytic sphere;
  - from the float inverse view-projection, it wanders by 4.3 m (the control).
- Evidence: `docs/prototypes/strange-attractor/09-canyon-cirrus-2.md` § shadows.

## Context

The owner reported shadow jitter on the canyon walls in the flythrough. The investigation, in the evidence doc,
printed every cascade's fit per frame (`AVGEN_SHADOW_STATS`) across consecutive frames of a constant field of view.
- The nearest cascade's half-extent went 118.6, 111.4, 110.9, 119.3, 118.7, 110.7, 110.8, 127.7, 127.6, … m.
- The farthest went 2906, 2726, 2714, 2919, … m.
- The texel moves with it, up to 15% from one frame to the next. The shadow map is re-rasterised at a new scale every
  frame, and every edge's staircase is cut afresh. Texel snapping (ADR-112) cannot hold a grid whose pitch changes.

The cause is the sub-frustum's corners. They are unprojected through a float `invViewProj` whose far plane is tens of
kilometres away. ADR-1807 found the same imprecision in the sky's view rays and moved them onto the camera basis.

## Decision

The renderer fits each cascade to the camera's basis: the eye, the view's unit axes, and `tan` of the half fields of
view. The slice's eight corners are computed in double as `eye + forward·z ± right·z·tanX ± up·z·tanY`. Everything
after the corners is unchanged: the sphere, the 1/16 m quantisation, the texel snap, the pull-back and the bias.

The inverse-view-projection fit remains for callers that hold only a matrix (the Shadow Lab and the tests). It is the
same function behind both.

## Consequences

- A cascade's size now depends only on the field of view and the split. The Strange Attractor's nearest cascade is
  112.00 m every frame, and its texel is fixed.
- Every scene with cascaded shadows gets the same stability. The effect is largest far from the origin with a deep far
  plane, and nothing is lost elsewhere: in exact arithmetic the corners are the ones the matrix path computes.
