# ADR-562: The march carries media, not a vortex

- Status: Accepted (2026-09-20)
- Implements the fix for ADR-560's headline defect and builds the shared volumetric foundation the
  Fog Bank rebuild and `agent/tornado` both stand on. Extends ADR-561 (the conversion extracted),
  ADR-388 (one field everything can ask), ADR-500 (an effect is one file and four lines).
  Corrects ADR-390's reason for refusing to pack in the world, by checking whether it still applies.
  Related: ADR-374 (one medium costs +5.5 ms), ADR-401 (a test can be green about a path nobody
  renders), ADR-182 (a probe that cannot fail proves nothing).
- **Inverts one step of the brief's §46 ordering, deliberately.** §46 runs A diagnose, B clean
  analytical volume, C local banks. This is built before B. §46's ordering assumes the foundation
  exists and it did not: one live owner-facing bug and a second agent's entire effect were both
  blocked on a literal `1` in `atmospherics.cpp`, and the per-slot ray interval is simultaneously
  §31's adaptive sampling, §32's empty-space optimisation and what makes §20's self-shadowing
  affordable. Building the thing everything else stands on, first, is the ordering §46 would have
  had if it had known the slot limit was real.

## 1. What was wrong

The volumetric march had **one** medium slot. A fog bank and a cosmic vortex authored in one
project rendered **byte-identical to whichever appeared first in the array** — both ways round,
measured in ADR-560 — with the loser contributing not one pixel. `agent/tornado` hit the same
defect harder: a seven-variant showcase that renders a **flat grey frame**, because the survivor was
a 70 m dust devil sub-pixel at group distance.

Nothing said so. `AtmosphericCounts::dropped` had exactly one reader in the whole tree and it was a
CPU conformance finding, so in a running editor the number did not exist — against two comments
claiming the limit was "reported … not a silent no-op".

## 2. The slot is packed lanes, not an authored struct

`AtmosphericFrame` carries `MediumSlot media[kMaxMedia]` — 16 `vec4` and a kind tag — in place of
`bool hasVortex; Vortex vortex;`.

The alternative on the table was `{ kind; Vortex; Tornado; }`, a struct per kind. It is affordable
(~368 bytes a slot) and it was rejected for three reasons, in ascending order of importance:

1. **It grows a shared header by a member per medium kind**, which is the edit ADR-500 exists to
   delete, and §35 names four more media (smoke, clouds, dust, steam) that would each pay it.
2. **The frame already carries packed GPU structs** — `CometGpu comets[]`, `AuroraGpu auroras[]`.
   The vortex holding an authored struct was the exception, not the pattern, and the proposal was to
   generalise the exception.
3. **It deletes a bug class this exact function has already had.** `frameDiffers`'s first version
   compared the **padding between `hasVortex` and `vortex`** and reported a difference that was not
   one. A slot is `vec4` lanes with its padding declared, so a whole-struct `memcmp` is total and
   correct by construction rather than by care.

### ADR-390's objection was checked rather than inherited

ADR-390 refused to pack in the world because `packCosmicOcean` takes a `CosmicQualityScale` and the
quality tier is the renderer's to know, not the world's. Checked: **neither `packVortex` nor
`packTornado` takes a tier.** Both take only the field. The stated reason is simply absent for these
two kinds, so it does not carry.

**The reason an ADR gave is not the same as that reason still being true**, and the difference is
one grep. A medium that needs a tier at pack time is this decision's revisit trigger.

### Per-kind packing is on the schema

`EffectResolve::pack` sits beside `resolve.fill`, so a new medium kind is still one file and four
lines. The alternative was a `switch` over kinds inside `buildAtmosphericFrame` — the shared-header
edit again, one layer down.

## 3. The per-slot ray interval is a cylinder, and it is why this came first

Each slot gets a `[tEnter, tExit]` computed **once per pixel** from a bounding **vertical cylinder**;
a step outside a medium's interval costs one comparison instead of a field evaluation.

A cylinder rather than a sphere because every term in this family's field is a function of
`length(rel.xz)` and `rel.y` alone — so a cylinder is the field's own shape, and for the two cases
that matter it is dramatically tighter: a wide flat fog bank and a tall thin tornado are both mostly
empty inside their bounding spheres.

**Two independent measurements say a global step count is wrong rather than merely slow:**

- ADR-560: shrinking a medium's screen footprint moved `volume.march` only **7.14 → 6.62 ms**, 7%,
  because every pixel still marched all 32 steps across the full 4 km. Smaller on screen did not
  mean fewer steps.
- `agent/tornado`: a fixed 128-step march put **31 m between samples and broke a 24 m column into
  three disconnected blobs**; deriving the step from each medium's own narrowest feature fixed it
  (rope 462 steps, wedge 96).

