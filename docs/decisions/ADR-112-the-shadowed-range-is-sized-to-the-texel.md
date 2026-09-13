# ADR-112: the shadowed range is sized to the texel it can afford

Status: accepted

## Context

`ShadowRenderer::update` used to choose how far the directional cascades reach like this:

```cpp
shadowFar = clamp(max(sceneRadius * 3, near * 20), near * 4, cameraFar);
```

Three scene radii. That is the *world's* size, and the world's size is not what the camera needs
shadowed. A landscape 2.4 km across asked for 3 km of shadow range, `fitDirectionalCascade` fitted
the last cascade to a bounding sphere most of that across, and the texel came out **2.36 metres**.
Every prop in the scene smaller than a garden shed was a fraction of a texel, so it cast nothing --
and in a scene made of rocks, plants and fence posts, that is almost everything.

Measured (`tests/rendering/test_shadow_wide_scene_gpu.cpp`): the identical camera, props and light
over ground planes of different extents, counting the share of ground the cast shadows darken.

| ground extent | range | coarsest texel | near ground darkened |
| --- | --- | --- | --- |
| 25 m | 62 m | 4.7 cm | 5.13% |
| 120 m | 300 m | 23.2 cm | 5.27% |
| 400 m | 1000 m | 78.1 cm | 4.53% |
| 1200 m | 3000 m | 236 cm | 1.81% |

Nothing in the frame changed between those rows except how far the ground plane extended off-screen.

## What was evaluated

The brief asked for practical/logarithmic splits, receiver-focused fitting, camera-relative fitting
and cascade resolution distribution to be evaluated before anything was implemented. Each was, and
three of the four are rejected on a measurement.

**Split scheme (lambda).** Already the practical scheme (Zhang et al. 2006) at lambda 0.85, and the
lambda is genuinely mis-serving the *near* cascades in a wide world: at a 1 km range the uniform
term, weighted 0.15, puts cascade 0's far plane at 55 m where the pure-log split would put it at
6 m, an eleven-fold texel inflation on the cascade the viewer looks at most. But it does **nothing**
for the coarsest cascade, which is what decides whether small objects cast at all. Measured over
lambda from 0.5 to 1.0 at a fixed range, the coarsest texel moves by under 15%
(`tests/unit/test_shadow_cascades.cpp`). Not implemented, because once the range is capped the
lambda's damage is capped with it and changing a stable, shipped split scheme for a second-order
effect is not a trade worth making. Recorded here so it is not re-derived.

**Cascade count.** Same answer, same measurement: two, three and four cascades produce the same
coarsest texel to within 15%. Four cascades cost four depth passes and four atlas layers and buy
nothing at the far end. The reason is geometric and worth stating plainly: **a camera frustum widens
linearly with distance**, so the last cascade's bounding sphere is proportional to how far it
reaches no matter how the range is divided up. Splitting redistributes texels among the near
cascades; it cannot make the far one smaller.

**Receiver-focused fitting** -- fitting each cascade to the depth range receivers actually occupy
rather than to the whole sub-frustum. Rejected on the case at hand: in the scene this ADR exists to
fix, the receiver *is* the ground, and the ground fills the frustum from the near plane to the
horizon. There is nothing to focus onto. It would pay in an interior or a scene of isolated objects
against sky, neither of which has this problem.

**Camera-relative fitting** -- building the light matrices around the camera's position rather than
the world origin. That is a *precision* technique, for worlds large enough that world-space float
coordinates lose bits. It does not change a texel's world size by one part in a thousand. Rejected:
it addresses a different failure (jitter far from the origin) that this scene does not exhibit.

**Cascade resolution distribution** -- sub-dividing the atlas so the far cascade gets more texels
than the near one. This one is real, and it is the only rejected option with a measurable benefit:
with a log split, cascade 0's texel is around fifty times finer than the last cascade's, which is
resolution spent where nobody can see it. Redistributing would buy the far cascade a factor of two
or so. Rejected for this change on cost-benefit: it means an atlas allocator, per-view viewport
rects, and per-view texel sizes threaded through the uniforms and the PCF radius, for a 2x against a
30x problem, and it can be done later on top of what is here. **A factor of two is also exactly what
raising the resolution buys, which the brief rules out, and redistribution is the same trade paid
for differently.**

