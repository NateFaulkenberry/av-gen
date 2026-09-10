# The asset library

## The rule

**One artistic language, many variations.** The library is curated by hand and listed in
`assets/manifest.json`; procedural generation may only place what that manifest names. The renderer
never scans a directory and uses whatever it finds, because that is how a world ends up looking
like it came from four different art teams.

## What is in it

Thirty-one meshes from the Kenney Nature Kit (CC0), in five categories:

| Category | Count | Triangles |
|---|---|---|
| Large structures | 6 | 891 |
| Midground plants | 8 | 570 |
| Small detail | 6 | 504 |
| Rocks | 5 | 340 |
| Fungi | 6 | 480 |
| **Total** | **31** | **2,785** |

Two thousand seven hundred triangles for the entire library. `tools/fetch_polyhaven.py` remains for
photoreal source material, but the photoreal assets are not used by any showcase: they average
150,000 triangles each and, more importantly, they do not look like each other.

## Colour transformation

The assets arrive as a bright cartoon forest -- green trees, red mushrooms. The scenes do not use
those colours.

**Textures come from the asset, factors from the author.** When a scene file writes a `material`
block for a mesh source, its base colour, emission, roughness and metallic replace the asset's while
the asset's texture maps stay bound. That split is what lets one library become several biomes: the
mesh and its maps are fixed, and the palette is retuned per scene. Omitting the material block gives
the asset's own colours.

So `mushroom_redGroup` is cyan in the blue forest and would be magenta in a coral cavern, with no
second asset and no second mesh.

## Emission hierarchy

Most vegetation is not emissive. Roughly:

| Tier | Emission | What |
|---|---|---|
| Normal | 0.0 | rocks, logs, ground |
| Barely | 0.015-0.05 | canopy, undergrowth |
| Clearly | 0.08 | ground cover near the light |
| Hero | 2.4-3.4 | fungal colonies |
| Rare | 2.0 | one amber flower in the whole scene |

## Budgets

- **Textures**: 1k. The photoreal fetcher defaults to 1k for the same reason.
- **Meshes**: `source.meshBudget` decimates an imported mesh once at resolve time; imported LOD
  levels are further decimations at 35%, 12% and 4%. Vertex clustering, so it suits scanned organic
  shapes and would be the wrong tool for hard-surface geometry.
- **Instancing**: one procedural object per asset, many instances. A scene that writes one node per
  placement gets one draw per placement, which is what the first version of the grove did.

## Where the frame time actually goes

Worth recording, because the intuition was wrong. With the photoreal library the grove ran at
27-35 ms; the assumption was that 4.9 million triangles were the cause. They were not:

| Change | Frame time |
|---|---|
| Photoreal library, one node per placement | 27-35 ms |
| Instanced, budgeted, LOD | 15-17 ms |
| Stylized library (2,785 triangles total) | 14-18 ms |
| **Volumetrics off** | **7.4 ms** |

1080p cost the same as 720p, and disabling depth of field and bloom saved nothing. The frame is not
pixel-bound and it was never triangle-bound: **the volumetric march is over half of it.** The
stylized library was the right move for coherence and for memory, and it is not what buys the frame
rate. Volumetric step count and reach are the dial that does.

## Quaternius Stylized Nature MegaKit

Added 2026-09-09, supplied by the user. CC0 1.0 (`assets/quaternius/License_Standard.txt`), 68
glTF meshes with their textures, from the free tier of the pack at https://quaternius.com. This is
the primary library the curated-asset direction called for, and it arrives as glTF, so it needs no
conversion.

| group | meshes |
|---|---|
| trees | `CommonTree_1..5`, `Pine_1..5`, `TwistedTree_1..5`, `DeadTree_1..5` |
| undergrowth | `Bush_Common`, `Bush_Common_Flowers`, `Fern_1`, `Plant_1`, `Plant_1_Big`, `Plant_7`, `Plant_7_Big`, `Clover_1..2` |
| grasses | `Grass_Common_Short/Tall`, `Grass_Wispy_Short/Tall` |
| flowers | `Flower_3_Single/Group`, `Flower_4_Single/Group`, `Petal_1..5` |
| fungi | `Mushroom_Common`, `Mushroom_Laetiporus` |
| rock | `Rock_Medium_1..3`, `Pebble_Round_1..5`, `Pebble_Square_1..6`, `RockPath_*` |

It is not in the manifest and no scene uses it yet: ecology is phase 3 of the world, and the
hierarchy is deliberate -- terrain, then biomes, then what grows on them. Like the other packs the
files are gitignored; the pack is a single download and the licence permits redistribution, but
120 MB of textures does not belong in the history.

One thing to check when it is used: these meshes carry several materials each (cap and stem, trunk
and canopy), and `mergedAssetMesh` collapses an asset to one material chosen by triangle count. The
Kenney fungi already read as single-colour blobs for this reason. Multi-material instancing is the
open problem, not the assets.

## Skies

Added 2026-09-09 for ADR-049. CC0, from Poly Haven, fetched by
`tools/fetch_polyhaven.py --hdri --fetch` into `assets/hdri/` with `assets/hdri/manifest.json`
recording the provenance. The `.hdr` files are gitignored: six files at three resolutions is
210 MB.

| id | what it is | source |
|---|---|---|
| `kloppenheim_02_puresky` | clear moonlit night: a hard-edged moon 17 degrees up, dense stars, a faint Milky Way, a broad lunar haze along the horizon. Greg Zaal (original), Jarod Guest (sky edits) | https://polyhaven.com/a/kloppenheim_02_puresky |
| `qwantani_moonrise_puresky` | a hazier, brighter moonrise; more atmosphere, fewer stars. Greg Zaal, Jarod Guest | https://polyhaven.com/a/qwantani_moonrise_puresky |

Poly Haven's "pure sky" variants have the photographed ground replaced with a synthetic gradient,
which is what a scene with its own terrain wants: nothing to hide below the horizon.

**Fetch 2K, 4K and 8K; ship 4K.** At 2K a star is a blurred four-pixel blob at 1280x720; at 4K it
is a crisp point; 8K resolves a handful more faint ones and is otherwise indistinguishable there,
for four times the memory (89 MB resident against 356) and three times the load (425 ms against
125). Higher output resolutions will want 8K. 24K exists and is never the answer.

The two skies differ by about 4x in mean radiance (0.22 against 0.78), so a scene's `intensity` and
`skyIntensity` do not carry across a swap. Both peak past the half-float range at the moon --
1.0e5 and 1.8e5 at 4K -- which is why `gpu::uploadTextureAsHalf` saturates rather than letting a
texel become `+inf`.
