# The asset library

## Project ownership and AI awareness

AV Gen now has a shared lightweight catalog for the first phase of project asset ownership. It
does not replace the curated manifest above; it gives the application and AI tools one vocabulary
for discovering files that are already available in the session.

The catalog classifies visible files as:

- `builtin`: files found under the configured content roots outside the open project's `assets/`
  directory.
- `project`: files under the open project's `assets/` directory (and project root for legacy
  project-local files).
- `external`: reserved for future legacy/path migration records; arbitrary files are never added
  to the catalog unless the host places them in an authorized readable root.

AI discovery uses the same catalog as the future Asset Library:

```text
asset.list  { query?, limit? }
asset.search { query, limit? }
asset.get { id }
```

Each record includes a stable session identity such as `asset://builtin/models/tree.glb` or
`asset://project/models/my-tree.glb`, source ownership, type, name, physical path and basic tags.
The physical path is returned for tool execution, but scene authoring should prefer the stable ID.
Search is case-insensitive over ID, name and type. `asset.get` resolves one stable ID to the same
authoritative record, so AI scene authoring does not rely on ambiguous filenames after import.

Project visual import is available to the AI through:

```text
asset.import { file }
```

The source must resolve through the existing canonicalized `ToolContext::contentRoots()` boundary.
Supported first-phase types are glTF/GLB models, HDR/EXR environments and common textures. The
file is copied into `<project>/assets/{models,environments,textures}/`; an existing SHA-256 match
is reported as a duplicate instead of copied again. A `.gltf` import preflights and copies its
referenced buffer and image sidecars, rejecting missing dependencies before any project file is
written rather than creating a half-imported asset. No source file is modified, and no destination
outside the project is writable by this tool. Filesystem I/O failures are returned as hard tool
errors. Newly created files are tracked during the operation and removed if dependency copying or
manifest replacement fails; pre-existing files are never removed by rollback.

Every successful visual import also updates `<project>/assets/manifest.json` atomically. The
manifest stores the stable project ID, asset type, project-relative path and SHA-256 source hash.
It is intentionally small and deterministic: it is an ownership/index record, not a copy of the
bundled library and not a second scene format. Re-importing the same content reports a duplicate;
replacing an asset and migrating legacy scene references are later phases that can retain this ID.

This is deliberately phase one. Existing scene files still use their legacy path fields and the
catalog does not silently rewrite them. Stable `asset://` scene references, a visual Asset Library
panel, thumbnails, replacement/delete workflows and migration of old absolute paths remain the
next ownership phase. Built-in content is not copied into new projects merely because it appears
in the catalog.

Composition saves now take the first migration step: when a referenced file resolves under the
composition's project `assets/` directory, the saved node uses `asset://project/...`; built-in and
external files retain relative/legacy paths rather than being relabeled without ownership proof.
The registry resolves those IDs on the next load, while old path-based scenes remain readable.
The composition integration suite covers this round trip with a project-local glTF and verifies the
stable ID is written instead of an absolute path.

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

These meshes carry several materials each (cap and stem, trunk and canopy). That used to collapse
to one material per asset -- the Kenney fungi read as single-colour blobs for exactly this reason --
and since 2026-09-10 an asset is split into one instanced object per material at flatten time
(ADR-044), so a tree's bark and its leaves each get their own draw and their own maps.

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

## The Glowmere vegetation is in the manifest (2026-09-10)

Thirteen Quaternius entries were added to `assets/manifest.json` under a new `quaternius` group, so
the World Builder can compose from the species Glowmere is actually built out of. **This supersedes
the sentence in "Quaternius Stylized Nature MegaKit" above that says the pack is not in the
manifest.** The pack's own files are unchanged; what was added is a description of them.

The derivation -- every height, emissive weight, density and tag, and where each number came from --
is in `docs/glowmere-asset-families.md`. The short version:

| family | entries |
|---|---|
| canopy | `CommonTree_1`, `TwistedTree_2`, `DeadTree_1` |
| understorey | `Bush_Common`, `Plant_1_Big` |
| ground cover | `Fern_1`, `Grass_Common_Short`, `Flower_3_Group` |
| fungi | `Mushroom_Common`, `Mushroom_Laetiporus`, `Mushroom_Common_Beacon` |
| rock | `Rock_Medium_1`, `Pebble_Round_2` |

