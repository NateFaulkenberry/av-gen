# Phase F — volumetric scalability

Agent `vol`, 2026-09-13. Every number here was taken by me on an M2 Max (Dawn/Metal, release,
1280×800, 120 frames with the first 12 discarded) under `tools/gpu-lock.sh`, with both arms
interleaved in one process (ADR-113, ADR-117). ADRs 139–143 carry the decisions; this file carries
the numbers, their conditions and their noise floors. Revisions `bfed449` and `ae3248c`.

## 0. The floor, established before anything was measured against it

ADR-113 recorded Constellation's within-session GPU spread at **36.97%**. Measured again today over
four separate null and paired baselines in one session, its floor was **13.16%, 21.77%, 3.82% and
34.96%**. It is not one number, not even within a session. Every row below quotes the floor its own
run computed.

The null A/B on Constellation reported **+0.88% GPU against a 13.16% floor — correctly not a
result**, which is the instrument working. **Glowmere's null in a later batch reported "A RESULT" at
−2.39% against a 2.00% floor, which is the instrument failing**, and the two Glowmere rows it
invalidates are marked below. See ADR-141.

## 1. The split (task F2)

Both volume passes marked themselves `"volume"`, so the march and the composite were one number.
They are now `volume.march` and `volume.composite` (ADR-140).

| | Constellation | Glowmere |
|---|---|---|
| `volume.march` | **3.867 ms** | **0.655 ms** |
| `volume.composite` | 0.066 ms | 0.066 ms |
| whole frame GPU p50 | 6.226 ms | 13.435 ms |
| volume share | **63%** | **5.4%** |

**0.066 ms is one timestamp tick** (the counter's period is 0.065536 ms). The composite is *at or
below* the instrument's resolution on both scenes, so it is not a measurement and the composite is
not a target. Six other Constellation passes report the same single tick.

**The denominator check the wave-2 preamble asks for.** Phase A: 2.29 of 3.60 ms = 64%. Now: 3.93 of
6.23 ms = 63%. Both absolute numbers are inside Constellation's noise and cannot be compared; the
*share* held, because both terms moved together inside one session. Glowmere's volume share
re-derives from 3.5% (0.66 of 18.61) to **5.4%** (0.72 of 13.43) — the volume did not grow, the
frame shrank.

## 2. The axes (task F1), and what they cost

`QualitySettings::volumeResolutionScale` and `volumeStepScale` (ADR-139). "march ratio" is the arm's
`volume.march` median against its own paired baseline block's — the statistic ADR-141 argues for on
a scene whose frame median is not one.

| arm | scene | march ratio, per pair | whole-frame GPU | that run's floor | verdict |
|---|---|---|---|---|---|
| `volumefull` (1.0) | Constellation | 2.45, 1.57, 1.91 | −55.8% | 21.8% | a result: it costs |
| `volumequarter` (0.25) | Constellation | 0.35, 0.43, 0.61, 0.51 | **+40.0%** | 13.0% | **a result** |
| `volumesteps` (0.5×) | Constellation | 0.59, 0.58, 0.64, 0.64, 0.64 | +26.8% | 35.0% | not a result |
| `volumepreview` (both) | Constellation | 0.27, 0.26, 0.26 | **+51.0%** | 3.8% | **a result** |
| `volumefull` | Glowmere | 3.00, 3.00, 2.75 | −3.1% | 6.7% | not a result |
| `volumequarter` | Glowmere | 0.60, 0.50, 0.55 | +2.7% | 2.0% | withdrawn — bad null |
| `volumesteps` | Glowmere | 0.60, 0.50, 0.60 | +3.4% | 2.9% | withdrawn — bad null |

`volumesteps` on Constellation is **not a result** by the whole-frame statistic and is reported as
such, although all five pair deltas were positive and the march ratio was tight (0.58–0.64). The
frame statistic could not certify it; the pass statistic and the frame statistic do not contradict.

### The finding

**The march is strongly sublinear in resolution: a sixteenth of the pixels costs about half the
time,** and four times the pixels costs 1.6–3.0×, never four. **So roughly half the `volume.march`
interval is not pixel work** — pass setup, the target's clear and store on a tile-based GPU,
pipeline and barrier cost, fixed latency before the first wave retires. This is the bound that
decides F3.

**The two axes multiply.** 0.47 × 0.61 = 0.29 predicted for `volumepreview`; 0.26–0.27 measured on
Constellation, 0.25–0.27 on Glowmere. Two scenes, within 10% of the product.

## 3. The visual gate (§50)

`--quality-arm` (ADR-142) was built for this: an arm could previously be reached only inside `--ab`,
which captures nothing. Each capture's log line carries `volumeTarget=WxH` beside `volumeSteps`, so
it proves the reduction it was taken under.

Frames captured under each arm and differenced against the shipped Realtime frame, amplified 16× and
inspected at full size:

| arm | Glowmere: max Δ / pixels >1 | what it looks like |
|---|---|---|
| `volumesteps` | 23 / 0.06% | **concentric banding across the smooth sky**, almost nothing at edges |
| `volumequarter` | 24 / 0.39% | that banding *plus* bright fringing along the mushroom's gills and plant edges — the depth-aware upsample failing on thin geometry |
| `volumepreview` | 22 / 0.62% | both, together |
| `volumefull` | 23 / 0.30% | the reference: the same two artefacts, removed |

**Two parameters, two distinct artefacts**, which is the §50 evidence that ADR-139's axes deserved
to be two rather than one dial.

**Constellation shows almost nothing**: 0.13% of pixels differ at any arm, and the differences sit
entirely on the particle sprites — the fog body is a wide smooth glow with no silhouettes to
resolve, so quarter resolution is visually near-free on the scene where it is worth the most. The
diff image is the particle field and nothing else.

**The shipped picture does not move.** The Realtime capture is **byte-identical** (`cmp`) on both
scenes to one taken from a binary built at `ccb5d5c`, before any of this. Both binaries built, both
frames captured under the lock.

## 4. Temporal reprojection (task F3) — rejected

Full argument in ADR-143. The short form: reprojection amortises march work; §2 measured that only
about half the march *is* pixel work; so its perfect-case ceiling is ~2 ms of Constellation's frame
— **which is what `volumeResolutionScale = 0.25` already delivers today** (+40.0% frame GPU, four of
four pairs, against a 13.0% floor), from a tier-table default with no history buffer, no velocity
dependency and no determinism surface. It would additionally break ADR-035's tier contract (offline
may take no temporal shortcut per §5.9, so preview and final would differ by a reconstruction) and
re-open the `SYM-TERRAIN-1` defect class. ADR-143 states what would reopen the question.

## 5. Found, not fixed

- **The A/B harness's fixed 2% GPU floor is too low for Glowmere** in a session whose baseline
  spread reaches it: a null reported "A RESULT" at −2.39% against a 2.00% floor. `render_stats.cpp`
  is not this phase's to change. ADR-141 records it.
- **`volume.composite` cannot be measured by this instrument**, only bounded by it. Any future claim
  about the composite needs a Metal frame capture, which §3.4 already lists as the missing
  capability.
- **The march's fixed half is not attributed.** §2 measures that it exists and how large it is; it
  does not say how much is the tile store, how much is pass setup and how much is launch latency.
  Separating them needs the same Metal capture.
