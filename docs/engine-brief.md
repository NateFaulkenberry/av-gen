# Inside av-gen

**Engine state — 10 September 2026.** What the tree actually contains, what runs in production,
what is prototype, what is broken, and the measured numbers behind all three.

Every figure here was measured on an M2 Max on the date above, not recalled. Where something is
unverified — a base M1's frame rate, the cause of the residual motion churn — it is labelled as
unverified rather than estimated.

| | |
|---|---|
| Source | 75,475 lines |
| Tests | 43,494 lines, 925 tests |
| WGSL | 8,126 lines in 30 files |
| Decision records | 58 |
| Commits | 250 |

---

## 1. Source tree

The architecture is deliberately not a framework. Generators produce plain scene data; the
renderer consumes it. There is no scene graph traversal at draw time and no virtual dispatch in
the frame loop — `scene::Scene` is a flat set of arrays that `SceneRenderer` walks.

The pipeline reads: audio → analysis → signal bus → timeline → modulator → parameter set →
generators → `Scene` → renderer → output.

| Module | Files | Lines | Responsibility |
|---|---:|---:|---|
| `scene/` | 41 | 16,300 | Composition, node kinds, procedural geometry, SDF objects, material programs, light rigs, camera |
| `rendering/` | 42 | 12,937 | Every GPU pass: scene, shadow, AO, volume, particles, SDF, post, output mapping |
| `spatial/` | 19 | 7,600 | Point clouds, typed attributes, fields, effectors, splines, SDF trees, vegetation simulation |
| `app/` | 26 | 7,462 | Engine, projects, render jobs, scene states, world director, outputs |
| `graph/` | 5 | 4,603 | The authoring node graph and its invalidation |
| `world/` | 8 | 3,173 | World map, terrain, biomes, ecology and glow aggregation |
| `ui/` | 9 | 3,104 | ImGui inspector, parameter editors, plots |
| `control/` | 11 | 3,045 | OSC, MIDI, macros, route chains |
| `assets/` | 15 | 2,825 | glTF import, image decode, EXR, video writers |
| `params/` | 13 | 2,590 | Typed parameter set, modulation, serialisation, migration |
| `core/` | 21 | 2,528 | Noise, colour, wind field, plant chains, logging, error type |
| `gpu/` | 20 | 2,274 | Dawn context, targets, shader library, readback, frame timeline |
| `signals/` `analysis/` `audio/` | 27 | 4,110 | FFT, bands, onsets, beat tracking, playback, live capture |
| `shaders/` `share/` `platform/` | 14 | 2,924 | User WGSL layers, Syphon/NDI sharing, SDL window |

Fifty-eight architecture decision records in `docs/decisions/` carry the reasoning, numbered in the
order the decisions were made — ADR-001 chose Dawn over Metal directly; ADR-058, written today,
made the styled ambient authorable. They are the primary documentation and they explain *why*, not
what.

---

## 2. Renderer

WebGPU through Dawn on Metal. Everything is RGBA16Float from the first pass until the tonemap,
which is AgX by default with a chroma-retention term. Clustered forward shading with a froxel grid,
up to 256 scene lights and 32 per cluster, LTC area lights for rect, disk, tube and sphere
emitters.

Because this is Apple TBDR, **MSAA is off everywhere** (`multisample.count = 1`) and there is no
TAA. That single fact shapes a lot of the open visual problems below.

### Frame budget, measured

`glowmere-stylized` · 1280×800 · realtime tier · gpu frame median **21.95 ms**

| Pass | ms | Share |
|---|---:|---:|
| scene | 19.33 | 87.8% |
| shadow | 0.85 | 3.9% |
| volume | 0.66 | 3.0% |
| cull | 0.33 | 1.5% |
| depth | 0.33 | 1.5% |
| ao | 0.20 | 0.9% |
| bloom + clusters + particles + tonemap | 0.34 | 1.5% |

The scene pass is **88% of the frame**. Shadows, the usual suspect, are 4%. This is measured by
`gpu::FrameTimeline`, which timestamps at each pass *end* so the intervals partition the frame
exactly — an earlier per-pass timer reported 39.4 ms for 5.4 ms of real work and was replaced.

