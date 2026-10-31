# Art Pass 2, Gate R2: research, audit and proofs

The brief is `30-art-pass-2-brief.md`, with the coordinator's rules in `31-art-pass-2-coordinator-notes.md`. The plan
that follows from this research is `33-pass2-plan.md`; the assets are recorded in `assets-pass2.md`. Media is in
`~/Desktop/av-gen-review/101-strange-attractor/pass2-r/`.

## 1. What already exists (the audit)

Art Pass 1 left most of the machinery this pass needs:

| Need | Exists | Notes |
|---|---|---|
| Portals that open with the camera's approach, seek-safe, clear centre | ADR-1873 corruption, 1879 portal view, 1880 crossing, 1883 nesting | Portals nest (canyon → Moon → dusk). A portal may rotate (exitAxis/entryUp), and a destination is clipped at an exit cut into its own rock. Renderers nest up to three deep: Pass 2 needs a **chain**, not a deeper nest (below). |
| The Matrix language on world geometry | ADR-1872 mesh wire v2, 1873 corruption (blocks, strips, echoes, glyphs, floor, front) | Approved; reused on every portal. |
| A strong image-space mosh for the UFO ejection | `MoshSettings` (temporal family; Digital Mosh ADR-1160..1166), post chromatic aberration | Layered with the geometric corruption (§6.6). |
| Canyon at several times of day without new geometry | the dusk world (ADR-1884) shares the `attractorWorld` generation by hash | Night and dawn are two more scene files of the same kind. |
| The Moon | ADR-1882 generator: craters, boulders, regolith, Earth | Gate R2 adds a `curvature` radius (below). |
| Skinned characters from glTF | the glTF importer: rigs and clips | A 164-joint MPFB rig imports and draws (proofs 06 and 07). Morph targets are ignored: no face shapes, so expressions are posed with bones or not at all. |
| Light in air: shafts, haze, local glow | placed fog volumes (ADR-566), the volume self-shadow march (ADR-570) for shafts, `volumeLocalLights` (ADR-053) | Underwater shafts and beam haze. |
| Beams and fire | effect registry: a light beam that follows its owner (ADR-1670/1671), a fireball that looks like fire (ADR-1665), particles | Forehead beams, the campfire and the embers. |
| Glossy interiors | glossy SSR (ADR-1820), metallic PBR | The UFO interior (proof 06). |
| Stars, Earth | star field (Wave 2); the Blue Marble Earth | Night sky. |
| Music | Feline Footwear 0–60 s, the beat clock (ADR-1780..1785) | Measured below. |

**What does not exist:**
- **Fur.** No fur, hair-card or shell-fur system for skinned meshes. This is the main engine risk (§4).
- **Morph targets.** No face animation; the Gray's head turn is a bone animation.
- **A portal chain longer than three worlds.** Pass 2 visits Moon → dusk → Mars → night → ocean → UFO → dawn. Each
  crossed world hands the frame to the next (ADR-1883). The chain should be a list of worlds in the root, not a
  nesting seven deep (plan §2).
- **Underwater light-shaft occluders.** Shafts need something above to break the light: a wave surface or a ceiling.

## 2. The library (`01-cast/`)

- **The flexunit UFO** (`assets/imported/ufo.gltf`, CC0): a clean saucer, 4.2k triangles, unrigged. It is good for the
  parked craft and the submersibles. It needs its own emissive window and underside lights (plan).
- **The modular aliens** (purchased, rigged, about 5k triangles): green, stylized and cartoonish. **Not a Gray.**
- **The duendeds alien** (CC0): a cartoon. Rejected.
- **The farm-pack goat** (purchased, rigged, 2k triangles): stylized low-poly, white, with recognisable proportions.
  It is a fallback only: the brief asks for a realistic goat.
- **Quaternius**: stylized CC0 kits, plus the Universal Animation Library (retargetable humanoid clips). The look is
  wrong for the cast, but the animation library is useful.

## 3. Sourcing (`assets-pass2.md`)

- **The licensed sources that need no login** are Poly Haven, AmbientCG, Kenney, Quaternius, MakeHuman/MPFB and NASA.
  - They have almost no realistic creatures.
  - Poly Haven and AmbientCG cover surfaces (rock, sand, metal) and HDRIs.
- **Sketchfab is the only large source of realistic CC-BY creatures** (goats, Grays, Sasquatch-like apes, gurneys),
  but **it needs an account to download**. That is owner decision 1.
- **MPFB 2 (MakeHuman for Blender, assets CC0) gives realistic, rigged humans** that are export-licensed as CC0.
  - Proven at R2: the suited man in a CC0 suit with shirt and tie, the Nordic female and male, a heavy base and a
    slim base.
  - 40–65k triangles each, 163 bones (`01-cast/mpfb-*`).
  - They are the plan's backbone for the human-shaped cast. The Sasquatch and the Gray are built over MPFB bases.

