# ADR-056: Plants that are actually simulated, for the few hundred worth simulating

Status: Accepted

## Context

ADR-055 gave the valley a wind field and gave every plant in it a way to answer, for
approximately nothing: a driven damped harmonic oscillator solved *analytically*, on the CPU, once
per draw, at the frequencies the field is known to contain. Three gains and a phase lag come out,
the vertex shader multiplies by them, and thirty-six thousand plants move without a single line of
physics running per instance. It cost +0.16 ms.

What it cannot do is have a state. A transfer function is instantaneous: the tip's offset is a
function of the field *now*, filtered and delayed, and that is all it will ever be. So a Tier 0
plant tracks a gust front exactly — it can lag it, it can take a fraction of it, but it cannot go
past it and come back. It never overshoots, it never rings, and it cannot answer anything that is
not the wind field, which rules out a body walking through it. Standing three feet from a fern,
that reads: the frond arrives with the gust and leaves with it, like a texture being scrolled.

The obvious answer — simulate the plants — is also obviously unaffordable. Thirty-six thousand
chains at any price is not a frame budget, it is a screensaver. And this frame is over budget
already and is being measured by other work in parallel, so the ceiling is real: the renderer costs
about six microseconds per *drawn* instance, roughly 1281 of them, twenty-one milliseconds at
1440x900. A millisecond is five per cent of that.

The whole design therefore turns on one number: **how many plants can a viewer actually resolve the
tip of?** In this world, near this camera, at this resolution: a few hundred. Everything below is
about spending the simulation on exactly those and on nothing else.

## Decision

### The chain, and what makes it agree with the tier below it

A plant is four movable points on a pinned root, in **normalised plant space**: the root is the
origin, the rest pose is the unit vector +Y, and every position is in units of the plant's own
height. Two things fall straight out of that choice.

The first is that the tip's horizontal offset **is** the `bend` vector `shaders/wind.wgsl` already
consumes. `vegetationDisplacement` was split into `vegetationBend` — the tip's offset in plant
heights, which is everything the field and the species decide — and `bendDisplacement` — the height
profile, the soft ceiling and the length-preserving drop, which is everything a vertex does with it.
Tier 1 replaces the first half and leaves the second untouched, on the CPU and in the shader alike.
So the root anchoring, the bend curve, the `bendLimit` saturation and the stem-length correction are
not reimplemented for simulated plants; they are literally the same code.

The second is that the two tiers can be made to *agree*, which is what lets this be a level of
detail rather than a second look. The chain is calibrated against Tier 0's own numbers:

- its **fundamental** is set to the species' `omega0 = sqrt(stiffness/mass)` — the same resonance
  Tier 0 filters the field at;
- its **static gain** is set so that under a steady wind the tip settles at exactly
  `steadyGain * strength`, the lean Tier 0 draws.

Everything Tier 1 adds is therefore *transient*. A promoted plant leans the same distance as its
unpromoted neighbours and differs from them only in how it gets there — late, past, and back. That
is the property being bought, and it is the only one.

### The bending model took two attempts, and the first one is the interesting failure

The obvious spring is: pull each point toward the straight continuation of the segment below it. It
is one line and it is wrong. It is a *one-way* spring — the joint at the soil resists its own
deflection and never the load of everything above it — so the base barely moves while the last
segment flails. Measured on a 2.4 Hz grass blade under a step: base point 0.02, tip 0.47, ringing
at something that was not the species' resonance. That is a whip, not a stalk, and it is exactly
the rubber this was supposed to avoid.

What is there instead is the discrete elastic rod. The energy is the squared curvature at each
joint, `e_j = P(j-1) - 2 P(j) + P(j+1)`, and its gradient puts equal and opposite forces on the
three points involved, so the base joint carries the reaction of every joint above it and the chain
bends in a smooth arc. A fixed virtual point one segment *below* the root supplies the boundary
condition that makes the rest pose upright rather than merely straight.

Rest length is a **constraint**, not a spring: one Gauss-Seidel pass from root to tip, root pinned,
and velocities read back out of the constrained positions rather than surviving the projection
untouched. A spring stiff enough not to stretch is a spring stiff enough to explode, and a stalk
that stretches is the single tell that reads as rubber. Tests check every segment's length every
step under a reversing drive and a shove.

### The calibration is linear algebra, not an experiment

The bending force is exactly linear in the point positions, so near upright the horizontal dynamics
is `a = K x` for a 4x4 matrix. Every number the chain needs is an exact property of `K`:

| quantity | how | value (4 points, unit spring) |
|---|---|---|
| static tip under unit uniform load | one matrix inverse | 65.0 |
| fundamental | smallest eigenvalue | 0.144 rad/s |
| stiffest mode / fundamental | largest eigenvalue | 24.3 |
| self weight that topples it | `(-K)^-1 G` power iteration | 0.0157 |

