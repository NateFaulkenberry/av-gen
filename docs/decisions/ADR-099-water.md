# ADR-099: Water occupies a channel

## Status
Accepted, 2026-09-11.

## Context

The brief (`docs/world-authoring-spec.md` §8–§16) opens with the symptom: the river *"reads as a flat
opaque surface laid over the world rather than water occupying a physical channel"*. A screenshot of
Glowmere showed a hard, stair-stepped boundary where water met land and a uniform flat fill between
the banks.

Reading the code, the symptom was a literal description of the implementation.

* The water entity's `alphaMode` was `Opaque`. The generated material program computed an opacity
  register and set `program.opacityRegister`, `pbr_shade.wgsl` read it — and then
  `let alpha = select(1.0, baseColor.a, alphaMode > 1.5)` threw it away. The shoreline fade
  ADR-046's addendum describes had never reached a pixel.
* `deepColor` defaults to `(0.004, 0.020, 0.043)`, which at Glowmere's exposure is black. An opaque
  black polygon is what the screenshot shows.
* Depth colouration came from a vertex lane interpolated across 1.25 m quads, so the only spatial
  variation the surface had was the resolution of the mesh.
* The surface never moved. There was no time term anywhere in the water path.
* Water was in the depth prepass and cast shadows, so a translucent sheet threw a hard shadow onto
  its own bed.

So this is not "make the water shader better". It is four separate pieces of missing machinery.

## Decision

### The thickness of the water is the shoreline

The single change that does the most is this: **a blended surface is not in the depth prepass, so
the linear depth it reads is the bed underneath it.** Water being opaque was the reason there was
nothing to read.

`shaders/water.wgsl` samples `sceneLinearDepth` (already bound at group 0 binding 7 for contact
shadows) and subtracts its own view depth. That gives the metres of water the view ray crosses at
*this pixel*, and everything that used to be a mesh property falls out of it:

* opacity is Beer-Lambert over that thickness, so the same sheet is glass in the shallows and solid
  in the channel with nothing authored per pixel to make it so;
* the waterline is a smooth alpha ramp over `edgeFade` metres of depth, so the stair-stepped polygon
  boundary is gone — not because the mesh changed, but because the mesh's edge stopped being where
  the water ends;
* the foam band, the depth colour and the underwater glow gate all read the same number;
* reading it at a point the ripple normal displaces gives the edge distortion §10 asks for, for the
  cost of adding two floats to a texture coordinate. That is the only refraction here: the colour
  behind the water arrives through the blend, not through a copy of the frame.

A scene with water in it now asks for the depth prepass whatever else is on. That is 0.2–1.3 ms of
depth-only geometry against a surface that otherwise falls back to the vertex depth and to the mesh's
own edge for its waterline. The fallback is real and tested (`params.z`), because a tier with no
prepass must degrade to the old picture rather than to garbage.

### Its own pipeline, not a branch in the shared one

`rendering::WaterRenderer` owns one pipeline inside the scene pass. It is not a pass: water is
geometry, depth-tested against the world it sits in, and it wants to composite over the bank while
the bank's depth is still bound.

It is not a branch in `pbr_shade.wgsl` either, and that is deliberate against the usual instinct to
extend rather than fork. Water is not a metallic-roughness surface with an alpha on it; it is a
volume seen through its own boundary, and the three things it needs most — the scene's depth, a
normal from travelling ripples, an environment reflection — are not what that path carries. More
practically: the scene pass is the frame's dominant term and is fragment-bound, and a branch in the
shader every entity in the world runs is a bad place to put a feature one of them uses.

What it *does* reuse is `lighting.wgsl`: the same packed lights, the same shadow atlas, the same
cascade lookup, so the moon that lights the bank is the moon that glints off the river. Only the
directional lights are walked. The clustered local lights are 18% of the scene pass on this world
(`docs/renderer-2-backlog.md`) and the water is fullscreen-adjacent; what they would add to a
specular this tight is a handful of pixels.

Finding that out cost something worth recording: `lighting.wgsl` called `material.wgsl`'s
`matSafeNormalize`, so it could only be included by a module that had already included the material
interpreter. Nothing declared that and nothing enforced it. It has its own four lines now.

### Downstream is read off the terrain, never authored twice

