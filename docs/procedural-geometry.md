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
| Distribution | `distribution/kind`, `count`, `radius`, `startAngle`, `endAngle`, `turns`, `radiusGrowth`, `spiralHeight`, `gridCount`, `gridSpacing`, `start`, `end`, `orientation`, `plane` | instances regenerate on change (microseconds) |
| Transform | `transform/position|rotation|scale` | the whole arrangement |
| Variation | `variation/seed`, `position`, `rotation`, `scale`, `uniformScale` | same seed = same world, live or offline |
| Hierarchy | `hierarchy/depth`, `scalePerLevel`, `offsetPerLevel`, `rotationPerLevel` | self-recursion (ADR-029); structural, so a change regenerates the cloud |
| Deformation | `deform/1/amount`, `speed`, `phase`, `frequency`, `scale`, `falloff`, `center`, `axis`, `enabled` (slot 1..8, label shows the kind) | evaluated on the GPU every frame |
| Material | `material/baseColor`, `emissiveColor`, `emissive`, `roughness`, `metallic`; `materialVariation/hueShift|hueGradient|valueRandom|emissiveRandom|emissiveGradient` | per-instance colour is baked into the instance records |

**Transform order**: `world = node × distribution × placement(i) × variation(i) × source`.
Local-space deformers act on the source shape before instancing (each column twists about its
own axis); world-space deformers act on the final world position (a wave across the whole hall).

## Material parts

Multi-material mesh sources in procedural composition nodes expose additional parameter paths:
`procedural/<node>/parts/<index>/tint`, `emissiveGain`, `roughnessScale`, `opacityScale` and
`emissiveColor`. `emissiveColor` is black by default, meaning "whatever the material already had" —
an emission of zero and an emission that is black are the same picture, so black is free to mean
something else. Setting it gives that part its own colour *and* its own unit strength, which is
what lets a lamp answer the bass while the lens beside it answers the treble; a multiplier alone
cannot light a part whose glTF `emissiveFactor` is `[0,0,0]`, which is most of them.

The index is ordered by surface area and is not something a scene file should have to guess. The
composition logs what an asset's parts are called (`material parts 0=Gray, 1=Light, 2=Black,
3=Blue`), and an entity's reactions may address them by that name — see `"entities"` in
`docs/project-format.md`.
Nested compositions include their existing prefix before the node name. All default to one.
These multiply each part's own material after the legacy whole-node material controls have been
applied, and are recalculated from the rest material each frame, not compounded over time.
Texture bindings remain independent. Roughness and opacity are clamped to `[0, 1]`.

Indices are zero-based in the imported asset's surface-area ordering, not glTF material names.
An asset edit can change that ordering; inspect the parts before transferring overrides to a
different asset. These paths belong in project parameters, timeline tracks or modulation routes.
They do not change the scene's authored rest materials.

Only multi-material procedural nodes expose this surface, not terrain scatter or ordinary glTF
nodes. A material program can replace base color, roughness, opacity or emission downstream;
for such outputs, use the program's own controls. In particular, `emissiveGain` is not a gain on
the final emission register of a material program.

## Distributions

- **single**: one instance.
- **linear**: `count` instances from `start` to `end` (or `spacing` apart), optionally oriented along the line.
- **grid**: `gridCount` × `gridSpacing`, centred.
- **radial**: `count` around `center` at `radius` in `plane`, from `startAngle` to `endAngle` (a full turn closes the circle); orientation `none | outward | inward | tangent`.
- **spiral**: a helix: `turns`, `radius` growing by `radiusGrowth`, rising `spiralHeight` along the plane normal.
- **grammar**: the placements come from the object's `grammar` expansion — see [Compositional grammar and hierarchical instancing](grammar-and-hierarchy.md).

## Deformers

| Kind | What it does | Main fields |
|---|---|---|
| bend | bends the shape along `axis` with curvature `amount` (radians per unit), towards `displacementAxis` | amount, axis, center, falloff |
| twist | rotates about `axis` by `amount` radians per unit of height (+ `speed`·time) | amount, axis, center, falloff, speed |
| sine | pushes along `displacementAxis` by `amount·sin(frequency·x + phase + speed·t)` | amount, frequency, speed, phase, axis |
| noise | three decorrelated channels of seeded 3-octave value noise (one per axis, masked by `axisMask`), animated by `speed` | amount, scale, speed, seed, axisMask |
| displacement | pushes along the surface normal by a procedural pattern | amount, scale, speed |

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

## Grammars and hierarchies

Two ways to get a lot of structure out of a little data, both covered in full by
[docs/grammar-and-hierarchy.md](grammar-and-hierarchy.md):

- **A grammar** (`distribution.kind = "grammar"`) rewrites named rules — `place`, `repeat`,
  `branch`, `alternate`, `mirror`, `choice`, `conditional` — into the object's placements, and
  tags each point with `depth`, `rule` and `branch` columns that point ops and the `extraLane`
  projection can select on. Six `repeat`ed bays, each `mirror`ed into two aisles, each
  `branch`ing into a column and an arch is five rules and 24 instances.
- **A hierarchy** composes placements with themselves or with another object's. Setting
  `hierarchy.recursionDepth` to `d` makes the object a fractal of its own arrangement:
  every point is a chain of `d + 1` placements composed through the per-level transform
  (`offsetPerLevel`, `rotationPerLevel`, uniform `scalePerLevel`), so `n` placements become
  `n^(d+1)`, truncated at `hierarchy.maxInstances`. The cloud is the *deepest* level only, so a
  visible trunk-and-branches tree wants separate objects (one per depth) or a `branch` grammar.
  `colorPerLevel` rotates the hue by `(root index mod (d+1)) / (d+1)` turns so the root branches
  read as families.
- **A procedural source** (`source.kind = "procedural"`, `source.reference = "<object>"`) makes
  another procedural object of the same scene the source of this one: its mesh *and* its whole
  arrangement are composed under every placement here (`n × m` instances, ids `i * m + j`,
  colours multiplied). Chains may be up to four hops deep; cycles, missing references and
  over-deep chains are rejected before anything is generated. Generation regenerates the
  referenced object's cloud from the scene, so a composition may build its objects in any order.

```json
"hierarchy": { "recursionDepth": 2, "scalePerLevel": 0.45,
               "offsetPerLevel": [0, 5, 0], "rotationPerLevel": [0, 30, 0], "colorPerLevel": true }
```

`hierarchy/depth`, `hierarchy/scalePerLevel`, `hierarchy/offsetPerLevel` and
`hierarchy/rotationPerLevel` are ordinary parameters, so audio and the timeline drive the whole
recursion; they are structural, so each change regenerates the cloud (microseconds at these
sizes) rather than tinting it.

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
- Grammars and hierarchical instancing (ADR-028/029) are described in
  `docs/grammar-and-hierarchy.md`; `generateCloud()` dispatches to them when the
  distribution is `grammar`, `hierarchy.recursionDepth` is above 0 or the source is
  another procedural object.
