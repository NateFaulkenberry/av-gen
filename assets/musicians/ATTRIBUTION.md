# Astronaut musicians (prototype)

Four GLBs built by `tools/make_astronaut_musicians.py` from source files the owner supplied in
`~/Desktop/musician_assets/` on 2026-09-28. **Neither the sources nor the GLBs are in this repository.**

> **Licences: partly unestablished. NOT for distribution until the owner clears them.**
> - The repository is public.
> - Do not commit, cache or upload the GLBs, the validation .blend, or anything else derived from the sources.
> - The GLBs are ignored by `/assets/musicians/*.glb` in `.gitignore`.

Regenerate them with (about 8 s):

```
/Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup \
    --python tools/make_astronaut_musicians.py -- \
    --src ~/Desktop/musician_assets --out assets/musicians --blend <validation.blend>
```

## Sources

The origins come from the macOS "where from" metadata on the owner's downloads (the zips on the Desktop and the
two FBX files).

### Rigged Low Poly Astronaut v1.0

- **Files:** `Untitled.blend` and `Untitled.fbx` (FBX binary, written by Blender 5.2.0).
- **Origin:** itch.io, <https://mrlentereng.itch.io/rigged-low-poly-astronaut>, by MrLentereng. The page also
  credits "Original creator: ThunderWare Games".
- **Licence:** the page states **CC0 1.0 Universal**, commercial use allowed and attribution not required. This
  is the uploader's claim; given the "original creator" credit, the owner may want to confirm it.
- **Mesh:** one skinned mesh of 2,019 vertices, 2,018 faces and 4,038 triangles.
- **Skeleton:** 53 bones with Unreal-mannequin naming, resting in a T-pose facing -Y.
- **Weights:** up to 10 per vertex. The export keeps the strongest 4 and renormalises them, because AV Gen reads
  `JOINTS_0`/`WEIGHTS_0` only.
- **Texture:**
  - The one material samples `//game87/resurs/Astronaut/AstronautTexture.png`, a path on the author's machine.
  - The published 422 kB zip (the owner's copy is byte-for-byte the listed size) **does not contain that
    texture**, although the page says "texture included".
  - The UVs are a full painted-texture unwrap, so the texture cannot be reconstructed.
- **Conversion:**
  - The astronaut is stood on z = 0 and its object transforms are baked into the data.
  - The material is replaced by a flat off-white suit plus a dark visor, assigned to the visor's own UV island
    (30 faces).
  - **The helmet is re-weighted.** The supplied weights are automatic and bleed onto the shell: each of its 349
    vertices is about 37 % head, 24 % neck_01, 11-12 % each upper arm and 8 % each clavicle. So the helmet
    dented whenever the arms moved.
    - The shell is now 100 % head.
    - Three rings of suit under the rim (56 vertices) blend from head to chest.
    - The clips keep half of their neck and head rotation away from the chest, because the helmet sits directly
      on the shoulders.
    - `--no-helmet-fix` rebuilds the rig as supplied, for comparison.
  - The skeleton and the mesh geometry are otherwise unchanged.

### Piano Playing / Playing Drums

- **Files:** `Piano Playing.fbx` and `Playing Drums.fbx`.
- **Origin:** mixamo.com (Adobe).
- **Licence:** the Mixamo terms. Animations may be used in projects, but not redistributed as standalone files.
- **Content:** "Without Skin" motion files with the 65-bone `mixamorig:` skeleton, in centimetres, resting in a
  T-pose, at 30 fps.
  - Piano: 500 frames, 16.633 s.
  - Drums: 142 frames, 4.700 s.
  - In both files the last frame equals the first, so each clip loops cleanly.
- **Conversion:** each clip is retargeted to the astronaut's skeleton and baked as "Piano" and "Drums". The FBX
  files themselves are not modified.

### Basic_Keyboard

- **Files:** `BasicKeyboard.obj`, `.mtl` and `.fbx`, plus `BasicKeyboard_IMG.png`. The PNG is a 1000x1000 preview
  render, not a texture.
- **Origin:** CGTrader. The download does not record which model page it came from.
- **Licence:** not established.
- **Mesh:** 310 vertices and 484 triangles, 20 white keys, three materials (all 0.64 grey), no UVs, no textures.
- **Conversion:**
  - The OBJ is used.
  - It is scaled 0.1384. That makes its keys about 2x real width, so the key bed spans the piano clip's 0.95 m of
    hand travel.
  - It is turned to face the player.
  - It is recoloured from the preview render: black case, white keys, black keys.

### Folding_Stand

- **Files:** `OBJ/Folding_Stand.obj` and `.mtl` (exported by LightWave), plus `LWO/Folding_Stand.lwo`. The LWO
  file is not used.
- **Origin:** CGTrader.
- **Licence:** not established.
- **Mesh:** 23,363 vertices and 46,368 triangles in 38 loose parts; four materials (Rubber, Painted_Metal,
  Plastic, Chrome); no UVs.
- **Conversion:**
  - It is scaled 1.3.
  - It is raised to hold the keyboard at 0.77 m by turning each leg assembly about the pivot bolt, as the real
    stand adjusts.
  - The materials are recoloured to dark painted metal, black plastic and rubber, and chrome.

### lowpoly-drums

- **Files:** `drums.blend`, `.obj`, `.mtl` and `.fbx`.
- **Origin:** CGTrader.
- **Licence:** not established.
- **Content:** seven mesh objects with 3,112 triangles in total: kick, two rack toms, snare, hi-hat, ride, floor
  tom and throne. There are six flat-colour materials and no textures. The file also holds a camera and two
  lights, which are not used. There are no pedals.
- **Conversion:**
  - The .blend is used.
  - The kit is scaled 0.1337, so the kick is 22" and the snare 14.5", and turned to face the drummer.
  - The pieces are repositioned to the performance.
  - Stand heights are telescoped and the snare is tilted 7.4°.
  - The throne is scaled 1.2x.
  - The materials are renamed.

### Added here (not from any pack)

- The piano bench: a box cushion on four legs.
- Two drumsticks, 0.41 m long and skinned to the hands.

## What ships

| file | contents |
|---|---|
| `astronaut_keys.glb` | the astronaut (53-joint skin, suit and visor materials) plus the clip `Piano` |
| `keyboard_set.glb` | the keyboard, the stand and the bench, in the pianist's frame |
| `astronaut_drums.glb` | the astronaut plus drumsticks skinned to its hands, plus the clip `Drums` |
| `drum_kit.glb` | seven drum pieces in the drummer's frame |

Each character and its props share one frame: place both nodes with the same transform.
