# Hero assets, after H1: the Sketchfab-alien compare, H4 campfire, H5 UFO, H3 Sasquatches (v1)

The owner approved H2-H5 on 2026-11-03. The coordinator ran them in this order:
1. the Sketchfab-alien compare (the owner's download);
2. H4 (the campfire) and H5 (the UFO), which do not depend on the downloads;
3. H3 (the Sasquatches; the owner rejected the bigfoot download, so H3 follows the H1 plan);
4. H2 (the Gray) waits for the owner's verdict on the alien.

All outputs are in `assets/strange-attractor/hero/` (gitignored); each has a `.blend` beside its `.glb`. Sources and
licences are recorded in `hero-assets.md`. Media is in `~/Desktop/av-gen-review/101-strange-attractor/hero/`.

## How the AV Gen renders were made (and why that way)

- **The binary.** A detached worktree `../av-gen-sa-compare` sits at the art agent's checkpoint, 7fe07cae (ADR-1904..1907
  and the M3e night lighting), built in release.
- **The scenes.** Copies of `examples/strange-attractor/*` go into `examples/sa-compare-<variant>/`. They are
  untracked and never committed, and only the asset path of the node under test changes. The real scene is never
  touched.
- **The owner's reference camera.** It uses the art agent's own `camref` recipe:
  - night look from 0;
  - portals off;
  - `attractorWorld.camera: "scene"`;
  - pos [67.956, 4.16, 1356.186], target [74.965, 3.06, 1349.764], fov 58;
  - a 110-112 s run-up, last frame.
- **The journey clip.** 101.5-121.5 s, 1080p30, supersample 2, from the project with Feline Footwear.
- **GPU.** Every run went through `tools/gpu-lock.sh`, with `AVGEN_GPU_LOCK_TIMEOUT` raised. The first attempt timed
  out (exit 75) behind the SA agent's 70-minute full render. The final runs exited 0 with 0 GPU errors.

## The Sketchfab alien (Graphetta, CC BY 4.0)

**What it ships with:**
- 79k tris of clean quad sculpt topology;
- a separate eye mesh (black, roughness 0.05, glossy);
- 4K base colour, metal/roughness (metal 0) and normal maps;
- `KHR_materials_specular`;
- no rig, no animation, no AO map;
- the 4K displacement shown on Sketchfab is not in the glb;
- a static A-pose, pink-brown "mummy" skin.

**What I did** (`examples/strange-attractor/hero/rig_sketchfab_alien.py` → `alien-sketchfab-v1.glb`):
- Scaled it to the Gray's glb height (2.888 units), so the scene's node (position, rotation 54.7°, scale 0.68 =
  1.96 m) is unchanged and only the asset path changes.
- Fitted a clean 22-bone biped to measured landmarks (crotch, leg and arm centrelines, neck, skull) and skinned it with
  bone heat (0 unweighted verts). The eyes ride the head.
- Retargeted the Gray's own clip `mixamo.com` (4 s) in world space:
  - the hips take the source's rotation relative to its rest, plus its translation;
  - every other bone is turned, parent first, to the source bone's world direction;
  - then the hips are re-grounded and each leg is solved with two-bone IK onto its planted foot.
  - Feet drift over the clip: 0.4 mm. The clip keeps its name, so the scene's `animation.state` works as is.
- **This is the current Gray's creeping Mixamo idle, not the talking/gesturing idle.** That one is H2's work and
  does not exist yet. The new rig is the one H2 would animate: finger and jaw bones get added then.

**What it loses in AV Gen** (the H1 gaps):
- **G4:** no subsurface scattering, so the skin is opaque; no wrap lighting.
- **`KHR_materials_specular` ignored:** the default F0 is used instead; minor.
- **No displacement:** not in the glb anyway; its normal map carries the detail.
- **G1, G2, G5:** not applicable (no alpha cards, no fur, no anisotropic metal).
- **G3:** no morph targets, so the face talks with bones.

## H4: the campfire (`campfire/campfire-v1.glb`)

