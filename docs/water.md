# Water

How a world's water is described, shaded, inhabited and made to answer the music. The decisions and
the reasons are in [ADR-099](decisions/ADR-099-water.md); this is the authoring reference.

Three things own three different parts of it, and the split is worth holding on to:

| | owns | lives in |
|---|---|---|
| `world::WorldMap` | *where* water is: `waterSurface(p)`, `Sample::submerged` | `world/world_map.hpp` |
| `world::WaterCourse` | *which way it runs*: a downstream-ordered centreline, width, depth, descent | `world/terrain_water.hpp` (ADR-090) |
| `world::WaterBody` | *how its surface moves*: speed, shear, meander, turbulence, arc length | `world/water.hpp` |
| `scene::WaterSettings` | *what it looks like* | `scene/water_surface.hpp` |

Nothing authors a river twice. A `WaterCourse` is derived from the map's own features, a `WaterBody`
is built from a course, and the water mesh's flow lanes are baked from the body. Move the river in
the world and everything downstream of it moves with no edit.

## Authoring a surface

Water settings live on a terrain node, beside the terrain that holds them:

```json
{ "name": "valley", "kind": "terrain",
  "terrain": {
    "chunkSize": 40.0, "resolution": 32,
    "flow":  { "speedScale": 1.0, "bankShear": 0.7, "meander": 0.18, "turbulence": 0.25 },
    "water": { "enabled": true,
               "shallowColor": [0.05, 0.22, 0.24], "deepColor": [0.006, 0.035, 0.075],
               "shallow": 2.0, "clarity": 1.25, "edgeFade": 0.8, "maxOpacity": 0.93,
               "fresnel": 0.30, "reflection": 8.0, "reflectionTint": [0.55, 0.74, 0.98],
               "specular": 3.0, "roughness": 0.17,
               "ripple": 1.0, "rippleScale": 0.42, "rippleSpeed": 1.0, "chop": 0.35,
               "foam": 0.6, "foamWidth": 0.22, "refraction": 0.25,
               "glow": 1.2, "glowColor": [0.15, 1.0, 0.75], "glowCoverage": 0.22,
               "glowScale": 0.16, "glowDepth": 0.55,
               "sparkle": 0.6 } } }
```

### The four knobs that decide whether it reads as water

Everything else is trim. These are the ones to reach for first.

**`fresnel`** — reflectance at normal incidence, and deliberately not water's real 0.02. At the angle
a camera looks at a river from, physical water returns five per cent of the sky and ninety-five per
cent of whatever is on the bed; at night that bed is black, and the result is the flat dark ribbon
this whole pass exists to replace. 0.25–0.35 for a night scene, 0.08–0.15 for daylight where the sky
is doing the work already.

**`reflection`** — multiplies the environment sample. High numbers here are not cheating so much as
compensating for a dim sky: Glowmere's night sky needs 8 to put a legible sheen on the water, and a
daylight lake wants 1.

**`clarity`** — metres of water the bed stays visible through. This is what makes the surface a
volume rather than a sheet, because the opacity of every pixel comes from how much water the view
ray crosses. Small numbers (under a metre) give a murky pool; large ones (four or five) give a clear
mountain stream you can see the stones in.

**`ripple` / `rippleScale`** — how much the surface moves and at what size. `rippleScale` is the
*coarsest* layer's frequency in cycles per metre; the two above it are derived from it, so 0.4 gives
a two-and-a-half-metre swell with chop and capillary detail on top. Halve it for a lake, double it
for a brook.

### The rest, in the order they matter

