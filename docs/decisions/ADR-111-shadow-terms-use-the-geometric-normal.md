# ADR-111: shadow terms are biased along the geometric normal

Status: accepted

## Context

ADR-087 moved the directional shadow-map term out of the lit pass into a half-resolution
screen-space pass (`shaders/shadow_mask.wgsl`). That pass runs *before* the scene pass, over the
linear depth the prepass resolved, so the only normal it can have is one it reconstructs from the
depth buffer: a geometric normal, and an approximate one.

The lit pass handed `shadowFactor` a different vector. `ShadeContext::normal` is the *shading*
normal -- the interpolated vertex normal after the material program's perturbation (ADR-036) and
after the material's tangent-space normal map. Every shadow term is biased along whichever normal it
is given:

* the normal offset, `normal * (texelWorld * (1 + 2 * (1 - nDotL)) * 1.4 + light.shadowBias)`,
* the slope-scaled constant bias, `tan(acos(nDotL))`,
* the contact march's ray origin, `worldPos + normal * stepSize * 0.5`.

So a normal-mapped surface got one shadow with the mask on and a different one with it off. The
brief that opened this work measured 5.6% of pixels differing against the full-resolution path and
named this as the cause.

## Decision

`ShadeContext` carries a second normal, `geoNormal`: the interpolated vertex normal, front-facing
corrected, captured in `shadeSurface` before anything perturbs it. Every shadow term -- the cascade
lookup and the contact march both -- is biased along `geoNormal`. The BRDF keeps using
`ShadeContext::normal`, unchanged.

This is not merely "agree with the mask". It is the correct normal on its own terms:

* A shadow map holds the depth of *rasterised geometry*. A normal map does not displace geometry, so
  the surface the shadow map recorded is the geometric one. The normal offset exists to move a
  sample point off that surface; offsetting along a normal-mapped normal slides the lookup sideways
  across the surface by an amount that tracks the texture, which is acne in the troughs and
  detachment on the peaks. Holbert's normal-offset shadows (2011) uses the vertex normal for this
  reason.
* The contact march reads the *depth buffer*, which holds the same geometry. A march started along a
  normal-mapped normal starts somewhere that surface is not, and the depth buffer immediately reads
  as an occluder in front of it. That is not a subtle error: see the measurement below.

Nothing real is lost. Normal-mapped self-shadowing was never being produced -- a shadow-map texel is
orders of magnitude larger than a normal-map bump, and the shading normal only moved the bias.

`evaluateLight` reads the vector through a guard (`dot(geoNormal, geoNormal) > 0.5`, falling back to
the shading normal), because a WGSL `var ctx: ShadeContext;` is zero-initialised and a future
construction site that forgot the field would otherwise bias along the zero vector -- an offset of
nothing and a slope scale pinned at its maximum, presenting as a scene-wide bias bug with no obvious
cause.

## What it fixed, measured

`tests/rendering/test_shadow_normals_gpu.cpp`, 256x256, a 400 m normal-mapped floor and a box, the
same frame rendered with `PassToggles::shadowMask` off (the full-resolution reference) and on, and
the fraction of pixels whose luminance differs by more than 2/255.

| case | before | after | flat-normal-map control (before / after) |
| --- | --- | --- | --- |
| normal-mapped floor, light 13 degrees up | 0.94% | **0.71%** | 1.04% / 1.04% |
| grazing light, 8 degrees up | 2.15% | **0.91%** | 1.33% / 1.33% |
| cascade transition, raking light | 1.29% | **0.94%** | 1.18% / 1.17% |
| camera dolly, worst of three positions | 2.28% | **0.97%** | -- |
| contact march, share of frame moved | 3.34% | **0.085%** | 0.148% / 0.148% |

The flat-normal-map controls are the point of the table. They do not move -- the contact one is
identical to six figures -- so the change provably does nothing where there is no normal map to get
wrong, and the improvements above are attributable to the normal and to nothing else. Before the
fix, a normal-mapped floor disagreed with the reference *more* than an identical flat one in every
case; after, it disagrees less.

