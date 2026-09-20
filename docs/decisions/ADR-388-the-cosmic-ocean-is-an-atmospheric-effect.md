# ADR-388: The Cosmic Ocean is an atmospheric effect, and the sky can afford to be procedural

- Status: Proposed (2026-09-19) — design only; no code written yet, pending sequencing against the
  agent that currently owns `src/world/atmospherics.*`.
- Extends ADR-230 (atmospheric effects), ADR-387 (the vortex became one), ADR-207 (world effects),
  ADR-011 (parameters), ADR-382 (a path a panel computes needs a test that computes it the same way),
  ADR-350 (reachability), ADR-170/182 (measurement).
- Supersedes, for the Tree of Life, the four `cosmos-*` procedural nodes and the
  `glowmere-cosmos.wgsl` background shader layer (ADR-338 / ADR-339 / ADR-360).

This is the technical design the brief's §1 and §37 ask for. It answers the twelve questions §37
lists, in its order, and then states the migration and the sequencing.

---

## 0. The three measurements this design rests on

Taken today on this machine, worktree `agent/ocean` at `1896513d`, the shipped project
`examples/treeisland/tree-of-life-floating-island.json`, realtime tier, serialised through
`tools/gpu-lock.sh`. The machine was shared with other agents at the time, so **minima** are the
comparable numbers (ADR-170) and the medians are inflated; the minimum GPU frame reproduces the
14.2 ms the brief quotes, which is the check that these runs are the same runs.

**M1 — the sky is 79% of the frame.** A depth AOV at t = 6.00, 1920×1080:

| depth | share of frame |
| --- | --- |
| < 500 m (island, tree, motes) | 20.88% |
| 500 m .. 2 km (the three star shells) | **0.05%** |
| far plane (nothing drawn) | **79.07%** |

1,639,665 of 2,073,600 pixels are sky. Anything drawn in a direction-space pass at the far plane
runs on 1.64 Mpx and on nothing else.

The second row is the finding nobody was looking for: **the existing starfield covers about a
thousand pixels.** ~4,500 instanced point quads at 480–1580 m, most of them sub-pixel at this
field of view. Whatever the Tree of Life's cosmos looks like today, the stars are not most of it.

**M2 — the frame, by pass.** Medians over 248 measured frames at 1920×1080; `min` for the frame
totals:

```
wall  min 19.07  p50 27.46        gpu min 14.22  p50 21.43        cpu frame p50 27.18
scene            7.012     volume.march     5.177     post/bloom   1.966
particles        1.114     shadow           0.524     post/dof     0.524
shadowmask       0.393     ao               0.328     depth        0.197
volume.composite 0.131     background       0.000     tonemap      0.066
```

**M3 — a full-resolution procedural sky is far cheaper than it feels, and the instrument proves
it.** `background` — the pass that draws `glowmere-cosmos.wgsl`, six five-octave value-noise fBMs,
about 120 sin-hashes per pixel, over the whole frame — reports **0.000 ms** at 1080p. That is the
number a design would be wrong to believe: the frame timeline's quantum is 65.536 µs and a
250-million-hash pass cannot be free.

So the instrument was tested rather than trusted. The same run at 3840×2160:

| pass | 1920×1080 | 3840×2160 | ratio |
| --- | --- | --- | --- |
| `background` | 0.000 | **0.459** | — (the 1080p median quantised to zero) |
| `volume.march` | 5.177 | 16.056 | 3.10× |
| `scene` | 7.012 | 15.663 | 2.23× |
| gpu frame (min) | 14.22 | 38.86 | 2.73× |

The pass is timed, it does scale, and the 1080p median was a quantisation artefact rather than a
free pass. Reading back linearly — right for a fullscreen ALU-bound draw — **glowmere-cosmos costs
of the order of 0.11–0.15 ms at 1080p.** Cross-checked against the hardware: 2.07 Mpx × 120 hashes
in 0.115 ms is ~2.2 Tera-hash/s, which is the right order for this GPU. It is consistent, so it is
evidence.

That single number sets the whole budget, and it points the opposite way from intuition:

> **~0.02 ms per full-1080p-frame per five-octave fBM.** A Cosmic Ocean five times heavier than
> `glowmere-cosmos`, drawn only on the 79% that is sky, costs about **0.45 ms**.

`scene` scaling at 2.23× for 4× the pixels is the corroboration: the scene pass is dominated by the
3.16 M-triangle hero tree, which is geometry-bound, so fragment work added to it is added to the
cheap axis. The expensive thing in this frame is `volume.march` — 5.18 ms at *half* resolution —
and it is expensive because it is a 32-step march of three 3D fBMs per step, not because it is
procedural.

---

## 1. Existing AV Gen architecture discovered

- **Sky is drawn twice, in one pass.** Inside `scene-pass`: `skybox.wgsl` (`fs_sky`, opaque, all
  five MRT targets, depth `LessEqual`, no depth write) then `atmosphere.wgsl` (`fs_atmosphere`,
  **additive** into target 0 (HDR) and target 3 (emission), write mask `None` on 1/2/4, same depth
  state). Both are one fullscreen triangle at clip z = 1.