| key | what it does |
|---|---|
| `shallowColor` / `deepColor` / `shallow` | the colour ramp over `shallow` metres of depth |
| `edgeFade` | metres of depth the surface fades out over at the bank |
| `maxOpacity` | the deepest water still lets a little through; 1 reads as paint |
| `roughness` | how blurred the reflection is, and how wide the moon's glint |
| `specular` | the moon's own glint over the reflection |
| `chop` | how much the ripple layers travel *across* the flow as well as along it |
| `foam` / `foamWidth` / `foamColor` | the surf band at the waterline, broken up by the ripple field |
| `refraction` | metres of world-space wobble in the shoreline and the depth colour |
| `glow` / `glowColor` / `glowCoverage` / `glowScale` / `glowDepth` | bioluminescence under the surface |
| `sparkle` / `sparkleColor` | glints riding the finest ripples |
| `swell` | metres of whole-surface rise; 0 unless a signal is driving it |
| `cascade` | whitewater on steep water (a falls), in the foam's colour; 0 = none. See [Steep water](#steep-water) |

`glowCoverage` is a *threshold*, not a multiplier: turning it down removes patches instead of dimming
the river, which is the difference between "something is glowing under there" and "the river is
green". 0.2 is about right for the brief's "occasionally notice"; past 0.5 the channel lights up.

Two things hold for every setting above:

- **Nothing drifts further than eight seconds of its own travel** ([ADR-914](decisions/ADR-914-the-waters-advection-is-bounded.md)).
  Each travelling field is two samples half a period apart, crossfaded, so the surface at the end of a
  four-minute film has the character it had at the start. (It used to streak as the film went on,
  wherever the baked flow changed between two vertices.)
- **The fades count reference pixels** ([ADR-915](decisions/ADR-915-the-waters-fades-count-reference-pixels.md)):
  a 1080-row frame's. A preview at 960x540 with 2x supersampling and a final at 1920x1080 or 4K fade
  the same ripples, so tune on previews and trust the final. Below 1080 rows the frame's own pixels
  still decide, because finer detail can only alias.

## Tears

Thin, stepped seams across the surface in which the ripples are compressed into dense parallel
stripes ([ADR-916](decisions/ADR-916-deliberate-water-tears.md)). Off by default; at `tears` 0 the
surface is drawn by a pipeline with the tear code compiled out, byte-identical to one without them.

```json
"water": { "ripple": 0.1, "rippleScale": 2.6,
           "tears": 0.5, "tearShear": 4.0, "tearCoverage": 0.4,
           "tearCell": 1.2, "tearSpacing": 40.0, "tearStretch": 3.0,
           "tearDirection": "wind", "tearDrift": 0.15, "tearWind": 0.6 }
```

Every setting is a control. In the app they sit under one heading, **tears**: the Parameters panel's
**nodes** group, sub-group **`<terrain>/water/tears`**, and the World panel's Inspector with the terrain
selected, **water** > **`tears/...`**. Each row is labelled for what it does to the picture.