### Passes, in order

cull (4-dispatch prefix-sum compaction) → depth prepass → shadow cascades → cluster build →
background/sky → scene (opaque, then alpha) → SDF raymarch → particles → volumetric march → GTAO →
post chain (exposure, bloom, composite, tonemap, output map).

### Culling and LOD

All GPU, all deterministic — prefix sums, no atomics, so the visible list is always in ascending
record order and the pass is a pure function of its inputs. One dispatch chain per object per frame
produces `DrawIndexedIndirect` args per LOD level. In Glowmere that reduces **114,287 candidate
instances to 2,325 drawn** across 110 draws, with the LOD ladder at 386/1,410/524/5.

---

## 3. Scene format

A **project** (`.json`) holds parameters, routes, sources, presets, timeline tracks and cues. It
points at a **composition** (`.scene.json`), which is a list of nodes plus camera, light rig,
environment, post settings, material programs and the wind field. Both round-trip: load and save
produces the same file, and fields that were never named are not written back.

Ten example worlds ship as data. **None of them required a line of C++** — that is the design
constraint the format is held to.

| Node kind | What it is |
|---|---|
| `Gltf` | An imported glTF/GLB scene, with its materials and textures |
| `Procedural` | A source primitive × a distribution, with deformers and a material program |
| `Terrain` | Heightfield chunks with LOD, plus scatter layers (the ecology) |
| `Sdf` | A signed-distance tree, raymarched or meshed by surface nets |
| `Field` | A scalar/vector field sampled identically on CPU and GPU |
| `Spline` | A curve used for distribution, deformation, emission or a camera rail |
| `Particles` | A GPU particle system that reads the same fields |
| `Scene` | Another composition, nested with a name prefix so it stays self-contained |
| `Orb`, `Grid` | Built-in primitives from earlier milestones |

Procedural nodes compose from **8 primitives** (box, cylinder, sphere, torus, point, tube, mesh, or
another procedural object), **8 distributions** (single, linear, grid, radial, spiral, spline,
grammar, scatter) and **7 vertex deformers** (bend, twist, sine, noise, displacement, field, spline
path). SDF trees have 8 primitives, 6 boolean and smooth-boolean operators, 8 transform and repeat
nodes, and 4 displacement nodes.

> **What still requires C++:** a new node kind, a new distribution, a new field type, a new material
> op, a new SDF node, or a new render pass. Everything expressible as a combination of those is
> data. In practice the last several months of work — an entire world, its ecology, its weather and
> its look — has been data plus small additions to the op sets.

---

## 4. Asset pipeline

**glTF / GLB** import via fastgltf: meshes, materials, textures, punctual lights and cameras, with
`KHR_materials_emissive_strength`. Imported meshes are usable as procedural *sources*, not only as
static props (ADR-044), which is how the vegetation works — a Quaternius fern becomes an instanced
scatter layer.

**Textures** arrive with the glTF (PNG/JPEG via stb) or as HDR/EXR for environments. **Environment
maps** are either an HDRI or the procedural sky (ADR-036/049), which doubles as the IBL source so
metals always have something to reflect. Output writes PNG, HDR, EXR (half or float, ZIP) and video.

### Licensing posture

`assets/manifest.json` records *source* and *licence* per pack, and is a curated list rather than a
directory scan — *"only names listed here may be placed by procedural generation."* The Kenney
Nature Kit is CC0. The heavy packs (`nature/`, `terrain/`, `quaternius/`) and all `.hdr` files are
**gitignored**: the repo carries manifests and fetch scripts, not redistributed third-party
binaries.

Optional runtimes follow the same rule. ffmpeg is spawned as the user's own process so no libav code
ever links in; NDI is `dlopen`'d if present and reports "not available" if not. Neither is ever
shipped.

> **The gap: no animation.** glTF animations and skins are explicitly rejected at import —
> `"N animation(s) ignored (not supported in 0.2)"`. Draco compression is also unsupported.
> Everything that moves in a scene moves because a *parameter* moves it: the wind field, effectors
> applying a field to instances, vertex deformers, particle simulation, spline paths, timeline
> keyframes, or audio modulation. That is a deliberate architecture, but it means an authored
> character performance cannot currently be imported.

