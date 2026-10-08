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
