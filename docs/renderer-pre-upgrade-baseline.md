# The pre-upgrade baseline

Taken 2026-09-13, immediately before the rendering engine upgrade, on Apple M2 Max / Dawn on Metal,
release build, nothing else running. **This document and the committed frame snapshots beside it
exist because neither can be made afterwards.**

## What is baselined, and what deliberately is not

**Not pixels.** An upgrade changes them by design, so an image baseline would be discarded on the
first day and would have told you nothing in the meantime.

**The state the renderer derives from a scene**, which must survive: an object is in the same place,
the same size, made of the same things, and drawn or not drawn for the same reason. That is a
contract a new renderer has to meet, and it is what
[`examples/qa/baselines/*.snapshot.json`](../examples/qa/baselines) records --
`[gpu][composition][forensics][baseline]` compares against them and reports differences as sentences
naming the object and the field. To adopt a deliberate change, run with `AVGEN_UPDATE_BASELINES=1`
and review the JSON diff in git; the diff is the change, in a review.

**Cost, with its conditions**, so "the upgrade made it faster" is a measurement rather than an
impression.

## Frame cost

1280x800, headless, 140 frames, medians over the 128 steady frames after warm-up.

| Scene | Wall median | p10 / p90 | GPU median | Dominant pass |
|---|---|---|---|---|
| Glowmere (stylized) | **21.87 ms** | 20.88 / 23.48 | **18.68 ms** | `scene` 15.66 (84%) |
| Constellation | **4.71 ms** | 4.53 / 5.66 | **3.74 ms** | `volume` 2.29 (61%) |

Glowmere's submitted geometry that frame: 141 draws (154 indirect, 8 empty, 62 skipped), 99 shadow
draws over 2 cascades, 430,233 triangles, 2,329 visible against 114,283 culled instances,
LOD 382/1417/525/5. Constellation: no mesh entity instances at all, which is why it is a volumetric
and particle measurement and **not** a transform, character or water one.

**Two warnings about these numbers**, both learned the hard way:

- **Constellation's GPU median is not a stable statistic.** It is volumetrics over an animated
  particle fill, so the workload genuinely differs frame to frame. Do not treat a change in it as a
  regression without re-measuring several times.
- **The two scenes do not generalise to each other.** Glowmere is 84% one pass and reproduces to
  1.4%; Constellation is a different shape of work entirely. One number for "the renderer" would be
  meaningless.

## What to expect to change, and what not to

| Should not change | Should be expected to change |
|---|---|
| every field in the committed snapshots: world transforms, bounds, mesh and material identity, visibility, cull reason, projection inputs | pixels, pass names, pass counts, per-pass timings |
| object identity: a pick id keeps naming the same object, and the material id stays one-based (or the emptiness test breaks) | GPU slot assignment, buffer offsets, draw counts |
| the derived-copy rule: the parameter is authoritative and the scene object is a per-frame derivation | which passes exist at all |

If a snapshot field changes and the upgrade did not intend it, that is the upgrade dropping a
property the old renderer had -- which is exactly the class of thing that is invisible without a
baseline and expensive to find later.

## No open renderer defects

`SYM-TERRAIN-1` -- the last one -- was fixed before this baseline was taken, so the numbers and
snapshots above describe a renderer with no known nondeterminism. That matters for what comes next:
every image comparison used to validate the upgrade is now measuring the upgrade rather than
competing with a defect underneath it. Debug and release agree.
