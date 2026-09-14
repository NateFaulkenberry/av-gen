# Glowmere Valley — what it actually is, and what of it survives

Phase 1 deliverables 1 and 2. Read against `c232776`, 2026-09-14. **Nothing was modified to write
this**, in the same spirit as `docs/glowmere-world-builder-audit.md`, which this document extends
rather than repeats: that audit answered "what makes the picture work" and its seven findings are
still correct. This one answers "what is it made of, where does each piece live, and does it come
with us".

---

## 0. The first thing to know: there is no single Glowmere Valley

**"Glowmere Valley" is seven entries in `examples/index.json` over two scene files, and the entry
literally named *Glowmere Valley* is the wrong one.**

| index.json entry | file | what it is |
|---|---|---|
| Glowmere Valley **- Painterly** | `world/glowmere-stylized.json` | **the reference look.** 16 nodes, 5 heroes, wanderer, UFO, water dressing |
| Glowmere Valley **- Lyrics** | `composition/glowmere-lyrics.json` | the same scene under a 2D composition |
| Glowmere Valley **- Matched PBR** | `world/glowmere-stylized-pbr.json` | the same scene file, `stylized: false`, as a controlled comparison |
| **Glowmere Valley** | `world/terrain.json` | the older generated 38k-instance showcase. 6 nodes. **No heroes block, no wanderer, no UFO, no water dressing** |
| Glowmere Density - Low / Medium / Dense / Extreme | `recipes/glowmere-*.recipe.json` | a separate composer pipeline; never touches either scene file |

Both scene files carry `"name": "Glowmere Valley"`. `docs/glowmere-world-builder-audit.md:8-12`
already ruled `terrain.json` out of scope and "not to be treated as the reference look", and every
ADR since — 126, 138, 150–155, 158 — measures `glowmere-stylized.scene.json`.

**This audit therefore takes `examples/world/glowmere-stylized.scene.json` as "Glowmere Valley", and
the ambiguity is an open question for the user** (`04-plan.md` Q1), because the brief says
"Glowmere Valley 2" and there is a real risk of building the successor to the wrong parent.

There is also no scene *identifier*. `src/app/examples.cpp:48-54` reads `project`, `scene` and
`recipe` into the same field — the distinction is documentation, not routing — and selection is by
display-name match (`src/app/application.cpp:1036-1054`). Glowmere Valley 2 is therefore **one new
entry in a JSON file**; nothing is compiled in.

---

## 1. Inventory (brief §3.1)

### 1.1 Terrain — hardcoded in C++, not in the scene

**Neither scene file contains a heightfield.** The terrain node has no `"world"` key, and
`src/scene/composition.cpp:5861-5874` documents that `{"kind": "terrain"}` alone yields the shipped
world at shipped defaults. That world is `world::defaultWorld()`, `src/world/world_map.cpp:480`, and
its `name` is literally `"glowmere"`: seed `20260909`, size **640 × 640 m**, erosion 0.35, six noise
octaves (0.0030/32 m → 0.3400/0.45 m, the third ridged at 0.72).

Geography is **13 authored `Feature` stamps** (`world_map.cpp:517-598`) — `northern-rim`,
`west-arm`, `east-arm`, `west-spur`, `basin-floor`, **`glowmere-run`**, `west-tarn`,
`west-tarn-bed`, `the-hollow`, `upper-shelf`, `lower-shelf`, `south-knoll`, `hollow-knoll`,
`east-gully`, `south-terrace` — each a polyline with a width, a flatten weight and Chaikin
smoothing.

**This is the single most consequential finding for Phase 2.** The engine already builds terrain by
stamping authored features onto noise, which is exactly the architecture `02-research.md` §1.2
selects. The work is authoring and two new terms, not a new system.

### 1.2 The river

`glowmere-run`: `FeatureKind::River`, 15 control points, **width 7 m**, amplitude 2.4 m, roughness
0.12, `water = true`, `waterDepth = 0`. Plus `west-tarn-bed`, a still pool at `waterDepth 1.4`.
`seaLevel = -1000`, so there is no sea.