**What is deliberately NOT done here:** redistributing the march's steps into the union of the
intervals. That is the larger win and it changes the fog's own integration and every existing frame
with it, so it is a separate change with its own arm and its own measurement. The step positions are
untouched; only the work at each step is skipped.

## 4. `mediaDropped` reaches a human

The frame carries `mediaDropped`. The budget is still a budget — ADR-374's +5.5 ms is real and four
slots is a march-cost decision, not a capacity one (a slot is 272 bytes; eight would be 2 KB against
a 64 KB uniform limit) — but **going over it is now said rather than counted into a field nobody
read.** That is the difference between `agent/tornado`'s empty render being explicable and being a
mystery.

## 5. A third copy of one conversion, and why it had fallen behind

Landed in the same branch and recorded here because it is the same lesson: `src/app/engine.cpp`'s
field-bus publisher held a **third** hand-written `world::Vortex` → `vortex::VortexField` copy, and
it assigned **17 of the 24 members** — omitting `eyeWallWidth`, `eyeWallGain`, `bandArms`,
`bandPitchDegrees`, `bandDepth`, `bandHarmonic` and `cloudNoise`, every one of Vortex 2.0's
macro-structure controls. The field the bus *published* had no eye wall and no spiral bands while
the march *drew* them.

Three independent copies of one conversion (ADR-401's clamps, ADR-561's conversion, this), and the
third had silently diverged. **A struct that must be copied to be used will be copied, and the
copies will diverge.** `world::Vortex` composes `vortex::VortexField` now and all three are deleted.

**Why nobody could see it**, measured: there are **zero flow subscriptions of any kind** in authored
content — every non-arm `.json` under `examples/`, every dict walked. The publisher has no
subscribers, so nothing downstream was positioned to disagree with it. ADR-420's dead-subscription
warning has no subscriptions to check.

That is the same condition `agent/tornado` found the ADR-032 grid solver in: complete, tested,
CPU-referenced, zero users, latent defect, green forever. **Tests share the product's blind spot,
because they are written against the same reachable surface the product exposes.** It is the
degenerate case of the failure family `docs/testing.md` now calls C — not a control aimed at the
wrong place, but no control at all.

## The probe, and the failure that earns it (ADR-182)

`avgen_render_tests "[media]"` — two media in one scene must produce a frame that differs from
either alone, **in both orders**. An assertion that the frame merely has two slots would pass on a
march that still drew one.

Broken deliberately by restoring the one-slot limit in the march (`min(mediumCount, 1u)`):

```
both vs first alone:  identical (36864 bytes)
both vs second alone: 26075 of 36864 bytes differ
```

**`identical (36864 bytes)`** is ADR-560's measurement reproduced exactly — the second medium
contributing not one pixel — and in the reversed order the other arm is the identical one. 1 case,
0 passed, 1 failed, exit 42. Restored: 26 assertions, exit 0.

The case also carries its own control: the two single-medium arms must differ from *each other*, or
"both differs from each" would be satisfied by a renderer drawing nothing at all.

## 6. Which check is load-bearing is not the same for every change

Two edits in this branch, verified two different ways, and the pairing is the point.

**The rename (ADR-562 part one) was ~220 regex substitutions across 12 files, and the compiler was
the witness.** Every missed site was a compile error, so "it builds" genuinely was evidence. That is
a property of *that* change — a member moving behind a `.field.` — and not of regex substitution.
Through a template, a macro, or a name that also exists on another type, a miss would have compiled
and the same method would have proved nothing.

**The WGSL uniform change is the opposite: the compiler is not a witness at all.** Replacing twelve
named `vortexN` members with `media: array<vec4<f32>, 64>` fails at **pipeline creation**, not at
compile time — a `minBindingSize` mismatch between the C++ struct and the shader's block is a
runtime error inside Dawn. The C++ builds, the shader parses, and every check short of running it on
a device passes. Only the GPU suite established that the two agree about the layout.

So: **know which check is actually load-bearing for the kind of change you are making, because it is
not always the same one.** "It compiles" ranges from conclusive to worthless depending on the edit,
and the difference is not visible in the diff.

(There is a third case in this branch: `engine.cpp`'s seven-member divergence compiled, ran, and was
wrong for two ADRs, because nothing downstream consumed it. See §5 — when there is no consumer, no
check is load-bearing, which is the degenerate case.)

## 7. The defect this branch nearly shipped, which is the one it exists to fix

Recorded because it is not carelessness and the mechanism generalises.

`resolveAtmosphericEffects` takes `std::span<ResolvedAtmospheric> vortices = {}` — **defaulted**, for
callers that want the counts and not the records. The slot cap was written as:

```cpp
if (counts.vortices >= vortices.size()) { ++counts.dropped; continue; }
```

