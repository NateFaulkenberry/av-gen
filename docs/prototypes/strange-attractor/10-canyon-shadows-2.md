# Canyon shadows, pass 2: the stair-steps on the walls

The owner still saw "jitter" on the canyon walls after ADR-1866. This pass follows the coordinator's order (split
scheme, then resolution, then a fifth cascade), stopping at the first step that fixes it. The sun stays at 14°. The
lattice was not tuned, but it is affected; see below.

Media is in `~/Desktop/av-gen-review/101-strange-attractor/canyon-shadows2/`:
- `00-shadows2-audio-reactive-46s-1080p30-with-sound.mp4`.
- `01`–`04`: four consecutive frames at each worst window (5.4, 20.3, 26.3, 28.6 s), before on top and after below.
- `05-wall-25.5-28.5s-LEFT-before-RIGHT-after-1080p-crop.mp4`.
- `06-LATTICE-CHANGES-11.5s-before-after.png`.
- `07-measures.txt`.

## Measure

This uses the renderer's own sun-visibility pass (`--aov shadow,depth`) on the worst windows (5–6, 20, 26–27,
28–29 s), and reports three things:
- **edge flips:** pixels at a shadow edge whose visibility reverses by more than 0.1 in consecutive frames;
- **the same, by cascade:** using the depth pass;
- **raggedness:** the length of the 0.5 visibility contour against the length of the same contour after a 2 px blur.
  A staircase is longer than the edge it approximates; a smooth edge scores close to 1.

## Steps

| Step | Variant | Raggedness (5 / 20 / 26 / 28 s) | Edge flips | Seen |
|---|---|---|---|---|
| — | as it was (lambda 0.85, 3000 m, 4096) | 1.74 / 1.39 / 1.58 / 1.44 | 9.6 / 2.6 / 4.0 / 11.9% | teeth on every crest |
| 1 | lambda 0.6 | 1.71 / 1.42 / 1.59 / 1.45 | ≈ | no change |
| 1 | lambda 0.5, 2000 m | 1.61 / 1.41 / 1.56 / 1.35 | 6.9 / 2.4 / 3.2 / 8.2% | no change |
| 1 | range 1500 m | 1.53 / 1.48 / 1.61 / 1.36 | 3.8 / 1.4 / 2.6 / 7.0% | no change |
| 2 | 6144 | 1.67 / 1.31 / 1.53 / 1.39 | ≈ | no change |
| 2 | **8192** | 1.63 / 1.28 / 1.50 / 1.38 | ≈ | **pixel-identical teeth** |
| — | **watertight RTIN, 1 m, tight (ADR-1867)** | **1.38 / 1.23 / 1.29 / 1.19** | 9.3 / 2.5 / 3.5 / 10.6% | teeth gone, cracks gone |

**Why steps 1 and 2 could not work.** A stair-step that does not change when the shadow map doubles in resolution is
not in the shadow map. It is in the caster:
- the faceted silhouette of a 2 m triangulation along a curved crest;
- stretched about four times by the grazing 14° sun into teeth.

A 1 m grid at a tighter tolerance smooths the silhouette. A 2 m grid at any tolerance does not, because the facets are
the grid's own diagonals.

**The cracks.** Dark-blue dashes crawled along crests: 2,965 px in one frame, and `background` set to magenta turned
them magenta. They were T-junctions from the RTIN's depth-first error propagation, which read a shared midpoint before
the neighbour had written it. The new watertightness test counted 269,303 open edges before and 0 after. ADR-1867 has
the details.

## Cost

1080p ultra on an M2 Max. The scene pass is much faster than in earlier notes because of main's compiled material
programs (ADR-1812..1817).

| | GPU median | Scene | Shadows | Mesh |
|---|---|---|---|---|
| Before | 26.7 ms | 12.8 ms | 2.8 ms | 8.27M triangles, 238.6 MiB |
| Step 2 at 8192 | 30.1 ms | 12.9 ms | 5.9 ms | Atlas 8 × 8192² × 4 B = **2 GiB** (from 512 MiB) |
| **After** | **24.3 ms** | 11.7 ms | 1.9 ms | 2.92M triangles, 78.7 MiB; bake 5.2 s (was 4.3) |

The fix makes the frame cheaper. The 8192 atlas is not used.

## Remaining edge flips

Edge flips barely move with any step. They are the camera's pixels crossing shadow edges that now hold still on the rock
(05, the wall clip), not jitter. The worst remaining band is 258–627 m at 28 s (23%), where the camera rises over a
ridge and every edge in that band sweeps fast.

## The lattice changed

The mesh wire draws the real triangles, so the corrected, finer-near-the-walls mesh changes the lattice (06):
- denser and more regular on the near walls;
- larger, cleaner triangles on the flats, where the old leaf-level error inflation had over-refined in blotches.

The lattice is on hold for the owner, so it was not retuned. If the old density is preferred, `meshSpacing` 2 restores
coarser near triangles, but brings back the teeth.

## Flicker (coordinator's metric)

On the new clip (before the water fixes): median 0.004, p90 0.015, p99 0.041, max 0.13. After them: see `07-measures.txt`. The `canyon-cirrus2` clip measured 0.004 / 0.014 /
0.044 / 0.15. The offline 46 s render at 2x supersample took 293 s, down from 433.

## Water flicker at the banks (ADR-1868)

Media is in `canyon-shadows2/water/`:
- `01`–`03`: four frames at 14.0, 16.9 and 20.8 s, before on top and after below.
- `04-bank-16-19s-LEFT-before-RIGHT-after-1080p-crop.mp4`.
- `05-measures.txt`.

**Measure.** Luminance flips per frame among water pixels within 4 px of rock. The mask is a same-camera render with
the river painted flat green.

**Frozen camera.** 0.00–0.01% flips: view-dependent, not noise.

**Floor.** The green render itself flips 4.8%: the waterline's geometric edge stair-stepping. Depth precision was
ruled out (a 10× near plane changed nothing). This is the floor any edge has without MSAA; the 2x supersampled final
reduces it.

**Causes:**
1. **The river carried the engine's default 0.55 foam band.** The attractor's water block silently dropped every water
   key outside a fifteen-key list. The foam band is 0.42 m of depth, which far away is a one-pixel bright line hugging
   every bank and crawling in dashes. Fixed by reading every key, refusing unknown ones, and setting `foam` 0.
2. **ADR-1865's reflected cirrus, ours.** The ripples swing the reflected ray across a streak within a pixel. The water
   now measures the reflected ray's spread across the pixel; where the ripples dominate, the cirrus is looked up on a
   flat mirror, and the SSR hit is blurred by the same spread. The GPU test (derived control) measures 2,658 → 595 sign
   reversals.

**Ruled out:**

| Candidate | Finding |
|---|---|
| Sky mirror | No effect |
| Specular glint | No effect |
| Depth-based shore fade | No effect, even widened to two pixels |
| z-fighting / depth precision | Near plane ×10: no effect |
| SSR hit/miss | Removing SSR is worse; four rays is worse |
| Reflected shadows | Frozen camera is clean, and shadows don't move with time |

**Result.** Full flight bank flips 6.48% → 5.33%, against the 4.77% floor: the excess fell by two thirds. Open water
2.85% → 2.17%.

**Cost.** GPU 26.4 → 26.2 ms (no measurable cost).

**Still visible.** Sparse light specks along the waterline sliver (01–03, both rows). They are in the green-mask render
too, so they are the geometric edge, not shading.