**It does not traverse the map.** Seven metres of channel on 640 m of ground, beginning and ending
inside the bounds. This is the brief's §4.1 gap, and the baseline capture shows what it looks like:
a bright ribbon behind the elder with no direction.

### 1.3 Water system

`world::buildChunkWater` (`src/world/terrain.hpp:193`) emits a quad per chunk-field cell with any
corner underwater, sampling `WorldMap::waterSurface` — **so it already follows a descending course
down a valley**, and the shoreline is where the ground and water surfaces cross, not where a mesh
boundary fell. Above it: `world::waterCourses()` → downstream-ordered `WaterCourse`;
`world::waterBodies()` → `WaterBody` with a cumulative arc table, `pointAt`, `tangentAt`,
`shearProfile`, `wettedHalfWidth`.

The vertex format is deliberately overloaded: `uv = (depth, channel)`, `normal.xz` = downstream flow
direction, `normal.y` = speed as a fraction of the body's fastest. Rendered by
`rendering::WaterRenderer` as a pipeline *inside* the scene pass, blended, excluded from the depth
prepass — so the linear depth it reads is the bed, which is what thickness and shoreline are made of.

The stylized scene authors a full 31-field `water` block (clarity 1.25, foam 0.6, glow 1.2 at
`[0.15, 1.0, 0.75]`, sparkle 0.6, reflection 8.0) plus a `flow` block (`bankShear 0.7`,
`meander 0.18`, `stillFactor 0.12`). `terrain.scene.json` authors neither, so its river is still.

### 1.4 Sky, moon, stars

**There are no stars in the painterly scene and no star field in the engine's scene data.** The sky
is analytic (`src/scene/sky.hpp`): a zenith/horizon/ground gradient plus a sun disc whose direction
is the first directional Key light. The "moon" *is* that disc. (The stylized surface path does draw
direction-anchored filtered stars in the procedural sky — `docs/stylized-glowmere.md:49-52` — which
is what the baseline capture shows; they are a renderer-mode feature, not scene content.)

`terrain.scene.json` is HDRI-backed (`kloppenheim_02_puresky_4k.hdr`, intensity 0.12,
`lightFromEnvironment: true`). The painterly scene has **no HDRI at all**: `stylized: true`,
`lightFromEnvironment: false`, zenith `[0.008, 0.016, 0.048]`, horizon `[0.04, 0.08, 0.17]`.

### 1.5 Terrain material

Painterly: `paintedGround` — three ops. `uv` → a three-stop `ramp` over teal/grey-violet → a flat
0.95 roughness. A broad colour region, not a texture. Plus generated ground glow in
`world::terrainMaterialProgram` (`src/world/terrain.cpp:511-600`): a **Voronoi** field at
`groundGlowScale 0.055`, smoothstepped at `coverage × 0.55`, times `[0.08, 1.0, 0.68] × 0.08`. Its
cost does not grow with distance, "which is the only way the far hillside gets any bioluminescence
at all."

### 1.6 Vegetation — 13 scatter layers, one asset pack

All 13 are `world::ScatterLayer` inside the terrain node. Pure Quaternius (CC0), catalogued in
`assets/glowmere.manifest.json` with the heights and importances read back out of the scene.

The emission ladder — the scene's most important single artefact, now also encoded as
`world::EmissionLadder` (`src/world/art_direction.hpp:41-55`) with a validator enforcing a minimum
4× gap:

| rung | layers | intensity | emissiveColor |
|---|---|---:|---|
| inert | deadwood, boulders, pebbles | **0** | — |
| silhouette | canopy, pines | 0.035 | `[1.0, 0.82, 0.45]` |
| ground cover | ferns, grass, fan-plants | 0.0615 | `[0.02, 1.0, 0.58]` etc. |
| noticeable | bushes | 0.2952 | `[0.55, 0.14, 1.0]` |
| *(13.3× gap)* | | | |
| special | flowers | 3.936 | `[0.3085, 0.1208, 1.0]` |
| rare | shelf-fungi | 4.428 | `[0.05, 1.0, 0.62]` |
| beacon | beacons | 6.15 | `[0.06, 0.82, 1.0]` |
| brightest | fungi | **6.888** | `[0.341, 0.0821, 1.0]` |

