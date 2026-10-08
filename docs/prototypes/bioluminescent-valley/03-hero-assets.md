# Bioluminescent Valley: hero asset library (art pass, Phase 2)

The hero organisms are authored in Blender by script, and every asset can be regenerated:

```
cd examples/biovalley/hero_assets
/Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup -P build.py -- \
    --assets veilcap [...] --out <dir> [--copy <dir>] [--render <dir>] [--lod0-only]
python3 sheet.py <render dir> <out.png> <name> ...            # turntable-style contact sheet
```

- **`lib.py`**:
  - parametric grid surfaces and tubes (numpy);
  - UV-space texture painting: noise, cellular noise, and explicit vein trees rasterised on a planar canvas;
  - the Blender material, mesh and GLB export;
  - a GLB patch that writes each slot's declared alphaMode, cutoff and sidedness.
- **`<asset>.py`**: one module per organism, holding its geometry and its painters.
- **Output:** exported GLBs, textures and emitters are generated and gitignored. They go to:
  - `examples/biovalley/meshes/hero/` in the lead's worktree;
  - the same path in this worktree.

## Conventions (what the GLBs contain)

| | |
|---|---|
| Units | metres, real organism scale (the veilcap is 0.32 m tall) |
| Up | +Y in the GLB (authored Z-up in Blender, exported with `export_yup`) |
| Origin | at the base, where the organism meets the ground; roots dip up to 5 mm below y = 0 |
| Files | `hero_<name>_lod0.glb` for the macro shot, full textures; `hero_<name>_lod1.glb` for scene scale, about 1/8 of the triangles, half-resolution textures, same UV layout |
| Emitters | `hero_<name>.emit.json`: `{"points": [[x, y, z, v, u, radius], ...]}` in GLB space (Y up). It marks luminous knots, bead tips and gill-edge beads, for the lead's compute-point glints |
| Attributes | POSITION, NORMAL (authored smooth normals) and TEXCOORD_0. No vertex colours, no tangents (AV Gen doesn't read them) |
| Materials | one per anatomical tissue, named `<name>_<tissue>`; AV Gen makes one instanced draw per slot |
| Textures | embedded PNGs (copies are in `textures/` for inspection), 1k-2k. **base** = sRGB baseColor plus alpha; **emissive** = sRGB luminous anatomy; **orm** = R ambient occlusion (also referenced as `occlusionTexture`), G roughness, B metal = 0; **normal** = tangent-space, OpenGL (+Y = +v) |
| Emission | emissiveFactor (1,1,1), with the brightness in the texture; `KHR_materials_emissive_strength` = 3.0 is the overall dial. The texture carries the gradient: bright core, then dim body, then dark tissue |
| Alpha | `MASK` with cutoff 0.5 for lace, frayed rims, pores and holes. Never BLEND: the procedural path draws blend as opaque |
| Sidedness | membranes, veils and gills are `doubleSided`; stalk tissue is single-sided |

**UV convention** (shared with the lead's material programs):
- **v** runs from the root, centre or attachment (0) to the tip, rim or free edge (1).
- **u** runs around or across the form.
- Where one slot holds several parts, **u** is split into strips. **v** always spans 0 to 1 for each part, so core-to-rim falloff from `v` is valid on every part.

**Translucency** has no transmission support, so it is painted:
- thin regions get a lighter base and an emissive rim;
- veins under the membrane show dimly on the underside sheet;
- lace and holes are alpha cut-outs.

**Scene usage:** omit the node's `material` block, and the asset's own materials and textures are used per slot. An authored `material` block overrides only the factors (colour, emission, roughness); the textures stay.

## The library

Nine archetypes (spec Phase 2, items 1-8, plus the coordinator's lattice). All are verified loading in AV Gen: `meshes/hero/lineup-lod0.json` and `lineup-lod1.json` give 0 loader warnings and 0 GPU errors.

| # | Asset (`hero_<name>_lod*.glb`) | Archetype | Height | LOD0 tris | LOD1 tris | Slots (texture) |
|---|---|---|---|---|---|---|
| 1 | `veilcap` | translucent fungus: layered cap, gills, lace veil | 0.32 m | 121,004 | 13,936 | membrane MASK 2048x1024; veil MASK 2048x1024; stalk 1024 |
| 2 | `filigree` | branching organism, curved tapering filaments, croziers | 0.22 m | 68,304 | 3,040 | filament 1024 |
| 3 | `chalice` | three folded, asymmetric trumpets with glowing throats | 0.22 m | 84,864 | 12,948 | bell MASK 2048x1024 |
| 4 | `lanternberry` | lace-husked luminous berries on a gnarled body | 0.14 m | 52,746 | 10,330 | body 1024; husk MASK 1024; berry 256 |
| 5 | `veilfern` | rosette of broad translucent fronds, one unfurling | 0.28 m | 53,088 | 8,190 | blade MASK 2048x1024; stalk 512 |
| 6 | `bellspire` | tall stalk, umbel of pendant bells, secondary branches | 0.78 m | 32,852 | 2,944 | stalk 1024; bell MASK 1024x512 |
| 7 | `glowmat` | moss and fungal cushion, mycelium, spore colonies, sporophytes | 0.08 m | 44,472 | 3,852 | moss 2048x1024; sporophyte 512 |
| 8 | `reliquary` | focal hero: rib cage cupping a lace-shelled glowing heart | 0.55 m | 40,358 | 4,484 | rib 1024; shell MASK 2048x1024; core 512 |
| + | `lattice` | curling perforated honeycomb sheets, blue-violet | 0.28 m | 39,864 | 4,608 | net MASK 2048x1024; foot 512 |

**Lighter LODs:**
- LOD1 is the same organism, pruned and coarsened from the same seeds (the filigree drops generations 4-5), with half-resolution textures.
- Use LOD1 beyond ~2 m. AV Gen's own ladder can simplify it further.

**Palette:** one shared material language across the family.
- **Tissue:** near-black teal stalks and bodies, velvety (roughness 0.75-0.9).
- **Membranes:** gelatinous (0.3-0.6).
- **Luminous anatomy:** veins, edges, beads, knots and throats, in saturated emerald (0.06, 1, 0.3) and cyan (0.02, 0.75, 1). Lace is blue and violet (lattice, veils, the reliquary's outer shell); the berries are cyan, emerald or a rare blue.
- **Body glow:** below 1% of peak, so a dark body stays dark.

## Round 2 (after the owner's approval)

**Reworked:**
- **chalice:** now five open trumpet cups with glowing throats and thin, crimped, translucent lips. Two slots, `chalice_bell` (outside) and `chalice_throat` (lining). 97,920 / 14,688 tris.
- **glowmat:** a denser thread network and a speckle of emerald and blue dots. The dots vary in size (a few large, many small) and brightness, and gather in drifts. They are painted from the surface's 3D position with `worley3`, so they stay round on slopes.

**New:** organisms and scene support.

| Asset | Use | Size | LOD0 tris | LOD1 tris |
|---|---|---|---|---|
| `glowmound` | the macro's big foreground mound, glowmat tissue | 1.0 x 0.49 m | 61,600 | 11,472 |
| `shadowbough` | dark framing tree (vista left, riverbank) | 29 m tall, overhangs **+X** by ~25 m | 43,396 | 1,700 |
| `riverrock_a` .. `_d` | craggy mossy boulders | 2.0 / 1.1 / 0.7 / 0.3 m | 47,960 | 1,848 |
| `glowsedge` | bank sedge with luminous seed-heads | 0.6-1.4 m | 3,980 | 198 |
| `bulbstalk` | meadow bulb-tip stalks (5 per clump) | 0.12-0.3 m | 1,040 | 144 |
| `canopy` | valley-floor tree, 300 m to 2 km | 16 m | 1,128 | 1,128 |
| `canopygrove` | 7 canopy trees as one instance, ~30 m across | 18 m | 7,266 | 7,266 |
| `darkfern` | low dark understorey fern | 0.25 m | 396 | 36 |

**Notes:**
- **shadowbough:**
  - Trunk plus buttress roots, with moss on the upper side of every limb: limbs are built with `tube_up`, so u = 0 faces up.
  - Foliage: ragged horizontal leaf pads plus curtains of crossed drooping strand cards (MASK).
  - Emission: a few pods, moss speckle and rare leaf-tip glints only.
  - Rotate the instance to aim the overhang.
- **riverrocks:**
  - Moss on top and in the crevices. **Only the moss emits** (a faint spore speckle); the lichen rings are unlit.
  - The waterline band is darker and glossy.
- **canopy:**
  - One level of detail only.
  - Self-shadowing is painted: every lobe darkens toward its underside, and lower lobes take the darker half of the texture.
  - Sparse emerald glints on the upper crown.
- **Instanced in thousands:** glowsedge, bulbstalk and darkfern LOD1s are 36-198 tris.

**Verified in AV Gen** with the lead's ADR-1284 rim fix, 0 loader warnings each:
- `meshes/hero/round2-near.json`
- `meshes/hero/round2-far.json`
- `meshes/hero/lineup-lod0.json`

**Look-dev:** framing assets render against a lit sky (`SILHOUETTE` in build.py), and rocks under brighter light. The light rig scales with asset size.

## Engine note: the emissive Fresnel rim (FIXED, ADR-1284)

`shaders/pbr_shade.wgsl` adds `pow(1 - N.V, 3) * emissiveBase * 0.3 * intensity` to every emissive material. `emissiveBase` is the emissive factor and **not** the texture. These assets carry their colour and localisation in the emissive texture, with a factor of (1,1,1) and strength 4. As a result every surface gets a neutral grazing sheath of up to 1.2, which reads as silver in the bright look-dev sky.

Measured on the glowmat in the lineup:
- moss pixel as exported: 92, against 5 for the ground beside it;
- with emissiveFactor x0.05: 18;
- albedo, roughness and the normal map have no effect on it.

Fixed on the lead's branch (ADR-1284): the rim now follows the emissive texture. After the fix, the same moss pixel reads (3, 17, 11), dark green.

### 1. Veilcap

A 0.32 m fungus. Its anatomy, from the bottom up:
- **Feet:** buttress root tendrils hugging the ground.
- **Stalk:** a leaning, S-curved, fluted stalk with a raised reticulate net on its upper half, and faint blue sap veins.
- **Veil:** a lace indusium hanging from under the cap (cellular net, blue strands, cyan knots, ragged hem).
- **Outer cap:** a broad drooping parasol membrane:
  - an explicit branching emerald vein tree that glows more toward the rim, in patches;
  - a frayed, lobed and irregularly ruffled cyan margin.
- **Underside sheet:** darker and ribbed, with the veins showing through dimly.
- **Gills:** 112 lamellae (full, half and quarter) with wavy free edges that glow emerald to cyan, plus beads.
- **Crown:** a smaller, honeycombed second cap standing proud of the first, its rim dissolving into lace.
- **Filaments:** 22 hair-fine filaments hanging from the rim, each ending in a small luminous bead.

Slots:
- **`veilcap_membrane`**, u strips: cap top 0 to 0.36, underside 0.36 to 0.58, crown 0.58 to 0.82, gills 0.82 to 1.
- **`veilcap_veil`**: the lace veil.
- **`veilcap_stalk`**, u strips: stalk 0 to 0.62, roots 0.62 to 0.80, filaments 0.80 to 1.


### 2-8 and the lattice

Each module's docstring describes its anatomy: `filigree.py`, `chalice.py`, `lanternberry.py`, `frond.py` (veilfern), `spire.py` (bellspire), `mat.py` (glowmat), `reliquary.py`, `lattice.py`.

Every slot uses `v` from root or centre (0) to tip or rim (1):
- **filigree:** v is the path distance from the root over the longest path, so the tips sit at v ≈ 1.
- **lanternberry husks** and **bellspire bells:** v runs from the attachment to the mouth or tip.
