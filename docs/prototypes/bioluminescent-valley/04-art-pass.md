# The VFX art pass: diagnosis, experiments and decisions

Spec: `02-vfx-art-pass.md`. Media: `~/Desktop/av-gen-review/45-bioluminescent-valley/artpass/`. The roles:
- **the valley lead** (this branch) owns the diagnosis, the AV Gen import and material side, the macro test scene,
  the method comparison and the shots;
- **the hero asset artist** (`proto/bio-valley-assets`) authors the hero GLBs in Blender and exports them to
  `examples/biovalley/meshes/hero/`.

## Phase 1: diagnosis of CP4 (`cp4/sbs-all.png`, fb29efb0)

**The Creative Critic** (started and run on the four CP4 stills, `preview`, with an intent file) reported 0
findings and scored every dimension 1.0. Its still-image mode measures technical statistics (exposure, saturation,
clipping, and so on) and has no reference-comparison mode. It therefore cannot see the problems below, and I have not
leaned on it. The diagnosis is from the side-by-sides and the measured statistics in `cp4/stats.md`.

### Riverbank (panel 1)

**What improved:**
- the bank's light is reflected as streaks;
- the falls has mist;
- the banks glow;
- the midground is lit and misty.

**The three biggest problems:**

| | Problem | Cause | Experiment |
|---|---|---|---|
| 1 | Half the frame is a flat purple sheet of water that contributes nothing | The water reflects the environment map (sky) uniformly. The camera's 0.6 m eye and level aim give the water 50% of the frame. ADR-1281 reflections are only drawn under emitters | Re-aim the camera up 3°, frame a near bank in the left foreground, and lower the water's `reflection` to darken it; then compare the share of the frame doing work |
| 2 | The banks read as hedges of dots: flat-topped, uniformly stippled walls | Moss-cushion dots and tuft emitters are scattered at constant density along a level bank. There are no rocks or overhangs to break the bank line | Swap the bank's dot moss for shaped organisms (hero assets) plus rocks along the waterline; compare the silhouette |
| 3 | The veil fungus reads as a pale box; there is no hero | The numpy veil's threads hang straight in a ring, so the silhouette is a cylinder | Replace it with a Blender hero (the lantern), sized and placed as the focal point |

### Vista (panel 2)

**What improved:**
- the floor glows across its width;
- the mist is lit;
- the river is visible;
- the masses are layered.

**The three biggest problems:**