Three consequences worth knowing before touching either pack.

**`material.emissive` is a weight, not an intensity.** The composer computes
`rung x weight x bioluminescence`, where the rung comes from the art-direction profile's emission
ladder and the tags choose which rung. So a manifest entry says how brightly a species burns *for
its kind*, and can dim a rung but never promote a species past one. The Quaternius entries are
written that way. The Kenney entries predate the rule and read their `emissive` more absolutely --
`tree_tall` at 0.06 lands on 0.0021, about seventeen times dimmer than Glowmere's canopy at the same
rung. Nothing was changed about them; they are simply not calibrated the same way.

**The manifest now spans two packs, and the composer reads all of it.** A world generated from
`assets/manifest.json` will mix Kenney and Quaternius species, which is exactly what the "one
artistic language" rule at the top of this file is against. A recipe's `assetLibrary` field is the
lever: a manifest per pack would keep a composed world coherent. That has not been done.

**The Quaternius files are gitignored; the Kenney meshes are committed.** `assets/quaternius/` is
excluded by `.gitignore:14`, and the pack is a manual download from quaternius.com rather than
something `tools/fetch_polyhaven.py` restores. The manifest is committed and now names thirteen
files that a fresh checkout does not have, so
`tests/unit/test_asset_library.cpp`'s "resolves assets independently of the working directory" case,
which asserts `fs::exists` for every entry's resolved path, will fail without the pack present.

## Mesh LOD generation: meshoptimizer (2026-09-10)

Added for ADR-078, ahead of the renderer's Phase 3. `src/assets/mesh_lod.hpp` builds LOD chains
from a `scene::MeshData`; nothing consumes them yet and nothing in `src/rendering/` changed.

**Licence: MIT.** Verified from the files that actually land in the tree rather than from memory:
`.cache/cpm/meshoptimizer/3811/LICENSE.md` at tag `v1.2` (commit `9d9890c`), "MIT License,
Copyright (c) 2016-2026 Arseny Kapoulkine", with the same notice repeated at the head and foot of
`src/meshoptimizer.h`. MIT is compatible with this project and with every other dependency here;
the obligation is to preserve the copyright and permission notice, which the library's own headers
and `LICENSE.md` carry and which the source cache keeps intact.

Pinned in `cmake/Dependencies.cmake` with `MESHOPT_BUILD_DEMO`, `MESHOPT_BUILD_GLTFPACK`,
`MESHOPT_BUILD_SHARED_LIBS` and `MESHOPT_INSTALL` all off, so only `src/*.cpp` compiles. That
matters for licensing as well as for build time: the repository vendors `extern/cgltf.h`,
`extern/fast_obj.h` and `extern/sdefl.h` for gltfpack and the demo, and with those targets off none
of them is compiled, linked or shipped. **`docs/dependencies.md` still lists meshoptimizer under
"Planned, not yet added"; it needs a row.**

### What it changes about the budgets above

The "Budgets" section says an imported mesh is decimated once at resolve time by vertex clustering,
with LOD levels at 35%, 12% and 4%. That is still true and still the code that runs. What ADR-078
adds is a second, measured route, and what it found is worth knowing before either is used:

- **`source.meshBudget` frequently does not hit its budget.** Measured on the Quaternius pack,
  `scene::decimateMesh` returned 100% of `Bush_Common` when asked for 50%, and 14.2% of
  `Grass_Common_Short` whether asked for 12%, 7% or 4%. The grid saturates, and the result carries
  no indication that the budget was missed. A mesh budget in a scene file is a request, not a
  guarantee.
- **meshoptimizer is decisively better on connected geometry** — rocks, fungi, ferns, grasses —
  where it hits the requested ratio to within a percent and holds the bounding box still. On
  `Grass_Common_Short` at 50% it had a tenth the geometric error of the clustered version.
- **Vertex clustering is still the better tool for the far levels of the trees**, which are
  non-manifold enough that meshoptimizer's preserving simplifier will not go below 90% of their
  triangles, and whose sloppy-simplified far levels drift further than the clustered ones. ADR-045
  is not superseded.

The Quaternius pack is what the calibration was measured on; the full table is in ADR-078 and the
harness is `tests/unit/test_mesh_lod.cpp` under the hidden `[.lodmeasure]` tag, which skips when
the pack is absent.
