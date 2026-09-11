# ADR-081: Stabilising cascaded shadows

Status: accepted
Date: 2026-09-10

## Context

Renderer 2.0 §21 reports shadow popping. The Phase 0 audit listed "whether shadow cascade splits
are stable, and the cause of the reported shadow popping" as *not yet audited* — so the first job
was to find out what the defect actually is, rather than to apply the usual remedies and hope.

There turned out to be two separate defects with two separate causes, and one of them was being
caused by the code written to prevent it.

## Defect 1: the texel snap snapped nothing

`fitDirectionalCascade` did this:

```cpp
const glm::vec3 eye = center - dir * (radius + back);
const glm::mat4 lightView = glm::lookAt(eye, center, stableUp(dir));

// Texel snapping in light space: the projection window moves in whole-texel steps only.
const float texel = 2.0f * radius / static_cast<float>(std::max(resolution, 1u));
glm::vec3 centerLs = glm::vec3(lightView * glm::vec4(center, 1.0f));
centerLs.x = std::floor(centerLs.x / texel) * texel;
centerLs.y = std::floor(centerLs.y / texel) * texel;
```

`glm::lookAt(eye, center, up)` sends `center` to the light-space origin **by construction**. So
`centerLs.xy` is exactly `(0, 0)` every frame, and flooring zero is zero. The comment describes a
stabilisation that the code does not perform.

It is worse than a no-op. Zero arrives carrying floating-point error of either sign, and
`std::floor` maps the two signs to `0` and `-texel`. The projection window therefore jumped a
**whole texel back and forth** depending on which way the rounding error happened to fall.

Measured, at 2048² with a 0.0276 m texel, dollying the camera in 1 mm steps:

| | mean texel drift/frame | median | frames the receiver stayed on its texel |
|---|---:|---:|---:|
| before | 0.684 | 0.982 | 4 / 239 |
| after | 0.042 | 0.0003 | 229 / 239 |

The "after" maximum is 1.0002 texels: it sits still, then steps by exactly one texel at a grid
boundary, which is the intended behaviour of a stabilised cascade.

### Why the existing test did not catch it

There was already a test called *"cascades are stabilised against camera motion"*, whose comment
read "translating by less than a texel must not move the projection at all". Its assertion was:

```cpp
CHECK(nudged.texelWorldSize == Approx(base.texelWorldSize).epsilon(1e-4));
```

`texelWorldSize` is `2 * radius / resolution`. It is constant by construction whether or not
snapping works, so the test passed against a snap that did nothing — and would have passed against
no snapping code at all. The test asserted texel *size* while its name and comment promised texel
*alignment*.

It now projects a fixed world point through the cascade and asserts that it stays on the same
shadow-map texel. That assertion fails against the old code (0 of 120 stationary frames) and passes
against the new one.

## Defect 2: cascades were switched, never blended

`cascadeFor` returns a hard index, and `shadowFactor` sampled that one cascade. Neighbouring
cascades differ in texel size, in the normal offset derived from that texel size, and in depth
bias, so the shadow term is discontinuous exactly at the split plane. That discontinuity is a seam
that sweeps across the ground as the camera dollies — the popping of §21.

## Decision

**Anchor the snap to a light basis at the world origin.** A rotation-only `lookAt(0, dir, up)` has
no degeneracy: `center` lands wherever the world puts it, flooring genuinely quantises, and the
snapped centre is transformed back to world space to build the actual light view. The orthographic
window then becomes symmetric about that snapped centre.

**Crossfade the last 12% of each cascade's depth extent into the next.** `shadowFactor`'s body was
split into `shadowVisibility(view, …)` so two cascades can be evaluated and mixed. The second
lookup is taken only inside the band, so pixels outside it pay nothing.

One WGSL trap worth recording: `select` evaluates both arms, so the near edge of cascade `index`
steps the index down through a guarded `select` rather than writing `splits[index - 1u]`, which at
`index == 0` would index a `vec4` at four billion.

## Consequences

- Shadow edges no longer crawl under camera translation.
- Cascade transitions fade rather than switch.
- Cost, measured on Glowmere at 1440×900 with 2 cascades, alternating the blend constant between
  0.12 and 0.0 in the same binary (shaders load from disk, so no rebuild intervenes) over three
  interleaved pairs of 150-frame runs:

  | | GPU frame | scene pass |
  |---|---:|---:|
  | blend on, best of 3 | 24.64 ms | 21.50 ms |
  | blend off, best of 3 | 24.12 ms | 21.04 ms |
  | **cost** | **+0.52 ms** | **+0.46 ms** |

  Best-of rather than median because the machine was not quiet; the blend was the slower arm in
  all three pairs, which is the part worth trusting. About 2% of the frame. The cost lands in the
  scene pass, not the shadow pass — it is a second lookup in the lighting shader, not extra
  rasterisation into the atlas. On the CPU it adds one `glm::inverse` of a rotation matrix per
  cascade per frame.
- `CASCADE_BLEND` is a shader constant, not a quality setting. If it needs to vary per tier it
  should move into `ShadowUniforms::info`.
- The blend does not apply to spot shadows, which have no cascades.

## The general lesson

Both defects were invisible because something claimed they were handled: a comment saying the
window "moves in whole-texel steps only", and a test named for stabilisation. A test that asserts
a quantity which is constant by construction proves nothing, however good its name. The rule this
adds to the Renderer 2.0 methodology: **before trusting a test that guards a defect class, break
the code deliberately and confirm the test fails.**