The continuum cantilever with `EI = k h^3` and a mass per unit length of 4 predicts a static tip of
32 and a fundamental of 0.22 rad/s; a four-element discretisation is softer than the continuum,
which is the direction these miss in, and the test asserts the band rather than the number.

The first version of this measured the same things by simulating — settle under a load, then time an
undamped free ring. It reported nonsense, twice. Once because a unit drive against a unit spring
lays a unit-length chain flat on the ground, so it was measuring the length constraint rather than
the spring; and once because the undamped ring it was timing drifted slowly upward and never crossed
zero, so the frequency silently stayed at its default of 1.0 and every species was mistuned by a
factor of five. An eigenvalue cannot do either of those things.

### The stiffest mode is what the time step has to survive

An explicit integrator survives a mode only while `omega * h < 2`, and the mode that matters is not
the one the plant is tuned to. A 2.4 Hz grass blade has a stiffest mode 24 times its fundamental: it
is really a 57 Hz problem, and at a 120 Hz substep it explodes. So the substep is derived per
species from `stiffRatio * omega0` with a margin, and separately from an accuracy target of twenty
substeps per period of the fundamental — whichever is smaller, capped at eight substeps a frame. A
fourteen-metre tree ends up at one substep a frame and a grass blade at four, which is the right way
round and is why the cost is not uniform across species.

If even eight substeps are not enough, the species is **softened** to the resonance the step can
carry rather than being allowed to explode. Softening moves the resonance and leaves the settled
lean alone, so the worst it can do is make a very stiff plant ring slightly slower than its transfer
function says; an exploding one would leave the valley. A test drives a 55 rad/s species at fifteen
frames a second with a disturbance two orders of magnitude too strong and checks that the result is
finite and inside the lean limit.

### Gravity, and how much of it can honestly be admitted

Gravity is a real force on a real chain here: it is applied to every point, the length constraint
absorbs its radial part, and what is left is the tangential sag of a standing column. But the
species numbers are not SI — a `mass` of 40 for a tree is a fitting constant, not kilograms — so
`g / height` in chain units is a made-up quantity, and taken at face value it topples exactly the
species whose stiffness is smallest in those invented units. The first attempt did precisely that:
the mushrooms and the trees buckled and lay down.

It is therefore admitted up to a fixed fraction (0.4) of the chain's own *measured* buckling load,
which is the only threshold in the model that means anything. At that level gravity is doing real
work — the plant is visibly softer and recovers more slowly from a shove — and because the loaded
modes are recomputed with the geometric stiffness included, the settled lean is put back exactly
where Tier 0 has it. Both halves of that matter: an earlier version had the sign of the geometric
stiffness backwards, so gravity appeared to *stiffen* the chain, and every simulated plant leant
25–115% further than its neighbours. Its agreement with Tier 0 is the test that caught it.

This is the one place where the model is doing less than it looks like it is doing. In a world whose
species parameters were in kilograms and newtons per metre, gravity would set the resting pose. Here
it modulates recovery and nothing else, and the resting pose comes from the mesh.

### Level of detail: perceptual, hysteretic, budgeted, and cheap to decide

`VegetationMotion::simulate` is per layer and off by default, so nothing written before this exists
starts simulating anything. A specimen is promoted when its **projected radius in pixels** is at
least `minScreenRadius` and it is within `maxDistance` — the same projected radius the culler and
the geometric LOD ladder already use (ADR-029), so a threshold in pixels means the same thing at any
resolution or field of view, and "big enough to be worth simulating" is measured in the same units
as "big enough to draw at all". It is demoted below `hysteresis` of that, because a plant hovering
on the boundary would otherwise be promoted and demoted on alternate frames.

`WindParams::simBudget` is a global ceiling shared out over the layers that ask, because what has to
stay bounded is the frame and not the meadow; each layer also has its own tighter `budget`.

**Deciding is the part that must not scale with the population.** The instance positions are indexed
into a uniform XZ grid once, when the scatter changes — the same O(n), in the same place, as the
bounds the culler already rebuilds there — and per frame only the cells the camera can reach are
visited, capped at twelve records for every slot still free. Incumbents are skipped entirely. In the
valley this comes to **104 records examined per frame out of 36,000**, and the test that guards it
quadruples the population and checks the examined count does not follow.

### Transitions are a property, not a fade

Up: the chain is initialised from the pose Tier 0 is drawing *this* frame and from the velocity Tier
0's own bend has right now, which costs one extra field sample at last frame's time. The tip
therefore starts exactly where the shader already had it, moving at the speed it was already moving.
There is no blend and no tolerance; there is nothing to see because nothing changed. `setFromBend`
has to land the tip exactly on a given offset *and* keep every segment's rest length, which is a
small allocation problem — hand each joint a share of the offset along a quadratic profile, cap any
share a segment cannot physically reach, give the remainder to the joints with room — and a test
checks both properties over a range of bends including ones near the geometric limit.

