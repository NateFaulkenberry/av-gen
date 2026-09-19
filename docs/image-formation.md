# Image formation

Decisions: ADR-037 (physical camera, exposure and camera behaviours), ADR-039 (image formation,
selective post and tone mapping), ADR-016 (post-processing). Research:
`docs/research/cinematic-camera-and-color.md`, `docs/research/render-passes-and-post.md`.

This is the path from what the renderer shaded to what a viewer sees. Everything before the tone
map is scene-linear HDR; everything after it is display-encoded sRGB.

The full colour-space map -- every boundary named, with the file and line that decides it -- is
`docs/image-look-audit.md` 2. Three things from it are worth having here. Primaries are Rec.709
throughout and are nowhere declared. The sRGB OETF is applied **exactly once**, in `linearToSrgb`:
no render target in this engine is an `*Srgb` format, so there is no hardware encode to double it.
And **vignette and grain are applied in display-linear, after the curve and before the OETF**, which
is why grain reads coarser in the highlights than in the shadows.

## The order

The order is fixed. It is not a matter of taste: each stage assumes the ones before it have already
run, and the two moves that matter — exposure before bloom, and depth-aware effects before lens
distortion — are what stop the image being uniformly glowy and geometrically inconsistent.

```
scene shading (HDR RGBA16Float)          rendering::SceneRenderer, shaders/pbr*.wgsl
volumetric atmosphere                    rendering::VolumeRenderer (ADR-032)
user post layers                         shaders::LayerStage::Post
--- rendering::PostProcessor, shaders/post.wgsl -----------------------------------
 1. metering            fs_meter_prefilter, fs_meter_reduce   (automatic exposure only)
 2. exposure            fs_exposure     (skipped when the scale is within 1e-3 of 1 -- a
                        deadband, not an equality: a settling automatic exposure would
                        otherwise encode a full-resolution pass forever to multiply by
                        1.0000001)
 2b. atmospheric        fs_look_atmos   (Image/Look 68.1; NOT encoded unless
                        post/look/atmospheric is non-zero. Here because it reads depth)
 3. defocus             fs_dof          depth of field AND the ADR-079 tilt-shift band, in
                        one shared gather -- they differ only in how the circle of
                        confusion is decided and the shader takes the larger of the two,
                        so this runs when EITHER is on. Needs undistorted depth
 4. motion blur         fs_velocity_tile_max, fs_velocity_neighbour_max, fs_motion_blur
                        (reads the ADR-035 velocity target and undistorted depth)
 5. lens                fs_lens        distortion, chromatic aberration
 6. bloom               fs_prefilter, fs_downsample, fs_upsample
 7. halation/anamorphic fs_halation_prefilter, ..., fs_wide
 8. composite           fs_composite   bloom + wide tier mixed in, then the colour grade.
                        ALWAYS RUNS -- it is what carries the grade
 8b. colour / local contrast / light wrap
                        fs_downsample, fs_look_blur x2, fs_look   (Image/Look 68.2-68.4;
                        NOT encoded unless one of those three amounts is non-zero)
 9. antialias           fs_fxaa        (ADR-059) before sharpening, because sharpening an
                        aliased edge fixes the contrast and keeps the stair step
10. sharpen             fs_sharpen
--- shaders/tonemap.wgsl ------------------------------------------------------------
11. tone map            AgX by default; ACES, Reinhard, PBR Neutral, clamp
12. output              vignette, film grain, sRGB encode
```

The FXAA row was missing from this table until 2026-09-19. It had been in the chain since ADR-059
and in `post_processor.hpp`'s own header comment; only this document omitted it. That is the drift
`docs/image-look-audit.md` 1.1 exists to catch, and it is not cosmetic -- FXAA is a spatial filter
on scene-linear HDR immediately before the tone curve, so anything added near the end of the chain
has to decide which side of it to sit on.

Passes that are not needed are not encoded: with everything off and a unit exposure scale, the
chain is one pass (the composite, which always runs because it carries the grade).