Ceilings: grass 120,000 · ferns 16,000 · pebbles 14,000 · bushes 9,000 · fungi 6,000 · flowers 5,000
· boulders 4,000 · fan-plants 3,000 · canopy 2,000 · pines 1,600 · shelf-fungi 900 · deadwood 600 ·
beacons 400.

### 1.7 Vegetation placement — the actual algorithm

`world::scatter`, `src/world/ecology.cpp:299-447`. **It is a jittered grid with an fBm cluster
weight and Bernoulli acceptance** — not random scatter, and not Poisson disk.

Cell side is one expected instance at peak density, `clamp(sqrt(1/peak), 0.25, 64)` m. One candidate
per cell, jittered within it, then in order: bounds → **proximity** (a hashed spatial bin,
`HabitatIndex`, against a host layer's cloud, smoothstepped) → `map.sample` → water avoidance →
hard slope and altitude bands → clearance weight → biome weights dotted with the layer's densities →
**clustering** (`fbm3(p / clusterScale)` remapped and mixed by `layer.clustering`) → acceptance
`random01(seed, cellId, kAcceptChannel) < density × cellArea × habitatWeight × clearing`.

Randomness is `noise::hashIndex(seed, cellId, channel)` on fixed channels 11–15 — **stateless,
order-independent, fully deterministic.** The same world always grows the same forest.

`ScatterProximity` relations already in use: ferns→canopy (1.5–16 m, 0.55), fan-plants→canopy
(1–22 m, 0.65), fungi→ferns (0.3–4 m, 0.75), pebbles→boulders (0.7–5 m, 0.8).

**Two real defects.** `maxInstances` is enforced with `break` on the *inner* loop (`ecology.cpp:347`),
so hitting a ceiling truncates row-by-row from −Z instead of thinning uniformly — invisible at
640 m, a visible hard edge on a larger map. And the cluster noise is world-space and independent of
the cell grid, so two layers sharing a `clusterScale` clump in the *same* places, which is a useful
association mechanism being used by accident rather than by declaration.

### 1.8 Mushrooms — two unrelated things

- **Scattered mushrooms are glTF assets.** `fungi` and `beacons` are *the same mesh*,
  `Mushroom_Common.gltf`, at 0.28 m and 1.5 m, reading as two species. `shelf-fungi` is
  `Mushroom_Laetiporus.gltf`. All three carry `materialProgram: "glowmereTissue"`.
- **The hero "elder" is 100% procedural** and contains no mushroom mesh: a 96×48 sphere squashed to
  `[8.2, 2.0, 7.4]` with two displacement deformers, on a 5-point Catmull-Rom tube (radius 1.25,
  taper 0.65, scales 1.5 → 0.85 → 0.7 → 0.9 → 1.4, so it *curves*), with 38 radial filament tubes at
  radius 6.8.

### 1.9 Heroes — three of five are primitives

| hero | importance | what it is | §3.4 verdict |
|---|---:|---|---|
| `elder-crown` | 0.95 | the three-node elder above | **keep the role, replace the geometry** |
| `monument-spire` | 0.80 | one tube, **7 sides**, taper 0.22 | **remove** |
| `far-arch` | 0.72 | **one torus**, major 15, minor 1.6 | **remove** |
| `visitor` | 0.68 | `ufo.gltf`, metallic 0.88 | **keep** |
| `beacon-grove` | 0.62 | **nine squashed spheres**, radial count 9 | **remove** |

**The Wanderer is not in the heroes list at all.** It is an entity with a node, and the brief calls
it a character hero. That is a one-line fix and a real finding.

`world::HeroPoint::assetId` is one object by design (ADR-107), which is why the elder — three nodes —
cannot be a composer-placed hero and only works hand-authored. `docs/glowmere-world-builder-audit.md`
calls the hero sub-assembly "the highest-value unbuilt thing", and the brief's §7 is that.

### 1.10 The Wanderer

`alien.gltf` at (165, 23.08, 20), scale 0.08. Clips idle/walk/run/turn/observe/react →
`Idle`/`Walk`/`Run`/`Walk`/`Idle`/`Idle` (so turn, observe and react are aliases, not clips).
Behaviours: `interest` (signal `music.impact`, subjects `["visitor", "elder", "beacon-grove"]`),
`explore` (speed 5, run 11, bodyRadius 1.6, glowAffinity 1.35, characterAffinity 1.25,
landmarkAffinity 1.0, waterAffinity 1.0, vistaAffinity 0.8, noveltyRadius 28), `lookAt`.

**Defect: the `interest` subject `"elder"` matches no node and no hero.** The nodes are
`elder-crown` / `elder-stem` / `elder-filaments`. Whether this resolves or silently no-ops is
**unverified** — it is the one inventory item this audit could not settle by reading.

### 1.11 The Visitor UFO

`ufo.gltf`, metallic 0.88, roughness 0.3, at (−45, 31.7, 185). Child particle node `visitor-beam`
(disc, capacity 16,384, spawnRate 1050, `velocityStretch 4.5`, additive). Behaviours: `hover`
(amplitude 0.85, tilt 1.7), `drift` (radius 2.4), `bank` (5°), `spin` (signal `audio.beat`,
impulse 13, maxRate 30).

### 1.12 Water dressing and air

Three floating layers bound to the water body — `river-lilies` (Plant_7, 150), `tarn-lilies`
(Plant_7_Big), `river-petals` (Petal_5) — via `scene::FloatSpec`, which is **stateless**:
`placement(i, t) = body.pointAt(wrap(s_i + v_i·t)) + lateral_i`, so scrubbing and out-of-order
offline rendering are exact. Plus `river-motes`, a particle system emitting **along the river's
centreline** (`shape: "spline"`, `spline: "valley.glowmere-run"`), and `spores`, a 110 × 16 × 110 m
box emitter, capacity 10,240, spawnRate 300.

### 1.13 Lighting

`examples/lightrigs/glowmere-valley.rig.json` (rig name `GlowmereValley`): key **4.5**, ambient
**0.6** — 7.5:1. Moon directional, azimuth 64°, **elevation 22°**, `[0.42, 0.62, 1.0]`, 7800 K,
shadow strength 0.85, volumetric 0.12. `skyfill` at −108°/34°, 0.35. `elder-practical`, a green
point light `[0.12, 0.75, 0.42]` at intensity 3.0 under the crown.

`glowmere.rig.json` is an **earlier, unused variant** — rig name `GlowmereMoon`, key 1.3 (3.5×
dimmer), moon `[0.74, 0.84, 1.0]` (cooler) — referenced only by `terrain.scene.json`. That answers
the "what is the difference between those two files" question: everything else is byte-identical.

Engine capacity: clustered forward, 16 × 8 × 24 froxels, **256 scene lights**, 32 per froxel,
directionals evaluated outside the grid. Shadows: **8 shadow views total**, ≤4 cascades; Glowmere
sets `shadowCascades: 2`. Point and area lights get no shadow map in practice.

**ADR-053's ecology light field is how the glow casts:** `world::aggregateGlow` bins scatter
placements into 9 m cells and reduces each to one soft emitter; the nearest **224** within 120 m
become ordinary point lights each frame, `castsShadow = false`, `volumetricStrength = 0`. Measured
at 4.0 ms at 2880×1800. **This is the answer to the brief's §6 "no dynamic light per plant"** — it
already exists and is already the right shape.

### 1.14 Post and volumetrics

Post order: meter (pre-exposure) → exposure → defocus → motion blur → lens → bloom → halation →
anamorphic → composite → FXAA → sharpen, then a **separate** tone-map pass. So **bloom samples
linear HDR strictly before tonemapping**, and its threshold is in exposed-linear units. All five MRT
targets are RGBA16Float (radiance, normal+roughness, velocity, **emission**, identifiers); nothing
clamps before the tone map, and emissive parameters range to 50 and 100.

Glowmere: AgX, `chromaRetention 0.6`, bloom intensity **0.18** at threshold **1.0**, knee 0.5,
radius 1.15, 6 levels, `bloomEmissionWeight 0.75`, exposure −1.3 EV. Halation, anamorphic, DoF,
tilt-shift, motion blur, grain and vignette all off.

Volumetrics: `density 0.006`, `scattering 0.5`, **`anisotropy 0.12`** — near-isotropic on purpose —
12 steps, 320 m, absorption 0.4, `volumeLocalLights: 1.0`. **There are no true light shafts:** the
march does not sample shadow maps (`shaders/volume.wgsl:118`). Distance fog `[0.1, 0.18, 0.32]` at
0.0055, height 4.0, falloff 0.1.

### 1.15 Special shaders

`environment.stylized` is a **renderer mode**, not a shader file — one branch at
`shaders/pbr_shade.wgsl:326`. It gives soft light bands, a hemisphere ambient, a **floored** AO
(full-strength SSAO printed a lattice on open ground) and a subtle rim; it bypasses normal maps,
metal/roughness textures and split-sum IBL. Emissive textures and material programs still run.

Five material programs are loaded, all register-machine programs in `examples/materials/`:

- **`paintedGround`** — 3 ops, a UV ramp.
- **`paintedCrown`** — 17 ops. A downward `gradient` as an underside mask; local-position `noise`
  (1.4, seed 617) into a violet ramp; the UV **warped by that noise** and fed to a cosine `palette`
  at **frequency 38**, powered and remapped — that is the noise-warped radial gill rhythm. Warm gill
  constant `[1.0, 0.38, 0.10]`, `emissionIntensity 1.7`.
- **`glowmereTissue`** — 14 ops. A `fresnel` 3.5 mixed into the underside gradient for grazing
  translucency, plus an `instanceEmissive` input so per-instance variation reaches the program.
- **`paintedFrond`** — 5 ops; emission is a vertical gradient, so the frond glows from its base out.
- **`bushGlow`**.

The painterly idiom, stated: three-stop ramps over UV / local position instead of texture lookups; a
downward gradient as an underside mask; noise-warped cosine palettes for gill rhythm; fresnel for
tissue translucency. **No normal maps, no IBL, broad colour regions, clear silhouettes.**

### 1.16 Audio-reactive modulation (brief §10)

Architecture: `audio → analysis → signals → ModRoute → ParameterSet finals → Composition → GPU`,
with a strict per-frame order in `Engine::update` — sources, reset finals, **timeline first**
(ADR-018), spatial field gains, routes, behaviour offsets, then the scene reads the finals.
`ModRoute` chains are fixed-order: gain → offset → curve → clamp → threshold → asymmetric one-pole
smoothing → envelope → remap, with frame-rate-independent smoothing `1 - exp(-dt/tau)`.

**Glowmere has 23 routes, not the 16 the docs claim**, plus 8 entity reactions. The full table is in
the audit appendix below. The shape:

| group | count | notable |
|---|---:|---|
| continuous | 2 | `audio.bass → paintedCrown/emissionIntensity` (+0.16, 1200/3000 ms) |
| musical events | 14 | `music.drop` drives four things at once, including `elder-filaments/distribution/radius` +0.30 |
| **water and river** | **7** | **undocumented anywhere** — water glow, ripple, swell, sparkle, foam, mote spawn and emissive |
| entity reactions | 8 | UFO height from `audio.bass` (depth −0.95), wanderer speed from `audio.rms` (depth 3.4) |

**The most important finding here, and it is a defect:** `glowmereTissue` is referenced by exactly
**three of thirteen** scatter layers — fungi, shelf-fungi, beacons — **1,771 instances. The other
111,789 cannot see the `music.beat` and `music.impact` routes at all.** The scene's headline
audio-reactive relationship reaches 1.6% of its vegetation.

---

## 2. The reuse matrix (brief §3.2)

Verdicts: **reuse** unchanged · **configure** (same code, new values) · **optimise** (same idea,
needs work) · **replace** · **remove**.

### Geography and environment

| element | where | verdict | reason |
|---|---|---|---|
| `WorldMap` feature-stamping architecture | `src/world/world_map.cpp` | **reuse** | already the selected architecture (`02-research.md` §1.2); stamps ridges, valleys, flats and rivers onto octave noise |
| `defaultWorld()` itself — the 640 m map, 13 features | `world_map.cpp:480` | **replace** | it is Glowmere Valley's geography. GV2 needs its own, and must not edit this one or the original changes |
| `glowmere-run`, 7 m, non-traversing | `world_map.cpp:553` | **replace** | the brief's §4.1 is precisely that this does not traverse |
| noise amplitude as a function of distance to river | — | **new** | the term that stops noise destroying the valley; does not exist |
| `HAR` (height above river) as a field | — | **new** | `height - waterSurface` exists per-sample but only where there is water |
| `WaterCourse` / `WaterBody` / arc tables | `src/world/terrain_water.*`, `water.*` | **reuse** | downstream ordering, `wettedHalfWidth`, `shearProfile` are exactly what a river corridor needs |
| `buildChunkWater` | `src/world/terrain.hpp:193` | **reuse unchanged** | already follows a descending course; §4.3 says don't add complexity and the measurement agrees |
| water look + flow blocks | `glowmere-stylized.scene.json` | **configure** | 31 authored fields, tuned; port the values, widen for a real river |
| `TerrainSettings` chunking, LOD, skirts | `src/world/terrain.hpp` | **reuse** | but `viewDistance 520` and `chunkSize 40` are tuned for 640 m and must be re-derived for a larger map |
| analytic sky, moon-as-sun-disc | `src/scene/sky.hpp` | **reuse** | |
| `glowmere-valley.rig.json` | `examples/lightrigs/` | **configure** | copy to a GV2 rig; **the rig's `name` is bound by a route** — renaming silently unbinds it |
| `glowmere.rig.json` | `examples/lightrigs/` | **leave alone** | belongs to `terrain.scene.json` |
| atmosphere + volumetrics values | `art_direction.cpp`, scene | **configure** | anisotropy 0.12 and the 7.5:1 ratio are on the audit's do-not-change list |

### Vegetation

| element | where | verdict | reason |
|---|---|---|---|
| `world::ScatterLayer` (40 fields) | `src/world/ecology.hpp:145` | **reuse** | emissive colour/intensity, height, hue field, chroma drift, sparsity, material program, wind — all already there |
| `world::scatter` jittered grid | `ecology.cpp:299` | **optimise** | keep the structure, the determinism and the channel scheme; add HAR, per-species spacing and competition |
| fBm macro-clustering (`clustering`, `clusterScale`) | `ecology.cpp:402` | **reuse** | this is `02-research.md` §2.3 Stage B, already built |
| `ScatterProximity` + `HabitatIndex` | `ecology.cpp:59` | **reuse** | Stage E's association rule, already built and already in use |
| `ScatterClearance` | `src/world/ecology.hpp` | **reuse** | the brief's §4.5 negative space, already a first-class type |
| slope / altitude bands | `ecology.cpp` | **configure** | hard and unfeathered; a HAR ladder wants soft shoulders |
| `maxInstances` inner-loop `break` | `ecology.cpp:347` | **replace** | truncates from −Z instead of thinning; a visible edge on a bigger map |
| Poisson-disk / variable-radius spacing | — | **new** | does not exist anywhere; `app::planPlacements` is O(n²) dart throwing on a disc |
| FON competition | — | **new** | |
| the 13 Quaternius species + manifest | `assets/glowmere.manifest.json` | **reuse unchanged** | CC0, curated, one artistic language, heights already calibrated |
| the emission ladder | `art_direction.hpp:41` | **reuse** | its *shape* is on the do-not-change list; the values are a starting point |

### Heroes

| element | verdict | reason |
|---|---|---|
| `elder-crown` / `-stem` / `-filaments` geometry | **replace** | "an overly regular hero" is a recorded defect and the baseline capture shows a perfect ellipse. The *stem* — a curved 5-point tube — is good and its technique carries forward |
| the elder's **role** (one warm hero in a cool world) | **reuse** | the audit's do-not-change rule, and it is why the eye goes to it |
| `monument-spire` | **remove** | a 7-sided tapered tube. Brief §3.4 |
| `far-arch` | **remove** | a torus. Brief §3.4 |
| `beacon-grove` | **remove** | nine squashed spheres. Brief §3.4 |
| `visitor` (UFO) + `visitor-beam` + behaviours | **reuse** | a real model with real behaviour; stage it better (§7) |
| `wanderer` + clips + behaviours | **reuse, and promote to a hero** | it is not in the heroes list today |
| `foreground-leaves` | **configure** | one hand-placed plant is the entire foreground-framing device, and it works |
| `HeroPoint` | **reuse** | but `assetId` is one object (ADR-107), so a multi-node mushroom needs `HeroPoint::assembly` — the named seam |

### Mushrooms

| element | verdict | reason |
|---|---|---|
| `makeTube` + `SplinePoint::scale` as a radius profile | **reuse** | `docs/visual-cookbook/fungi.md`: "a cap is a radius profile, not a new primitive" |
| Catmull-Rom curves, arc-length tables, RMF frames | **reuse** | `src/spatial/spline.hpp` is strong |
| **per-angle radius modulation on the sweep** | **new** | `makeTube` is circular-only. This is the one missing primitive |
| a public `makeLathe(profile, segments)` | **new (extraction)** | one exists hard-coded inside `makeBeveledCylinder`; ~40 lines to extract |
| `paintedCrown` (noise-warped cosine gills) | **configure** | the technique is right; the palette-38 rhythm is "perfectly periodic", a parked defect |
| `glowmereTissue` | **reuse** | fresnel translucency + `instanceEmissive` is the right material shape |
| `Mushroom_Common` at two scales | **reuse** | one mesh reading as two species is a technique worth keeping |
| per-instance lanes | **constraint, not an element** | `InstanceRecord` is 96 bytes with **exactly one** free lane (`emissive.a`) |

### Camera

| element | verdict | reason |
|---|---|---|
| the **bake to timeline keys** (ADR-075) | **reuse** | it is why offline and live agree; any per-frame camera would have to re-earn that |
| `DirectionBrief::continuous` | **optimise** | exists, defaults true, invisible to users, and only pins start points — position is C⁰, velocity is not |
| `Shot` / `ShotKind` / the section→shot table | **reuse** | becomes Edited-sequence mode |
| arc-length reparameterisation of camera paths | **new** | `pathLength`/`peakSpeed` measure but nothing reparameterises |
| damped aim | **new** | there is no damping anywhere in the camera code |
| `ClearanceField` | **optimise** | vertical-only and only ever raises — see `04-plan.md` R1 |
| `AimFollow` (ADR-158) | **reuse** | already makes a shot track a walking hero |
| `CompositionProfile::framing` / `headroom` | **remove or implement** | declared, serialised, read by nothing |
| `HeroPoint::preferredCameraElevationDegrees` | **remove or implement** | unused by the director |
| `validateCadence` | **wire up** | never called by production code |
| the name "Direct to Music" | **replace** | brief §9 — and it is a *new* name, not a substitution |

### Audio

| element | verdict | reason |
|---|---|---|
| the whole `signal → ModRoute → parameter` chain | **reuse** | brief §10 requires it |
| the 2 continuous + 14 event routes | **configure** | port with new targets; document the ranges |
| the 7 water/river routes | **reuse and document** | they work and are written down nowhere |
| the 8 entity reactions | **reuse** | |
| `glowmereTissue` reaching 1.6% of instances | **replace** | the headline relationship must reach the vegetation it is supposed to animate |
| `make_glowmere_score.py` + the 90 s track | **reuse** | |

### Rendering

| element | verdict | reason |
|---|---|---|
| GPU cull + LOD (4 dispatches, prefix-sum, no atomics) | **reuse** | 0.1 ms / 100k; deterministic ordering |
| ADR-053 ecology light field (224 lights, 9 m cells) | **reuse** | already the answer to "no light per plant" |
| clustered forward lighting, 256 lights | **reuse** | |
| the post chain and AgX + chroma retention | **reuse** | |
| **impostors / HLOD** | **do not propose** | ADR-151 and ADR-153 measured this content: the whole 2–8 px band is worth +1.8%, wrong sign |
| `RepresentationSelector` | **evaluate** | built, calibrated, unit-tested, wired to nothing; ADR-151 says connecting it is worth more than any new machinery |
| `kMaxGpuSplines = 16` | **defect** | Glowmere already logs "22 splines; only the first 16 are available on the GPU" |

---

## 3. Appendix — the 23 routes, in full

All `op: add`, `polarity: unipolar`, `component: -1`, `curve: linear`, no clamp, no remap.

| source | target | amount | attack/decay ms |
|---|---|---:|---|
| `audio.bass` | `material/paintedCrown/emissionIntensity` | +0.16 | 1200 / 3000 |
| `audio.treble` | `particles/spores/spawnRate` | +24 | 600 / 2000 |
| `music.beat` | `material/glowmereTissue/emissionIntensity` | +0.09 | 25 / 240 |
| `music.impact` | `material/glowmereTissue/emissionIntensity` | +0.22 | 12 / 900 |
| `music.downbeat` | `nodes/elder-filaments/emissiveBoost` | +0.10 | 45 / 620 |
| `music.downbeat` | `lightrig/GlowmereValley/elder-practical/intensity` | +0.30 | 60 / 900 |
| `music.phrase` | `particles/spores/turbulence` | +0.18 | 1400 / 3600 |
| `music.section` | `nodes/elder-crown/emissiveBoost` | +0.06 | 2200 / 6000 |
| `music.build` | `scene/windSpeed` | +0.18 | 2400 / 2800 |
| `music.build` | `particles/spores/spawnRate` | +70 | 2600 / 2400 |
| `music.break` | `scene/windSpeed` | **−0.20** | 1500 / 3000 |
| `music.break` | `particles/spores/spawnRate` | **−80** | 1400 / 3000 |
| `music.drop` | `nodes/elder-filaments/emissiveBoost` | +0.18 | 70 / 2200 |
| `music.drop` | `procedural/elder-filaments/distribution/radius` | +0.30 | 320 / 2600 |
| `music.drop` | `particles/spores/burst` | +220 | 10 / 260 |
| `music.drop` | `scene/volumeScattering` | +0.09 | 250 / 1800 |
| `audio.bass` | `nodes/valley/water/glow` | +1.3 | 90 / 900 |
| `audio.bass` | `nodes/valley/water/ripple` | +0.3 | 120 / 1100 |
| `music.beat` | `nodes/valley/water/swell` | +0.055 | 18 / 420 |
| `audio.treble` | `nodes/valley/water/sparkle` | +0.85 | 14 / 260 |
| `audio.rms` | `nodes/valley/water/foam` | +0.45 | 450 / 1800 |
| `audio.bass` | `particles/river-motes/spawnRate` | +70 | 140 / 1400 |
| `audio.treble` | `particles/river-motes/emissive` | +1.6 | 30 / 420 |

---

## 4. Verified vs assumed

**Verified by reading the files named:** every path, symbol, line number, JSON value and numeric
constant above. The rendered baseline frame. The example index's seven Glowmere entries.

**Assumed / unverified, and each could be wrong:**
- **That the wanderer's `interest` subject `"elder"` silently no-ops.** It matches no node and no
  hero by name. Not traced into `entity::`; it may resolve by prefix or by some other rule.
- **That `terrain.json` is not what the user means by "Glowmere Valley".** It is what the index
  literally names. This audit follows the prior audit and every recent ADR instead. **Open question.**
- **That no scene element was missed.** The inventory is from the scene JSON's 16 nodes plus the
  heroes, entities, post, wind and environment blocks. A node whose effect is entirely in the
  project file's 1134 parameter overrides would not show up here.
- **That the 7 undocumented water routes are intentional** rather than left over from an experiment.
