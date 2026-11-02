# Art Pass 2: third-party assets

Every third-party asset this pass downloads, builds from or reuses, with its source, author and licence. The repository
is public, so none of these files are committed. Downloads live under `assets/strange-attractor/` (gitignored, like
`assets/space/`). Tools that are not assets (Blender add-ons) live outside the repository.

## Downloaded (Gate R2)

| What | Source | Author | Licence | Where it lives | Used for |
|---|---|---|---|---|---|
| MakeHuman system assets (base mesh targets, skins, eyes, eyebrows, eyelashes, hair, proxies, default clothes) | https://files.makehumancommunity.org/asset_packs/makehuman_system_assets/makehuman_system_assets_cc0.zip | MakeHuman community | CC0 1.0 | `assets/strange-attractor/sources/makehuman/` (zip); unpacked into MPFB's private data directory | every human in the cast |
| MakeHuman Suits 01 (eight formal suits) | https://files.makehumancommunity.org/asset_packs/suits01/suits01_cc0.zip | Margaret Toigo | CC0 1.0 | `assets/strange-attractor/sources/makehuman/` | the suited man (`toigo_male_suit_tie_and_jacket`) |

**Tool, not an asset:** the MPFB 2.0.18 Blender add-on (https://extensions.blender.org/add-ons/mpfb/), GPL-3.0. It is
installed into a private Blender user directory (`BLENDER_USER_RESOURCES`), so the user's own Blender is not touched.
MakeHuman's licence page says that a model exported from it is CC0, along with its bundled assets
(https://static.makehumancommunity.org/about/license.html).

## Downloaded by the owner (Sketchfab, CC BY 4.0), placed under `assets/strange-attractor/sketchfab/` (M1)

Shortlisted in `34-pass2-downloads.md`. Licence checked against Sketchfab's model data on 2026-10-31.

| File | Model | Author | Use |
|---|---|---|---|
| `goat/boer-goat/source/Meshy_AI_Boer_Goat_Portrait_0616004314_texture.glb` (+ `textures/`) | Boer Goat 🐐 3D Model Free (Meshy AI-generated) | iRahulRajput | the levitating goat (M5) |
| `gray/grey-alien-idle/grey_alien_idle.glb` | Grey Alien idle | ShaxerTakkuY | the Grays (M3, M4) |
| `props/biobed-mk1/medical_biobed_mk-1.glb` | Medical Biobed Mk-1 | Krahazik | the examination gurney (M4) |
| `sasquatch/bigfoot-matrex/bigfoot.glb` | Bigfoot | Mat Rex | the Sasquatches: rigged, furred and posed by `pass2/build_sasquatch.py` (M1) |
| `ufo/ufo-brunolatour/ufo.glb` | UFO | BrunoLatour | the parked craft and the submersibles (M3) |

Attribution, to carry wherever the work is shown:
- This work is based on "Boer Goat 🐐 3D Model Free" (https://sketchfab.com/3d-models/boer-goat-3d-model-free-d9a3eea388624b6cb6455ddee8866104) by iRahulRajput, licensed under CC-BY-4.0 (http://creativecommons.org/licenses/by/4.0/).
- This work is based on "Grey Alien idle" (https://sketchfab.com/3d-models/grey-alien-idle-4983ac47fa674c1e8e9230c8afa4546e) by ShaxerTakkuY, licensed under CC-BY-4.0 (http://creativecommons.org/licenses/by/4.0/).
- This work is based on "Medical Biobed Mk-1" (https://sketchfab.com/3d-models/medical-biobed-mk-1-75a467fe77f844478251ea74c8c8e0c4) by Krahazik, licensed under CC-BY-4.0 (http://creativecommons.org/licenses/by/4.0/).
- This work is based on "Bigfoot" (https://sketchfab.com/3d-models/bigfoot-31fb69bb153444a39ff82ae2b9708ca9) by Mat Rex, licensed under CC-BY-4.0 (http://creativecommons.org/licenses/by/4.0/).
- This work is based on "UFO" (https://sketchfab.com/3d-models/ufo-9b5d14c4d34d4559abdfc9a392268b8e) by BrunoLatour, licensed under CC-BY-4.0 (http://creativecommons.org/licenses/by/4.0/).

## Built here from those (not committed)

| What | Built by | From | Licence of the result |
|---|---|---|---|
| suited man, Nordic female and male, the Sasquatch base, the Gray base (`.blend`, turntables) | `examples/strange-attractor/pass2/mpfb_cast.py` | MPFB + CC0 packs above | CC0 (derived from CC0 only) |
| `assets/strange-attractor/proofs/gray.glb` (enlarged cranium, almond eyes, forehead triangle, beam) | `pass2/proof_gray_beam.py` | the Gray base | CC0-derived |
| `assets/strange-attractor/proofs/dome.glb` (geodesic crater dome) | `pass2/proof_dome.py` | nothing third-party | original |
| `assets/strange-attractor/proofs/interior.glb` (exam hall, gurney, suited man) | `pass2/proof_interior.py` | the suited man | CC0-derived |
| `assets/strange-attractor/build/sasquatch-{a,b,c}.glb` (rigged, hair cards, idle) | `pass2/build_sasquatch.py` | Mat Rex's Bigfoot (CC BY 4.0) | CC BY 4.0 (attribution above) |
| `assets/strange-attractor/build/campfire.glb` (logs, stone ring, ember bed) | `pass2/build_campfire.py` | nothing third-party | original |
| `assets/strange-attractor/build/pyramid-{a,b,c}.glb` (eroded Cydonia pyramids, sand ramps) | `pass2/build_pyramids.py` | nothing third-party | original |
| `assets/strange-attractor/build/suited-man.glb` (scale reference) | exported from the MPFB suited man | the suited man | CC0-derived |
| `assets/strange-attractor/build/ufo-craft.glb` (opaque, dimmed emission) | `pass2/prepare_ufo.py` | BrunoLatour's UFO (CC BY 4.0) | CC BY 4.0 (attribution above) |
| `assets/strange-attractor/build/ocean-base.glb`, `bay-membrane.glb` (underwater base, launch bay) | `pass2/build_base.py` | nothing third-party | original |
| `assets/strange-attractor/nasa/curiosity/24584_Curiosity_static.glb` (M3b: the Curiosity rover, downloaded from https://assets.science.nasa.gov/content/dam/science/psd/mars/resources/gltf_files/24584_Curiosity_static.glb) | downloaded, no login | NASA/JPL-Caltech 3D resources | public domain (NASA media guidelines; no endorsement implied) |
| `assets/strange-attractor/build/rover.glb` (centred, facing +z) | `pass2/prepare_rover.py` | the NASA Curiosity model | public domain |
| `assets/strange-attractor/build/sasquatch-{a,b,c}.glb` (M3b: three individuals, own base poses, IK-planted idles) | `pass2/build_sasquatch.py` | Mat Rex's Bigfoot (CC BY, above) | CC BY (attribution above) |
| `assets/strange-attractor/build/pyramid-{a,b,c}.glb` (M3b: coursed, chipped blocks, deep sand) | `pass2/build_pyramids.py` | nothing third-party | original |
| `assets/strange-attractor/build/dome-rave-{m,c,g,a,v}.glb` (M3c: the domes' glass in five colour bands) | `pass2/build_dome_rave.py` (from `proofs/dome.glb`) | nothing third-party | original |
| `assets/strange-attractor/build/earth-halo-{a,b,c}.glb` (M3c: the Earth's stylised glow options) | `pass2/build_earth_halo.py` | nothing third-party | original |
| `assets/strange-attractor/build/{scrub-a..c, rock-a..d, seat-rock, basalt-a..c, cave-mouth, laser-fan, kelp-a,b, sponge, fan-coral, seagrass, flash, rover-tracks}.glb` (M3b prop kit) | `pass2/build_kit.py` | nothing third-party | original |

## Already in the library (reused, not committed)

| What | Recorded in | Licence |
|---|---|---|
| `assets/imported/ufo.gltf` (saucer, 4.2k triangles) | `assets/imported/ATTRIBUTION.md` (flexunit, itch.io) | CC0 1.0 |
| `assets/aliens/*.glb` (modular aliens, rigged) | `assets/aliens/ATTRIBUTION.md` | purchased, not for distribution |
| `assets/farm/goat.glb` (low-poly goat, rigged) | `assets/farm/ATTRIBUTION.md` | purchased, not for distribution |
| `assets/quaternius/` (nature kit, Universal Animation Library) | `assets/imported/ATTRIBUTION.md` | CC0 1.0 |
| `assets/space/` (LROC Linné DTM, Blue Marble) | Art Pass 1 (ADR-1882) | NASA, public domain |

## Candidates not downloaded: owner decision

These need a login or an account, or their licence or IP is unclear, so the rule is to stop and ask:

| Candidate | Source | Licence as listed | Why not yet |
|---|---|---|---|
| Sketchfab CC-BY models (a realistic goat, a Gray, a Sasquatch, a stretcher such as https://sketchfab.com/3d-models/stretcher-bd69f5a963714cb2b2c08ee098a03aab) | sketchfab.com | per model, CC-BY | Sketchfab downloads need an account (owner decision 1) |
| Mountain Goat by ShaderAndre | https://www.sloyd.ai/free-3d-models/model/mountain-goat-az746qdi | CC BY 4.0 | the site may need an account; not checked further |
| Quaternius Ultimate Monsters (50 animated monsters) | https://quaternius.com/packs/ultimatemonsters.html | CC0 1.0 | stylized low-poly (not the brief's look); a yeti is not confirmed |
| "willump sasquatch skin" | Sketchfab | CC-BY | a League of Legends character: game IP, rejected |
