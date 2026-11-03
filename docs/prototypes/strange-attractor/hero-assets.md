# Hero assets: the source and licence record

This record covers the hero-asset lane of brief 49: the Gray, the Sasquatches, the campfire and the UFO.

- Every file this lane downloads, builds from or writes is recorded here.
- The outputs live in `assets/strange-attractor/hero/`. It is a symlink into the art agent's gitignored asset folder,
  and it is **never committed**, because the repo is public and some sources are commercial.
- Sources already recorded in `assets-pass2.md` are referenced here, not duplicated.

## Downloaded (by the owner, for this lane)

| File | Source | Author | Licence | Where it lives |
|---|---|---|---|---|
| "Realistic Alien (Grey) Mid Poli" (owner download, 2026-11-03; `~/Desktop/realistic_alien_grey_mid_poli.glb`, left untouched) | https://sketchfab.com/3d-models/realistic-alien-grey-mid-poli-74c15d8bbebf4235838f7ef135ad00f8 | Graphetta (https://sketchfab.com/Graphetta) | CC BY 4.0 (read from the glb's `asset.extras.license`: "CC-BY-4.0 (http://creativecommons.org/licenses/by/4.0/)") | `assets/strange-attractor/hero/sources/sketchfab-graphetta-alien-grey-mid.glb` (a copy) |

Attribution, to carry wherever the work is shown: This work is based on "Realistic Alien (Grey) Mid Poli"
(https://sketchfab.com/3d-models/realistic-alien-grey-mid-poli-74c15d8bbebf4235838f7ef135ad00f8) by Graphetta, licensed
under CC-BY-4.0 (http://creativecommons.org/licenses/by/4.0/).

The owner rejected jtressle's Bigfoot (an incomplete LiDAR scan); it was not downloaded by this lane.

## Built by this lane (in `assets/strange-attractor/hero/`)

| File | Built by | From | Licence of the result |
|---|---|---|---|
| `fur-proof/fur-p{1..6}-*.glb` (the H1 fur proof; disposable) | `examples/strange-attractor/hero/fur_proof.py` | `build/sasquatch-a.glb`, which comes from Mat Rex's "Bigfoot" (https://sketchfab.com/3d-models/bigfoot-31fb69bb153444a39ff82ae2b9708ca9, CC BY 4.0) | CC BY 4.0 (attribution as in `assets-pass2.md`) |
| `stage-ground.glb` (the test-stage ground) | `examples/strange-attractor/hero/proof_stage.py` | Poly Haven `red_sand` (CC0) | CC0 |
| `alien-sketchfab-v1.glb` (+ `.blend`): the Sketchfab alien rigged (22 bones, bone heat), the Gray's clip `mixamo.com` retargeted | `examples/strange-attractor/hero/rig_sketchfab_alien.py` | Graphetta's alien (CC BY 4.0) + the Gray's clip (ShaxerTakkuY, CC BY 4.0) | CC BY 4.0 (both attributions) |
| `campfire/campfire-v1.glb` (+ `.blend`): logs, char, glowing cracks, ash, coals, bone-animated flame sheets | `examples/strange-attractor/hero/build_campfire_v1.py` | Poly Haven `dead_tree_trunk` (CC0); the pit scan only for ray casts | CC0 |
| `ufo/ufo-v1.glb` (+ `.blend`): reworked materials, rim cells, nav lights, underglow | `examples/strange-attractor/hero/build_ufo_v1.py` | BrunoLatour's "UFO" (CC BY 4.0) | CC BY 4.0 (attribution in `assets-pass2.md`) |
| `sasquatch/sasquatch-{a,b,c}-v1.glb` (+ `.blend`): the H1 fur hybrid on the current individuals | `examples/strange-attractor/hero/build_sasquatch_v1.py` (+ `fur_proof.py`) | `build/sasquatch-{a,b,c}.glb` (Mat Rex's "Bigfoot", CC BY 4.0) | CC BY 4.0 |