---

## 5. Shaders

8,126 lines of WGSL across 30 files, assembled by `gpu::ShaderLibrary` with a simple include
mechanism. `common.wgsl` declares `FrameUniforms` once and mirrors `rendering/scene_renderer.hpp`
exactly; a single `static_assert` on `sizeof(FrameUniforms)` is the only description of that layout,
and every binding derives from it.

Shaders load **from the working tree at runtime**, not baked into the binary, so a shader edit needs
no rebuild — and the binary warns when they are newer than itself, because a uniform block changed
on one side only will fail pipeline creation with a `minBindingSize` mismatch.

Notable files: `pbr_shade.wgsl` (the whole material evaluation, styled and PBR branches),
`material.wgsl` (the ADR-030 op interpreter), `cull.wgsl`, `volume.wgsl`, `gtao.wgsl`, `wind.wgsl`
(a transliteration of `core/wind.cpp`, with tests comparing the two within float rounding),
`sdf_raymarch.wgsl`, `chroma.wgsl`, `tonemap.wgsl`.

Users can also add their own WGSL post layers under a documented contract (ADR-014).

---

## 6. Lighting and materials

**Lights:** seven types — directional, point, spot, and the four area emitters (rect, disk, tube,
sphere) integrated with linearly-transformed cosines. Rigs express lights *relative to a subject*:
intensity is a ratio against the key and converted photometrically, so a rig looks the same in a
room and in a cathedral. Six roles (key, fill, rim, back, ambient, practical) that rigs and the
inspector use and shading never does.

**Shadows:** cascaded shadow maps, cascade count authorable per scene because it is a property of
the world's scale, plus optional contact shadows. **Occlusion:** ground-truth ambient occlusion
(GTAO) with bent normals.

**Materials:** standard metallic-roughness PBR with base colour, normal, metal-rough, emissive and
occlusion textures — plus a procedural *material program* layer (ADR-030/036/050). A program is a
list of ops over eight vec4 registers, maximum 48 ops, interpreted on the GPU.

**30 op kinds:** input, constant, gradient, noise, voronoi, fresnel, ramp, remap, multiply, add,
mix, mixBy, power, smoothstep, threshold, hueShift, saturate, palette, field, triplanar,
worldProject, objectProject, heightBlend, detailNormal, curvatureMask, edgeWear, decalBox,
anisotropy, roughnessFilter, microDetail, swizzle.

**28 inputs:** world and local position, normal, uv, object and instance identity, instance
random/colour/emissive, time, **audio bands, beat phase**, view direction, depth, curvature,
convexity, concavity, cavity, occlusion, height, normal variance, triplanar weights, camera
distance, material id, screen footprint.

Audio and beat phase being *material inputs* is the point of the engine: a surface can respond to
the music without any CPU round-trip.

Beyond that: volumetric atmosphere with a height-falloff mist layer, noise, a density field and
local-light scattering; an **ecology light field** that aggregates glowing vegetation into clustered
point lights so a patch of fungi actually illuminates the ground; a **living chromatic field** that
drifts hue in world space and time inside the vertex shader; and a physically-motivated **wind
field** of travelling plane waves and gust fronts, with a driven damped harmonic oscillator solved
per species on the CPU.

> **The honest assessment:** the lighting and material system is substantially ahead of what the
> current showpiece scene uses. Area lights, IBL, contact shadows, triplanar projection, curvature
> masks and edge wear are all implemented and tested; the Glowmere scene uses two directional
> lights, a point light, the ecology field, and four material programs of three to twelve ops each.

---

## 7. Performance

Target platform is **macOS 26 on Apple silicon**; the prebuilt Dawn archive is arm64 only. All
numbers are measured on an **M2 Max**. The working goal is 60 FPS / 16.67 ms; no other Apple silicon
model has been tested, so any claim about a base M1 or M2 is unverified.

### Resolution scaling

