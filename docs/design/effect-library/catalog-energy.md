# Catalog: Energy

Part of the [Effect Library](README.md). The master table is in [catalog.md](catalog.md). Tags are
defined in [shared-infrastructure.md](shared-infrastructure.md).

*Shockwave is listed under Energy in the spec. It is the Distortion type
([catalog-distortion.md#shockwave](catalog-distortion.md#shockwave--shockwave)), and its "Energy
Blast" preset is the Energy reading.*

**The family's shared infrastructure.**

- **BOLT** is a seeded CPU generator of branching fractal polylines. It serves Lightning, Arc,
  Electric Field, Discharge and Reality Tear's crack outline.
- **RIBBON** draws camera-facing HDR strips with a hot white *core* and a wide coloured *glow*
  profile.
- **SHELL** draws analytic proxies. It serves Shield, Force Field and Plasma.
- **LIGHTMOD** provides real flash lights.
- **TRIGGER** schedules strikes deterministically.

Every energy effect writes HDR values well above 1, both to colour and to the emission target, so it
reaches selective bloom. The engine's HDR and emission targets are RGBA16F with no clamp on emission
(`scene_renderer.hpp:732-739`).

The HDR convention is:

| Element | Value |
|---|---|
| Core | 20–200× |
| Glow | 2–10× |
| Afterglow | < 2× |

The engine exposes with AgX tonemapping (ADR-039) and has chroma retention in compressed highlights.
The retention matters: without it a cyan bolt tonemaps to white.

References:

- [R13] Reed & Wyvill 1994. They model the bolt as a random walk whose segment angles are normally
  distributed around the main direction (mean ~16°), with branching probability per step, and render
  it as glowing line segments.
- [R14] The dielectric breakdown model (Niemeyer 1984; Kim & Lin 2004/2007). It gives physically
  plausible branching but costs too much per frame. It is fine as a *seeded, cached* precompute, and
  is listed as a BOLT quality mode.
- Midpoint displacement: recursively offset each segment midpoint perpendicular to the segment by
  `±jaggedness·length`, halving at each level. This is the standard game technique and has O(2^depth)
  cost.

Rejected: "random segments", meaning unstructured jitter with no scale hierarchy and no branching
statistics.

---

## Lightning  (`lightning`)

**2.1 Definition.** A discrete strike event: a branching channel from a source (sky, cloud, entity)
to a target (the ground or an entity). It has a temporal structure and lights the world.

**2.2 References.** [R13] and [R14] for shape. For the temporal model, the physical sequence is a
stepped leader, then a return stroke (the brightest phase), then 2–4 re-strikes over ~100–300 ms with
dark gaps, then decay. Tatarchuk's ToyShop lightning, from the SIGGRAPH 2006 course [R30], adds a
sky flash and scene relighting.

**2.3 Visual anatomy.**

1. A thin leader grows down over ~2–4 frames.
2. The full-brightness return stroke: a white core with a coloured glow, and branches dimmer than the
   main channel.
3. The scene flashes, with hard shadows from the strike's light and a brightened sky.
4. It flickers off and on 2–3 times.
5. The glow decays.

**2.4 Implementation.**

**Shape (BOLT).** For each strike index `i`, the seed is `seed ⊕ i`. The main channel is built by
midpoint displacement between endpoints to depth 6–8 (64–256 segments). Branches spawn at vertices
with probability `p·(1 − depth/maxDepth)`, deviate 20–60°, and have length and intensity decaying by
~0.5 per generation. The result is cached per strike index, so it is generated once per strike. That
is at most a few hundred vertices.

**Rendering (RIBBON).** Every segment is a camera-facing quad strip with two layers:

- The core, with width ≥ 1.5 px clamped in the vertex stage so a thin core never aliases away.
- The glow, a wide Gaussian profile.

Both use additive blending, depth testing and no depth write, and write HDR and emission.

The leader reveal is an arc-length cutoff: `s ≤ reveal(age)`.

**Light (LIGHTMOD).** A transient pool point light sits at the channel's lower third, with intensity
following the stroke envelope. For a distant strike, a short boost of sky/ambient exposure sells the
flash. The flash can optionally cast shadows (`castsShadow`, the cube map uses 6 of
`kMaxShadowViews = 8`, so it is gated by a quality flag).

**Endpoints.** The source and target are `EffectEndpoint`s (`effect_registry.hpp:541`). Ground
targets snap to terrain through `src/scene/ground_query.*`.

**Needs:** BOLT, RIBBON, LIGHTMOD and TRIGGER.

**2.5 Targets.**

- **World:** sky strikes at random or authored points.
- **Entity:** strikes to or from the owner.
- **Light:** no. It creates its own light.
- **Camera:** no.

**2.6 Parameters.**

| Parameter | Notes |
|---|---|
| `source` | endpoint |
| `target` | endpoint |
| `jaggedness` | |
| `depth` | |
| `branchProbability` | |
| `branchDecay` | |
| `coreWidth` | |
| `glowWidth` | |
| `coreIntensity` | |
| `glowColor` | |
| `restrikes` | 0–4 |
| `strokeDuration` | |
| `afterglow` | |
| `flashIntensity` | |
| `flashShadows` | |
| `skyFlash` | |
| `trigger*` | |
| `randomRate` | strikes per minute, for a Poisson schedule on a fixed grid |
| `seed` | |

**2.7 Modulation.**

- Triggers: `music.drop`, `music.impact`, `audio.onset` (strength above a threshold), `beat` every N
  bars, or a timeline marker.
- Continuous: `audio.rms → randomRate`, and `audio.treble → jaggedness`.

**2.8 Animation model.** The phases are idle, leader, return stroke, restrikes, afterglow, then
complete. Each is a function of `age = t − t0`, and the strike index is `i = count(triggers ≤ t)`.
There is no state. The random schedule places a strike in grid cell `k` when
`hash(seed, k) < rate·cell`, with its time offset hashed too. That is pure.

**2.9 Compositing.**

- The bolt is additive and depth-tested, so it is hidden behind hills. It is fogged by `applyFog`
  (blended path). The fog attenuation should use a reduced coefficient, since a bolt is bright enough
  to punch through.
- The flash light lights surfaces, fog and particles (volume `localInScatterAt`).
- The bolt blooms through the emission target.
- The bolt writes zero velocity. It must be excluded from motion blur, because it is instantaneous.

**2.10 Stack behaviour.** Geometry (RIBBON) and `Lighting` (flash). It can optionally emit Sky-stage
exposure.

**2.11 Performance class.** Low–Medium. CPU generation runs once per strike. Fragment cost is the
glow overdraw, and a shadowed flash adds an extra shadow pass.

**Presets.** Storm Strike, Beat Lightning (on drops), Divine Smite (entity target, gold), Red Sprite
(upward, red, slow).

---

## Arc  (`arc`)

**2.1 Definition.** A *continuous* electric arc between two endpoints that stays alive and crawls: a
Tesla coil, or a link between two UFOs. Lightning's generator is reused, with no strike sequence.

**2.2 References.** [R13], midpoint displacement, and the Jacob's-ladder look.

**2.3 Visual anatomy.** One to four writhing strands between the endpoints. They re-form several
times a second, flicker, and sometimes throw short side-branches.

**2.4 Implementation.**

- BOLT is regenerated at `rate` Hz with `seed ⊕ floor(t·rate)`.
- Between regenerations, the displacement is cross-faded between seed `k` and seed `k+1` so the arc
  writhes rather than pops.
- The endpoints are transformed by the owners every frame.
- Rendering uses RIBBON.
- An optional LIGHTMOD point light sits at the midpoint.

**Needs:** BOLT, RIBBON, LIGHTMOD.

**2.5 Targets.** Entity (to another entity, or to a world point), World.

**2.6 Parameters.** `source`, `target`, `strands`, `rate`, `jaggedness`, `sag`, `coreWidth`,
`glowWidth`, `intensity`, `color`, `flicker`, `light`.

**2.7 Modulation.** `audio.treble → rate`, `audio.bass → intensity`, and `owner.cameraDistance`
(LOD on depth).

**2.8 Animation model.** Stateless: the seed is a function of t.

**2.9 Compositing.** As Lightning, without the sky flash.

**2.10 Stack behaviour.** Geometry (RIBBON) and `Lighting`.

**2.11 Performance class.** Low.

**Presets.** Tesla Coil, UFO Link, Unstable Conduit.

---

## Electric Field  (`electricField`)

**2.1 Definition.** Electricity crawling over the owner's *surface*. It has two components, and
either or both can be on:

- **Surface Crackle:** an emissive animated cell-edge pattern on the surface.
- **Crawling Arcs:** short arcs hopping between surface points.

**2.2 References.**

- Cellular (Worley) edges for crackle. `voronoiF1` exists (`shaders/noise.wgsl`). Proper edges need
  F2−F1, which does **not** exist, so this is a small addition to `noise.wgsl`.
- [R13] for the short arcs.

**2.3 Visual anatomy.** A flickering blue-white web on the surface, plus brief arcs jumping out and
back. The surface lights up locally where an arc lands.

**2.4 Implementation.**

**Crackle.** FXL lane `fxCrackle = (color.rgb, intensity)` plus scale and speed. The fragment
computes `edge = smoothstep(w, 0, F2−F1)(p·scale + t·speed)`, masked by `noise > threshold` so the
web breaks up. It is added to emission.

**Arcs.** A surface-point set is sampled once from the owner's mesh on the CPU (area-weighted, 64
points, seeded) and transformed by the owner every frame. Each arc slot picks a point pair within
`hopDistance` from `hash(seed, floor(t·rate), slot)`. BOLT and RIBBON draw it.

Skinned owners use the bind pose × root transform. This is approximate, and it is documented.

**Needs:** FXL, BOLT, RIBBON, a Worley F2 addition, and CPU mesh access.

**2.5 Targets.** Entity. Material is also valid: "this material crackles" is a material-program
Voronoi op, which exists. Crackle mode becomes a preset of *that* once F2 exists.

**2.6 Parameters.** `crackle`, `crackleScale`, `crackleSpeed`, `coverage`, `arcCount`, `arcRate`,
`hopDistance`, `color`, `intensity`.

**2.7 Modulation.** `audio.treble → crackle`, `audio.onset → arcRate`, `owner.speed`.

**2.8 Animation model.** Stateless.

**2.9 Compositing.** Crackle is part of the lit surface: fogged, bloomed, depth-exact. The arcs
behave as Arc does.

**2.10 Stack behaviour.** `Material` (crackle) and Geometry (arcs).

**2.11 Performance class.** Low.

**Presets.** Overcharged, EMP'd, Storm Golem.

---

## Plasma  (`plasma`)

**2.1 Definition.** A turbulent glowing volume, such as a ball of plasma, a fireball core or an
energy orb. It emits light and is not lit.

**2.2 References.**

- Emission-only volume integration in a bounded proxy: GPU Gems 3 ch.30 (Crane, Llamas & Tariq),
  which ray-marches a 3D texture inside a box proxy clamped against scene depth.
- Curl or fbm noise for turbulence [R12]. `curlNoise` and `fbm3` exist in `noise.wgsl`.

**2.3 Visual anatomy.** A hot white core fades to coloured swirling filaments with a soft limb. It
boils, and a halo blooms around it.

**2.4 Implementation.**

**Recommended: SHELL sphere with an in-shader emission march.** The ray-sphere interval is analytic
and clamped to scene linear depth. The march takes 12–24 steps:

`density = falloff(r)·fbm3(p·scale + curl·t)`

Emission accumulates as `L += color(density)·density·dt` with no lighting, since plasma is
self-emitting. The result is written additively to HDR and emission.

*Alternative:* a new `MediumSlot` kind in `volume.wgsl`. That is a better choice for very large
clouds, because it composites with fog properly and is self-shadowed. It consumes one of the four
medium slots (`kMaxMedia = 4`), and those are already contested (see "one medium slot" in the audit).

**Needs:** SHELL, linear depth, `noise.wgsl`, and LIGHTMOD (the core light).

**2.5 Targets.** Entity (the orb follows its owner) and World (placed). Light: the plasma's light is
a pool light it creates.

**2.6 Parameters.** `radius`, `coreIntensity`, `coreColor`, `edgeColor`, `turbulence`, `scale`,
`speed`, `steps`, `limbSoftness`, `light`.

**2.7 Modulation.** `audio.bass → radius`, `audio.rms → coreIntensity`, `beat → turbulence`.

**2.8 Animation model.** Stateless.

**2.9 Compositing.**

- The plasma is additive, so it does not occlude what is behind it. That is correct for emission-only
  media.
- It is depth-clamped against geometry.
- Scene fog in front of it is applied through `applyFog` at the entry point, which is approximate.
- It blooms.

**2.10 Stack behaviour.** Geometry (SHELL) and `Lighting`.

**2.11 Performance class.** Medium. Fragment cost is proportional to screen coverage × steps.

**Presets.** Plasma Ball, Fireball Core, Dark Orb (subtractive tint: an alpha-blended variant).

---

## Energy Shield  (`energyShield`)

**2.1 Definition.** A protective shell around the owner. It is mostly invisible, reveals a pattern at
the rim and at impacts, and shows ripples where it is hit. It has two modes:

- **Sphere:** an ellipsoid shell.
- **Contour:** an inflated hull around the mesh, using REDRAW as Aura does.

**2.2 References.**

- The Fresnel rim.
- [R16] and [R25] depth fade, to highlight where the shell intersects geometry.
- Hexagonal or Voronoi tiling masks.
- Hit ripples as rings travelling from impact points on the shell's surface, which is common game
  practice. The impact list is uniform data.

**2.3 Visual anatomy.** A near-transparent shell with a glowing rim. A hex pattern flashes near
impacts, and bright rings travel out from hits. A bright line runs where the shell cuts the ground.
Background refraction is subtle.

**2.4 Implementation.**

SHELL, drawn with back faces then front faces at lower back-face intensity. The fragment computes:

`rim·pow(1−|N·V|,p)` + `hex(p)·(reveal)` + `Σ_i ring(angle(p, hit_i) − speed·age_i)·decay`
+ `intersect(depth)`

- At most 8 hits are carried, each as a direction and an age from TRIGGER times.
- The hit direction is hashed per trigger index, or aimed at a named entity endpoint.
- An optional DF term adds subtle refraction.

**Needs:** SHELL (or REDRAW), linear depth, TRIGGER, and DF (optional).

**2.5 Targets.** Entity. World works too, as a placed dome, but the separate type Force Field covers
barriers.

**2.6 Parameters.**

| Parameter | Notes |
|---|---|
| `mode` | Sphere or Contour |
| `radius` | |
| `padding` | |
| `rimIntensity` | |
| `rimPower` | |
| `color` | |
| `pattern` | Hex / Voronoi / None |
| `patternScale` | |
| `idleReveal` | |
| `hitIntensity` | |
| `rippleSpeed` | |
| `rippleWidth` | |
| `hitDecay` | |
| `intersectWidth` | |
| `refraction` | |
| `trigger*` | |
| `maxHits` | |

**2.7 Modulation.**

- Hits: `audio.onset`, `music.impact`, and `field.<name>.enter` (existing entity field events).
- Continuous: `owner.speed → rim`, and `audio.rms → idleReveal`.

**2.8 Animation model.** Hits are a function of the last K trigger times (TRIGGER). There is no
state.

**2.9 Compositing.** The shell is blended after opaque with no depth write, and fogged. It blooms.
It casts no shadow.

**2.10 Stack behaviour.** Geometry (SHELL) and `ScreenSpace` (refraction).

**2.11 Performance class.** Medium. Fragment cost is the overdraw of a large shell, drawn twice.

**Presets.** Sci-Fi Bubble Shield, Hex Barrier, Contour Ward.

---

## Force Field  (`forceField`)

**2.1 Definition.** A *barrier region* placed in the world (a wall, box, cylinder or dome) rather
than a shell around an entity. It is revealed by proximity and by intersection.

**2.2 References.** As Energy Shield, plus "proximity reveal". The barrier shows only near entities
or the camera, as a function of the distance from the shell point to a small list of revealer
positions.

**2.3 Visual anatomy.** A scrolling energy pattern on a plane or volume boundary. It is bright where
entities approach it or pass through, and where it meets the terrain. Otherwise it is faint.

**2.4 Implementation.** SHELL in plane, box or cylinder form. The pattern scrolls along a direction.
The reveal term is `Σ smoothstep(R, 0, |p − e_j|)` over at most 8 revealer entity positions,
supplied by the CPU from named entities or the owner's neighbours. Depth-intersection glow and
optional DF complete it.

**Needs:** SHELL, linear depth, SIGNALS (positions), and DF.

**2.5 Targets.** World (primary) and Entity (a projected wall that moves with the owner).

**2.6 Parameters.** `shape`, `size`, `pattern`, `patternScale`, `scrollSpeed`, `color`, `idle`,
`revealRadius`, `revealers`, `intersectWidth`, `refraction`.

**2.7 Modulation.** `audio.rms → idle`, `beat → scrollSpeed`.

**2.8 Animation model.** Stateless.

**2.9 Compositing.** As Energy Shield.

**2.10 Stack behaviour.** Geometry (SHELL) and `ScreenSpace`.

**2.11 Performance class.** Low–Medium.

**Presets.** Laser Grid, Containment Dome, Glowmere Ward (organic cells).

---

## Charge-Up  (`chargeUp`)

**2.1 Definition.** The gather before a release. Energy converges on the owner, which brightens and
crackles more over `chargeSeconds`. It is **a producer of a phase signal**, not a mega-effect. Its
charge value is published so that any other effect can follow it.

**2.2 References.** The convention of anticipation in animation principles. Particle convergence
uses an attractor. The particle system has `attractorPosition`, `attractorStrength` and
`attractorRadius`, plus `orbit` (`src/scene/particles.hpp:144-152`).

**2.3 Visual anatomy.** Motes stream inward from a sphere around the owner and accelerate. The core
brightens (via Glow) and the arc rate rises (via Arc or Electric Field). There may be a rising hum,
which comes from the audio side and is out of scope. The peak is a held flare.

**2.4 Implementation.**

A kind with its own envelope: `charge(t) = ease((t − t0)/chargeSeconds)`, where `t0` comes from
TRIGGER.

It renders:

- **EMIT:** an effect-owned particle system. The spawn shape is a Sphere at `radius`, with inward
  velocity, the attractor at the owner, `orbit` set for swirl, and rate × charge.
- **A core Halo** through SHELL.

It publishes `fx.<id>.charge` (0–1) and the event `fx.<id>.release` on the signal bus (SIGNALS). A
Glow on the same owner routes `fx.<id>.charge → gain`, and an Electric Field routes `→ arcRate`.
That is how stacking happens: through routes, not through sub-effects.

**Needs:** EMIT, SHELL, TRIGGER, and SIGNALS (effect-published signals).

**2.5 Targets.** Entity (primary) and World (a placed charging point).

**2.6 Parameters.** `chargeSeconds`, `ease`, `radius`, `particleRate`, `particleColor`,
`coreIntensity`, `swirl`, `trigger*`, `autoRelease`.

**2.7 Modulation.**

- Trigger: `music.build` starts it.
- `music.drop` releases it.
- Continuous: `audio.rms → particleRate`.

**2.8 Animation model.** The phases are idle, charging (0→1), held (1), release event, and idle.
`charge` is a pure function of t, so a scrub is exact. Its particles use the particle system's seek
relaxation: they are empty after a seek and warm up over a lifetime (ADR-360/395).

**2.9 Compositing.** Particles are additive and emissive, and the core blooms.

**2.10 Stack behaviour.** `Particles`, Geometry (SHELL), and it publishes signals.

**2.11 Performance class.** Low–Medium. Particle compute is bounded by capacity.

**Presets.** Power Gather, Beam Wind-Up, Build-to-Drop (`music.build` → `music.drop`).

---

## Discharge  (`discharge`)

**2.1 Definition.** The release: a burst of arcs radiating from the owner, a spark burst and a flash.
It is often paired with Shockwave and Charge-Up.

**2.2 References.** [R13] for the arcs. The spark burst uses the particle system's `burst`
parameter; the orb scene already routes `audio.onset → particles/sparks/burst`
(`src/scene/orb_scene.cpp:171`).

**2.3 Visual anatomy.** An instant flash. N bolts radiate outward from the owner to random directions
or to the ground, with sparks sprayed out. It fades within ~0.5 s.

**2.4 Implementation.**

A TRIGGER-driven kind:

- **BOLT:** N ≤ 8 bolts from the owner to targets at `radius`, in directions hashed from
  `(seed, triggerIndex, k)`. Ground-snapped targets are optional.
- **RIBBON:** rendering.
- **EMIT:** a spark `burst` on the trigger frame.
- **LIGHTMOD:** the flash.

It triggers from its own trigger or from `fx.<chargeId>.release`. A Shockwave on the same trigger
completes the look.

A particle burst is a *frame event*. It is the one piece that is not a pure function of t: after a
seek past the trigger, the sparks are gone, which is the particle relaxation. Arcs and flash are
exact.

**Needs:** BOLT, RIBBON, EMIT, LIGHTMOD, TRIGGER.

**2.5 Targets.** Entity and World.

**2.6 Parameters.** `bolts`, `radius`, `groundSnap`, `boltDuration`, `sparkCount`, `sparkSpeed`,
`flashIntensity`, `color`, `trigger*`.

**2.7 Modulation.** Triggers: `music.drop`, `audio.onset`, `fx.<id>.release`.

**2.8 Animation model.** The phases are idle, the trigger, the burst (age < boltDuration), fade, and
idle.

**2.9 Compositing.** As Lightning.

**2.10 Stack behaviour.** Geometry, `Particles` and `Lighting`.

**2.11 Performance class.** Low–Medium.

**Presets.** Overload, Bass Discharge, Static Pop.
