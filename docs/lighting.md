# Lighting

Decisions: ADR-033 (clustered forward, area lights, colour temperature, light rigs), ADR-034
(cascaded shadows, contact shadows, ground-truth occlusion), ADR-035 (auxiliary targets),
ADR-036 (the procedural sky environment, below).
Research: `docs/research/cinematic-lighting.md`, `docs/research/shadows-and-occlusion.md`.

Everything below happens in `shaders/lighting.wgsl`, which `pbr_shade.wgsl` includes, so a mesh,
an instanced procedural object and a raymarched SDF surface receive light through exactly the same
code. There is one shading path, not three.

## The light

`scene::PunctualLight` (the name is historical; it covers area emitters too):

| Field | Meaning |
|---|---|
| `type` | `directional`, `point`, `spot`, `rect`, `disk`, `tube`, `sphere` |
| `role` | `key`, `fill`, `rim`, `back`, `ambient`, `practical` - used by rigs and the inspector, never by shading |
| `position`, `direction`, `up` | world placement; `direction` is the way the light travels, `up` orients a rect or tube emitter |
| `color`, `intensity` | linear colour and a photometric level: lux for directional, candela for point and spot, nits over the emitter for area kinds |
| `temperature`, `tint` | Kelvin (1500..12000) and a green/magenta axis; multiplied into the colour on the CPU |
| `range` | 0 = infinite, else a smooth window `(1 - (d/r)^4)^2` on the inverse square |
| `innerConeAngle`, `outerConeAngle` | spot cone, smoothly interpolated |
| `width`, `height`, `radius` | emitter size: rect uses width and height, disk and sphere the radius, tube both |
| `castsShadow`, `shadowStrength`, `shadowBias`, `softness` | shadow map participation (ADR-034) |
| `contactShadow` | the screen-space march, independent of whether the light has a map |
| `volumetricStrength` | how much this light lights the *air* in the volumetric pass (ADR-032): 1 = as much as it lights surfaces, 0 = not at all. Defaults to 1 on a `PunctualLight` and to 0 on a rig light |
| `diffuseOnly`, `specularOnly`, `enabled` | shaping switches |

`rendering::packLight` turns one of these into the 128-byte `GpuLight` the shader reads. The
colour-temperature product, the influence radius and the shadow-view index are all resolved there,
so the GPU never sees Kelvin and never searches for a shadow map.

## Colour temperature