That one pass is **not** the identity, and it is worth being exact because a disabled-path guarantee
gets quoted. At default grade settings `fs_composite` still evaluates `pow(x, 1.0)` twice -- the
log-space contrast and the gamma -- which lowers to `exp2(log2(x))` and is off by an ulp or two; it
clamps every channel up to `1e-5`, so a true black pixel does not leave the composite black; and its
divide and multiply by 0.18 do not cancel in binary floating point. `post_processor.hpp`'s summary,
"the input is returned unchanged", overstates this. A feature that must not change the image
therefore has to prove it **differentially** -- against the same build without the feature -- rather
than against the scene HDR, which is a comparison that could never pass. See ADR-368.

Two notes on where the boundaries fall:

- **Vignette and grain live in the tone-map pass**, not the post chain, because they are display
  effects and the tone-map pass is the one that writes the final target. Sharpening is the odd one
  out: ADR-039 lists it with the output effects, but it runs at the end of the post chain, in
  scene-linear, because the tone-map pass belongs to the renderer rather than the post chain. In
  practice a sharpen that is clamped to its own neighbourhood (as this one is) behaves the same
  either side of the curve.
- **Depth of field is a lens effect** and ADR-039's list puts lens effects after motion blur, but it
  has to run before distortion and chromatic aberration: both of those resample the image, and the
  depth buffer is not resampled with it. So the depth-aware stages come first and the geometric lens
  stages after.

## Why exposure comes first

The chain used to hand raw HDR values straight to the tone curve, and bloom thresholded raw
luminance. That has two consequences, both of which the Hyperspace showcase demonstrated: when the
camera flew into the core the frame clipped to solid white, and because *everything* in the scene
was above a threshold expressed in raw scene units, every surface bloomed equally, so nothing read
as brighter than anything else.

With exposure first, `post/bloom/threshold` is a number in *exposed* units — "one unit above what
the camera is currently calling mid grey" — and it means the same thing whether the camera is
looking at a dim corridor or a blazing core.

## Exposure (ADR-037)

`camera/exposure/*`, `scene::ExposureSettings`, maths in `src/scene/camera.cpp`.

Two modes:

**Manual.** The photographic triangle: aperture (f-number *N*), shutter (*t* seconds) and
sensitivity (ISO *S*) give

```
EV100 = log2(N^2 / t) - log2(S / 100)
```

and the photometric scale for an EV is `1 / (1.2 * 2^EV100)` (Lagarde and de Rousiers, *Moving
Frostbite to PBR*, SIGGRAPH 2014). Both are available exactly as written (`exposureValue100`,
`exposureScaleFromEv100`).

The scale the chain *applies* is that value normalised by the scale at `referenceEv100`:

```
appliedScale = 2^(referenceEv100 - EV100 + compensation)
```

The normalisation exists because this engine's scene-linear unit is not calibrated in nits — an
emissive of 30 means "30", not "30 cd/m²" — so an absolute photometric scale would darken every
existing scene by a factor of about 470. `referenceEv100` defaults to 8.61471, which is exactly
EV100(f/5.6, 1/50 s, ISO 400), the default aperture, shutter and ISO. So **the defaults are a
no-op**: a scene that never mentions exposure renders exactly as it did before ADR-037, and one stop
of aperture, shutter or ISO is still one stop.

**Automatic.** The chain reduces the *pre-exposure* image to a single centre-weighted average
luminance (six passes: a quarter-resolution prefilter, then 4x4 box reductions to 1x1), copies that
texel into a readback buffer, and maps it synchronously at the start of the *next* frame. The EV
that would place that luminance on mid grey is

```
targetEv100 = referenceEv100 + log2(L / 0.18)
```

clamped to `[minEv, maxEv]`, and the state walks toward it at `speedUp` EV per second when the image
is brightening and `speedDown` when it is darkening.

The average is arithmetic, not a log-average. A geometric mean is the more usual choice and is more
perceptual, but on this engine's material — a mostly-black frame with a blazing centre — the empty
background dominates the pixel count and a log-average would tell the camera to *brighten*. ADR-037
allows either.

**Determinism.** The metering loop is part of render state. Frame *N* uses frame *N-1*'s
measurement, read back with a blocking map so there is no timing race, and the update is a pure
function of `(previous EV, measured luminance, delta time, settings)`. Two offline renders of the
same project produce bit-identical frames; `tests/rendering/test_image_formation_gpu.cpp` asserts
that over 40 frames. The first frame of an automatic run has nothing to meter, and it seeds from the
*manual* EV rather than from black, so a render never opens on a flash. `Engine::resetCameraState()`
(called on a scene swap) re-seeds it.