For a counts-only caller that is `0 >= 0`: **every medium in the scene dropped, silently**, in the
subsystem whose entire purpose is that no medium is dropped silently. ADR-560's defect was one
medium lost; this was all of them.

**Why it looked like simplification.** The old code tested `counts.vortices >= 1` and guarded the
*store* separately, so counting and storing were independent by construction. Collapsing them into
one bound reads as tidying and is actually a **coupling**: the span's size is a fact about the
*caller's buffer*, not about the engine's capacity, and using it as the cap makes an empty buffer
mean "no capacity exists". The general form: **a bound derived from a caller-supplied container is
not a statement about the system's limits, and substituting one for the other is invisible until a
caller passes an empty one.**

**It was caught by a control**, and that is the part worth keeping. `test_atmospherics.cpp`'s
section is called *"the control: a real vortex still resolves as one vortex"* — it exists to make an
*adjacent* assertion meaningful, and it caught a defect the adjacent assertion was not looking for.

That is the exact inverse of the family `docs/testing.md` calls C. The controls that failed tonight
failed because they were derived from the belief under test — a probe sampling where the author
assumed the knob acted, a teeth-check defeating the mechanism the author assumed carried the safety.
This one worked because it was **not** derived from anything under test: it asserts something
boringly true that the feature's author had no reason to special-case, which is precisely why it was
still pointing somewhere useful when the author was wrong.

## 8. A measurement taken at the worst case and applied to the best case is wrong in a DIRECTION

The per-slot cost curve is the number that decides four slots versus eight, and the first attempt at
it was confounded. It is recorded because the failure is instructive rather than embarrassing.

Four media of 500 m radius placed close together, `volume.march` minima under the lock:

| slots | 1 | 2 | 3 | 4 |
|---|---|---|---|---|
| min (ms) | 8.19 | 12.06 | 14.55 | **14.02** |

A fourth slot cheaper than a third is not physical if each slot adds work. The linear fit —
**"7.21 ms fixed + 2.00 ms per slot, eight slots → 23.2 ms"** — is clean, quotable and
decision-shaped, and it was one message from being reported.

**The arms were wrong, and rendering them is what said so.** n1 and n4 come back as
indistinguishable pale washes, mean luma 131.2 against 131.4 with n3 at 138.3 — non-monotonic. Four
large banks close together merge into one volume, so *"another medium"* and *"another medium in a
region already being marched"* had become the same arm.

### The direction, which matters more than the noise

Even a clean ladder from those arms would have been the wrong evidence. **Four large overlapping
media filling the frame is where the ray interval helps LEAST** — every ray hits every cylinder, so
nothing is skipped. The case the decision is about is `agent/tornado`'s §47: seven **small,
separated** columns, which is where the interval helps **most**.

So the curve would not have been merely imprecise. **It would have argued against eight slots using
evidence drawn from the opposite geometry.** A measurement taken at the worst case and applied to
the best case is wrong in a *direction*, not only in magnitude, and that is a distinct failure from
a noisy number — a noisy number announces itself, a directional one does not.

The second, independent reason: six samples a rung, with rungs 3 and 4's five-lowest spreads
(14.55–15.53, 14.02–15.73) almost entirely overlapping. **Fixing the geometry without raising the
sample count would produce a cleaner-looking ladder that is still not separable.**

### The answer, after three more ladders

The sequential ladder above is not the only thing that was wrong with the first attempt. The arms
were re-cut small and separated -- the geometry the decision is actually about -- and the anomaly
reappeared **at the same rung with different geometry and three times the samples**, which is when
it stopped being an arm problem.

Re-run **interleaved**, paired within each repeat (ADR-460's method, which the first ladder did not
use), `volume.march` minima:

| | 1 medium | 4 media | delta |
|---|---|---|---|
| rep 1 | 5.70 | 4.26 | −1.44 |
| rep 2 | 6.16 | 4.65 | −1.51 |
| rep 3 (load ~49) | 5.11 | 4.59 | −0.52 |

**Four media measurably cost LESS than one**, in all three repeats. Note rep 3: half the magnitude
of the others and the same sign, which is what a paired delta does under load -- noisier, not
biased. **A paired delta's sign is the robust quantity and its magnitude is not.**

The mechanism is `shaders/volume.wgsl`'s `if (transmittance < 0.002) { break; }`. Four media at
density 0.010 drop a crossing ray's transmittance roughly four times faster, so the march terminates
earlier and executes fewer steps. Tested by matching optical depth -- four media at a quarter the
density -- interleaved against one medium:

| arm | mean delta vs 1 medium |
|---|---|
| 4 media, full density | **−1.16 ms** |
| 4 media, matched depth | **−0.24 ms** |