ADR-090 gives water a `world::WaterCourse`: a downstream-ordered centreline with the surface level
at every node, a half-width, a depth and the descent over its length, derived from the same
`WorldMap::features` the ground was cut from.

`world::WaterBody` is **built from** one. It adds only what a renderer needs and terrain has no
opinion about — how fast the surface travels, how much of that is lost against the bank, how far the
current wanders off the centreline, and an arc-length table so a floating leaf can be placed at
"sixty per cent of the way down" rather than at a world point. Direction, surface height and position
along all come from the course, so a change to how terrain traces a river arrives here with no edit.

`flowSpeed` is therefore **not** an authored number. `WaterCourse::flowSpeed()` derives it from the
course's own gradient, and `WaterFlowSettings::speedScale` multiplies it; `speedOverride` exists for
the shot that needs to contradict the terrain. Glowmere's river comes out at 0.91 m/s from 39 m of
descent over 516 m, and moving the river changes it without anyone editing a speed.

A course with no descent is a pond, a lake or a sea, and gets a slow wind-driven drift instead of a
direction — which is §12's "no strong direction, subtle circular/ripple motion". A perfect mirror
reads as glass, not as water, so still is not stationary.

### The flow rides in the water vertex's normal

A water sheet's normal is `(0, 1, 0)` at every vertex and the shader computes its own from the
ripples, so the slot was already paid for and unread. `buildChunkWater` packs the downstream
direction into `normal.xz` and the speed, as a fraction of the fastest body in the world, into
`normal.y`. No vertex format change, no second stream, and the surface shader never hears the word
"river".

`uv.x` changed from a 0..1 ratio of `shallow` to depth in **metres**, because the shader curves it
itself now and the foam width is authored in metres. `uv.y` was a second copy of the same number and
now carries the cross-channel coordinate — 1 on the centreline, 0 at the bank — which the bed's depth
cannot say: a wide shallow reach is shallow all the way across and its middle is still its middle.
The bioluminescence keeps to the channel because of it.

A world that passes no bodies gets exactly the vertex it always got. That is tested.

### Three ripple layers whose amplitude falls as the square of their frequency

§11 asks for layered motion and specifically not one scrolling texture. Three layers, each carried
along the *local* flow and pushed a different amount across it, from value noise with its analytic
gradient so one evaluation gives both height and slope.

The amplitudes matter more than the layer count. A first version gave each layer an amplitude
inversely proportional to its frequency, which makes every layer contribute the same *slope* — and a
surface whose slope is dominated by its finest layer reads as crazed glass. Falling as the square
puts the broad swell in charge of the shape and leaves the fine layers as detail on it, which is
also what nature does: capillary waves are millimetres tall on top of metre-long swell.

Each layer fades against the world-space size of one pixel. The sparkle is band-passed instead, at
both ends: a glint field whose cells are eighty pixels across is a row of white ovals lying on the
water, which is what a world-space frequency gives in the near field. Sub-pixel water detail
does not shimmer, it *crawls*, because the pattern is travelling; this is the level of detail that
keeps the far reach of a river smooth.

### The Fresnel lie, stated plainly

Water's reflectance at normal incidence is 0.02. At the angle a camera looks at a river from, that
returns five per cent of a night sky and ninety-five per cent of a black bed — which is precisely the
flat dark ribbon this work exists to replace.

`WaterSettings::fresnel` is the normal-incidence reflectance, authored, and Glowmere sets it to 0.30.
That is the one deliberate departure from physics here and it is the one that turns the surface back
into a surface. Everything else (the Schlick curve, Beer-Lambert absorption, the shear profile of an
open channel) is the real shape of the thing.

### Floating objects are a pure function of the clock

`scene::FloatSpec` / `evaluateFloaters` places lily pads, leaves and petals:

```
placement(i, t) = body.pointAt(wrap(s_i + v_i * t)) + lateral_i,  yaw_i + spin_i * t,  bob(t, phase_i)
```

There is no integration and no state. A render that starts at t = 40 s puts every leaf exactly where
a render that started at 0 would have had it, so a scrub, a re-render and a live preview of the same
second agree exactly — and a floating layer is free to seek.

