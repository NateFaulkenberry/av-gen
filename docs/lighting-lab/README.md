# Lighting Lab

> Which lights reached this pixel, and with how much?

Lab #7 of the Engineering Lab Suite (ADR-260, ADR-261). Registered in `src/labs/lab.cpp`; opens on
`examples/labs/lighting-lab.scene.json` with `examples/lightrigs/lighting-lab.rig.json`; cases in
`examples/labs/lighting/cases.json`.

**The map is the deliverable.** §1 below is every stage between "a light exists in the scene" and
"this pixel is brighter", named by file and symbol, because the two labs that run after this one --
HDR/Exposure/Bloom and Volumetric/Atmosphere -- build on the radiance that leaves it. Everything
§7 of the specification asks for that this engine **does not have** is in §5 under its own heading
rather than quietly filled in.

One boundary is worth stating before the map: **the Shadow Lab owns what shadow does with a light;
this lab owns the light.** Position, colour, intensity, reach, packing, which froxel it lands in,
and the BRDF it drives. Whether it is occluded is `docs/shadow-lab/README.md`.

---

## 1. The pipeline, stage by stage

### 1.0 Where a light comes from — and the first surprise

**A scene file can author a light — since ADR-278, and it could not when this lab mapped the
pipeline.** `Composition::fromJson` (`src/scene/composition.cpp`) reads `camera`, `cameraDirection`,
`lightRig`, `lights`, `environment`, `composition`, `nodes`, `entities`, `fields`, `grids`,
`heroes`, `staging`, `graph`, `worldEffects`, `atmosphericEffects`, `materialPrograms`, `post`,
`wind` and the `nav*` keys. There are now **five** routes into `scene::Scene::lights`, in the order
`rebuild` and `applyParameters` add them:

| route | where | note |
|---|---|---|
| a glTF asset's own lights | `importLight` (`src/assets/gltf_loader.cpp`) | KHR_lights_punctual; Directional, Point and Spot only. **No asset in this repository carries one.** |
| the scene file's `"lights"` | `Composition::fromJson` → `rebuild` (ADR-278) | every `PunctualLight` field; `"node"` makes the light ride a composition node's world transform |
| the default key | `defaultKeyLight()` (`src/scene/composition.cpp`) | added only when the scene has no rig **and** no light from either route above |
| a light rig | `Composition::applyParameters` → `LightRig::expand` (`src/scene/light_rig.cpp`) | re-expanded **every frame**, after the camera is final, so `followCamera` is live. Appended *alongside* the authored lights, not instead of them |
| procedural ecology lights | `Composition::updateEcologyLights` | ADR-053; glow clusters near the camera become Point lights, capped by `kMaxEcologyLights` |

**What this section used to say, and what it cost.** Until ADR-278 the first route did not exist,
and unknown top-level keys were ignored rather than refused — so **two** lab fixtures,
`examples/labs/lod-geometry-lab.scene.json` and `examples/labs/visibility-culling-lab.scene.json`,
carried a top-level `"lights"` array that nothing read, and both were lit by `defaultKeyLight()` at
a **19.2-degree** different direction, intensity 3 rather than the 4 they wrote, and 5600 K rather
than the neutral 6500 K. It was ADR-225's defect in a scene file. Both now obey their own files;
`tests/unit/test_scene_authored_lights.cpp` is the guard, and it is paired with the same fixture
with its `"lights"` key removed because an assertion that the fixture has "a directional key that
casts a shadow" is a description of `defaultKeyLight()` and passes on the bug (ADR-182).

The other half of ADR-278 is that a key nobody reads is no longer silent: `core/json_keys.hpp`
warns by name, and its first run over 56 scenes found that **fifteen** of them write
`"volumeNoiseAmount"` where the parser reads `"volumeNoise"`.

It is also why the reach fix of §3.1 **cannot change a shipped frame**. Every non-directional light
in this repository comes from a rig or from the ecology, and both set an explicit range; no asset
carries a KHR_lights_punctual light; and a directional light never enters the froxel grid. The
heuristic path is reached only by a light built in code, which today means the render tests.

