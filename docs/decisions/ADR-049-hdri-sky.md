# ADR-049: The HDRI sky

## Status
Accepted, 2026-09-09.

## Context
The engine could already synthesise an analytic sky and could already light a scene from an
equirectangular `.hdr`. What it could not do was *show* one. The background pass sampled the
128-pixel GGX-prefiltered cube the IBL chain ends with, at whatever intensity the shading was
using. Three things followed from that, and all three are visible:

- **A photographed sky lost everything that made it worth photographing.** A cube face 128 texels
  across is under three texels per degree. A star in an 8K equirect is one texel — about a
  twentieth of a degree. Resampling the map to a 256-pixel cube and then GGX-filtering it to 128
  averages every star into the sky it sits in. What came back was a smooth gradient with a smear
  where the moon had been.
- **Sky brightness and lighting brightness were one number.** `env/intensity` scaled both. A night
  valley wants a sky dark enough to read as night and enough ambient light to keep shadowed rock
  off black; there is no single value that is both, so scenes picked one and lived with the other.
  Several shipped scenes simply set `intensity` to 0.
- **A scene file could not aim the sky.** `environmentRotation` existed on `Environment` and had an
  `env/rotation` parameter, but no scene-file key ever wrote it, so the parameter always started at
  0. The one control that decides where the moon appears was reachable only by dragging a slider.

## Decision

### The visible sky is the map, not the IBL made from it
`EnvironmentProcessor::process` already uploads the equirect as a mipped RGBA16Float texture to
build the cube from. It now hands that texture back on `IblResources`, the IBL bind group carries
it, and `shaders/skybox.wgsl` samples it directly. One upload, one resident texture, no per-frame
decode or conversion. The IBL chain is untouched: irradiance and specular still come from the cube,
because that is what they are for.

The procedural sky (ADR-036) keeps the cube path, because it has no map behind it and its cube *is*
its highest-resolution form.

**Mip selection is computed, not left to the hardware.** `atan2` wraps at the antimeridian, so the
2×2 quad straddling the seam sees a derivative the width of the whole texture and would take the
coarsest mip — a blurred column down the sky. The shader corrects the wrapped derivative and
computes the level itself, which also gives `skyboxBlur` somewhere to add in. The sky gets its own
sampler, identical to the IBL's except that it repeats in longitude.

### Two intensities, because they are two looks
`environment.intensity` scales the light the environment casts. `environment.skyIntensity` scales
what the background pass draws. They ride separate lanes to the GPU (`params.w` and `skyExtra.z`)
and neither touches the other. This is the change the whole ADR is for: Glowmere Valley draws its
sky at 0.08 and lights from it at 0.3, and no single number produces that image.

`skyBloom` is the third of the same family: how much of the sky reaches ADR-039's selective-bloom
mask. It defaults to 0, which is the behaviour before this ADR — a bright environment cannot glow
through the emission target. It only bites when `post/bloom/emissionWeight` is above 0; with
selective bloom off, the sky blooms on luminance alone and this knob has nothing to gate.

### The moonlight comes from the moon
`lightFromEnvironment` points the key light away from the map's brightest direction. That direction
is the radiance-weighted centroid of every texel within a quarter of the peak, weighted by
`sin(theta)` for solid angle — an argmax would land on whichever single texel won and would move
when the resolution changed, and an unweighted centroid would be dragged towards the poles by the
projection's stretching. Measured on the shipped sky, 2K gives (0.778, 0.296, 0.555) and 4K gives
(0.777, 0.293, 0.557): under a fifth of a degree apart, so swapping resolutions does not move the
shadows.

It is computed once when the map loads — an 8K scan is 33 million texels — and rotated by
`environmentRotation` when applied, so turning the sky turns the light with it. The alternative was
an author keeping a rotation and a light azimuth in step by hand, which is a bug waiting for the
first time someone changes one of them.

The rig still owns the light's colour, intensity, shadows and softness. Only its direction is taken
over, and only when the scene asks.

### Half-float saturates instead of overflowing
Kloppenheim 02's moon peaks at 1.0×10⁵ at 4K and 1.3×10⁵ at 8K; a half tops out at 65504. Five
texels of the 4K map became `+inf` on upload, and inf does not stay where you put it: it survived
filtering, reached the bloom pyramid, and came back as a black rectangle four hundred pixels wide
across the top of the frame. `uploadTextureAsHalf` now clamps. No tone map resolves 65504 from
1.0×10⁵, so nothing is lost and a whole class of "one texel ruins the frame" is closed.

### `rotation`, `skyIntensity`, `skyBloom`, `skybox`, `lightFromEnvironment` are scene-file keys
Five keys under `environment`. Two of them (`rotation`, `skybox`) name fields that already existed
and had no way in from a file.

## Consequences
The background is still one fullscreen triangle with one texture fetch, drawn inside the scene pass
with the camera's own view ray. It goes through no culling, no shadow work and no per-object path,
and the ray direction is a difference of two unprojected points, so it depends on the camera's
orientation alone: crossing the 640 m valley does not move the sky. On the shipped world at 1080p
the pass is not separable from run-to-run variation — with the sky on, the median offline-tier frame
was 27.85 and 27.98 ms across two runs; with it off, 27.66 and 28.70 ms.

Load cost scales with the map. Building the IBL chain took 39 ms from a 2K map, 125 ms from 4K and
425 ms from 8K, against 33 ms for the procedural sky. The resident texture is about 89 MB at 4K.

**4K is the resolution to ship at 720p–1080p.** At 2K a star is a blurred four-pixel blob; at 4K it
is a crisp point; 8K resolves a few more faint stars and is otherwise indistinguishable at
1280×720, for four times the memory and three times the load. Higher output resolutions will want
8K; 24K is never the answer.

Two scenes changed more than their environment block. Putting the moon in frame puts it near the
view axis, and Glowmere Valley's volumetrics ran a Henyey-Greenstein `g` of 0.6 — strongly
forward-peaked — which turned the whole valley into glare the moment the light came from where the
camera was looking. The scene now uses `g = 0.12` and less in-scatter. That is not a bug the sky
introduced; it is a look that had only ever been seen with the key off to the side.

The two shipped skies differ by a factor of about four in mean radiance (0.22 against 0.78), so
`intensity` and `skyIntensity` are per-scene numbers, not a global calibration. There is no
automatic exposure matching between environments and this ADR does not add one.

Not done: no importance-sampled light extraction (one dominant direction, not a set), no sky
animation or time of day, no separate rotation for the background and the lighting — deliberately,
since a sky that rotates away from its own light is the failure this ADR exists to prevent.
