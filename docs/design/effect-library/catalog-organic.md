# Catalog: Organic

This file is part of the [Effect Library](README.md). The master table is in [catalog.md](catalog.md),
and the tags are defined in [shared-infrastructure.md](shared-infrastructure.md).

**Merged with the Particle family.** Floating Spores is the same thing as **Spores**, and Falling
Leaves is the same thing as **Leaves**. Each has one entry, in
[catalog-particles.md](catalog-particles.md). Pollen is kept here, because the spec lists it only
under Organic, but it is also a particle preset.

**What already exists for this family.** Most of the Glowmere scene's organic look is already built.
The effects below *expose and generalise* those systems rather than re-implementing them:

- **Mesh wind.** ADR-360 (`meshWindOffset`, `shaders/common.wgsl:268`) provides layered trunk,
  branch and foliage bending [R11]. Procedural vegetation sway is at `shaders/procedural.wgsl:446`
  and `wind_field.wgsl:107-147`.
- **Tree energy pulse.** ADR-376 (`treeEnergyAt`, `common.wgsl:408-465`) is an emissive band that
  travels up a body. Its parameters are `nodes/<n>/energy/*` (`src/scene/composition.cpp:4449-4465`).
- **Bark veins.** These are a material program (`src/scene/tree_veins.hpp`).
- **Bioluminescence ladder.** `recipe.lighting.bioluminescence` (`src/world/world_recipe.hpp:69`,
  `world_composer.cpp:813-835`) sets it. Ecology glow clusters are turned into up to
  `kMaxEcologyLights = 224` point lights (`src/world/ecology.hpp:240-261`, `composition.cpp:7671-7735`).
- **Tubes.** Swept tubes come from the procedural `Tube` primitive (ADR-043, `src/scene/procedural.hpp:135-146`).
  Procedural deformers cover bend, twist, sine, noise, displacement, field and path, in 8 slots
  (`procedural.wgsl:68-93`).
- **SDFs.** The SDF tree lives in `src/spatial/sdf.hpp`. It is raymarched or meshed, and no example
  scene uses it yet.

**What is missing.**

- **Growth or reveal.** There is no growth parameter anywhere. `src/world/hero.hpp:127-132` lists "a
  one-shot reveal" as unsupported.
- **General per-entity vertex displacement.** A material program has no vertex stage, and the only
  vertex hooks are wind, skinning and procedural deformers.
- **An alpha-clip path in the depth prepass and shadow pass that follows an effect.** `fs_depth`
  reads only `object.baseColor.a` (`shaders/pbr.wgsl:63-95`).

FXL (object effect lanes) closes all three gaps, because depth and shadow passes read object uniforms
and do not read material programs.

---

## Bioluminescence  (`bioluminescence`)

**2.1 Definition.** A living light pattern on an organism. It has spots, stripes or cells that glow,
pulse slowly and propagate waves, and it may flare when something comes near (touch or proximity
response). The effect is scoped to one entity. **World-scale bioluminescence** is the existing
recipe ladder, which is a World preset of this effect that writes that ladder.

**2.2 References.**

- Real bioluminescent displays: comb-jelly iridescent rows, firefly squid photophores and
  dinoflagellate flashes when disturbed.
- Cellular patterns from Worley noise.
- ADR-376's travelling band.
- The engine's own emission-tier ladder (Phase A of the bioluminescence overhaul).

**2.3 Visual anatomy.**

- Dark body.
- Scattered photophores or cell edges in cyan, green or violet.
- Slow asynchronous breathing of brightness.
- An occasional wave rolling along the body.
- A bright flare that spreads from the nearest point when an entity approaches, then relaxes.
- A soft light spilling onto the ground.

**2.4 Implementation.**

- **Recommended:** FXL lanes.
  - `fxBio0` holds pattern (Spots / Stripes / Cells), scale, coverage and colour.
  - `fxBio1` holds pulse rate, wave speed, and flare position (object space) with its age.
  - The fragment computes `mask(pattern) × (base + breathe(t, cellHash) + wave + flare(dist, age))`
    into emission.
  - The flare comes from SIGNALS/TRIGGER. The CPU finds the nearest revealer entity within the
    radius, and a `field.<name>.enter` event or a proximity crossing sets `t0`.
- **Spill.** Spill **joins the ecology light aggregation**, `aggregateGlow` → `GlowCluster` → pool
  lights, rather than allocating its own lights. The ecology system already owns the light budget,
  and a second allocator would fight it.
- **Alternative:** a per-material program (Voronoi + Time). That is shared across all users of the
  material and cannot flare per entity.
- **Needs:** FXL, the Worley F2 addition, LIGHTMOD via ecology aggregation, and SIGNALS.

**2.5 Targets.**

