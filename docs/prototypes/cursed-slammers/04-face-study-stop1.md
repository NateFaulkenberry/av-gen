# Cursed Slammers: face study, stop 1

2026-10-25. **The owner narrowed the scope:** "we're not at the point of adding cursed slammers to the app yet ...
lets just simplify the scene, just try rendering this head [H1] with lenticular effects".
- One subject: jtuhtan's Gorilla Face scan.
- No bars, hands, cycle, audio or animation.
- No registration, no index entry, no suites.
- A look-development study on `scene/cursed-slammers` only.

## The asset

- **Source:** the owner downloaded it (signed in) to `assets/cursed-slammers/reference/gorilla-face/`. It is
  gitignored and recorded in `assets/cursed-slammers.manifest.json` with file hashes.
- **Credit**, verbatim from the download's `license.txt`:
  > This work is based on "Gorilla Face" (https://sketchfab.com/3d-models/gorilla-face-808b2c0c11bf44c983e8b8c787ada844)
  > by Jeffrey Tuhtan (https://sketchfab.com/jtuhtan) licensed under CC-BY-4.0 (http://creativecommons.org/licenses/by/4.0/)
- **Prep:** `tools/cursed_slammers/prep_face.py` (Blender 5.2.1 headless, about 25 s). It produces
  `assets/cursed-slammers/cs-face.glb` (gitignored):
  - 559,884 tri in six objects, decimated to 90,000;
  - a 4096² tangent normal map baked from the full scan;
  - the 8192² colour downsampled to 4096², with its black atlas gutter filled outward 24 px. Without the fill,
    mipmaps pulled the gutter into hair-line black specks along every island edge;
  - auto-oriented (its mean normal faced the viewer to within 3 degrees), normalised to width 1.0.
- **Stand-in:** `study/make_standin_scan.py` proved the recipe before the download. It is kept for re-testing the
  recipe without the scan.

## The study

`study/face_study.py` writes four variants. Each is 16 s, one slow side-to-side camera sweep (x ±0.40 at 2.0 from
the face, always aimed at it), 1080p30, tier high:

| Variant | Treatment |
|---|---|
| V0 | Plain: the lit scan over the dark substrate (reference). The edges sink into the dark (a Fresnel fade) |
| V1 | Lens ridges in the highlights only: a faint vertical lenticule normal on the skin (tilt 0.12, 70 per unit, anisotropic roughness), a strong one on the near-black metal substrate (32 per unit), a travelling sweep light |
| V2 | V1 plus the interlaced, view-dependent flip. From one side the face is embossed into the substrate (near black, glossy, ridged, so it shows only as glints). From the other side it is the lit ape. Between, the two interleave in strips, and the boundary sweeps across the face with the camera |
| V3 | V2 with the embossed look in muted foil (silver F0 cycling cold violet, teal and bronze with the view angle) and a foil substrate |

The relief is exaggerated in depth (the node's scale z 1.5). The substrate is at z -1.4.

## Review folder: `~/Desktop/av-gen-review/93-cursed-slammers/stop1/`

The clips, stills, the asset-prep sheet and `20-gpu-numbers.txt`.

## Cost (`20-gpu-numbers.txt`)

- **Headless 1080p GPU p50** (3 reps): V0 14.3, V1 16.2, V2 18.0, V3 20.6 ms.
- **Live profile at 30 fps:** V0 GPU median 15.6 ms (Ultra, 1080p); V3 17.2 ms (High, 0.85 scale). 100% of frames
  on time.
- **It holds 30 fps, but it is heavy for one head.** Any full-frame lit surface costs about 7 ms, and the
  substrate's program adds about 4.5 ms. This is undiagnosed. Profile it before the study grows.

## Known limits

- The scan is a mask. At the sweep's ends its cut side edge shows, softened by the edge fade. A smaller sweep, or a
  back volume, would hide it.
- The world-space lenticules are fixed in the world, so a moving head would slide under them. The head does not
  move here.
- The substrate's ridges alias into a faint moiré near the top of the frame. That is on-brand for a lens sheet, but
  it can be lowered with a finer `roughnessFilter`, or by dropping the substrate's ridge frequency further.
