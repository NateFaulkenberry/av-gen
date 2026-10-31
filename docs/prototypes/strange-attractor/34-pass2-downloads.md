# Art Pass 2: downloads for the owner (Sketchfab)

The owner downloads these by hand (Sketchfab needs a signed-in account). Everything here is **CC Attribution (CC BY
4.0)**: free to use commercially and to modify, with credit. There is no NonCommercial, NoDerivatives or Standard
licence among them.

I checked each licence, polycount, animation count and downloadability on 2026-10-31 against Sketchfab's own model
data: the API that backs each listing page (`https://api.sketchfab.com/v3/models/<id>`). I also looked at each
thumbnail. I rejected game rips and characters that belong to a game or film, even where the listing says CC BY.

## How to download

1. Open the URL and press **Download 3D Model**.
2. Pick **glb** ("Autoconverted format"): one file with its textures inside. If there is no glb, pick **glTF** (a zip)
   and unzip it into the folder.
3. Drop the file into the exact folder given below, under the repository's gitignored `assets/strange-attractor/`.
   Make the folder if it is missing. **Never commit these files.**
4. Keep the file name Sketchfab gives it. I will find it in the folder.

When I build with an asset, I record it in `assets-pass2.md` with the attribution text given here.

## 1. Goat (top priority)

| | Pick | Alternate A | Alternate B |
|---|---|---|---|
| Model | **Boer Goat 🐐 3D Model Free** | Realistic Domestic Goat - Curved Horns | Goat |
| Author | iRahulRajput (rt699448) | Pigcraft (s8819296) | Tolibjon (tolibjon) |
| URL | https://sketchfab.com/3d-models/boer-goat-3d-model-free-d9a3eea388624b6cb6455ddee8866104 | https://sketchfab.com/3d-models/realistic-domestic-goat-curved-horns-f67b3514cb834587ace458ec37c59c1e | https://sketchfab.com/3d-models/goat-15692d6248564a19bf2514ce2344ce70 |
| Licence | CC BY 4.0 | CC BY 4.0 | CC BY 4.0 |
| Polycount | 10.4k faces | 1.92M faces (I decimate it in Blender) | 50k faces |
| Textures, rig | textured; not rigged | textured, shaggy coat; not rigged | textured; not rigged |
| Folder | `assets/strange-attractor/sketchfab/goat/boer-goat/` | `assets/strange-attractor/sketchfab/goat/domestic-goat-curved-horns/` | `assets/strange-attractor/sketchfab/goat/white-goat/` |

**Why.** The Boer goat is a real breed, realistically proportioned and textured (brown and white, floppy ears, short
horns). It is light enough to use as it is. The goat only levitates and rotates, so no rig is needed.

Alternate A is the most realistic of all (a shaggy cream and chestnut coat, long ridged horns), but it is very heavy.
Alternate B is a white goat with big swept horns: a strong silhouette when it glows.