- **Entity:** the primary target.
- **World:** preset only, which sets the recipe ladder.
- **Material:** already possible via a program.
- **Light:** not applicable.

**2.6 Parameters.**

| Parameter | Notes |
|---|---|
| `pattern` | Spots / Stripes / Cells |
| `scale` | |
| `coverage` | |
| `color` | |
| `colorVariation` | |
| `intensity` | |
| `breatheRate` | |
| `breatheDepth` | |
| `waveSpeed` | |
| `waveInterval` | |
| `flareIntensity` | |
| `flareRadius` | |
| `flareDecay` | |
| `proximityRadius` | |
| `spill` | |

**2.7 Modulation.**

- `audio.mid → intensity`
- `beat → wave` as a trigger
- `owner.speed → breatheRate`
- `field.<n>.enter` as the flare trigger
- `owner.cameraDistance`, which fades detail with distance

**2.8 Animation model.** The breathe and wave components are stateless. The flare is a function of
`t − t0`, where the TRIGGER source is proximity crossings. Proximity crossings are pure only if the
revealer's path is pure. For autonomous bodies the path is checkpointed state and is replayed exactly
under ADR-700, so the crossing time is exact after a seek. The proximity check therefore runs on the
fixed step grid inside the entity step, not in the render frame.

**2.9 Compositing.** The emission is part of the lit surface, so it is fogged, depth-exact and
bloomed. Spill lights the surroundings through clustered lights.

**2.10 Stack behaviour.** `Material` (surface) plus `Lighting` (spill). It sums with Glow.

**2.11 Performance class.** Low.

**Presets.** Glowmere Creature, Deep-Sea Photophores, Jelly Rows (Stripes), Touch Flare (a
proximity-only variant).

---

## Pulsing Veins  (`pulsingVeins`)

**2.1 Definition.** Light travels along a network of veins, either on the surface or along branch
topology. It is ADR-376's tree energy, generalised to any entity, with a real vein mask.

**2.2 References.** ADR-376 `treeEnergyAt` (a band along normalised height), `tree_veins.hpp` (vein
mask as a material program), and Worley F2−F1 edges as a procedural vein network.

**2.3 Visual anatomy.** Thin glowing channels, with brighter pulses flowing from a root or source
outward, repeating. Channels branch and thin toward the extremities.

**2.4 Implementation.**

