# ADR-1640: Refraction reads a spectrum when a material disperses

- Status: Accepted (eng/refraction-dispersion)
- Extends ADR-1603 (one refraction phase). Adds a `QualitySettings`/`LiveQualityRung` lever beside its two. Asked for by
  the Living Prism aurora valley's offline pivot (`docs/prototypes/living-prism/17-offline-pivot.md` on the scene
  branch: "chromatic dispersion if achievable").
- Code:
  - `src/scene/scene_types.hpp` (`Refraction::dispersion`), `src/scene/material_optics.{hpp,cpp}` (the block's
    `"dispersion"` key, written only when non-zero; the `material/refraction/dispersion` parameter, 0..2, soft 0..0.6);
  - `src/rendering/optics_pack.hpp` (`packRefraction(m, dispersionAllowed)`: lane0.w, clamped to 2, 0 when not
    allowed), `src/rendering/procedural_renderer.{hpp,cpp}` (`setRefractionDispersion`, set by `scene_renderer.cpp`
    from `QualitySettings::refractionDispersion` before the object slots are packed);
  - `shaders/procedural.wgsl` (`fs_proc_refract`, `DISPERSION_TAPS`, `dispersionWeights`), `shaders/common.wgsl`
    (the lane's comment);
  - `src/rendering/render_quality.hpp`, `quality_policy.hpp` (offline: always on), `src/app/interactive_resolution.
    {hpp,cpp}` (the ladders, ceilings, the priority ladder's `post` group, lever and quality arm `nodispersion`,
    ceiling key `refractionDispersion`).
- Tests: `tests/rendering/test_refraction_gpu.cpp` (`[adr1640]`): a tilted clear slab over a sharp white/black edge is
  grey through the single read and fringes in colour at the edge only with dispersion; the ladder off is the
  dispersion-0 frame, hash for hash. Hidden: `[baseline]` (dispersion 0 against the shaders of a commit before this one,
  for this fixture and ADR-1603's crystal), and two `[measure]` cases (banding, cost).
  `tests/unit/test_material_optics.cpp`, `tests/unit/test_interactive_resolution.cpp` (`[adr1640]`).

## Problem

ADR-1603 reads the refraction copy once, at one normal offset, so every wavelength bends alike. Faceted glass seen
against a bright edge shows no colour fringe, and the scene agent's crystals read as tinted glass rather than prisms.

## Decision

**A per-material amount.** `"refraction": {..., "dispersion": d}`. 0 (the default) is ADR-1603's single read,
unchanged: the packed lane is 0 and the shader's branch is not taken. A block without the key writes the four keys it
wrote before.

**A spectrum, not three reads.** With d > 0, `fs_proc_refract` reads the copy at seven wavelengths evenly from 650 to
450 nm. Each read is along the same normal offset, scaled by Cauchy's `1/lambda^2` about 550 nm, normalised so the two
ends are `d` of the offset apart (red reads least far, blue most):
`uv_k = screenUv - offset * (1 + d * k(lambda))`, `k(650) = -0.365`, `k(450) = +0.635`. Each read lands in red, green
and blue by three Gaussian weights (610, 545 and 465 nm), normalised per channel, so a grey backdrop stays grey and a
white edge splits into a spectrum. The depth behind, the absorption path, Fresnel and opacity are the single read's,
as before.

**The ladder.** `QualitySettings::refractionDispersion` (true; offline always). The three live ladders turn it off
from Medium (the refraction phase itself stays to Low), and so does the priority ladder's `post` group from its first
step. It is a ceiling like the others, with lever `nodispersion` and ceiling key `refractionDispersion`. Off, a
dispersive material packs dispersion 0 and draws the single read. It is not counted in `effectChanges`. It costs only
on refractive pixels, and nothing in most scenes, so counting it would promise Medium a quarter more of a gain.

**The water** keeps its own `refraction` (a world-space offset in `water.wgsl`, ADR-1286) and gets no dispersion here.
Its offset is not ADR-1603's normal-offset UV, so sharing the parameter would not be the small change the brief allowed.

## Evidence (M2 Max, AC power, Release; honest timestamps, ADR-1600; media `~/Desktop/av-gen-review/87-dispersion/`)

**Identity at 0.** The hidden `[baseline]` case renders the dispersion fixture and ADR-1603's crystal fixture with
these shaders, and again with the shaders of 0b0e2634 (`git archive 0b0e2634 shaders`). The image hashes are equal.
The object uniforms of a material without dispersion are the same bytes either side (a unit test checks the packing),
so the shader is the only difference.
- Control: the same case against a baseline whose Fresnel f0 was edited from 0.04 to 0.06 fails both comparisons.
- The `[adr1640]` case: dispersion 0 has a largest |R-B| <= 4 across the edge; with the lever off, a dispersion-0.6
  frame is the dispersion-0 frame, hash for hash.

**Banding** (hidden `[measure]`: dispersion 1.5, 480 px, one row across the fringe):

| variant | fringe | distinct colours (6 bits/channel) | largest channel step |
|---|---|---|---|
| three reads (R, G, B at their own offsets; the first version) | 42 px | 6 | 195 |
| five spectral taps | 41 px | 9 | 171 |
| **seven spectral taps (shipped)** | 41 px | **13** | 155 |
| seven taps, per-pixel jittered wavelengths (rejected) | 34 px | 34 | 171 |

- Three reads give flat cyan and blue bands with a full-height step between them. Seven taps double the colours.
- Jittering the taps per pixel (interleaved gradient noise) makes the spectrum continuous. At the scene's 2x
  supersample, though, the jitter shows as a fixed screen-space dither over the fringes
  (`rejected-jitter-fixed-vs-jittered-taps.jpg`), and live it would be raw noise. Fixed taps are deterministic.

**Cost** (hidden `[measure]`: 4K, a slab over about 40% of the frame, 8 interleaved reps of 120 frames, the scene
label's per-rep delta against dispersion 0):

| variant | scene-label delta, median of reps | range |
|---|---|---|
| three reads | +0.050 ms | 0.040-0.059 |
| **seven taps** | **+0.149 ms** (a second run: +0.163) | 0.142-0.171 |

- At 1080p that is about +0.04 ms over 40% of the frame.
- A first attempt at 1080p over 4 reps could not resolve it: medians wandered 2.4-3.9 ms with the clocks.
- On the Living Prism's 0.30 s offline frame it is invisible.

**The aurora valley** (the scene branch's `aurora-valley-offline.json`, copied unchanged except dispersion on its
eight refractive crystals, `--tier offline --supersample 2`, 1080p, t = 40 s and 100 s):
- 1.8% of pixels change by more than 8/255, all on the crystals and their bloom (the diff mask).
- With dispersion 0.2-0.5 the refracted edges and cracks behind a crystal gain spectral fringes
  (`aurora-t40-hero-crop-0-0.2-0.5.jpg`).

## Consequences

- The scene agent can author `dispersion` per crystal material. 0.1-0.3 is the useful range at the valley's strength
  0.12. The spread grows with `strength x dispersion`.
- A thin, bright line behind a strongly dispersive surface splits into up to seven coloured copies once the spread is
  wider than about seven pixels (visible at 0.5 in the aurora crop). Lower dispersion, or a softer backdrop, keeps them
  merged.
- Seven reads for one on refractive pixels when on. Live, from Medium down, the single read.
- No new binding, pipeline or pass. The lane was ADR-1603's unused lane0.w.

## Rejected

- **Three reads (R, G, B).** The brief's minimum, and the first version. Its fringes are two flat bands with a hard
  step (above). Seven taps cost 0.1 ms more at 4K and double the colours.
- **Jittered spectral taps.** Smoothest per pixel, but a visible screen-fixed dither at 2x supersample, and noise live.
- **Per-channel IOR fields** (`iorR/G/B`). Three numbers where one spread suffices. The normal-offset model has no
  physical IOR to feed them.

## Revisit triggers

- The banding of thin features matters in a shot: try jitter with temporal accumulation (motion-blur subframes, or a
  TAA if one arrives), or more taps offline only.
- Crystal seen through crystal (ADR-1603's single copy) lands: dispersion should follow the second layer.
