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


## Breakthrough pass (05-hero-asset-breakthrough.md): clay, material family, colour

### Neutral-material diagnostic

Every organism is rendered as grey clay with no emission (cut-outs kept) next to its final look, with the same framing:
`render_glbs.py -- <out> hero:clay <glbs>`. The composed views come from `macro_scene.py`.

**Revised because they failed in clay:**

| Asset | Before | After |
|---|---|---|
| glowmat / glowmound | dots on a smooth blob | layered cushion lobes with defined rims, clusters of graded translucent nodules (new `nodule` slot), tufts of miniature pinnate fronds (new `frond` slot), threads and dots kept; the mound is built from big overlapping cushions |
| beadcluster | identical beads on pins | a colony: a gelatinous pad; a translucent coral-like body forking 2-3 times; small sacs at the tips and large ones at the forks; strands sag between neighbours. Slots `body`, `sac` |
| bulbstalk | sticks and balls | articulated stalks (two nodes, a bract), heads that nod over into hanging buds cupped by three dark sepals |
| lattice | a crumpled bag in scene | superseded by `lacefan` (flat perforated fans, radial ribs, blue cell edges); still exported, but not recommended |

**Signature organism: the reliquary (v2).** It combines a layered membrane bud (A), filament fringes (B) and a gelatinous organ (D):
- **Ribs:** seven articulated, keeled blade-ribs with swollen nodes, a slight kink at each node, and thorns along the outer keel.
- **Bud:** in the cage, a three-layer petal bud (closed inner, half-open middle, outer reflexed at the tips). The petals are veined translucent membrane, cyan at the core grading to violet outside; the outer petals are perforated.
- **Core:** a small gelatinous core.

### Material family (section 4): every slot is one of six tissues

| Tissue | Base | Roughness | Emission | Slots |
|---|---|---|---|---|
| translucent membrane | dark, lighter where thin | 0.3-0.6 | rims and veins; light gathers near its source | veilcap membrane/veil, reliquary petals (`shell`), veilfern blade, chalice bell/throat, lacefan fan |
| gelatinous tissue | mid, translucent-looking | 0.1-0.25 | core gradient, dark skin at the ends | lanternberry berry, beadcluster sac, glowmat/mound nodule, reliquary core |
| filaments | dark to pale at tips | 0.25-0.5 | tips only, brightness varied per filament | filigree, veilcap/reliquary fringes, strands, sporophytes |
| veined tissue | dark | 0.3-0.6 | vein trees and channels; never the whole surface | veilcap cap, chalice, veilfern, reliquary rib channel |
| matte organic support | near-black | 0.75-0.9 | none, or faint speckle | stalks, ribs, roots, bark, moss, rocks |
| luminous organs and tips | bright | 0.1-0.3 | a bright core, **brightness varied per organ** | bulbstalk buds, beadcluster sacs, sedge heads, lanternberry berries, nodules |

"Not every tip equally bright": each organ or filament gets its own u-strip, and its brightness is a per-strip hash (0.2-1.0).

### Reference palette (sampled from 00-reference.png)

**Glow:** brightest quarter of the saturated, bright pixels per hue band, as display RGB 0-255.

| Role | Swatch | Share of glow |
|---|---|---|
| electric / sky blue | (64, 134, 220) | 35% |
| blue | (84, 99, 209) | 35% |
| cyan | (49, 206, 237) | 13% |
| emerald | (107, 230, 117) | 6% |
| teal-green | (52, 232, 190) | 5% |
| violet | (145, 104, 231) | 3% |
| lime / yellow-green | (147, 235, 112) | 2% |
| gold / amber | (234, 172, 86) | 0.4% |
| chartreuse | (213, 220, 105) | 0.3% |
| magenta-pink | (202, 100, 221) | <0.1% |

**Shadow and tissue:** median of the dark pixels.

| Role | Swatch |
|---|---|
| deep blue | (8, 18, 35) |
| teal dark | (8, 31, 30) |
| green dark | (11, 31, 13) |
| indigo | (9, 8, 21) |

The reference is overwhelmingly blue and cyan. Green, violet and gold are accents: use them by region and sparingly.

### Colourways (`colorways.py`)

**Files:**
- `hero_<species>_<colour>_lod{0,1}.glb` plus `hero_<species>_<colour>.emit.json`, alongside the default `hero_<species>_lod{0,1}.glb`.
- Same mesh. The emissive image is recoloured (and the tissue tinted for the cool variants) band by band, so a green body with cyan tips becomes, for example, a violet body with blue tips.