- **Recommended.** Use Pulse in Travelling mode, masked by a vein pattern.
  - The mask is Worley F2−F1 edges in object space, with scale and width, or the mesh's own vertex
    colour or UV channel when authored.
  - The travel coordinate is height (tree energy's frame), distance from origin, or tube arc length.
    Procedural tubes are generated on the CPU, so `makeTube` can write arc length into UV.v
    (`procedural.hpp:199`).
  - This supersedes tree energy's special case. ADR-376's `energy0-3/A/B` lanes *are* this effect's
    lanes, and the `nodes/<n>/energy/*` parameters migrate to `fx/<id>/*` with no alias (ADR-441).
    Tree energy is only on the entity path, and it stays there.
- **Needs:** FXL, the Worley F2 addition, and the tree-energy lane migration.

**2.5 Targets.** Entity (primary). Material, where it is a program-driven mask.

**2.6 Parameters.** `pattern` (Voronoi / Authored), `scale`, `width`, `color`, `colorFar`,
`intensity`, `pulseSpeed`, `pulseWidth`, `pulseInterval`, `coordinate` (Height / Radial / ArcLength),
`noise`.

**2.7 Modulation.** `beat → pulse` (one pulse per beat), `audio.bass → intensity`,
`fx.<charge>.charge`.

**2.8 Animation model.** Stateless.

**2.9 Compositing.** Surface emission, as Glow.

**2.10 Stack behaviour.** `Material`.

**2.11 Performance class.** Low.

**Presets.** Tree of Life, Alien Veins, Circuit (hard edges, stepped).

---

## Growth  (`growth`)

**2.1 Definition.** The owner grows into existence. There are three readings:

- (a) **scale-in**, which is uniform, with overshoot;
- (b) **reveal front**, where a threshold rises through the mesh with a glowing growth edge;
- (c) **arc-length extrusion**, where tubes, vines and tendrils extend from root to tip.

All three are modes. (b) is the default for arbitrary meshes and (c) is the default for tubes.

**2.2 References.**

- Dissolve and reveal via threshold with an emissive edge band (the standard technique; the alpha
  test must also apply in depth and shadow passes).
- Arc-length vertex collapse for growing splines and vines, as in procedural-vine demos.
- L-systems (Prusinkiewicz & Lindenmayer, *The Algorithmic Beauty of Plants*) for true topological
  growth. That is out of scope here, because it is a geometry regeneration per frame.

**2.3 Visual anatomy.** A front moves from the root upward or outward. Behind it is solid geometry.
At it is a bright thin band, with vertices shrinking into the front. Ahead of it is nothing.

**2.4 Implementation.**

- **(b)** Use the FXL clip lane `fxClip`: `(mode, threshold, edgeWidth, edgeEmission)`.
  - The coordinate is height, distance from origin, or noise.
  - The fragment discards where `coord > threshold` and adds emission in the band.
  - The vertex stage optionally pulls vertices within `edgeWidth` toward the front, which gives
    softer growth.
  - The same discard runs in `fs_depth` (the prepass and shadows). It can, because it reads object
    uniforms. This fixes the gap that makes a program-driven dissolve leave its shadow behind.
- **(c)** Tubes carry arc length in UV.v. The vertex stage collapses vertices with `v > grown` onto
  the tip ring, and the fragment caps with emission.
- **(a)** XFORM scale.
- **Needs:** FXL (clip and vertex lanes), and `makeTube` writing arc length.

**2.5 Targets.** Entity (primary). World, where a scene-wide "world blooms in" drives many entities,
is better done as one route fanned to each entity's Growth.

**2.6 Parameters.** `mode`, `progress` (0–1, the keyable heart of it), `coordinate`, `edgeWidth`,
`edgeColor`, `edgeEmission`, `noiseBreakup`, `overshoot`, `duration` (for triggered use).

**2.7 Modulation.** Timeline `progress` (primary), TRIGGER plus `duration`, `audio.rms → progress`
(growth that follows the music's build), and `music.build`.

**2.8 Animation model.** `progress` is a parameter, so it is stateless. The triggered form is
`progress = ease((t − t0)/duration)`.

**2.9 Compositing.** Real geometry: correctly lit, shadowed (with the clip in the shadow pass) and
depth-exact. The edge emission blooms.

**2.10 Stack behaviour.** `Geometry` (clip and vertex) plus `Material` (edge emission). Growth and
Dissolve share the lane. Two clip effects on one owner combine as `max(coord)`, which is documented.

**2.11 Performance class.** Low. There is a discard in the depth prepass, which loses early-Z for
that draw only.

**Presets.** Sprout, Vine Creep (Tube), Crystal Grow (Noise with a hard edge), Bloom In (Scale).

---

## Sway  (`sway`)

*A preset of **Wind Response**
([catalog-motion.md#wind-response](catalog-motion.md#wind-response--windresponse)).* It is not a
separate type. Sway adds one thing, a **Procedural oscillator source** in place of the wind field. It
is for kelp underwater, idle tentacles, or plants in a windless scene: a sum of sines with seeded
phase per body, fed into the same ADR-360 bend lanes.

- **Targets:** Entity.
- **Presets:** Kelp, Idle Plant, Dreamy Sway.
- **Performance class:** Very Low.

---

## Breathing  (`breathing`)

**2.1 Definition.** A slow periodic swelling of the owner's volume, like a chest or a pod or a living
rock. It is either uniform or localised to a region.

**2.2 References.** Vertex inflation along the normal (the same operation as inflated-hull shells),
masked by a region. Volume preservation is optional.

**2.3 Visual anatomy.** A subtle, slow in-and-out swell, strongest in a band (the chest or the
middle), with an asymmetric inhale and exhale.

**2.4 Implementation.**

- **Recommended.** Use the FXL displacement lane `fxDisplace`: mode Inflate, amplitude, rate, and a
  region (an axis plus a band centre and width).
  - `p' = p + n·A·breath(t)·region(p)`, where `breath` is an asymmetric waveform.
  - It is applied in `vs_main` for all passes, following the `meshWindOffset` precedent.
  - The previous-frame displacement comes from the previous time (`windTune.w` already carries
    "previous frame's time" for exactly this reason).
- **Alternative.** XFORM uniform scale, which is cheaper but moves the whole object.
- **Needs:** FXL vertex lanes.

**2.5 Targets.** Entity.

**2.6 Parameters.** `amplitude` (m), `rate`, `asymmetry`, `regionAxis`, `regionCenter`,
`regionWidth`, `seed`.

**2.7 Modulation.** `audio.rms → amplitude`, `beat.phase` (breathe on the beat), and `owner.speed`
(panting).

**2.8 Animation model.** Stateless.

**2.9 Compositing.** Real geometry, and shadows follow.

**2.10 Stack behaviour.** `Geometry`. Displacements sum.

**2.11 Performance class.** Very Low.

**Presets.** Sleeping Creature, Living Pod, Pulsing Rock.

---

## Pollen  (`pollen`)

*A particle preset, see EMIT in [catalog-particles.md](catalog-particles.md).* The
`examples/weather` scenes already have a `pollen` system.

- **Recipe:**
  - small warm-yellow round cards
  - low `gravity` (slightly buoyant)
  - `turbulence` curl noise
  - `windInfluence` about 0.8
  - `scatterStrength` for sun-lit sparkle, with forward scatter (HG) `scatterAnisotropy` of about 0.6
  - `sizeVariance`
- **Emitter:** a Box around a meadow (World), or the canopy source on a flowering entity
  (`canopySource`, `composition.hpp:321-325`).
- **Modulation:** `audio.treble → spawnRate`.
- **Performance class:** Low.

---

## Tendrils  (`tendrils`)

**2.1 Definition.** Several flexible tentacle or vine strands grow out of the owner, waving
organically and optionally reaching toward a target.

**2.2 References.**

- Spline skinning of tube geometry: bend a canonical straight tube along a curve evaluated in the
  vertex stage.
- Curl noise [R12] for coherent waving.
- An FK chain with noise angles, damped toward a target direction, for reach. This is the analytic
  equivalent of a cheap CCD pose.

**2.3 Visual anatomy.** N strands, thick at the root and tapering to fine tips. They wave with a
travelling wave from root to tip, curl at the tips, and optionally glow along their length (Pulsing
Veins).

**2.4 Implementation.**

- **Recommended (Wave 3+).** An instanced canonical tube.
  - The mesh is unit length with arc length in UV.v.
  - A per-instance storage record holds 8 control points, computed on the CPU each frame from an FK
    chain: `angle_k(t) = base_k + A·sin(ωt − φ·k) + curl(seed, t)`, plus a target blend.
  - The vertex stage evaluates a Catmull-Rom curve at `s = v`, builds a rotation-minimising frame
    approximation, and offsets by the tapered radius.
  - One draw serves all strands. Growth's arc-length mode composes with it.
- **Stopgap (reuse).** A procedural `Tube` plus a Path deformer on a spline that is re-uploaded per
  frame. Splines are limited to 16 slots scene-wide (`shaders/spline.wgsl:1-25`), so this is fine
  for a hero with at most 8 tendrils, but it does not scale.
- **Needs:** instanced tube rendering (a new path on the entity side; procedural instancing exists
  and could host it) and `noise.wgsl`.

**2.5 Targets.** Entity (primary). World (placed roots).

**2.6 Parameters.**

| Parameter | Notes |
|---|---|
| `count` | |
| `length` | |
| `rootRadius` | |
| `tipRadius` | |
| `waveAmplitude` | |
| `waveSpeed` | |
| `curl` | |
| `reachTarget` | endpoint |
| `reach` | 0–1 |
| `growth` | |
| `spread` | |
| `seed` | |
| `material` | a material ref, or Glow/Veins via stacking |

**2.7 Modulation.** `audio.bass → waveAmplitude`, `owner.speed → trail-back` (strands stream behind
a moving owner), `beat → reach`.

**2.8 Animation model.** Stateless (seeded functions of t). `owner.speed` streaming uses the measured
velocity, which is exact under replay.

**2.9 Compositing.** Real, lit, shadowed geometry with correct velocity. The previous control points
come from the same functions at `t − 1/60`, not from last frame's values, so the result is frame-rate
independent.

**2.10 Stack behaviour.** `Geometry`. It creates geometry, which is the first effect to do so on the
entity path.

**2.11 Performance class.** Medium. The cost is vertex count times strands, plus the shadow pass.

**Presets.** Kelp Crown, Alien Tentacles, Root Reach, Glowmere Vine.

---

## Organic Pulsation  (`organicPulsation`)

**2.1 Definition.** A peristaltic bulge that travels along the owner, like a swallowing vine or a
pulsing sac. It differs from Breathing: Breathing is a standing swell, and this effect is a
*travelling* one.

**2.2 References.** A travelling wave `band(axisCoord − speed·t)` applied as an inflation along the
normal. It is the same algebra as ADR-376's band, but on positions instead of emission.

**2.3 Visual anatomy.** One or more bulges roll from one end to the other, repeating. It can be
paired with a matching emissive band (Pulsing Veins using the same speed).

**2.4 Implementation.** Use the FXL `fxDisplace` lane in mode Travelling:
`p' = p + n·A·Σ_k band(coord(p) − speed·t − k·interval)`. The coordinate is height, axis, or arc
length. It shares the lane and code with Breathing.

**2.5 Targets.** Entity.

**2.6 Parameters.** `amplitude`, `bandWidth`, `speed`, `interval`, `axis`, `coordinate`, `direction`.

**2.7 Modulation.** `beat` (one bulge per beat, with the interval synced), `audio.bass → amplitude`.

**2.8 Animation model.** Stateless.

**2.9 Compositing.** As Breathing.

**2.10 Stack behaviour.** `Geometry`.

**2.11 Performance class.** Very Low.

**Presets.** Swallow, Heart Vessel, Pod Pulse.
