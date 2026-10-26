# Cursed Slammers POC 2: real optical depth (report)

2026-10-25, branch `scene/cursed-slammers`. The brief is `05-brief-poc2-optical-depth.md`.
- An isolated study: nothing registered, no production file touched, no engine change.
- The asset is jtuhtan's Gorilla Face (CC BY 4.0, credit in `assets/cursed-slammers.manifest.json`), unchanged as
  the subject.
- Media: `~/Desktop/av-gen-review/93-cursed-slammers/poc2/`.

## Verdict

**Partly achieved.**
- **Achieved:**
  - a clear, detailed face with no stripe overlay;
  - real separation from the background;
  - a real perspective change with viewpoint;
  - a genuine multi-view lenticular pipeline: views, interlaced print, lens lookup;
  - a lens surface that does not obscure the face.
- **Not achieved:** the deep, sculptural "pops off the card" depth of a 1990s 3D lenticular.
  - That depth comes mainly from binocular disparity: each eye sees a different strip. A monitor shows one image to
    both eyes, so we can only reproduce **motion parallax**.
  - At the ±6° span tested, parallax reads as a subtle turn of the head, not as stacked depth.
- **The lens surface (Test C) is restrained to the point of near-invisibility** at review size.
- **Recommendation:** do not proceed to the scene yet. Test the representation in §7 next.

## 1. The three mechanisms (research; sources in 01 §1)

| Mechanism | What it is | What it contributes | Can a monitor reproduce it? |
|---|---|---|---|
| Lens structure | Parallel cylindrical lenticules, vertical for 3D. Each lens magnifies one narrow strip of what is under it, chosen by the angle you look from | Selects the view. Its surface also makes the glossy ridge glints and the slight horizontal softness (one sample per lens) | Its *appearance*, yes. Its optics, only as a simulation |
| Interlaced views | N perspective images cut into strips, laid under each lens in reverse order (`10-B1-interlaced-print-16-views.png`, zoom `11-…`) | The data the lens selects from | Yes, exactly (we build the print) |
| Perceived depth | (a) **binocular disparity**: the two eyes, about 6 cm apart, fall in different viewing zones and see different views at once; (b) **motion parallax**: moving changes the view | (a) is the static "it floats in front of the card" depth; (b) is the change as you tilt | (b) yes. (a) no, not without a stereo or autostereoscopic display |

## 2. Why stop 1 showed vertical lines (`study/face_study.py`)

**Entirely shader**, from three material programs. There is no texture, post pass or geometry cause:
1. **`cs_face` wrote a tangent-space normal** from `worldProject` (70 per unit) through a `palette` cosine, with
   tilt 0.12-0.65.
   - That modulates the *diffuse* and specular of every pixel: about 120 ridges across the face, a period of about
     9 px at 1080p.
   - This alone made the corrugated, ribbed-screen look.
2. **The flip (V2, V3) added a stripe term** `0.2 cos(2π·70x)` to its view selection, so mid-transition the two looks
   interleave in bands of 4-5 px.
3. **`cs_plate` ridged the full-frame metal backdrop** (32 per unit, tilt 0.75) into a comb of glints.
4. **Anisotropic roughness stretched every highlight vertically**, reinforcing all three.

It was a stripe pattern on one image: no views, no lens. POC 2 draws the face with **no material program at all**.

## 3. The harness (`poc2/harness.py`)

- **The subject:** the face against a dark neutral backdrop (a very soft mottle, so its parallax is visible). The
  face fills about 90% of the frame height.
- **Lights:** key (upper left), weak fill, rim. Unshadowed.
- **Modes:**
  - `front`: the fixed frontal reference;
  - `view --x`: one fixed horizontal offset;
  - `sweep`: a live 3D camera moving on the rail;
  - `rail --views N --span deg`: the N fixed capture cameras, all converged on the key plane.
- **The effect toggle** is the choice of stage: the original, parallax only, the lenticular with the lens off or on
  (`lenticular.py --lens 0|1`).

## 4. Test A: the multi-view lenticular (`poc2/lenticular.py`)

**This is a cached, offline simulation**, and it says so in its header:
- AV Gen renders N views once, from a rail of cameras converged on the face's key plane. That is valid because the
  face is static.
- numpy builds the print and simulates a viewer in front of it.
- It simulates how a print *looks*. It does not ray-trace refraction or reproduce binocular disparity.

| Step | Implementation |
|---|---|
| Lens pitch and orientation | Vertical lenticules, 4 px pitch at 1080p (480 lenses) |
| Views and ordering | 16 views over ±6° (8 views tested first). Under each lens, strips in reversed order (a lens inverts) |
| Interlace coordinates | Under lens j, column k of P carries view `N-1-floor(kN/P)`, sampled at lens j |
| View selection | The viewer at offset X, distance D = 4 print widths, sees lens j at θj = atan((X−xj)/D). Strip `s = N(0.5 + tanθj / (2 tan zone)) − 0.5`, zone 6°, wrapping into repeated zones |
| What the lens shows | One strip, magnified across the lens (horizontal resolution = lens count), blended over a narrow band with its neighbour, plus a 6% ghost of both neighbours (crosstalk) |
| Alignment | The print and the lens share one pitch and origin by construction (the simulated equivalent of a pitch test) |

