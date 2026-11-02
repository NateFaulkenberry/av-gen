# CHRONOS: third-party assets

Every third-party asset this scene uses, with its source and licence. The repository is public, so none of these files
is committed. Outputs built from them stay in gitignored or scratch paths.

| What | Source | Licence | Where it lives | Used for |
|---|---|---|---|---|
| MPFB 2.0.18 Blender add-on (a tool, not an asset) | https://extensions.blender.org/add-ons/mpfb/ | GPL-3.0 | a private Blender user directory (`BLENDER_USER_RESOURCES`), shared with Strange Attractor Art Pass 2; the user's own Blender is untouched | building the creature base and the souls |
| MakeHuman system assets (base mesh and targets, old caucasian male skin, high-poly eyes, eyebrow003, eyelashes01, long01 hair, teeth_base, tongue01) | https://files.makehumancommunity.org/asset_packs/makehuman_system_assets/makehuman_system_assets_cc0.zip | CC0 1.0 (models exported from MakeHuman are CC0: https://static.makehumancommunity.org/about/license.html) | unpacked into MPFB's data directory in that private Blender user directory | the creature look-dev (Gate D0) |

## Downloaded by the owner from Sketchfab (Gate D1) -- no longer used

The Devourer creature was abandoned on 2026-11-02 (`20-singularity-brief.md`): the Blay and Rodin scans below are no
longer referenced by anything; they stay on disk, gitignored. The log is kept as history.

## (history) Downloaded by the owner from Sketchfab (Gate D1), placed under `assets/chronos/sketchfab/` (gitignored)

All ten models shortlisted in `13-devourer-downloads.md` are present. Licences were checked against Sketchfab's model
records on 2026-11-01. Only the models used are listed here.

| File | Model | Author | Licence | Used for |
|---|---|---|---|---|
| `blay-old-man/old_man_head._miquel_blay.glb` | Old man head. Miquel Blay (https://sketchfab.com/3d-models/old-man-head-miquel-blay-8fe5023517b74eddb564130cbf2e4869) | albert_sierra | CC BY 4.0 | **the creature's base** (the scan route): cut, rigged, pushed past human and repainted by `examples/chronos/devourer/build_scan_creature.py` |
| `rodin-wissant/1917.722_heroic_head_of_pierre_de_wissant.glb` | 1917.722 Heroic Head of Pierre de Wissant (Rodin) (https://sketchfab.com/3d-models/1917722-heroic-head-of-pierre-de-wissant-6af79e37f2bd4ee0b6c16342b2b06bad) | clevelandart (Cleveland Museum of Art) | CC0 1.0 | **the skin relief (D1b)**: its modelled clay surface is ray-sampled into a high-passed relief and baked into the creature's normal map by `build_scan_creature.py`; no Rodin geometry ships |

Attribution, to carry wherever the work is shown:
- This work is based on "Old man head. Miquel Blay" (https://sketchfab.com/3d-models/old-man-head-miquel-blay-8fe5023517b74eddb564130cbf2e4869) by albert_sierra, licensed under CC-BY-4.0 (http://creativecommons.org/licenses/by/4.0/).

## Built here (not committed)

| What | Built by | From | Licence of the result |
|---|---|---|---|
| `devourer-d0-look-*.png` (creature look-dev stills) | `examples/chronos/devourer/lookdev_creature.py` | MPFB + the CC0 assets above | CC0-derived |
| `assets/chronos/build/chronos-creature.glb` (the MPFB-route creature, D1) | `examples/chronos/devourer/build_creature.py` | MPFB + CC0 assets | CC0-derived |
| `assets/chronos/build/chronos-scan.glb` (the scan-route creature, D1, D1b) | `examples/chronos/devourer/build_scan_creature.py` | the Blay scan (CC BY 4.0), with the Rodin relief (CC0) from D1b | CC BY 4.0 (attribution above) |
| `assets/chronos/build/souls/soul-1..8.glb` (the souls, D2) | `examples/chronos/devourer/build_souls.py` | MPFB + the CC0 MakeHuman base and eyes | CC0-derived |

## Reference images

The owner's reference images in `~/Desktop/chronos/` are for study only. They are likely copyrighted, apart from
Goya's *Saturn Devouring His Son*, which is public domain. They are never copied into the repository.
