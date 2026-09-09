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