**Shortening the range.** The remaining lever, and the only one that moves the coarsest texel
directly, because the texel is `2 * k * range / resolution` with k around 1.06 at a 16:9 frame
whatever the split scheme. Implemented.

## Decision

`rendering::directionalShadowRange` takes the old rule -- three scene radii, clamped to the camera's
planes -- and then shortens it until the coarsest cascade's texel is no larger than
`QualitySettings::shadowTexelTarget`, defaulting to 8 cm:

```
range = max( min(world, texelTarget * resolution / 2.12), min(world, cameraNear * 20) )
```

Three things about that expression are load-bearing.

**It only ever shortens.** A room-sized scene already asks for less than the rule allows, and the
outer `min` is what keeps it byte-identical -- pinned by a test that renders a 25 m world both ways
and requires equality, not similarity. Everything that worked before this ADR keeps working.

The floor is capped at `world` for the same reason, and that is not decoration: `world` is already
clamped to the camera's far plane, and a bare `max(..., near * 20)` hands back a range *past* that
plane for a camera whose far plane is closer than twenty near planes -- a macro shot, or anything
with a deliberately shallow depth range. The rule would then be lengthening the range, which is the
one thing it must never do. Found by writing the test, not by reading the code.

**The resolution in it is a constant, not the tier's.** `kShadowRangeReference` is 2048 whatever map
the tier actually renders. Where the shadows stop is *composition*: a preview whose shadows ended at
40 m and a final whose shadows ended at 150 m would not be the same shot, and a preview that does
not predict the final is worse than no preview. So every tier gets the same range and they differ
only in how sharp it is -- preview's 1024 gets twice the target texel, offline's 4096 gets half.
This is the one place a tier is deliberately not allowed to scale something.

**The 2.12 was measured, not derived.** It is `2k` for the real `fitDirectionalCascade`, sampled
over ranges from 50 m to 1 km and resolutions from 1024 to 4096, where k lands between 1.049 and
1.070 at 16:9. It varies with the frame's aspect -- 0.80 at 1:1, about 1.3 at 2.39:1 -- and the
aspect is not passed in, so the realised texel is within about 25% of the target across every aspect
anyone shoots at. That is the accuracy the rule needs: it is choosing how far the shadows reach, not
calibrating an instrument. Both the constant and the aspect spread are pinned by unit tests that
measure the realised texel through the fit rather than through the approximation.

Read the setting as *how small a thing may be and still cast*. A caster has to be a few texels
across to survive the PCF filter, so 8 cm is about a 30 cm object at the far end of the range.

### The fade

Past the last cascade a fragment reads as fully lit, because `shadowLookup` reports the lookup
invalid and the shading pass treats that as unshadowed. That has always been a step and has always
been there; it was invisible only because three scene radii usually put it behind the far plane.
Shortening the range moves it into the picture, so `shadows.wgsl` now eases the shadow term out over
the last 18% of the range (`SHADOW_RANGE_FADE`), squared so the fade begins imperceptibly and
finishes quickly -- a linear ramp reads as fog that only shadows have.

## Results

`tests/rendering/test_shadow_wide_scene_gpu.cpp`, 320x320, a field of 70 cm props on ground planes of
several extents, contact shadows and ambient occlusion off so the map term is alone in the frame.

| | before | after |
| --- | --- | --- |
| 1200 m world: shadowed range | 3000 m | 77 m |
| 1200 m world: coarsest texel | 236 cm | 5.9 cm |
| 1200 m world: near ground darkened | 1.81% | **5.30%** |
| 1200 m world: far ground darkened | 0.21% | **0.58%** |
| 400 m world: near ground darkened | 4.53% | **5.30%** |
| 25 m world: everything | unchanged | unchanged, exactly |