`LightRig::expand` is also the reason most of §1.3's arithmetic never runs in production: it sets
`range = distance * 6` on **every** non-directional light it makes, and the ecology lights set
`range = radius * 4`. A light with an explicit range takes the well-behaved path.

### 1.1 Which lights are shaded, and in what order

`rendering::orderLightsForShading` (`src/rendering/light_data.cpp`)

Enabled directional lights first, then enabled local ones, truncated to `kMaxSceneLights = 256`.
The return value is the number of leading directional lights, and every index in the frame depends
on it: the froxel lists hold *local* indices and the shader adds this count back
(`directLighting`, `shaders/lighting.wgsl`).

Disabled lights are dropped here and nowhere else — there is no per-light frustum cull, no
distance cull and no importance sort. Two hundred and fifty-seven lights means the two hundred and
fifty-seventh is not in the frame, silently, in scene order.

### 1.2 Packing

`rendering::packLight` (`src/rendering/light_data.cpp`) → `GpuLight`, 128 bytes, the layout
`shaders/shadows.wgsl` declares.

* Colour temperature is resolved **here**: `colorIntensity.rgb = color * colorTemperatureToRgb(temperature, tint) * max(intensity, 0)`. The shader never sees Kelvin.
* A light whose position, colour, intensity or range is not finite is **dropped, not corrected**, with the value in the warning. It is uploaded black.
* `cone.w` is a bit field: `castsShadow` (only when a shadow view was allocated), `cascaded`, `area` (Rect/Disk/Tube/Sphere), `cube`.
* `cone.z` is the index of the light's first shadow view, from `ShadowRenderer::viewForLight`, or -1. Which lights get one is the Shadow Lab's table.

### 1.3 Reach — the hard edge

`rendering::lightInfluenceRadius` (`src/rendering/light_data.cpp`)

This is the number that decides how far a local light is *evaluated at all*, and the word that
matters is **hard**: past this radius the froxel pass does not put the light in any cluster, so
whatever it was still contributing stops within the width of one froxel. It is not a fade.

    Directional            0 (infinite; never enters the grid)
    explicit range > 0     the range -- and the shader's own window has already closed there
    otherwise              sqrt(peak / cutoff) + half the emitter's largest extent, cutoff = 0.004

where `peak` is the brightest channel of the light **as the shader sees it**: colour, times the
colour-temperature tint, times intensity, times `scene::emitterArea` for the four area kinds. The
last two factors were added by ADR-272 and are the defect §3.1 is about.

### 1.4 The froxel grid

`rendering::ClusterGrid` (CPU reference) and `shaders/clusters.wgsl` (the pass). 16 x 8 x 24 =
3,072 froxels, at most 32 lights each, at most 256 scene lights. Depth is sliced exponentially
(Olsson et al. 2012) so a slice is a constant ratio of the one before.

The grid has two halves and they are checked separately:

| half | code | CPU reference | checked by |
|---|---|---|---|
| **build** — which lights reach froxel (i,j,k) | `cs_build` | `assignClusters` | `test_shadows_gpu.cpp`, index for index |
| **look-up** — which froxel does this fragment read | `clusterIndexFor` (`shaders/lighting.wgsl`) | `ClusterGrid::clusterOf` (**new**, ADR-272) | `test_lighting_lab.cpp`, and the GPU probe in §2.2 |

The look-up half had no reference and no test. The build pass is indexed by its own invocation id,
so comparing the lists it builds against `assignClusters` says nothing about which list a *pixel*
then reads: a grid whose rows ran the wrong way up would pass that comparison exactly. The screen's
y runs down from the top-left and the grid's rows run bottom-up in NDC order; the flip between them
is one line of `clusterIndexFor` and it was the one line nothing could fail on.

**The cap is resolved in light-buffer order.** `assignClusters` and `cs_build` both fill a cluster's
list in index order and stop at 32, so the light that is dropped from a crowded froxel is the one
latest in the buffer, whatever its brightness or its distance. `rendering::lightAssignments`
(**new**) is that measurement per light: how far it was told it reaches, how many froxels it
touches, how many kept it, and how many dropped it.

