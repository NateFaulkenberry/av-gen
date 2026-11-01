# CHRONOS, The Devourer: Gate D1, the portrait (Test A)

Media is in `~/Desktop/av-gen-review/104-chronos/devourer-d1/`. There is no engine change in D1: the creature is
built in Blender by committed scripts, and the scene uses existing systems. No ADR.

## What was tried, and what was chosen

**Two routes, compared on the same light** (`devourer-d1-route-comparison.png`).

| Route | What it is | Verdict |
|---|---|---|
| **MPFB, pushed past human** (`build_creature.py`) | An MPFB adult with every anatomical target at its limit, a scripted sculpt (longer jaw, deeper orbits, carved cheeks, brow ridge, taller skull), reworked teeth, a strand-tube hair mass, a baked skin, and its default rig. | **Rejected.** The coordinator's doubt held. The hair read as a straw mop over the face, the 64° jaw stretched the lower face into a sack around tiny teeth, and the face stayed a human mask. Kept in the repo as a fallback; not used. |
| **The Blay scan, made into a creature** (`build_scan_creature.py`) | Miquel Blay's sculpted study of an old man (photogrammetry, CC BY 4.0, one of the owner's ten downloads). | **Chosen.** It already has what MPFB could not give: a sculptor's exaggeration of age, and the hair and beard as one matted, sculpted mass, which is how Goya paints Saturn's. |

**What the scan route does,** in order:
1. Normalises the bust and cuts away the pedestal.
2. Pushes it past human: deeper sockets, hollowed cheeks (pushed toward the head's axis, because the scan's vertex
   normals are too noisy to push along), and a lengthened, hanging lower face.
3. Cuts the mouth open along the lip line under the moustache.
4. Builds a rig of its own: root, neck, head, jaw, eye.L and eye.R. The weights come from landmark distances, and the
   jaw blends into the cheeks so they stretch rather than tear.
5. Seats bulging eyeballs in the deepest point of each carved socket, found by ray casts: dirty white, tiny dark
   pupils, raw rims (r02's stare).
6. Adds a dark throat cavity behind the lips, and long, irregular, yellowed, worn teeth along both lips, with some
   missing.
7. Paints the skin from the scan's own cavities: baked occlusion drives an umber-to-ochre-bone ramp, mottled, over a
   trace of the plaster's tone.
8. Bakes a 12 s life clip.
9. Exports a glTF of 7 joints and 1 clip.

**The landmarks** (eyes, mouth line, hinge, chin) were read from orthographic grid renders (`landmarks.py`).

**The other nine downloads** were surveyed under the same light (`devourer-d1-downloads-survey.png`). Rodin's
*Wissant* surface and the Messerschmidt grimaces are the next detail sources (§4). Nothing else from them is used
yet, so only the Blay scan is logged in `assets.md`.

## Test A, the portrait in AV Gen

`examples/chronos/devourer/devourer.scene.json`, written by `build_scene.py`. Out of `examples/index.json`.

**Creature.** 50 m to the bust's height, so the head is about 33 m from beard to crown.

**Life.** The baked `idle` clip loops every 12 s:
- the jaw breathes irregularly (4-22°);
- once per loop it opens slowly to **66°** (8.2-9.4 s) and closes;
- the eyes hold, then jump (saccades);
- the head drifts a degree or two and lifts into the wide opening.

**Light, as chiaroscuro.**
- One hard, dirty-amber directional key raking from high on the creature's left at three-quarters.
- A faint crimson spot from below and in front, where the procession will come from.
- A burgundy rim from behind.
- `fallbackAmbient` off (ADR-1882), with no sky and no environment light, so what the key does not reach falls to
  black.

**How the light was found.** My first set-up was frontal (a front-left key spot). It lit the whole face flat, and it
took five lighting tests to see that the "fill" was my own key's direction, not ambient light.

**Background.** Near-black burgundy, vignette, film grain. The ambient chaos is a D3 task.

**Camera.** Two compositions, as the owner asked: direct frontal, and frontal three-quarter at 32°. A 30° lens, about
52 m from the face, with a 24 s timeline drift of a metre or two (alive, never travelling).

**Audio.** The projects carry Feline Footwear and its Drums stem (`feeds.beat = stem.drums`). No routes in D1: the
creature works with audio off, as the brief requires.

## Media

| File | What |
|---|---|
| `devourer-d1-portrait-frontal.mp4` | 24 s, 1080p30, with sound: two life loops, frontal |
| `devourer-d1-portrait-three-quarter.mp4` | The same, frontal three-quarter |
| `devourer-d1-portrait-{frontal,three-quarter}-{rest,devour}.png` | Frames from the clips at 4 s (breathing) and 9.2 s (the 66° opening) |
| `devourer-d1-portrait-sheet.png` | The four together |
| `devourer-d1-blender-scan-*.png` | The same creature path-traced in Blender (Cycles) under the same light idea: the asset's ceiling |
| `devourer-d1-route-comparison.png` | MPFB route against scan route |
| `devourer-d1-downloads-survey.png` | The ten downloads under the creature's light |

## Measured

**GPU.** 1080p, High tier, headless, 300 frames:

| Measure | Value |
|---|---|
| Frame median | **7.07 ms** |
| Frame p99 | 13.4 ms |
| Scene pass | 6.53 ms |
| Shadow | 0.27 ms |
| Bloom | 0.13 ms |

**Offline throughput.** 1080p30 renders at about 75 fps.

**Creature.** About 200k triangles after the pedestal cut, one skinned mesh plus eyes, teeth and throat. 7 joints.
Textures: a 4096² painted skin and a 512×256 eye.

## Honest critique (the bar is r02, the Goya)

**What works:**
- the wide, white, tiny-pupilled stare under an overhanging brow;
- the matted mass of hair and beard (no wig, no bald crown);
- a maw that opens to 66° with the beard swinging open like a jaw;
- long, irregular, yellowed teeth;
- ochre and umber skin;
- half the face in black.

The frontal composition is the stronger: it is the more iconic, and the mouth faces the camera as the procession's
destination.

**What does not yet:**
- **Inhuman, ancient, past an old man.** It reads as a monstrous old man, closer than D0 but not yet *other*. The
  proportions are still Blay's: a sensible skull and nose. The next push is non-uniform: a longer face, wider-set,
  larger eyes, a narrower skull, the lower jaw hanging further at rest.
- **Painterly surface.** The skin reads as painted plaster, matte and smooth at this distance. It wants the Rodin or
  Messerschmidt surface baked in as a normal map (their thumb-mark clay), and a broken, brush-stroked albedo rather
  than a smooth ramp.
- **The teeth** are simple cones (crude in the close stills). Next: sculpted, curved, worn pegs, or the Blind Creature
  or Dessen teeth reworked.
- **The mouth** is a slit cut through a shell. At 66° the corners tear slightly, and the throat is a dark ellipsoid
  rather than an anatomical cavity. Next: inner lip walls and a gum ridge.
- **Scale cues** are weak with no environment. They will mostly come from the souls in D2 (1.8 m figures against a
  33 m head), plus atmospheric depth (D3).
- **Eye tracking** is baked saccades. Aiming at the incoming soul arrives with the procession in D2: an aim layer on
  the eye bones, or the clip.

## Next (D2, on approval)

The souls (MPFB CC0 variants in grey-white wraps), the procession, ID-gated history and one soul through stages A-E.
Also, if the owner wants it before D2: one more sculpt pass toward *inhuman* (the proportions and the painterly normal
map above).