It is not a new instancing system. A floating layer is an ordinary `ProceduralGeometry`: an imported
mesh, a material, GPU culling, LOD, the wind deformer. All this adds is where its instances are this
second, written into `instances` with `structureVersion` bumped — which is the renderer's upload key,
and "the records are different" is exactly what that key means.

The numbers that make it read as a river rather than a conveyor are `driftSpread` (each instance
travels at its own fraction of the water's speed) and the shear profile the body already has, so a
leaf near the bank falls behind one in midstream for free.

Where a body's banks are the only test available, a pad can land on a shoal: the banks are a planar
test and the bed is not planar. ADR-090 §3's `TerrainQuery::waterDepthAt` is the right question, so
`evaluateFloaters` takes an optional query and walks an instance in toward the centreline until it is
genuinely wet, dropping it if even the centreline is dry. Dropping is the honest outcome — a river's
head is too shallow to hold a lily pad.

### Reactivity is six parameters, not a modulation system

`nodes/<terrain>/water/{glow, sparkle, ripple, flowSpeed, swell, foam, glowColor}` are ordinary
`params::Parameter`s. A project's `routes` reaches them through the `ProcessorChain` every other
reactive property in this engine uses — gain, curve, threshold, asymmetric attack/decay, envelope,
depth — and the editor gets sliders for them for nothing. ADR-088 made this argument once already and
it did not need making twice.

What this layer decides is only *which* six things are worth moving. Bass to the glow underneath and
a little to the ripple; the beat to a 5.5 cm swell, which is small on purpose because a river that
pumps on every kick has stopped being water; treble to the sparkle, fast in and fast out because that
is what a glint is; energy to the surf at the banks.

## Consequences

**The stair-step is gone**, and not by tessellating anything: the visible waterline is a depth-based
alpha ramp, so it is smooth across a quad boundary the geometry is not, and it wobbles with the
ripple because the depth is read at a displaced point.

**Water no longer casts shadows.** A translucent sheet throwing a hard shadow onto its own bed was
a bug nobody had looked at.

**One material-program slot came back.** `waterMaterialProgram` is deleted; the surface shader does
what it did, per pixel instead of per vertex, from the same `WaterSettings`.

**`WaterSettings` moved to `scene/water_surface.hpp`** and `world::WaterSettings` is an alias. The
settings have to reach `scene::Scene`, which the renderer is handed, and pulling the whole world map
into `scene.hpp` for twenty-five floats would have inverted the dependency.

**A terrain node publishes each water body's centreline as a scene spline** named `<node>.<body>`. A
river is a curve through the world and the engine already has a curve type that particle emitters,
path deformers, instance distributions and the camera all read. Glowmere's mote emitter runs down the
river because it names `valley.glowmere-run`, with no new emitter kind.

**Cost**, from `docs/world-performance.md`: **+1.44 ms** of the scene pass at the editor's
2880×1166 canvas, at a camera where the water is 15.3% of the frame — **≈2.8 ns per water pixel**,
so a surface filling that canvas would be about 9.4 ms. Glowmere's river never comes close, because
it is a seven-metre channel: at the shot this work was judged on it is 7.6% of the frame and the
difference is under what this machine could resolve under the load it was carrying, which is stated
as "not resolvable" there rather than rounded to a number that happened to come out.

The floating layers cost **0.019 ms** on the CPU for 320 instances. They cost 0.70 ms first, and both
fixes were the same mistake twice: asking a question per frame that had a per-*world* answer. The
expensive one was `TerrainQuery::waterDepthAt` per pad per frame to keep pads off the shoals — a
six-octave noise evaluation for a fact about the riverbed, which does not move. `WaterBody::wetted`
measures it once.

**Determinism**, which the drifting is written for: two headless runs of 150 frames are byte
identical, and the same timeline second rendered at 30 fps and at 60 fps leaves the water region
96.9% identical -- the residue being the particles over it and the temporal AO history, both of
which `renderer-2-backlog.md` already records as cadence-dependent and neither of which is water.

**What is not here.** No planar reflection and no screen-space reflection: the sky comes from the
environment cube and the scene does not reflect. No copy of the frame for refraction. No underwater
post-processing when the camera dips below the surface — the surface shades itself differently from
beneath and that is all. No caustics. Each of those is a pass, and the brief asked for a beautiful
stylized surface rather than an ocean simulator.
