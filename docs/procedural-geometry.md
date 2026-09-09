# Procedural geometry (user guide)

Procedural geometry (ADR-023) lets a scene *generate* architecture instead of importing it:
one source shape, many instances arranged by a distribution, each with seeded variation, all
deformed per vertex on the GPU by a stack of deformers, shaded with the normal PBR material.
Every knob is an ordinary parameter, so audio, LFOs, the timeline, presets, OSC and MIDI drive
it without any special code.

```
primitive / mesh  →  distribution (instances)  →  variation  →  deformer stack  →  material
        CPU (on change)            CPU (on change)      CPU (seeded)     GPU (every frame)
```

## The node

Add a `procedural` node to a composition (Scene tab → kind `procedural`, or in a scene file):

```json
{ "name": "columns", "kind": "procedural", "position": [0, 0, 0],
  "procedural": {
    "source": { "kind": "cylinder", "radius": 0.35, "height": 9, "radialSegments": 24, "caps": true },
    "distribution": { "kind": "radial", "count": 48, "radius": 14, "orientation": "outward" },
    "variation": { "seed": 7, "randomScale": [0, 0.15, 0], "randomRotation": [0, 0.1, 0] },
    "deformers": [
      { "kind": "twist", "amount": 0.15, "speed": 0.05 },
      { "kind": "noise", "amount": 0.12, "scale": 0.6, "speed": 0.2, "space": "world" } ],
    "material": { "baseColor": [0.12, 0.12, 0.14], "emissiveColor": [0.9, 0.5, 1.0], "emissiveIntensity": 0.4, "roughness": 0.35 },
    "materialVariation": { "hueGradient": 0.25, "emissiveRandom": 0.5 }
  } }
```

Parameters appear under `procedural/<node>/…`:

| Group | Paths | Notes |
|---|---|---|
| Source | `source/kind`, `source/radius`, `source/height`, `source/size`, segments, `source/position|rotation|scale` | changing structure regenerates the mesh |
| Distribution | `distribution/kind`, `count`, `radius`, `startAngle`, `endAngle`, `turns`, `radiusGrowth`, `spiralHeight`, `gridCount`, `gridSpacing`, `start`, `end`, `orientation`, `plane`, `splineStart`, `splineEnd`, `alignToSpline`, `roll`, `splineOffset` | instances regenerate on change (microseconds); the spline *name* is structural and comes from the file |
| Transform | `transform/position|rotation|scale` | the whole arrangement |
| Variation | `variation/seed`, `position`, `rotation`, `scale`, `uniformScale` | same seed = same world, live or offline |
| Deformation | `deform/1/amount`, `speed`, `phase`, `frequency`, `scale`, `falloff`, `center`, `axis`, `enabled` (slot 1..8, label shows the kind); `pathOffset`, `pathScale`, `pathRoll` on `path` slots | evaluated on the GPU every frame |
| Material | `material/baseColor`, `emissiveColor`, `emissive`, `roughness`, `metallic`; `materialVariation/hueShift|hueGradient|valueRandom|emissiveRandom|emissiveGradient` | per-instance colour is baked into the instance records |

**Transform order**: `world = node × distribution × placement(i) × variation(i) × source`.
Local-space deformers act on the source shape before instancing (each column twists about its
own axis); world-space deformers act on the final world position (a wave across the whole hall).

## Distributions

- **single**: one instance.
- **linear**: `count` instances from `start` to `end` (or `spacing` apart), optionally oriented along the line.
- **grid**: `gridCount` × `gridSpacing`, centred.
- **radial**: `count` around `center` at `radius` in `plane`, from `startAngle` to `endAngle` (a full turn closes the circle); orientation `none | outward | inward | tangent`.
- **spiral**: a helix: `turns`, `radius` growing by `radiusGrowth`, rising `spiralHeight` along the plane normal.
- **spline**: `count` instances along the named scene spline (ADR-026, `docs/splines.md`),
  evenly by arc length between `splineStart` and `splineEnd` (fractions of the length; reversed
  ranges run backwards). `spacing` > 0 switches to fixed spacing instead: `floor(length ×
  |splineEnd - splineStart| / spacing) + 1` instances exactly `spacing` apart from `splineStart`.
  Each instance takes the sample's position, its `scale` factor and — with `alignToSpline` — the
  frame rotation (+Z along the tangent, +Y along the normal) plus `roll` radians about the
  tangent; `splineOffset` shifts it in frame space (x = binormal, y = normal, z = tangent). A
  closed spline whose span is a whole number of turns drops the duplicate seam instance. An
  unknown spline name leaves every placement at the identity.

## Deformers