## 4. Each environment and effect: references, technique, risk

### 4.1 Lunar terminator (brief 1.1)

- **The reality.** Near the terminator the sun is within a few degrees of the horizon. Only sun-facing slopes and
  crater rims catch light, everything else is in long shadow, and the boundary is ragged, set by the terrain. The real
  Moon's curvature moves the sun about 1° per 30 km, so a few kilometres of flight barely change the sun's angle.
- **Technique, proven (`02-proofs/01`).** A `curvature` radius on the lunar generator (new, tested) bends the ground
  faster. The surface's own normal then carries the camera across a true terminator: the lit fraction falls as
  sin(elevation − d/R), with nothing faded. Over z = 1450 → −100, the lit ground shrinks to rims, then to nothing.
- **The catch, found by the proofs.** A tight radius (12 km) brings the horizon in to under a kilometre, which hides
  distant domes. The plan therefore uses two scales together:
  - a moderate radius (about 150–200 km), for a gentle sun gradient across the route;
  - a designed terrain step: a long sunlit ridge descending into a broad basin in its own shadow, which the camera
    crosses.
- **Earthshine.** The dark side facing Earth gets a faint blue-white fill from it (`02-proofs/03`). The landscape stays
  readable without fading anything.

### 4.2 Crater domes (brief 1.2)

- **Look.** Monumental, ancient, not a city:
  - geodesic or faceted shells sunk into crater floors and walls, partly behind rims;
  - heavy dark ribs and inset panels, many dark, the lit ones warm amber;
  - light spilling onto the crater walls.
- **Proven (`02-proofs/02`).** A procedural geodesic dome (Blender, `pass2/proof_dome.py`), recognisable from more than
  a kilometre behind crater rims.
- **Lessons.** A node's scale is capped at 100, so meshes are built at 100 m. The panels are too white-hot and too
  clean: M1 makes them amber, weathered, irregular and partly broken, and adds a few lit slits and openings. Point
  lights are too weak for crater-wide spill: M1 uses large area-like lights (several point lights) and dust haze over
  the domes.

### 4.3 Campfire and embers (brief 2.2, 4.1)

- **Technique.** The fireball effect (ADR-1665) for flames, particles for embers and smoke, and a flickering warm
  point light (noise-driven intensity, two frequencies) for the figures.
- **Night embers.** The same light at a tenth of the power, with red-orange emissive coals and a thin smoke plume.
- **Risk:** low.

### 4.4 Sasquatch fur at a distance (brief 2.1)

- **Look (`02-proofs/05`, Blender Cycles).** Dark fur figures reading by silhouette, with fire-light rimming the fur
  edges. That is what sells it at a distance.
- **Technique options** for AV Gen:
  - (a) **Hair cards baked in Blender**, exported as alpha-tested mesh strips on the skinned body: no engine work, 100–200k
    triangles each.
  - (b) **A shell-fur engine feature** (N offset shells with a noise mask, in the entity vertex stage): generic and
    cheap, but new engine work.
  - (c) **Displaced, clumped geometry fur** (coarse clumps): reads at a distance, fails close up.
- **Recommendation: (a) for M1, (b) only if (a) flickers.** Cards at a distance are an aliasing risk: the owner's flicker
  sensitivity applies, and TAA plus alpha-to-coverage matter.
- **Proportions.** An ape head (brow ridge, sagittal crest, no neck), long arms, a heavy upper body, 2.2–2.5 m: sculpted
  over the MPFB heavy base (`01-cast/mpfb-sasquatch-base`).

### 4.5 Mars, Cydonia (brief 3)

- **References.** HiRISE images of Cydonia: mesas, knobs and wind-shaped yardangs, under a dusty butterscotch sky with
  low contrast and blue at sunset. HiRISE products are public domain and include Cydonia DTMs, an optional calibration
  source like the LRO one.
- **Technique.** Reuse the lunar generator's machinery with a Mars mode: larger relief, mesas (terraced, flat-topped
  stamps), yardang streaks (anisotropic noise along the wind), dust in hollows; a height-fog haze; a warm sky gradient.
- **Pyramids.** Built procedurally in Blender:
  - stepped cores under eroded casing;
  - broken corners and partial collapse, a different state for each;
  - sand ramps against the bases.

  They are arranged as Giza is: the offset diagonal line and the three sizes.
- **Risk:** medium (art time, not technique).

### 4.6 Deep ocean (brief 5)

- **Look (`02-proofs/04`).** Depth haze and colour read immediately with the existing fog: blue-green, things vanishing
  in the distance.
