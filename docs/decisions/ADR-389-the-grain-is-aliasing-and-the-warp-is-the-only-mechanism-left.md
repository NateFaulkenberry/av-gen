# ADR-389: The grain is aliasing, and the warp is the only mechanism left

- Status: Accepted (2026-09-19)
- Corrects ADR-374 in place (step count is not within noise). Extends ADR-371 (the vortex in the
  march), ADR-388 (the vortex as a sampler), ADR-264 (a project's parameters over its scene),
  ADR-182 (a probe that cannot fail), ADR-385 (a stated reason is not evidence).

## Problem

The owner reports the vortex reading as **noise and grain** rather than as a vortex of smoke. The
coordinator's diagnosis, offered as arithmetic rather than a hunch, was ray-march aliasing:
features far smaller than the distance between march samples, amplified by a contrast exponent and
a colour ramp that both select the brightest samples.

The diagnosis is right. Measuring it corrected it in three places and killed the obvious fix.

## What the measurement says

### The step length is 125 metres, not 83

The scene file says `volumeSteps: 48`. The **project overrides it to 32** (ADR-264: a project's
parameters are applied over its scene). 4000 m over 32 samples is **125 m per step**.

Worth stating as a general fact because it will recur: **a measurement taken from the scene file is
a measurement of a file nobody renders.** The rule is one this project quotes at people and then
walks past — it did so here, in the arithmetic that opened the investigation.

Against 125 m, and remembering that `fbm3` is itself three octaves at 1 / 2.03 / 4.11:

| octave | nominal period | finest internal content |
|---|---|---|
| 0 (`scale`) | 83 m | 20 m |
| 1 (`×3.1`) | 27 m | 6.5 m |
| 2 (`×9.7`) | 8.6 m | **2.1 m** |

Nyquist wants two samples across a period. The finest content is undersampled **sixty times**.

### The step ladder is the control, and it is decisive

Grain measured as the mean absolute high-frequency residual (image minus a 3×3 box blur) over the
**lower half** of the frame, where the funnel is — hero camera, 1280×720, t = 6:

| steps | 32 (shipped) | 48 | 96 | 192 | 384 |
|---|---|---|---|---|---|
| grain | 1.139 | 0.969 | 0.756 | 0.578 | 0.520 |

Monotone, converging near 0.52. **Fifty-four per cent of the high-frequency energy in the lower
frame is aliasing rather than detail.** The two rendered frames say it more plainly than the metric
does: salt-and-pepper speckle at 32, smooth swirling bands at 192.

### The falsification test partly refuted the diagnosis, which is what it was for

The coordinator asked to be told if the grain survived with the third octave's weight at zero. It
does survive:

| arm | grain | share of the artifact |
|---|---|---|
| third octave off | 0.843 | 26% |
| octaves 2 and 3 off | 0.475 | 58% |
| `pow(n, contrast)` linearised | 0.764 | 33% |

So aliasing dominates, but it is **spread across octaves**, and the contrast exponent is an
**independent amplifier** rather than a multiplier on the same artifact. A band-limit that dropped
only the finest octave — which is what the original instruction implied — would have fixed a
quarter of it and looked like it had worked.

## The fix that looked obvious, and is not affordable

ADR-374's "step count is within noise" is corrected in place. Re-measured, `volume.march` alone:
**4.78 ms at 32 steps, 8.20 at 48, 17.5 at 96.** Superlinear, because more steps put more samples
*inside* the funnel where the three fBMs run; 96 steps spends the whole frame budget on the march.
Raising the step count is not available at Realtime. It stays available at High and Offline, where
the budget exists.

## Decision

### The band-limit cannot be a weight, and finding that out made the grain worse

The first implementation did the reasonable thing: attenuate each octave whose period falls below
twice the sample spacing, and renormalise by the weights actually used so the mean does not move.

**It made the grain worse — 1.139 → 1.307.**

The reason is counterintuitive enough to be worth the paragraph, because the next person will reach
for the same fix. At 125 m spacing **even the coarsest octave is past Nyquist** (83 m period against
a 250 m requirement). All three octaves are aliased, and summing several aliased signals *cancels*
some of the error, while attenuating two of them and renormalising hands the whole signal to the one
that remains — which is itself aliased, and now carries the variance alone. Weighting redistributes
an artifact; it cannot remove one.

So the band-limit has to be a **frequency**. The base scale is clamped to the finest the march can
carry, and the clamp is taken against `fbm3`'s own finest internal octave rather than the call's
nominal scale: 1.139 → 0.895, and → 0.869 once that factor is included.

### The strict answer erases the funnel, and that is the result, not a failure

Clamped strictly, the honest Nyquist scale at 32 steps is **0.19 against an authored 2.4**, and the
render comes back a **flat teal wash with no swirl at all**. Every filament gone.

That is the correct answer to "what can 125-metre samples carry", and it is the proof that this
march is starved rather than this noise being mis-designed. It is recorded here rather than buried
because it is the load-bearing fact of the whole investigation.

What ships is a **floor** on the clamp — grain 0.963 at default, −15% — which takes the worst of the
aliasing and leaves the rest of the problem visible where it belongs, in the step count. That is a
compromise and it should be legible as one: the strict answer is written beside it, in the shader,
so nobody later mistakes the floor for the maths.

### When samples are unaffordable, warping is the only mechanism left

This is the design rule that outlives the effect.

More samples are not available. Therefore coarse noise is the only affordable kind. Therefore the
only way to make a resolvable-frequency field read as smoke is to give the coarse structure
**shape** rather than to add detail on top of it — which is what domain warping does: it advects
the finer octaves through the coarse flow, so their variation is carried *by* the spiral instead of
sitting *on* it. Uncorrelated detail on a smooth flow is what the eye calls grain. Advected detail
is what it calls smoke.

The warp was offered as the best of three suggestions. The cost measurement makes it the only one.

### The controls

`smokeWarp`, `smokeBillow` and `detail` on `world::Vortex`, one row each in the `constexpr` field
table — which, per ADR-387's architecture, is the whole of the work: panel row, parameter,
modulation target, timeline key, preset member and save entry, with no further code. Soft ranges are
the usable span and hard ranges are wider, because a modulation route clamps to the hard range.

The band-limit is deliberately **not** a knob. Nobody should have to find a slider called "stop
aliasing", and the right answer changes whenever `volumeSteps` or `volumeMaxDistance` do — which
they do between Preview and Cinematic.

## Consequences

**The authored values were compensation for an artifact, and are re-tuned.** `turbulenceScale` 2.4
and `contrast` 3.6 were chosen by eye on top of a sixty-times undersample. They are not authored
intent; keeping them would preserve the artifact's fingerprint in numbers that are then wrong for
every future change. The old values are recorded here so the change is recoverable and legible:

| parameter | was | now | why |
|---|---|---|---|
| `turbulenceScale` | 2.4 | see the tuning commit | 2.4 put every octave past Nyquist |
| `contrast` | 3.6 | see the tuning commit | `pow` above 1 on a noise sum is a grain amplifier |

**A metric that measures the whole frame would have missed it.** The tree's foliage dominates the
high-frequency energy of this shot, and `hf_all` moves by 3% across arms that move the funnel's own
grain by 54%. The measurement is restricted to the lower half for that reason — ADR-182's point,
met by choosing where to look rather than by choosing a threshold.
