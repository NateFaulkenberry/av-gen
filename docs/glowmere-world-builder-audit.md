# Glowmere → World Builder: audit

Milestone 1. **Nothing in Glowmere was modified to write this.** The scene was read, rendered and
measured as it stands.

Scope: **the painterly scene only** — `examples/world/glowmere-stylized.json` and its composition
`examples/world/glowmere-stylized.scene.json`. The older `examples/world/terrain.json` ("Glowmere
Valley", the 38k-instance showcase) and the matched-PBR comparison project are out of scope and are
not to be treated as the reference look.

Baseline captured for this audit: `--range 8:8.05`, 1280×720, zero GPU errors.

---

## 1. What makes Glowmere visually distinctive

Seven decisions, in the order they matter.

### 1.1 An authored luminance ladder spanning about 200:1

This is the single most important thing in the scene and it is written down explicitly, layer by
layer, as `emissiveIntensity`:

| Role | Layers | Intensity |
|---|---|---:|
| Inert | deadwood, boulders, pebbles | **0** |
| Silhouette | canopy, pines | 0.035 |
| Subtle ground cover | ferns, grass, fan-plants | 0.0615 |
| Noticeable | bushes | 0.295 |
| Special | flowers | 3.94 |
| Rare and bright | shelf-fungi | 4.43 |
| Beacons | beacons | 6.15 |
| Brightest | fungi | **6.89** |

Three things follow from this table that a generated world does not get by accident. Most of the
biomass emits *essentially nothing* — the trees that fill the frame are at 0.035, and three whole
layers are at exactly zero. The bright things are **small**: the two brightest layers are 0.28 m and
1.5 m tall. And the ladder is not a smooth ramp — there is a deliberate **13× gap** between bushes
(0.295) and flowers (3.94). That gap is what makes the bright things read as a different *kind* of
thing rather than as the top of a gradient.

### 1.2 A palette split warm against cool, with the warm reserved for the hero

The world is cool: fern green-cyan `[0.02, 1.0, 0.58]`, grass `[0.05, 1.0, 0.55]`, beacon cyan
`[0.06, 0.82, 1.0]`, bush and flower violet `[0.55, 0.14, 1.0]` / `[0.31, 0.12, 1.0]`.

Warm appears in exactly two places: the canopy's `[1.0, 0.82, 0.45]` at intensity 0.035 (so faint it
reads as a tint, not a light), and the elder's filaments at `[1.0, 0.47, 0.15]`, intensity 1.2.
**The hero is the only warm light in the world.** That is why the eye goes to it from anywhere in
the frame, and it costs nothing.

Measured: mean saturation **0.716**, against 0.583 for the current generated world.

### 1.3 The hero is built, not placed

`elder-crown`, `elder-stem` and `elder-filaments` are three separate procedural nodes, not one
asset:

- **crown** — a 96×48 sphere squashed to `[8.2, 2.0, 7.4]`, two displacement deformers (0.115 at
  scale 1.45, 0.038 at 4.8) to break the sphere, the `paintedCrown` program for the underside mask
  and noise-warped radial gills, heavy motion (mass 120, stiffness 18) so it barely moves;
- **stem** — a 5-point Catmull-Rom tube, radius 1.25, taper 0.65, that *curves*: it leans −0.8 then
  back +0.5 and widens again at the top (scale 1.5 → 0.85 → 0.7 → 0.9 → 1.4). A straight stem would
  read as a cylinder;
- **filaments** — 38 tubes on a radial distribution at radius 6.8, radius 0.045, warm emissive at
  1.2 with `emissiveRandom: 0.7`, light and floppy (mass 0.12, stiffness 0.6, tip amplitude 0.12).

The silhouette reads because the three parts have completely different scales — 8 m cap, 14.5 m
curved stem, 3.4 m hanging threads — and because the filaments move while the cap does not.

### 1.4 Deliberate framing, held for 90 seconds

The camera is `mode: 1` (free) at 40° FOV, driven by two independent timeline tracks for position
and target, 8 keys each over 90 s, mostly `smooth` with an `easeOut` at 80 s. It travels
`(-14, -2.4, 6) → (-30, 10, -118)`: forward through the valley, rising 12 m. It never orbits, never
zooms, and holds its final pose rather than looping.