**Centre weighting.** `meterCenterWeight` is 0 for a flat average and 1 for a strongly
centre-weighted one; the weight falls smoothly from 1 in the middle to `1 - meterCenterWeight` at
the frame edges, aspect-corrected.

### The idiom worth knowing: auto-exposure as a highlight guard

Setting `camera/exposure/minEv` to `referenceEv100` (8.61471) means the exposure can only ever
*darken*. The scene keeps the brightness it was authored with, and the camera only steps in when a
shot would otherwise clip. That is what the Hyperspace example does, and it is almost always what
you want for an authored piece: a full auto-exposure normalises every frame to mid grey, which
destroys deliberate darkness as surely as it fixes deliberate blow-out.

## The lens (ADR-037)

`camera/lens/*`, `scene::LensSettings`.

- **Field of view** comes from `fovY = 2 atan(sensorHeight / (2 f))` — a 24 mm lens on a 36x24 mm
  full-frame sensor is a 53-degree establisher, 35 mm is 38 degrees, 85 mm is a 16-degree portrait.
  It is only used when `camera/lens/useExplicitFov` is turned **off**; it defaults to on, so every
  scene that sets `camera/fov` keeps working untouched. There are deliberately two ways to express
  the angle and the precedence rule is that one flag.
- **Circle of confusion**: for a lens of focal length *f* at f-number *N* focused at *s*, a point at
  *d* projects a blur circle of diameter

  ```
  c = f^2 |d - s| / (N d (s - f))
  ```

  on the sensor (Potmesil and Chakravarty 1981; Pharr, Jakob and Humphreys, *Physically Based
  Rendering*, 3rd ed., 6.2.3), in whatever unit *f*, *d* and *s* share. `circleOfConfusion` returns
  millimetres; `circleOfConfusionPixels` scales by `imageHeight / sensorHeight`.

  (The research note transposes *d* and *s* in the denominator; the form above is the standard one
  and is what the code and the tests use.)
- **Depth of field**: with `post/dof/physical` on, `fs_dof`'s gather radius is *c*/2 in pixels
  evaluated per tap, and `post/dof/maxRadius` demotes itself to a safety clamp. With it off, the
  original `focusDistance`/`focusRange`/`maxRadius` behaviour is unchanged, so existing scenes and
  the existing tests are untouched.
