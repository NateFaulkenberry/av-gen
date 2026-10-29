# Canyon: cirrus follow-up (bands, reflected cirrus) and the shadow jitter

This covers the owner's three items after accepting ADR-1864. The lattice, wire, contour and revelation effects are
still on HOLD and were not touched.

Media is in `~/Desktop/av-gen-review/101-strange-attractor/canyon-cirrus2/`:
- `00-cirrus2-audio-reactive-46s-1080p30-with-sound.mp4`: the Phase 2 audio project, with sound.
- `01`–`04`: before/after at 9, 22.5, 33 and 40 s. Before is the `canyon-cirrus` clip.
- `shadows/01-wall-26.4s-five-frames-TOP-before-BOTTOM-after.png`.
- `shadows/02-wall-26s-LEFT-before-RIGHT-after-1080p-crop.mp4`.
- `05-perf-flicker-shadows.txt`.

## 1. Thinning the wide sky (ADR-1865, bands)

The even layer's single noise scale, compressed toward the horizon by the plane projection, read as hatching. A
low-frequency band field now gathers the streaks into a few broad bands of uneven width, spacing and brightness.
- Settings: `bands` 1, `bandWidth` 5 km, `amount` 1, `coverage` 0.5.
- The fibrous detail inside each band is unchanged.
- I compared six variants (band width 5, 6, 7, 9 and 14 km; three seeds) at 9, 33 and 40 s before choosing.

## 2. The river reflects the clouds (ADR-1865, water)

**How the water got its sky:**
- The IBL cube at a roughness mip.
- ADR-1650's sky mirror, which reads the frame copy, but only where the reflected direction's vanishing point is on
  screen. For a river under a camera looking down a canyon it mostly is not.
- SSR, for what is on screen.

The cube carried 15% of the layer, which is a faint shimmer, because a cube face cannot hold a streak.

**What changed:** the water now evaluates the sky's own cirrus function along its reflected ray.
- It completes the cube's share exactly: `k = c(1 − s)/(1 − sc)`, so no streak is counted twice.
- The ripples are eased 60% toward flat, because the finest ripples shattered the streaks into sparkle.
- The streaks fade where they would alias, and on rough water.
- The cube keeps its 15% share for the diffuse lighting and rough surfaces.
- The GPU test checks that on a glass sheet the reflected streaks fall where the CPU twin puts them along the mirrored
  ray.

At 9 s the reflections show as soft cloud patches bent by the swell. Under the canyon's view angles most of the river
reflects sky low over the far rim, so the streaks are short there.

## 3. Shadow jitter on the walls (ADR-1866)

**Measure.** The whole-frame metric cannot see a crawling edge, so I built three local ones:

| Measure | What it is | Verdict |
|---|---|---|
| (a) Tile jitter | Per-24-px-tile second difference | Dominated by parallax |
| (b) Sign-reversal energy, shadows on minus off | Per tile; a pixel that brightens then darkens in consecutive frames | Dominated by water and edge sampling |
| (c) Reversal on the shadow term alone | From the renderer's own visibility AOV (`--aov shadow`); nothing else in the picture can reach it | The one used |

**Worst moments.** 5.1, 5.6, 6.0, 20.1, 26.5, 27.0 and 28.9 s.

**Frozen camera.** Nothing that feeds the shadow varies with time: the sun is fixed and no noise is temporal. Rendering
the same instant repeatedly is bit-identical (verified for 18.5 s in `08`). The jitter is therefore entirely
projection: it is caused by the camera moving.

**Candidates.** Variants were run on the worst windows and scored with (b) and (c):

| Candidate | Variant | Effect |
|---|---|---|
| PCF/PCSS screen-space kernel rotation | Fixed rotation; world-stable rotation | None |
| PCSS | Plain PCF | Slightly worse |
| Cascade blend band | 0.12 → 0.30 | None |
| Cascade count | 4 → 2 | −5% |
| Shadow range | 3000 → 1500 m | ±3% |
| Atlas resolution | 4096 → 1024 | No worse, so the speckle is not resolution-limited |
| Normal-offset bias | 1, 4, 8, 16 or 30 m | 1 m −10%, 30 m +40%: larger offsets leak more |
| Contact shadows | Already off; the arm changed nothing | None |
| ADR-1858 caster reach | Depth only; does not move the XY grid | n/a |
| Cascade fit | **Per-frame dump of every cascade** | **The cause, below** |

**The cause.** I printed every cascade's fit per frame (`AVGEN_SHADOW_STATS`, with the throttle removed locally). With
a constant field of view:
- the nearest cascade's half-extent went 118.6, 111.4, 110.9, 119.3, 118.7, 110.7, 110.8, 127.7 m;
- the farthest went 2906, 2726, 2714, 2919, 2906, 2710, 2711, 3128 m.

The texel changed up to 15% from frame to frame, so the shadow map was re-rasterised at a new scale every frame and
every edge's staircase was re-cut. Texel snapping cannot hold a grid whose pitch changes. The sub-frustum's corners
were unprojected through the float `invViewProj` with a far plane tens of kilometres away: ADR-1807's imprecision,
in the shadow fit.

**The fix (ADR-1866, generic).** The cascades are fitted to the camera's basis in double. After the fix the nearest
cascade is 112.00 m and the farthest 2742.25 m on every frame. The unit test covers it: 3 km from the origin with a
60 km far plane, the matrix path wanders 4.3 m and the basis path 0.

**What remains.**

| Measure | Before | After |
|---|---|---|
| Wall window (c): edge pixels flipping per frame | 271 | 254 |
| Wall window (c): mean reversal | 0.0166 | 0.0162 |
| 1080p wall clip: edge flips per frame | 1320 | 1233 |

The cascades no longer change scale, so the staircases are fixed to the rock. The remaining flips are not jitter: they
are the camera's pixels sweeping over shadow detail that is stable on the rock. The stairs of the far cascade, whose
1.34 m texels are stretched about four times by the 14° sun, are the coarse part of that detail.

Making them finer is a resolution decision, not a jitter fix:
- tighter cascade splits for the far walls;
- a fifth cascade, against the engine's four-cascade limit;
- or a less grazing sun.

I did not blur them away. The before/after crop and clip show the edge holding its shape on the rock.

## Cost

In `05-perf-flicker-shadows.txt`, at 1080p ultra:
- **The water's cirrus reflection:** about 1.5 ms of the scene pass (43.5 → 45.0 ms); GPU median 57.4 → 59.2 ms. The
  GPU was shared with another agent's suite between runs, so read this as ±0.5 ms.
- **The shadow fit:** CPU only (eight corners in double per cascade), with no GPU cost.

Flicker (coordinator's metric) on the new clip: median 0.004, p90 0.014, p99 0.044, max 0.15. The `canyon-cirrus`
clip measured 0.004 / 0.015 / 0.045 / 0.16.
