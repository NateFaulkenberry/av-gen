# ADR-1540: The fisheye lens remaps the rectilinear frame

- Status: Accepted (art/rainbow-world)
- Amends the post chain's lens pass (`fs_lens`, stage 4). This change is additive. With `post/lens/fisheye` at 0,
  which is the default, nothing is encoded that was not encoded before. A model set with no amount leaves the frame
  byte-identical; the GPU test holds this.
- Code:
  - `src/scene/lens_projection.hpp` holds the reference mapping.
  - `shaders/post.wgsl` (`fisheyeUv`) transliterates it.
  - `src/rendering/post_processor.cpp` adds the lens-stage trigger and computes the source's half-angle tangents
    from the frame's inverse view-projection.
  - `src/scene/post_settings.{hpp,cpp}` add the parameters `post/lens/fisheye` and `post/lens/fisheyeModel`, and
    the scene `post` keys `fisheye` and `fisheyeModel`.
- Tests:
  - `tests/unit/test_lens_projection.cpp` (`[adr1540]`): identity when off; centre and corners fixed for every
    model; a monotonic radius, so the image never folds; the centre magnified and the edges compressed; a linear
    blend.
  - `tests/rendering/test_post_gpu.cpp` (`[adr1540]`): off renders byte-identical; on magnifies the centre for all
    three models.

## Problem

Rainbow World's reference animation is shot through a full-frame fisheye of roughly 160-190 degrees on the diagonal
(`docs/prototypes/rainbow-world/01-clip-analysis.md` section 3). AV Gen renders rectilinear only. Its existing lens
distortion is `uv * (1 + k r^2)` clamped to the frame. That polynomial bends lines but cannot reach a fisheye's
mapping, and the clamp smears the border.

Three routes were compared (`docs/prototypes/rainbow-world/04-production-plan.md` section 4):

| Route | Verdict |
|---|---|
| Multi-view (cube faces) | Reaches 180 degrees, at 2.5-5x the scene cost, and breaks every screen-space effect at the seams |
| Vertex-shader projection | Needs every pipeline changed and every large triangle tessellated |
| Remap of the rectilinear frame | Chosen |

E1 measured the remap against Cycles' true fisheye (`05-gate1-experiments.md`). A 140-degree rectilinear frame
remapped to stereographic is the same picture as a true 150-degree fisheye.

## Decision

The lens pass gives each output pixel the angle it stands for under a fisheye model, and samples the rectilinear
frame at `tan(theta)`.

**The model:**
- 0 is stereographic, `r = 2f tan(theta/2)`. It keeps small shapes round and is the default.
- 1 is equisolid, `r = 2f sin(theta/2)`.
- 2 is equidistant, `r = f theta`.

**The fit.** `f` is chosen so the output's half-diagonal lands on the source's half-diagonal angle. Every output
pixel then samples inside the source: there is no black and no clamp. The centre is magnified and the edges are
compressed. The field of view stays the camera's own: the camera's vertical FOV decides how wide the fisheye is.

**The blend.** `amount` mixes the sample position between the rectilinear frame (0) and the fisheye (1), so the lens
can be eased in or keyed.

**Pass order.** The remap runs where the lens always ran: after the depth-reading effects (outline, atmospheric
look, defocus, motion blur), which read undistorted depth, and before bloom, which then follows the bent image.

The source's half-angle tangents are measured from the frame's inverse view-projection, not passed separately, so
they follow any camera, lens or roll.

## Consequences

- **Softness at the centre.** The centre is magnified, by about 1.15x from a 120-degree source and 1.5x from a
  140-degree one (E1). A scene that wants a sharp centre renders at a higher internal scale.
- **Culling.** Visibility uses the rectilinear frustum the frame was rendered with. That is exactly the right
  frustum, since nothing outside it is sampled.
- **No change to existing scenes.** No shipped scene sets `fisheye`, so no shipped frame changes.
