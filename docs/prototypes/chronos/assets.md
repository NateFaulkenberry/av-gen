# CHRONOS: third-party assets

Every third-party asset this scene uses, with its source and licence. The repository is public, so none of these files
is committed. Outputs built from them stay in gitignored or scratch paths.

| What | Source | Licence | Where it lives | Used for |
|---|---|---|---|---|
| MPFB 2.0.18 Blender add-on (a tool, not an asset) | https://extensions.blender.org/add-ons/mpfb/ | GPL-3.0 | a private Blender user directory (`BLENDER_USER_RESOURCES`), shared with Strange Attractor Art Pass 2; the user's own Blender is untouched | building the creature base and the souls |
| MakeHuman system assets (base mesh and targets, old caucasian male skin, high-poly eyes, eyebrow003, eyelashes01, long01 hair, teeth_base, tongue01) | https://files.makehumancommunity.org/asset_packs/makehuman_system_assets/makehuman_system_assets_cc0.zip | CC0 1.0 (models exported from MakeHuman are CC0: https://static.makehumancommunity.org/about/license.html) | unpacked into MPFB's data directory in that private Blender user directory | the creature look-dev (Gate D0) |

## Built here (not committed)

| What | Built by | From | Licence of the result |
|---|---|---|---|
| `devourer-d0-look-*.png` (creature look-dev stills) | `examples/chronos/devourer/lookdev_creature.py` | MPFB + the CC0 assets above | CC0-derived |

## Reference images

The owner's reference images in `~/Desktop/chronos/` are for study only. They are likely copyrighted, apart from
Goya's *Saturn Devouring His Son*, which is public domain. They are never copied into the repository.