| | Problem | Cause | Experiment |
|---|---|---|---|
| 1 | The river reads as a glowing green stripe | The floor layers' reflections in the river (ADR-1281 with a slope) plus the glades crowd the banks; the river is only 34 m wide | Mirror off for the floor; let the water reflect the sky; check that the stripe goes |
| 2 | The masses are repeated dark triangles | The massif catalogue has 13 shapes. Peaks and spires dominate, all lit the same flat dark, with no rim | Swap the near layer to ranges and cliffs and add a moon rim (key direction plus rock program); compare silhouettes |
| 3 | The floor is a speckle of dots, with no ecological regions | The glades are one species at constant density modulated by noise, and every grove carries crown specks | Put glades only in a few authored regions (wet hollows, the river's inside bends), with dark quiet forest between |

### Macro (panel 3)

**What improved:** it is calmer and darker; colour zones (blue, violet, lime) exist.

**The three biggest problems:**

| | Problem | Cause | Experiment |
|---|---|---|---|
| 1 | No convincing organism: flat caps, sticks, rings and dots | The numpy species are parametric shells with no secondary structure (veins, membranes, filaments) | A dedicated macro test scene with one Blender hero (Phase 2); judge it under restrained emission |
| 2 | Large empty dark-green slope | The camera looks down a bare moss bank; the density is in a far band | A macro composition that layers FG mounds, a midground hero, a supporting group and a dark BG curtain |
| 3 | Glow is uniform dots | Every species' light comes from emitter dots of one size | Localised anatomy (gills, knots, rims) on the hero assets; fewer dots |

### Meadow (panel 4)

**What improved:**
- the composition;
- the stream;
- the rose horizon;
- drifts.

**The three biggest problems:**

| | Problem | Cause | Experiment |
|---|---|---|---|
| 1 | Big black FG rocks without detail | Six squashed icosphere boulders, 0.4-1.4 m, flat dark material | Smaller rocks, the rock program, moss and hero plants on them |
| 2 | The far carpet is a bright band in rows | The carpet's tufts at their scatter radius pile up edge-on; one species | A ragged edge (done in CP4b), and multiple layers (ground growth, clusters, tall stalks, rare focal specimens) |
| 3 | A uniform carpet of identical points | One tuft species everywhere | Species layering from the hero library |

## The AV Gen side of hero assets (what the engine can read)

Measured in `src/assets/gltf_loader.cpp` and `Composition` (the procedural mesh path):
- **Attributes:** POSITION, NORMAL and TEXCOORD_0. Vertex colours and TEXCOORD_1 are ignored.
- **Materials:**
  - base colour (factor and texture), metallic/roughness texture, normal map, occlusion;
  - emissive (factor, texture, KHR_materials_emissive_strength);
  - alpha mode and cutoff.

  Transmission, volume, sheen, clearcoat and iridescence are ignored. KTX2, WebP and HDR images are refused.
- **A multi-material GLB** instanced by a procedural node becomes one sub-object per material slot, each with its own
  textures and factors. An authored `material` block overrides the factors for all slots, and one `program` applies
  to all slots.
- **No transmission:** the approximation is an emissive map painted with the luminous anatomy, plus a fresnel-aware
  program (brighter face-on, at thin edges), plus alpha for thin membranes and lace.
- **Emitter templates** (`.emit.json`) mark fine glints for the ecosystem (ADR-1200).

## Phase 2: the first macro test (Gate A attempt), 2026-10-08

`examples/biovalley/macrotest.py` builds one composition for every production method:
- one hero at 0.66 m from a 13 cm high lens, 50° FOV;
- a mossy floor with hummocks and a fallen root;
- supporting organisms: small blue fungi, emerald bank fungi, violet lattices, bulb-tip stalks, fronds, moss
  mounds;
- out-of-focus FG mounds;
- a dark curtain of trees and shrubs behind;
- a cyan back light rimming the hero;
- depth of field focused on the hero.

Renders: `artpass/01-macrotest-sbs.png` (both variants beside panel 3), `01-macrotest-*.png`, and full-resolution
crops.

**Blender variant.** The test hero is the "veiled lantern" (`blender/heroes.py`, built before the team change; the
asset artist now owns Blender):
- a tiered, frilled bell cap;
- 72 radial gills;
- a lace net veil (Blender's wireframe modifier on an irregular cell net).

It has a silhouette nothing else in the scene has. **Verdict:**
- The veil reads as lace, and the frilled cap reads.
- The underside is still one bright mint mass: gill emitters plus the gill emission are too uniform.
- The lace is too regular in thickness and too pale.
- The supporting organisms around it are still the old primitive vocabulary (the lattice is a blob of ribbons).
- The scene is emptier than panel 3.

**Native variant** (the same composition, `life.fungus3d` at hero scale): a ruffled cap on a stick, with dots.
- Clearly weaker: no secondary structure, and the cap reads as a lit sheet.

**Next:**
- tune the lantern's materials (gill light only along the gill edges; the cap membrane dark with a lit rim);
- fill the composition's midground;
- wait for archetype 1 from the artist, then build the three-method comparison on this scene.

## Coordinator notes on the first macro test, as done (09291a3c)

The macro test is rebuilt to panel 3's layout:
- a mossy bank climbs to the back-left, and the low camera looks up into it;
- the hero stands up the bank, left of centre;
- a dense skin of moss mounds (emerald, blue, cyan, with clumped glowing dots), bulb-tip stalks, small blue fungi,
  emerald bank fungi and fronds covers the bank;
- there is no lattice (the asset artist owns it now);
- the lantern's stalk is translucent green (the `bulb` program).

## Engine fix found here: ADR-1283

An authored `alphaMode`/`alphaCutoff`, `baseColorTexture`, `doubleSided` and `unlit` were silently dropped for
every mesh source whose GLB carries a material (all of ours). The cards drew as rectangles, and the valley's fronds
were one-sided and uncut. They are carried over now (CPU test).

## Phase 3: the method comparison (2026-10-08)

The same panel-3 composition and camera, with only the hero and background method changed. Renders:
`artpass/02-method-comparison.png` (each beside panel 3), `02-*.png` at 1080p, and full-resolution `02-*-crop.png`.
GPU at 1920x1080, realtime tier, p50 (60 frames, under the lock):

| | Method | Hero | GPU p50 | Triangles | Image |
|---|---|---|---|---|---|
| A | Native AV Gen | `life.fungus3d` (numpy shells) at hero scale | 44.0 ms | 4.29 M | A ruffled sheet on a stick. No secondary structure; it reads as CG at macro range |
| B1 | Blender-assisted (the lead's test) | the veiled lantern: modifiers (subdivision, solidify, wireframe lace) | 45.4 ms | 4.39 M | A distinctive silhouette (tiered frilled bell, lace veil), but flat untextured materials: the cap reads as one lit mass |
| B2 | Blender-assisted (the asset artist) | **the veilcap**: parametric surfaces plus UV-space anatomy painting, with base, emissive, normal and ORM textures, three tissue materials (membrane, veil, stalk), 121 k triangles | 45.7 ms | 4.40 M | **Clearly the best.** It has gills and veins in the membrane, a translucent rim with dripping beads, a fibrous stalk with a texture, and lace filaments. It stays structurally interesting at restrained emission (Gate A's test) |
| C | Hybrid 2.5D | the veilcap plus three depths of painted silhouette cards (trunks, roots, strands, ferns; alpha cutout) behind the bank | 46.9 ms | 4.40 M | The best overall image: the empty navy sky becomes a dark, out-of-focus forest. Parallax is kept; the cost is +1.2 ms |

**Cost.** The hero method is not the cost driver: B2 against A is +1.7 ms. The textured hero is 16 MB of GLB at
LOD 0, and the asset ships a 14 k-triangle LOD 1 for scene range. The frame is dominated by the scene pass
(26-28 ms: the supporting carpet) and the moss mounds' shadow pass (8 ms), which is the same in every variant.

**Iteration speed:**
- A: about 1 s per change (numpy).
- B2: about 15 s to regenerate an asset headless (`Blender -b -P examples/biovalley/hero_assets/build.py`), plus a
  ~10 s render.
- C's cards: about 1 s.

All three are fast enough.

**Robustness:**
- A and B are full 3D, robust to any camera.
- C's cards are robust only as background, at 1.3 m or more and out of focus. Seen near edge-on, a card betrays
  itself, so they must never be the subject or be crossed by a camera move.

**Decision (proposed, for the owner):**
1. **Hero and focal organisms: Blender-assisted (B2)**, authored by the asset artist, imported as multi-material
   GLBs with their own textures (no material block), with `.emit.json` glints for the ecosystem.
2. **Background depth: hybrid cards (C)**, only for out-of-focus layers behind the subject in the close shots
   (macro, riverbank, meadow foreground).
3. **Supporting carpets and the far ecosystem: native AV Gen**: instanced numpy species, emitter layers and glow
   maps. They are good at density and cheap. As the asset library grows, the artist's LOD 1 meshes replace the numpy
   species where they read better.

**What is still wrong in the best (C) frame:**
- The veilcap's top surface is pale grey, lit like plaster by the moon. That is the base texture's value; it is
  feedback for the artist.
- The supporting fungi still use the striated numpy caps (concentric rings).
- The foreground bulbs are too uniform and too many.
- The frame lacks the reference's bottom-right out-of-focus mound and its deep blue shadows.

Those are the roll-out's first tasks, after the owner's decision.