`foreground-leaves` is a single hand-placed `Plant_1_Big` at `(-19, -8.8, -3)` scaled `[2, 2.4, 2]`.
One node, positioned by hand, that puts a large soft shape across the bottom-left of frame for the
opening. This is the entire foreground-framing device and it is one object.

### 1.5 A moon that rakes, over an ambient that stays out of the way

`glowmere-valley.rig.json`: key 4.5, ambient **0.6** — a 7.5:1 ratio. The moon is directional at
azimuth 64°, elevation **22°** (low), colour `[0.42, 0.62, 1.0]`, 7800 K, shadow strength 0.85,
volumetric 0.12. A fill at −108° / 34°, intensity 0.35. One point light, `elder-practical`, green
`[0.12, 0.75, 0.42]` at intensity 3.0 under the crown.

The rig's own description states the principle: *"the landscape's form comes from the moon raking
across it, and what fills the shadows is the ecology's own light rather than a flat sky term."*

### 1.6 Atmosphere tuned to the valley's actual size

`fogDensity 0.0055`, `fogHeight 4.0`, `fogHeightFalloff 0.1`, fog colour `[0.1, 0.18, 0.32]`;
volumetrics at density 0.006, scattering 0.5, **anisotropy 0.12** (nearly isotropic — no god-ray
look), 12 steps, 320 m max distance, absorption 0.4. Sky zenith `[0.008, 0.016, 0.048]`, horizon
`[0.04, 0.08, 0.17]` — the horizon is roughly 4× the zenith, which is what gives the ridge line
something to sit against.

Terrain adds `groundGlow: 0.08` at scale 0.055 with coverage 0.3, colour `[0.08, 1.0, 0.68]`: the
ground itself is faintly luminous in patches over 30% of its area.

### 1.7 Restrained post

`tonemap: 1` (AgX), `chromaRetention 0.6`, bloom intensity **0.18** at threshold 1.0, knee 0.5,
radius 1.15, 6 levels, `bloomEmissionWeight 0.75`, `antialias 0.75`.

Bloom at 0.18 with a threshold of 1.0 is the reason the bright fungi glow instead of smearing: only
the ladder's top two rungs exceed the threshold at all.

### Measured

| | Glowmere painterly | Current generated world |
|---|---:|---:|
| mean | 0.163 | 0.121 |
| rms contrast | 0.101 | 0.091 |
| p99 | 0.497 | 0.381 |
| shadow fraction | 0.292 | 0.463 |
| **mean saturation** | **0.716** | 0.583 |

Performance, 1440×900 realtime, headless, 300 frames, zero GPU errors: 107 draws, 121 shadow draws,
2 cascades, 15.1 M logical triangles, **2255 visible of 114 357 candidate instances**, LOD split
452/1449/349/5, one particle system (10 240 capacity, 10 emitting), CPU 0.29 ms procedural /
0.77 ms scene. The published figure for this configuration is a 31.2 ms wall median (~32 FPS
reciprocal); this run was not a controlled timing sample and no new speed claim is made here.

---

## 2. What the World Builder can represent directly today

| Glowmere element | World Builder representation | Status |
|---|---|---|
| 13 scatter layers | `world::ScatterLayer` | **Exact** — same type, already |
| Per-layer emissive colour + intensity | `emissiveColor`, `emissiveIntensity` | **Exact** |
| Per-layer height normalisation | `ScatterLayer::height` | **Exact** |
| Emission sparsity / hue variation | `emissiveSparsity`, `hueField`, `chromaDrift` | **Exact** |
| Material programs per layer | `materialProgram` | **Exact** |
| Terrain, biomes, LOD, view distance | `WorldMap` + `TerrainSettings` | **Exact** |
| Fog, volumetrics, sky | `world::EnvironmentPlan` (ADR-067) | **Exact** — the fields line up one for one |
| Negative space | `world::ScatterClearance` (ADR-067) | **Exact** |
| Wind and per-species motion | `wind::VegetationMotion` per layer | **Exact** |
| The luminance ladder | `emissionTierFor` in the composer | **Structurally present**, values differ |
| The palette | `world::PaletteRoles` (ADR-067) | **Structurally present**, needs Glowmere's values |
| Camera keys | Timeline tracks on `camera/position` / `camera/target` | **Exact** |
| Audio routes | `params::ModRoute` | **Exact** |

