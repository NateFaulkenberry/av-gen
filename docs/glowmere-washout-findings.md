# The washed-out Glowmere picture: what is attributed, and what is not

**Written:** 2026-09-15. All arms interleaved in single GPU-lock sessions at 1280×720,
`examples/world/glowmere-valley-2.scene.json`, `range 30:33`, scene's own camera unless stated.
Instruments: `tools/image_stats.py` and the new `tools/chroma_speckle.py`.

## It is not the directed camera

That was the report — "Glowmere's directed camera renders washed out" — and it is wrong. The
scene's own camera is washed out too. Four frames from each arm, same seconds, same session:

| | mean | rms contrast | p01 | p50 | p99 | shadow frac |
|---|---:|---:|---:|---:|---:|---:|
| directed | 0.385 | 0.087 | 0.259 | 0.381 | 0.655 | 0.0001 |
| scene camera | 0.366 | 0.080 | 0.141 | 0.371 | 0.608 | 0.0084 |

Both sit inside a narrow band with **no blacks and no whites** — 98% of pixels in the midtones.
The directed camera is *worse* (the black floor lifts from 0.14 to 0.26) and the reason is ordinary:
a continuous take stands further from its subjects, and there is more atmosphere between the camera
and the world. That is the atmosphere behaving correctly, not the director misbehaving.

## Finding 1: the lifted black floor is volumetric scattering, and the desaturation is fog

Single frame at t=30, every arm non-vacuous (distinct file hashes):

| arm | p01 | p50 | p99 | rms contrast | saturation |
|---|---:|---:|---:|---:|---:|
| current | 0.0452 | 0.3684 | 0.7058 | 0.1037 | 0.486 |
| `fogDensity` halved | 0.0452 | 0.3426 | 0.7229 | 0.1059 | 0.515 |
| `fogDensity` 0 | 0.0452 | 0.3283 | 0.7284 | 0.1084 | **0.537** |
| `volumeDensity` 0 | **0.0062** | 0.3869 | 0.7322 | 0.1156 | 0.486 |
| `volumeDensity` ×¼ | 0.0143 | 0.3827 | 0.7255 | 0.1124 | 0.487 |
| both off | **0.0062** | 0.3390 | 0.7548 | **0.1173** | **0.549** |

They are two separate effects and each owns one symptom:

* **`volumeDensity: 0.006` owns the black floor.** Turning it off drops p01 from 0.0452 to 0.0062 —
  a **7.3×** difference. Fog does not move p01 at all, at any density.
* **`fogDensity: 0.0055` owns the desaturation.** Turning it off lifts mean saturation from 0.486 to
  0.537 (+10.5%). Volume does not move saturation at all.

Neither is a bug. Both are authored values in `environment`, doing exactly what they say, and the
choice of how much atmosphere a bioluminescent valley should have is art direction. **This is a
decision for a person, not a defect to fix.** A middle setting (`volumeDensity 0.0015`,
`fogDensity 0.0035`) measured p01 0.0143, contrast 0.1129, saturation 0.510 — most of the blacks
back, most of the atmosphere kept.

## Finding 2: there is severe RGB fringing, and I have not attributed it

Visible in the renders and not subtle: dense vegetation at middle distance and the water sparkle
carry violent per-pixel red/blue/green speckle. **Mean saturation cannot see this** — a frame
speckled with red and blue pixels has the same *mean* saturation as a clean one — so
`tools/chroma_speckle.py` measures it directly: luminance is discarded, and what is reported is how
far each pixel's opponent chroma `(R−G, G−B)` sits from its four neighbours' mean. No real surface
is red on one pixel and blue on the next.

Baseline at t=31: **3.52% mean chroma step, 20.7% of pixels over 0.06.**

Arms, all at the same second in interleaved sessions:

| arm | speckle % | over % | reading |
|---|---:|---:|---|
| baseline | 3.5246 | 20.666 | — |
| `--disable worldeffects` | 3.5246 | 20.666 | **identical — vacuous**; world effects are inert at this view |
| FXAA off (`antialias 0`) | 4.0370 | 21.958 | **worse by 14.5%** — FXAA is *suppressing* it, not causing it |
| `chromaRetention 0` | 3.4850 | 20.611 | −1.1%, marginal |
| bloom off | 3.5702 | 20.907 | +1.3%, marginal |
| all per-instance hue variation zeroed | 3.5455 | 20.723 | **no change** |

So it is **not** FXAA, **not** the world effects, **not** bloom, **not** `chromaRetention`, and
**not** per-instance hue variation — which was my hypothesis, stated in advance, and refuted. The
artifact is produced inside the scene pass, before post.

Two controls that did not settle it, reported rather than dressed up:

* **Supersampling made the metric worse** (2× render box-downsampled: 3.79 against 3.52 native).
  That would ordinarily exonerate undersampling, but the comparison is confounded — FXAA runs at the
  render resolution, so the 2× arm's blur is halved in output-pixel terms. It is not evidence either
  way. (It does match the project's earlier supersampling refutation, item 4 of the Level 3 list.)
* **The first frames are ~20% worse** (4.23% at frame 0 falling to 3.31% by frame 120), but the
  camera is moving, so that decay is confounded with a change of content — frame 20 goes back up to
  4.18%. There is a hint of a temporal component and it is not established.

**I am not claiming a cause.** The next arms worth running are inside the scene pass rather than
after it: alpha-tested foliage coverage, the emissive path for the bioluminescent plants, and the
water shader's sparkle — the last two are where the speckle is visually worst. Editing `water.wgsl`
on a hypothesis is what cost three rounds on the anamorphic comb, so the per-term scene arms come
first.

## What this changes about the Level 3 list

Item 2 asked what FXAA's 20% share of the flicker metric should be done about. This says: **not
removed.** On this view, turning FXAA off makes the visible chroma artifact measurably worse. Any
proposal to drop FXAA at the offline tier has to answer that.
