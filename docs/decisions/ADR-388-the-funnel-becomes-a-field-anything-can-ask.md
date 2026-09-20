# ADR-388: The funnel becomes a field anything can ask, and the light gets a way back in

- Status: Accepted (2026-09-19)
- Extends ADR-055 (the wind field, whose shape this copies), ADR-371/374/379 (the vortex),
  ADR-380 (particles entrained by it), ADR-091 (two-tier determinism), ADR-387 (the vortex as an
  authored effect).

## Problem

Two, and they are the first two layers of the vortex FX work.

**The funnel existed only inside the march.** `vortexShape()` lived in `volume.wgsl`, so the only
thing in the engine that could ask where the vortex was, or what the medium was doing there, was the
volumetric raymarch. Everything else had to guess again in its own vocabulary: ADR-380's particles
approximated it with an attractor plus an orbit force, which is a different shape that happens to
look similar, and the debug view re-derived the geometry from the uniforms by hand. The FX spec asks
for clouds, lightning and seven particle populations that all sample the same field. Seven more
guesses was not a plan.

**And the funnel could not be lit, at all, by anything.** ADR-371 removed the vortex from the
scattering term and was right to: folded in, it swallowed the intensity-22 key light and returned a
frame at mean luminance 131 of 255 with the vortex's own emission set to zero. The consequence
nobody had stated is that an upward spotlight aimed through the funnel lights the surfaces it
reaches and the ordinary fog, and its beam **stops dead at the funnel's edge** — because the medium
the beam is supposed to be visible in does not scatter. The owner asked for that to be tunable.

## Decision

### The vortex field is a sampler, on the wind's precedent

`src/core/vortex.{hpp,cpp}` and `shaders/vortex.wgsl`, in exactly the shape ADR-055 established:
a pure function of (packed uniforms, position, time), no state, no wall clock, with the shader as a
transliteration of the C++ and a test that compares them **through the packed form**, so both sides
start from bytes identical by construction and a disagreement can only be about the maths.

```
VortexUniforms packVortex(const VortexField&);
VortexSample   sampleVortex(const VortexUniforms&, vec3 worldPos, float t);
float          vortexShape (const VortexUniforms&, vec3 worldPos, float t);
```

`VortexSample` carries `density` (the normalised shape), `velocity` (the medium's motion),
`radialT`, `depthT` and `envelope`. The velocity is derived from the *same* geometry the shape is,
which is the whole point: the tangential term is the swirl the angular shear already describes, the
radial term draws inward and vanishes at the axis, the vertical term descends the throat scaled by
how far in the sample already is. A particle integrating that gets ADR-380's spiral as a
consequence rather than as an approximation.

This is also the only version of §33's determinism that is **checkable**. ADR-091 asks that second
N render identically however you arrive at it, and the way that is always lost is something
integrating a frame delta. Two independent implementations that agree at arbitrary (position, time)
pairs, in any order, cannot be integrating anything — and the test asserts exactly that, including a
control that the field is not merely constant in time.

### Taking uniforms as a parameter is not bit-neutral, and that cost 62 pixels

Worth recording because it is not obvious and it will recur. Reading `vol.vortexN` directly from the
uniform buffer and reading the same values from a function parameter produce **different
arithmetic** on this backend: the Metal compiler contracts the expression differently. Moving the
body out of `volume.wgsl` and calling it with the uniforms passed in moves the shipped frame by
**62 of 518400 pixels — 60 of them by a single code value, one by 4, one by 11, and no two of them
adjacent**. They are rounding flips at the two early-out thresholds and inside `pow`.

Three call-site arrangements were tried — a helper returning the struct, a struct constructed inline
at the call, and the body reading the parameter directly — and all three give the identical frame as
each other and a different one from the version that read the uniform. The shader preprocessor here
has `#include` and no defines, so there is no textual way to have one body and two accessors.

So the choice was one shared body that is provably-invisibly different, or two copies that can
silently disagree. One body wins: 60 single-level pixels are not a picture anybody can see, and two
copies of a 40-line function is the drift this file exists to prevent. The alternative is recorded
here so the decision can be revisited rather than rediscovered.

### The scattering coefficient, default 0

`world::Vortex::scattering`, one row in the `constexpr` field table, one line in the march:

```wgsl
let scattering = (fogDensity + vortexDensity * vol.vortex5.z) * vol.params0.w;
```

ADR-371's comment above that line is **extended, not replaced**, so the next reader meets the
original refusal and the controlled way back in together.

The table row is the whole of the UI work, and that is worth saying because it is what ADR-387's
architecture bought: one row gives the panel control, the parameter, the modulation target, the
timeline key, the preset member and the save entry, with no further code anywhere.

**The range is a measurement.** Laddered on the shipped Tree of Life at t = 6, mean frame luminance
of 255:

| scattering | 0.00 | 0.02 | 0.05 | 0.10 | 0.20 | 0.50 | 1.00 |
|---|---|---|---|---|---|---|---|
| mean | 65.5 | 65.9 | 66.4 | 67.2 | 68.9 | 73.4 | 80.3 |

Near-linear, and usable across the whole of 0..1 — so 0..1 is the soft range. Clamping it to 0..0.2
"to be safe" would have put every usable value in the first fifth of the travel, which is the
mis-scaled-knob defect this branch already fixed once on `scene/windSpeed`, arrived at from the
cautious direction instead of the careless one. The **hard** maximum is 4, because that is what a
modulation route clamps to and somebody driving this from a drop is entitled to overshoot.

**And the first version of this decision quoted the wrong number.** ADR-371's catastrophic 131-of-255
was measured on the pre-funnel *slab*, before ADR-374 gave the vortex a throat, a void and a rim.
Against today's shape, full scattering costs +15 luminance levels, not a wash. The refusal ADR-371
records is still the right default; the number it records is no longer what this setting does, and
shipping a comment that said otherwise would have been ADR-385's stated reason that is not evidence,
written by the person who had just read the evidence.

## Consequences

**Both ends are proved, which is what the field being safe rests on.** At 0 the shipped frame is
byte-identical to the frame before the field existed: `0 of 518400 pixels differ, max channel delta
0`, on both frames. At 0.5 it is visibly different — 98.25% of pixels, and the funnel plainly takes
the scene's light. A field that only passed the first test would be decorative.

**Every row in the effect panel carries its own tooltip now.** The panel used to attach one to
whatever it had drawn last, so appending `scattering` to the end of the list silently stole the
explanation off `spill`. Found by adding a row, which is the only way that defect is ever found.
Three rows that had tooltips keep them and the new one has its own, written in terms of what is on
screen: *at 0 the funnel makes its own light and the scene's lights do not appear inside it — a
spotlight aimed up through it stops at its edge.*

**A test that could not fail was avoided by counting.** A parity test over a field that is zero
everywhere passes perfectly (ADR-182), so the case counts how many of its samples landed inside the
funnel with a non-zero density and a non-zero velocity, and fails if too few did. The sample spread
is chosen to hit all four early-outs, the void, the wall, the region past the rim, above the mouth
and down the throat.

**What is not done.** The spotlights themselves are the next layer — this is the coefficient that
makes them worth aiming. Turning it on by default is an art decision with a render behind it and is
deliberately not taken here.
