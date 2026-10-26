# Cursed Slammers POC 5: the four surface treatments on other subjects (report)

2026-10-26, a one-pass survey: no engine change, no registration. Media: `~/Desktop/av-gen-review/93-cursed-slammers/poc5/`.

- **The materials** are POC 4's A (holofoil engraving), B (intaglio), C (iridescent chrome) and D (hybrid security
  foil), **unchanged**. The only change is a prepended rescale of the object-space position, two ops, so about 100
  engraved lines span each subject whatever its size.
- **One camera, lighting and exposure per subject:** the POC 4 studio, scaled to the subject, and a slow ±20° orbit.
- `poc5/survey.py` writes all 35 projects.

## Subjects and why

| Subject | Source | Why |
|---|---|---|
| bust | Talking Heads porcelain bust: `th-body`, `th-neck`, `th-head` (ours, committed) | Smooth, clean, untextured, a strong classical silhouette. The obvious "engraved statue" test |
| horse | `assets/farm/horse.glb` (third-party: rendered locally, never committed or copied) | An animal silhouette in profile |
| astronaut | `assets/musicians/astronaut_keys.glb` (third-party, not for distribution: local only) | Untextured hard surface: a suit and helmet |
| medallion | Procedural SDF, meshed: a disc, a rim torus, a domed boss, a six-arm star in relief | A coin or relic, the classic engraved object |
| torus | Procedural primitive | Continuous curvature in both directions: a ring or a link |
| gem | Procedural low-segment sphere | A faceted gem or crystal |
| mask | Procedural SDF, meshed: cranium, brow, cheeks and jaw blended; sockets and nose cut away | A stylised skull mask: a cursed-relic stand-in |

## What looks genuinely good

1. **Torus + A and torus + D are the best results of the whole study.** The lines flow round the tube in continuous
   rings, the spectrum sweeps with the orbit, and it reads as an engraved holographic ring. This is the look the face
   could not get, because the geometry has clean, continuous curvature.
2. **Mask + B and mask + D:** an engraved silver skull relic. The relief lines wrap the sockets, and the crosshatch
   fills the shadows. Very legible, with a strong "cursed object" character.
3. **Bust + B and bust + D:** an engraved silver statue. Cleaner than on the scan: the porcelain's smooth surfaces let
   every line read. This is directly usable for Talking Heads, which already draws the bust as gltf nodes that can
   take a program.
4. **Gem + C:** an oil-slick crystal. Chrome works on smooth convex forms, where its reflections sweep with the
   orbit.
5. **Medallion + B:** a silver engraved coin with a raised star and boss. Good, but the flat face gets plain straight
   lines (the depth term is constant there). On the flat face A and D read as a uniform pink, because their colour
   index needs a varying normal or depth.

**Poor:**
- **horse:** its low-poly palette texture multiplies the program colour into a dark brown mosaic, and its faceted
  shading breaks the lines. Unsuitable without a texture-free copy.
- **astronaut:** flat-shaded low poly, so the lines step at every facet. C is passable; A, B and D look patchy.

## Which material reads best on clean geometry

- **B and D (the engraving) read best** on everything with relief or curvature: bust, mask, torus, medallion. D adds
  enough colour to be "foil", and B is the safest.
- **A needs strongly curved forms** (the torus) to look holographic rather than like a rainbow wash.
- **C is the best of the four only on smooth convex shapes** (gem, torus).
- **Across the study:** these looks need clean, continuous surfaces. Photogrammetry noise (the scan), facets
  (astronaut, gem in B) and baked textures (horse, the scan) all fight them.

## Where these looks could fit in AV Gen

- **Talking Heads:** busts as engraved silver statues (B/D). It is the same program on the existing gltf nodes, with
  the audio able to route its constants. For example, the spectral band position of D on the voice.
- **Relic objects in a scene:** an engraved holographic ring, a medallion or a skull mask as a hero prop. They would
  suit the Rift or Astral environments as the object the camera finds, or a Cursed Slammers variant where the relic
  replaces the ape.
- **A stand-alone "engraved object" live scene:** one object turning slowly in a dark studio, with the music on the
  band position, line density and sparkle. That is the cheapest route to a finished look from this study.

## Performance (`20-performance.txt`)

- **The program costs scale with screen coverage**, not with the drawing path.
- **gltf subjects** cost +1.2 to +3.8 ms over their bare render, headless 1080p:
  - bust D +3.75 ms (15.2 ms total);
  - live Ultra 1080p, bust D runs at 13.6 ms GPU median.
- **The procedural and SDF subjects cost more**, because they fill more of the frame:
  - mask D +8.6 ms headless, 19.0 ms live at Ultra against 10.3 ms bare;
  - medallion B +9.1 ms;
  - torus D +6.0 ms.
- **No evidence the procedural path's compiled programs (ADR-1493) made these cheaper.** Meshed SDFs probably draw
  through the SDF renderer rather than the procedural one, and the headless bench may not enable the compiled
  variant. This is worth one check before building a scene on them.
- **Everything holds 1080p30.**

## Files

- `poc5/survey.py`: the subjects, the rescale, the projects. It imports `poc4/variants.py` unchanged.
- Review folder:
  - `01-combined-slow-orbit-7-subjects-x-4-variants.mp4`;
  - `10-…16-stills-<subject>.jpg` (base, A, B, C, D);
  - `20-performance.txt`;
  - `21-report.md`.