The sharpest statement is the spread rather than any single number. Over 120 m, 400 m and 1200 m
worlds -- identical camera, identical props, identical light, only the ground plane's extent
differing -- the share of near ground the shadows darken was 5.27% / 4.53% / 1.81% before, a factor
of nearly three, and is 5.30% / 5.30% / 5.30% after. **The cascade fit no longer depends on how wide
the world is**, which is the actual defect and is asserted as such.

On real content, the "Glowmere Valley" example at 960x540 moves 12.3% of its pixels, mean absolute
luminance difference 1.2/255. Inspected rather than trusted: the change adds shadow detail on the
mid-ground ridges and *removes* a patch of texel-grid shadow acne on the near hillside that the
2.36 m cascade had been generating. Crops of the hillside before and after are hard to tell apart;
nothing visible is lost, and no boundary is visible where the range now ends.

## Consequences and costs

* **Shadows now end.** In a world larger than about 80 m across, geometry past the range is
  unshadowed where before it had a shadow made of metre-wide texels. The fade makes the transition a
  gradient rather than a line, but it is a real change to what the far distance looks like, and a
  scene that wants long-range terrain self-shadowing must raise `shadowTexelTarget` (0.25 buys
  240 m, at a 24 cm texel). This is the trade the ADR makes deliberately: sharp near and nothing far,
  rather than mush everywhere.
* **It is faster, measured, which was not the point but is worth recording.** Glowmere
  (`examples/world/glowmere-stylized.scene.json`) at 1280x800, the protocol in
  `docs/renderer-upgrade/01-audit-and-baseline.md` -- 120 frames, first 12 discarded, GPU timestamp
  queries, three runs per arm:

  | | rule off | rule on |
  | --- | --- | --- |
  | GPU frame median | 18.68 / 18.48 / 18.81 ms | 18.02 / 17.76 / 17.89 ms |
  | `scene` pass | 15.53 / 15.40 / 15.66 | 14.81 / 14.55 / 14.68 |
  | `shadow` pass | 0.66 / 0.66 / 0.66 | 0.52 / 0.52 / 0.59 |

  18.68 -> 17.89 ms, **-4.2%**, against a within-arm spread of 1.8% and 1.5% -- over the 2% bar the
  protocol sets. Most of it is the *scene* pass, not the shadow pass: with a shorter range, a
  distant fragment falls outside the last cascade, `shadowLookup` reports the lookup invalid, and
  the PCSS blocker search never runs. The shadow passes themselves save about 0.1 ms because
  tighter cascade frusta cull more casters.

  The two arms were run in blocks rather than interleaved run-by-run, because switching arms needs a
  rebuild. The within-arm spreads are smaller than the gap, but this is weaker evidence than a
  properly interleaved A/B and is labelled as such.
* No cost in memory, passes, or draws. The shadow passes draw the same geometry; only the matrices
  differ. The range is computed once per frame from four scalars.
* The finite-value guard in `ShadowRenderer::update` is untouched and still refuses a non-finite
  cascade. Its premise -- that a bad frustum still yields a non-finite matrix rather than a silently
  zeroed one -- is now pinned by a unit test rather than assumed.
* Texel snapping (ADR-081) is untouched in code but its inputs changed, since a different range means
  different splits and a different texel size. It is re-pinned by a test that slides the camera in
  fractional-texel steps and requires a fixed world point to move through the shadow map in whole
  texels, with a control that it moves at all.

## Not fixed

* Cascade resolution distribution, above: a further factor of about two on the coarsest texel, at the
  cost of an atlas allocator. The right next step if this is not enough.
* The near cascades still pay the practical split's uniform term. With the range capped the damage
  is bounded, but at a 77 m range and lambda 0.85 cascade 0 still reaches 9 m where a pure-log split
  would put it at 3 m. Worth revisiting with a measurement of shadow *sharpness* rather than
  coverage, which is not what the tests here measure.
* `shadowTexelTarget` is a length in metres, so it assumes a world authored at human scale. A scene
  composed at 1:100 would want a different number. Deriving it from the scene's own caster sizes was
  considered and not attempted: a world of one large terrain mesh and many small props has no single
  representative size, and picking the small one would delete the terrain's shadow.