**Diluting removes 79% of the saving.** The early-out is four fifths of the effect.

**The prediction was stated in advance and was the wrong SHAPE.** It was binary -- *matched depth
must cost more* -- and the result is quantitative. By the letter it refutes; by the magnitudes it
strongly confirms. Framing a quantitative effect as a yes/no invites reading a supporting result as
a refutation, which is what happened for several minutes.

The 21% residual is not resolved, and the arm is biased against it: most rays cross **one** dilute
medium rather than four, so for those rays the dilute arm has a quarter of the single medium's
optical depth and should have been *more* expensive. The residual is therefore real and its size is
meaningless.

### So the answer is a regime, not a number

> **Per-slot cost is not additive, and the sign of the marginal depends on optical depth.** Dense
> media in one frame subsidise each other through the transmittance early-out, and at four they more
> than pay for themselves. Thin, separated media do not subsidise each other and approach the
> additive regime, which is where the measured **+0.5 ms per medium** of rungs two and three lives.

That is a better answer than the curve, because it says *when* each regime applies. `agent/tornado`'s
§47 -- seven thin separated columns -- is the additive regime, so ~+0.5 ms each is the expectation,
resting on two measured rungs **plus a mechanism that explains why they should be additive** rather
than on an extrapolation. It is still not an argument for eight slots; four stands until measured.

### What stands, and what caught it

**A second medium costs +3.87 ms on top of the first's 8.19** — at the worst geometry, both arms
tight. That is a real upper bound on the marginal cost of slot two, and it is directly the owner's
case (fog plus one vortex) rather than a hypothetical eight.

The defence that worked is `docs/testing.md`'s family-C headline: *when a filter, census or probe
returns the number you expected, that is when to check it; a surprising number gets checked for
free.* The fit looked right, which is the only reason it was checked. And the instrument was the one
ADR-560 already prescribes — **look at the picture before trusting the statistic derived from it.**

## 9. The hazard this architecture introduced, recorded beside it

Per-kind dispatch has a cost nobody priced when this was approved, and it surfaced within a day.

**A reachability probe names a field. Giving a kind its own density function silently invalidates
every probe written against the shared one** -- the probe does not break, it goes on passing while
asserting about a field the march no longer calls for that kind.

Measured when `shaders/fog.wgsl` landed (ADR-563): **three** probes in `test_fog_bank.cpp` were
mis-aimed, and the worst was the one guarding ADR-561's hole fix -- a defect that had survived in
three places and been closed with a break demonstration -- whose *fog* assertion was against the
**vortex's** envelope. From that commit onward the fix's claim for the kind it was named after was
untested and green.

**The audit is two greps and the intersection is the suspect set**: tests that call the old field,
crossed against tests that construct the dispatched kind. Run it **when the dispatch is added**,
because nothing will look wrong afterwards. `docs/testing.md` entry 22 carries the method and the
table.

So the rule this architecture comes with: **adding an arm to `mediumShape` is not complete until
the probes for that kind have been re-aimed at the arm.** The next one is the tornado's.

## Consequences

- **`EffectBucket::Vortex` is `EffectBucket::Medium`.** The name was a lie about what the bucket
  carried the moment a fog bank went in it.
- **The `static_assert` on `sizeof(AtmosphericFrame)` no longer guards a hand-maintained list.** It
  still pins the size, because a member added outside the slots is still worth being asked about;
  but `frameDiffers` compares an array with no interior padding, so the question it used to ask —
  *did you remember to add your member here* — cannot arise.
- **The debug view draws every medium**, and reading the packed lanes makes it *more* correct than
  before: `packVortex` applies the clamps, so it now draws what the shader marches rather than what
  was authored. Its own comment asked for that agreement and could not have it.
- **`spill` is lane 12.** It is a surface irradiance consumed by the lit pass, not a per-metre
  coefficient, so it appears in none of the twelve lanes the march needs — and packing without it
  would have silently broken ADR-379's glow. Found by auditing the readers before the change, which
  is the only reason it is not a regression.
- **The surface glow is still first-medium-only**, stated as a limit rather than discovered as one:
  `frame.vortexGlow` is one sphere and one colour, and summing irradiances from media at different
  distances is wrong in a way that wants a measurement.

## Revisit when

- **A medium needs a quality tier at pack time.** That is ADR-390's objection becoming true, and it
  is the one thing that would move packing back to the renderer.
- **Four slots is not enough.** It is a march-cost number and the ray interval is what pays for
  more; `agent/tornado`'s §47 showcase wants seven. Raising it is a constant and a measurement, not
  a redesign.
- **The step redistribution lands.** The intervals exist; using them to place samples rather than
  only to skip them is the next measurement, and the two independent step-count findings above are
  the case for it.