- The Tree of Life **does not draw the skybox at all**: its `env/sky/background` is false, so
  `skyBackgroundFor` returns `FlatColour` and that draw is skipped. Its background is the flat clear
  colour `[0.0045, 0.0062, 0.0185]` plus the `glowmere-cosmos` shader layer.
- **Procedural noise** exists twice: `shaders/noise.wgsl` (pcg3d, value noise, `fbm3`, `fbm3Vec`,
  curl, Worley — the WGSL transliteration of `core/noise.hpp`, tested against the CPU) and
  `atmosphere_fx.wgsl`'s private 2D hashes, which exist because not every module that includes it
  reaches `noise.wgsl`.
- **Ray marching** exists in `volume.wgsl` (fog + the vortex) and `sdf_raymarch.wgsl`. **Analytic
  ray intersection** exists in `atmosphere_fx.wgsl` (the aurora's cylinder shells).
- **Particles** are ADR-015's fixed pool with prefix-sum compaction and indirect draw. Changing pool
  capacity destroys and recreates it.
- **Billboards**: camera-facing point quads via `ProceduralGeometry` + `ProceduralRenderer` — which
  is exactly what the four `cosmos-*` nodes are.
- **Render-to-texture** exists (`gpu::RenderTarget`, post ping-pong, volume half-res, AO, shadow
  mask). **Cubemap rendering exists only for IBL** (`EnvironmentProcessor`, 256 px source cube,
  128 px prefiltered). There is no render-to-cube of the scene and no equirect capture.
- **Temporal accumulation** exists only in GTAO (ping-pong history, 8 frames, invalidated on seek).
  There is no TAA; antialiasing is FXAA.
- **Bloom** is selective, driven by the emission target, and the atmosphere pass already writes its
  own bloom weight there — which is the stated reason it is a separate pipeline from the skybox.
- **Depth-aware compositing** exists in `volume.wgsl`'s composite pass. **Atmospheric fog** is
  `fogParams`/`fogHeight` in the frame block plus the volumetric pass.
- **Camera**: `frame.viewProj / invViewProj / cameraPos`. `skybox.wgsl` deliberately cancels the
  camera position (sky at infinity); `atmosphere.wgsl` deliberately does not — it passes
  `frame.cameraPos.xyz` as the ray origin, which is what gives the comet real parallax. There is no
  separate editor camera; the viewport is `camera/*`.
- **Shader layers** (`glowmere-cosmos.wgsl`'s mechanism) are ISF-style, full resolution, opaque,
  and — the fact that decides this design — **get no camera data at all**. `StdUniforms` is 96 bytes
  of time, frame, pass, size, audio and beat. A shader layer cannot know where the camera is
  looking, so it cannot have parallax, cannot rotate with the camera, and its "halo behind the
  island" is pinned to screen coordinates.

## 2. Existing World Effect architecture

Two families, and the brief's §38 is satisfied by joining the second rather than by inventing a
third.

- `world::WorldEffect` (ADR-207), paths `worldfx/<name>/*`, resolved on the CPU into a fixed record
  and evaluated per fragment **in the surface shader**. Wrong for a sky: the surface shader only
  runs where there is a surface.
- `world::AtmosphericEffect` (ADR-230), paths `atmos/<name>/*`, evaluated against a **view ray**.
  ADR-387 established what this family is: a **table-driven universal authoring interface**. Two
  `constexpr` tables of `{leaf, ranges, get, set}` function-pointer pairs and one case in each of
  `floatFields` / `colorFields` / `boolFields` buy, with no bespoke code, a registered and
  modulatable parameter per field, a Parameters-panel row, a modulation target, a timeline key, a
  preset member, a save entry, apply and capture in both directions, and a place in the World
  Effects panel.

Concrete sizes, for scale: the vortex is 22 kind floats + 3 colours + 10 shared floats + 1 shared
colour + `enabled` = **37 parameters**; a comet and an aurora are 47 each. Nothing in the design
degrades at those sizes, and the engine already registers over three thousand parameters in
Glowmere Valley 2.

Three things the family does **not** give for free, all of which this design must write:

1. **`toJson`/`fromJson` are hand-written per kind** (`atmospherics.cpp:344-618`) and the JSON key
   set is a second, independently maintained list beside the field-table leaves. They already
   disagree — the JSON says `speedScale`/`curtainCount` where the tables say `speed`/`curtains`.
2. **`defaultAtmosphericRoutes` has no vortex branch**: a vortex falls into the comet `else` and is
   handed three routes aimed at `coreIntensity`, `tailIntensity` and `sparkleIntensity`, none of
   which a vortex registers. The test loops only over `{Aurora, Comet}`, so it does not see this,
   and nothing in `src/` calls the function anyway. A fourth kind must add its own branch and the
   test must loop over every enumerator.
3. **`AtmosphericFrame::any()` is `cometCount > 0 || auroraCount > 0`** — it does not mention
   `hasVortex`. Harmless today because the vortex is drawn by the volumetric pass, and *not*
   harmless for a kind drawn by the atmosphere pass.

## 3. Rendering approach selected

**One additional term inside the existing atmospheric sky-layer draw.** No new pass, no new
pipeline, no new draw call, no new render target, no entities, no instances, no textures.

- `shaders/cosmic_ocean.wgsl`, a new file, exposes `cosmicOceanAt(ro, rd, pixelAngle) -> AtmosResult`.
- `shaders/atmosphere_fx.wgsl`'s `atmosphereSkyAt` gains one call and one add, beside the comet's
  and the aurora's.
- The parameters ride in the frame block as a `CosmicOceanGpu` appended at the end of
  `FrameUniforms`, which is the pattern that block has used four times already.

Everything is a pure function of `(ro, rd, pixelAngle, t, seed)`. `t` is the transport second and
nothing reads a frame counter or a wall clock, so an offline render of second N is identical to a
realtime playthrough of second N (ADR-091) and §34's determinism requirement is satisfied by
construction rather than by a reset.

### Depth strata and parallax — the one mechanism that does the work of §10, §11, §25 and §26

Every layer has a depth `D` in metres and a parallax scalar `p` in 0..1. Given a cosmic origin `O`
(the effect's authored `center`, defaulting to the world origin):

```
E = O + (ro - O) * p              // the effective eye for this layer
P = E + rd * t,  |P - O| = D      // the ray's hit on this layer's shell (one quadratic)
sample the layer's field at normalize(P - O), or at (P - O)/D for a 3D field
```

`p = 0` collapses `E` to `O`: the layer is sampled by direction alone, so camera rotation reveals it
and camera translation does nothing. `p = 1` gives true geometric parallax for something `D` metres
away. The brief's conceptual defaults (nebula 0.02, distant stars 0.05, planets 0.18, dust 0.45,
near particles 0.80) are literally this number, per layer.

This is why the effect must live in the atmosphere pass and not in a shader layer: the mechanism
needs `ro`, and a shader layer has no `ro`.

It is also the answer to §26's floating-point concern. Nothing is sampled at a world coordinate that
grows without bound; every field is indexed by a unit direction plus a bounded offset, so the
effect is stable at the floating-island scale and has no seam to wrap.

### The layers, and how each is drawn

| # | layer | technique | ~hashes/px |
| --- | --- | --- | --- |
| 0 | deep space | direction gradient + zenith/horizon/accent mix, atmospheric tint | 0 |
| 1 | galaxies | sparse cube-cell hash, 3×3 neighbourhood; oriented elliptical/spiral profile in the tangent plane | ~36 |
| 2 | far nebula | domain-warped fBM on the shell direction, 3–5 octaves, two-colour emissive density | ~120 |
| 3 | mid nebula | ditto, different scale/flow/colour, higher parallax | ~120 |
| 4 | ultra-distant stars | cube-cell hash, dense grid, 1 cell | ~4 |
| 5 | far stars | cube-cell hash, 3×3 | ~36 |
| 6 | mid stars | ditto | ~36 |
| 7 | planets | cube-cell hash, 3×3; **analytic ray–sphere**, then shade | ~36 + ALU |
| 8 | near stars | cube-cell hash, 3×3, twinkle | ~36 |
| 9 | cosmic dust | 3D cell lattice about the camera, 3×3×3 | ~108 |
| — | events | `floor(t/period)` hashed; no state | ~8 |

Cube-face cell hashing rather than the spherical `dir.xz/(dir.y+1)` the skybox uses for its stars:
that projection has a pole singularity and stretches cells badly away from the zenith, and a sky
whose star density visibly changes with elevation is one of the failure modes on §0's list.

**Planets are analytically intersected, not billboarded** (§6). A cell hash yields a direction, a
radius, a colour, a rotation phase, a ring configuration and a drift; the view ray is intersected
with that sphere at depth `D`; the hit gives a real surface normal, and from the normal come the
terminator, the dark side, the atmospheric rim, procedural cloud bands (2 octaves in the planet's
local frame, rotating with its phase) and the ring (one ray–plane intersection, an annulus test,
occlusion against the sphere, and the sphere's shadow on the ring). The illumination direction is
`frame.skySun.xyz`, the scene's own key light, so the planets are lit by the same thing the island
is. This is the difference between "a distant celestial object" and "a primitive sphere", and it
costs one quadratic plus shading on the small minority of pixels that hit a planet at all.

**Anti-aliasing is angular.** `pixelAngle` already arrives in `atmosphereSkyAt` — taken once, in
uniform control flow, at the top of the fragment shader, for reasons `atmosphere_fx.wgsl` documents.
A star or a mote smaller than a pixel is faded by its solid-angle ratio rather than point-sampled,
which is the rule the comet's shed fragments already use and the only thing that stops a dense star
field from being a shimmering mess. This is a second reason to share the pass: the number is already
there.

**Shimmer is phase-offset, never synchronous** (§8). Each star's twinkle phase is a hash of its
cell, so the field glistens rather than blinks; the brief names this distinction and it is a
one-line consequence of hashing the phase.

**Events are stateless** (§18). `i = floor(t / period)`; `hash(i ⊕ seed)` decides whether event `i`
fires and supplies its bearing, speed, size, colour and lifetime; the local time is
`t - i * period`. No accumulator, no RNG state, so an offline render and a live playthrough agree
and a seek cannot desynchronise it.

**Depth blur (§12) is octave truncation, not a blur pass.** A layer's `softness` drops its top
octaves and widens its lowest, which makes distant structure genuinely soft for *less* cost rather
than more. Nothing here duplicates the post chain; §12's "integrate intelligently with AV Gen's
existing post pipeline rather than duplicating an entire post-processing system" is satisfied by
writing a per-layer bloom weight into target 3 and letting the existing selective bloom do the rest.

**Composition mask (§33) is in world directions, not screen space.** A subject direction, an inner
and outer angle and a suppression amount: one dot product. This is a strict improvement on
`glowmere-cosmos`'s `haloCenter [0.5, 0.47]`, which is pinned to the frame and slides off the tree
the moment the camera moves — because a shader layer cannot know where the tree is.

## 4. Why this approach

1. **§38 says do not build a second modulation architecture, and ADR-387 already built the first
   one.** `AtmosphereKind::CosmicOcean` inherits registration, modulation, timeline, presets, save,
   capture, panel presence and serialisation from two `constexpr` tables. The alternative is a
   parallel system for the same job.
2. **The measurement says full resolution is affordable.** M3 puts a sky five times heavier than
   `glowmere-cosmos` at ~0.45 ms. A low-resolution nebula buffer plus an upsample — which is what I
   expected to design before measuring, and which is the house pattern from ADR-139 — buys back
   perhaps 0.3 ms in exchange for a render target, a pass, a bind group, an upsample filter and a
   resolution-dependent look. It is complexity bought against a cost that was measured not to exist.
   If the built shader misses the budget, the target is the fallback and not the starting point.
3. **The pass is already exactly right.** Depth-tested so the island occludes the cosmos with no
   horizon to author; additive into HDR so it is radiance rather than a picture; writing its own
   bloom weight so it can glow without the environment's `skyBloom` gate; skipped entirely when
   nothing is live so an unused feature is not even a uniform branch; and drawn whether or not the
   skybox is, which matters because the Tree of Life's skybox draw is skipped.
4. **It has the camera.** §24/§25 are free: the pass reads the *active render camera* out of the
   frame block, which is the same block the director, the shot cameras, multicam, the output frame
   and the offline renderer all fill. There is nothing to tie to an editor camera, because there is
   no editor camera.
5. **Nothing is per-object.** §23's prohibition is satisfied by there being no objects: no entities,
   no instances, no particle pool, no capacity to change.
6. **It is cheaper than what it replaces.** `glowmere-cosmos` runs on 100% of the frame in the
   background pass and is then overwritten on the 21% the island covers. The atmosphere pass runs
   on the 79% that is sky.

## 5. Alternatives considered

| alternative | why not |
| --- | --- |
| **A second shader layer** like `glowmere-cosmos` | No camera data at all (96-byte `StdUniforms`). No parallax, no rotation, no depth test, no world-space mask, and it draws behind the island only to be overwritten. This is precisely the ceiling the current cosmos has hit. |
| **A low-resolution nebula buffer + depth-aware upsample** (the ADR-139 pattern) | Measured unnecessary — see §4.2. Kept as the documented fallback if the built shader misses budget; it is one target and one sampler away, and the shader is written so the nebula terms can be split out. |
| **Render the cosmos to a cubemap or equirect and sample it** | Kills the thing the effect is for. Per-pixel planet parallax and terminators cannot survive resampling; ADR-049 already records that a star does not survive being resampled to a 128 px cube face, which is why the visible sky does not share the IBL's cube. The environment evolves continuously, so the capture would have to be re-run, spikily. |
| **Extend `volume.wgsl`'s march to carry the nebula** | The march is depth-bounded and already the most expensive thing in the frame at 5.18 ms half-res. A nebula at 10⁵ m would need an enormous ray. It is also the file another agent owns, and it is where the per-metre conversion has been got wrong three times (ADR-374/379/381). **Nothing in this design marches**, so this design is not the fourth candidate — but the rule still binds the one place it could: a nebula's `density` and `emission` are *per unit of normalised shell thickness*, not per metre, and they will be named and commented as such so nobody reads them as ADR-374 quantities. |
| **GPU particles / instanced billboards for stars and planets** | Forbidden by §23 and by §0's "no obvious billboard sprites". ADR-015's pool capacity cannot change without destroying and recreating the pool, so "planet density" could not be a modulatable parameter. Sorting and overdraw for tens of thousands of transparent quads is worse than the analytic solve. And M1 shows what this approach already produced: ~4,500 instances covering ~1,000 pixels. |
| **A `CosmicOceanRenderer` class with its own pass** (the `VolumeRenderer` shape) | Would earn its own `timeline_->mark` and a separate line in the bench record, which §36 wants. Costs an extra load/store of two RGBA16F attachments on a TBDR GPU — real bandwidth, plausibly ~0.2 ms, to measure a ~0.45 ms effect. Rejected in favour of a dedicated `--ab` / `--disable` arm; see §12. |
| **A singleton on `scene::Environment`**, as the vortex used to be | Exactly what ADR-387 undid, for exactly the reasons that apply here: no parameter paths, no panel, no serialisation, no modulation until somebody writes all four. |
| **A new first-class "Cosmic Ocean" panel** | §28 forbids it and ADR-387 established the rule: first-class UI represents reusable engine concepts. |

**The external references were not reachable from this environment**, so nothing above cites them.
The concepts used — separating atmosphere, clouds, fog and celestial rendering into complementary
systems; a density field with multiple layers and artist-facing controls over scattering,
turbulence, flow and coverage rather than raw march parameters; scalable quality as sample and
resolution counts and not as removed controls — are taken from the brief's own descriptions of them
in §2, §4 and §23, and are in any case the shape this codebase already uses for fog and the vortex.

## 6. Expected GPU cost

Against M3's calibration of **~0.02 ms per full-frame five-octave fBM at 1080p**, and M1's 79% sky
coverage:

| component | hashes/px | est. ms @ 1080p |
| --- | --- | --- |
| deep space, tint, mask, colour engine | ~0 | 0.01 |
| far + mid nebula (2 × warped fBM, 4 octaves) | ~240 | 0.19 |
| four star strata | ~112 | 0.09 |
| cosmic dust (3×3×3 lattice) | ~108 | 0.08 |
| planets (cell hash + ray–sphere + shading) | ~36 + ALU | 0.10 |
| galaxies | ~36 | 0.04 |
| events | ~8 | 0.01 |
| **total, realtime tier** | | **≈ 0.5 ms** |

Budget: **2.4 ms of GPU headroom at 60 fps**, so this is about a fifth of it. The design target is
**≤ 0.8 ms at 1080p realtime**, and the acceptance gate is a measured A/B, not this table.

Set against what it replaces: `glowmere-cosmos` at ~0.12 ms plus four procedural nodes at ~4,500
instances, ~9,100 triangles, their share of the depth prepass, the opaque pass, the cull and LOD
chain, and `cosmos-motes`'s entry into the shadow-caster list. The net is a real increase of
perhaps 0.3–0.4 ms of GPU.

**CPU is the binding constraint and this design adds nothing to it.** Resolution is a handful of
float operations per frame in `buildAtmosphericFrame` — no per-star, per-planet or per-mote CPU work
exists, because there are no stars, planets or motes on the CPU. Deleting the four procedural nodes
*removes* CPU work: their instance generation, their cull, their LOD selection and their
shadow-caster test. The expected CPU change is negative.

**Memory**: one `CosmicOceanGpu` block in `FrameUniforms`, ~640 bytes, taking the block from 3,888
to ~4,528 against a 64 KiB limit. No textures, no buffers, no allocations.
**Draw calls**: unchanged — the atmosphere draw already exists.

## 7. Quality tiers

Mapped onto the existing `QualityTier { Preview, Realtime, High, Offline }` and
`QualitySettings`; the brief's four names are these four, and no new enum is introduced.

| | nebula octaves | star strata | planet cells | dust | events |
| --- | --- | --- | --- | --- | --- |
| Preview | 3 | 2 | 1×1 | off | on |
| Realtime | 4 | 4 | 3×3 | 3×3×3 | on |
| High | 5 | 4 | 3×3 | 3×3×3 | on |
| Offline | 6 | 4 | 5×5 | 5×5×5 | on |

Two new `QualitySettings` fields, `cosmicOctaveScale` and `cosmicSampleScale`, in the same style as
`volumeStepScale` — and, per that field's comment, **a tier may scale sample counts and may not
remove an artistic control.** Preview turns dust off as a *sample count of zero*, reached through
the same field an artist can reach; it does not hide a knob.

Deliberately absent: a `cosmicResolutionScale`. §6 measured that the resolution lever is not needed
here, and a lever whose only justification is symmetry with the volume pass is a lever that will be
tuned by somebody who has not measured either.

## 8. Data structures

```
src/world/cosmic_ocean.hpp / .cpp        NEW, and mine
  struct CosmicOceanLayer { float depth, parallax, density, brightness, softness, ... }
  struct CosmicOcean {                   the authored effect: ~95 floats, ~9 colours, 1 seed
      glm::vec3 center;                  the cosmic origin the strata are concentric about
      float seed;                        §34: change it and the arrangement changes
      Master / Depth / Nebula / Stars / Planets / Dust / Galaxies /
      Atmosphere / Colour / Motion / Events / Mask sub-structs
      Result<void> validate() const;
  }
  struct CosmicOceanGpu { glm::vec4 ... };  static_assert(sizeof == 640)   // ~40 vec4 lanes
  CosmicOceanGpu packCosmicOcean(const CosmicOcean&, float envelope, double seconds);
  constexpr FloatField kCosmicFloats[];  constexpr ColorField kCosmicColors[];
  constexpr BoolField  kCosmicBools[];
  CosmicOcean cosmicOceanPreset(std::string_view style);   // §14's five palettes
```

The field tables are **declared in my file** and merely *referenced* from the three switches in
`atmospheric_params.cpp`, so the shared file takes three one-line cases rather than 110 lines of
table. The `Effect` typedef the accessor macros need is `AtmosphericEffect`, so
`cosmic_ocean.hpp` is included by `atmospherics.hpp` and the tables live in the `.cpp`.

On `AtmosphericEffect`: one new member, `CosmicOcean cosmicOcean;`, beside `comet`, `aurora`,
`vortex` — the same "one struct with both payloads rather than a variant" decision, for the same
reason.

On `AtmosphericFrame`: `bool hasCosmicOcean; CosmicOceanGpu cosmicOcean;` — **one, not an array**,
the same deliberate asymmetry ADR-387 chose for the vortex and for a stronger reason: a second
cosmos is not a composition, it is a mistake. Extra instances are counted in
`AtmosphericCounts::dropped` so the UI can say so rather than silently ignoring them.

`AtmosphericFrame::any()` must gain `|| hasCosmicOcean`. This is the bug ADR-387 left behind: the
predicate gates the whole atmosphere draw, and a scene whose only atmospheric effect is a Cosmic
Ocean would render nothing at all. **This is the single highest-risk line in the change**, because
it fails silently and looks exactly like "this scene has no such effect".

~95 floats is 2.6× the vortex's 22 and comfortably inside what the tables demonstrably carry
(comet 30, aurora 31); the count comes from §28's panel outline, which is the contract.

## 9. Shader architecture

```
shaders/cosmic_ocean.wgsl           NEW, and mine. ~700 lines.
  cosmicShellHit(ro, rd, O, D, p) -> vec3      the parallax solve, §3
  cosmicCubeCell(dir, density)    -> cell id + local coords
  cosmicNebula / cosmicStars / cosmicPlanets / cosmicGalaxies / cosmicDust /
  cosmicEvents / cosmicAtmosphere / cosmicPalette
  cosmicOceanAt(ro, rd, pixelAngle) -> AtmosResult

shaders/atmosphere_fx.wgsl          +1 include, +2 lines in atmosphereSkyAt
shaders/common.wgsl                 + the CosmicOceanGpu mirror at the end of FrameUniforms
```

Its own hashes rather than `noise.wgsl`'s, for the reason `atmosphere_fx.wgsl` and
`world_effects.wgsl` both give: the modules that include this one do not all reach `noise.wgsl`, and
a helper that exists in three of four consumers is a compile error waiting for the fourth.

`AtmosResult` is the existing `{ radiance, bloom }` — the contract is unchanged, so the sum in
`atmosphereSkyAt` is one more term.

Not a monolith (§38) and not a false decomposition either: the layers are functions in one shader
file because they share the parallax solve, the palette and `pixelAngle`, and splitting them across
WGSL modules would mean including the same helpers four ways. The C++ *is* decomposed, along the
seam the codebase already uses: `cosmic_ocean.hpp` (data, validate, presets, pack) separate from
`atmospherics.*` (resolution, lifecycle) separate from `atmospheric_params.cpp` (registration).

## 10. UI architecture

World Effects ▸ Atmospheric ▸ a Cosmic Ocean instance. **No new panel** (§28).

The rows are **plain data** in the `ui::EffectRow` table format ADR-387 introduced, which already
carries a `section` field that starts a `SeparatorText` — so §28's twelve sections are twelve
non-empty `section` strings and need no new machinery:

```
ui::cosmicOceanRows()          Master, Colour, Nebula, Stars, Planets     (the ~35 rows anybody reaches for first)
ui::cosmicOceanAdvancedRows()  Depth & Layers, Cosmic Dust, Galaxies, Atmosphere, Motion, Events, Mask, Quality
```

Declared in a **new** `src/ui/cosmic_ocean_rows.hpp` rather than appended to `ui_logic.hpp`, purely
to stay out of a file another agent is editing; if the sequencing makes that moot they belong in
`ui_logic.hpp` beside `vortexRows()`.

`world_effects_panel.cpp` takes: one `isCosmicOcean` bool, one `else if` calling `drawEffectRows`,
one entry in the "Add" menu, one style-list case, one beat-target leaf, and — like the vortex — **no
ground-glow combo**, because a cosmic background does not light the island and a combo that changed
nothing would be worse than no combo.

One reachability note found while auditing, not caused by this change: `"atmos/"` appears in neither
`kBeginnerPrefixes` nor `kIntermediatePrefixes`, so every atmospheric parameter is invisible in the
Parameters panel below the Advanced authoring layer. The World Effects panel is the intended surface
and is unaffected, but a flagship artist-facing effect whose parameters vanish on two of three
layers is worth a decision rather than an accident.

## 11. Modulation architecture

No hardcoded audio logic anywhere (§27). Every field is a registered parameter at
`atmos/<name>/<leaf>`, so every one of §27's targets is a `ModRoute` with a source, an amount, an
op and an attack/decay chain, editable in the Modulation panel.

`defaultAtmosphericRoutes` gains a real `CosmicOcean` branch — and, more to the point, stops being
an `if aurora else comet`, because the *reason* the vortex silently inherits the comet's three
dead routes is that the function has no fourth arm to fall out of. The test must loop over every
`AtmosphereKind` and assert that every default route's target is a path that kind actually
registers. That assertion would have caught the vortex's.

Proposed defaults, sparse on purpose (§32: a good Cosmic Ocean is not every effect maximised):
`audio.rms → intensity`, `audio.bass → nebulaDensity`, `audio.mid → flowTurbulence`,
`audio.treble → starTwinkle`, `beat.pulse → eventProbability`.

The beat-response slider's leaf for this kind is `intensity`.

## 12. Testing strategy

**CPU suite (`avgen_tests`) — no GPU, no lock:**

- `validate()` rejects every out-of-range field; round-trip `toJson`/`fromJson` is the identity on a
  fully-populated effect **and on a default-constructed one**, which is what catches a leaf added to
  the struct and forgotten in the serialiser.
- **The table and the serialiser agree.** A test that every `kCosmicFloats`/`kCosmicColors` leaf
  appears as a key in `toJson`'s output, and vice versa. The comet's `speedScale`/`speed` and the
  aurora's `curtainCount`/`curtains` are the existing evidence that two hand-maintained lists drift;
  this is the assertion that stops the third instance.
- **ADR-382, the panel's paths.** `cosmicOceanRows()` and `cosmicOceanAdvancedRows()` are walked by
  the test exactly as the panel walks them, and every leaf must resolve to a registered, modulatable,
  serialized parameter. A path a panel computes needs a test that computes it the same way; asserting
  what registration produced proves the parameters exist and says nothing about whether the UI can
  reach them.
- **Every default route binds.** Loop over all four `AtmosphereKind`s.
- **`any()` is true for a Cosmic Ocean.** One line, and it is the line that would otherwise draw an
  empty sky and look like a shader bug.
- Determinism: `packCosmicOcean` at the same transport second is bit-identical across calls; two
  seeds give different packed phases; one seed gives the same.
- Migration: the Tree of Life scene and **project** both load, both carry the effect, and — the
  ADR-387 lesson — every route, timeline key, preset member and cue that named a migrated path names
  the new one. A parameter path is three things at once and a correct value is not a reached value.

**GPU suite (`avgen_render_tests`), serialised through `tools/gpu-lock.sh`, minima over repeats:**

- **Render and look.** A black frame and a nearly-black frame are identical in a hash, and this
  effect's entire failure mode is being too dim to see. Frames go in
  `examples/treeisland/renders/` for the owner's eye, and the assertions are on `byteDiff` and on
  statistics — never `REQUIRE(a == b)` over a multi-megabyte buffer, which kills Catch2 inside the
  assertion handler and prints `FAILED:` with no `with expansion:` (ADR-362).
- **A probe that can fail** (ADR-182). Switching the whole effect off is the arm that is worth
  having *here*, unlike the volume case, because this is one additive term in one fullscreen draw
  that touches nothing else — so a new `toggles_.cosmicOcean` beside `toggles_.atmospherics`, wired
  to `--disable cosmic` and `--ab cosmic`, removing the fragment work and the uniform content
  together the way `drawAtmosphere_` already does. That arm *is* §39's acceptance test — "then
  disable Cosmic Ocean; the scene should suddenly feel dramatically emptier" — measured as pixel
  difference and looked at as an image.
- Camera stability (§24/§25): three frames from the viewport camera, a director shot and a multicam
  shot at the same transport second must agree where the cameras agree; translating the camera must
  move near layers more than far ones, asserted as a measured parallax ratio rather than by eye.
  **The baked `cameraAimFollow` (37 entries) and `cameraShotSpans` (42) must not be touched** — a
  viewport drag on a directed project destroys them, so these tests load and never save.
- Performance: a bench-json A/B at 1080p realtime with the budget asserted at ≤ 0.8 ms, plus the
  four tier arms.
- Offline parity: an offline render of second N against a realtime frame at second N.

**Before any commit touching `examples/`: `python3 tools/check_project_integrity.py`.**
**After any merge: `cmake -S . -B build/release` before building** — the test glob is
configure-time, so an incremental build silently omits test files a merge added.

---

## Migration — the Tree of Life must not get worse (§29, §31)

What exists, and what replaces it:

| today | under Cosmic Ocean |
| --- | --- |
| `cosmos-stars-near/mid/far` (~3,880 instances, 480–1580 m) | star strata 5–8 |
| `cosmos-motes` (~660 instances, 175–540 m) | the cosmic dust layer |
| `glowmere-cosmos.wgsl` background layer, 13 ISF inputs | nebula + deep space + halo (now world-anchored) |
| flat clear colour `[0.0045, 0.0062, 0.0185]` | unchanged; the effect adds to it |

**Four consequences that must be handled, not discovered:**

1. **Deleting the star shells collapses the scene radius from ~2,737 m to the island's ~100 m**,
   and `camera.farPlane = max(radius * 50, 2000)` collapses with it — from ~135 km to the 2 km
   floor. It also narrows every node's `position` slider, whose soft range is `10 × radius`.
   `tests/unit/test_cosmic_key.cpp:144,160` hard-codes 2737. The Cosmic Ocean is drawn at the far
   plane and does not itself need the radius, but the scene's framing was tuned with it. **The
   staging must be: add the effect, tune it, render an A/B against the shipped frames, and only then
   remove the nodes** — with the radius change made deliberately and looked at, not inherited.
2. **`environment.dayNight.starNodes` names all four nodes** in
   `tree-of-life-ocean-world.scene.json` and nine scratch arms. Removing the nodes removes the
   day/night cycle's star channel. The replacement is a route or a timeline key onto
   `atmos/<name>/starBrightness`, which is strictly better than the node list — but it is a thing to
   write, not a thing that happens.
3. **`tools/make_treeisland_arms.py:141` builds the `_ctl-no-stars` control by dropping the four
   names.** With the nodes gone it silently produces an arm identical to the deliverable — a control
   that cannot fail. It must be repointed at the effect's enable.
4. **Ten `examples/assetlod/tree-*.scene.json` LOD fixtures carry byte-for-byte copies** of the four
   nodes and of the shader layer, and ~35 `_`-prefixed treeisland arms do too. They are fixtures for
   a different measurement and should be left alone rather than swept.

**One audit finding I could not confirm and am not relying on.** The four nodes' authored
`emissiveBoost` — which the brief flags as owner-tuned, and which two presets set to 1.15–2.2 —
appears to be **inert for these nodes**. `emissiveBoost` is applied at `composition.cpp:6411` in a
loop over `scene_.entities`, and a procedural node produces no entities (it pushes into
`scene_.procedurals`). What actually carries their emission is
`procedural/cosmos-*/material/emissive` (0.55 / 1.3 / 2.0 / 3.4) and `materialVariation.emissiveRandom`.
If that reading is right, ADR-343's star-dimming path is also inert for them and only its
visibility override reaches them. **This is a code read, not a measurement**, and ADR-385's lesson is
that a stated reason is not evidence — including this one. It is a ten-line check and it must be run
before anything in the migration leans on it either way.

---

## Sequencing: the shared files, and why each is needed

Another agent owns `src/world/atmospherics.{hpp,cpp}`, `src/world/atmospheric_params.cpp`,
`src/ui/world_effects_panel.*`, `shaders/volume.wgsl` and `src/core/vortex.*`.

**This design needs none of `shaders/volume.wgsl` or `src/core/vortex.*`.** Nothing here marches and
nothing here is a vortex. The four it does need take small, additive, per-kind edits:

| file | edit | ~lines |
| --- | --- | --- |
| `src/world/atmospherics.hpp` | `CosmicOcean` enumerator; include; member on `AtmosphericEffect`; `hasCosmicOcean` + `CosmicOceanGpu` on `AtmosphericFrame`; **`any()` gains `\|\| hasCosmicOcean`**; style-list and factory declarations | ~14 |
| `src/world/atmospherics.cpp` | kind name / `fromName`; `validate` arm; `toJson` / `fromJson` arm; style list + `applyCosmicOceanStyle`; factory; resolve + pack in `buildAtmosphericFrame` | ~130, all inside per-kind switches |
| `src/world/atmospheric_params.cpp` | three one-line cases in `floatFields` / `colorFields` / `boolFields`; a real `CosmicOcean` branch in `defaultAtmosphericRoutes` | ~12 |
| `src/ui/world_effects_panel.cpp` | `isCosmicOcean`; one `drawEffectRows` arm in each of `drawAtmospheric` / `drawAtmosphericAdvanced`; Add-menu entry; style case; beat-target leaf | ~30 |

Files this change owns outright: `src/world/cosmic_ocean.{hpp,cpp}`, `shaders/cosmic_ocean.wgsl`,
`src/ui/cosmic_ocean_rows.hpp`, `src/rendering/scene_renderer.{hpp,cpp}` (uniform lanes, fill, the
new toggle), `shaders/atmosphere_fx.wgsl`, `shaders/common.wgsl`, `src/app/application.cpp` (the
`--disable`/`--ab` arm), `src/rendering/render_quality.hpp` (two tier fields),
`tests/unit/test_cosmic_ocean.cpp`, `tests/rendering/test_cosmic_ocean_gpu.cpp`, and the Tree of Life
scene and project.

Three of the four shared edits are inside `switch (kind)` statements that the compiler will flag:
`floatFields`, `colorFields` and `boolFields` are exhaustive with no `default:` and warnings are
errors, so **adding the enumerator makes the build fail at exactly the three places that must
change**. That is the mechanism that keeps two agents' edits from silently missing each other, and
it is worth saying out loud because it means the merge conflict, if there is one, will be a compile
error rather than a wrong picture.
