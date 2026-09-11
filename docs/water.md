# Water

How a world's water is described, shaded, inhabited and made to answer the music. The decisions and
the reasons are in [ADR-091](decisions/ADR-091-water.md); this is the authoring reference.

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

`glowCoverage` is a *threshold*, not a multiplier: turning it down removes patches instead of dimming
the river, which is the difference between "something is glowing under there" and "the river is
green". 0.2 is about right for the brief's "occasionally notice"; past 0.5 the channel lights up.

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

Six properties are ordinary parameters, so a project's `routes` reaches them through the same
`ProcessorChain` as everything else (attack, decay, curve, threshold, depth):

```
nodes/<terrain>/water/glow        bass: more light from underneath
nodes/<terrain>/water/ripple      bass: a slightly rougher surface
nodes/<terrain>/water/swell       beat: one small synchronised lift
nodes/<terrain>/water/sparkle     treble: glints, fast in and fast out
nodes/<terrain>/water/foam        energy: more surf at the banks
nodes/<terrain>/water/flowSpeed   how fast the ripple pattern travels
nodes/<terrain>/water/glowColor   the colour of what is glowing underneath
```

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

No planar or screen-space reflection: the sky comes from the environment cube and the scene does not
reflect in the water. No copy of the frame, so the only refraction is a wobble in the depth the
shoreline is read at. No underwater post-processing when the camera dips below the surface — the
surface shades itself differently from beneath and that is all. No caustics. Each of those is another
pass, and the brief asked for a beautiful stylized surface rather than an ocean simulator.