| key | what it does | control (`nodes/<terrain>/water/tears/...`, label) |
|---|---|---|
| `tears` | ripple amplitude a seam adds on top of `ripple`; 0 = no tears. Its first 0.25 also brings the shear in, so a route off 0 grows the seams rather than snapping them | `amount`, "amount"; routable |
| `tearShear` | metres of ripple packed into a band (0-8). `tearShear / tearCell` is the compression and `tearShear * rippleScale` the stripe count: the reference is about 3x and ten stripes | `shear`, "shear (m)"; routable, slow chains only |
| `tearCoverage` | roughly the share of the seam network that shows (0 none, 1 all) | `coverage`, "coverage"; routable, slow chains only |
| `tearCell` | metres: the lattice the seams step along. A band is one cell wide and steps along the lattice's axes and diagonal, as the accident stepped along the water mesh | `cell`, "step size (m)" |
| `tearSpacing` | metres between neighbouring seams (at least two cells) | `spacing`, "spacing (m)" |
| `tearStretch` | how many times longer a seam runs along its direction than across (0.25-20) | `stretch`, "stretch" |
| `tearDirection` | `"wind"` (the scene wind's steady direction) or an angle in radians about +Y (0 = +X, pi/2 = +Z), within a turn either way | `followWind`, "follow the wind", and `direction`, "direction (radians, if not following the wind)" |
| `tearDrift` | metres per second the whole lattice drifts along its direction | `drift`, "drift (m per second)" |
| `tearWind` | how much the local wind's strength and gust envelope tighten the seams (0-1); inert with no scene wind | `wind`, "wind (gusts tighten the seams)" |

**Why only three are routable.** The other seven are controls a route cannot drive
(`modulatable = false`; a route to one is refused at bind). The lattice is anchored at the world
origin, so changing the step size, spacing or stretch rescales it about the origin and changing the
direction turns it there: 350 m out, a 1% change moves a seam 3.5 m, and a route would make every seam
jump. The drift multiplies the timeline second. The wind coupling is a choice, not a signal: to make
the music move the seams through the wind, route the wind (below). A timeline track can still key
any of them; `avgen --audit-routes` then reports a varying key on the drift, or on the others while
the lattice drifts, as a `phase-rate` hazard, and for the same reason reports `scene/windDirection` as
one while drifting seams follow the wind (ADR-902's registry). A tear setting of a water whose amount
is 0 with nothing to lift it, and the fixed direction of seams that follow the wind, are reported as
`tear-setting-unread`: those draws never read them.

**Routing.** Past its first quarter `water/tears/amount` only adds slope along the seams, so a fast
route (audio.bass, a 100 ms attack) flashes them and moves nothing -- give it a base of at least 0.25
so the shear is already in. `water/tears/shear` and `water/tears/coverage` change what is packed into a
band, so they want chains of a second or more. The wind reaches the tears without a route: with `tearWind` above 0, a gust front
crossing the water tightens the seams it crosses, in step with the grass on the bank; routing
`scene/windSpeed` or `scene/wind/gustAmount` therefore reaches them too.

**Seeing them.** A band's stripes show only where the reflection has contrast -- in the moon's glint,
against a bright stretch of sky -- and only where they are at least a few reference pixels apart. Further
out they fade, and the band reads as a smooth, slick line through the rippled water; narrower than a
couple of reference pixels, the whole band fades out. Close shots over a lit surface carry them best.
The reference's 1.2 m step reads only a few metres from the lens: for water seen from tens of metres
(Glowmere Valley 3's framings), a step of about 3 m at a compression (`tearShear / tearCell`) of about
2 keeps the packed stripes in the near water, and the seams become slick lines further out.

## Steep water

Water whose surface falls more than 20 degrees along its own flow -- a falls, a flume down a hillside -- is
shaded about its own plane ([ADR-985](decisions/ADR-985-water-on-a-steep-course-is-a-cascade.md)), fully past 30
degrees and blended between, so nothing switches at a line. "Along its flow" because the mesh's bank has slivers
that tilt across the flow, and those are not cascades. The face's slope comes from the surface's own derivatives
(one value per triangle of the water mesh), and four things follow from it:

- which side of the surface the camera is on is the plane's side, so a camera below a falls sees its upper
  side (it used to see the part above its own height as a ceiling from under the water);
- the depth that decides colour, opacity and the shoreline is measured across the sheet, not straight
  down (straight down, a falls seen from its foot read as a shore and went half transparent);
- the ripples are turned onto the slope, so the moon and the sky light it as a slope;
- the tears fade out: their lattice lies in XZ and stood up on a slope reads as brickwork.

`cascade` adds whitewater: streaks along the flow, in the foam's colour and composited as foam is,
running three times the water's own speed and thinning out with the shoreline. Where a streak is too
fine to resolve the whitewater fades to its mean, so a distant falls is a pale ribbon rather than a crawl.
Flat water never takes any of this path: under 20 degrees the surface is byte-identical to the shader
without it, `cascade` included (`test_water_cascade_gpu.cpp`).

```json
"water": { "foamColor": [0.7, 0.88, 0.95], "cascade": 0.35 }
```

## Flow

```json
"flow": { "speedScale": 1.0, "speedOverride": 0.0, "bankShear": 0.7,
          "meander": 0.18, "turbulence": 0.25,
          "stillFactor": 0.12, "stillSpeed": 0.55, "windDirection": [0.7, 0.7] }
```

§11 asks for `flowDirection`, `flowSpeed`, `flowStrength` and `turbulence`. They are here, spelled
for where the answers actually come from: direction and speed are the course's, `speedScale` and
`bankShear` are the strength, and the variation is split between `meander` (the direction wanders)
and `turbulence` (the speed does, so a river has fast reaches and slow pools). Both are *spatial*
and not temporal, because a river does not speed up and slow down in place -- it has fast stretches,
and the pattern moves through them.

There is **no `flowDirection` and no `flowSpeed`**, on purpose. Direction is the direction the bed
falls, read from the course. Speed comes from the course's own gradient — a steep short river runs, a
long flat one drifts — and `speedScale` multiplies it. `speedOverride` (metres per second, 0 = off)
is there for the shot that has to contradict the terrain.

A body with no descent is a pond, a lake or a sea. It gets a slow wind-driven drift at
`stillSpeed * stillFactor` along `windDirection` instead of a downstream direction, because a perfect
mirror reads as glass rather than as water.

`bankShear` is how much of the centreline speed is lost at the bank, on a parabolic profile — the one
an open channel actually has. It is why a leaf in midstream overtakes one at the edge, and it costs
nothing because the floating layer reads the same profile.

Each terrain node logs what it derived, which is the quickest way to see a river is running the wrong
way:

```
terrain 'valley': 256 chunks (21 with water), 589824 triangles at LOD 0, built in 48 ms
  water 'glowmere-run': river 516 m, half width 7.0 m, descent 39.0 m, 0.91 m/s, heading (0.50, 0.86)
  water 'west-tarn-bed': pond 0 m, half width 30.0 m, descent 0.0 m, 0.07 m/s, heading (0.71, 0.71)
```

## Flow streaks (ADR-1643)

A long exposure of moving water: fine streak lines drawn out along the river, in palette colours, curving with every
bend. The streak lines are the river's own flow lines: the lanes of position across the channel that the surface
already carries. So they follow the course exactly and converge where it narrows. Each lane is lit in stretches carried
downstream at the river's speed.

```json
"water": { "flowStreaks": 2.5, "streakLines": 320, "streakLength": 25, "streakDensity": 0.55, "streakHighlight": 1.2,
           "streakColor1": [0.95, 0.45, 0.8], "streakColor2": [0.3, 0.95, 0.85], "streakColor3": [0.7, 0.55, 1.0] }
```

| key | default | meaning |
|---|---|---|
| `flowStreaks` | 0 (off) | brightness (0..20). Above 0 the surface is drawn by the cinematic pipeline with the streaks in |
| `streakLines` | 160 | lanes from bank to bank (4..2000). The channel's width / lines is a streak's width |
| `streakLength` | 12 | metres a lit stretch runs along the flow, roughly |
| `streakDensity` | 0.35 | the share of each lane's length that is lit (0..1) |
| `streakSpeed` | 1 | how fast they travel, times the water's speed |
| `streakHighlight` | 0.5 | white at the brightest lanes' cores |
| `streakColor1/2/3` | pink, teal, lavender | the lanes' colours |
| `streakColorScale` | 0.02 | cycles per metre at which the lanes' colours drift, so the two banks differ |

**Parameters** (routable, so a palette or the music can drive them):
- `nodes/<terrain>/water/streaks/amount`;
- `nodes/<terrain>/water/streaks/color1`, `color2` and `color3`.

**Where they appear:**
- Flowing water only: a still pond has no current to draw out.
- They stay off the banks' very edge.
- A narrow band down the centreline (about 6% of the channel) is left calm. The vertex carries 1 - |across|, and its
  interpolation across the centreline is a sawtooth that would zigzag the lanes there.

**How they render:**
- A lane narrower than about a 1080p pixel fades to its mean, so a distant reach reads as a soft colour, not crawling
  lines.
- The motion uses ADR-914's bounded cycle, so it is stable for any length of film.

**Cost:** off, nothing. The code is compiled out, and a cinematic surface without streaks is drawn by a shader without
it. On, about 0.1 ms on the QA river at 1080p.

## Sky mirror (ADR-1650)

The sky the frame drew, in the water: an aurora, a comet, the sky layer itself. The screen-space reflection cannot find
the sky (its depth is 1e7, so no step of the march lands behind it) and the environment cube has no sky effect in it,
so without this a river under an aurora reflects none of it. With it, where a reflected ray points up, the surface reads
the copy of the frame at that direction's vanishing point on screen (`viewProj * (r, 0)`): the sky is at infinity, so
that is where the frame shows what the ray sees, whatever the surface's height.

```json
"water": { "ssr": 1.0, "ssrDistance": 700, "ssrSteps": 16, "skyMirror": 1.0 }
```

| key | default | meaning |
|---|---|---|
| `skyMirror` | 0 (off) | how much of the reflection the drawn sky supplies where it is trusted (0..1). Above 0 the surface is drawn by the cinematic pipeline with the mirror in |

**Parameter** (routable): `nodes/<terrain>/water/skyMirror`.

**Where it is trusted:**
- the vanishing point is on screen, faded out over the last 6% of the frame, so a ripple that throws it off an edge eases
  back to the environment reflection instead of smearing the edge;
- the reflected ray points up;
- what the copy shows there is far: beyond half `ssrDistance` (the sky, or a distant range). A near bank at that point is
  not what the ray sees; the march finds those.

**With the SSR:** the march's hits still win over it, and its far fade hands over to the mirror. Its brightness is the
SSR's (`ssrGain`), so the two meet without a seam. It does not need the SSR: with `ssr` 0, or the live ladder's
Emergency rung (`waterSsrScale` 0), the sky stays in the river.

**Framing:** a water pixel's mirror direction is as far above the horizon as the pixel is below it. A camera with the
horizon near or above the frame's centre sees every water pixel's mirror on screen; pitched well down, the mirror points
fall off the top and those pixels keep the environment reflection.

**Tint patches** (`tintPatches`) recolour the reflection after the mirror, so they tint the aurora's reflection too.

**Cost:** off, nothing (compiled out). On, one tap of the frame copy and one of the depth per water pixel; see ADR-1650
for the measurement.

## Water on an imported mesh (ADR-2020)

A lake that is part of an imported GLB can be drawn as water too. Name its glTF material in the gltf node's
`waterMaterials`; the value is exactly the `water` object above, read by the same reader:

```json
{ "name": "environment", "kind": "gltf", "asset": "alpine.glb",
  "waterMaterials": {
    "Lake": { "shallowColor": [0.113, 0.25, 0.251], "deepColor": [0.02, 0.09, 0.11],
              "clarity": 6.0, "maxOpacity": 1.0, "edgeFade": 0.6, "fresnel": 0.04, "reflection": 1.0,
              "roughness": 0.04, "ripple": 0.03, "rippleScale": 0.05, "rippleSpeed": 0.0,
              "ssr": 1.0, "ssrDistance": 2000, "ssrSteps": 128, "skyMirror": 1.0 } } }
```

- **What it reflects.** Distant terrain comes from the sky mirror: anything past half `ssrDistance` that is on screen at
  the reflected ray's vanishing point. Near things (a bank, a boat, the trees at the shore) come from the march, out
  to `ssrDistance`. A mountain range 3-6 km out needs no long march, so the 2000 m cap is not what limits it. Off-screen
  sources fall back to the environment cube, as with any screen-space reflection, so a camera pitched well down shows
  cube in the lower lake.
- **Matching.** The match is exact and case-sensitive on the glTF material name (what Blender calls the material).
  Batching merges nodes but keeps material names, so batch freely. A name that matches nothing logs `gltf node 'n':
  waterMaterials 'X' matches no material; available: ...` and is added to the project's warnings.
- **Flow.** The water reads its vertex normal as flow. A flat sheet's normal (0, 1, 0) is direction zero (the shader's
  +X fallback), and its speed fraction is 1 of a still-water 0.05 m/s. Set `rippleSpeed` 0 for a dead-still lake. A
  curved mesh's normals tilt, and the tilt reads as a direction.
- **Shoreline.** Depth thickness and the edge fade come from the scene depth under the surface, so a terrain bed under
  the lake gives the shoreline. A sheet with nothing under it reads as deep water out to its edge.
- **What does not apply.** The node's `program` and `roughnessScale`, skinned entities (skipped with a warning), and
  routes. A gltf water surface has no `nodes/<n>/water/*` parameters. Day/night still drives its reflection and deep
  colour, as it does every surface.
- **Limit.** A scene draws 8 water surfaces in all, terrains included. Asking for more is refused at load.
- **Checking it.** The load logs `gltf node 'n': water material 'Lake' -> N entity(ies), M triangles, bounds y
  [lo..hi], cinematic=yes (ssr 1, skyMirror 1)`. The renderer's first frame with water logs `water: K surface(s), C
  cinematic`.
- **Cost.** It is the cinematic water's cost. On the acceptance scene at 1080p, with half the frame lake, 128 steps
  and the march missing on every far ray, the water's SSR is about 2-3 ms of GPU live (`--ab nowaterssr`). The live
  ladder halves it at Low and drops it at Emergency, and the mirror stays. Measure a real world with `--ab nowaterssr`
  (ADR-2020).
- **The id AOV.** An imported water material's pixels carry its own identifier, so an id AOV shows the lake rather
  than the ground under it (ADR-2021). A terrain's water still leaves the bed's identifier there. The depth AOV is
  the opaque depth in both cases: the bed under the lake.
- **Glossy SSR.** `environment.glossyReflections` never draws a water entity, so a converted lake is not reflected
  twice. Turn it off if nothing else in the scene wants it.

## Shaping the frame's reflection (ADR-2021)

`reflectionTint` and `reflection` colour and scale the *environment's* reflection: the cube, the sky. What the
march and the sky mirror return is the frame's own picture, and three keys shape it. Their defaults are the
reflection as it was, to the byte:

| key | default | meaning |
|---|---|---|
| `ssrTint` | [1, 1, 1] | the march's and the mirror's reflection, multiplied per channel (0..4). A dark, blue lake: [0.64, 0.79, 0.88] |
| `ssrBlur` | 1 | how much `roughness` blurs a march hit (0..4). Roughness is read as a cone angle, so at 1 a still lake at roughness 0.04 blurs a far treeline across about 3% of the frame. 0 is a mirror (the ripples still spread it) |
| `ssrEdgeFade` | 0.06 | the share of the frame's width over which the march and the mirror fade to the environment at the left and right edges (0.001..0.25). The default hides a ripple's lookup leaving the frame. On a still lake it shows the cube's bright horizon as a pale band, and 0.004 removes it |

Any water takes them, a terrain's included. None changes the cost.

**Not reflected, by default:** anything drawn after the water. That means blended meshes (an alpha-blended
waterfall, glass, leaf cards), particles and ribbons. The frame copy the water reads is taken before them.

**`reflectBlended`** (boolean, default false; ADR-2022) puts the blended *meshes* in. When any cinematic surface
in the frame has it on, the frame's blended meshes are drawn a second time into the copy, before the water, so a
translucent fall and its spray appear in the lake with their translucency intact. The frame itself is unchanged.
It costs one more draw of every blended mesh in the frame, and no extra memory (the blend pipelines write only the
colour target). Measure it with `--ab nowaterblend`; a profile shows it as the `waterblend` pass. Particles and
ribbons stay out. A blended mesh *under* the water shows twice: refracted through it, and drawn over it as before.

## Things that float

A `float` block on a **procedural** node makes its instances drift on water instead of standing on
the ground. Everything else about the node is unchanged — an imported mesh, a material, GPU culling,
LOD, the wind deformer — so this is a floating layer of whatever the asset library already has:

```json
{ "name": "river-lilies", "kind": "procedural",
  "procedural": { "source": { "kind": "mesh", "asset": "../../assets/quaternius/glTF/Plant_7.gltf" },
                  "sourceTransform": { "scale": [2.3, 0.5, 2.3] },
                  "distribution": { "kind": "single" },
                  "castsShadow": false,
                  "material": { "program": "paintedFrond", "baseColor": [0.16, 0.44, 0.32] } },
  "float": { "water": "valley", "body": "glowmere-run", "count": 150, "seed": 9101,
             "sizeMin": 0.55, "sizeMax": 1.25,
             "driftScale": 1.0, "driftSpread": 0.38,
             "lateral": 0.82, "margin": 0.24, "minDepth": 0.25,
             "clustering": 0.62, "clusters": 11,
             "spin": 0.05, "bob": 0.035, "bobRate": 0.28, "tilt": 0.09, "sink": 0.015 } }
```

`water` names the terrain node and is **required** — a floating layer with no water is exactly the
kind of thing that silently places nothing, so it is rejected at load rather than at the first empty
frame. `body` narrows it to one water body; leave it empty and the layer spreads over all of them,
which is what a layer of leaves wants.

The numbers that make it read as a river rather than a conveyor belt:

* **`driftSpread`** — how much each instance's speed varies from the water's. At 0 they all travel at
  exactly the same rate and the whole raft reads as one object. 0.3–0.5 is a river.
* **`clustering` / `clusters`** — 0 spreads them evenly along the course, 1 gathers them into knots
  with bare water between. Still water collects leaves in eddies and on the inside of bends; an even
  spread is the tell that a scatter was uniform.
* **`spin`** — radians per second of yaw, with the sign taken from the instance hash. Every pad
  rotating the same way is the second-loudest tell after a uniform speed.
* **`minDepth`** — metres of standing water an instance needs under it, checked against
  `TerrainQuery::waterDepthAt`. A course's banks are a planar test and the bed is not planar; without
  this a pad can sit on a shoal. An instance that lands too shallow walks in toward the centreline
  and is dropped if even that is dry, which is the right answer at a river's head.

The placement is a pure function of the timeline second — no integration, no state — so a seek, a
re-render and a live preview of the same second put every leaf in the same place.

## Reactivity

These properties are ordinary parameters, so a project's `routes` reaches them through the same
`ProcessorChain` as everything else (attack, decay, curve, threshold, depth):

```
nodes/<terrain>/water/glow        bass: more light from underneath
nodes/<terrain>/water/ripple      bass: a slightly rougher surface
nodes/<terrain>/water/swell       beat: one small synchronised lift
nodes/<terrain>/water/sparkle     treble: glints, fast in and fast out
nodes/<terrain>/water/foam        energy: more surf at the banks
nodes/<terrain>/water/flowSpeed   how fast the ripple pattern travels
nodes/<terrain>/water/glowColor   the colour of what is glowing underneath
nodes/<terrain>/water/tears/amount    bass: the seams flash (keep a base of 0.25 or more; see Tears)
nodes/<terrain>/water/tears/shear     sections, slowly: how much a seam packs
nodes/<terrain>/water/tears/coverage  sections, slowly: how many seams show
```

ADR-350 made ten more of them parameters (`clarity`, `maxOpacity`, `fresnel`, `reflection`,
`roughness`, `refraction`, `rippleScale`, `shallowDepth`, `shallowColor`, `deepColor`). Routing
`flowSpeed` or `rippleScale` makes the ripples jump -- the first shifts the pattern by the change times
a cycle's travel (at most 8 s of it since ADR-914), the second rescales it about the world origin -- so
move them only with slow chains; the route audit reports both as `phase-rate` hazards.

Glowmere's amounts are in `examples/world/glowmere-stylized.json` and are deliberately small. The
swell is 5.5 cm on the beat: a river that pumps on every kick has stopped being water.

## Water bodies as splines

A terrain node publishes each body's centreline as a scene spline named `<node>.<body>`. Anything
that takes a spline can then run along the river with no new machinery — a particle emitter, a path
deformer, an instance distribution, a camera track:

```json
{ "name": "river-motes", "kind": "particles",
  "particles": { "shape": "spline", "spline": "valley.glowmere-run",
                 "extent": [4.0, 1.0, 1.0], "position": [0.0, 0.35, 0.0],
                 "spawnRate": 45, "blend": "additive", "emissive": 2.6 } }
```

## What it costs

Numbers, method and the table are in [`world-performance.md`](world-performance.md).

## What is not here

The default surface has no screen-space reflection and no copy of the frame: its sky comes from the
environment cube, and its only refraction is a wobble in the depth the shoreline is read at. The
cinematic pipeline (ADR-1286) adds both. It copies the lit opaque frame just before the water is drawn,
reflects the scene by a screen-space march against that copy (`ssr`), and sees the bed through it with
absorption; the [sky mirror](#sky-mirror-adr-1650) and [flow streaks](#flow-streaks-adr-1643) ride on
the same pipeline, and an imported mesh can use it too ([ADR-2020](#water-on-an-imported-mesh-adr-2020)). There is no planar reflection.

No underwater post-processing when the camera dips below the surface: the surface shades itself
differently from beneath and that is all. No caustics. Each of those is another pass, and the brief
asked for a beautiful stylized surface rather than an ocean simulator.

And one more, which is a *defect* rather than a scope decision: **the cube the reflection samples is
not the sky the camera sees.** Since ADR-345 a scene may light itself from a baked HDRI while
drawing the procedural sky behind it, and `water.wgsl` gates its reflection only on "an IBL is
live" — so at the warm ends of a day/night cycle the sky is a sunset and the water is still
reflecting a photograph of noon. Measured 2026-09-20 on `tree-of-life-ocean-world`, as R−B in sRGB
bytes over bands either side of frame:

| phase | sky R−B | water R−B | gap |
|---|---|---|---|
| dawn | +45.4 | −13.1 | **+58.5** |
| noon | −34.5 | −41.9 | +7.4 |
| sunset | +47.7 | −9.6 | **+57.3** |
| twilight | −7.4 | −34.6 | +27.2 |

Noon is the control: when the map and the procedural sky agree about the sky, the gap is an eighth
of what it is at the ends. ADR-347's `waterReflection` curve turns the reflection down at dawn and
sunset to carry the colour in the water's body instead, and calls itself a workaround.

It is **not fixed**, and ADR-400 has the three costed options and why each is wrong today — the
short version being that the cheap fix would make `water.wgsl` a third hand-copy of the analytic
sky, and the correct fix is a second prefiltered cube with a per-phase re-bake budget. The
measurement above re-runs as
`tools/gpu-lock.sh ./build/release/tests/avgen_render_tests "[water][sky][.probe]"`; it is a
before-table for whoever fixes it and deliberately asserts nothing about the gap.

The [sky mirror](#sky-mirror-adr-1650) works around it on a cinematic surface: where a reflected ray
points up and its vanishing point is on screen, the water shows the sky the frame drew, so a sunset
sky gives a sunset reflection there. Everywhere else, and on every surface without `skyMirror`, the
reflection is still the cube's.
