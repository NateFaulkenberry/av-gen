# Volumetrics and simulation

ADR-032. Two features that share one idea: the air and the space between objects are data the
rest of the system can read. Volumetric atmosphere is a raymarched fog pass driven by the
`Environment`; simulated grid fields are 3D grids stepped on the GPU and sampled through the
ordinary field interface, so effectors, deformers, particles, materials and the fog itself read
them with no change on their side.

Related: [ADR-032](decisions/ADR-032-simulation-and-volumes.md),
[research](research/spatial-simulation.md), [fields and effectors](spatial-data.md),
[GPU fields](gpu-fields.md), [performance](performance/procedural-geometry.md).

---

## 1. Volumetric atmosphere

### 1.1 The density model

Every raymarch sample multiplies four terms (the header comment on `scene::Environment` is the
specification; `shaders/volume.wgsl` implements it):

```
density(p) = volumeDensity
           * exp(-max(0, p.y - fogHeight) * fogHeightFalloff)          height falloff
           * (1 + volumeNoiseAmount * (fbm3(p * volumeNoiseScale
                                            + t * volumeNoiseSpeed) * 2 - 1))   detail
           * densityField(p)                                            shaping (1 when unset)
```

* **Height**: below `fogHeight` the term is 1; above it the density decays exponentially at
  `fogHeightFalloff` per unit. `fogHeightFalloff = 0` is uniform fog everywhere.
* **Noise**: the same 3-octave value fBM as everywhere else (`shaders/noise.wgsl` ↔
  `core/noise.hpp`), animated by adding `t * volumeNoiseSpeed` to the sample position, so the
  detail drifts rather than boiling. It is the expensive term and is skipped where the other
  terms are already zero.
* **Field**: `volumeDensityField` names a scalar field in `Scene::fields`; its sample (clamped at
  0) multiplies the density, which is how fog gets shaped, animated and made audio-reactive. A
  simulated grid (`FieldKind::Grid`) works here like any other field.

Along the ray, with `sigmaT = density * volumeAbsorption` and `sigmaS = density * volumeScattering`:

```
radiance += transmittance * (sigmaS * phaseHG(cos theta, volumeAnisotropy) * keyLight
                             + density * volumeEmission * emissionColour) * stepLength
transmittance *= exp(-sigmaT * stepLength)
```

`keyLight` is the first enabled light (directional: its colour × intensity; point/spot: with
inverse-square, range and cone attenuation). There are no shadows in the fog yet. The emission
colour is `volumeColorField`'s colour sample (rgb × alpha) when one is named, otherwise
`fogColor`. The march stops early once transmittance falls below 0.002.

`fogDensity` (the older exponential-squared distance fog in `pbr_shade.wgsl`) is unrelated and
still applies to surfaces; the two can be used together.

### 1.2 Parameters and JSON

Registered in `scene/composition.cpp` next to `scene/fogDensity`, so audio, the timeline,
presets, OSC/MIDI and macros drive them through the ordinary routes.

| Parameter | Environment member | JSON key (`environment`) | Default | Meaning |
|---|---|---|---|---|
| `scene/volumeDensity` | `volumeDensity` | `volumeDensity` | 0 | master density; **0 turns the pass off entirely** |
| `scene/fogHeight` | `fogHeight` | `fogHeight` | 0 | height above which the density falls off |
| `scene/fogHeightFalloff` | `fogHeightFalloff` | `fogHeightFalloff` | 0 | decay per unit above `fogHeight` (0 = uniform) |
| `scene/volumeScattering` | `volumeScattering` | `volumeScattering` | 1 | in-scatter strength |
| `scene/volumeAbsorption` | `volumeAbsorption` | `volumeAbsorption` | 0.5 | extinction multiplier |
| `scene/volumeAnisotropy` | `volumeAnisotropy` | `volumeAnisotropy` | 0.3 | Henyey–Greenstein `g` (forward > 0) |
| `scene/volumeNoise` | `volumeNoiseAmount` | `volumeNoise` | 0 | detail amount |
| `scene/volumeNoiseScale` | `volumeNoiseScale` | `volumeNoiseScale` | 0.1 | detail spatial frequency |
| `scene/volumeNoiseSpeed` | `volumeNoiseSpeed` | `volumeNoiseSpeed` | 0.1 | detail drift per second |
| `scene/volumeEmission` | `volumeEmission` | `volumeEmission` | 0 | self-emission strength |
| `scene/volumeSteps` | `volumeSteps` | `volumeSteps` | 32 | raymarch samples per pixel (4…256) |
| – | `volumeMaxDistance` | `volumeMaxDistance` | 200 | how far the march goes (not a parameter: changing it per frame would swim) |
| – | `volumeDensityField` | `volumeDensityField` | "" | scalar field name |
| – | `volumeColorField` | `volumeColorField` | "" | colour field name (else `fogColor`) |