The important finding: **the painterly scene needs no new engine concept to be described as a
recipe.** Every part of it maps onto a type that already exists. The composer's *values* are wrong,
not its vocabulary.

## 3. What is currently hard-coded

1. **The elder.** Three procedural nodes with hand-authored curves, deformer seeds and a radial
   distribution. Nothing in the recipe vocabulary describes "a squashed sphere on a curved tube with
   38 hanging filaments". This is the single largest gap.
2. **`foreground-leaves`.** One hand-placed node. Trivially reproducible now that placement exists
   (ADR-069), but the *decision* to put it there is not something a recipe expresses.
3. **The camera.** 16 hand-authored keys. `app::Sequence::toTimelineTracks` (ADR-062) can bake shots
   into exactly these tracks, but no shot definition currently reproduces this move.
4. **The two audio routes.** Authored in the project, with specific gains and attack/decay.
5. **`elder-practical`.** A point light in the rig, positioned relative to the hero rather than
   owned by it.
6. **Ground glow.** `groundGlow` / `groundGlowColor` are `TerrainSettings`, not recipe fields.

## 4. What should become reusable World Builder primitives

- **`ArtDirectionProfile`** — the palette, the ladder's rung values and gap, the fog/sky/volumetric
  treatment, bloom restraint, and the warm-reserved-for-hero rule. This is Milestone 2 and it is
  where most of §1 lives.
- **`HeroPoint`** — the spec's §6. Glowmere has exactly one hero and it is hand-built; the primitive
  needs to carry position, importance, preferred camera distance and angle, and a reaction profile.
- **A hero as an authored sub-assembly.** The elder is three related nodes plus a practical light.
  Whatever represents heroes must be able to carry a small group, not a single mesh.
- **Ecological zones** — the spec's §8. Glowmere is one uniform ecology over one biome set; zones do
  not exist yet in any form.
- **Ground glow** as an art-direction field rather than a terrain setting.

## 5. Existing assets that should become asset families

All Quaternius glTF, already in the curated manifest, already CC0:

- **canopy** — `CommonTree_1`, `TwistedTree_2`, `DeadTree_1` (14 m, 15 m, 12 m)
- **understorey** — `Bush_Common`, `Fern_1`, `Plant_1_Big` (1.1 m, 1.4 m, 3.2 m)
- **ground cover** — `Grass_Common_Short`, `Flower_3_Group` (0.7 m, 0.55 m)
- **fungi** — `Mushroom_Common`, `Mushroom_Laetiporus` (0.28 m, 0.55 m; `Mushroom_Common` is used
  twice, at 0.28 m as ground fungi and again at 1.5 m as beacons — the same mesh at two scales
  reading as two species, which is worth keeping as a technique)
- **rock** — `Rock_Medium_1`, `Pebble_Round_2` (1.6 m, 0.28 m)

Note these are **Quaternius**, while `assets/manifest.json` — the library the World Builder composes
from — is **Kenney Nature Kit**. Two different packs. Reproducing Glowmere through the World Builder
requires the Quaternius vegetation in the semantic manifest, with the same authored heights and
emissive weights recorded above. Licences must be re-verified per pack before anything is added.

## 6. What should stay Glowmere-specific

- The elder's authored geometry. It is a designed object and abstracting it into a generator would
  produce a worse elder and a generator nobody else uses.
- The five material programs (`paintedGround`, `paintedCrown`, `paintedFrond`, `glowmereTissue`,
  `bushGlow`). They can be *referenced* by other worlds; they should not be genericised.
- The specific 90-second camera move. It is a shot, not a template.
- `environment.stylized: true`. It is a renderer mode, correctly a scene-level switch.

## 7. Minimum work to reproduce the current visual result

Ordered by value, not by architectural tidiness:

1. Put the Quaternius vegetation into `assets/manifest.json` with the heights and emissive weights
   from §1.1 and the palette from §1.2. **No engine change.**
2. Add a `glowmere` art-direction profile resolving to the §1.1/§1.2/§1.6/§1.7 values, and let a
   recipe name it. Small, contained.
3. Reproduce the elder as a hero sub-assembly the World Builder can place. Needs the hero primitive.
4. Bake the 90-second move as a `Sequence` of shots. Needs shot definitions but no new engine work.
5. Carry the two audio routes into whatever the recipe emits.