Built by `build_campfire_v1.py` in the stone pit's own frame: place it with the pit's transform (position as
`campfire-logs`, rotation [0, 20, 0], scale 0.85).

- **Logs.**
  - Poly Haven `dead_tree_trunk` (CC0), decimated to about 12k tris per log, `rotation_mode` set to XYZ (the H1
    teepee bug).
  - Five logs lean into a collapsing teepee; two lie fallen across the coals; three sticks of kindling.
  - One 2048² baked atlas (albedo, roughness, normal, emissive) from a world-space shader: bark where it is cold;
    charred black with noise-warped alligator cracks near the heart; ash grey on the char's upper faces; a gated
    subset of cracks glowing orange to yellow toward the core.
- **Bed.** An ash patch conforming to the pit's floor (ray-cast), with a masked edge and an ember-glow texture, plus
  about 260 coals in four heats.
- **Flames.**
  - 12 tongues, each three crossed emissive BLEND sheets with explicit back faces.
  - The flame atlas is four variants: a white-hot root, yellow, orange, deep red at the edges, wisps breaking off.
  - Each tongue rides a four-bone chain. The `burn` clip is a 4 s seamless loop of whole-cycle sines (height pulses,
    licks, sway), so it is deterministic and seek-safe.
- **Integration (for the art agent):**
  - Use the node's `animation: {state: "burn"}`.
  - Turn the scene's `campfire-flames` and `campfire-core` particles off (`spawnRate` 0 in my test copies).
  - Keep `campfire-embers` and `campfire-smoke`.
  - The fire's light stays theirs.
- **Triangles:** logs 122k, coals 5.2k, flames 1.7k.

## H5: the UFO (`ufo/ufo-v1.glb`)

Built by `build_ufo_v1.py`. It is the same glTF frame and units as `build/ufo-craft.glb`, so the scene node (scale
0.018, emissiveBoost) takes it unchanged.

**Why it looked like a dark toy:**
1. Every surface is near-mirror metal (roughness 0.30 ± 0.02), which reflects the black night sky.
2. Its occlusion map is very dark (means 0.17-0.46), and AV Gen multiplies IBL by it.

**The fixes:**
- **Roughness:** 0.34-0.62 per panel, plus a brushed streak (anisotropy is unsupported).
- **Occlusion:** remapped to 0.55-1.
- **Grime:** in the recesses.
- **Emissives:** the authored green recoloured. The window band became a dim warm interior; the underside lights
  became cool white. The green spots in the albedo became dark lenses.
- **Added:** 48 rim light cells, four soft white navigation lights and a thin underbody ring glow.
- **Format:** JPEG textures, 75 MB → 32 MB.
- **Not done:** glass. The window band is not a separate material, so making it transmissive needs a mesh split.

## H3: the Sasquatches (`sasquatch/sasquatch-{a,b,c}-v1.glb`)

Built by `build_sasquatch_v1.py` with `fur_proof.py`'s functions: the H1 p6 hybrid on the current individuals.

- **Kept:** bodies, rigs, base poses and the 24 s `idle` clips are kept exactly.
- **Per creature:**
  - the body re-tinted;
  - a baked fur albedo and normal (the silverback greys over the crown, shoulders and back, but not the face);
  - 8 clump shells (none on the face);
  - 8-11k soft-rooted lock cards with explicit back faces;
  - the silverback gets 6k more grey cards.
- **The proof's defects, fixed:**
  - Each card is weighted wholly by its root face, so no card spans a hand and a hip.
  - The face is bare (no cards; no shell length).
- **Not done:**
  - The silhouettes are not opened. The crouch and sit poses come from `pass2/build_sasquatch.py` (the art agent's
    builder).
  - No re-UV or groom bake yet. The 3-D noise bake works on the existing fragmented UVs.

## The renders: what they show

All media is in `~/Desktop/av-gen-review/101-strange-attractor/hero/`. Every avgen run exited 0 with 0 GPU errors.

