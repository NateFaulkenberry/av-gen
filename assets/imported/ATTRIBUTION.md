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
| `camera_01.glb` | https://polyhaven.com/a/Camera_01 | Rajil Jose Macatangay (Poly Haven) | CC0 |

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
- `metallicRoughnessTexture` pointed at `Green_Metallic.png`, a metallic-only map, where glTF
  expects roughness in green and metallic in blue. The shader therefore read roughness out of a
  metal mask's green channel -- near zero everywhere -- so the character rendered as a mirror.
  `Green_ORM.png` now packs the authored maps the way glTF expects (R = 1, G = roughness,
  B = metallic) and the material points at it with both factors at 1.0.

  The authored values are uniform: roughness 1.0, metallic 0.0. A fully matte dielectric, which is
  what a stylized character wants and the opposite of what it was rendering.

## What is in them

**`ufo.gltf`** — 2 meshes, 4 materials, 4,220 triangles, no textures, no UVs, no animation.
The body (`UFO.001`, 700 tris) and the lights (`Lights.001`, 3,520 tris) are *separate meshes*, and
the four materials separate the hull (`Gray`), the trim (`Black`), the lamps (`Light`) and an
already-emissive blue (`Blue`, emissive `[0.17, 0.50, 4.11]`). That separation is what lets
different frequency bands drive different parts of the craft rather than tinting the whole thing.

**`alien.gltf`** — 1 mesh, 1 material, 61 nodes, one skin of **49 joints**, and three clips:
`Idle` (3.63 s), `Walk` (1.10 s), `Run` (0.90 s). Authored in centimetres: the bind pose is about
121 units tall, so roughly a 1.2 m character at 0.01 scale.

## The city, characters and concert (imported 2026-09-11)

Brought in for the *All You Got* music video. **742 models, every one verified to load** through
`avgen --scene`; the curated library over them is `assets/city.manifest.json`.

| Asset | Source | Author | Licence |
|---|---|---|---|
| `assets/kenney/city/` (537 GLB: roads, commercial, industrial, suburban, modular buildings, cars, furniture, mini-characters) | https://kenney.nl/assets | Kenney | CC0 1.0 |
| `assets/quaternius/downtown/` (153 glTF) | https://quaternius.com — Downtown City MegaKit, free Standard tier | Quaternius | CC0 1.0 |
| `assets/quaternius/characters/` (18 glTF, 66-joint rigs) | https://quaternius.com — Universal Base Characters, free Standard tier | Quaternius | CC0 1.0 |
| `assets/quaternius/animations/` (5 GLB, **43 clips** on the same 66-joint rig) | https://quaternius.com — Universal Animation Library 1 and 2 | Quaternius | CC0 1.0 |
| `assets/quaternius/street/` (25 glTF) | https://quaternius.com — Street Pack | Quaternius | CC0 1.0 |
| `assets/imported/concert/concert.gltf` (30 meshes: stage, truss, speakers, spotlights, barricade, mic) | lowpoly concert pack | iPoly3D (https://www.patreon.com/ipoly3d/) | **not stated in the download — verify before redistributing** |
| `assets/imported/instruments/` (drumset, guitar, vintage news microphone) | supplied with the same batch | unrecorded | **unknown — verify before redistributing** |

Every Kenney and Quaternius licence file travels with its meshes. The Quaternius packs marked
`[Standard]` are the **free** tier and their `License_Standard.txt` states CC0 1.0, the same terms
as the Stylized Nature MegaKit already here.

**Two entries above have no licence I could verify**, and they are flagged rather than assumed. The
concert pack ships a README with a Patreon link and no licence text; the three loose instrument
files arrived with no accompanying terms at all. They load and they are usable for private work;
they should not be shipped in a public build until somebody confirms the terms.

## What was changed

- **Formats.** The engine reads `.gltf` and `.glb` only. Kenney and the Quaternius city, character
  and animation packs all ship glTF or GLB, so those were copied unmodified. The Quaternius Street
  Pack and the three instruments ship OBJ (and `.blend`, which nothing here reads), and were
  converted with `assimp` 6.0 — the same tool and the same reason as the alien and the UFO above:

  ```
  assimp export Street_4Way.obj              assets/quaternius/street/street_4way.gltf
  assimp export drumset.obj                  assets/imported/instruments/drumset.gltf
  ```

- **Two textures that do not exist.** `Superhero_Male_FullBody.gltf` and its female counterpart
  reference `T_Eye_Normal_png.png` and `T_Hair_1_Normal_png.png`, and the free tier does not ship
  them. A glTF whose image is missing does not load at all — fastgltf reports "an external buffer
  was not found" and the whole character is lost. Rather than edit the asset, a 1x1 neutral normal
  (128, 128, 255 — straight out of the surface) was written at each path. Both characters load now,
  and the materials ask for exactly what they were authored to ask for.

- **Nothing else was edited.** No mesh was re-authored, rescaled or re-materialled on disk. The
  scale differences between the packs are reconciled in the manifest, not in the geometry, which is
  what `preferredScale` is for.