`glowmere-stylized` · realtime tier · gpu frame median

| Resolution | Megapixels | GPU frame | FPS |
|---|---:|---:|---:|
| 1440 × 900 | 1.30 | 21.43 ms | 46.7 |
| 1920 × 1200 | 2.30 | 25.69 ms | 38.9 |
| 2880 × 1800 | 5.18 | 42.27 ms | 23.7 |

Fitting those gives **t = 14.5 ms + 5.4 ms per megapixel**. At a full retina window, 28 of the 42 ms
is fill. That is the entire explanation for "sub 20 FPS at standard app size", and it means
resolution scaling — not geometry reduction — is the largest untried lever.

### Three-arm benchmark

300 frames · 1440×900 · three interleaved rounds · quiet machine · gpu frame median

| Scene | R1 | R2 | R3 | Reading |
|---|---:|---:|---:|---|
| `_skyonly` (calibration) | 4.06 | 4.00 | 4.06 | Floor cost of the pass chain |
| `glowmere-stylized` | 20.84 | 20.97 | — | The product scene |
| `glowmere-stylized-pbr` | 20.84 | 20.64 | 20.71 | Same scene, PBR shading — the painterly path is free |
| `terrain` | 37.81 | 37.81 | 37.62 | 1.8× the product scene; not the thing to optimise |

Reproducible to ±0.2 ms across rounds. Scene complexity per frame: **15.1 million triangles**, 110
draws plus 121 shadow draws, 2,325 visible instances from 114,287 candidates, 2 shadow cascades at
2048², GTAO at 640×400, 13 post passes.

> **Measurement discipline, learned the hard way.** Frame *median* is unusable under machine
> contention — the same scene gave medians of 94.9, 102.6 and 86.8 ms against minima of 42.0, 26.9
> and 23.4. Benchmarks now use the GPU timeline's own median on a quiet machine with a sky-only
> calibration arm. And every A/B prints something proving the edit applied: three separate wrong
> conclusions in this project came from edits that were silently no-ops.

---

## 8. Implementation state

925 tests — 778 unit, 147 GPU — with sanitizer presets (ASan/UBSan, TSan) that are clean on the
scene tags. Four tests skip by design: two optional Khronos sample imports, external ffmpeg, and the
NDI runtime.

| Area | Status | Note |
|---|---|---|
| Renderer core, culling, LOD | **Production** | Deterministic, GPU-tested against CPU references |
| Parameters, modulation, projects | **Production** | Typed, serialised, migrated, round-tripped |
| Audio analysis, beat tracking | **Production** | Bit-identical goldens against KissFFT |
| Procedural geometry, SDF, fields | **Production** | CPU and GPU sample identically, by test |
| Material programs | **Production** | 30 ops, cost-bounded, interpreted on GPU |
| Offline render: PNG, EXR, native video | **Production** | Deterministic frame sequences |
| Vegetation simulation (Tier 1 rods) | *Partial* | Implemented and tested; currently **disabled** on four Glowmere layers because it read as glitchy |
| Audio-reactive scene authoring | *Partial* | Infrastructure complete; the showpiece scene wires only two routes |
| Syphon output | **Unresolved** | A burst-publish failure passed once and was never diagnosed. Not fixed. |
| Test suite under `-j4` | **Flaky** | A different small set fails each run — HEVC encode, asset relink, bundle temp dir, GPU readback ring. Each passes alone; never anything scene-related. |
| glTF animation & skinning | Not built | Rejected at import with a warning |
| MSAA / TAA | Not built | Deliberate on TBDR; consequences below |

### Open visual defects

- **Foliage edge crawl under motion.** Alpha-tested leaf cards with no MSAA and no TAA. Distant
  crown *erosion* was fixed today; the crawl was not, and I could not even measure it — the metric
  was swamped by parallax.
- **Hero geometry.** The elder mushroom's cap meets its stem as a plain cylinder against a disc with
  no flare, and the gill rhythm is perfectly periodic. Neither is a deformer's job.
- **Nothing is placed relative to anything else.** Scatter layers have proximity rules, but there is
  no general "put this near that" relation.
