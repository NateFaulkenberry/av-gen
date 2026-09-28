# Astronaut musician prototype: progress notes

Branch `proto/astronaut-musicians`, worktree `../av-gen-astro`, started from main `77ea4247`. The brief is
`00-brief.md` (the owner's words; it governs). GV3 is not touched by this work.

## Resume here

- **Status: complete. Stopped at the brief's stop condition** (the standalone demo works and has been visually
  validated). The owner decides whether the astronauts go into GV3. Nothing here touches GV3.
- **Done:**
  - the Blender pipeline, its validation, and the four GLBs;
  - the AV Gen demo, `examples/musicians/astronaut-musicians.{json,scene.json}`, registered in `examples/index.json`
    as "Astronaut Musicians - Prototype";
  - the renders and videos in the review folder;
  - **the helmet fix** (owner feedback, 2026-09-28: the helmets crumpled). See "The helmet" below; the build
    applies it by default.
- **Where the report is:** the compatibility report and the recommendation went to the coordinator in the agent's
  handback. The facts behind them are below. `assets/musicians/ATTRIBUTION.md` is the asset report.
- **After a merge, the owner's checkout has no GLBs.** Run the command below in that checkout.
- **To regenerate the assets** (they are not in git, and never may be; see "Licences" below):

  ```
  /Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup \
      --python tools/make_astronaut_musicians.py -- \
      --src ~/Desktop/musician_assets --out assets/musicians \
      --blend ~/Desktop/av-gen-review/21-astronaut-musicians/blender/astronaut_musicians_validation_rigid_helmet.blend \
      --report /tmp/astronaut-report.json
  ```

  The run takes about 15 s. Add `--no-helmet-fix` to rebuild the rig exactly as supplied, for comparison.
  It writes these files:
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
      `renders/16-naive-retarget-fails-f120.png`.
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

## How it was verified

- **Pianist contact on the final build:**

  | | fingertips into the keys: worst | frames deeper than 2 cm |
  |---|---|---|
  | left fingers | 1.7 cm | 0 of 250 |
  | right fingers | 2.6 cm (frame 11, the flourish) | 8 |

  The left palm heel goes deeper than 2 cm in 13 frames, at the keyboard's front edge.
- **Pianist, other props:** the stand has no contact. The bench is at most a 1.3 cm sink with a median gap of
  0.9 cm.
- **Drummer:** the body touches no drum except the throne cushion (thigh edge). The sticks touch the hi-hat in
  8 frames and the snare in 3, at impact only.
- **AV Gen `build/release/src/avgen`** (built in this worktree, run under `tools/gpu-lock.sh`) loads both GLBs as
  one rig each: 54 joints (the 53 bones plus the armature node) and one clip, with 0 warnings.
  - Stills and three `--render` videos: binary exit 0, and no warn or error lines in the log.
  - An AV Gen frame and a Blender frame at the same instant, from the same camera, match pose for pose.
  - Both loops are seamless in the rendered videos.
- **`avgen_tests`** (built in this worktree), all passing with binary exit 0:
  - "Every example in the index exists and loads"
  - "The scene parser's key list matches what it reads"
  - "no project overrides its scene's volumetric noise back to zero"

  The index case also passes with the GLBs moved away (each node is skipped with a warning).
- **`tools/check_project_integrity.py`:** 20 problems, all pre-existing stale fingerprints under `examples/world`.
  None names this demo.
- **The pipeline is deterministic.** Two runs give byte-identical GLBs and a byte-identical `build_report.json`
  (the set-order tie-breaks in the audit are sorted).

## Review files (`~/Desktop/av-gen-review/21-astronaut-musicians/`)

- `00-brief.md`: the brief.
- `01`-`03`: AV Gen videos: both performers (16.7 s), the keyboard astronaut close (16.7 s), the drummer close
  (9.4 s).
- `renders/04`-`11`: AV Gen stills and a sheet of representative frames.
- `renders/12`: AV Gen against Blender at the same instant.
- `renders/13`, `14`: the loop seams.
- `renders/15`: the astronaut as supplied (magenta: missing texture) against as adjusted.
- `renders/16`: the naive retarget failing.
- `renders/17`: the Blender validation scene.
- `blender/`: the validation .blend and `build_report.json`.

## The helmet (owner feedback, 2026-09-28)

**What the owner saw:** the helmets crumple as the head turns and tilts, mostly on the drummer.

