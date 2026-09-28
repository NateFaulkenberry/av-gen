# Astronaut musician prototype: progress notes

Branch `proto/astronaut-musicians`, worktree `../av-gen-astro`, started from main `77ea4247`. The brief is
`00-brief.md` (the owner's words; it governs). GV3 is not touched by this work.

## Resume here

- **Done:**
  - The Blender pipeline and its validation.
  - All four GLBs, built and structurally checked.
  - The AV Gen demo files: `examples/musicians/astronaut-musicians.{json,scene.json}`.
- **In progress:** rendering the demo in AV Gen (stills and a video) and confirming that the engine plays both clips.
- **Still to do:**
  - the report (`REPORT.md`);
  - the review folder `~/Desktop/av-gen-review/21-astronaut-musicians/`;
  - registering the demo in `examples/index.json`.
- **To regenerate the assets** (they are not in git, and never may be; see "Licences" below):

  ```
  /Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup \
      --python tools/make_astronaut_musicians.py -- \
      --src ~/Desktop/musician_assets --out assets/musicians \
      --blend ~/Desktop/av-gen-review/21-astronaut-musicians/blender/astronaut_musicians_validation.blend \
      --report /tmp/astronaut-report.json
  ```

  The run takes about 8 s. It writes these files:
  - `assets/musicians/astronaut_keys.glb`
  - `assets/musicians/keyboard_set.glb`
  - `assets/musicians/astronaut_drums.glb`
  - `assets/musicians/drum_kit.glb`
  - the validation .blend, which holds the layout, both Mixamo sources for comparison, and loop modifiers for playback in Blender
  - a JSON report of every measurement
- **Keep the .blend out of the repository.** The ignore rule `/assets/musicians/*.blend` covers it only if it is
  written under `assets/musicians`.

## What the sources are

The sources are in `~/Desktop/musician_assets/`. Their download origins come from the macOS "where from" metadata
on the zips on the Desktop.

| Asset | Origin | Licence |
|---|---|---|
| Astronaut (`Untitled.blend`/`.fbx`) | itch.io, MrLentereng, "Rigged Low Poly Astronaut" | The page says **CC0 1.0** ("Original creator: ThunderWare Games"). The page says "texture included", but the 422 kB zip holds no texture. |
| Keyboard | CGTrader | Not established |
| Folding stand | CGTrader | Not established |
| Drums | CGTrader | Not established |
| `Piano Playing.fbx`, `Playing Drums.fbx` | mixamo.com | Adobe Mixamo terms |

The CGTrader downloads do not record which model page they came from.

## The pipeline (`tools/make_astronaut_musicians.py`)

Every step is a function. The script runs headless through `main()`, or you can `exec()` it in a live Blender
session and call the steps one at a time, which is how it was validated.

1. **`load_astronaut`** appends `Human.rig` and `Cube` from the .blend.
   - It stands the astronaut on z = 0. The rig ships at z = -0.92, and the soles sit at -1.078.
   - It bakes the object transforms into the data through `Mesh.transform` and `Armature.transform`. It does not
     use `transform_apply`, because that operator needs an operator context.
2. **`fix_astronaut_materials`** replaces the material that points at the missing texture.
   - The suit becomes flat off-white.
   - The visor becomes dark and glossy. The visor faces are found by casting a ray at the helmet front and
     flood-filling that face's UV island. That gives 30 faces, and no face is picked by hand.
3. **`import_mixamo`** imports each FBX as is. Both are "without skin" motion files: 65 `mixamorig:` bones in
   centimetres, a T-pose rest, and 30 fps.
4. **`retarget`** retargets each clip in world space from the rest delta.
   - The rotation is `q_dst_world = q_src_world * q_src_rest^-1 * q_dst_rest`.
   - The pelvis also gets the hips' translation delta, scaled by the ratio of hip heights (0.989).
   - The result is baked into a new action on the astronaut. The Mixamo actions are left untouched.
   - The bone map (`BONE_MAP`) covers 52 of the 53 bones. `Root` stays at rest. The 13 leftover Mixamo bones are
     all end bones.
   - It takes these options:
     - `lift`: grounding. `grounded_retarget` iterates until the lowest foot vertex over the whole clip is at z = 0.
     - `hand_height_ik`, `hand_offsets` and `hand_shift`: a two-bone arm solve that holds each wrist at the
       source's wrist height plus a per-hand offset. Only the upper arm and forearm swing; the world rotations of
       the hand and fingers are kept.
     - `leg_turnout`: a constant rotation of the whole leg about the vertical axis through the hip. The foot stays
       on the floor.
5. **Props:**
   - `load_keyboard`: scaled 0.1384, turned to face the player, and recoloured from the pack's own preview render.
   - `load_stand`: scaled 1.3, then raised the way a real X-stand is, by turning each leg assembly about the pivot
     bolt.
   - `make_bench`: our own box geometry.
   - `load_drum_kit`: scaled 0.1337, so the kick is 22", then turned so the drummer faces -Y.
   - `place_drum_kit`: places each piece and adjusts it with `telescope` (stand heights) and `tilt_parts` (the
     snare basket).
   - `make_drumsticks`: two sticks skinned 100 % to `hand_l`/`hand_r`. AV Gen flattens bone-parented props, so
     parenting would not work.
6. **Validation:**
   - `audit_intersections` runs a triangle-level `BVHTree.overlap` between the evaluated (skinned) character and
     each prop, frame by frame.
   - `fingertip_heights`, `stick_tip_track`, `strikes` and `seat_contact` are the measurements the placements
     come from.
7. **`export_set`** opens the build and keeps one set. It exports with the farm pack's glTF settings: Y-up, no
   modifier apply, ACTIONS mode, forced sampling, slide-to-zero, and 4 influences.

## Measured facts and decisions

- **The rig:**
  - The astronaut uses UE4-mannequin names (`pelvis`, `spine_01..03`, `clavicle_l`, `upperarm_l`, `thigh_l`,
    `ball_l`, three bones per finger) and rests in a T-pose facing -Y.
  - It is **not** Mixamo-compatible:
    - Assigning the Mixamo action binds zero channels.
    - A name-mapped copy of local rotations mangles the body, because the bone rolls differ. The evidence is
      `renders/01-*`.
  - The world-space delta retarget works. After it, each limb segment keeps a constant 0.8-2.6° offset from the
    source; that is the difference between the two rest poses.
- **Loops:** both clips' last frame equals their first (0° on every bone). They loop cleanly at 499/30 s
  (16.633 s) and 141/30 s (4.700 s).
- **Pianist:**
  - Lift: 4.3 cm, because the retargeted feet sink.
  - The astronaut's shoulders sit about 12 cm further forward of its spine than the Mixamo skeleton's. So when the
    player leans into a right-hand run, the left wrist dropped 6 cm, and the fingers went up to 9.5 cm into the
    keys. `hand_height_ik` fixes this.
  - The gloves also left the right fingertips 3.7 cm above the left. `calibrate_hands` offsets each hand so both
    medians sit on one key plane.
  - Result: left fingers go at most 1.7 cm into the keys, and 0 frames exceed 2 cm (it was 117 of 250).
  - The piano clip's hands travel about 0.95 m across the keys, while the keyboard model has 20 white keys.
    Scaling the model to fit that travel makes the keys about 2x real width.
  - Stand: at 1.0x the X foot bar was under the left boot and the arms were at the knees. At 1.3x the arms land
    outside the knees. At y = -0.63 the legs clear the left calf; dead centre, y = -0.576, touches it in 16 frames.
- **Drummer:**
  - Lift: 3.4 cm.
  - The stick grip is not defined by the low-poly glove, so each stick's hand-local direction is *solved* from
    the clip's own strike frames:
    - Left: at the snare backbeats (frames 23/58/93/129), it points 20° inward at -8° pitch.
    - Right: at the hi-hat on-beats, it points 35° outward at -10° pitch.
  - At the source's snare position, the suit's 0.28 m-wide right knee filled the space between the knees. Three
    changes were chosen by a joint search over 24 configurations:
    - **Right leg turned out 40°**.
    - **Left wrist raised 8 cm**. Without this the left stick cut through the suit's left thigh in 60 of 142
      frames.
    - The snare raised to 0.73 m and tilted 7.4°.
  - Result: the body touches no drum except the throne cushion (the edge of the right thigh). The sticks pass
    slightly into the hi-hat (8 frames) and the snare (3 frames), only at the moment of impact.
  - The kick sits 0.21 m past the right toe and has no pedal; the supplied kit has none.
  - The right stick's off-beats stop 7-19 cm short of the hi-hat. That is the source clip: it air-taps the
    off-beats.

## Licences and the repository

The repository is public. Never commit, cache or upload the source files, the GLBs or the .blend.
`.gitignore` has `/assets/musicians/*.glb` and `*.blend`. This machine's shared `info/exclude` also ignores
`assets/`, so the tracked `assets/musicians/ATTRIBUTION.md` needs `git add -f`. Check `git status` before every
commit.

## Commits

1. `1efbbc2b`: the brief.