**Processing:**
- Saturation is boosted toward the reference.
- A thickness-modulated body glow (lit from within, kept low) is added to every colourway, including the default.

**Pipeline:** `build.py` writes the raw GLBs, then `colorways.py <raw> <out>` writes the final defaults and the variants.

| Species | Colourways |
|---|---|
| bulbstalk | chartreuse, gold, blue, violet |
| glowmat, glowmound | emerald, blue, violet, chartreuse |
| filigree | violet, goldtip (gold tips on a cyan body), blue |
| lanternberry | gold, blue, violet |
| veilcap | emeraldcyan (emerald, cyan rim), chartreuse, violet, blue |
| glowsedge | blue, violet, chartreuse |
| chalice, veilfern | violet, blue, chartreuse |
| bellspire | violet, gold, blue |
| reliquary | violet, gold |
| lacefan | violet, cyan, emerald (default: electric blue) |
| beadcluster | emerald, violet, gold (default: emerald and blue sac by sac) |
| lattice (superseded) | violet, cyan |

Every emissive strength is still 4.0: dial per scene.

**Verified in AV Gen** (`meshes/hero/breakthrough.json`): the revised species plus a mix of colourways, 0 loader warnings, 0 GPU errors.


## Environment pieces (07-cinematic-finishing-pass.md): `meshes/env/`

**Files:** `env_<name>_lod{0,1}.glb` plus textures, in `examples/biovalley/meshes/env/`, generated by `build.py`.
- Same conventions as the hero library: metres, +Y up in the GLB, the asset's own materials (omit the node `material` block).
- Look-dev uses `macro_scene.py <dir> <out> <clay|final> fallset`.
- AV Gen check scenes: `meshes/env/envcheck-fall.json` and `envcheck-karst.json`.

### Riverbank fall set (`riverset.py`): one shared frame

**Frame:**
- The origin is the plunge point at water level.
- **+X (GLB +X) points away from the cliff, toward the riverbank camera.** The cliff face runs along GLB Z.
- The drop is 32 m (the bank-cliff fall, fall3).
- **Placement:** put the origin at fall3's foot, about (-147, 0.8, 862) in world space. No rotation is needed, since world +X already faces the camera.
- The lead's `fall3` strands and bank-cliff massif can be dropped or kept behind it.

| File | Content | Tris (LOD0) |
|---|---|---|
| `env_fall_rock` | **Cliff face:** an alcove behind the fall, so the lip overhangs it; flat-topped ledges either side; vertical fissures; an irregular top edge with lower shoulders; wings that curve back into a gorge head. **Plateau** behind the lip, with **notch channels** where the streams pour. **Pool rim:** nine half-sunk wet mossy rocks, open on the outflow side. Slots `cliff` and `rock` | ~85k |
| `env_fall_water` | Three main streams through the notches (each with a back layer) plus thin threads. Each stream runs along its channel, rolls over the lip, then falls ballistically, widening. **UV v runs down the flow (0 at the lip, 1 at the pool), u across.** MASK, double-sided: streaky, with fine vertical gaps opening toward the foot | ~4k |
| `env_fall_foam` | A foam and churn disc at the impact, at water level; v runs from the centre outward. MASK | small |
| `env_fall_pool` | The pool's rocky bowl, 3.2 m deep, under the water | small |

**Wet and dry:** painted from set-space positions.
- Wet, dark and glossy: behind the falling water, under each notch, inside the spray radius and below 0.5 m.
- Matte and mossy: up-facing surfaces elsewhere.

### Shoreline pieces

Origin at the waterline, Z up, water at 0.

| File | Content |
|---|---|
| `env_shelf_a`, `env_shelf_b` | Shallow stepped shelves, 5.5 m and 3.6 m long, stepping from 0.16 m above the water to just below it |
| `env_waterrock_a`, `env_waterrock_b`, `env_waterrock_c` | Dark mossy waterline rocks, 1.1, 0.7 and 0.45 m; wet and glossy below 0.4 m, matte moss on top. Sink them as needed |

### Karst (`karst.py`): vista landmarks

| File | Silhouette | Size |
|---|---|---|
| `env_karst_tower` | The right-hand landmark: a tiered limestone tower. Four stepped erosion ledges with undercut recesses, vertical fins and fissures, water staining, dark vegetation on the ledges, and a broken crown of three pinnacles of different heights. The base flares into a buttressed apron that sinks below ground | 540 m tall, ~380 m across |
| `env_karst_fin` | A long, thin ridge-blade with a broken crest (supporting) | 380 m tall, ~760 m long |
| `env_karst_stack` | A squat, leaning stack with a flat, overhanging table top (supporting) | 220 m |

