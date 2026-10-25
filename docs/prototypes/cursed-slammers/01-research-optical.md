# Cursed Slammers: optical and art-direction research (Part A)

Prep, 2026-10-25, branch `scene/cursed-slammers` (base main daf93949). One agent covered both research briefs (the
owner's agent cap). No engine code was changed. The probe was built against this tree (`build/release`, Release, M2
Max on AC); every GPU run went through `tools/gpu-lock.sh`.

**Headline.**
- **The illusion needs no new rendering architecture.** Real geometry at exaggerated depths gives the parallax. The
  existing material-program ops (`viewDirection`, `worldProject`, `palette`, `gradient`, `smoothstep`, `mixBy`,
  `anisotropy`, the `normal` output) give the lens ridges, the foil and an **interlaced view flip**. Light tracks give
  the sweeping glints.
- **The probe proves the three building blocks on a slow side-to-side camera** (clips in §6):
  1. depth planes around a key plane;
  2. specular-only lens ridges plus foil bars with a thin travelling highlight;
  3. a view-dependent interlaced flip whose transition crosses the image as a striped curtain. This is the real
     artefact of a lenticular print, and the strongest single "this is a lenticular" cue found.
- **The core idea for the scene.** The ape is always there, embossed into a dark, ridged substrate, and visible only
  as ridge glints (Phase 1). It *flips* into a lit, dimensional image through the interlace (Phases 2-3), and flips
  back (Phase 6). The reveal is an optical image resolving, not a model fading in.

## 1. How the real things look and behave

### 1.1 Lenticular prints

