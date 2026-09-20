# ADR-560: The fog bank has no shape, and the second medium in a scene is a silent no-op

- Status: Accepted (2026-09-20)
- Opens the Fog Bank rebuild (the brief's §3, §4 and §46 A). Extends ADR-460 (the vortex's envelope
  is uniform in angle) and ADR-461 (the step count was buying the jitter back) -- both findings hold
  one effect over, and one of them is stronger here. Related: ADR-374 (one medium costs +5.5 ms),
  ADR-389 (the grain is aliasing), ADR-264 (measure through the project), ADR-385 (a stated reason
  is not evidence), ADR-421 (a control that does nothing teaches an artist the system is broken),
  ADR-500 (an effect is one file and four lines).
- **Corrects two statements this repository currently makes about itself in its own comments.**

> **The brief's §44 bar 1 -- "noise disabled -> still looks like fog" -- is currently unreachable by
> construction, not by tuning.** With the detail off, a fog bank's density field is monotone in
> radius, uniform in angle and smooth in height: no eye, no wall, no bands, and no rows for any of
> them. There is nothing underneath the noise for a clamp, a step count or a parameter to leave
> behind. That is the whole finding, and every phase after this one follows from it.

Worth stating because it arrived twice: `agent/tornado` reached the same conclusion about the vortex
half of this family from different evidence and on its own -- that the shipped effect **computes the
wrong thing** rather than computing the right thing badly. Two independent routes to one diagnosis
is a stronger result than either, and it is why the rebuild is a rebuild.

## 1. The owner's bug: the second placed medium is not reduced, it is absent

The report is "if I turn on fog I can't see the vortex". Measured, with a fog bank and the shipped
Cosmic Vortex both authored in the Tree of Life project, hero camera, t = 6, 1280x720, bloom off,
`--disable particles`:

| arm | sha256 of the frame (first 16) | grain | mean luma |
|---|---|---|---|
| the vortex alone | `6410cd279e8dde50` | 1.5693 | 100.39 |
| **vortex listed first, fog second** | **`6410cd279e8dde50`** | 1.5693 | 100.39 |
| the fog bank alone | `e5db96483f52b95b` | 0.3221 | 186.78 |
| **fog listed first, vortex second** | **`e5db96483f52b95b`** | 0.3221 | 186.78 |

**Byte-identical, both ways round.** The second placed medium in a scene does not contribute a
single pixel, and *which* of the two survives is the order of the array in the project file --
nothing an artist can see, name or control. `resolveAtmosphericEffects` walks the effects in
authored order and `EffectBucket::Vortex` keeps the first one; the rest increment
`AtmosphericCounts::dropped`.

### The comment that stopped anyone checking

`atmospherics.hpp` says of that limit: "the resolve counts what it dropped **so the UI can say so
instead of silently ignoring it**", and `volumetric_fog_effect.cpp` repeats it: "counted in
`AtmosphericCounts::dropped` and **reported -- a stated limit, not a silent no-op**."