Down: the slot is kept for `release` seconds while the buffer carries a blend from the simulated
pose to the one Tier 0 is about to draw. By the time the slot is freed the two are the same number,
and the shader's switch changes nothing. A test watches the value the shader would draw across a
demotion and checks that it never jumps and that it equals Tier 0 exactly on the frame the slot goes.

### Sleep

A plant that has been still for `sleepSeconds` stops being integrated. While asleep it is asked what
the wind is doing on one frame in four, staggered by record index — a wind sample is nine
transcendentals and the point of sleep is not to pay them — and it wakes when the field has moved
away from where it is standing. A disturbance always wakes it on the frame it arrives, because that
test is two subtractions and a compare.

### Disturbance

`Disturbance{position, direction, radius, strength, duration}` decays as `(1 - (d/r)^2)^2` in space
— smooth at the rim, so a plant never snaps as the radius passes it — and as `(1 - age/duration)^2`
in time. One with no direction pushes radially out from its centre, which is what a landing body or
a downdraught does. The set is bounded at sixteen and the weakest gives way, because a disturbance
costs every *active* plant a distance test and an unbounded list would turn the one cost that scales
with the active set into one that does not.

The first use case is the camera moving through the vegetation, and it is a **persistent slot**
rather than an impulse per frame: a body walking through a meadow is a continuous presence, and
pushing a new impulse every frame would evict every other disturbance in sixteen frames. Its
velocity is taken from where the camera was last frame rather than from any camera rig, so it works
for a keyframed camera, a live one and a scripted flythrough alike. `wind.wake` is off by default.

### How the result reaches the GPU without a pass over the world

This was the crux, and the constraint was explicit: no new draw, no new pipeline, no per-instance
CPU pass over the population, and the existing instance buffers and the scatter renderer must carry
the result.

What crosses the bus is two things in **one** buffer per simulated layer, bound twice because WGSL
cannot read one region as two types:

- `plantBend[slot]` — a compact `vec4` per simulated plant, this frame's bend in `xy` and last
  frame's in `zw` (so the velocity buffer and TAA stay honest). Sized to the layer's budget. One
  contiguous write a frame: 399 plants is 6.4 KB.
- `plantSlots[recordIndex]` — 0 for "Tier 0, as before", slot+1 otherwise. It is a per-record array,
  but it is only ever *written* where the active set changed, coalesced into ranges. With a static
  camera that is zero writes a frame; with a moving one it is the churn, which is tens.

The vertex stage gains one uniform-valued branch (`prevInfo.y`, zero for every draw that existed
before this) and, inside it, one storage read per vertex. Everything downstream is the arithmetic it
already was. Objects with no simulated specimens bind the same inert 256-byte placeholder the
visible list already uses.

An object with effectors is excluded from Tier 1 entirely: the GPU moves its records after this
point, so the CPU positions the level-of-detail decision reads would be the wrong ones.

## What it costs

Interleaved A/B, 150 frames at 240 fps and 1440x900, `examples/world/terrain.scene.json` against
`examples/world/_tier1.scene.json`, which is the same file with a `simulate` block on grass, ferns,
flowers and bushes. **Frame minimum**, never the median: another agent was building on this machine
throughout, and the median is unusable under contention. Every run prints the number of plants it
simulated, so an arm that failed to apply cannot be mistaken for a null result.

Sky-only calibration at the same size: 2.31 / 2.26 before the first set, 2.83 after; 2.73 before the
second set, 2.59 after. Both sets are inside the expected 2.6–3 ms band and neither moved during a
set.

| arm | plants | frame min (5–6 runs, ms) | mean | cpu(proc) |
|---|---|---|---|---|
| Tier 0 (set 1) | 0 | 21.32 20.59 21.33 21.26 21.43 | 21.19 | 0.05–0.09 |
| Tier 1 (set 1) | 399 | 21.58 21.31 21.78 21.34 21.48 | 21.50 | 0.14–0.19 |
| Tier 0 (set 2) | 0 | 20.98 21.21 20.97 20.99 20.87 20.92 | 20.99 | 0.05–0.08 |
| Tier 1 (set 2) | 399 | 21.54 21.17 20.84 21.14 21.01 21.14 | 21.14 | 0.12–0.16 |

**399 simulated plants cost about +0.2 ms** — under 1% of a 21 ms frame. Set means differ by +0.31
and +0.15; taken pair by pair in the order they were run, eleven pairs come out +0.26, +0.72, +0.45,
+0.08, +0.05, +0.56, -0.04, -0.13, +0.15, +0.14, +0.22, so nine of eleven favour the same direction
and the mean is +0.22 ms.

