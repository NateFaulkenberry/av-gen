# Imported assets

Third-party assets brought into av-gen, with their provenance. **Every entry here records where
the asset came from and under what terms**, because "check the licence of every asset" is a
standing rule of this project and a file with no recorded origin cannot be redistributed.

Both assets below are **CC0 1.0 (public domain dedication)**. CC0 requires no attribution; the
credit is given because it is the decent thing to do and because it records provenance for anyone
who needs to re-verify it later.

| Asset | Source | Author | Licence |
|---|---|---|---|
| `alien.gltf`, `alien.bin` | https://duendeds.itch.io/stylized-alien-low-poly-character | duendeds | CC0 1.0 |
| `ufo.gltf`, `ufo.bin` | https://flexunit.itch.io/3d-game-ready-ufo-spaceship | flexunit | CC0 1.0 |

## Also vendored, and not committed

| Asset | Source | Author | Licence |
|---|---|---|---|
| `assets/quaternius/` | https://quaternius.com (Stylized Nature MegaKit, free tier, 68 models) | Quaternius | CC0 1.0 |

Declared by the kit's own `License_Standard.txt`, which travels with it.

It is **gitignored for size, not for licence**: 85 MB, of which about 37 MB is the texture set
duplicated between `glTF/` and `Textures/`. The consequence is worth knowing because it bites
every time: a fresh git worktree has no `assets/quaternius`, so every world loads as bare terrain
behind a wall of "glTF file not found" warnings until the directory is symlinked in from the main
checkout.

## What was changed

Both arrived as binary FBX (version 7400), which this engine does not read — it loads `.gltf` and
`.glb` only. Each was converted with `assimp` (v6.0) rather than adding a permanent FBX dependency
for two assets:

```
assimp export UFO_Low_Poly.fbx        ufo.gltf
assimp export Alien_Low_Green.fbx     alien.gltf
```

The originals (`UFO_Low_Poly.fbx`, `Stylized_Alien.zip`) have been removed from the repository
root now that the conversion is committed; both are CC0 and re-downloadable from the links above.
The archive also held a smooth-shaded variant, a grey colourway, T-pose meshes, and PBR texture
sets for both colourways. Only the green set is kept, under `Textures/Green/`, because it is what
`alien.gltf` references.

**Two things the conversion got wrong and that were fixed by hand**, worth recording because both
fail silently:

- assimp wrote the texture URIs with the FBX's Windows separators -- `Textures\Green\Green_BaseColor.png`
  -- and glTF URIs are forward-slashed. The loader could never have resolved them, so the character
  would have rendered untextured with no error naming the cause.
- `metallicRoughnessTexture` points at `Green_Metallic.png`, which is a metallic-only map, where
  glTF expects roughness in green and metallic in blue. `Green_Roughness.png` is kept beside it so
  the two can be combined properly if the stylized material ever wants them; Glowmere's night
  lighting may not need either.

## What is in them

**`ufo.gltf`** — 2 meshes, 4 materials, 4,220 triangles, no textures, no UVs, no animation.
The body (`UFO.001`, 700 tris) and the lights (`Lights.001`, 3,520 tris) are *separate meshes*, and
the four materials separate the hull (`Gray`), the trim (`Black`), the lamps (`Light`) and an
already-emissive blue (`Blue`, emissive `[0.17, 0.50, 4.11]`). That separation is what lets
different frequency bands drive different parts of the craft rather than tinting the whole thing.

**`alien.gltf`** — 1 mesh, 1 material, 61 nodes, one skin of **49 joints**, and three clips:
`Idle` (3.63 s), `Walk` (1.10 s), `Run` (0.90 s). Authored in centimetres: the bind pose is about
121 units tall, so roughly a 1.2 m character at 0.01 scale.