**The cause, measured.** It is the supplied rig's automatic weights, not the clips and not the export.
- Every one of the helmet shell's 349 vertices (the part above z = 1.58 m, connected to the crown) has six
  influences: about 37 % `head`, 24 % `neck_01`, 11-12 % on EACH upper arm, and about 8 % on each clavicle.
- The collar under it is mostly clavicles and upper arms, with stray `thigh_r`.
- So any arm or shoulder movement drags the shell. The drum clip's arms move asymmetrically all the time (the
  right arm crosses over to the hi-hat), which is why the drummer shows it most.
- My own linear-blend reproduction matches Blender to 3.4e-7 m, and the 4-influence glTF truncation measures the
  same. The export is not the cause.
- The clips also move the head a long way against the chest: up to 48.7° (drums) and 49.6° (piano), with a
  median of 24°. On the drummer that is a nod of -24° to +46°; on the pianist, turns of up to 47°.

**The fix** (`fix_helmet_weights`, plus `NECK_STIFFNESS` in the retarget):
- The helmet is 100 % `head`.
- There is no neck: the helmet's rim joins the shoulders and upper chest directly. So three rings under the rim
  blend from head to chest:
  - Ring d moves a fraction 1 - d/4 of the way to {head, neck_01} (0.75, 0.5 and 0.25).
  - Ring 1 blends in full; rings 2 and 3 fade out between 0.19 m and 0.26 m from the neck axis.
  - Each ring keeps the rest of its own weights, arms included.
- The retarget keeps 0.5 of the neck's and head's rotation away from the chest (same axis and timing). Arms,
  contacts and prop placements do not depend on the neck, and did not move.

**Numbers, over every frame of both clips** (`helmet_audit`, in the build report):

| | drummer before → after | pianist before → after |
|---|---|---|
| helmet max deviation from rigid | 7.66 → **0.00 cm** | 8.90 → **0.00 cm** |
| frames over 1 cm | 142/142 → 0 | 500/500 → 0 |
| head vs chest, max | 48.7° → 24.3° | 49.6° → 24.8° |
| helmet-body overlaps | 0 → 0 | 0 → 0 |
| suit edge strain p50 / p90 | 19.0 / 42.0 → 19.6 / 45.3 % | 13.1 / 36.4 → 13.1 / 36.6 % |
| suit edge strain p99 / max | 70 / 93 → 89 / 205 % | 70 / 91 → 108 / 206 % |

The p99 and max rise comes from 6 edges (drummer) and 9 edges (pianist) out of 759. They are short (1.8-4.3 cm),
at the sides of the helmet just under the rim. Rendered close up at their worst frames (drummer 41 and 81,
pianist 103 and 303), they show no tear or spike, while the before renders show the bent ear pods and the warped
shell.

**What was tried and rejected:**
- Two rings: the collar tore at about 300 % strain.
- Five or seven rings with the arm weights stripped: the shoulders stretched into flat wings.
- A radial fade that also weakened ring 1: the rim band took 280-360 %.
- Neck stiffness 0.35 against 0.5: they look alike; 0.5 keeps more of the performance.
- A helmet cut free of the collar as a separate rigid part: not tried. It would open a visible hole at the neck
  whenever the helmet tilts.

**Unrelated, found on the way:** the drummer's 40° right-leg turnout strains a few 0.5 cm edges at the crotch by
up to 477 %. They are hidden between the thighs, behind the snare and throne, at the worst frame (108).

**Files** (in `~/Desktop/av-gen-review/21-astronaut-musicians/blender/`):
- `astronaut_musicians_validation_rigid_helmet.blend` and `build_report_rigid_helmet.json`: the current build.
- `*_before_helmet_fix.*`: the build the owner reviewed.
- `helmet_investigation.blend` and `helmet_fix_check.blend`: the investigation's work files.

## Licences and the repository

The repository is public. Never commit, cache or upload the source files, the GLBs or the .blend.
`.gitignore` has `/assets/musicians/*.glb` and `*.blend`. This machine's shared `info/exclude` also ignores
`assets/`, so the tracked `assets/musicians/ATTRIBUTION.md` needs `git add -f`. Check `git status` before every
commit.

## Commits

1. `1efbbc2b`: the brief.
2. `cff51cb0`: the pipeline, the demo, ATTRIBUTION.md and these notes.
3. The next commit: the index entry, the reproducibility fixes to the tool, and these notes updated.