All three are about 153k tris at LOD0 and 9k at LOD1.


### Revisions after review (E2, E4)

- **env_fall_water:**
  - The water bulges and curls over the lip on a quarter-circle before it drops.
  - Each stream is two braided ribbons of unequal width that split and rejoin, with irregular widths and sway.
  - The streak texture is warped, not even columns.
  - Edges fray into threads, more toward the foot.
  - The lower third breaks into spray and dissolves (MASK dissolve). UV v still runs down the flow.
- **env_karst_tower:**
  - Taller and narrower: 640 m on a ~210 m base.
  - Crisper, sharper-edged fins and deeper undercuts at each tier.
  - Dark vegetation tufts on the ledges (a `veg` slot).
- **env_karst_fin and env_karst_stack:** broken, notched top edges, stronger vertical fluting, and ledge tufts.

### Moon (`sky.py`): `env_moon`

- A unit sphere (scale it in the scene), with equirectangular UVs.
- **Surface:** soft irregular maria, faint highland relief, three soft craters and one faint ray crater. Relief is only in the normal map; the silhouette stays round.
- **The designed face looks along GLB +Z.**
- **No emission and no cut shape:** the crescent comes from lighting.
- In AV Gen the moon is lit by the scene's lights, so it needs its own light placed sun-side, or a phase-aware shader. With the vista's key light behind it, it reads as a dark sphere (F5).

### Hero clouds (`sky.py`)

**`env_vista_clouds.lobes.json` (the deliverable for the lead's blob renderer):**
- In the vista world frame (metres, Y up), with eye and `moon_dir` recorded.
- Per lobe: `c` (centre), `r` (radius), `scale` (x, y, z), `density` (0..1), `soft` (edge softness as a fraction of r).
- **Four masses**, placed by screen position for the vista camera (the moon projects to (1358, 96) on a 1920x1080 frame):

| Mass | Lobes | Placement |
|---|---|---|
| `bank_left` | 316 | a layered bank below-left of the moon, behind the far peaks, ~7 km |
| `tower_right` | 325 | a towering mass rising just right of the moon, leaving a window, ~8 km |
| `veil_upper` | ~300 | a thin broken band across the top, ~9.5 km, density 0.5 |
| `horizon_band` | 376 | small masses low at the horizon glow, ~11 km |

- Each mass: big lobes, then billows (2-3 levels), then a fringe of small soft lobes for edge breakup.

**`env_vista_clouds_lod*.glb`:** the same lobes as a metaball mesh, in world space (node at the origin, no rotation). Silhouette fallback and framing check only; procedural nodes draw it as a hard cut-out.

**`env_cloud_a`, `env_cloud_b`, `env_cloud_c`:** standalone sculptural cloud meshes (bank, band, broken cluster) in their own frames, each with a `.lobes.json`.

### Near canopy (`bough.py` habits): designed crowns with explicit limb layouts

| File | Habit | Size | Tris (LOD0 / LOD1) |
|---|---|---|---|
| `env_canopy_a` | broad umbrella: short trunk, seven radiating limbs, stacked flat foliage tiers | 55 m wide, 30 m tall | 64k / 1.9k |
| `env_canopy_b` | tall, with layered whorls of horizontal branches (pagoda-like) | 37 m tall | 64k / 3.9k |
| `env_canopy_c` | leaning and weeping, with long drooping curtains | 34 m tall | 70k / 2.4k |

- `shadowbough` is unchanged.
- All are dark matte: moss on the limb tops and rare glints.

### Macro substrate (`substrate.py`)

| File | Content |
|---|---|
| `env_mosscarpet_a`, `env_mosscarpet_b` | broad low cushion carpets, 2.0 and 1.4 m, glowmat tissue at 1/8 glow |
| `env_roots` | surface roots, half-sunk, forking, with moss on top |
| `env_litter` | humus disc ~1.2 m, embedded stones, curled decayed leaves, twigs |

### AV Gen verification

All load with 0 loader warnings and 0 GPU errors, at metre scale with outward normals. Check scenes in `meshes/env/`:
- `envcheck-fall.json`
- `envcheck-karst.json`
- `envcheck-sky.json`
- `envcheck-moon.json`
- `envcheck-canopy.json`
- `envcheck-substrate.json`
- `envcheck-vistaclouds.json`: the lead's vista with the cloud fallback added, paths rewritten.