- **Mechanism.**
  - Two or more source images are sliced into strips and interlaced under a sheet of parallel cylindrical lenses
    (lenticules).
  - At each viewing angle every lens magnifies one strip, so the image changes with the viewpoint.
  - Pitch is quoted in LPI: 40-75 for posters seen from afar, 75-100+ for cards held in the hand.
  - Sources: [Kodak white paper](https://www.kodak.com/content/pdfs/How-does-lenticular-printing-work-white-paper-EN.pdf),
    [Wikipedia: Lenticular printing](https://en.wikipedia.org/wiki/Lenticular_printing),
    [4over4 guide](https://www.4over4.com/guide/how-to-make-lenticular-prints).
- **Effect families and their angle budgets** (Wikipedia):
  - **flip:** a large angle change jumps between two images;
  - **animation, morph, zoom:** a medium change steps through frames;
  - **3D:** a small change, each eye seeing a different view.
  - 3D needs vertical lenticules (a side-by-side view). Flips and animation read better top-to-bottom.
- **3D lenticulars are layered, not sculpted.**
  - A designer splits the art into up to about 15 depth layers, keeps one as the **key (focal) plane**, and offsets the
    others by increments.
  - Foreground layers move one way as you move, background layers the other
    ([4over4](https://www.4over4.com/content-hub/stories/how-to-make-lenticular-prints),
    [tek-tips layering thread](https://www.tek-tips.com/threads/depth.1237156)).
  - Vendors quote 10-16 views for 3D, 2 for a flip and 4-8 for motion
    ([VICGI lens FAQ](https://vicgi.com/lenticular-lens-faqs.html)).
  - The 2.5D "pop-up book" look (flat cut-outs at depth) is the honest description of most 3D lenticular cards.
- **The artefacts are the character.** Each one below is something we reproduce on purpose (Wikipedia):
  - **ghosting / crosstalk:** a neighbouring view stays faintly visible. Exaggerated depth, too few frames or a pure
    white ground makes it worse;
  - **the curtain:** a transition sweeps across the print as "a veil or curtain crossing the visual", because each part
    of the print sees a slightly different angle;
  - **streaks parallel to the lenticules** during a transition;
  - **colour fringing** (misregistered colour separations) in tilts;
  - **discrete view jumps:** too few views reads as stepping, with doubled outlines.
- **Surface optics.**
  - The lens sheet is glossy plastic with ridges at the lens pitch.
  - A light source reflects as a comb of thin parallel glints, one per lenticule, that slides as you tilt.
  - The image under it is matte. That is why the ridges show in the speculars and not as stripes in the image.
- **Era references** (1950s-90s "flickers", "winkies", Vari-Vue):
  - eye-movement and "roving eye" cards;
  - winking-eye flips;
  - 1990s lenticular CD covers, comic covers and collectible inserts
  - Sources: [Wikipedia](https://en.wikipedia.org/wiki/Lenticular_printing),
    [Science Museum Group: Vari-Vue picture](https://collection.sciencemuseumgroup.org.uk/objects/co8207002/vari-vue-picture),
    [EBTH: Vari-Vue Batman & Robin flicker insert, c. 1966](https://ebth.com/items/4033405-vari-vue-batman-and-robin-framed-flicker-ring-vending-machine-insert-circa-1966).
  - The "eyes follow you" card is the archetype the brief's uncanny brief is reaching for.

### 1.2 Holographic foil and iridescence

- **Embossed holographic foil** is a diffraction grating. Highlights split into a spectrum around the specular
  direction, and the colour shifts with angle. **Thin-film iridescence** (oil, beetles) shifts hue with the
  incidence angle through interference.
- **Real-time practice:**
  - **physical:** a first-order grating approximation with a lookup table
    ([Toisoul & Ghosh 2017](https://wp.doc.ic.ac.uk/rgi/?p=474)); spherical-harmonic precomputation
    ([ISVC 2006](https://unpaywall.org/10.1007%2F11919476_51)); Belcour & Barla's thin-film microfacet extension
    ([SIGGRAPH 2017](https://history.siggraph.org/?p=198010));
  - **cheap and common:** a view-angle-driven rainbow ramp weighted by Fresnel, plus flakes
    ([three.js foil stickers](https://feedbagel.com/post/creating-foil-sticker-effects-with-custom-threejs-shaders),
    [Godot holo foil](https://godotshaders.com/?p=16763),
    [Blender Artists grating shader](https://blenderartists.org/t/a-complex-approach-iridescence-in-cycles/613211?page=2)).
- **For this brief:** a full rainbow foil is the "Pokémon holo" cliché and fights "mostly dark neutrals". Use a
  **muted** cosine palette around silver, only in the speculars of the metal (the bars and the substrate). The
  colour comes from reflections, never from emission.

### 1.3 Multi-view rendering (what real lenticular displays do)

- Real lenticular displays render N views and interlace them per sub-pixel column in a shader, plus a sharpening
  pass ([BYO3D 2011](https://www.slideshare.net/slideshow/byo3d-2011-rendering/8832306),
  [EVL VRST 2010](https://www.evl.uic.edu/documents/vrst2010.pdf), Leia's two-pass interlace and sharpen).
  Apple's patent precomputes a lens-to-UV map and samples one texture per view
  ([US 2024/0201512](https://patents.justia.com/patent/20240201512)).
- **Not for us.** N renders per frame is N times the scene cost, and the result would only be correct on a real lens
  sheet. On a flat screen, the *visible behaviour* (the parallax, the curtain, the ridges, crosstalk) is what we
  reproduce.

### 1.4 The cinematic ape behind bars

- **Composition.**
  - The face is huge, frontal or slightly three-quarter, eyes on the upper third, with the brow shadowing the eyes.
  - The hands are outboard of the face silhouette, gripping the bars at about muzzle height. They frame the face and
    never cover the eyes (the probe broke this rule; §6).
  - The bars are a few thick verticals, not a grid: the owner's note is that it must **not read as a prison cell**.
- **Lighting.**
  - Grazing side light and negative fill. The brow ridge throws a slight shadow over deep-set eyes, so they read
    recessed and wild.
  - A subtle rim separates the silhouette from the black.
  - Flat front light kills it.
  - Sources: [Planet of the Apes stills notes (unverified blog)](https://library.peoplespharmacy.com/planet-apes-stills),
    [low-key rim lighting](https://www.picturecorrect.com/low-key-rim-lighting-tutorial-for-portraits/).
  - The wet catchlight in the eye is the "alive" cue.
- **Avoid** the generic horror grade (teal/orange, crushed greens), fog soup and jump-scare flashes (the brief §2).

## 2. What AV Gen already has (verified against the code)

| Need | Engine feature | Where | Notes |
|---|---|---|---|
| Depth planes | Real geometry at authored depths; gltf nodes with `position`/`rotation`/`scale`/`opacity`/`emissiveBoost`/`tint` routable (`nodes/<n>/...`) | `src/scene/composition.cpp` `registerNodeParameters` | Parallax is physical, so no per-layer shift code is needed |
| Key-plane camera | Camera `position`/`target` tracks with `loopLength`, smooth keys; `kit.Scene.drift_camera` | `src/params/timeline.hpp`; `tools/sonic_vfx/kit.py` | Aim at the face, so the face is the zero-parallax plane |
| View-dependent shading | Material program input `viewDirection`; `fresnel`; `anisotropy` (view-dependent roughness) | `docs/procedural-materials.md` | A program may run on a gltf node (ADR-1572), up to 8 programs a scene, 48 ops each |
| Lens ridges | Program `normal` output (tangent space through a derivative cotangent frame, no tangents needed) built from `worldProject` and `palette` (the cosine gives the ridge profile) | same | Verified: vertical ridges in world space, visible in speculars (P2) |
| Interlaced flip | `viewDirection` and a ridge-phase stripe, through `gradient`, `smoothstep` and `mixBy` between two looks | same | Verified (P3): the curtain and striped mid-flip appear unprompted |
| Foil / thin film | `palette` (Quilez cosine) on a view-angle term into a metal's `baseColor` (F0); `oklabRamp` | same | Muted around silver |
| Thin, sharp bar highlight | Low-roughness metal cylinders (procedural `cylinder`, `linear` distribution, one draw) lit by a moving spot (`lights/<id>/position` track) | `composition.cpp` light params | Verified: hair-thin vertical glints with a tint (P2). Area `tube` and `rect` lights also exist |
| Selective materialise / dissolve | `dissolve` effect (an entity lane: noise or a height sweep, glowing edge, `reverse` = materialise; runs in depth and shadow too) | `src/world/effects/kinds/dissolve_effect.cpp` | Per entity, so hands, head and eyes each get their own curve |
| Glints, bloom | `post/bloom/*`; the glint-flake particles (ADR-1153); `lightBeam` shells | | Bloom only on true highlights (threshold 1.0) |
| Crosstalk ghost | The frame echo `temporal/echo/*` (an FIR over clean frames) | `src/scene/temporal_settings.hpp` | Verified (P4): doubled bars and fingers across a view jump |
| Sequencing | `timeline` signal sources (keys, `loopLength`, pure in time, so seek-safe), timeline tracks on any parameter, interpret mappings `M()`, bumps `kit.place_bumps` | `src/signals/source.hpp` `TimelineSource` | One looping cycle drives every phase envelope |
| Audio | `response.*`, `stem.*`, `role.*`, `beat.*`, `section.*`, `music.*` (the mix kick/snare/hat are retired on main) | `docs/help/audio-signals.md`, `tools/sonic_vfx/signals.py` | See 03 §4 |

**Not available, and not needed:**
- N-view rendering;
- a screen-space lens-sheet post pass;
- a diffraction BRDF;
- per-pixel blended overlays on procedural nodes (ADR-1572: procedural nodes draw blended materials opaque, so any
  translucent layer must be a gltf node).

## 3. Approaches considered

| # | Approach | Look | Cost | Verdict |
|---|---|---|---|---|
| O1 | **Depth planes + key-plane camera** (real geometry at exaggerated, separated depths) | Strong, physical parallax. The bars and hands slide against the face one way, the substrate the other | Free | **Foundation** |
| O2 | **Specular lens ridges** (program normal, vertical world-space lenticules, anisotropic roughness) on the skin, the substrate and the haze cards | A comb of thin glints that slides with the camera and the light. Diffuse almost untouched at small tilt | 4-6 ops | **Use**, tilt ≤ 0.2 on skin, stronger on the substrate |
| O3 | **Interlaced view flip** between two looks (embossed-in-substrate vs lit ape; or ape vs silver negative) | The unmistakable lenticular transition: a striped curtain sweeping across | ~10 ops | **Use** as the reveal, the disturbance and the return. Time-biased by the timeline, view-gated by the camera |
| O4 | **Muted foil metal** (palette F0 on bars and substrate) | Silver with violet, teal and bronze in the highlights only | 6 ops | **Use**, restrained |
| O5 | **Stepped views + echo crosstalk** (camera snaps between 12 views; the echo doubles the jump) | Authentic, but a whole-frame stutter if held | Free | **Accent only**: a few steps in Phase 5 (disturbance) |
| O6 | Flat 2.5D cut-out cards (the classic 3D-lenticular look) | Flat and cheap-looking; loses the face's form | Free | Reject for the ape. **Use** for the background haze and glint cards |
| O7 | Screen-space interlace post (two renders) | Exact | 2x the scene | Reject |
| O8 | Chromatic aberration and stripe overlays | The cliché the brief forbids | | Reject (at most a hair of fringing in Phase 5) |

**Chosen: O1 + O2 + O3 + O4, with O5 as a Phase-5 accent.** All are existing ops, routes and tracks.

## 4. The depth plan (shared with 02 and 03)

```
camera z +4.0, sweeping x +-1.0 (period 32 s), always aimed at the face
  |
  z  0.0   NEAR PLANE: four thick foil bars, the two hands gripping the inner pair (outboard of the face)
  z -2.2   KEY PLANE: the face (zero parallax). Eyes 0.3-0.4 in front of the face plane, in deep sockets
  z -4.5   SUBSTRATE: the ridged, near-black "print" (Phase 1's glints live here)
  z -7/-10 two faint haze/glint cards (flat 2.5D layers), very low emission
```

- **Why exaggerated.** At these depths the near plane moves about 1.5x as far across the screen as the face for the
  same camera step, and the substrate moves the other way. The arms joining hands to head are never drawn: the hands
  sit "in another optical layer". That is the owner's note: the ape pushes through the image, not a cell.
- The bars are wider apart than a cage (about 0.9 units at the near plane) and few (4), so they read as the print's
  own foreground layer.

## 5. Palette and light

- **Palette.**
  - the ground: near black, violet-biased (`#040308` to `#0b0812`);
  - the skin: near-black brown, with selective wet specular (lips, nose, brow, knuckles);
  - the eyes: amber-brown iris with a single hard catchlight;
  - the metal: silver with a muted foil cycle (cold violet, teal, bronze) in the highlights only;
  - **no saturated emission anywhere** except the brief violet glow of the flip's "negative" layer.
- **Lights.**
  - **key:** a spot, high camera-left, grazing; reveals the brow and muzzle and leaves the eyes in the brow's shadow;
  - **rim:** cold violet from behind right;
  - **sweep:** a narrow spot travelling across in front, which makes the bar glints and the ridge combs;
  - **eye catchlight:** a tiny point near the camera, so the catchlight stays in the eyes as the camera moves;
  - all unshadowed (cost; ADR-278 note in kit).

## 6. The probe (throwaway): what it showed

**Files:**
- generator `probe/make_probe_subject.py` (Blender 5.2.1 headless): a crude ape-ish head (voxel-fused ellipsoids), an
  eyeball, a procedural hand gripping a bar and its mirror;
- scene arms `probe/optical_probe.py`.

All arms are 16 s, 1920x1080, 30 fps, tier `high`, one slow lateral camera sweep x ±1.0 aimed at the head.

| Arm | Contents | Clip |
|---|---|---|
| P1 | Depth only: bars and hands at z 0, head at z -2.2, plain materials | `~/Desktop/av-gen-review/cursed-slammers/probe/P1-depth-only.mp4` |
| P2 | + optical surface: ridged skin (64/unit, tilt 0.22, anisotropic), foil bars, ridged metal substrate, sweep spot | `.../P2-optical-surface.mp4` |
| P3 | P2 + the interlaced flip on the head and hands (skin to a silver negative with a violet glow) | `.../P3-interlaced-flip.mp4` |
| P4 | P2 with a stepped 12-view camera + a 3-frame echo | `.../P4-stepped-views-crosstalk.mp4` |

Also in that folder:
- `contact-sheet-P2-P3-P4.jpg`: rows P2, P3, P4 at t = 0, 4, 8, 12 s;
- `P4-step-crosstalk-frames.png`: six consecutive frames across one view jump.

**Readings:**
1. **Parallax works and is strong.** Across the sweep the bars and hands cross the face's features differently: an eye
   is behind a bar at one end and clear at the other. The substrate's glints move the other way. The face holds still
   as the key plane, which is what makes it feel embedded in a structured image.
2. **Ridges read as optics, not stripes, at a small tilt.** At tilt 0.55 (first still) they banded the diffuse like
   corduroy, which is the "striped overlay" the brief forbids. At 0.22 with anisotropic roughness they mostly live in
   the specular combs. The substrate takes a stronger tilt, because its whole job is glints.
3. **The bars' thin highlight is exactly the owner's "sharp, thin highlight".** A hair-line vertical glint, tinted
   by the foil palette, travelling as the sweep light crosses. It wants to be *occasional*: route the sweep light's
   intensity, don't leave it on.
4. **The interlaced flip is the discovery.** Mid-flip the surface is interleaved strips of both looks. Because a
   perspective camera sees each part of the head at a different angle, the boundary sweeps across the head as the
   camera moves. That is the print's "curtain crossing the visual", unprompted. With a time-driven bias it becomes
   the materialisation.
5. **Stepped views + echo** produce the authentic doubled-outline crosstalk (frames 4-5 of the strip). Held for a
   whole cycle it would read as stutter, so it is a Phase-5 accent.
6. **Composition faults to fix in the build:**
   - the hands sit over the eyes at some camera positions; put them outboard and lower;
   - the plate washed grey under the sweep in the first take; it must stay near black (metal F0 dark, roughness 0.1);
   - the skin read mid-grey; it needs a darker base with a wetter, selective specular.

**Cost:** the headless bench is still to run (it lost the GPU lock queue; 03 §5).

## 7. Limitations (honest)

- It is not a true multi-view lenticular. Depth comes from real parallax under a moving camera. On a still frame the
  only lenticular cues are the ridge combs and a mid-flip's stripes, which is why the owner's rule (judge it moving)
  is the right test.
- World-space ridges are fixed in the world, so a moving head slides under them. That is physically what a lens sheet
  over a moving image does, but it can shimmer if the head moves fast. Keep head motion slow, or switch the skin's
  ridge phase to `objectProject`.
- The flip changes the *look*, not the geometry. Both looks share one silhouette. A second silhouette (another
  expression) would need a second mesh layer crossfaded the same way (03 §8, later).
