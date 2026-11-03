# Art Pass 2, M3d: the plan

Governing notes: `44-pass2-m3d-owner-notes.md`. This is an art checkpoint: per-scene clips and stills, no full
sequence, suites deferred. Media goes in `pass2-m3d/`.

## The night camp: research

The references are night-exterior and campfire practice from cinematographers' forums and craft notes, and campfire
photography:
- [CML: creating a campfire effect](https://cinematography.net/edited-pages/Creating_A_Camp_Fire_Effect.htm)
- [CML: fire effects](https://cinematography.net/edited-pages/FireEffects.htm)
- [How to light a moonlit night exterior](https://www.dvxuser.com/threads/how-to-light-a-moonlit-night-exterior.360610/)
- [Lighting a scene for firelight](https://broadcastbeat.com/news/lighting-a-scene-for-firelight)
- [Catching the rim shot](https://www.videomaker.com/article/c13/15433-catching-the-rim-shot-with-lighting/)
- [Shooting great campfire photos](https://www.picturecorrect.com/?p=43067)

What carries over to a rendered shot:
1. **The fire is a small, hard, warm key low in the set.** Faces and fur are lit from below. The light falls off fast,
   to near black 5-8 m out. Shadows are hard and are thrown away from the fire.
2. **The fire behind or beside the subject reads more dramatic than the fire between the camera and the subject.** The
   figures become warm rims and silhouettes, and the ground shows the pool of light.
3. **The moon is a hard, cool rim from behind or to the side,** with a dim fill. The orange/blue contrast is the night
   look. The fill is never flat colour everywhere.
4. **Light the background a little,** so the place reads as outdoors: a cool, low separation on the bank and the
   skyline, not a black void and not a flat purple wall.
5. **Smoke catches the firelight** just above the flames and fades. Sparks rise in short, fast arcs and die within a
   couple of metres; they do not spray across the sky.
6. **Realism serves the shot.** Night in film is a convention (a blue-ish ambient, a warm key). Keep it consistent
   rather than physically flat.

## The night camp: the rebuild

- **Set.**
  - The burger rocks, the cave mouth and the floating scatter are gone.
  - Real, clearly licensed CC0 models (Poly Haven; logged in the asset record, never committed): a stone fire pit with
    logs from a dead trunk, Namaqualand boulders as seats and a backdrop at the bank's foot, small stones, and desert
    shrubs.
  - Each one is set on the sampled ground at several points under its footprint (the lowest), and only where the
    slope is gentle.
- **Ground.** The canyon's rock program gains close-range detail: a dry, stony ground texture on the flats and a cliff
  texture on the steep faces, triplanar in world space (ADR-1642), faded in near the camera. The bench stops being a
  bare slope.
- **Lighting.**
  - The fire is the key, behind the group from the camera: warm, high intensity, a short reach, hard shadows.
  - The moon is a cool rim from behind the group, with the fill and the sky dimmer. The fog is near black-blue, so
    darkness frames the group.
  - The craft's lights are a cool, separate source behind.
- **The fire.**
  - The stone pit, logs set in a teepee, and flames in two layers that lick and thin.
  - Smoke lit warm low down, fading as it rises.
  - Embers that rise 1-3 m and die.
- **Placement.**
  - The three round the fire, the Gray across from them facing them, all on the ground.
  - The craft behind, on the bench.
  - The camera low, at eye height, from the river side, the fire between the camera and the group's far side.
- **Proof.** One still at the 112.5 s beat before the full pass; then before/after stills at the same beats and a clip.

## The rest

- **Portals.** The Moon portal is the reference look.
  - Every portal gets its look: the mesh wire with its shards on the pyramid and the launch bay, which had none, and
    glyphs and tears at its scale.
  - Each one is shown side by side with the Moon's.
  - Portal 1's opening is cleared of the scatter rocks.
  - The one-frame particle pop at a crossing is found frame by frame and fixed, and every crossing is checked.
  - The launch bay: no sub launches while it opens; the canyon exit is moved high, out of the outer cliff; the bay floor
    no longer stands in front of the glimpse.
- **The Moon.** The pixel corona animates: three layers of blocks, each breathing in scale out of phase, flickering in
  on its own beat source, with the datamosh kept. The flicker is measured.
- **Mars.**
  - The sand embraces the pyramids. The references are the historic photographs of the Sphinx buried to its neck and of
    Giza with sand banked against the faces, and recreations of buried pyramids: one long wind ramp up each face, its
    top line uneven, the corners scoured lower, no flat-topped mound.
  - Texture: a rippled sand (`aerial_sand`) on the drifts and the plain near the camera; weathered sandstone
    (`old_sandstone_02`) on the blocks; a cliff texture (`cliff_side`, `rock_face`) on the mesas and rocks. These
    replace the banding and the vertical streaks.