| Kind | What it does | Main fields |
|---|---|---|
| bend | bends the shape along `axis` with curvature `amount` (radians per unit), towards `displacementAxis` | amount, axis, center, falloff |
| twist | rotates about `axis` by `amount` radians per unit of height (+ `speed`·time) | amount, axis, center, falloff, speed |
| sine | pushes along `displacementAxis` by `amount·sin(frequency·x + phase + speed·t)` | amount, frequency, speed, phase, axis |
| noise | three decorrelated channels of seeded 3-octave value noise (one per axis, masked by `axisMask`), animated by `speed` | amount, scale, speed, seed, axisMask |
| displacement | pushes along the surface normal by a procedural pattern | amount, scale, speed |
| path | bends the shape along a scene spline: the coordinate along `axis` becomes arc length, the cross-section rides the spline frame | amount, spline, pathOffset, pathScale, pathRoll, axis, center |

Stack order is the array order and is deterministic; `twist → noise` differs from `noise → twist`.

## Building an architectural structure

1. Start from a composition (File > New Project, Scene tab). Add a `procedural` node, choose
   `cylinder`, distribution `radial`, count 48, radius 14: a colonnade.
2. Add a second node: `torus`, distribution `single`, scale it up: a ring above the colonnade.
3. Add a `grid` node for the floor and a `particles` node for dust; set the environment fog
   (`scene/fogDensity`) and a dark background.
4. Add deformers: a local `twist` for slow rotation, a world `noise` for breathing.
5. Camera: `camera/mode` 0 orbits the bounds; 1 uses `camera/position` and `camera/target`,
   which the timeline can key for fly-throughs. Compositions do not spin by default
   (`root/rotationSpeed` 0); `scene/keyLight` scales the default light, `scene/fogDensity` and
   `scene/fogColor` add distance fog.

## Modulating it with audio

Routes are ordinary modulation routes (Modulation tab → Routes):

| Signal | Target | Why |
|---|---|---|
| `audio.bass` | `procedural/columns/distribution/radius` (amount 2, attack 30 ms, decay 400 ms) | the hall breathes with the low end |
| `audio.mid` | `procedural/columns/deform/1/amount` | structural twist follows the body of the mix |
| `audio.flux` | `procedural/columns/deform/2/amount` | spectral change becomes turbulence |
| `audio.centroid` | `procedural/columns/materialVariation/hueShift` | brightness of the sound colours the stone |
| `audio.onset` (peak-hold envelope) | `procedural/ring/transform/scale` | events make the ring kick |
| `beat.pulse` | `scene/brightness` (small amount) | the world pulses with the beat |

Keep amounts modest and let the deformers' own `speed` provide motion when the music is quiet.

## Presets and projects

Store a preset (Presets tab) to snapshot every procedural parameter; morphing between presets
morphs the world continuously because every field is a parameter. Save the project to keep the
scene file, routes, presets and timeline together; `--export-bundle` packages it.

## Rendering offline

`avgen --project temple.json --render out/ --size 3840x2160 --fps 60` renders the same frames the
window shows: instance randoms are hashed from the seed, deformer time is the frame's render
time, and the per-frame hashes printed with `--log debug` match between runs.

## Examples

File > Examples lists the built-in scenes from `examples/index.json`:

| Example | Demonstrates |
|---|---|
| Procedural Geometry Lab | every primitive, distribution, variation control, deformer and stack, with audio routes and a performance readout |
| The Temple | radial colonnade, rings, fog, particles, bloom, audio design (bass scale, mid twist, flux noise, centroid hue, onset impulse) |
| The Cathedral | a different vocabulary: arches from bent boxes, nested radial groups, free camera |
| The Helix | spiral distribution with twist, sine and noise; a camera travelling the helix |
| Impossible Chamber | nested scene files at extreme scale differences, camera inside |
| Hyperspace | everything at once: radial and spiral structures, noise and sine, emissive colour modulation, timeline |
| Procedural Benchmark | 100 / 1,000 / 10,000 instances with no, one, three and noise deformers |

## Conventions worth knowing

- Radial and spiral angles run counter-clockwise about the plane normal (`xz` → normal +Y,
  `xy` → +Z, `yz` → +X); orientation `outward` points each instance's +Z away from the centre.
- Bend curves the shape along `axis` towards `displacementAxis` with curvature `amount` in
  radians per unit; `falloff` limits the bent region and continues straight beyond it.
- Twist, sine, noise and displacement scale their effect by `clamp(|distance along axis| /
  falloff, 0, 1)` when `falloff` > 0.
- Per-instance hue variation is baked into the instance records against the material colours
  at rebuild time; live colour modulation tints approximately until the next structural rebuild.
- Nested scene files bring their procedural nodes along; their parameters are prefixed with the
  node path (`procedural/nodes_<outer>_<inner>/…`).
- Point clouds, point ops, fields and effectors (ADR-024/025) are described in
  `docs/spatial-data.md`; `rebuild()` now goes through `generateCloud()` and the `ops` list.
- The `spline` distribution and the `path` deformer resolve their spline name against the
  scene's spline set: `rebuild(ctx)` needs a `GenerationContext` carrying `splines` (the object's
  `contextualHash` mixes in the referenced spline, so editing the curve rebuilds the cloud), and
  the renderer needs the spline uploaded (at most 16 per scene reach the GPU). Path deformers are
  object space only — a `world` one is skipped. See `docs/splines.md`.