- **Material parameters cannot address one part of a multi-material asset.** A tree's bark and
  leaves share one handle.
- **A material program that writes `baseColor` makes the node's authored colour dead data.** Found
  today: scaling the terrain's `material.baseColor` by 0.5 and 0.3 produced byte-identical frames.
  The colour lived in the program's ramp constants.

### Changed in the last three commits, all today

- **ADR-058 — the air and the ambient.** The styled hemisphere ambient was a shader constant
  outgunning the key light 2.5:1; the surface fog ignored the mist layer the volumetric marches; and
  the fog colour matched the ground's luminance so distance could not separate. All three fixed.
  Defaults are bit-identical for every other scene — verified at 0 of 2,304,000 pixels differing.
- **Coverage-preserving alpha cutoff.** Against a 2× supersampled reference, ten times closer than
  the fixed cutoff.
- **The glow made dominant.** Exposure runs before bloom, so darkening the world had silently
  switched most emitters off.

> Measured across the whole 90-second shot, frames 120/600/1200 — **shadow fraction went from
> 0.0000, 0.0000, 0.0000 to 0.287, 0.356, 0.239**. Not one pixel anywhere in the movie had been
> below 8% luminance. That is what "the scene was flat" meant, and why it was a structural fix
> rather than a slider.

---

## 9. Where the pixels come from

The intended mix, and the actual mix, are close:

- **Procedural — the majority.** Terrain heightfields, biome assignment, ecology placement, point
  clouds, fields, splines, SDF bodies, deformation, particles, the wind field, colour fields, the
  sky, and every material program. The world is generated from seeds and rules, not laid out by
  hand.
- **Imported — the meshes only.** CC0 and permissively-licensed stylized packs (Kenney, Quaternius)
  supply plants, trees, rocks and fungi as *source geometry* for procedural instancing. They are
  placed by rules, scaled and tinted by fields, and deformed by the wind — the import is a mesh, not
  a layout.
- **Authored — the direction.** Camera tracks, light rigs, the emissive ladder, the palette, post
  settings, timeline cues. Roughly the decisions a director of photography would make, expressed as
  data.
- **AI-assisted — none in the runtime.** There is no generative model in the engine, no diffusion,
  no learned upscaler, no neural denoiser. Every pixel is computed from the geometry, the lights and
  the shader code.

The standing constraint on all of it: use appropriately licensed, public-domain or permissioned
material; check the licence of every asset; never redistribute a third-party asset without verifying
its terms. Broad modern stylized rendering is inspiration, not permission to copy anyone's IP.

---

## 10. Current work

The active push is turning the showpiece into a finished product driven by audio. In flight:

- **A rights-clean score.** `tools/make_glowmere_score.py` renders 90 seconds of D-Dorian ambient at
  72 BPM from pure standard library — a drone, a chord loop, a low pulse on beats 1 and 3, seeded
  pentatonic bells and an air bed. Generated rather than licensed, so there is nothing to clear. It
  is written to be *analyzed*: deliberate transients for the beat tracker, bell attacks for the
  treble routes, a noise bed that walks the spectral centroid without producing onsets. Rendered and
  in the tree.
- **Musical routing.** The scene currently wires two routes. It needs a full mapping: glow on the
  beat, spores on treble transients, wind energy on RMS, hue drift on spectral centroid, the elder
  pulsing on bass.
- **Motion smoothness.** Under investigation. The wind model itself is slow and smooth — every
  species resonates between 0.06 and 0.63 Hz, and the field's own periods are 9.5 to 33 seconds — so
  the glitchiness is something else. With wind fully disabled, 8.6% of all pixels still change by
  more than 24/255 between consecutive frames. The culler makes hard binary cuts at screen radius,
  view distance, every LOD threshold and a density hash, which was the leading hypothesis; a first
  test removing screen-radius culling did *not* reduce the churn (9.98% against 8.6%), so that
  hypothesis is not supported and the search continues.
- **Frame rate at window size.** 23.7 FPS at full retina against a 60 FPS goal. Resolution scaling is
  the identified lever and is not yet built.