- **Missing.** Light shafts need an occluder above. M3 adds a rippled ceiling plane (the surface far above) as a shadow
  caster, a slow particulate volume, and caustic flicker on the shelf.
- **The shelf** is a cliff made with the generator's terrain (a big step function).
- **The base** is architecture built into the cliff, lit from inside, with a launch-bay opening (Blender kit).
- **Submersibles** are the flexunit UFO with emissive rims and water displacement: a moving fog-density puff and a
  light cone.
- **Risk:** medium.

### 4.7 The UFO interior (brief 6)

- **Feel (Mothership Zeta: the feel only).** Riveted, ribbed metal, cold cyan light slots, a domed ceiling,
  examination tables under ring lights, and alien machinery around the walls.
- **Proven (`02-proofs/06`).** A ribbed elliptical hall with light slots and a ring light, Grays and the gurney: the
  lighting and the MPFB rigs work in AV Gen.
- **M4 work:**
  - wall machinery and panels (Blender kit), floor grates, haze;
  - the suited man actually on the gurney (posed lying, strapped);
  - the Nordic figures restyled: pale skin, blond hair tint, simple pale garments.
- **Risk:** medium (art time).

### 4.8 The forehead triangle and the beams (brief 6.5, 8.2)

- **Proven (`02-proofs/07`).** A gold rim around a glowing violet core, set into the Gray's enlarged cranium; a violet
  beam that reaches the camera.
- **M4 and M5 work:**
  - the beam as a soft additive cone with haze (`volumeLocalLights`), not a solid cone;
  - the gold edges catching the beam's light;
  - the Sasquatch beams converging on the goat;
  - the camera hit (below).

### 4.9 The invisible observer (brief 6.4)

- **Technique.** On arrival: chromatic aberration and the temporal family's frame echo (a short temporal displacement),
  plus a faint reconstruction (glyph flicker at screen edges), all decaying to a restrained residue.
- **Camera.** Mostly still, high in a corner, with slight micro-drift and image instability.

### 4.10 The ejection (brief 6.6)

- **The most intense moment.** The image mosh (`MoshSettings`) driven to full, RGB separation, row tears, and the UFO
  interior's geometry under the corruption at full field, crossing to the dawn canyon through a portal chained to the
  camera itself.
- The dawn world is composited through a screen-filling, torn aperture as the image breaks.
- **Risk:** medium; it needs a new "portal at the camera" mode (plan §2).

### 4.11 The goat (brief 8.3)

- **Realism.** The only no-login realistic goat found is CC-BY at Sloyd, which may need an account. Sketchfab has more,
  all needing an account. The library goat is stylized.
- **Fallback.** A Blender build of a goat (body from a quadruped base, horns, short coat as cards) is a large art task.
- **Recommendation:** owner decision 1 (allow a Sketchfab account) for a CC-BY realistic goat, or accept the library
  goat restyled (proportions refined, coat cards, darker coat).
- **Effects** (all existing pieces): slow rotation, glow, local data mosh (corruption targets the goat alone),
  disappear and reconstruct (corruption echoes with the field at 1).

## 5. Music (decision)

- **Feline Footwear is 210.5 s (3:30.5)**, measured from `assets/audio/feline-footwear.wav`.
- **The brief's sequence is about 255–270 s** at a pace that does not rush (plan §3). It outruns the track by 45–60 s.
- The options are in the plan (§4).

## 6. Proof list (`02-proofs/`)

| # | File | What it shows | Verdict |
|---|---|---|---|
| 01 | `01-terminator-crossing-z1450-to-z-100.png` | five positions across a true terminator (curvature R = 12 km, sun −2°) | works; the horizon is too close at that R, so a moderate R plus a shadow basin |
| 02 | `02-crater-domes-in-the-dark.png` | two geodesic domes behind crater rims in the lunar night | reads from a kilometre; panels too clean and white (M1: amber, weathered, broken) |
| 03 | `03-earthshine-on-the-dark-side.png` | earthshine fill on the night side | keeps the dark side readable without fading |
| 04 | `04-underwater-depth-and-haze.png` | deep-water colour and depth haze | colour right; shafts need an occluder above (M3) |
| 05 | `05-sasquatch-fur-in-fire-light-blender.png` | fur silhouettes rim-lit by fire (Blender) | the look to match; AV Gen representation plan §4.4 |
| 06 | `06-interior-lighting-grays-gurney.png` | ribbed hall, light slots, ring light, Grays, the suited man (AV Gen) | lighting and rigs work; the man must be posed onto the gurney |
| 07 | `07-gray-triangle-and-beam.png` | the Gray's cranium, forehead triangle and beam (AV Gen) | reads; beam to soften, triangle to catch the light |
