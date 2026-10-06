# The Astral Forge: Iteration 3

The owner approved iteration 3 and plans to merge to main after it. The rules are unchanged, and the track is
*Trench* throughout. Media is in `~/Desktop/av-gen-review/37-astral-forge/iter3/`:

- `clips/`: T06 with audio, T07, T01;
- `production-vs-prototype/`;
- `dev/`.

## 1. The production look (ADR-1150 to ADR-1153)

Built by an engineering agent, with commits `f502429a`, `a0d33817` and `df282826`. Each ADR is in `docs/decisions/`.

| ADR | What | Key result |
|---|---|---|
| 1150 | Density surface quality | Banding came from a march with no refinement. Blockiness came from normals taken at 1/6 of a voxel on trilinear density. The fixes: the ray clipped to the volume, a coarse 8³ occupancy grid for empty-space skipping, steps clamped to 0.65-3 cells, 7 bisections, and B-spline normals. All of it is compiled into the density variant only. Steps per ray fall from 52.7 to 10.3. A frame-filling density surface drops from 30 ms to 13.2 ms. The mid-frame mask gets slower, 7.9 → 9.6 ms |
| 1151 | Reflection-only bands (`environment.bands`) | The prototype's strips and soft box are added to `pbr_shade`'s ambient specular. They are never drawn in the background. They replace the constant hemisphere ambient when there is no environment map, which is what made iteration 2's production chrome purple. +0.8 ms on a sphere covering about 40% of the frame |
| 1152 | Guilloché engraving (`material.engraving`) | The prototype's rosette, contour and engine-turned families, in 4 octaves with footprint fade, cut as V-grooves. The line direction becomes the anisotropy tangent, and the grooves add a grating term. SDF materials only (the procedural uniform slot is full). The lines follow the object's transform but not its domain warps. +1.05 ms |
| 1153 | Glint flakes (`shape2d: "flake"`) | Flakes are dark unless they reflect a band. Bound flakes take the latent normal. +1.0 ms at 1M particles |

**Byte-identity:** 7 reference scenes show 0 differing channels against the pre-change binary. Focused tests pass:
10 new unit cases and 6 new GPU cases.

**Production against the prototype** (`production-vs-prototype/`; entity luminance matched to 0.18 linear):

| Pair | Luminance-histogram distance (Hellinger) | SSIM, entity box | Fine-detail share, prototype / production | Specular coverage | Colourfulness, prototype / production |
|---|---|---|---|---|---|
| T01 face | 0.112 (iteration-2 example: 0.263) | 0.13 | 0.29 / 0.27 | 5.9% / 4.4% | 16.9 / 11.3 |
| T02 metal | 0.099 | 0.16 | 0.14 / 0.20 | 8.3% / 7.0% | 18.4 / 12.8 |

**Verdict: the prototype cannot retire yet.**

- **What production now matches:** the tonal distribution, the detail statistics, the engraved and banded metal,
  and the dark-unless-glinting dust. Iteration 2 matched none of these.
- **What it still lacks:**
  - The anatomy. Its latents are hand approximations from capsules and boxes, with no ellipsoids or tapered
    cones, so the T01 face reads as a cartoon mask (`side-t01.png`).
  - Per-region temper and polish. It is about 30% less colourful.
  - Shards, heat, tendons, sharpness that varies over the face, and engraving that follows the domain warps.
- **The retirement list, in order:**
  1. ellipsoid and tapered SDF primitives (or a compiled port of the face latent);
  2. per-region film and polish;
  3. tendons and shards in the particle renderer.

## 2. Off-centre framing and scale cuts

- **Framing.** The subject sits on a third, and the side changes by phrase. LOW shots put it on the upper third.
- **Cuts within phrases:**
  - a held phrase cuts at its midpoint to an extreme close-up of one eye, or, from MICRO, to a wide shot;
  - a build phrase cuts for its last four beats to the eyes;
  - camera smoothing never crosses a cut.
- **Critic on T06** (`clips/t06-audio-trench.mp4`):
  - "Repeated composition" findings fell from **6 medium to 1 medium**, and visual coherence rose from 0.855 to
    **0.91**. Musical synchronisation stays 1.0; overall 0.96.
  - **Regression:** "camera shake" rose from 1 to **5 medium** findings (motion 0.87 → 0.79). In the close cuts
    the streaming dust fills the frame, and the global-motion estimator reads it as camera motion. The camera
    path itself is smooth within a shot.
  - Iteration 4 should slow the dust's screen-space motion in close-ups, or hold close shots for less time.