**The Sketchfab alien vs the Gray** (`02-sketchfab-alien-compare/`):
- On the journey camera (101.5-121.5 s) the camp is small in frame and the two read alike.
- From the owner's reference camera and in the close-ups, the Sketchfab alien reads as organic skin, with wrinkles,
  a belly, knuckles, a real face and glossy eyes, where the Gray reads as smooth plastic.
- Visible defects in my rig (bone heat on a first fit):
  - a dark crease at the shoulder;
  - the hands splay, because there are no finger bones.
- It is pink-brown, not grey. A re-tint toward grey would be a texture edit, if the owner wants a classic Gray.

**Campfire** (`01-campfire/`):
- Up close, the logs, char, coals and flame tongues read as a physical fire.
- From the reference camera it reads smaller and dimmer than the old particle glow, which supplied the "light in the
  air".
- The scene's point light sits about 1 m above the pit and bleaches the nearby logs under AgX.
- For the art agent: keep a reduced `campfire-core` glow (or raise the flame emission at integration), and sit the
  light above the flame tips.
- The flames still go slightly salmon at their brightest. That is a tone-map interaction (AgX on saturated orange).

**UFO** (`03-ufo/`):
- The brushed hull shows panels and streaks under the night light.
- The rim cells, navigation lights and underglow read as individual lights. Their strengths are set low (0.9-3)
  because the scene's `emissiveBoost` 4 multiplies them.
- The hull still reads dark navy: that is the night light's colour on a metal, and it is the art agent's.

**Sasquatches** (`04-sasquatch/`, `05-combined/`):
- The clearest gain: three recognisable furry creatures with heads, shoulders, arms and individual colours (a tan
  adult, a grey silverback, auburn), where before they were speckled rocks.
- A reads lighter (tan) than the intended dark brown under the fire and AgX. The base colour can be darkened.

## H2: which alien to animate (the comparison, then the decision)

The owner said either alien works, so the choice came down to which is cheaper to animate.

| | Current Gray (`gray-m3e.glb`) | Sketchfab alien (Graphetta) |
|---|---|---|
| Finger bones | **none**: Mixamo, 26 joints; the modelled fingers are skinned to one hand bone each | none at first; added by script |
| Jaw | none | none at first; added by script |
| Bind and rest pose | a Sketchfab FBX round trip: the rest skeleton does not match the mesh (bone tails are guesses, the root sits under a 0.0245 scale); a bone added in edit mode has to be fitted against posed bones | my own clean rig in the mesh's own A-pose, so bones go in at measured landmarks |
| Mesh | a decimated triangle soup: bone heat for new finger and jaw bones bleeds, and there is no edge flow at the mouth | clean quad sculpt: heat weighting works, and the mouth has loops |
| Known defects | the creeping clip; smooth plastic surface | shoulder crease; no fingers |
| Talk/gesture clip | none either way; a new clip is authored for both | same |

**Decision: the Sketchfab alien, tinted grey.**
- Neither rig has fingers or a jaw, so both need new bones.
- Only the Sketchfab rig is clean and mine, so bones can be added and re-weighted without fighting a broken bind pose.
- The mesh is better too.
- No downloaded motion was needed: Mixamo needs a login, and a procedural clip is deterministic and seek-safe by
  construction.

**What was built** (`rig_alien_h2.py` → `assets/strange-attractor/hero/alien-grey-h2.glb` + `.blend`):
- **Grey tint:** a texture edit under a new image name. Luminance is mapped to a cool grey and 10% of the original
  hue variation is kept. The source download is untouched.
- **Shoulder crease** (the real cause was found by preview, and fixed):
  - glTF split the vertices at UV seams. The split copies took different heat weights, and the seams opened as the
    arms lowered (dark rings at the shoulders and upper arms). The vertices are now welded before rigging; UVs stay
    per loop.
  - The shoulder weights are Laplacian-smoothed.
  - The clavicle takes a share of every arm move.
- **Fingers:**
  - Four two-segment fingers per hand (thumb, index, middle, ring), found by clustering the hand's distal vertices.
  - They curl toward the palm; the palm normal comes from the hand's flatness.