Steps 1 and 2 alone would move a generated world most of the way to the Glowmere look, because §1.1
and §1.2 are most of what makes the picture work.

## 8. What must not be changed

- **The ladder's shape.** Not the exact numbers — the *shape*: three layers at zero, most biomass
  under 0.07, a 13× gap before the bright rungs, and the bright things small.
- **Warm reserved for the hero.** One warm light in a cool world.
- **Ambient at 0.6 against a key of 4.5.** Raising ambient to "see more" would destroy the scene.
- **Bloom at 0.18 / threshold 1.0.** The glow is selective because almost nothing crosses it.
- **Anisotropy 0.12.** The mist is not god rays and should not become them.
- **The camera's restraint.** It travels forward and rises. It does not orbit, shake, zoom, or cut.
- **`environment.stylized: true`.** The painterly surface mode *is* the look.

---

## Recorded defects (from the existing docs, not re-verified here)

`docs/stylized-glowmere.md` lists these as open and this audit does not close any of them: noisy
foliage edges and ground patterns, repetitive tree silhouettes, sparse hillside composition, an
overly regular hero, and a weak ending. Neither the 16.67 ms budget nor premium visual quality is
claimed. Whole-shot interactive timing, base-M2 behaviour, thermal behaviour and soundtrack review
remain unverified.

The Syphon frame-burst failure recorded there is still unresolved and is unrelated to this work.

---

# Determination (milestones 3 and 4)

Written after building it, not before.

## Milestone 3: can Glowmere reasonably become a World Recipe?

**Yes for the world, no for the hero and the shot.** That split was visible in the audit above and
building it did not change it.

What came through as a recipe, with no new engine concept:

- the thirteen species, at their authored heights, from the same Quaternius pack;
- the emission ladder, rung for rung, because the profile's rungs *are* Glowmere's numbers;
- the palette, the fog, the volumetrics, the sky, the stylized hemisphere and the light rig's 7.5:1
  key-to-ambient ratio;
- the negative space, now that clearances are applied by the placer.

What did not, and will not:

- **The elder.** Three hand-authored procedural nodes plus a practical light. A recipe has no
  vocabulary for "a squashed sphere with two displacement deformers on a five-point curved tube with
  thirty-eight radial filaments", and inventing one would produce a worse elder and a generator
  nothing else uses. `HeroPoint::assembly` (ADR-072) is the seam; the composer currently places a
  single asset per hero, which is honestly weaker.
- **The ninety-second camera move.** Sixteen hand-authored keys. That is a shot, not a template.

## Milestone 4: what the recipe actually produces

`examples/recipes/glowmere.recipe.json`, composed against `assets/glowmere.manifest.json`.

The manifest is separate on purpose. A world composed from a library spanning two packs mixes two
artistic languages, which is the opposite of what a controlled palette is for — so a recipe that
wants Glowmere's vegetation names Glowmere's library rather than the combined one.

It produces a recognisable alien night valley in the same species and the same palette: ferns and
grass underfoot, mushrooms and a luminous specimen in the midground, tree silhouettes on the ridge,
haze with depth in it.

It is **not** the authored scene and should not be described as one. Measured against the painterly
baseline (`--range 8:8.05`, 1280×720):

| | Glowmere painterly | Glowmere recipe |
|---|---:|---:|
| mean | 0.163 | 0.241 |
| rms contrast | 0.101 | 0.067 |
| shadow fraction | 0.292 | 0.001 |
| mean saturation | 0.716 | 0.601 |

The recipe is flatter and brighter, with almost no shadow. Three reasons, none of them mysterious:
there is no elder, so nothing large is close to the camera casting anything; the five material
programs that do most of Glowmere's colour work are not named by the composer; and the composed
camera is a generated viewpoint rather than a designed shot.

So the fallback position in the brief applies to the parts that did not transfer: keep the authored
Glowmere as the showcase scene, and let the World Builder supply the world *around* a design rather
than replace it.

## What this means for the remaining milestones

The hero assembly is the highest-value unbuilt thing. Everything else in the spec — heroes reacting
to music, a camera that discovers them, spotlighting — is machinery pointed at objects, and the
objects are currently single library assets scaled up. The machinery is real and tested; what it
points at is not yet worth pointing at.