`SceneRenderer::updateLights` (`src/rendering/scene_renderer.cpp`) builds the grid from
`scene.camera.nearPlane`, `farPlane`, `effectiveFovY()` and the render aspect, writes the uniform,
and dispatches. It is also where `--cluster-stats` fills `RenderStats::clusters`
(`ClusterOccupancy`, ADR-114) — off by default, because it is real CPU work inside a measured frame.

### 1.5 Evaluation, per light

`evaluateLight` (`shaders/lighting.wgsl`), switching on `positionType.w`:

| kind | path | attenuation |
|---|---|---|
| Directional | `shadePunctual` | 1 |
| Point | `shadePunctual` | `1/d²`, times the range window `(1 - (d/range)⁴)²` when a range is set |
| Spot | `shadePunctual` | the same, times `clamp((cos θ - cos outer) / (cos inner - cos outer), 0, 1)²` |
| Rect, Disk | `shadeArea` — linearly transformed cones (ADR-033) | none; the polygon form factor *is* the falloff. The range window multiplies it when set. |
| Tube, Sphere | `shadeRepresentative` — representative point (Karis 2013) | `area / d²`, times the range window |

Two conventions meet here and both matter downstream:

* **`intensity` means candela on a punctual light and nits over the emitter on an area one.** The
  conversion between them is the emitter's area, which is `scene::emitterArea` — Rect is `w × h`,
  Disk and Sphere are `π r²`, Tube is `2 r × length`. `LightRig::expand` divides by it; `shadeRepresentative`
  multiplies by it; `shadeArea` does it geometrically through the form factor. They agree: a facing
  surface at distance d receives `albedo × L × A / (π d²)` from either path.
* **A Disk is integrated as the square of equal area** (side `r√π`), on the CPU (`areaLightCorners`)
  and on the GPU (`areaCorners`), so the two never disagree about what a disk is.

`extra.x` (diffuse only) zeroes the specular, `extra.y` (specular only) zeroes the diffuse, and the
shadow term (Shadow Lab) multiplies both at the end.

### 1.6 The BRDF

`shadePunctual`: GGX (`distributionGgxL`), height-correlated Smith (`visibilitySmithL`), Schlick
Fresnel, Lambert `/π` diffuse with `(1 - F)` energy conservation. `alphaOverride` is a parameter
rather than `ctx.alpha` because `shadeRepresentative` widens the lobe by the emitter's angular size
and normalises the energy back.

There is a **second, entirely separate shading model**: when `scene.environment.stylized` is set,
`frame.lightCounts.z` is 1 and every light goes through `shadePainterly` — a three-band ramp with a
smoothstep highlight and no Fresnel. It is the first branch of `shadePunctual`, so it replaces the
BRDF for every kind, and any measurement of material or normal response has to say which model it
was taken under.

### 1.7 The LTC table

`buildLtcTable` (`src/rendering/light_data.cpp`), 32 x 32, generated at startup and never shipped
as data. Two RGBA32F textures at group 0 bindings 8 and 9: the inverse transform, and (directional
albedo, Fresnel bias) for the split-sum specular. Indexed by (roughness, n·v) with a half-texel
inset.

### 1.8 What else is added to the pixel

Direct lighting is one term of several, and a lighting question that ignores the others is a
question about the wrong number. From `pbr_shade.wgsl`:

    color = fog( direct + ambient + emissive + rim + worldEffects + skyLit )