- **Jaw:** the hinge sits behind the mouth and the tip at the chin, both from the face's midline profile. The lower
  face is weighted to it with a soft edge.
- **The `talk` clip:**
  - An 8 s loop: every sine has a whole number of cycles and every beat eases back by 8 s.
  - Neutral upright stance, breathing.
  - The right hand points across the fire at 1.1-3.6 s (index extended, the other fingers curled), with two
    emphatic jabs.
  - The left hand opens in a presenting gesture at 4.3-7.0 s, with two small beats.
  - The head sweeps across the group and nods on the phrases.
  - The jaw talks in two phrases at 4.5 syllables a second.
  - The legs are solved to planted feet (drift 1.2 mm in the scene).
  - The pointing tip stays clear of the body.
- **Scene integration:** the same node (position, rotation 54.7°, scale 0.68) with
  `animation: {"state": "talk", "speed": 1.0}`. The alien already faces the Sasquatches, with the fire between them.

## Owner note: the auburn Sasquatch clipping its stone

The stone is `seat-rock` (`ph-namaqualand_boulder_05`).

**The check:** `check_contacts.py` runs over the whole 24 s idle, 193 frames (every 3rd), and tests every vertex of
the body, shells and fur cards against the stone (inside test by nearest-face normal) and against the ground.

**Before:** the body sank up to 19 cm into the stone in every frame, and the shells up to 21 cm.
- Cause: the stone's top is 0.46 m above the ground, but the sitting pose's seat (the hips-weighted skin) is at
  0.32 m (0.319-0.325 over the loop).
- The stone was also 0.36 m from the creature, under its thighs and calves.

**Two fixes failed by eye before this one:**
1. Moving the stone back and lowering it cleared the numbers, but the creature squatted in mid-air with the stone
   behind it.
2. A stool-sized stone under the original pose touched the seat, but the pose is a deep squat: the seat is 0.32 m up,
   the knees are high, and the heels sit under the buttocks. It still read as squatting.

**The fix: a seated pose plus a stool-sized stone.**
- **Pose.**
  - `build_sasquatch_base.py`: a copy of `pass2/build_sasquatch.py` whose sitting hips can be set
    (`SIT_HIPS="0.24,-0.14"`, in units of the height; the original is 0.16, -0.23).
  - The hips come up and back: the seat is 0.48 m up and spans 0.42-0.79 m behind the feet, the thighs go forward
    and down to the knees, and the feet stay planted. IK: foot drift 0 mm.
  - The idle is otherwise the original 24 s loop.
  - Base: `hero/sasquatch/sasquatch-c-base-sit2.glb`. With fur: `hero/sasquatch/sasquatch-c-v1.glb` (rebuilt).
  - The buttocks and the backs of the thighs carry no shell volume and no cards (`build_sasquatch_v1.py`'s `seat`
    patch).
- **Stone (a scene change, for the art agent).** The boulder's long axis runs front-to-back here, so it is scaled
  down along that axis. `seat-rock`:
  - position [75.081, 1.71, 1352.631] (was [75.037, 1.71, 1352.471]);
  - scale [0.27, 0.81, 0.85] (was [0.85, 0.95, 0.85]);
  - rotation unchanged.
  - Its front face sits behind the calves and its top under the seat.

**After:** `check_contacts.py` over all 193 frames finds no body, shell or card vertex inside the stone, and none
below the ground. At a 4% taller stone (scale y 0.84), two fur vertices touch it in 18 frames, so the seat is in
contact.

**Caveat:** the stone is now stool-sized (about 0.37 m deep, 0.64 m wide, 0.45 m high) and reads small. A broader,
flat-topped rock with the same top height (0.46 m) and its front face 0.30 m behind the feet would read better. The
footprint is the art agent's call.

**The crouching one (B) by the fire:** no rock near it. The body never goes below the ground; only fur shells at the
toes dip up to 1.2 cm into it, in about two-thirds of the frames. That is within the ground's own unevenness. Lifting
the node 1.3 cm would remove it.