**Readings:**
- **8 views were not enough.** With the relief ×2.2, neighbouring lenses showed visibly different views. That gave
  vertical seams where the face jumps between views: a real lenticular fault with too few views.
- **16 views at a 4 px pitch** remove the seams. What remains is a soft crossfade and slight horizontal softening.
- **The perspective changes in steps as the viewer moves,** and the view boundary drifts across the face (the print's
  curtain).

## 5. Test B: facial depth (`tools/cursed_slammers/depth_variants.py`)

The depth is measured along the view axis and recentred on a key plane at the median depth. The face is not scaled
or moved toward the camera.

| Variant | Relief | Result |
|---|---|---|
| B0 | The scan's own | Depth range 0.53 face widths (1-99%). Not flat, but much of that range is the mask curving back at its sides. Small perspective change at ±6° |
| B1 | Relative ×2.2 about the key plane | The muzzle and brow come forward and the sockets recede. Clearly more parallax. **The best of the three** |
| B2 | Terraced into 5 plateaus with soft risers (the stacked-card look) | Reads as contour bands on the cheeks and a flattened nose. The cut-out look, but it damages the face. Not recommended on this asset |

**The asset's deficiencies, specifically:**
- **It is a front mask.** No back, ears or neck. Its cut side edges show past about ±8°, which limits the span.
- **The eyes are carved.** No separate eyeball, so the eyes cannot sit at their own depth or catch a light
  independently.
- **The relief is that of a sculpture,** with a shallow brow-to-socket step compared with a real gorilla.
- **The texture holds baked shading** (photogrammetry of a bronze-like patina). That fights directional lighting:
  some features read lit from the scan's original light.
- **The detail is good.** The 8K texture, downsampled to 4K, plus a 4K normal map is ample.

## 6. Test C: the lens surface

- Each lenticule is a cylindrical cap. A glint sits where the cap's normal bisects the light and the eye: the same
  place in every lens. It appears only inside the soft window where the light's mirror image falls on the sheet,
  and slides as the viewer moves.
- **Result** (`04-testC-lens-surface-off-on.mp4`): the face stays clear, with nothing painted elsewhere.
- **But at the restrained strength** (peak 0.22 additive) it is barely visible at review size. The window is often
  off the face.
- **Next:** a stronger, narrower window placed across the brow, and a brighter, smaller light. It is a parameter
  pass, not a new mechanism.

## 7. Performance (M2 Max on AC; all runs under `tools/gpu-lock.sh`; `07-performance.txt`)

**Measured:**

| What | GPU |
|---|---|
| One view of the harness, headless 1080p, B1 (3 reps) | p50 14.9 ms (scene 13.8) |
| The same without the backdrop | 8.3 ms |
| The lens-lookup pass, priced by a stand-in post layer doing the same arithmetic and 4 texture taps per pixel (`poc2/lens_lookup_cost.wgsl`; 3 reps) | **0.06 ms** |
| The parallax-only stage, live (`live_scene_profile.py --mode live --target-fps 30`, Ultra, 1080p) | GPU median 17.3 ms, 100% of frames on time |

**Implied:**
- **Cached lenticular, live:** one view's cost plus about 0.06 ms per frame, plus N cached views in memory (16 ×
  1080p RGBA8 = 133 MB; 33 MB at half resolution).
- **Live N-view rendering** (needed only if the face animates): 16 × 8-15 ms is far over budget. 8 views at quarter
  resolution is about 2 views' cost, borderline at 1080p30.

**The gap for a live version:** AV Gen shader layers cannot read images other than the frame (no image inputs). The
smallest change would be **ADR-1690: a post shader layer may declare image inputs (a cached view array)**, tested off
= byte-identical. Not built, because the cached offline path answered the question.

**Note:** one face plus a full-frame backdrop costing about 15 ms is high. A full-frame lit surface alone costs about
6.5 ms. That is undiagnosed (also seen at stop 1) and is worth a renderer look regardless of this POC.

## 8. What produced what

- **The depth:** the real 3D geometry seen from different viewpoints (motion parallax), amplified by B1's relative
  relief ×2.2, with the backdrop as the reference plane.
- **The lenticular character:** the 16-view print and lens lookup. That is the stepped view changes, the drifting
  curtain, the slight horizontal softness and the crosstalk ghost.
- **The lens appearance:** the per-lens cap glints (too faint so far).

## 9. What is missing, and what to test next

1. **Wider span with more layers of reference.** The parallax is honest but small. Try ±10-12° with 24 views, plus a
   *near* element in front of the key plane (a thin foreground frame edge or floating dust motes) and a far plane.
   Then the face sits between layers moving in opposite directions: the lenticular "stacked" read without
   damaging the face.
2. **A view-dependent representation for the disparity we cannot show.** Exaggerate the per-view disparity (B1 ×3
   on the muzzle only, region-weighted) so a single eye's motion parallax carries more of the depth. Optionally
   offer an anaglyph or side-by-side stereo output of the same views, to check what binocular depth would add on a
   stereo display.
3. **The lens:** a parameter pass on the glint window (§6).
4. **The asset:** the minimum change is a back volume and separate eyeballs (02 §3.1 steps 2 and 5), to allow a
   wider span and an independent eye depth. Not a replacement.