* **ambient** — image-based when `frame.envParams.w > 0.5`: irradiance for diffuse, the prefiltered
  cube for specular, the BRDF LUT for the split sum, scaled by `frame.params.w`
  (`environmentIntensity`, or the sky's own intensity when the IBL came from the procedural sky).
  Otherwise a **hardcoded** hemisphere — `sky = (0.10, 0.12, 0.20)`, `ground = (0.02, 0.015, 0.03)`,
  times 0.8. That fallback is not reachable from any scene setting, and it is the floor that made
  the first version of this lab's GPU probe unable to see a light switch off (§2.1).
* **emissive**, **rim** — material, not lighting.
* **skyLit** (ADR-230) and **worldEffects** (ADR-207) — additive radiance from the atmosphere and
  world-effect systems, resolved on the CPU. The Volumetric Lab owns both.
* Then the post chain, which is the HDR Lab's.

### 1.9 The material tier caps the light count

`rendering::MaterialTierSelector` (ADR-133), per draw, wave-uniform:

| tier | local lights | shadow | specular |
|---|---|---|---|
| Full | every light in the froxel | full | yes |
| ReducedLights | `QualitySettings::reducedTierLocalLights` (4 preview / 6 default / 12 offline) | full, no contact march on **local** lights | yes |
| Flat | `flatTierLocalLights` (1 / 2 / 12) | none at all | none |

Directional lights are never capped. The subset a capped fragment evaluates is the froxel list's
leading entries, which is buffer order again — deterministic, and the same two lights every frame.

### 1.10 The fallback tier

When `QualitySettings::clusteredLighting` is off, `directLighting` takes an entirely different
path: `frame.lights`, a uniform array of **eight** `LightUniform`s packed separately in
`SceneRenderer::render`, no clusters, no shadows and no area kinds. It is the pre-ADR-033 renderer,
kept as an A/B arm, and §2.2 is the probe that holds the two paths to each other.

---

## 2. What was measured

### 2.1 An area light's reach, seen in a frame

`tests/rendering/test_lighting_lab_gpu.cpp`, "an area light with no authored range reaches as far as
one that has it". A corridor of diffuse floor seen edge-on, one Rect light at the near end, no sky,
no second light, **post disabled**. The middle column of the image is the fall-off curve, and
`AVGEN_LAB_DUMP=1` prints it.

**The first two versions of it passed with the defect in place.** Both failures are worth writing
down, because each cost a run that proved nothing, and the first version looked for the sharpest
ratio between neighbouring rows of that one image:

1. **The post chain has to be off.** AgX plus the exposure meter compressed a 0.76 step in
   scene-linear radiance into 0.08 of display luminance — under any threshold that is not also
   triggered by ordinary fall-off. A lighting probe read through the tone curve is a probe of the
   tone curve. `--disable post` is the boundary between this lab and the HDR Lab, and it is drawn
   in code rather than argued about.
2. **The emitter has to be large.** The radiance a rect light still carries where a reach computed
   from its radiance alone runs out is `cutoff × area / π` — *independent of its intensity*, because
   the reach and the radiance scale together. A 6 x 4 m softbox leaves 0.031 behind, which is under
   the hemispheric ambient floor of §1.8 and reads as nothing. A large panel leaves enough.

So the probe as it now stands is an **A/B on the reach itself**, not a hunt for a step inside one
frame — which also removes the exposure meter's per-frame adaptation from the comparison: the arm is a 40 x 28 m panel with `range = 0`, the control is the identical light with an
explicit 600 m range whose own window is within 0.05% of 1 everywhere in shot, and the difference
between the two frames is exactly the light the heuristic threw away. Measured:

| | worst channel difference | mean over the frame |
|---|---|---|
| before ADR-272 | **48** of 255 | 0.261 |
| after | 1 | 0.00034 |

Both numbers are in the assertion because both had teeth and they say different things. A threshold
on the mean alone would have passed with the defect in place — the far half of a corridor seen
edge-on is a small share of the image, which is exactly the mistake a whole-frame metric invites.

### 2.2 The froxel a fragment reads

`tests/rendering/test_lighting_lab_gpu.cpp`, "the clustered path and the uniform fallback agree on
an off-centre point light".

The existing clustered test in `test_shadows_gpu.cpp` compares the two paths for **one directional
light**, which never enters the grid at all. That is this suite's centred box (ADR-182) exactly: the
test exercises none of the cluster look-up and would pass identically with the grid's rows upside
down. This probe uses a point light off-centre in x, off-centre in y and off the view axis in z,
with a floor and a back wall so a flip cannot cancel on a single plane, and requires the mean level
of the clustered frame to be bright enough that "the two agree" is not two black images agreeing.

### 2.3 Structural counts, not milliseconds (ADR-170)

`rendering::lightAssignments` and `ClusterOccupancy` are the lab's performance instruments, and they
are counts: froxels touched per light, froxels admitted, froxels crowded out, and the per-cluster
demand before the cap saturates it. They are exact, they are the same test the GPU pass makes, and
they do not depend on what else was running on the machine.

The reach fix of §3.1 makes some of those counts larger — that is what it costs, and it is a count
rather than a time so it can be stated: a Rect light's touched-froxel count scales with its reach
squared, and the reach now scales with `sqrt(area)`.

---

## 3. Defects

### 3.1 A light was cut off while it was still lighting things (ADR-272)

**Symptom.** A hard edge across a lit surface, at a fixed distance from a local light, in a
fall-off that should be smooth.

**Root cause.** `lightInfluenceRadius` computed the reach from `color × intensity` — the value the
author typed — while the shader multiplies by two more factors before any radiance leaves the
light: the colour-temperature tint (`colorTemperatureToRgb` is normalised to luminance 1, so a
2000 K practical's red channel is well above 1.0), and, for the four area kinds, the emitter's
area, because their `intensity` is nits rather than candela. So the reach was short by
`sqrt(tint)` and by `sqrt(area)`, and the light stopped being assigned to froxels while it was still
contributing `cutoff × area / π`.

**Measured, before the fix**, by `tests/unit/test_lighting_lab.cpp` over ten deliberately awkward
lights (the radiance each still carried at the reach it was given, against a cutoff of 0.004):

| light | radiance at its reach | over |
|---|---|---|
| white point at the origin (**the control**) | 0.00400 | 1.0x |
| point at 2000 K | 0.00952 | 2.4x |
| point at 11000 K | 0.00563 | 1.4x |
| 6 x 4 m rect | 0.07364 | 18.4x |
| 2.5 m disk | 0.07126 | 17.8x |
| 1.4 m sphere | 0.02368 | 5.9x |
| 8 m tube | 0.00757 | 1.9x |

And in the other direction: an 8 cm x 5 cm rect at 40 nits was given a reach **four times** further
than the distance at which it fell under the cutoff — froxel slots spent on a light that cannot
light anything there, which is what pushes another light out of a crowded cluster's 32.

**Fix.** `peak` is now computed from what the shader is handed. `scene::emitterArea` was promoted
out of `light_rig.cpp`'s anonymous namespace into `scene_types.hpp` beside `colorTemperatureToRgb`,
so the rig's nits-to-candela conversion and the reach's candela-to-distance conversion use one
area rather than two copies of it.

**Regression.** "a light's packed reach outlives its contribution" (CPU) and "an area light with no
authored range reaches as far as one that has it" (GPU). The CPU probe failed on six of ten
lights before the fix and passes on all ten after; the white control point passed throughout, which
is what says the other six were being measured.

**Who is affected.** Not rigs and not the ecology — both set an explicit range. Lights imported
from glTF (`range` is optional in KHR_lights_punctual and is usually absent) and any light built in
code, which is every light in `tests/rendering/`.

### 3.2 A non-finite light reached the froxel pass after being dropped from the light buffer

**Root cause.** `packLight` refuses to upload a light whose state is not finite, and says so. But
`SceneRenderer::updateLights` asks `lightInfluenceRadius` *separately*, on the scene light, for the
cluster uniform's radius — and that function had no such refusal, so a NaN intensity produced a NaN
radius. It was not visible, because every comparison a NaN takes part in answers false and the pass
therefore assigned the light to nothing; but "harmless because every test of it fails" is a property
nobody should be relying on, and `clusterOccupancy` and the new `lightAssignments` both count on
`radius > 0` meaning something.

**Fix.** The same refusal, in the same words, in `lightInfluenceRadius`.

**Regression.** "a light nobody can describe reaches nothing", with the finite version of the same
light as the control.

### 3.3 A rig light's `cone` was authorable under a name the parser ignores

Reported here, **fixed in ADR-278**. `LightRig::fromJson` reads the spot angle from `"cone"`; a
file that wrote `"coneDegrees"` got the 45-degree default and no error, because the rig parser
ignored unknown keys. This lab's own fixture was written with the wrong key first and the only
thing that caught it was reading the parser. It is ADR-225's defect in an authoring format and it
applies to every key in a `.rig.json`.

The key is still ignored — that is what "unknown" means — but it is no longer silent:
`json_keys::warnUnknownKeys` names the key, the light and the rig, at the rig's root and in each of
its lights, and `scene::rigFileKeys()` / `scene::rigLightKeys()` are public so a test can hold the
list against all 21 rigs that ship. The same check covers a scene file's top level, its
`environment`, its `environment.sky`, a `"lights"` entry and a node's `animation` block.

---

## 4. The fixture, the overlays and the cases

### 4.1 The fixture

`examples/labs/lighting-lab.scene.json` + `examples/lightrigs/lighting-lab.rig.json`.

Flat, fog-free, fixed-seed, and every light placed in **world** coordinates (`followCamera: false`)
so the camera can be moved in a case without moving the experiment. The rig is sized by the
fixture's `composition.focalPoints` entry — (0, 1.5, 0), radius 4 — which is load-bearing: without
it the rig would be sized from the 256 m terrain and every local light would sit hundreds of metres
out, contributing almost nothing. Distance 2.0 radii is therefore 8 m and every local light's range
is 48 m.

Ten lights, all seven kinds:

| | why it is there |
|---|---|
| `sun`, `ambient` (Directional x2) | the two that never enter the froxel grid — the control for any cluster measurement |
| `point-a`, `point-b` | 15 degrees apart at the same distance: the only place in the fixture where cluster occupancy is 2 |
| `spot-narrow` | a 16-degree cone, which is where the sphere-shaped influence volume is at its least honest (§5.3) |
| `rect-large`, `rect-small` | the same azimuth, elevation and intensity ratio; 3.0 radii against 0.06. Emitter area, isolated. |
| `disk`, `tube`, `sphere` | the three kinds that are neither punctual nor a polygon |

Two things about the fixture are compromises and are written down rather than hidden. The ground is
a **terrain** surface, chosen because it is what the Shadow Lab uses and what a production frame
actually stands on — and it therefore carries its biome albedo, which is a green-grey rather than a
neutral card. There is no global illumination in this renderer, so it tints only its own pixels; the
surfaces a colour reading should be taken from are the procedural ones, which are neutral by
construction. And `DebugVertex::size` is a **world-space diameter** (`shaders/debug.wgsl`) while
`DebugViewOptions::pointSize` reads like a pixel count and defaults to 3 — a light marker drawn at
`pointSize` is a three-metre ball across the middle of the frame, which is what the first capture of
the `lights` overlay was.

The geometry answers four of the specification's questions and nothing else: `normal-ball` and the
`tilt-NN` fan are normal response; the `rough-NNN` and `metal-NNN` rows are material response at
fixed albedo and fixed size; the post row is fall-off read as a sequence; `far-ball` at 40 m sits
just inside the 48 m range with `far-ball-control` beside it. The three glTF nodes are the Shadow
Lab's own assets at the Shadow Lab's own scales (§29), so a finding about a tree here and a finding
about a tree there are about the same tree.

### 4.2 The overlays

Two new switches in `rendering::DebugViewOptions`, drawn by `buildDebugGeometry`, reachable from
`--debug-draw` and from `labs::overlaysFor(LabId::Lighting)`:

* **`lights`** — every enabled light in its own colour: the position, the direction it travels, the
  **emitter the shading pass actually integrates** (a spot's two cone angles, a rect's quad from
  `areaLightCorners`, a disk's equal-area square *and* its circle, a tube's capsule, a sphere's
  ball), and the sphere of influence. That last ring is the reason the profile is not empty: it is
  the hard edge of §1.3, and before it was drawn the only way to find it was to notice a line on
  the floor. A disabled light is drawn grey rather than omitted — "there is no light here" and "the
  light here is switched off" are different answers.
* **`lightClusters`** — the froxel grid, coloured by how many lights reach each **occupied** froxel,
  white at the 32-light cap. Near faces only; `lightClusterSlice` restricts it to one depth slice.
  Deliberately **off** in the lab's profile: it covers exactly the pixels a lighting question is
  about.

`overlaysFor(LabId::Lighting)` turns on `lights` and `worldAxes` and nothing else. It seeds the
panel, not a headless render, so a case's `reproduceCommand()` is still a clean frame.

### 4.3 The cases

`examples/labs/lighting/cases.json`. Every arm has a control and the controls are named as controls.

---

## 5. What this engine does not have

### 5.1 Area lights *do* exist — this is the section that is not empty

The specification expected area lights to be the missing piece. They are not: Rect and Disk go
through a linearly-transformed-cone fit (ADR-033), Tube and Sphere through a representative point,
the LTC table is generated at startup, and `polygonIrradiance` is a CPU reference for the diffuse
term that the unit suite already checks. What area lights do **not** have is a shadow map that
knows their shape: `ShadowRenderer` gives them six cube faces around their centre, which is a point
light's shadow on an emitter with extent, so a large softbox casts a hard shadow.

### 5.2 There is no per-light isolation arm

`--disable` has arms for whole passes and `--quality-arm` for behaviours, and neither can say
"render this frame with light 4 only". The lab answers "how much did *this* light contribute" by
editing the scene and rendering twice, which is what the GPU probes do. A per-light arm is the
single most useful thing that could be added next: it would make "which lights reached this pixel"
answerable from a frame rather than from a diagnostic, which is §37's own preference.

### 5.3 A light's influence volume is a sphere, whatever shape the light is

`clusterTouchesSphere` is the only test the grid makes. A spot is assigned to every froxel within
`range` of its **position**, in every direction including behind it, and the shader then multiplies
almost all of them by a spot term of zero.

Measured, by `tests/unit/test_lighting_lab.cpp`: **a 16-degree spot at 40 m range is assigned to
2,618 of the grid's 3,072 froxels, and its cone reaches 57 of them.** Forty-six times more than it
can light, and eighty-five per cent of the whole grid, for one lamp. The reachable count is
deliberately generous — a froxel counts when the angle to its centre minus the angular radius of
its bounding sphere is inside the cone, which over-counts — and the control is the identical sphere
belonging to a **point** light, whose cone is the whole sphere and whose ratio is therefore exactly
one, computed by the same code.

It is a count rather than a millisecond (ADR-170), and it is the instrument that would size a
cone-aware or OBB assignment before anyone wrote one. Not attempted here: a tighter assignment test
that is wrong in one corner is a missing light, and this lab's first job was to stop cutting lights
off, not to start.

### 5.4 There is no light culling against the camera

No frustum test, no distance test, no importance ordering. A light behind the camera is packed,
uploaded, and offered to the grid, where the froxel test rejects it. With 256 lights that is 256 x
3,072 sphere-box tests per frame on the GPU, which is cheap, and 256 of 256 slots spent, which is
not.

### 5.5 There is no shadow for a local light unless it was given a view

The atlas is eight layers and the Shadow Lab's table says who gets them. Every other light's only
shadow is the screen-space contact march, per ADR-034 — and on the ReducedLights tier local lights
do not get that either.

### 5.6 There is no light-linking, no light channels and no per-object light lists

Every light in a fragment's froxel is evaluated for every drawable in it. `diffuseOnly` /
`specularOnly` are the only per-light contribution controls.

---

## 6. How to use it

    avgen --labs                                     # the ownership map, printed
    avgen --lab-case lighting:1                      # the fixture, at the case's time, with the lab's overlays
                                                     # (it logs `reproduce: avgen --headless ...` as it opens)

`docs/engineering-labs.md` §7 advertises `avgen --lab-case <lab>:<n> --print`. There is no `--print`
flag and the binary rejects it. `--lab-case` logs the reproduce command as it opens, from the same
`labs::reproduceCommand`, which is the string that flag was meant to print.
    avgen --headless --composition examples/labs/lighting-lab.scene.json \
          --size 1280x720 --range 0.5:0.5 --render out --debug-draw lights
    avgen ... --cluster-stats --bench-json out.json  # froxel occupancy, ADR-114 (it reports
                                                     # through the benchmark record, and the
                                                     # record says it perturbed the wall clock)

    build/release/tests/avgen_tests "[lighting][lab]"
    tools/gpu-lock.sh build/release/tests/avgen_render_tests "[lighting][lab]"