Attribution:
- This work is based on "Boer Goat 🐐 3D Model Free" (https://sketchfab.com/3d-models/boer-goat-3d-model-free-d9a3eea388624b6cb6455ddee8866104) by iRahulRajput, licensed under CC-BY-4.0 (http://creativecommons.org/licenses/by/4.0/).
- This work is based on "Realistic Domestic Goat - Curved Horns" (https://sketchfab.com/3d-models/realistic-domestic-goat-curved-horns-f67b3514cb834587ace458ec37c59c1e) by Pigcraft, licensed under CC-BY-4.0 (http://creativecommons.org/licenses/by/4.0/).
- This work is based on "Goat" (https://sketchfab.com/3d-models/goat-15692d6248564a19bf2514ce2344ce70) by Tolibjon, licensed under CC-BY-4.0 (http://creativecommons.org/licenses/by/4.0/).

## 2. Gray alien (fallback for the MPFB build; sexless body)

| | Pick | Alternate A | Alternate B |
|---|---|---|---|
| Model | **Grey Alien idle** | Classic Grey Alien | Allan the Grey |
| Author | ShaxerTakkuY | Mat Rex (mriexinger) | Rasmus (Rasmus82) |
| URL | https://sketchfab.com/3d-models/grey-alien-idle-4983ac47fa674c1e8e9230c8afa4546e | https://sketchfab.com/3d-models/classic-grey-alien-bd612788fb154df98e32ba307782684b | https://sketchfab.com/3d-models/allan-the-grey-4544e06bbb344c6cad54505584a1a6ad |
| Licence | CC BY 4.0 | CC BY 4.0 | CC BY 4.0 |
| Polycount | 55k faces | 50k faces | 11.2k faces |
| Textures, rig | textured; **rigged, with an idle animation** | textured; not rigged | textured (Substance); rigged, one animation |
| Folder | `assets/strange-attractor/sketchfab/gray/grey-alien-idle/` | `assets/strange-attractor/sketchfab/gray/classic-grey/` | `assets/strange-attractor/sketchfab/gray/allan-the-grey/` |

**Why.** A classic Gray: thin limbs, a big smooth head, big black eyes and no sexual anatomy. It is rigged and already
has an idle, which suits the brief's "subtle" behaviour. The same author has a walking variant:
https://sketchfab.com/3d-models/alien-walk-f339a62af93d406caa64aad1c7851838 (CC BY, 52k faces), which can go in the
same folder.

Alternate A is the textbook small grey with bulbous eyes. Alternate B is leaner and creepier.

Attribution:
- This work is based on "Grey Alien idle" (https://sketchfab.com/3d-models/grey-alien-idle-4983ac47fa674c1e8e9230c8afa4546e) by ShaxerTakkuY, licensed under CC-BY-4.0 (http://creativecommons.org/licenses/by/4.0/).
- This work is based on "Alien Walk" (https://sketchfab.com/3d-models/alien-walk-f339a62af93d406caa64aad1c7851838) by ShaxerTakkuY, licensed under CC-BY-4.0 (http://creativecommons.org/licenses/by/4.0/).
- This work is based on "Classic Grey Alien" (https://sketchfab.com/3d-models/classic-grey-alien-bd612788fb154df98e32ba307782684b) by Mat Rex, licensed under CC-BY-4.0 (http://creativecommons.org/licenses/by/4.0/).
- This work is based on "Allan the Grey" (https://sketchfab.com/3d-models/allan-the-grey-4544e06bbb344c6cad54505584a1a6ad) by Rasmus, licensed under CC-BY-4.0 (http://creativecommons.org/licenses/by/4.0/).

## 3. Sasquatch (fallback and reference for the MPFB build)

| | Pick | Alternate A | Alternate B |
|---|---|---|---|
| Model | **Bigfoot** | Sasquatch | Bigfoot (a LiDAR scan of a statue) |
| Author | Mat Rex (mriexinger) | 3D MODELS (3dworldz) | jtressle |
| URL | https://sketchfab.com/3d-models/bigfoot-31fb69bb153444a39ff82ae2b9708ca9 | https://sketchfab.com/3d-models/sasquatch-403f2e849444418b91ccff44dbae21b4 | https://sketchfab.com/3d-models/bigfoot-a6ad392247524ee7803380d7d52791cb |
| Licence | CC BY 4.0 | CC BY 4.0 | CC BY 4.0 |
| Polycount | 50k faces | 12.9k faces | 218k faces |
| Textures, rig | textured; not rigged (T-pose, so ready for auto-rigging in Blender) | textured; not rigged (A-pose) | scan texture; not rigged (posed) |
| Folder | `assets/strange-attractor/sketchfab/sasquatch/bigfoot-matrex/` | `assets/strange-attractor/sketchfab/sasquatch/sasquatch-3dworldz/` | `assets/strange-attractor/sketchfab/sasquatch/bigfoot-scan/` |

**Why.** A believable, heavy ape body (not a movie monster) in a T-pose: easy to skin to a rig in Blender and to fur.
Alternate A is browner and more muscular. Alternate B has a striking, natural pose and silhouette, but it is a statue
scan with baked lighting, so it is only usable for distant silhouettes.

**Rejected:**
- "bigfoot v1" and the other toro_ardido models: their descriptions name Black Desert, Diablo 2, GTA 5 and
  others (game rips).
- "willump": a League of Legends character.

Attribution:
- This work is based on "Bigfoot" (https://sketchfab.com/3d-models/bigfoot-31fb69bb153444a39ff82ae2b9708ca9) by Mat Rex, licensed under CC-BY-4.0 (http://creativecommons.org/licenses/by/4.0/).
- This work is based on "Sasquatch" (https://sketchfab.com/3d-models/sasquatch-403f2e849444418b91ccff44dbae21b4) by 3D MODELS, licensed under CC-BY-4.0 (http://creativecommons.org/licenses/by/4.0/).
- This work is based on "Bigfoot" (https://sketchfab.com/3d-models/bigfoot-a6ad392247524ee7803380d7d52791cb) by jtressle, licensed under CC-BY-4.0 (http://creativecommons.org/licenses/by/4.0/).

## 4. Examination gurney (UFO interior)

| | Pick | Alternate A | Alternate B |
|---|---|---|---|
| Model | **Medical Biobed Mk-1** | Post apocalyptic examination table | Morgue Table |
| Author | Krahazik | Sousinho | Kevin Saucedo (ksaucedo) |
| URL | https://sketchfab.com/3d-models/medical-biobed-mk-1-75a467fe77f844478251ea74c8c8e0c4 | https://sketchfab.com/3d-models/post-apocalyptic-examination-table-41c14888e60b4910bd5edddf5d0d16c6 | https://sketchfab.com/3d-models/morgue-table-3a1a86f8202240898ef6200b86bef9f1 |
| Licence | CC BY 4.0 | CC BY 4.0 | CC BY 4.0 |
| Polycount | 11.2k faces | 7.3k faces | 3k faces |
| Textures, rig | textured (sci-fi bed with displays) | PBR textures (rusty steel) | textured (steel pedestal table) |
| Folder | `assets/strange-attractor/sketchfab/props/biobed-mk1/` | `assets/strange-attractor/sketchfab/props/exam-table-rusty/` | `assets/strange-attractor/sketchfab/props/morgue-table/` |

**Why.** A clinical sci-fi exam bed that fits an alien interior without looking like a hospital. The two alternates
are more human-medical, which reads as more unsettling.

Attribution:
- This work is based on "Medical Biobed Mk-1" (https://sketchfab.com/3d-models/medical-biobed-mk-1-75a467fe77f844478251ea74c8c8e0c4) by Krahazik, licensed under CC-BY-4.0 (http://creativecommons.org/licenses/by/4.0/).
- This work is based on "Post apocalyptic examination table" (https://sketchfab.com/3d-models/post-apocalyptic-examination-table-41c14888e60b4910bd5edddf5d0d16c6) by Sousinho, licensed under CC-BY-4.0 (http://creativecommons.org/licenses/by/4.0/).
- This work is based on "Morgue Table" (https://sketchfab.com/3d-models/morgue-table-3a1a86f8202240898ef6200b86bef9f1) by Kevin Saucedo, licensed under CC-BY-4.0 (http://creativecommons.org/licenses/by/4.0/).

## 5. Also worth having: the UFO (parked craft and submersibles)

| | Pick | Alternate |
|---|---|---|
| Model | **UFO** | UFO |
| Author | BrunoLatour | sebslom |
| URL | https://sketchfab.com/3d-models/ufo-9b5d14c4d34d4559abdfc9a392268b8e | https://sketchfab.com/3d-models/ufo-75655f43c4fc4c56ab60746caf7119fe |
| Licence | CC BY 4.0 | CC BY 4.0 |
| Polycount | 139k faces | 8k faces |
| Textures, rig | textured; a detailed underside with light rings; not rigged | textured, weathered, green lights; not rigged |
| Folder | `assets/strange-attractor/sketchfab/ufo/ufo-brunolatour/` | `assets/strange-attractor/sketchfab/ufo/ufo-sebslom/` |

**Why.** A far more physical craft than the library's CC0 saucer: panel detail and an underside lit for a parked
craft at night, and for the submersibles. The alternate is lighter and weathered.

Attribution:
- This work is based on "UFO" (https://sketchfab.com/3d-models/ufo-9b5d14c4d34d4559abdfc9a392268b8e) by BrunoLatour, licensed under CC-BY-4.0 (http://creativecommons.org/licenses/by/4.0/).
- This work is based on "UFO" (https://sketchfab.com/3d-models/ufo-75655f43c4fc4c56ab60746caf7119fe) by sebslom, licensed under CC-BY-4.0 (http://creativecommons.org/licenses/by/4.0/).

## Minimum set

If only a few are fetched: **the Boer goat**, **Grey Alien idle** and **Medical Biobed Mk-1**. The Sasquatch is built
from MPFB either way; the Mat Rex Bigfoot helps as reference and fallback.