That is worth being careful about, because the two Tier 0 arms — the same file, the same binary, on
either side of a set — differ by 0.2 ms between sets, which is the same size as the effect. The
frame minimum on this machine simply does not resolve a fifth of a millisecond in one pair. What
does resolve it is `cpu(proc)`, the renderer's own CPU timer, which is unambiguous and moves the
right way every single run: **0.05–0.09 ms with nothing simulated, 0.12–0.19 ms with 399 plants,
0.38–0.43 ms with 1681**. The frame-min A/B and the CPU timer agree on the order of magnitude, and
the slope below is large enough to be clear of the noise on its own.

The slope, from a deliberately over-budget arm (`_tier1big.scene.json`, every threshold dropped):

| arm | plants | frame min (3 runs) | mean | cpu(proc) |
|---|---|---|---|---|
| Tier 0 | 0 | 21.31 21.20 21.32 | 21.28 | 0.06–0.08 |
| Tier 1 | 1681 | 22.14 21.93 21.74 | 21.94 | 0.38–0.43 |

**0.39 us per plant per frame**, of which about 0.22 us is the CPU update. At the design point of a
few hundred that is a fifth of a millisecond; at four times the design point it is two thirds. The
budget is a hard ceiling and the cost is linear in it, which is the property that was asked for.

What is *not* claimed: the cost of the new shader branch and the two extra bind group entries when
nothing at all is simulated was **not** measured against a pre-change binary. Both arms above are
the same binary, so everything above is the cost of the physics and its transport, not of the
feature existing. On inspection there is nothing per-frame to pay — the branch is on a uniform that
is zero for every draw that existed before this, and the two extra entries point at a shared
256-byte placeholder created once — but that is an argument, not a measurement, and the honest thing
is to say so rather than to quote a number nobody took.

## Does it look different?

Yes, and by more than the numbers suggest. Rendering the same frame of the same scene with and
without Tier 1: **4% of the 1440x900 frame differs**, with peak per-pixel differences of 532 out of
765, and the difference is concentrated at the tips of individual clumps and is exactly zero at
their bases — which is the root anchoring, visible in the diff. It appears on scattered individual
plants near the camera rather than as a global shift, which is the per-instance level-of-detail
decision, also visible in the diff.

Numerically, over ten seconds of the valley's own wind field, a simulated grass blade differs from
its transfer function by a mean of half its own travel while agreeing with it to within 8% on
average amplitude. That is the shape of the thing: same size, different moment.

## What was tried and rejected

- **A one-way bending spring.** Cheapest correct-looking thing; makes a whip. Above.
- **Calibrating by simulation.** Two separate silent failures. Above.
- **Gravity at `g / height`.** Buckles the species whose invented stiffness is smallest. Above.
- **A physics library.** Out of scope by instruction, and nothing here needs one: the chain is four
  points, the constraint is a length, and the only non-obvious mathematics is a 4x4 eigenvalue done
  once at start-up.
- **Scattering the results into the existing instance records.** One `writeBuffer` per active plant,
  a few hundred a frame. Rejected before measuring on the grounds that Dawn's per-call overhead
  would put it in the same order as the entire physics budget; the compact array with a slot map
  costs one write instead.
- **Permuting the record buffer so the active set is contiguous.** Would have removed the slot map
  entirely — instance records are interchangeable, so a permutation is visually free — but it makes
  every promotion two scattered record writes instead of one four-byte one, and it entangles Tier 1
  with the culler's compacted lists for no gain.
- **A compute pass over the population to apply the bends.** A dispatch over 36,000 records is
  microseconds of GPU time, but it is a per-instance pass over the whole world by any honest
  reading, and it needs the "live" record buffer that only objects with effectors have.
- **Turning it on in `terrain.scene.json`.** Deliberately not done. That file is the reference scene
  for the performance work running in parallel, and changing its cost mid-flight would corrupt
  somebody else's A/B. `_tier1.scene.json` carries the settings; promoting them is a one-line change
  once that work lands.

## What is deliberately left undone

- **Tier 2 hero physics** — out of scope by instruction.
- **Collision.** A disturbance is a field, not a body: plants are pushed aside by the camera's wake
  but nothing is swept by actual geometry, and two plants do not touch.
- **The chain's shape is thrown away.** Four points are integrated and only the tip's offset reaches
  the shader, which then draws the species' `bendCurve` profile through it. A chain that whips has an
  inflection the profile cannot express. Passing a second control point would cost eight more bytes
  a plant and would break the exactness of the tier transition, which was judged the worse trade at
  this size on screen; it is the obvious next increment if hero plants ever want one.
- **Sleeping plants still hold their slot.** Sleep saves the integration and the wind sample, not the
  budget. A meadow that has gone completely calm keeps its active set rather than handing it to
  plants further away, which is harmless but not free.