`scene::colorTemperatureToRgb(kelvin, tint)` walks the Planckian locus (Kim et al.'s cubic fit) to
CIE xy, converts to linear sRGB and normalises to luminance 1, so **changing the temperature never
changes the exposure** - only the hue. The result is divided through by the value at 6500 K, which
makes the neutral point exactly neutral: a 6500 K black body is close to, but not identical with,
the D65 white point sRGB is defined against, and an artist reasonably expects the "neutral" mark on
the dial to be neutral. Positive `tint` is magenta, negative is green, as in every grading tool.

Useful values: 1800 K candle, 2700 K tungsten, 3200 K studio lamp, 4300 K morning sun, 5600 K
daylight, 6500 K neutral, 7500 K overcast, 9000-12000 K open shade and sky. The cinematic
relationships fall out of the pairing: a warm key against a cool ambient, or a cool key with warm
practicals, is two numbers rather than six hand-mixed channels.

## Area lights

Rect and disk emitters use **linearly transformed cones** (Heitz et al. 2016). The diffuse term is
the exact clamped-cosine irradiance of the polygon; the specular term is the same integral through
a fitted linear transform of the GGX lobe, read from a 32x32 table. A disk is integrated as the
square of equal area, which is indistinguishable at the sizes worlds use and keeps one code path.

The table is **built at startup, not shipped as data** (`rendering::buildLtcTable`): for each
(roughness, cos theta) cell it importance-samples the GGX lobe, takes the energy, the mean
direction and the second moments about it, and constructs the transform that maps the clamped
cosine onto that ellipsoid. That is the initial guess of Heitz's fit - smooth, energy-consistent
and deterministic - and it costs about 10 ms once. The second table holds the split-sum
scale/bias, so the Fresnel behaviour matches the punctual path.

Tube and sphere emitters use the **representative point** (Karis 2013): the closest point on the
emitter to the reflection ray for specular, with the lobe widened by the emitter's angular size and
renormalised, and the emitter centre for diffuse. Their radiance is multiplied by the emitter area
so the units agree with the rect path.

An area light's `intensity` is a radiance, so a bigger emitter at the same intensity is *brighter*.
Rigs handle this for you (see below); authoring by hand, divide by the area if you want the same
illuminance from a softer source.

## Clustered forward

A compute pass (`shaders/clusters.wgsl`) builds a **16 x 8 x 24 froxel grid** each frame. Depth is
sliced exponentially (Olsson et al. 2012), so slice *k* spans
`near * (far/near)^(k/24) .. near * (far/near)^((k+1)/24)` and froxels stay roughly cubic. Each
thread builds one froxel's view-space bounds and tests every local light's sphere of influence
against it, writing up to 32 indices. The output is one storage buffer: 3072 counts, then a
fixed block of 32 indices per froxel.

Directional lights never enter the grid - they reach every fragment, so they are evaluated first
and unconditionally. `lightInfluenceRadius` gives a local light its sphere: its explicit `range`
when it has one, otherwise the distance at which inverse-square falloff drops it below 0.4% of its
peak, plus its own extent.

The **fallback tier** (`QualitySettings::clusteredLighting = false`) evaluates the eight lights of
the old uniform array instead, with no clusters, no shadows and no area kinds. It exists so a frame
can always be rendered the way the pre-ADR-033 renderer did, and a GPU test checks the two paths
agree on a single directional light.

## Shadows

**Cascaded shadow maps** for one directional light: 2 cascades at the preview tier, 3 at realtime,
4 at high and offline, into a `texture_depth_2d_array` atlas (1024 to 4096 square per layer).

- The split scheme is the practical blend of logarithmic and uniform (Zhang et al. 2006) at
  lambda 0.85, over `[camera near, min(3 x scene radius, camera far)]` - fitting cascades to a
  kilometre-deep far plane wastes every texel on geometry nobody looks at.
- Each cascade is fitted to the **bounding sphere** of its slice of the camera frustum, which is
  invariant to camera rotation, and its light-space origin is **snapped to whole texels**, which
  makes it invariant to translation within a texel. Together those stop the shadow edges crawling.
- The light's near plane is pulled back by `clamp(scene radius, 0.5r, 3r)` to catch casters behind
  the visible range. Every metre of pull-back costs depth precision, so it is kept tight.
- Bias is **normal-offset** (the receiver is pushed along its normal by a texel-scaled amount, more
  at grazing angles) plus a **slope-scaled constant** expressed in world units and converted to
  normalised depth per view, so a cascade covering ten metres and one covering a kilometre get the
  same visual bias rather than the same number.
- Filtering is PCF over a **rotated Poisson disc** (12 to 24 taps, rotated per pixel by interleaved
  gradient noise indexed by the pixel, never a clock). The cascaded light additionally uses
  **percentage-closer soft shadows**: a blocker search, then a filter width proportional to the
  receiver-to-blocker distance, so a large soft source reads as one.

**Spot lights** that cast get one perspective map covering their cone. **Point and area lights get
no map** in this implementation and rely on contact shadows and occlusion.

Casters are drawn by a depth-only pass per view. The trick that makes this cheap to maintain: each
view has its own copy of the frame uniform block with `viewProj` replaced by the light's matrix, so
the depth passes run the *ordinary* vertex shaders - entities, procedural instances (the same
instance buffer, so instanced geometry casts without a second data path), meshed SDFs - unchanged.
Raymarched SDFs march their bounding box at a quarter of the steps with a looser epsilon, which is
all a caster silhouette needs.

**At the realtime and preview tiers the map term is looked up at half resolution** for up to three
directional lights and bilaterally upsampled in the lit pass (ADR-087): the cascade lookup is the
largest single term in the scene pass at the resolution the editor renders, and a penumbra is
low-frequency enough to survive it. The High and Offline tiers look it up per pixel, so an offline
render is the frame this optimisation did not touch. Blended surfaces, local lights and any
directional light past the third take the per-pixel path either way.

Known limits: one cascaded directional light per frame; instanced geometry culled against the
camera frustum does not cast from off-screen; there are no point-light cube maps.

## Contact shadows

A 12-step (8 to 24 by tier) march through the linear depth target from the shaded point towards the
light, combined with the shadow map by minimum. The ray scales with view depth so it keeps a
constant screen size, starts half a step off the surface, and treats a depth difference between a
quarter of a step and the ray's own length as an occluder - beyond that the depth buffer is showing
unrelated geometry rather than a blocker. The last steps fade out so the shadow ends in a gradient.

This is what makes small parts sit against each other: an object resting on a floor, a pipe against
a wall, the base of a column. It runs per light for any light with `contactShadow` set.

It used to be *the only* shadow a point or area light had. Since 2026-09-12 those get a real map too:
six faces in the ordinary shadow atlas, picked per fragment by the dominant axis of the direction
from the light (ADR-034). It is opt-in through the light's own `castsShadow` and costs six of the
eight shadow views, so it is for the lamp a shot is about rather than for every practical in a scene.

`contactShadow` defaults to true, so a light that casts no map still marches -- deliberately, per
ADR-034, because otherwise most lights would cast nothing. It is not free: Glowmere runs three
marches per fragment and only the moon has a map, and turning off the two that do not is worth
3.3 ms at the editor's 2880x1166 canvas. A light rig can now say so per light
(`"contactShadow": false` in a `.rig.json` light), and the default stays true because switching it
off changes the image (ADR-087).

**It is not masked.** The half-resolution shadow mask carries the map term only; a march evaluated
once per 2x2 doubles the width of every contact shadow it finds, which is exactly the detail the
march exists to produce. See ADR-087.

## Ambient occlusion

GTAO (Jimenez et al. 2016) at half resolution from the linear depth target, with normals
reconstructed from depth by a best-fit of the closer neighbour on each axis, oriented by the winding
of the two screen-space differences rather than by the sign of the normal's own view-space z -- that
sign is a coin flip on a surface seen nearly edge-on, which is most of the ground in a landscape
(ADR-087). Each of 2 to 6
direction slices searches the horizon both ways over 4 to 12 steps within a world radius, and the
ground-truth visibility integral is accumulated along with a **bent normal**. A temporal pass then
reprojects the previous frame with the camera motion, rejects history from a different surface by
depth, clamps what is left to the 3x3 neighbourhood of the new frame, and blends.

The result is `rg` = octahedral bent normal, `b` = visibility, `a` = view depth. The shading pass
upsamples it with a **depth-aware four-tap filter** (the depth in alpha is what weights it), which
keeps occlusion from bleeding across silhouettes and saves a full-resolution target. Visibility
multiplies ambient diffuse; the bent normal is what the irradiance is looked up along; and specular
gets Lagarde and de Rousiers' specular-occlusion term from the visibility cone.

**Determinism**: the per-frame sample rotation is interleaved gradient noise offset by the frame
index - never a wall clock - and the temporal history is dropped whenever the frame index does not
advance by exactly one. Two renders of the same frame are therefore bit-identical, which a GPU test
checks.

## The HDRI sky (ADR-049)

An equirectangular `.hdr` (2:1) is a first-class sky: it lights the scene *and* stands behind it,
at its own resolution and at its own brightness.

```json
"environment": {
  "map": "../../assets/hdri/kloppenheim_02_puresky_4k.hdr",
  "intensity": 0.3,
  "rotation": -1.517,
  "skyIntensity": 0.08,
  "skyBloom": 0.35,
  "lightFromEnvironment": true
}
```

| Key | Default | Meaning |
|---|---|---|
| `map` | — | the equirectangular `.hdr`, through the asset registry. Unset = the procedural sky below |
| `intensity` | 1.0 | how much light the environment casts. Nothing to do with how bright it looks |
| `rotation` | 0.0 | radians about +Y. Turns the visible sky *and* its lighting together |
| `skyIntensity` | 1.0 | how bright the sky is drawn. Nothing to do with how much it lights |
| `skyBloom` | 0.0 | how much of the sky the selective-bloom mask sees. Needs `post/bloom/emissionWeight` above 0 to do anything |
| `skybox` | true | draw the environment behind the world at all |
| `lightFromEnvironment` | false | aim the key light away from the map's brightest direction |

**Two intensities, deliberately.** `intensity` and `skyIntensity` are the same number in most
engines and that number cannot describe a night: Glowmere Valley draws its sky at 0.08 and lights
from it at 0.3. They ride separate lanes (`params.w` and `skyExtra.z`) and neither affects the
other, which a GPU test asserts in both directions.

**The moonlight comes from the moon.** With `lightFromEnvironment`, the key light's direction is
minus the map's brightest direction (`scene::environmentDominantDirection` — the solid-angle- and
radiance-weighted centroid of the disc, so it is stable across a map's resolutions), rotated by
`rotation`. Turn the sky and the light turns with it. The rig still owns the light's colour,
intensity, shadows and softness.

**Resolution.** The background samples the equirect itself, not the 128-pixel prefiltered cube the
IBL ends with, because a star is one texel and a cube face is under three texels per degree. 4K is
the right size for 720p–1080p output: 2K blurs stars into blobs, 8K is indistinguishable at
1280×720 for four times the memory. Fetch them with `tools/fetch_polyhaven.py --hdri --fetch`.

**Note.** Putting a moon *in frame* puts it near the view axis, where a forward-peaked volumetric
phase function (`volumeAnisotropy` near 1) turns the whole shot into glare. That is physics, not a
bug, but it means a scene tuned with the key off to the side will need its volumetrics revisited.

## The procedural sky (ADR-036)

A scene with no HDR environment map used to fall back to a two-colour hemispheric constant, which
gave a 0.95-metallic surface nothing to reflect: metal rendered as flat dark grey, because a metal
*is* its reflection. So when `environment.environmentMap` is unset, the renderer **synthesises an
environment**: an analytic gradient sky with a sun disc, rendered into the same cube / irradiance /
prefiltered chain the HDR path uses, so image-based lighting works identically either way.

`scene::SkySettings` lives on `scene::Environment` and every field is a parameter under
`env/sky/*`:

| Field (`env/sky/…`) | Default | Meaning |
|---|---|---|
| `enabled` | **on** | consulted only when there is no environment map, so an existing scene gains reflections and keeps its look |
| `zenithColor` | (0.055, 0.105, 0.235) | radiance straight up |
| `horizonColor` | (0.300, 0.340, 0.420) | radiance at the horizon |
| `groundColor` | (0.045, 0.042, 0.038) | radiance below the horizon |
| `haze` | 0.25 | turbidity-like: how far up the horizon colour reaches. Small = a tight bright band (studio); large = an evenly bright dome (overcast) |
| `sunColor` | (1.0, 0.93, 0.82) | tinted by the key light's colour and temperature |
| `sunIntensity` | 8 | radiance of the disc, relative to the gradient |
| `sunSize` | 0.045 rad | angular *radius* (~2.6 degrees; wider than the real sun so a 128 px prefiltered cube resolves it) |
| `sunGlow` | 0.18 rad | width of the aureole around the disc |
| `intensity` | 1 | multiplies the whole sky, **and is the IBL intensity the shading uses** |
| `background` | off | draw the sky behind the scene instead of the flat background colour |
| `useKeyLight` | on | take the sun direction from the scene's key light |
| `sunDirection` | (0.35, 0.75, 0.55) | the direction *towards* the sun, used when `useKeyLight` is off or there is no key light |

The model, exactly (`scene::skyRadiance`, transliterated by `fs_sky` in `shaders/environment.wgsl`):

```
h     = saturate(dir.y)
haze  = exp(-h / max(hazeWidth, 1e-3))
sky   = mix(zenithColor, horizonColor, haze)
band  = smoothstep(-0.03, 0.03, dir.y)          // a soft horizon: no seam in the cube
base  = mix(groundColor, sky, band)
theta = angle(dir, sunDirection)
r     = max(sunAngularRadius, minRadius)         // minRadius = one texel of the mip being written
disc  = (1 - smoothstep(r * 0.85, r * 1.15, theta)) * (sunAngularRadius / r)^2
glow  = exp(-theta / max(sunGlowWidth, 1e-3)) * 0.02
out   = (base + sunColor * sunIntensity * (disc + glow) * band) * intensity
```

Notes on the two parts that are not obvious:

- **The disc widens with the mip.** Instead of downsampling the cube to build its mip chain, each
  mip renders the sky with the disc widened to at least one texel and its radiance scaled by
  `(radius / widened)^2`. That conserves the disc's energy exactly, which is what keeps the sun
  from disappearing between texels in the coarse mips the prefilter pass reads — and it keeps the
  whole build analytic, deterministic and free of a downsample pass.
- **The aureole is 2% of the disc.** Coupling the halo any harder to `sunIntensity` floods the whole
  environment as soon as the sun is turned up enough to give a metal a real highlight, which is
  exactly the setting a cinematic scene wants.

### The sun and the key light

`scene::skyKeyLight` picks the first enabled directional light with role `key`, else the first
enabled directional light, else nothing. The sun direction is `-normalize(light.direction)` (a
light's `direction` is the way the light travels), and the sun colour is multiplied by the light's
colour and temperature **normalised to luminance 1**, so relighting with a warm key warms the
reflections without the light's intensity doubling as sky brightness. Swap a light rig and the
environment follows it.

### Build and caching

`SceneRenderer::updateEnvironment` resolves the settings against the scene's lights
(`scene::resolveSky`), hashes the result (`SkyRuntime::hash`) and calls
`EnvironmentProcessor::processSky` only when that hash changes — about **10 ms**, once, at the
default 256 cube / 128 prefiltered (`docs/performance/surfaces.md`). Nothing about the sky is
per-frame.

Two lanes carry it to the shaders: `envParams.w` is already "IBL is live" and now covers the sky
too, and `skyExtra` (x = the IBL came from the sky, y = draw it as the background) is new.
`frame.params.w`, the IBL intensity the ambient term multiplies by, becomes `env/sky/intensity`
rather than `env/intensity` when the sky is the source — several shipped scenes set
`env/intensity` to 0 precisely because it did nothing without a map, and they should not go dark
now that it would.

The skybox is **not** drawn by default: a procedural sky is an IBL source first, and a scene with a
near-black backdrop and a bright environment is a studio setup, not a mistake. `env/sky/background`
turns the backdrop on.

`scene::skyIrradiance` is the CPU reference for the irradiance cube — a fixed Fibonacci-hemisphere
quadrature, so it is bit-deterministic and can be compared against the GPU's result in tests.

## Light rigs

A rig is a named lighting setup expressed relative to the subject and the camera, so the same rig
lights any world at any scale. `scene::LightRig::expand()` turns it into ordinary
`scene::PunctualLight`s at build time; nothing downstream knows a rig existed.

### The placement frame

- **azimuth** is measured about the subject's up axis from the camera's view direction: 0 puts the
  light behind the camera pointing at the subject, +90 to the camera's right, 180 behind the
  subject. `followCamera: false` measures it from world +Z instead, for a light that belongs to the
  world rather than to the shot.
- **elevation** is degrees above the horizon, -90 to 90.
- **distance** is in *subject radii*, so "2.5" means the same thing in a room and in a cathedral.
- **size** is in subject radii too, and `aspect` is the rect's width over its height.

### Photometry

`intensity` is a ratio against the rig's `keyIntensity`, and means "this much illuminance at the
subject". `expand()` converts it into the light's own unit:

- directional: used as-is (it is already an illuminance),
- point and spot: multiplied by distance squared (an intensity),
- rect, disk, tube, sphere: multiplied by distance squared and divided by the emitter's area (a
  radiance), so making an emitter bigger makes it *softer*, not brighter.

That is what keeps a rig looking the same whatever the world's scale and whatever size the emitters
are given. Lights with the `ambient` role take the rig's `ambientIntensity`, `ambientColor` and
`ambientTemperature` instead of their own.

### Using one

In a scene file's environment block:

```json
"environment": {
  "background": [0.008, 0.007, 0.012],
  "lightRig": "../lightrigs/sacred-warm.rig.json"
}
```

The path is resolved through the asset registry like any other asset. The rig is re-expanded every
frame after the camera is final, so `followCamera` lights track the shot, and its parameters are
live: `lightrig/<rig>/keyIntensity`, `.../ambientIntensity`, `.../ambientColor`,
`.../ambientTemperature` and `lightrig/<rig>/<light>/intensity|azimuth|elevation|distance|
temperature|size|shadowStrength`. `scene/keyLight` scales the whole rig, so the existing world
macro still does what it always did. A world with a rig does not get the default key light; a world
without one still does, and it casts.

### Authoring a rig

```json
{
  "format": "avgen-lightrig", "version": 1,
  "name": "MyRig",
  "description": "what it is for",
  "keyIntensity": 1.6, "ambientIntensity": 0.14,
  "ambientColor": [0.45, 0.58, 0.85], "ambientTemperature": 9500,
  "lights": [
    { "name": "key", "type": "directional", "role": "key",
      "azimuth": 38, "elevation": 42, "distance": 3.0, "intensity": 1.0,
      "temperature": 2900, "size": 1.2, "castsShadow": true, "softness": 1.4,
      "volumetric": 0.5 },
    { "name": "window", "type": "rect", "role": "practical",
      "azimuth": -55, "elevation": 30, "distance": 2.2, "intensity": 0.35,
      "temperature": 3200, "size": 1.6, "aspect": 0.35 }
  ]
}
```

Rules of thumb, from the cinematography the research cites: the key sits 30 to 45 degrees off the
camera axis and above; the fill is opposite at a fraction of it, and that *ratio* is what sets the
drama (0.1 is hard and contrasty, 0.5 is soft and commercial); the rim is behind and high, cooler
than the key, and its job is separation, not illumination. Keep the sum of the ratios near 2 so
`keyIntensity` remains a meaningful exposure control. Give exactly one light `castsShadow` - it is
the only one that gets the soft-shadow treatment, and a second cascaded light is not rendered.

### The rigs that ship

Under `examples/lightrigs/`:

| Rig | What it is for |
|---|---|
| **SacredWarm** | A high warm key raking through the space, a cool sky fill, a cold rim: the cathedral relationship of candle against daylight. |
| **IndustrialCold** | Hard cold overheads with warm sodium practicals underneath. Machinery, not architecture. |
| **CosmicBlue** | A cold distant key with a magenta counter-rim. Deep space: no atmosphere, hard edges. |
| **Bioluminescent** | No sun. Low soft area sources under and beside the subject, a faint cold ambient above. |
| **Monumental** | One enormous soft source high and slightly behind the camera, almost no fill: scale reads through the falloff alone. |
| **NightCinematic** | Moonlight three-quarter back, a very low warm fill from the camera side, practicals doing the storytelling. |

## Budgets

Measured at 1920x1080 on an Apple M2 Max; the full table and the method are in
`docs/performance/lighting.md`. At the realtime tier, on a scene with one shadow-casting
directional light and fifty opaque draws:

| | Cost |
|---|---|
| Shadow map generation (3 cascades at 2048) | 0.07-0.13 ms |
| Ambient occlusion (half res, 3 slices x 6 steps) | 0.33-0.52 ms |
| **Combined, against the 4 ms budget of ADR-034** | **0.4-0.7 ms** |
| Whole frame, no shadows and no occlusion | 1.2 ms |
| Whole frame, cascades + PCSS + contact + occlusion | 4.1-6.0 ms |
| 200 clustered point lights on top of that | +3.7 ms |

The map generation is nearly free; the money goes on the *lookup* - PCSS costs a blocker search
plus a variable-width filter for every lit pixel. Turn `softShadows` off in the tier for plain PCF
if a world needs the milliseconds back.

## Where the code is

| File | What |
|---|---|
| `src/scene/scene_types.hpp` | `PunctualLight`, the light types and roles |
| `src/scene/light_rig.{hpp,cpp}` | rigs, their JSON, their parameters, colour temperature |
| `src/rendering/light_data.{hpp,cpp}` | light packing, the froxel grid and its CPU reference, the LTC fit, polygon irradiance |
| `src/rendering/shadow_math.{hpp,cpp}` | the cascade maths, GPU-free and tested against the frustum it covers |
| `src/rendering/shadow_renderer.{hpp,cpp}` | the atlas, the per-view uniforms, the view selection |
| `src/rendering/ao_renderer.{hpp,cpp}` | the occlusion and temporal passes |
| `src/rendering/render_quality.hpp` | the tiers and what each scales |
| `shaders/lighting.wgsl` | all of the shading above |
| `shaders/clusters.wgsl`, `shaders/gtao.wgsl` | the froxel build and the occlusion passes |
| `src/scene/sky.{hpp,cpp}` | the procedural sky: settings, the key-light sun, the analytic model, the irradiance reference |
| `src/rendering/environment.{hpp,cpp}` | the IBL chain, fed by an HDR map (`process`) or by the sky (`processSky`) |
| `shaders/environment.wgsl` | the cube, irradiance, prefilter, BRDF and `fs_sky` passes |
| `examples/lightrigs/` | the six rigs |
