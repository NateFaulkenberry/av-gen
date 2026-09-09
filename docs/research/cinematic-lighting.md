# Cinematic lighting: architecture, area lights, rigs

Status: research for the visual mastery phase (2026-09-09). Decisions: ADR-033, ADR-034.

## 1. What av-gen has today

Eight punctual lights (directional, point, spot) in a fixed-size uniform array, evaluated in a
single forward pass in `shaders/pbr_shade.wgsl`, plus image-based lighting from an equirectangular
environment (irradiance and prefiltered specular with a BRDF LUT) or a hemisphere fallback. There
are **no shadows of any kind**, no area lights, no light-specific volumetrics, and no notion of a
lighting setup that can be reused between worlds. Colour is a raw linear multiplier: there is no
colour temperature.

That single gap, shadows, is the largest reason the current frames read as "computer generated":
without occlusion, objects float, forms lose their volume, and every surface receives the same
key from the same direction with nothing interrupting it.

## 2. Forward, clustered or deferred

| Approach | Cost | Fit for av-gen |
|---|---|---|
| Single-pass forward (today) | cheapest; every light for every fragment | fine to 8 lights, does not scale to practical lights in a world |
| Clustered forward (Olsson et al. 2012; used by Unreal, Doom 2016) | build a froxel grid of light indices once per frame, each fragment reads only its cluster | keeps one pass, one shading path for meshes, instances and SDFs, scales to hundreds of lights |
| Deferred / visibility buffer | decouples shading from geometry; needs a G-buffer and a second shading pass | conflicts with the instanced/SDF/raymarch mix and with per-object material programs |

Clustered forward is the right step: it keeps a single shading function shared by rasterised
meshes, instanced procedural geometry and raymarched SDFs, which is what makes materials behave
identically across those three paths today. (Source: Olsson, Billeter, Assarsson, "Clustered
Deferred and Forward Shading", HPG 2012; Persson's "Practical Clustered Shading" notes.)

## 3. Area lights

Real light has size, and softness comes from it. Two practical options:

- **Linearly Transformed Cones** (Heitz, Dupuy, Hill, Neubelt, SIGGRAPH 2016) give analytic
  rectangle, disk and line lights for GGX with a small precomputed matrix table. Used by Unreal
  and Unity for rect lights. (https://eheitzresearch.wordpress.com/415-2/ — accessed 2026-09-09.)
- **Representative point** approximations (Karis, "Real Shading in Unreal Engine 4", 2013) are far
  cheaper and visually adequate for spheres and tubes, but weaker at grazing angles and for large
  emitters.

Decision leans to LTC for rectangles and disks, with the representative-point sphere/tube as the
cheap tier, because a rectangle emitter is what makes architecture read as lit by a window or a
panel rather than by a point.

## 4. Colour temperature

Artists reason in Kelvin, not RGB. Planckian locus to CIE xy, then to linear sRGB, gives a
correct warm/cool axis; a tint axis perpendicular to it covers the green/magenta correction. The
useful range is 1500 K (candle) to 12000 K (shade). Every light gets `temperature` and `tint`
alongside its colour, and the product is what reaches the shader. This is what makes the standard
cinematic relationships (warm key against cool ambient, cool key with warm practicals) reachable
in one control rather than by hand-mixing RGB.

## 5. Light rigs

A rig is a named set of lights with roles (key, fill, rim, back, ambient, practical) expressed
relative to the subject and the camera rather than in world coordinates, so the same rig applies
to any world. Film practice: key at 30 to 45 degrees off the camera axis and above, fill opposite
at a fraction of the key (the ratio sets the drama), rim behind and high to separate subject from
background. (Sources: Brown, "Cinematography: Theory and Practice"; Birn, "Digital Lighting and
Rendering", 3rd ed., chapters on three-point lighting and motivated light.)

Rigs must be data: a JSON block that expands into ordinary lights with ordinary parameters, so a
world can carry several and a preset can morph between them.

## 6. What lighting must feed

Lighting is not only shading. The same light set drives volumetric scattering (shafts), contact
shadows, and the exposure the camera meters against. That argues for one light description with
per-light flags (`castsShadow`, `volumetricStrength`, `contactShadow`) rather than parallel lists.
