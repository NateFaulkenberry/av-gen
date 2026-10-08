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
| Textures | embedded PNGs (copies are in `textures/` for inspection), 1k-2k. **base** = sRGB baseColor plus alpha; **emissive** = sRGB luminous anatomy; **orm** = R ambient occlusion (not referenced by the GLB, so the engine ignores it), G roughness, B metal = 0; **normal** = tangent-space, OpenGL (+Y = +v) |
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

| # | Asset | Archetype | LOD0 tris | LOD1 tris | Slots (texture) |
|---|---|---|---|---|---|
| 1 | `veilcap` | translucent fungus: layered cap, gills, lace veil | 121,004 | 13,936 | membrane MASK 2048x1024; veil MASK 2048x1024; stalk OPAQUE 1024 |

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

**Verified in AV Gen** (`avgen --render`, scene `meshes/hero/check.json`): 3 materials, 12 textures, 0 loader warnings. The MASK lace and the textures read correctly.
