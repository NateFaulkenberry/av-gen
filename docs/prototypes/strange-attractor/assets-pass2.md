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

## Built here from those (not committed)

| What | Built by | From | Licence of the result |
|---|---|---|---|
| suited man, Nordic female and male, the Sasquatch base, the Gray base (`.blend`, turntables) | `examples/strange-attractor/pass2/mpfb_cast.py` | MPFB + CC0 packs above | CC0 (derived from CC0 only) |
| `assets/strange-attractor/proofs/gray.glb` (enlarged cranium, almond eyes, forehead triangle, beam) | `pass2/proof_gray_beam.py` | the Gray base | CC0-derived |
| `assets/strange-attractor/proofs/dome.glb` (geodesic crater dome) | `pass2/proof_dome.py` | nothing third-party | original |
| `assets/strange-attractor/proofs/interior.glb` (exam hall, gurney, suited man) | `pass2/proof_interior.py` | the suited man | CC0-derived |

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