The `environment` block is only written when the fog is on, so existing scene files round-trip
unchanged. Field names are prefixed like every other reference when a scene is nested.

```json
"environment": {
  "background": [0.004, 0.008, 0.012],
  "fogColor": [0.01, 0.02, 0.03],
  "volumeDensity": 0.045,
  "fogHeight": 3.0,
  "fogHeightFalloff": 0.09,
  "volumeScattering": 1.4,
  "volumeAbsorption": 0.9,
  "volumeAnisotropy": 0.45,
  "volumeNoise": 0.45,
  "volumeNoiseScale": 0.07,
  "volumeNoiseSpeed": 0.12,
  "volumeEmission": 0.35,
  "volumeSteps": 24,
  "volumeMaxDistance": 90.0,
  "volumeDensityField": "heat",
  "volumeColorField": "heat"
}
```

### 1.3 Pass order and depth

```
scene pass  ──▶ HDR colour + depth   (opaque PBR, procedural, SDF raymarch, skybox, grid,
   │                                  particles, blended PBR)
   ▼
volume march ──▶ half-res RGBA16F    (rgb = in-scattered radiance, a = transmittance)
   │              reads the scene depth: the march stops at the first surface
   ▼
volume composite ──▶ HDR colour      (blend: src One, dst SrcAlpha ⇒ scatter + hdr * T)
   │                                  depth-aware bilinear upsample
   ▼
user post layers ──▶ built-in post chain ──▶ tonemap ──▶ target
```

* The march runs at **half resolution** (`ceil(w/2) × ceil(h/2)`) into its own RGBA16F target;
  each half-res pixel marches the ray of the full-res texel at `(2x, 2y)`.
* **Depth**: the pass reads `hdr_.depthView()` with `textureLoad` (no sampler) and converts the
  clip depth to a world distance through `invViewProj`, so `maxDistance = min(volumeMaxDistance,
  distanceToSurface)`. Fog therefore sits correctly in front of and behind geometry, and a sky
  pixel (depth 1) marches the full distance.