It is a silent no-op. `dropped` has exactly **one** reader in the whole tree, and it is not the UI:
`effect_conformance.cpp`, which formats it into a CPU-suite conformance finding. `engine.cpp` calls
`buildAtmosphericFrame` and never sees a `counts` at all, so the number does not exist in a running
editor -- no panel, no log line, no overlay. This is ADR-385's shape exactly, a stated reason that
is not evidence, and it is the second time in a month that a comment in this family has described a
report that was never wired up (ADR-387's §68 vortex-window comment was the first, and said so).

**So the limit is not "stated". It is invisible, and it is the whole of the owner's bug.** Lifting
it is the architectural centre of this rebuild and not a stretch goal.

## 2. The brief's §3 question, answered -- and its premise is wrong

§3 asks whether the artifact is **A** density noise, **B** march undersampling, **C** stochastic
sampling noise, **D** temporal instability, **E** shadow sampling noise, **F** excessive
high-frequency density detail, or **G** a combination. Every arm is the shipped Tree of Life
**project** (ADR-264: the scene file says `volumeSteps: 48` and the project overrides it to 32, so
the scene is a file nobody renders), with the vortex effect replaced by a fog bank carrying the
`Valley Mist` preset's own shaping numbers. `tools/make_fog_arms.py` writes them;
`tools/grain.py` measures.

| arm | grain | grain/mean % | mean luma |
|---|---|---|---|
| **base** -- 32 steps, jitter 1.0, `cloudNoise` 1 (what an artist gets) | 0.3221 | 0.2637 | 186.78 |
| jitter 0 | 0.2690 | 0.2380 | 186.73 |
| 256 steps | 0.2806 | 0.2852 | 180.59 |
| 256 steps, jitter 0 | 0.2808 | 0.2853 | 180.59 |
| `cloudNoise` 0 (the fBM stack contributes nothing) | 0.2050 | 0.1377 | 207.46 |
| `cloudNoise` 0, jitter 0 | 0.1821 | 0.1272 | 207.47 |
| **a perfectly uniform volume** (`Environment`, falloff 0, noise 0) | 0.1462 | 0.0640 | 233.45 |
| **a smooth analytical height gradient** (no noise anywhere) | 0.2011 | 0.0971 | 226.23 |
| the shipped cosmic vortex, same camera | 1.5693 | 1.7081 | 100.35 |

### The metric has an exposure sensitivity, and finding it out cost four arms

The first reading of that table was "the noise is 36% of the grain and the jitter 17%". It is not,
and the control that caught it is a density ladder with **the field's content unchanged in kind**:

| `density`, noise ON | 0.0016 | 0.0022 | 0.0028 | 0.0036 |
|---|---|---|---|---|
| grain | 0.3221 | 0.3054 | 0.2917 | 0.2753 |
| grain/mean % | 0.2637 | 0.2416 | 0.2238 | 0.2039 |
| mean luma | 186.78 | 190.79 | 194.10 | 197.76 |

**Grain falls 15% across a ladder that adds nothing but density**, because the tone curve compresses
contrast as it approaches white -- and normalising by the local mean does **not** fix it, as the
second row shows. Any arm pair whose mean luminance differs by more than about a luminance level is
not comparable by this number. ADR-389 does not say this, and the arms above straddle 53 levels.
`tools/grain.py` now prints both figures and says so in its own docstring.

It also could not be removed by dimming the medium, which is worth recording: `density` does not
reach it, because the march's emission term is `shape * emission` and not `shape * density *
emission`, so a 37% density cut moved the noiseless arm's mean by 3.4 levels; and `emission` does
not reach it either, because `scattering` rises with the shape's mean too -- a 40% emission cut
moved it 9 levels of the 21 needed. **Every knob that dims this medium also changes it.**

### The two comparisons that ARE at matched exposure

- **Jitter, 186.78 against 186.73 -- a twentieth of a luminance level, no correction of any kind:
  `volumeJitter` 0 is worth -16.5% absolute, -9.7% normalised.** Free, and it is ADR-461's finding
  reproduced on a second medium.
- **Noise, 197.76 against 198.28 -- two thirds of a level:** `density` 0.0036 with the fBM stack on
  reads 0.2039%, and `cloudNoise` 0 with `emission` 0.012 reads 0.1607%. **The fBM stack is worth
  +27%.**

### B is refuted, and that is the load-bearing result

**Eight times the steps does not reduce the grain.** 32 -> 256 moves it 0.3221 -> 0.2806, which
looks like -13% until you notice the mean fell 6 luminance levels with it; on the normalised figure
it **rises 8%**. And 256 steps with the jitter off (0.2808) is indistinguishable from 256 steps with
it on (0.2806), exactly as ADR-461 predicts -- the jitter's variance scales with the step length, so
at 15.6 m it costs nothing.

The reason more steps does not help is visible in the field: ADR-389's band-limit clamps the base
noise scale to what the march can carry, so *more steps admit more high-frequency content*. The
256-step frame does show structure the 32-step frame does not -- broad soft cloud masses in the
upper frame -- but it is more finely detailed noise, not more resolved fog. This is the brief's §48
in one measurement: **`bad density x 64 samples` = better-sampled bad density.**

### So the answer is F in form, and A in cause -- but the question's premise is wrong

The premise of §3 is that there is a good density field being badly rendered. There is not.

**The volume renderer is not the problem, and two arms prove it.** A perfectly uniform volume reads
0.064% and a smooth analytical height gradient reads 0.097%, against this fog bank's 0.264% -- and
both of those images are *clean*, with no grain anywhere in them. §4's diagnostics A and B needed no
new code at all: `Environment`'s own medium already IS a constant field (`fogHeightFalloff` 0) and a
smooth analytical gradient (`fogHeightFalloff` > 0), and switching `volumeNoise` off is one
parameter. **The renderer can produce clean fog today.**

## 3. The actual finding: a fog bank has no shape

`volumetric_fog_effect.cpp` is a different authoring surface onto `shaders/vortex.wgsl`'s field with
`swirl`, `funnelDepth`, `throat` and `innerVoid` at zero -- which is a defensible piece of ADR-500's
argument and is also why this effect cannot work. With those four at zero the envelope is

    smoothstep(0, 0.22, rr) * (1 - smoothstep(0.72, 1.3, rr)) * exp(-y^2 / thickness^2)

**Monotone in radius, completely uniform in angle, smooth in height.** ADR-460 found exactly this
about the vortex before the macro structure was added; but the vortex has an eye, an eye wall and
spiral bands now, and a fog bank has **none of them** -- `eyeWallGain`, `bandArms`, `bandDepth` and
`bandHarmonic` are all zero in `applyStyle` and none of the four is a row on the fog panel.

So everything anybody has ever seen in Fog Bank came out of the three fBMs underneath, and the
rendered arms say it more plainly than the metric does:

- with `cloudNoise` at 0 the frame is a **flat, featureless pale wash** -- not fog, a fogged lens;
- with it at 1 the frame is that wash with noise on it;
- and a bank placed *outside* the camera at 600 m radius and six times the density is a **smooth
  grey gradient rising from the bottom of the frame with no edge, no mass and no silhouette.**

The brief's Definition of Done is that with all noise and detail at zero the system must still
produce clean, coherent, spatially convincing fog. Today, at zero it produces a grey card.

**This is why no step count and no band-limit will fix Fog Bank, and it is what §46 B-F are for.**

### Two defects found on the way, both of which an artist can see and neither of which has a control

- **Every fog bank has a hole in the middle.** `eyeWallWidth` defaults to 0.22 and `applyStyle` does
  not reset it, so the envelope's `smoothstep(innerVoid, innerVoid + 0.22, rr)` rises from **zero at
  the axis** to full at 22% of the radius. For the 900 m bank above that is a 200 m clearing in the
  centre of the fog. It is the vortex's eye, inherited by an effect that has no use for an eye, and
  `eyeWallWidth` is not a row on the fog panel -- so it cannot be closed.
- **`cloudNoise` is not reachable from the fog panel.** It is §53's control and the one that answers
  "does this still look like fog with the detail off" -- the brief's §4 D, its §44 bar 1 and its
  Definition of Done, all in one parameter. The vortex declares it; the fog bank does not. Every
  arm above that needed it had to be authored by hand into the JSON, where it round-trips only
  because `AtmosphericEffect::toJson` walks **every** kind's rows and not just this one's.
  ADR-421's family: a control that exists and cannot be reached.

### And it is not mean-preserving, which invalidates a coefficient

Turning `cloudNoise` off at a fixed `density` and `emission` **raises** the lower frame's mean
luminance by 20.7 levels, 186.78 -> 207.46. The blend is written `mix(flatLevel, shaped,
cloudNoise)` with `flatLevel = 0.5 * 2/(contrast+1)` precisely so that it would not do this
(ADR-389's family rule), and the compensation assumes the shaped noise's mean is 0.5 before scaling.
Under this preset's `smokeBillow` and `turbulence` it is not: `mix(n, |2n-1|, billow)` has a mean
well below 0.5 for an fBM concentrated near 0.5. **So `density` and `emission` as authored are
calibrated against a distribution that the detail controls move**, which is ADR-389's general form
again: a coefficient tuned against a quantity is invalidated by a change to that quantity's
distribution. Measuring which of `smokeBillow`, `turbulence` and the `smoothstep` remap is
responsible is Phase B's, through `core/vortex.cpp`'s `VortexSample::envelope`, which already
returns the two halves separately.

## 4. Two of the brief's eight diagnostic modes describe machinery that does not exist

§4 asks for eight diagnostic modes. Checked rather than assumed:

| mode | status |
|---|---|
| A constant density | **exists today**: `Environment::volumeDensity`, `fogHeightFalloff` 0, `volumeNoise` 0 |
| B smooth analytical gradient | **exists today**: the same with `fogHeightFalloff` > 0 |
| C current density | the base arm |
| D noise disabled | exists as `cloudNoise`, **unreachable from the fog panel** |
| E temporal jitter disabled | exists as `Environment::volumeJitter` (ADR-461) |
| F temporal accumulation disabled | **vacuous.** `rendering/volume_renderer.cpp` has no history buffer and no reprojection; ADR-143 rejected it and ADR-460 re-confirmed the reopening trigger is shut |
| G increased ray steps | `Environment::volumeSteps` |
| H shadowing disabled | **vacuous.** `shaders/volume.wgsl` says so in its own words: "There is no shadowing in the fog: a beam is the falloff of a local emitter, not an occluded shaft" |

So §3's **D (temporal instability)** and **E (shadow sampling noise)** cannot be the artifact,
because neither mechanism is in this renderer. What "build the diagnostic modes" means here is
mostly **expose what is already there**, which is a smaller and more honest job than the brief
assumes -- and the one genuinely missing piece is the debug visualisation of §38.

## 5. The hero shot does not contain fog either

ADR-460 measured that the Tree of Life hero frame cannot hold a cyclone because the island and tree
occupy +/-15 degrees of a 36 x 60 degree frame. The same is true of a fog bank, differently: a bank
centred 380 m below the island at 600 m radius is **invisible** at the preset density (mean luma
57.96, no fog anywhere in the frame) and at six times it is a featureless pale gradient. The only
placement in which a fog bank reads in this shot is one that **encloses the camera**, and then it
washes the whole picture to mean 187 of 255.

That is not a fog defect; it is the same shot constraint, and it means **the Tree of Life hero is
not the fixture for Phases B-F.** `examples/labs/volumetric-atmosphere-lab.scene.json` -- five unlit
slabs at 4, 12, 24, 40 and 60 m in a known volume and nothing else -- is, and it already exists.

## Consequences

- **The single medium slot is lifted, not documented.** §34 and §35's reusable volume infrastructure
  is what lifts it, and "a fog bank and a vortex coexist" is a required outcome with a test.
- **`AtmosphericCounts::dropped` must reach something a person can see** whatever else happens, and
  until the slot limit is lifted that is the whole fix for the owner's report.
- **Do not buy steps.** ADR-461 demoted per-tier `volumeStepScale`; this demotes the step count for
  fog as well, from the other side -- more steps admit more aliased content here.
- **The exposure rule, which ADR-389's family did not have and is where the next agent will look.**
  *The grain metric is only valid between arms at the same exposure.* A density ladder that adds
  nothing but density moves it 15%; normalising by the local mean does not fix it, because the tone
  curve compresses faster than 1/mean as it approaches white. Two arms whose lower-frame mean
  luminance differs by more than about a luminance level are **not comparable by this number**, and
  an arm that changes the medium's brightness must be re-shot at matched exposure before its grain
  is quoted. Three of the four figures in the first version of §2's table were wrong for this reason
  and this ADR would have shipped them. `tools/grain.py` prints both figures and carries the rule.
- **The fixture moves to the Volumetric Lab** for the density work, and comes back to Tree of Life
  and Glowmere for §40 and §41 -- those are acceptance, not development.
- **The brief's §46 ordering is inverted by one step, deliberately.** §46 runs A diagnose, B clean
  analytical volume, C local banks; the next thing built is the medium-slot array and the per-slot
  ray interval instead. That is not drift. §46's ordering assumes the foundation exists, and it does
  not: one live owner-facing bug and a second agent's whole effect are both blocked on a literal `1`
  in `atmospherics.cpp`, and the ray interval is simultaneously §31's adaptive sampling, §32's
  empty-space optimisation and what makes §20's self-shadowing affordable. **Building the thing
  everything else stands on, first, is the ordering §46 would have had if it had known the slot
  limit was real.** §46 B resumes immediately after, on the Volumetric Lab fixture.

## Revisit when

- A scene appears whose fog bank is small relative to the march step -- the case this branch did not
  construct. ADR-389's clamp bites hardest there (a 100 m bank at a 125 m step), and the prediction
  is the flat-teal-wash failure rather than grain. If it is not, the clamp's floor is wrong.
- The medium-slot count is chosen. This ADR says one is too few; it does not say what N is, and that
  is a measurement (ADR-374's +5.5 ms) that Phase I owes.
