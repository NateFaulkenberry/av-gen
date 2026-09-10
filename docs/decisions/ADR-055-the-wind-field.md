# ADR-055: A wind field, and vegetation that answers it for nothing

Status: Accepted

## Context

The valley was finished and dead. Thirty-six thousand plants stood in it, lit by their own
bioluminescence, and not one of them had ever moved. Nothing reads as *alive* faster than
motion and nothing reads as *fake* faster than the wrong motion, so the cheapest available
version of this — multiply everything by `sin(time)` — was never on the table. A meadow driven
by a global clock moves in lockstep, and a viewer identifies that in about a second even when
they cannot say what is wrong with it.

There were also two hard constraints. The frame is over budget and a separate performance pass
is running, so anything added here has to cost effectively nothing. And the mass of vegetation
is a mass: there are 26,000 grass clumps in this world and there will be more, so a per-instance
simulation of any kind is out before it is designed.

## What already existed, and what was reused

The audit came back better than expected. Almost nothing new was needed at the architecture
level:

- **`scene::ProceduralGeometry`** already carries an ordered per-vertex GPU deformer stack
  (ADR-023/029), and scatter layers are already ordinary procedural objects (ADR-048,
  `composition.cpp`: "Scatter layers are ordinary procedural objects whose placements happen to
  have come from an ecology pass"). Vegetation motion is a vertex-stage deformation over
  instances that already exist. There is no new buffer, no new pass, no new draw, no new
  scene-graph concept.
- **`spatial::InstanceRecord`** already carries four hashed randoms per instance. Per-instance
  phase and amplitude are free; they were already sitting in the record being used for colour.
- **`FrameUniforms`** is already copied verbatim into every shadow view (`shadow_renderer.cpp`
  memcpys the block and overwrites the matrices), so putting the wind there makes a swaying
  plant and its shadow agree by construction rather than by two code paths staying in step.
- **`vs_proc`** is already the vertex stage for the depth prepass and the shadow passes as well
  as the lit pass, so one deformation covers all three.
- `core/noise.hpp` and `shaders/noise.wgsl`, `scene/particles.hpp`, ADR-032's grid fields and
  ADR-025's field/effector system were all examined and **deliberately not used**; the reasons
  are below.

## Decision

### The deformer stack is the right hook, and the wrong place to put this

The obvious move is a new `DeformerKind::Wind`. It was rejected on cost. `vs_proc` evaluates
`deformChain` **four times** per vertex — at the vertex, at two epsilon-offset tangent
neighbours for the finite-difference normal, and once more at last frame's time for the velocity
target. Anything inside the chain is paid for four times.

Instead the wind is applied *around* the chain, and factored so that the expensive half is
evaluated once:

```
w  = windSampleAt(instanceRoot, now - speciesLag)     // the field: once per vertex
p0 = deformChain(srcPos)              + windDisplacement(srcPos.y, w, ...)
p1 = deformChain(srcPos + t1 * eps)   + windDisplacement(srcPos.y + t1.y * eps, w, ...)
p2 = deformChain(srcPos + t2 * eps)   + windDisplacement(srcPos.y + t2.y * eps, w, ...)
```

The field is sampled at the *instance root*, not at the vertex, because a plant is small
compared with every length scale in the field: the regional pattern is seventy metres, the gust
fronts thirty-eight, the turbulence thirteen. A fern is sixty-five centimetres. Sampling per
vertex would buy nothing and cost four times as much. What does vary per vertex is the height
profile, and that is a `pow` and a handful of multiplies — so `p0`, `p1` and `p2` share one field
sample but get their own profile, and the finite-difference normal picks up the bend's rotation
for free instead of needing three more field evaluations. Two samples per vertex in total: this
frame's and last frame's, the second only so the velocity target is honest and TAA does not
smear the leaves.

### The field is a wave sum, not noise

The existing `valueNoise` was the first thing tried and the first thing dropped. One `valueNoise`
is eight `pcg3d` hashes, roughly 480 scalar operations; `fbm3` is three of those. ADR-050 already
established that this renderer's costs live in loop bodies rather than in the places one expects,
but a four-figure op count per vertex over every plant in a valley is not a subtlety worth
measuring — it is simply the wrong tool. Noise is what you reach for when you need a field with
no preferred direction. Wind has a very strong preferred direction.

So the field is a small sum of travelling plane waves, which is both cheaper and *more correct*:

- **Regional variation** — two long incommensurate waves (wavelength `regionScale`, ratio 1.63)
  drifting slowly downwind. This is which part of the valley is windier at all. Their beat period
  is long enough that the pattern never visibly repeats.
- **Gusts** — a sharpened travelling pulse, `pow(0.5 + 0.5 sin(k(a - t·gustSpeed) + bend), sharpness)`,
  where `a` is metres downwind. This is the part that matters most and the part that a global
  clock cannot express: the phase is a function of position *and* time, so a front arrives at the
  near hedge before it reaches the far one and the meadow moves as one ecosystem rather than as
  one object. The `bend` term is a slow wave across the wind, so a front is a curve rather than a
  ruler sweeping the map.
- **Turbulence** — two faster, smaller waves that *turn* the local direction rather than scaling
  it. Gusts change how hard; eddies change which way.
- **Flutter** — a spatial phase only. How fast a thing rattles is a property of the plant, not of
  the air; see below.

Nine transcendentals and about thirty-five multiply-adds. Everything divides out into wavenumbers
on the CPU in `packWind`, so no vertex spends a divide.

`core/wind.cpp` and `shaders/wind.wgsl` are the same expressions in the same order, and both read
the *packed* form rather than the authored one, so the two sides start from bytes that are
identical by construction rather than by two parsers agreeing.

### Species response is a transfer function, evaluated on the CPU

The spec asked for `stiffness`, `mass`, `damping`, `windSensitivity`, `bendLimit`, `tipAmplitude`
and `gustResponse` per species. The temptation is to treat those as seven fudge factors. They are
not: they are the parameters of a driven damped harmonic oscillator, and the field is a sum of
sinusoids whose frequencies are *known*. So the oscillator can be solved analytically, at those
frequencies, on the CPU, once per draw:

```
omega0 = sqrt(stiffness / mass)                  the plant's own resonance
gain(w) = 1 / sqrt((1 - r^2)^2 + (2 zeta r)^2)   r = w / omega0
lag(w)  = atan2(2 zeta r, 1 - r^2)               radians, divided by w to give seconds
static deflection = tipAmplitude * windSensitivity / stiffness
```

Three gains and a delay come out. The vertex shader multiplies by them; it evaluates no physics
at all. This is the whole reason Tier 0 can be physical without being simulated.

What falls out of it is the interesting part. Nobody hand-authored "trees should show a
low-frequency sway of the canopy only" — it is what the model *says*. A fourteen-metre tree with
`stiffness` 9 and `mass` 40 resonates at 0.075 Hz. The gust band in this valley is 0.2 Hz, so
`r = 2.6` and the gust gain is 0.157: a passing front is filtered out almost entirely. The 0.045 Hz
regional swing sits below resonance and passes with a gain of 1.14. The canopy therefore drifts
over a twenty-second cycle and ignores the gusts, and its phase lag is 2.2 seconds. Grass, at
2.4 Hz, sits far above the gust band, follows every front essentially whole, and lags by 0.04 s.
A mushroom at 0.29 Hz sits *near* the gust band with heavy damping, so it takes a fifth of the
motion and arrives 0.9 seconds late — which is exactly what "stiff and slow" means, and it is the
lag rather than the amplitude that sells it.

`damping` earns its place too: it is the only parameter with anything to do in a model with no
simulation in it, and what it does is set how loudly the plant rings at its own resonance when
broadband turbulence excites it — the flutter term. That is why the flutter *frequency* is a
per-species uniform (`flutterOmega`) and only its spatial scale lives in the wind.

### The deformation anchors the root, and is a bend rather than a shear

```
h       = clamp((objectY - baseY) / extentY, 0, 1)     0 at the root
profile = pow(h, bendCurve)                            exactly 0 at the root
off     = (windDir * (steady + gust) + perp * flutter) * amp * profile * plantHeight
off    *= maxLen / (len + maxLen)                      soft ceiling at bendLimit * height
dy      = -0.5 * |off|^2 / (height * max(h, 0.05))     the tip drops as it leans
```

Three things here are deliberate. `profile` is *exactly* zero at the root, not merely small: a
stalk that slides at the soil line is the tell that the whole mesh is being moved rather than
bent, which is the classic fake-wind look and the failure mode the spec named. The ceiling is a
soft saturation (`len·maxLen/(len+maxLen)`) rather than a `min`, because a hard clamp gives the
stalk a visible corner where it hits a wall. And the `dy` term keeps the stem's length: without
it the plant stretches sideways and reads as a shear.

Per-instance variation comes from `random.z` (amplitude) and `random.x` (flutter phase). The
gust phase is deliberately *not* jittered per instance — that would destroy the coherence the
travelling wave exists to create. Neighbours differ in how much and in how they rattle; they
lean together.

### Where it lives in the scene file

`wind` is a top-level block, a sibling of `environment` rather than a member of it, because it is
the weather rather than the sky: it moves geometry, and Tier 1 and 2 will move cloth and
particles with the same numbers. Each scatter layer carries a `motion` object. Both round-trip.

`VegetationMotion::windSensitivity` defaults to **zero**, and `WindParams::enabled` defaults to
false, so nothing written before this existed starts moving when it is loaded. `wind.speed` at 0
makes `WindParams::active()` false, which zeroes `windSway.w` on every draw and takes the shader
back to the path it had before — a genuine no-op, and also how the A/B below is taken. Two live
parameters, `scene/windSpeed` and `scene/windDirection`, expose the field to the timeline, audio
and OSC like anything else.

## What it costs

Interleaved ABBA, 300 frames at 1440x900, twelve pairs, wind on versus the same scene with
`wind.speed` set to 0 — which the log line proves took, because every layer prints the speed it
resolved against (`scatter 'grass' in wind 0.85: steady 0.189 gust 0.247 ...`).

| statistic | wind on | wind off | delta |
|---|---|---|---|
| frame minimum | 23.12 | 22.95 | **+0.16 ms** |
| p10 | 25.25 | 24.99 | +0.25 ms |
| median | 29.82 | 29.27 | +0.55 ms |

Sky-only calibration 5.68 ms before the set and 8.99 ms after: the machine warmed through it,
which is why the medians are the least trustworthy row and the minimum the most.

The honest reading is that this is **at or below the noise floor of the machine**. A null control
— the identical scene file under two names, same ABBA protocol, twelve pairs — spread by ±0.6 ms
run to run, which is larger than the effect being measured. An earlier set at 640x400, twelve
pairs, came out *systematically in favour of wind being on* by 0.8 ms, which is physically
impossible and is the clearest statement of what the noise on this machine looks like. Under 1%
of a 23 ms frame, and probably rather less.

That it is nearly free is not luck. The whole design is one field sample and four cheap profile
evaluations per vertex, over instances that were already being drawn, in a vertex stage that
already ran four times, in a frame that turns out not to be pixel-bound at all: 640x400 and
1440x900 both land at ~22.3 ms minimum.

Note also what is *not* on the per-pass line. ADR-051 established that the per-pass GPU timers in
this renderer are wrong; every number above is a frame-median A/B and nothing else.

## What was tried and rejected

- **A `Wind` deformer kind.** Four evaluations per vertex instead of one, for no expressive gain.
  The deformer stack remains the right hook for *authored* per-object deformation; a scene-wide
  field that thirty thousand instances share is a different thing.
- **`valueNoise` / `fbm3` for the field.** ~480 and ~1400 scalar operations respectively, per
  sample, to produce an isotropic field for a phenomenon that is not isotropic. Wave sums are
  cheaper *and* express travelling fronts directly, which noise cannot without advecting it.
- **A simulated grid field (ADR-032).** Correct, and enormous: a 3D velocity grid over a 640 m
  valley, plus a compute pass, plus a texture fetch per vertex, to produce something Tier 0 gets
  from nine sines. It is where Tier 2 hero physics should look, not where the mass of vegetation
  should.
- **Sampling the field per vertex.** Measurably identical output — a fern is a fiftieth of the
  smallest length scale in the field — at four times the cost.
- **A per-species phase offset instead of a phase *lag*.** A random offset makes species differ;
  the oscillator's lag makes them differ *in the right direction*, which is what makes a heavy
  thing read as heavy rather than as merely slow.
- **Jittering the gust phase per instance.** Destroys the coherence that is the entire point of a
  travelling front. Amplitude and flutter phase are jittered; the lean is not.
- **Skipping the previous-frame sample.** It halves the field cost and breaks the velocity target,
  so TAA ghosts every leaf. Not worth it at 0.08 ms.
- **An emissive stalk for the GPU parity test.** Bloom haloes it, and the outer edge of a halo is
  a convolution of the whole bar rather than the position of its tip — which is how the first
  version of that test came to measure 44% of the right answer and look like a real bug. A flat
  unlit stalk measures 0.331 m against a predicted 0.333 m.

## Test coverage

`tests/unit/test_wind.cpp` (11 cases, ~43k assertions): bit-level determinism of the field and of
the packing; gust propagation, checked by sampling the same front at `p` and at
`p + dir·gustSpeed·dt` a time `dt` later and requiring the same envelope; spatial coherence within
a metre and decorrelation over a transect; amplitude, where `regionAmount` is required to deliver
the swing it names rather than a third of it (the trap ADR-054 documents for `regionField`);
direction turn bounded by `turbulence`; calm being calm; the oscillator matching the textbook at
DC, at resonance and above it; species ordering (grass beats mushroom beats tree, and the lags run
the other way); root anchoring to exactly zero, monotonicity in height, the bend ceiling, and the
tip dropping as it leans; a patch that leans together but does not move as one plant; JSON round
trips.

`tests/rendering/test_wind_gpu.cpp` (4 cases): the parity test above, which renders a stalk in a
wind with every stochastic term silenced and measures its tip against `core/wind.cpp`'s
prediction to within 6%; the lean reversing symmetrically with the wind direction; wind off being
frame-identical across four seconds; and the full field being deterministic in time on the GPU
while still producing different frames at different times.

## Not done, deliberately

Tier 1 (per-instance spring simulation for mid-distance hero plants) and Tier 2 (real physics for
a handful of hero organisms) are out of scope here and no physics library was added. Particles
(ADR-040) do not read the wind yet, though the field is frame-global specifically so that they
can. `spatial::PointCloud` scatter is the only consumer; an imported glTF entity does not sway,
because entities do not go through `vs_proc`.

## Consequences

- Positive: the valley moves, and moves as one place. A gust crosses it. The parameters are
  physical, so tuning a species is a conversation about stiffness and mass rather than about
  magic numbers. It costs under a fifth of a millisecond.
- Negative: `FrameUniforms` grew 64 bytes and `ProceduralUniforms` 48, which pushed the
  per-LOD uniform slot stride from 512 to 768 bytes. Tier 0 cannot express collision, contact, or
  a plant that has been pushed and is still recovering — it has no state, by construction. And a
  species whose resonance lands exactly on the gust band gets an amplification that an author has
  to notice and damp; the model is honest about resonance, which means it can resonate.