## 3. The Machine God without gyro rings

- The rings and the radial blade fan are gone.
- The god's appendages are now **its own faceted face, peeling**: three nested shells of the face plate, each
  offset outward and cut by a rotating angular window. They lift away like lamellae and turn against each other,
  and they open with the breath.
- It is field-born and anatomical: the god is made of copies of its own face, in depth.
- **Weakness:** at mid distance the face reads less clearly than with the rings, because the shells obscure the
  eyes (`dev/s34-machine-*.png`).

## 4. Collapse heat on the release front

- Heat is injected only on a shell expanding from the mouth at about 30 units/s through the collapse. Matter
  released anywhere else flies cold.
- The haze no longer carries heat at all; it was what tinted the whole cloud.
- The collapse frames (`dev/s34-collapse-8.45.png`, `dev/s34-t1-collapse.png`) are now a grey-steel spray with
  sparse warm sparks near the mouth, with no orange whole-cloud frame.

## 5. Half-resolution march with full-resolution shading

**How it works.**

1. Pass 1 marches at half resolution and stores the hit depth, the previous step and the haze.
2. Pass 2, at full resolution, reads the 3×3 half-resolution neighbourhood:
   - **no hit anywhere:** haze only;
   - **an interior hit** (all neighbours hit, depth spread under 1.5 cells, more than 25 cells from the lens): a
     short refine march on the cache from just in front of the nearest hit, then exact bisection, normal and
     shading;
   - **otherwise** (silhouettes, MICRO, the Chimera): the full march.

**Three measured corrections on the way:**

- A cache-only *normal* mottled MICRO frames: rgba16f is too coarse for a 0.01-unit difference. Normals stay
  exact.
- The cache-only refine shifted the finest engraving octaves in MICRO, a mean difference of 11 codes. MICRO
  (within 25 cells of the lens) now takes the full path, and the difference is 0.04.
- The **Chimera's latent is discontinuous** (it picks a face by angular sector). Refining it from a neighbour's
  depth landed on a different sector, so it always takes the full path.

**Image equivalence against the full-resolution march**, mean |Δ| in 8-bit grey:

| Shot | Mean \|Δ\| |
|---|---|
| T02 | 2.6 |
| T03 | 0.65 |
| T05 MICRO | 0.04 |
| T05 inside the mouth | 0.14 |
| T07 Chimera | 6.4 |

The T07 residual is unresolved: it persists even on the full path, and is probably the half-resolution haze.

**Cost**, p50 / p90 ms, 2M particles, 1080p, under the lock:

| Shot | Iteration 2 | Iteration 3 |
|---|---|---|
| T02 close | 20.3 / 22.2 | **14.1 / 15.3** |
| T03 fold close | 23.3 / 28.3 | **18.9 / 20.9** |
| T04 lattice | 21.6 / 24.2 | **19.4 / 22.5** |
| T05 MICRO | 23.9 / 42.9 | **18.6** / 46.8 |
| T05 inside the mouth | 38.9 | 40.3 (full path by design) |
| T07 Chimera close | 22.9 | 24.6 (full path by design) |
| T01 face | 13.4 | **12.0** |
| T06 formed | 17.2 | **15.8** |
| T07 Machine God | 11.6 | **10.4** |

**1080p60 in close-ups is not met.**

- Close-ups now run at 14-19 ms p50, against 20-24 before.
- Only T02 is under 16.7 ms.
- The extreme MICRO and Chimera shots are 25-40 ms, because the paths that keep them correct are the full march.
- Mid and wide shots stay under 16.7 ms.

## Merge preparation

- **The ADR-1143 stride benchmark (512 → 768 bytes): not material.**
  - Measured on Glowmere Valley 2 and Liminal pass 3, alternating before and after, under the lock.
  - CPU, GPU and per-pass times show no measurable difference within run-to-run spread (about 8-9 ms GPU,
    about 2 ms CPU).
  - Memory is +262,144 B, exactly 256 B on each of 1,024 fixed slots.
  - Per-frame uploads are at most +86-104 KB, with no visible effect on the submission CPU.
  - No change was made. Raw data is in the scratchpad `bench/`, and the method is in the agent's notes.
- **Merge:** `origin/main` (a470a241) is already contained.
- **Suites and the large-file check:** see the final report.