- **Shutter** feeds motion blur: the blur length is the pixel's screen motion times
  `post/motionBlur/amount` times `shutterAngle / 360`, so 1.0 with a 180-degree shutter is the
  physically correct half-frame smear and a zero shutter angle is exactly no blur. See
  [Motion blur](#motion-blur) below.

### Focus tracking

`camera/focus/*`:

| mode | target |
|---|---|
| `0` fixed | `camera/lens/focusDistance` |
| `1` point | the distance from the camera to `camera/focus/point` |
| `2` focal | the distance to a composition focal point (ADR-038) |

In focal mode the point is chosen by name (`FocusSettings::name`), else by the composition's own
`cameraTarget`, else the heaviest focal point; a scene with no composition falls back to the fixed
distance, so nothing breaks. `camera/focus/speed` is metres per second — 0 (the default) is instant,
and the first frame always snaps rather than racking in from wherever the tracker happened to be.

## Bloom (ADR-039)

`post/bloom/*`. Prefilter, a 13-tap downsample chain (Jimenez, *Next Generation Post Processing in
Call of Duty: Advanced Warfare*, SIGGRAPH 2014), and a 9-tap tent upsample chain.

Two things changed from the pre-ADR-039 version:

- **The threshold is a soft knee on luminance**, and its weight is the *fraction of the pixel's
  energy that passes*, not a hard subtraction. At threshold 0 the prefilter is exactly the identity.
- **The upsample blends instead of adding.** Each step is `mix(fineLevel, tent(coarseLevel), blend)`
  with `blend = bloomRadius / 2`. Because the tent's weights sum to 1 and the blend is a convex
  combination, the pyramid's mean equals the prefiltered image's mean *whatever the level count*.
  The old chain added the levels, which multiplied a highlight's energy by six, which is why
  everything glowed and why a bloom "radius" also changed the brightness.

  The measurable consequence: with threshold 0 and intensity *I*, the total energy of the frame
  after bloom is (1 + *I*) times what it was before, within about 10 % (border clamping and half
  precision), and changing the level count from 6 to 3 does not change it. Both are asserted in
  `tests/rendering/test_image_formation_gpu.cpp`.

Because the upsample no longer multiplies, the same visual weight needs a larger
`post/bloom/intensity` than it did — roughly double. The shipped examples were re-tuned for this.

**The threshold is on luminance**, and the tone curve compresses each channel on its own, so the two
disagree about what "bright" means by the ratio of the luminance weights. Measured (HDR Lab §5.1):
against a neutral highlight, a pure blue needs **13.93x** the radiance to cross the same threshold
and a violet 5.79x -- which is `1 / luminance` in both cases, to within 1%. A neutral highlight
starts to bloom at scene-linear 0.55, before it reaches 204 on screen; a pure blue one does not
start until 7.6, by which point its blue channel is at the top of the display range. That is the
technique as written, not a fault, and it is why `post/output/chromaRetention` exists -- but it is
worth knowing before tuning an emissive palette, and it is the reason two species of the
bioluminescence ladder at the same authored intensity can sit 4.6 stops apart in the bright pass.

**Selective bloom** (`post/bloom/emissionWeight`, 0..1) weights the prefilter by the emission target
from ADR-035, so a lit-but-not-emissive highlight stops glowing like a light source. At 0 the
prefilter is the plain threshold; at 1 a pixel's bloom weight is scaled by how much of its radiance
is actually *emitted*.

The emission target is written by the scene pass, which runs **before** exposure, and the bloom's
source is the scene after it — so the prefilter is handed the exposure scale and brings the two into
the same space before taking their ratio. Without that the mask is off by the exposure factor, and
at ev-2 it suppresses the glow on the very lights it exists to keep.

An emitter keeps most of its glow rather than all of it, which is the technique and not a fault: the
prefilter's four-tap box mixes the background into `c` at a silhouette while the emission is sampled
at the pixel centre, so edge pixels mask down a little. Measured on two cubes of matched screen
brightness, taking the weight from 0 to 1 drops the merely-lit cube's halo by about eight times and
leaves roughly three quarters of the emitter's.

## Halation and anamorphic (ADR-039)

Both default to **off**. They are strong "authored film" cues and the brief's warning against
everything glowing applies to them more than to anything else here.

**Halation** (`post/halation/*`) is film's warm scatter in the emulsion backing. It is a second
pyramid, starting at quarter resolution so its halo is wider than bloom's, over a threshold that is
also restricted to highlights that are *already warm*: `warmth` blends between "any highlight" and
"only highlights whose red exceeds the mean of green and blue". The result is multiplied by
`halationTint` (a warm red-orange by default) and `halationIntensity`.

**Anamorphic** (`post/anamorphic/*`) is a horizontally stretched bloom tier: a gaussian whose reach
is `8 * stretch` texels of the quarter-resolution wide target, plus optional flare `ghosts` mirrored
through the frame centre, tinted by `anamorphicTint` (a cool blue by default). It needs the bloom
pyramid, so it is computed whenever bloom or anamorphic is on.

The tap count and the pyramid level it reads are **not** free choices: a Gaussian sampled more
coarsely than its source's texel is a comb, and it prints one copy of every isolated highlight per
tap. That is what produced the Glowmere water lattice, and both numbers are now derived from the
reach rather than fixed — see `docs/post-artifact-forensics.md`, which has the measurement, the
impulse response and the arithmetic that ties the comb's period to `stretch`.

Both land in one "wide tier" texture that the composite adds, so enabling the second one costs
almost nothing beyond the first.

## Colour grade

`post/grade/*`, in the composite pass, in scene-linear, after bloom and before the tone map: white
balance (temperature and tint), hue rotation, contrast about mid grey in log space, saturation about
luminance, then lift/gamma/gain.

The grade compensates for the tone curve rather than fighting it. AgX is flatter than the ACES fit
through the midtones — a four-stop grey ramp (0.045 to 0.72) spans 123 output levels under AgX
against 167 under ACES — so a scene moved from ACES to AgX generally wants `post/grade/contrast`
around 1.1 and `post/grade/saturation` around 1.15 to land where it was.

## Tone mapping

`post/tonemap/operator`, `shaders/tonemap.wgsl`.

| id | operator | character |
|---|---|---|
| 0 | ACES (Narkowicz fit) | the film-standard look, strong roll-off, saturated highlights skew toward white |
| 1 | **AgX** (default) | keeps hue in over-exposed regions, flatter default contrast |
| 2 | Reinhard (extended, white 4) | simple, desaturates little, no filmic shoulder |
| 3 | Khronos PBR Neutral | reference-neutral, for looking at materials rather than making pictures |
| 4 | clamp | none; for debugging and for tests that want to read linear values back |

AgX is the default (ADR-039) because this engine's palettes are emissive and saturated, and that is
exactly where the ACES fit misbehaves: feed it scene-linear (8, 1, 0.2) and it returns (255, 232,
149) — the blue channel crushed and the hue skewed — where AgX returns (255, 210, 175), a highlight
that reads as a bright warm colour rather than a different one.

## Selective post (ADR-035, ADR-039)

Three effects can be masked by the auxiliary targets:

| effect | mask | parameter |
|---|---|---|
| bloom | emission target | `post/bloom/emissionWeight` |
| halation | warm highlights above a threshold | `post/halation/warmth`, `post/halation/threshold` |
| sharpen | identifier target | `post/output/sharpenId` (0 = the whole image) |

The halation mask is derived from the colour itself and works today. **The emission mask works
today too**: `SceneRenderer` fills `PostFrameInputs::emission` (`src/rendering/scene_renderer.cpp`,
in the post-chain block), so `post/bloom/emissionWeight` does what it says. It did not until that
line was added -- the target had always been written, the debug view read it, and it was never
handed to the post chain, so the parameter resolved, ran and changed nothing.

`PostFrameInputs::identifier` is **still** unfilled: nothing in the renderer assigns it. So
`post/output/sharpenId` is a registered, round-tripping parameter whose shader path exists and is
tested, and which cannot affect any frame a user renders -- built, but unreachable. It is one line
in the renderer away. Until then the post chain binds a 1x1 placeholder, tells the shader the target
is absent, and sharpening behaves exactly as it would without a mask. Nothing needs to change in
this file when it arrives: set the view on `PostFrameInputs` and the flag follows.

## Restraint

The temptation with all of this is to turn it on. Some ground rules that the shipped examples
follow:

- **Bloom is a highlight, not a haze.** If the whole frame is lifted, the threshold is too low or
  the intensity is doing the job exposure should be doing. Default intensity is 0.2, and 0.5-1.2 is
  a strong-but-defensible range once the energy-conserving upsample is accounted for.
- **Exposure before grade before curve.** If the image is too bright, fix the exposure. If it is the
  wrong *shape*, fix the grade. Reaching for a different tone-map operator to solve a brightness
  problem hides the problem.
- **Halation and anamorphic are punctuation.** They say "this is film" loudly, once. Two scenes in a
  ten-scene piece, not ten.
- **Depth of field is the most expensive stage in the chain** and the one that most often looks
  wrong: a physically correct circle of confusion on a wide lens at f/5.6 is almost nothing, which
  is realistic and usually not what the shot wanted. Open the aperture rather than raising
  `maxRadius`, so focus stays coherent when the camera moves.
- **Auto exposure is a guard, not a normaliser.** See the `minEv` idiom above.
- **Grain and vignette are the last 5 %.** Both are in `post/output/*` and both look cheap above
  about 0.1 and 0.5 respectively.

## Where the code is

| piece | file |
|---|---|
| exposure, lens, focus maths and `camera/*` parameters | `src/scene/camera.{hpp,cpp}` |
| `LensSettings`, `ExposureSettings`, `Camera` | `src/scene/scene_types.hpp` |
| `PostSettings` and the `post/*` parameters | `src/scene/post_settings.{hpp,cpp}` |
| pass ordering, pyramids, metering readback | `src/rendering/post_processor.{hpp,cpp}` |
| the passes themselves | `shaders/post.wgsl` |
| tone map, vignette, grain, sRGB encode | `shaders/tonemap.wgsl` |
| lens/focus/exposure applied to the live camera each frame | `src/app/engine.cpp` (`Engine::update`) |
| the cinematic integration (Image/Look 68) | `scene::ImageLookIntegration` in `src/scene/post_settings.{hpp,cpp}`; `fs_look_atmos`, `fs_look_blur`, `fs_look` in `shaders/post.wgsl` |
| tests | `tests/unit/test_camera.cpp`, `tests/rendering/test_image_formation_gpu.cpp`, `tests/rendering/test_hdr_lab_gpu.cpp`, `tests/rendering/test_image_look_gpu.cpp` |
| the chain measured stage by stage, and the fixture that calibrates it | [`docs/hdr-lab/README.md`](hdr-lab/README.md) |
| every intermediate the chain rendered, from the command line | `--post-stages <dir>` (ADR-277) |
| measured cost | `docs/performance/image-formation.md` |

## Motion blur

Until ADR-040 the motion blur was a *depth reprojection*: it un-projected each pixel with the
inverse view-projection, pushed the world point through the previous frame's view-projection, and
blurred along the difference. That can only ever see **camera** motion. A rotating object, an
instance moving along a spline, a deforming mesh and a particle all sat perfectly sharp inside a
frame that was otherwise smeared, which is the single clearest "this is computer graphics" tell a
moving image has.

It is now tile-based reconstruction over the ADR-035 velocity target, following McGuire et al.,
*A Reconstruction Filter for Plausible Motion Blur* (2012). Every shading path writes that target
— entities, procedural instances, raymarched SDFs, the grid, the skybox and particles from their
simulated previous position — so all five kinds of motion blur for the same cost.

```
fs_velocity_tile_max        1920x1080 -> 96x54    the longest velocity in each 20 px tile,
                                                  in pixels, already scaled by the shutter and
                                                  clamped to post/motionBlur/maxRadius
fs_velocity_neighbour_max   96x54 -> 96x54        3x3 maximum over those tiles
fs_motion_blur              full resolution       samples along the neighbourhood velocity
```

The two tile passes are what let a moving object smear **outside its own silhouette**: a per-pixel
filter can only gather colours that are already there, so it can never widen a shape. The 3x3
neighbour pass is what lets a tile know about the fast thing that is about to sweep into it.

Each of the reconstruction pass's taps is weighted by three terms:

| term | meaning |
|---|---|
| foreground | the tap's surface is *in front of* this pixel and moving, so it smears over us |
| background | the tap is behind us and *we* are moving, so we uncover it |
| coherent   | both are moving at a similar rate: the ordinary blur of one moving surface |

The depth comparison is soft (a 5% band scaled by the nearer of the two view distances) so
silhouettes do not tear, and the "reaches this pixel" test is a cone in the tap's own velocity.
The practical consequence, and the visible difference from the old filter, is that a **still
background is not smeared into a moving object**: silhouettes against static surroundings stay
crisp, where the reprojection blur averaged everything in the neighbourhood.

**Blur length** is `velocity * post/motionBlur/amount * shutterAngle / 360` (ADR-037), clamped to
`post/motionBlur/maxRadius` pixels (40 at 720p, scaled by `height / 720`). A zero shutter angle,
or a zero amount, skips the three passes entirely, and there is no camera-only fallback any more:
without a velocity target the pass does not run.

**Determinism and temporal stability.** The tap jitter is interleaved gradient noise of the *pixel
coordinate alone*. It never reads the frame index or the clock, so the same frame renders
identically every time and a static image does not shimmer between frames. Below half a pixel of
tile motion the pass returns the centre sample unchanged, which keeps a still frame bit-identical
to the unblurred image.

**Particles.** A velocity-stretched particle (ADR-040) is *already* a shutter smear, so it writes
only the fraction of its motion the stretch has not drawn; otherwise the streak would be blurred a
second time. Transparent surfaces keep the usual limitation of any post-process motion blur: they
do not write depth, so where a particle sits in front of a fast-moving surface the filter treats
the pixel as belonging to the surface and smears the composite.
