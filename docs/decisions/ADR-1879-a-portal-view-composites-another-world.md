# ADR-1879: A portal view composites another world through the corruption's opening

- Status: Proposed (Gate B, dimensional portals), scene/strange-attractor.
- Code:
  - Scene side:
    - `Scene::portal` (`world`, `crossed`) in `src/scene/scene.hpp`.
    - The `"portal"` block (`world`, `exit`, `exitYaw`, `flight`) in `Composition` (`src/scene/composition_portal.cpp`).
      It loads the destination scene file as a second composition, attaches it under the prefix `moon/`, drives its
      camera and updates it after this world's camera is final.
    - `Composition::setCameraOverride`: a composition placed by another.
  - Renderer side:
    - `SceneRenderer::encodePortalView`: a second `SceneRenderer`, created on first use, renders the destination into
      its own target at the start of the frame, on the same encoder.
    - The `portal-view` pass (`shaders/portal.wgsl`).
- Tests:
  - `tests/unit/test_lunar_world.cpp` `[portal][adr1879]`: the composition loads the world, maps the camera through
    the transform, and hands the renderer its view.
  - `tests/rendering/test_corruption_gpu.cpp` `[gpu][portal][adr1879]`: the other world shows inside the opening and
    this one outside it, and the other world fills the frame once crossed.

## Context

The owner's brief asks for the portal to reveal the actual destination with real perspective, and for the camera to
pass through without a cut or a fade. Gate R's proof 02 validated the method offline: a per-pixel ray-plane aperture
against the depth, over a render of the destination from the camera carried through the portal.

## Decision

**Worlds.**
- A world is an ordinary scene file.
- The destination is drawn by its own renderer, so its meshes, shadow maps, sky and temporal state never churn.
- A portal view does not draw portals, so there is no recursion.

**The transform.** The canyon point `p` maps to `R_yaw(p - focus) + exit`, where `focus` is the corruption's focus.
Directions turn by `R_yaw`. The exit's height is measured above the destination's ground when it is a `lunarWorld`.

**The pass.**
- It runs after the opaque scene (and the sky and atmosphere), and before the water and the refraction copy, so the
  water and glossy reflections read the other world.
- It is a full-screen triangle. For each pixel:
  - the ray from the camera (ADR-1807's basis) meets the entry plane (the focus, the corruption's axis);
  - inside the corruption's aperture, which is exactly the shape the rock opens to, it writes the destination's scene
    radiance. That is its HDR before post, so bloom and the tonemap run once, on the composite.
- Depth:
  - The pass writes the plane's depth, pulled 25 m toward the eye, so the wall's own relief around the opening does
    not cover it while rock genuinely in front still does.
  - The emission target is cleared there: this world's glow must not bloom over the other world.
- A lens near the rim bends the far side slightly toward the middle.
- **Crossed:** the pass writes the destination everywhere at depth 0, and the medium is skipped. The frame is the
  other world's.

## Consequences

- With no portal, nothing is created or drawn.
- On portal frames the destination's full frame is rendered. That is about one more scene render (offline; live is
  not targeted in this pass).
- Seen through the portal, the destination's own fog and sky are its own, and the canyon's surface fog applies over
  the plane's distance.
- After the crossing, this world still renders under the composite. It is wasted work, but it keeps both worlds'
  state continuous.
