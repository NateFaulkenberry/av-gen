# ADR-577: §29's mandatory artefact is already absent, and the tier that reintroduces it is a trade

Status: accepted (measurement + instrument; no behaviour change). Date: 2026-09-21. Phase H, and
the §30--§32 assessment.

## Context

§29 is the bluntest section in the brief: *"Mandatory. No crawling noise, shimmering, temporal
popping, density flicker, unstable shadows or animated grain. Investigate temporal reprojection,
history buffers, blue-noise sampling, interleaved sampling, jitter stabilization, clamping, history
rejection. **Use the least expensive technique that produces stable results.**"*

And the march has a mechanism for the last artefact **by construction**: `stepJitter` hashes the
pixel *and the frame nonce*, `FrameTime::frameNonce` is a function of `renderTime`, and nothing
accumulates across frames -- ADR-143 rejected reprojection and ADR-460 re-confirmed the reopening
trigger is shut. Animated grain is not a risk here; it is the design.

The obvious move was to build one of §29's listed techniques. **The measurement said not to.**

## The instrument, and the control that saved it

`tests/rendering/test_volume_temporal_gpu.cpp` renders a still medium at several `renderTime`s and
reports the mean per-pixel temporal standard deviation. The medium has no drift, no detail, no
breath and no rotation, so `fogShapeAt` is constant in time and `renderTime` reaches the picture
through exactly one path: the jitter.

**The first attempt measured something else entirely.** It ran on the Tree of Life project over a
third of a second and produced 8.17 levels of temporal SD with the jitter on against 2.10 with it
off -- a tidy, quotable 4x. Then the control asked whether consecutive frames were identical with
the jitter *off*, and they differed by up to **212 levels**. The camera was moving. The whole
measurement was of camera motion, and the two arms differed for a reason that had nothing to do
with jitter. **A control that asks "is the thing I am holding still actually still" costs one
comparison and invalidated a whole result.**

## What it measures

Mean per-pixel temporal SD as a fraction of the medium's own mean luminance, plus the spatial
high-frequency energy with the jitter on against off (`spatialRatio`), over step counts and optical
depths:

| steps | tau 0.5 | tau 1.4 | tau 4 | tau 12 | spatialRatio |
|---|---|---|---|---|---|
| **16** | 1.0e-3 | 9.8e-4 | 2.0e-3 | **1.02e-2** | 1.00 -- 1.17 |
| **32** (shipped) | 1.2e-4 | 1.1e-4 | 7.9e-5 | 1.7e-4 | **1.000** |
| 64 | 1.8e-5 | 1.3e-5 | 8.1e-6 | 3.7e-5 | 1.000 |
| 128 | 3.8e-6 | 6.1e-7 | 6.3e-7 | 2.4e-5 | 1.000 |

Three readings, and the second is the one I did not expect:

1. **At the shipped 32 steps the animated grain is about 1e-4 of the medium's brightness** --
   roughly a fortieth of one level in 255. Present, measurable, and nowhere near visible. **§29's
   mandatory artefact is already absent**, and the least expensive technique that produces stable
   results is the one already in the tree. No history buffer, no reprojection, no blue noise.
2. **The jitter adds no measurable SPATIAL noise at 32 steps and above** -- `spatialRatio` is
   1.000, meaning a frame's high-frequency content is the same with the jitter on and off. So the
   speckle visible in this branch's own dense §46 C arms was **not the jitter**; it is the medium
   undersampled, which is a different artefact with a different fix (§31's adaptive sampling, or
   more steps). *I had attributed it to jitter in an earlier report, without measuring.*
3. **At 16 steps with a thick medium the grain reaches 1% of the medium** -- about 2.6 levels of
   255, and visible as crawl.

## The tier, which is a trade and not an oversight

`QualityTier::Preview` sets `volumeStepScale = 0.5`, so a scene authored at 32 steps marches at
**16** -- the row where the grain becomes visible.

`render_quality.hpp` says of that lever: *"`volumeStepScale` buys depth banding back and almost no
time."* **That does not generalise to a scene with a placed medium.** Measured with the engine's
own interleaved A/B harness, three pairs, 120 frames each, on a fog-bank scene:

```
A/B gpu : baseline 15.14 ms, arm 10.81 ms, delta +4.26 ms (+28.14%); noise floor 2.00%
          per-pair: +4.26 +4.13 +4.39     drift +0.65% -- the machine held still
```

**28% of the march.** The header's claim was presumably measured against the environment fog, whose
cost is dominated by how many pixels have non-zero density rather than by the step count
(ADR-374's finding, quoted in that same comment). A placed bank is the other regime. This is
ADR-389's family once more -- *a coefficient tuned against a distribution, cited for a different
one* -- and it is stated here as "does not generalise" rather than "is false", because the claim
may well hold for the scenes it was written against and I measured one scene.

So Preview's halving is a **real trade**: 28% of the march against the one artefact §29 calls
mandatory, on thick media. Not free, not an oversight, and not mine to resolve -- the tier table
belongs to the renderer.

## §30 -- §32, assessed

- **§30, quality tiers.** They exist -- `QualityTier::{Preview, Realtime, High, Offline}` with
  per-tier `volumeStepScale` and `volumeResolutionScale` -- and the brief's names are different
  (Draft/Preview/High/Cinematic). The gap is not the tiers; it is that **the step count is not the
  axis the brief thinks it is.** §30 says "do not make High merely mean throw samples at it", and
  the measurement above says the samples are worth 28% -- so the interesting question is what
  *else* High should buy, which §20's self-shadow march (ADR-570) is now a candidate for.
- **§31, adaptive sampling.** Not built. The march takes fixed steps over `maxDistance`. The
  groundwork exists and is unused: ADR-562 §4 computes a per-slot interval per pixel and then
  marches the fixed grid anyway, with its own comment saying *"redistributing the steps into the
  union of the intervals is the bigger win and is NOT done here."* That is §31, already scoped, by
  the ADR that built the bound it needs.
- **§32, empty-space optimisation.** **Done**, and it is the same ADR-562 §4 interval plus
  ADR-566's corrected bound: a step outside a medium's cylinder costs a comparison instead of a
  field evaluation, and ADR-562 §8 measured four media costing *less* than one because of it.

## Consequences

- **Two cases and a hidden instrument.** `[.]`-tagged so the sweep does not run by default; the
  two checks are that jitter 0 is *perfectly* still (bit-identical across eight frames) and that
  jitter 1 is not. The second asserts `> 1e-6`, not the measured 1.4e-5, because a test pinned to a
  measurement is a tripwire on every unrelated change to the march.
- **A unit error of my own, worth the line.** The first threshold was `0.05`, carried over from an
  8-bit habit; `renderToImageFloat` returns **scene-linear float**, where the medium itself sits at
  0.17. It failed against working code, which is the cheapest possible reminder of what a function
  returns.
- **`WARN` wraps its text at the console width**, so a wrapped number is one a script cannot parse
  -- one re-run's worth. The sweep prints through `fprintf(stderr, ...)`, the same escape the
  lighting lab's `AVGEN_LAB_DUMP` uses.

## Revisit when

- **The Preview tier's volume step scale is decided.** 28% against visible crawl on thick media;
  both numbers are here and the choice is the renderer's.
- **§31 is taken.** ADR-562 §4 says what it is and why it was deferred, and ADR-566 fixed the bound
  it would redistribute steps within.
- **A medium is authored thicker than tau 12 or a tier goes below 16 steps.** Those are the corners
  where this measurement stops covering the configuration.