* **Upsample**: the composite pass weights the four neighbouring half-res texels by the ordinary
  bilinear weight divided by `1 + 8 * |theirDepth − myDepth| / myDepth` (both linearised with the
  camera's near/far), so a half-res texel that marched a very different surface contributes
  almost nothing. That is what stops halos on silhouettes.
* The composite runs **before** the post chain, so bloom, DoF and grading see the fog.
* Because the fog is composited into the HDR target, it is captured by EXR output, texture
  sharing and the frame hash like anything else.

**Off is free.** `volumeDensity <= 0` means `VolumeRenderer::update()` allocates nothing and
`encode()` emits no passes at all — the command stream of a fog-free frame is byte for byte what
it was before ADR-032, which is why the golden frame hashes in
`tests/rendering/test_procedural_examples_gpu.cpp` are unchanged.

### 1.4 Driving fog from audio

Nothing special is needed: the `scene/volume*` parameters are ordinary parameters.

```json
{ "source": "audio.bass", "target": "scene/volumeDensity", "amount": 0.05, "op": "add" }
{ "source": "audio.onset", "target": "scene/volumeEmission", "amount": 1.5, "op": "add" }
```

For fog that moves *in space* with the music, route audio at a **field** instead and name that
field in `volumeDensityField`: a `radial` field whose `strength` follows the bass makes the fog
bloom outwards from a point, a `wave` field makes it pulse in rings, and a simulated grid fed by
an audio-driven injection field makes it billow and persist. The `heat` field in
`examples/machine` is the simplest version of this.

---

## 2. Simulated grid fields

### 2.1 What a grid is

`spatial::GridField` is a 3D grid of cells over an axis-aligned box, stepped with a fixed
sub-step. It is *not* a field itself: a grid is referenced by name from an ordinary
`FieldKind::Grid` field, and that field is what everything else samples.

```
Scene::fields.grids  ─── "smoke" ──▶  GridField { resolution, bounds, injection, advection, … }
Scene::fields.fields ─── FieldSpec { kind: "grid", reference: "smoke" }
                                   ▲
      effectors, deformers, particle field forces, material programs, volumeDensityField
```

| Mode | Floats per cell | Scalar reading | Vector reading |
|---|---|---|---|
| `scalar` | 1 | the value | value × the field's axis |
| `vector` | 4 (xyz + pad) | the length | the vector, rotated by the field's frame |
| `reactionDiffusion` | 2 (A, B) | **B**, the pattern channel | B × the field's axis |

Sampling is trilinear over the cell centres (cell *i* is centred at
`boundsMin + (i + 0.5) * cellSize`). Outside the bounds, `wrap` decides: `clamp` repeats the edge
cells, `wrap` tiles the grid. `spatial::GridField::sampleScalar` / `sampleVector` on the CPU and
`gridScalarAt` / `gridVectorAt` in `shaders/fields.wgsl` are the same arithmetic in the same
order.

### 2.2 One step

`GridField::step()` (CPU reference) and the kernels of `shaders/simulate.wgsl` (GPU) both run:

1. **inject** — `value += injectRate * dt * injectField(cellCentre)` (clamped at 0 for scalar and
   reaction grids, where it lands in B; vector grids take the field's vector).
2. **advect** — semi-Lagrangian: back-trace the cell centre by
   `advect * velocityField(centre) * dt` and gather trilinearly from the previous state
   (Stam, *Stable Fluids*).
3. **diffuse** — `diffuseIterations` Jacobi sweeps of
   `(x0 + a * Σ(6 neighbours)) / (1 + 6a)` with `a = diffusion * dt`, all relaxing towards the
   state the sweeps started from.
4. **dissipate** — `value *= max(0, 1 - dissipation * dt)`.

In `reactionDiffusion` mode, steps 3–4 are replaced by one Gray–Scott explicit Euler step with
the 6-neighbour Laplacian:

```
A' = clamp(A + (diffusionA * lap(A) - A B² + feed  * (1 - A)) * dt, 0, 1)
B' = clamp(B + (diffusionB * lap(B) + A B² - (kill + feed) * B) * dt, 0, 1)
```

The 6-neighbour Laplacian is *unnormalised*, so keep `diffusion * dt <= 1/6` for stability;
`diffusionA = 0.16`, `diffusionB = 0.08`, `simRate = 1` reproduces the classic `dA = 1, dB = 0.5`
setup. In 3D the reaction dilutes faster than in 2D — `feed = 0.030, kill = 0.062` patterns,
`feed = 0.055` dies out.

The initial state comes from `GridField::reset()` on the CPU and is uploaded: zero plus
`seedAmount` of seeded fBM for scalar/vector grids, and for reaction grids `A = 1` everywhere
with blobs of `(A, B) = (0.5, 0.25)` where a low-frequency fBM crosses 0.6.

### 2.3 Settings and JSON

Grids live in a scene file's top-level `"grids"` array. Only settings are serialised — never the
cell values, which are always a function of the settings and the render time.

```json
"grids": [
  {
    "name": "smoke",
    "mode": "scalar",
    "wrap": "clamp",
    "resolution": [64, 64, 64],
    "boundsMin": [-8, 0, -8],
    "boundsMax": [8, 16, 8],
    "injectField": "heat",
    "velocityField": "churn",
    "injectRate": 2.0,
    "advect": 1.0,
    "diffusion": 0.5,
    "diffuseIterations": 4,
    "dissipation": 0.4,
    "simRate": 60.0,
    "maxSubSteps": 4,
    "seed": 11,
    "seedAmount": 0.0
  }
]
```

`"resolution"` also accepts a single integer for a cube. Reaction-diffusion grids add `feed`,
`kill`, `diffusionA` and `diffusionB`. A field that reads it:

```json
{ "name": "smokeField", "kind": "grid", "reference": "smoke", "strength": 1.0 }
```

Names are prefixed when a scene is nested (`<node>_smoke`), and so are `injectField` /
`velocityField`, so a nested scene file stays self-contained.

### 2.4 Where the data lives

One shared storage buffer, the **grid table**, holds every grid of the scene back to back in
`FieldSet::grids` order (`spatial::gridTableOffset`). `shaders/fields.wgsl` declares it at
`@group(0) @binding(15)` — one binding for every module that includes the file — and
`rendering::FieldUniforms` owns the buffer, allocated once at a fixed 8 MB so no bind group ever
has to be rebuilt. `rendering::Simulation` owns two ping-pong buffers plus one snapshot buffer,
runs the kernels and copies the finished state into the table each frame.

Consequently a bind group layout that serves a `fields.wgsl` consumer must carry binding 15:
`SceneRenderer`'s frame layout (group 0 of `pbr.wgsl`, `procedural.wgsl`, `sdf_raymarch.wgsl`,
`volume.wgsl`), the particle compute layout, and the procedural effector (`points.wgsl`) layout
all do. Anything new that includes `fields.wgsl` and creates its own pipeline layout needs the
same entry.

### 2.5 Determinism

* The sub-step is fixed at `1 / simRate` seconds. A grid has taken
  `floor(renderTime * simRate)` sub-steps, and each frame runs
  `clamp(target - taken, 0, maxSubSteps)` of them — so the state at a given render time does not
  depend on the frame rate, and a live render and an offline render of the same project reach
  the same state.
* The first frame after a reset may run up to `Simulation::kCatchUpSteps` (240) sub-steps, so an
  offline render that starts at t > 0 catches up to where a live playhead would have been. A
  longer backlog than that is skipped with a warning, and the state then differs from a
  continuous playthrough — keep simulations that must match settled within ~4 seconds.
* All kernels are **gather-only**: a thread writes only its own cell. No atomics, no
  inter-thread ordering, so the result does not depend on scheduling.
* Iteration counts (`diffuseIterations`) are fixed, seeds are explicit (`seed`), the initial
  state is computed on the CPU and uploaded, and fields sampled by the kernels are evaluated at
  the frame's `renderTime` for every sub-step of that frame.
* A reset (initial state re-uploaded, counter cleared) happens when the set of grids changes
  structurally, when the render time moves backwards (a seek), or when the buffers are
  reallocated.
* A grid sampled *by* a simulation kernel (a `Grid` velocity field feeding another grid) reads
  the previous frame's table. That is still deterministic, just one frame behind.

### 2.6 Cost and limits

* `spatial::kMaxGridTableFloats` is 2 Mi floats = **8 MB**: one 128³ scalar grid, one 64³ vector
  grid, or a 100³ reaction-diffusion grid. `validate()` rejects anything larger.
* `Simulation::kMaxGrids` is 8 grids per scene; `spatial::kMaxGpuFields` (16) still caps how many
  fields — grid-reading or not — reach the GPU.
* Each stage is its own compute pass (WebGPU forbids a buffer being writable and readable storage
  in one pass, and the ping-pong swaps those roles), so one sub-step of a scalar grid with
  injection, advection, four diffusion sweeps and dissipation is 7 dispatches.
* Fluid **projection** (a divergence-free velocity solve) is not implemented: advect a grid by an
  analytic `curlNoise` field, which is divergence-free by construction, or by a simulated vector
  grid you accept is not incompressible.
* The CPU reference (`GridField::step`) is for tests and tools only. It is a straightforward
  triple loop with full copies per stage and is not fast.

Measured numbers: [performance/procedural-geometry.md](performance/procedural-geometry.md),
"Volumetrics and simulation".

---

## 3. Files

| Area | Files |
|---|---|
| Density model, parameters | `src/scene/scene_types.hpp` (`Environment`), `src/scene/composition.{hpp,cpp}` |
| Fog passes | `src/rendering/volume_renderer.{hpp,cpp}`, `shaders/volume.wgsl` |
| Grid data, CPU reference | `src/spatial/grid_field.{hpp,cpp}` |
| Grid field sampling | `src/spatial/field.{hpp,cpp}` (`FieldKind::Grid`), `shaders/fields.wgsl` |
| Grid simulation | `src/rendering/simulation.{hpp,cpp}`, `shaders/simulate.wgsl` |
| Shared grid table | `src/rendering/field_uniforms.{hpp,cpp}` |
| Tests | `tests/unit/test_grid_field.cpp`, `tests/rendering/test_volume_gpu.cpp`, `tests/rendering/test_simulation_gpu.cpp` |