The contact row is the largest defect this found and it was not the one being looked for: on a
normal-mapped floor the march was manufacturing 3.34% of a frame's worth of "contact shadow" out of
the normal map, more than twenty times what the identical flat floor produces, none of it cast by
anything.

## What it did not fix, and what the residual actually is

**The brief's hypothesis does not hold for the renderer's own content, and that is the more
important finding here.** On the "Glowmere Valley" example at 960x540, realtime tier, frame 8:

| | mask vs. no mask |
| --- | --- |
| before ADR-111 | 4.884% |
| after ADR-111 | 4.883% |

The whole frame moved 0.018%. Glowmere's materials are procedural programs and foliage cards; almost
nothing in it carries a normal *texture*, so there was almost no wrong normal to correct. The ~5%
residual on that scene is caused by something else, and it was decomposed rather than assumed
(same scene, same frame, each arm a single change):

| arm | residual | attributable |
| --- | --- | --- |
| shipping: half-resolution mask, PCSS | 4.88% | -- |
| mask forced to full resolution | 2.75% | **2.13pp: half-resolution sampling and the bilateral upsample** |
| full resolution, PCF instead of PCSS | 1.22% | **1.53pp: PCSS** |
| full resolution, normal removed from the bias entirely | 2.96% | **0.0pp: the normal** |

The last row is the decisive one. Zeroing both the normal offset and the slope-scaled bias -- which
removes the normal from the shadow term in *both* paths, so the two can no longer disagree about it
-- left the full-resolution residual at 2.96%, no better than the 2.75% it started at. On this
content the normal is not what the mask and the lit pass disagree about.

What they disagree about is **position**. The lit pass has the fragment's own interpolated world
position; the mask reconstructs a world position from the linear-depth texel and the camera basis.
The two differ by the depth buffer's quantisation and the reconstruction's float error, which on
ground running away from the camera is several centimetres along the view ray. PCSS then amplifies
it: its blocker search is `textureLoad` at uninterpolated coordinates, so a centimetre can flip a
blocker in or out of the search, which changes the average blocker depth, which changes the filter
radius, which changes the whole penumbra. That is the 1.53pp in the table, and it is why the
half-resolution cost (2.13pp) and the PCSS cost (1.53pp) are each larger than everything the normal
was ever responsible for on this scene.

Closing the position gap would mean the mask consuming an exact world position rather than
reconstructing one, which means the depth prepass writing a position or normal target. The prepass
is currently depth-only with no fragment work; giving it an attachment is a real cost against a
residual that is invisible at a glance (the mean absolute luminance difference over the Glowmere
frame is 0.42/255). **Not done, and deliberately not done.** It is recorded here so the next person
does not re-derive the decomposition.

Two further notes, found and not fixed:

* `shaders/water.wgsl` calls `shadowFactor` with the *wave* normal, which is the same class of error
  this ADR removes from the lit pass. Water does not go through `evaluateLight` and is not masked,
  so it is not part of the mask-agreement problem; changing it risks acne on the water surface and
  needs its own measurement.
* The claim in `shadow_mask.wgsl` that "target 1 already carries an octahedral normal" is true of
  the scene pass's output and irrelevant to the mask, which runs before it. The comment in that file
  is correct; the brief's reading of it was not.

## Consequences

* Normal-mapped surfaces now get the same shadow whether or not the mask is on, and the contact
  march no longer manufactures shadow from a normal map.
* The residual against the full-resolution path is unchanged on content without normal maps, and it
  is now documented as being the mask's *position* reconstruction and PCSS's sensitivity to it,
  not its normal.
* Anything that constructs a `ShadeContext` must fill `geoNormal`. The guard in `evaluateLight`
  makes forgetting it a quality regression rather than a black frame, which is the right failure
  mode but is not a substitute for filling it.
