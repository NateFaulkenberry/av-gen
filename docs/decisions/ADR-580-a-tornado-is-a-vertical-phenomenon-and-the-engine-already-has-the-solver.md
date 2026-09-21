# ADR-580: A tornado is a vertical phenomenon, the shipped shot has no vortex in it, and the engine already has a fluid solver

- Status: Accepted (2026-09-20)
- Opens the Tornado world effect (ADR range 580-599). Phase 1 of the owner's brief: research and
  architecture, no implementation.
- Extends ADR-460 / ADR-461 (the Vortex 2.0 rebuild), ADR-374 (the vortex is a funnel and the
  camera must be outside it), ADR-388 (the vortex as a sampler), ADR-500 (an effect is one file and
  four lines), ADR-032 (simulated fields and volumetric atmosphere).
- **Inverts ADR-461's blocking finding rather than working around it.** ADR-461 concluded "the
  shipped hero shot cannot hold a cyclone seen from above" and left the camera question open. That
  finding is correct and it does not apply to a tornado, for a reason that is one trigonometric
  ratio and is measured below.

## 1. The shipped frame contains no vortex, and that is not a matter of taste

The owner's complaint is that the effect reads as "procedural noise / stippled particles". ADR-460
established that structurally, and this is the same statement made from the shipped deliverable
rather than from the field:

```
./build/release/src/avgen --headless \
  --project examples/treeisland/tree-of-life-floating-island.json \
  --render <out> --format png --range 6:6 --fps 30 --size 960x540
```
sequence hash `996627a0403c6393`, 1 frame, 0 GPU errors.

What is in it: a tree on a floating island, centred, against a teal-to-green wash occupying the
whole of the lower two thirds of the frame, speckled with salt-and-pepper grain. **There is no
funnel, no eye, no rim, no silhouette and no rotation visible anywhere in the frame.** The teal
wash *is* the vortex -- it is the far lip of the funnel seen edge-on that ADR-371 called "a band of
atmospheric depth" -- and the speckle in it is the march's own step jitter that ADR-461 measured.

This is worth writing down because it changes what the Tornado has to beat. The bar is not "a
better funnel". There is nothing in the picture to improve on.

## 2. The hero camera favours a vertical column over a horizontal disc by 6.5 to 1

ADR-461's argument, restated exactly: a cyclone's readable features -- eye, eye wall, spiral
rainbands -- live in a **horizontal** plane, and their apparent size from a camera at depression
angle `d` is compressed by `sin(d)`. The hero camera's `d` is 8.80 degrees, so that factor is 0.15,
and everything at a steeper depression is behind the island. The conclusion followed.

A tornado's readable features -- the funnel's taper, the debris skirt, the wall cloud, the helical
striations, the whole silhouette -- live in a **vertical** plane. Their apparent size is scaled by
`cos(d)` = 0.988. The ratio is `cot(8.80 deg)` = **6.46**.

Measured from the project that renders (ADR-264: the project overrides the scene's camera, and
`make_vortex2_arms.py` records the afternoon that cost):

| | |
|---|---|
| camera | (197.68, 45.34, 83.46) |
| target | (-16.91, 9.35, -5.99) |
| distance | 235.2 m |
| depression | 8.80 deg |
| field of view | 36 deg vertical, 60 deg horizontal |
| the island and tree occupy | +/- 15.0 deg horizontally (ADR-461, off the shipped render) |

So there are **15 degrees of clear frame on each side of the hero**, and a column standing in it:

| height | distance | vertical extent | of a 36 deg frame |
|---|---|---|---|
| 300 m | 500 m | 33.0 deg | 92% |
| 400 m | 700 m | 31.5 deg | 88% |
| 600 m | 1000 m | 33.0 deg | 92% |
| 1200 m | 2000 m | 33.0 deg | 92% |

A column at roughly `D = 1.7 H` fills nine tenths of the frame's height, and at 20-25 degrees of
azimuth it stands clear of the island entirely -- 364 to 466 m of lateral offset at D = 1000.

**ADR-461's finding still bites for a cyclone and does not bite for a tornado.** The reason is not
that the shot got better; it is that a tornado is the one atmospheric phenomenon whose structure is
aligned with the axis a level camera is good at. This is a prediction from geometry and is not yet
a render; §47's dedicated showcase scene is what it will be judged in either way.

## 3. The existing Vortex is a hurricane, and the difference is not cosmetic

`shaders/vortex.wgsl` and `core/vortex.{hpp,cpp}` are, in their own vocabulary: mouth radius, eye,
eye wall, spiral rainbands at a pitch angle "real rainbands run 10 to 25 degrees" at, funnel depth,
throat. Every one of those is a feature of the horizontal plane at a given height. `vortexEvaluate`
computes `rr = length(rel.xz) / radius` and `angle = atan2(rel.z, rel.x)`, and the entire macro
structure -- `vortexRadialProfile(v, rr)` and `vortexSpiralBands(v, rr, angle, t)` -- is a function
of `(rr, angle)` with height entering only as a Gaussian wall and a throat taper.

That is a cyclone seen from above. It is a good one and ADR-460/461 built it deliberately. It is
not a tornado and no parameter of it is.

The owner's §2 instruction -- "do NOT simply rename the existing class/UI from Vortex to Tornado
while retaining the same implementation" -- is therefore not a risk this branch has to resist. The
implementation cannot be retained; it computes the wrong thing.

## 4. The engine already has a GPU fluid solver, and nobody has pointed a volumetric at it

This is the finding that decides the architecture, and it was not in the brief's list of
alternatives.

`spatial::GridField` (ADR-032) + `rendering::Simulation` + `shaders/simulate.wgsl` is a **3D
semi-Lagrangian advection solver on the GPU**, shipped, tested and deterministic:

- `cs_inject`, `cs_advect` (semi-Lagrangian back-trace, trilinear gather), `cs_diffuse` (Jacobi),
  `cs_dissipate`, `cs_reaction`, double-buffered, gather-only, no atomics.
- Scalar (1 float), Vector (4 floats) or Gray-Scott (2 floats) per cell, up to 128^3, with a
  scene-wide budget of 2M floats (8 MB) and 8 grids.
- Fixed sub-step `dt = 1 / simRate`; the step count is derived from `time.renderTime` and **never
  from the wall clock**, which is what makes it ADR-360-safe.
- Advected by any **vector field** named in the scene (`velocityField`), injected by any scalar
  field (`injectField`).
- Sampled from any shader through `fields.wgsl::gridTrilinear` over one shared storage buffer at
  group 0 binding 15 -- and `volume.wgsl::volumeDensityAt` **already multiplies fog density by a
  named density field**.

So the brief's §24 hybrid -- "analytical tornado field -> guide velocity -> low-res simulation /
advection -> macro turbulence -> procedural detail -> volume ray march" -- is, for its middle
stage, a configuration of code that already exists. What is missing is exactly one thing: the
vector field kinds in `spatial::FieldKind` are `Direction, RadialVector, Attractor, Repulsor,
Vortex (a bare normalised cross product), CurlNoise, Spiral, WaveVector`. None of them is a tornado
velocity field. One new kind makes the solver reachable.

Counted honestly, what the engine does **not** have: **no 3D textures anywhere in the tree.**
`grep -rn "e3D"` over `src/gpu` and `src/rendering` returns nothing. The grids are flat storage
buffers. That is fine and probably right on Apple Silicon at 64^3, and it means §23's option (A)
"3D density texture" would be new GPU infrastructure while option (D) is a configuration.

### And the trap in it, measured before relying on it

`Simulation::update` gives a grid `kCatchUpSteps = 240` sub-steps on its first frame and
`maxSubSteps` (default 4) on every frame after. A single-frame render -- `--range 6:6`, the idiom
the entire repository uses and the one ADR-521 caught doing this to particle emitters last week --
gives a 60 Hz grid 240 of the 360 sub-steps it is owed; `--range 30:30` gives it 240 of 1800, and
logs `grid '...' starts N sub-steps behind; skipping ahead`.

This does **not** violate ADR-360: two renders of the same range agree, because the skip is a pure
function of `renderTime`. It does mean a grid-backed tornado looks different depending on where the
render starts, and that a still frame is a shot of a partly-spun-up storm.

**So the silhouette may not live in the grid.** The analytic tier has to carry §38 Mode 1 and §39
panel A on its own, correct at any `t` with no history at all. That is what §45 demands for
artistic reasons and it is what determinism demands for a separate reason, and the two point the
same way, which is the most reassuring thing found this phase.

## 5. Decision: the architecture

**`Tornado` is a new first-class effect with its own field, built on the `core/wind` and
`core/vortex` pattern this repository has used twice and proved both times.**

`src/core/tornado.{hpp,cpp}` and `shaders/tornado.wgsl`: a pure function of (packed uniforms, world
position, time), no state, no wall clock, with the WGSL as a transliteration of the C++ and a
parity test that compares the two **through the packed form** so both sides start from bytes that
are identical by construction. That single decision buys four consumers one answer: the volumetric
march (density), particles (§25's secondary layer, via `world::fields::FieldBus`), the grid solver
(§24's advection, via a new `FieldKind::Tornado`), and a debug overlay (§38's diagnostic modes).
ADR-388 records what it cost when only the march could ask where the funnel was.

### The velocity field is Burgers-Rott, not a hand-rolled sum

The brief's §13 sketch (`tangential * swirl + axis * lift + normalize(radial) * radial`) is three
independent knobs. The **Burgers-Rott vortex** is the same three components as an exact steady
solution of Navier-Stokes, and it is the model the literature names for tornadoes specifically
because it is the only standard one with radial inflow *and* axial updraft:

```
u_r     = -a * r / 2
u_z     = +a * z
u_theta = (Gamma / (2 pi r)) * (1 - exp(-a r^2 / (4 nu)))
```

Three properties earn it the place:

1. It is **divergence-free by construction**: `div u = (1/r) d/dr (r u_r) + du_z/dz = -a + a = 0`.
   Free incompressibility with no pressure solve, which is most of what a real-time fluid cannot
   afford.
2. It is **steady**. Lamb-Oseen's core grows as `sqrt(4 nu t)` and the storm dissipates on its own;
   Burgers-Rott's stretching term `a` is what holds the core together, which is also the physically
   correct reason.
3. `a` is a single **intensity** control that narrows and speeds the core together, which is how an
   artist thinks about a tornado and is not how three independent sliders behave.

Rankine (`u_theta = Omega r` inside `R_c`, `Gamma / 2 pi r` outside) is rejected as the base model:
no radial and no axial term, so it is a spinning tube, and its velocity gradient is discontinuous
at the core boundary -- ADR-369's rule about there being no edge for a hard line to live on.

The artist controls of §17-§19 are height-curves and multipliers **on these three terms**, not
three separate fields: `Rotation Speed`/`Rotation Curve`/`Height Influence` shape `Gamma(h)`,
`Lift`/`Buoyancy`/`Updraft` shape the `a` in `u_z`, `Radial Flow`/`Compression`/`Expansion` shape
the `a` in `u_r`. Letting the two `a`s diverge costs divergence-freedom, so the panel says so and
the default locks them.

### The axis is a curve, not a line

§16 and §28 ask for asymmetry and a shape that is not a perfect cone; the SideFX references build
their tornadoes around a **guide curve** and drive velocity along it. Bridson's vortex-curve stream
function (curl noise paper, eq. 8) is that, in closed form and divergence-free:

```
psi_curve = f(|x - x_C| / R) * ((R^2 - |x - x_C|^2) / 2) * omega_C
```

with `x_C` the closest point on the curve and `omega_C` the angular velocity tangent to it. Taking
the curl of the `((R^2 - r^2)/2) omega` term gives `u_theta = omega r` -- the Rankine inner core --
with `f()` supplying the outer falloff. So a bent spine, a divergence-free rotational field that
follows it, and an added Burgers-Rott radial/axial pair is a tornado that leans, wobbles and ropes
out, analytically. `axisAt(h)` is a low-frequency lateral displacement in `h` and `t`; it is what
stops the result being a mathematical cone and it is structure, not noise.

### Density is a separate field from velocity, and it has four named parts

§22's "cloud mass is the priority" and §15's core/eye are density, not flow. Four analytic terms,
all band-limited, all surviving §38 Mode 1:

1. **The condensation shell.** A real funnel is a condensation surface: initially transparent,
   opaque where it carries debris. So peak density sits in a *shell* near `r = radiusAt(h)` with
   `coreRadius` / `coreDensity` / `coreFalloff` deciding whether the interior is solid, hollow or
   turbulent -- §15's four requested looks are the corners of that.
2. **The radius profile.** `radiusAt(h)` from a three-point artist curve (bottom, mid, top) plus a
   taper exponent, so cone, stovepipe, wedge, hourglass and rope are one expression. Reference
   proportions to aim the defaults at: classic cone is **6:1 to 15:1** height to width, rope is
   **20:1 to 40:1**, a wedge is **1:1 or wider** by the WMO's own definition.
3. **The debris skirt.** The single strongest read cue after the silhouette, and it is structure:
   a flare at the base **1.5x to 3x the funnel's ground width**, occupying 5-15% of the height,
   widening downward against the funnel's taper, denser and less luminous. The literature is blunt
   about the order of events -- debris swirls are visible *before* the condensation funnel reaches
   the ground -- so the skirt is not a decoration on a finished funnel.
4. **Helical striations.** The tornado's transposition of the hurricane's spiral rainbands, out of
   the `(r, theta)` plane and into the `(theta, h)` plane: `cos(n * (theta - k*h + omega*t))`, mean
   exactly 1 over angle (ADR-389's family rule, since `density` is a per-metre coefficient
   calibrated against this field's mean). This is what makes a *still frame* read as rotating, and
   a smooth funnel is ambiguous about whether it is spinning at all.

### Temporal coherence is free, because the flow is analytic

§29 forbids per-frame random noise and asks for advected noise. Because the velocity field is
analytic and steady in the column's own frame, the streamline coordinate is available in closed
form for the dominant terms: the noise is sampled at `theta - omega(r,h) * t` and `h - lift * t`
rather than at `p`. Detail *rides* the flow, deterministically, at any `t`, with no state and at
zero cost. The current vortex already half-does this (`warped = angle + rr*swirl + t*rotationSpeed`);
the difference is doing it in `(theta, h)` with a height-varying `omega`, which is what produces
the stretch and shear that reads as material being carried upward.

Curl noise (§43 Q4) is the secondary velocity perturbation, applied as a **divergence-free warp of
the sample point** rather than as a force on a simulated field, so it displaces density without
creating or destroying it. Bridson's rule is obeyed where it is easy to get wrong: a ramp must
multiply the **potential** `psi`, not the velocity -- `v = curl(A(x) psi(x))` is divergence-free and
`A(x) v(x)` is not. Each octave is ramped at its own length scale, with the ground as a free-slip
boundary, which also produces the skirt's outward flare for nothing.

### Vorticity, answered honestly rather than shipped as a dead knob

§20 asks for vorticity confinement. Fedkiw/Stam/Jensen's term is `f = epsilon * h * (N x omega)`
with `N = grad|omega| / |grad|omega||`, and its entire purpose is to **put back energy that
numerical diffusion removed**. An analytic field has no numerical diffusion. There is nothing to
confine, and a control labelled Vorticity wired to a confinement term over an analytic field would
be ADR-421's defect -- a control that does nothing teaches an artist the system is broken.

What §20 actually asks for is the visual consequence. In the analytic tier that is delivered by two
things, and `Vorticity` / `Vorticity Scale` / `Vorticity Falloff` drive them:

- the curl-noise amplitude, masked by the analytic `|omega_z| = omega_0 exp(-r^2 / R_B^2)` so detail
  appears where the rotation is rather than uniformly; and
- **secondary suction vortices** (§27): 2 to 6 scaled child vortices orbiting the parent axis at
  roughly the radius of maximum wind, faster than the parent turns, each the same model. These are
  real -- multi-vortex tornadoes have them, and they are what makes the base ragged.

If the grid tier lands, the same three controls additionally drive a real confinement kernel over
the grid, where it means something. Two extra dispatches; not free; not measured yet.

### Rendering stays inside the one shared march

The fog agent owns `shaders/volume.wgsl`, the medium-slot architecture and the density-field/march
abstraction. This branch does not restructure the march. **The reason is a live bug**: `EffectBucket
::Vortex` caps at one medium (`atmospherics.cpp:820`), `volumetric_fog_effect.cpp` resolves into the
same bucket, and the loser increments `AtmosphericCounts::dropped` -- which is read by
`effect_conformance.cpp` and by **no user interface anywhere**. That is the owner's "if I turn on
fog I can't see the vortex", exactly, and two separate volumetric paths would make it permanent.
The requirements this effect places on that foundation are listed in §7 below.

### Quality tiers (§35)

| tier | what is on |
|---|---|
| Draft | analytic structure only: shape, shell, skirt, striations. §38 Mode 1. |
| Preview | + macro and meso advected noise, curl warp |
| High | + micro detail, secondary eddies, self-shadowing |
| Cinematic | + the grid tier, full steps, per-medium jitter policy |

Mapped onto the existing `QualityTier {Preview, Realtime, High, Offline}` plus a per-effect
`Detail Quality`, because ADR-035 lets a tier scale sample counts and resolutions and this is both.

### Expected memory and cost, stated as predictions so they can be wrong

- **Analytic tier: zero additional memory.** Roughly 12-14 `vec4`s of uniform (`VortexUniforms` is
  128 bytes; Tornado will want 192-224). The cost is ALU inside the existing march.
- **A tornado should be cheaper than the vortex it replaces**, and this is a falsifiable prediction
  rather than a hope. ADR-374 measured the cost of a placed medium as **pixel coverage of non-zero
  density**, not march length -- a 4 km march is cheaper than a 1 km one. The shipped vortex is a
  200 m-radius funnel filling the lower two thirds of the frame; a tornado is a narrow column.
  ADR-460's model is `2.7 ms + 0.196 ms per step` at half res with the funnel filling the frame.
  Predicted: **1.5 to 3 ms against the shipped 5.5** at equal steps. Unmeasured. If it comes back
  higher, the coverage model is wrong and that is the more interesting result.
- **Grid tier: 1 MB** for a 64^3 scalar density grid (262 144 cells, 1 float), against the 8 MB
  scene budget. Velocity needs no grid at all -- it is analytic, so §43 Q6's "can density and
  velocity live at different resolutions" is answered by construction rather than by a second grid.
  Sim cost unmeasured.

### §43, answered

1. **Enough structure without a solver?** The architecture says yes and §38 Mode 1 is the evidence;
   the claim is not made until that render exists.
2. **Low-res advection for macro smoke?** The solver exists (§4). Untested at tornado scale.
3. **Procedural density advected through the field?** Yes, analytically and for free (§5).
4. **Curl noise as secondary perturbation?** Yes, as a warp of the sample point, ramped on `psi`.
5. **Vorticity confinement at acceptable cost?** Not in the analytic tier -- there is nothing to
   confine. In the grid tier, 2-3 dispatches; unmeasured.
6. **Density and velocity at different resolutions?** Yes: velocity is analytic, density is a grid.
7. **Temporal reprojection?** No. ADR-143 rejected it and ADR-460 re-confirmed the reopening
   trigger is shut: this march is not pixel-proportional (quartering the pixels saves 46%).
8. **Sparse/tiled 3D textures?** Not applicable -- there are no 3D textures in this engine at all.
   The grid is a flat storage buffer and a tornado's bounding cylinder is already a tight fit.
9. **WebGPU compute for the simulation?** Already done, in `shaders/simulate.wgsl`.
10. **Best density representation on Apple Silicon?** Flat storage buffer with trilinear gather, at
    64^3. Measured rather than assumed only once the tier exists.
11. **Convincing tornado without Navier-Stokes?** Burgers-Rott *is* an exact Navier-Stokes solution
    evaluated in closed form. The question is not whether to solve it but where.
12. **Which of EmberGen's workflow is essential?** The Line Force -- push (axial), twist
    (tangential), repel (negative = inflow) about a line segment, with a falloff curve. That is
    Burgers-Rott as three artist sliders, and it is the whole tornado.
13. **Which can be approximated analytically?** All of the above, plus the rotating emitter (a
    phase offset), the shape noise (a warp of `radiusAt`) and the noise force (curl noise).
14. **Which genuinely require simulation?** Only the large-scale *history* -- smoke that has been
    thrown clear of the funnel and is no longer in its flow. That is the grid tier's one job, and
    it is the one thing an analytic field genuinely cannot do.

## 6. Consequences

- `main` at `4d2a224e` **does not compile.** `sizeof(AtmosphericFrame)` is 2400 against the
  `static_assert` of 1640 in `effect_conformance.cpp:45`, because the Cosmic Ocean merge grew the
  frame. The guard worked. A minimal local fix rode on this branch for one commit so that Phase 1
  could build, and it is **dropped** in favour of the coordinator's, which was already in flight.

  **The reading attached to that stopgap was wrong, and the correction is worth more than the
  stopgap was.** This ADR first claimed that "a conformance check that varies only the ocean's
  interior still reports no difference". That is true of the committed tree and **false of the
  fix**, which handles the padding hazard exactly where this branch located it: `enabled` is
  compared on its own, and the `memcmp` runs from `offsetof(CosmicOcean, intensity)` to the end of
  the struct, pinned by `static_assert(offsetof(CosmicOcean, intensity) == 4)` and a `% 4 == 0` on
  the remaining span. Interior variation **is** caught.

  The evidence is the kind this repository asks for rather than an assurance: the suite reports
  `cosmicOcean [flow-reaches]` -- a check that requires a *difference* and correctly found none.
  It could not report that at all if the comparison were blind to the payload. So the finding is
  the ocean not reading the flow, not the comparison not seeing the ocean. Recorded because the
  wrong version of this paragraph would have made a later agent re-fix a function that was already
  correct, and because "I found a gap" is a claim that needs the same evidence as any other
  (ADR-385).
- `scene/composition.hpp` carries six dead `vortex*_` parameter pointers (`-Wunused-private-field`
  at lines 1408-1414). §42 removal work will collect them.

## 7. What the shared volumetric foundation must provide

Listed in the order the Tornado breaks without them. This effect does **not** need the foundation to
know anything about velocity, advection or vorticity: those are analytic and are evaluated inside
its own density function. It needs a slot, its own uniform bytes, and a call.

1. **More than one medium slot.** §47's showcase scene has seven tornadoes in one frame, and the
   owner's live bug is one tornado and one fog bank. `volumeTotalDensityAt` already *sums* fog and
   vortex, so the shape is right; the cap is in `atmospherics.cpp:820`. Minimum 4 slots, ideally 8,
   summing into one density, one transmittance, one early-out. And `AtmosphericCounts::dropped`
   must reach a human.
2. **Per-slot density dispatch.** `volumeTotalDensityAt` hard-codes
   `volumeDensityAt(p) + vortexShape(p, t) * vol.vortex1.w`. A slot needs a kind tag and its own
   uniform block, so a Tornado slot calls `tornadoDensityAt` and a Fog slot calls the fog's.
3. **Per-slot emission, scattering weight and phase `g`.** There is one `vortexEmissionAt` and one
   `vortex5.z` today. A dark, lit, forward-scattering tornado and a self-luminous isotropic nebula
   cannot share them.
4. **Per-slot ray interval.** The highest-value performance affordance the foundation can give,
   because ADR-374 measured that cost *is* coverage of non-zero density. A tornado is a narrow tall
   cylinder: if each slot reports `[tMin, tMax]` along the ray and the march unions them, the steps
   outside every medium cost nothing.
5. **`filterWidth` passed into the density function.** ADR-389's octave band-limit depends on the
   world-space sample spacing and it is deliberately not a knob. `vortexFilterWidth()` computes it
   from `volumeMaxDistance / volumeSteps` today; keep it and pass it.
6. **Keep `Environment::volumeJitter`.** ADR-461 put it there on purpose. A tornado is the case
   ADR-461 said it could not construct -- high optical depth *per step* -- so this branch will
   measure it rather than inherit a default.
7. **A shared self-shadowing light march, if it is in scope.** N taps toward the key light
   accumulating extinction through *all* slots is far better shared than duplicated, and it is the
   single largest visual win available for a smoke column (§31). If it is not in scope, say so and
   this branch will do it inside its own slot and pay for it.
8. **Keep grid/field sampling reachable from inside the march.** `fieldScalar(slot, p)` and the
   ADR-032 `gridTable` at group 0 binding 15. The optional grid tier depends on multiplying density
   by a named grid field from inside the march.
9. **`Vortex`-the-struct is shared.** `volumetric_fog_effect.cpp` stores in `e.vortex` with
   absolute `/vortex/...` JSON paths, and 26 files under `examples/` write that block. Renaming or
   re-homing it is a joint migration, not a unilateral one.
10. **Publish the WGSL include order.** `shaders/vortex.wgsl` records that the include directive
    does not de-duplicate and that ADR-360 learned it the expensive way.

## 8. What was built, and which of §5's predictions survived

Phases 2 to 6. §5 above is the architecture as proposed; this is the architecture as measured, and
the two differ in places that are recorded rather than quietly reconciled.

### 8.1 The silhouette, and the two things the renders decided that reasoning had not

`core/tornado.{hpp,cpp}` and `shaders/tornado.wgsl` on the `core/wind` / `core/vortex` pattern:
a pure function of (packed uniforms, world position, time), thirteen `vec4`s, CPU beside WGSL.
`world::Tornado` holds `tornado::TornadoField` **by value** rather than restating its members --
the one deliberate departure from `world::Vortex`, because ADR-388 records what the restated
version cost (`packVortex` with one caller, and the bytes the shipped frame marched assembled
somewhere else entirely).

Two corrections came from looking at a render, not from thinking about one:

**The first silhouette was a champagne flute.** Making the wall cloud the funnel's own top radius
flares the column continuously from about a third of its height, and a continuous flare is the one
thing that cannot read as a tornado -- it rendered as a smooth trumpet, unmistakably a vortex of
some kind and just as unmistakably not a tornado. The references say why: a classic funnel is
narrow for most of its height and the wall cloud is a distinct mass three to ten times wider that
it descends from. **The proportion that says "tornado" and nothing else says is a thin column
under a broad cloud with a SHOULDER between them.** The cloud is its own term now.

**A smoothly ramped cloud renders as a flying saucer.** Density easing from the cloud's underside
to its top is an ellipsoid whose only edge is the silhouette of an ellipsoid. It rises to full
within the lower third of the cloud band and holds, so the base is flat and heavy.

Neither was predictable from §5 and both were obvious in one frame.

### 8.2 The cost prediction was right, and it is not yet the measurement that matters

§5 predicted 1.5 to 3 ms against ADR-460's 8.13 for the vortex at 32 steps, on the argument that
ADR-374's cost model is coverage of non-zero density rather than march length.

`tools/gpu-lock.sh`, 1920x1080, half-res volume target, **minima over five to seven interleaved
repeats** (ADR-170):

| arm | `volume.march` |
|---|---|
| tornado, 32 steps | **1.25 ms** |
| tornado, 64 steps | **2.49 ms** |
| tornado off (the gate) | no `volume.march` pass at all; frame 1.05 ms |
| *ADR-460's vortex, 32 steps, half res* | *8.13 ms* |

**This is consistent with the coverage model and it is NOT a like-for-like comparison.** The lab
scene's column covers perhaps 15% of the frame against a funnel filling the lower two thirds. The
like-for-like test is the hero shot and belongs to Phase 7. Quoting 6.5x from this table would be
the ADR-389 family's mistake in a new place -- a ratio between two quantities measured under
different distributions.

Two of the early repeats attributed the entire march to `shadowmask` -- 2.69 ms with
`volume.march=0.00`, in a scene with one light and no geometry. A mean over those runs would have
been a number. ADR-170's "minima, never means" earned its keep on its first use here.

### 8.3 A thin medium at a global step count is WRONG, not slow

The Rope preset rendered as three disconnected ellipsoids. The obvious reading is a shape bug. It
is not:

| arm | result |
|---|---|
| rope, 64 steps | three disconnected ellipsoids |
| rope, 64 steps, **striations off** | three disconnected ellipsoids -- identical failure |
| rope, **256 steps** | a continuous twisted rope |
| rope, 1024 steps | indistinguishable from 256 |

A 24 m column sampled every 62.5 m is stepped straight past. **This is the strongest argument in
this record for the per-slot ray interval** (§7 item 4), and it is stronger than the cost argument
both agents started from: a medium narrower than the step spacing is not rendered coarsely, it is
rendered *wrong*, and no amount of patience fixes it because the samples are not there.

It recurred in §47's showcase at a fixed 128 steps, which is how it came to be a *rule* rather than
a number: each arm now derives a step of about a third of its own narrowest feature -- rope 462,
wedge 96. Expensive where it must be and cheap where it need not be, which is the correct
relationship and the opposite of a constant.

### 8.4 ADR-461's jitter finding replicates on a second, independent medium

The first render at 64 steps was salt-and-pepper throughout; `volumeJitter 0` removed all of it and
revealed helical striations that had been there the whole time. This is the high-optical-depth-
per-step case ADR-461 said it could not construct. **The default is not proposed for change here**
-- two media finding jitter unprofitable is a pattern, not yet a measurement of the control -- and
the lab scene authors 0 explicitly rather than inheriting it quietly.

### 8.5 Parity, and the probe that found a hole in itself

`tests/rendering/test_tornado_parity_gpu.cpp`: a compute harness comparing density, envelope,
radialT, heightT and all three velocity components against the CPU at t = 0, 6 and 41.7, through
the packed form. **All tests passed (2870 assertions in 5 test cases)**, exit 0 off the binary,
under the lock.

The margin is **1e-4 on the envelope** -- tighter than the vortex's 1e-3, and that is a property of
the architecture rather than of the test: the envelope is trigonometry, smoothsteps and one
exponential with no fBM in it. Only the density relaxed to 1e-3 when Phase 4 put three octaves of
value noise on top. Holding the envelope to the tighter bound is deliberate: it is where a drift in
the structure underneath the noise would show.

ADR-182 in both directions: a 2% shell-width perturbation is rejected at the same margin, and a
one-character break (`exp(-d*d)` to `exp(-d*d*1.03)`) produces **68 failed assertions** with real
`with expansion` blocks.

**The per-field reachability probe reported `cloudWidth moved 0 of 80 GPU samples` on its first
run, and it was not a dead control.** The cloud's interior is a plateau -- `1 - smoothstep(0.55,
1.0, dist/cr)` is exactly 1 inside 55% of the cloud radius -- and the sample spread was built from
the FUNNEL's radius while the cloud is four times wider, so every sample sat on the plateau.
Widening a plateau moves no point already on it. The general form, which is the part worth keeping:

> **A reachability probe built from one feature's extent is blind to every feature that is larger,
> and it reports that blindness as a dead control.**

Fixed by covering the feature, not by relaxing the check. The probe also compares **velocity**, not
only density: seven of the thirty-nine knobs move the velocity alone, and a probe watching the
output it expected to move would have called all seven unreachable and been wrong about every one.

### 8.6 Suction vortices are correct, cheap, and weaker than intended -- for a geometric reason

§27's secondary vortices as a cosine windowed at the radius of maximum wind, not a loop over N
orbiting Gaussians. A cosine's mean over angle is exactly zero, so the term's mean is exactly 1 at
every radius and height whatever the count -- and `density` is a per-metre coefficient calibrated
against this field's mean (the ADR-389 family). The loop would also have cost a loop.

Rendered, they are real and subtle: visible vertical flutes on the column, a scalloped rim on the
wedge. The reason is geometry and not tuning, and it is this record's own argument turned around:

> This is an **angular** modulation. From a side view the ray integrates through the near wall and
> the far wall, where the lobes are half a period apart and partially cancel. An angular feature is
> far more visible from above than from the side.

That is §2's trigonometry -- which argued *for* a tornado over a cyclone -- working against a
horizontal feature on a vertical phenomenon. They are kept: correct, cheap, mean-exactly-1 and
reachable. They are not the dramatic feature §20 might be read as promising, and no amount of
tuning from a ground camera will make them one.

### 8.7 Detail is one multiply whose mean is exactly 1, and §39 passes

Three scales at fixed 3.1x and 9.7x octave ratios -- **one scale control, not three**, because
three independent sliders get set to the same number and give one octave at triple amplitude, which
is the failure §21 exists to prevent. Sampled in the column's **co-moving frame**: the angle
advanced by `rotationAt(h) * t` and the height dropped by `climbRate * t`, so a feature sits still
in a frame that is itself rising and turning. §29's temporal coherence is a **change of coordinates,
not an advection** -- no texture, no history, no state, evaluated rather than integrated, and
therefore reproducible (ADR-360). That is the payoff for having made the flow analytic in §5.

`cloudAmount` at 0 returns **exactly 1.0 from an early-out**, so §38's Mode 1 is byte-identical to
the analytic field. **Noise is provably never load-bearing rather than intended not to be**, which
is the difference between a claim and a property.

§39's four panels, read in order: **A** a thin column with clear helical striations under a smooth
broad cloud, unmistakably a tornado and completely smooth; **B** the cloud breaks into lumpy masses
and the column gains broad variation; **C** finer granular mottling; **D** finest breakup and wispy
edges. **A already reads and D is a refinement of it.** The brief's failure condition -- A/B/C a
meaningless blob and D a tornado -- is not what this does.

**And the first reading of that panel was a fact about the instrument.** Rendered at 640x360 the
conclusion recorded was that B to D are subtle. `QualitySettings::volumeResolutionScale` is 0.5, so
the march runs at **320x180 for a 640x360 output against 960x540 for 1920x1080 -- nine times the
samples**. The detail was generated identically at both sizes and sampled nine times more coarsely
at the small one; at full resolution the progression is clear at every step. It is specifically
**not** the band limit, which is the plausible wrong answer: `filterWidth` is
`volumeMaxDistance / volumeSteps` and is resolution-independent, so the fine octaves fade by exactly
the same amount either way. **The lever is render resolution, not step count**, and the four panels
ship as lab scenes with that written in each file's own description so a reader who renders small
finds it before retuning anything.

### 8.8 §47: seven storms from one field, and a showcase that cannot be rendered

`examples/labs/tornado-showcase.scene.json` authors all seven variants the brief names, differing
only in the values of the same field. There is no per-variant code anywhere, which is the claim §47
is a test of. **Six of the seven read as distinct, recognisable storms.** The wedge is still the
weakest, and the diagnosis from §8.6's phase holds and looks intrinsic: its funnel is as wide as its
cloud, so the shoulder §8.1 identified as the thing that sells the other six is gone by definition.
It ships weak and stays in.

**The camera rule was biased and the bias looked like the effect's fault.** Framing on HEIGHT alone
-- from §2's own measurement -- made the wedge subtend **71.7 degrees of horizontal arc in a 40
degree frame** and the thick cloud 50, so both rendered as a featureless grey wall. Read off the
contact sheet that is two variants failing; read off the geometry it is one rule failing on anything
wider than it is tall, which the WMO's definition makes the *defining* property of a wedge. Framing
on `max(height, width)` is the removal of a bias, not special treatment, and the wedge is still
weakest after it.

**And the combined shot came back COMPLETELY EMPTY -- not one of seven, nothing at all.** With one
medium slot the survivor is whichever tornado comes first in the array; that was the 70 m dust
devil, which is sub-pixel at group distance; and there was no warning anywhere, because
`AtmosphericCounts::dropped` reaches no user interface. **A silent drop chosen by array order
producing a blank deliverable is the owner's "if I turn on fog I can't see the vortex" at its worst
expression**, and that file now reproduces it on demand. It is deliberately not worked around: its
own note says do not judge the system by rendering it today, and names both limits (the slot cap,
and an 8.2 km framing whose ~58 m sample spacing steps past an 80 m funnel).

### 8.9 The panel, and a guard that fired on shipped code

The controls existed; whether an artist could find them was a different question. Sections renamed
to §33's vocabulary -- Shape / Cloud / Ground / Flow / Turbulence / Appearance / Motion -- because
they had been named after this struct's field order. Nine rows had no tooltip; all 57 carry one now.

Two of §33's sections are **deliberately absent** and the file header says so. **Particles**: not
implemented, and a "Particle Density" control against nothing is ADR-421's defect exactly.
**Performance**: ray steps and volume resolution belong to the shared march, and a per-effect copy
would be a second opinion about a number the scene owns -- which is not hypothetical, it is the
divergent hand-written copy the fog agent deleted from `engine.cpp` one layer up.

`drawSchemaRows` emits a section header only on the row that declares one, **after** filtering by
page. So a section declared on one page whose later rows sit on the other leaves those rows under a
different section's header. Found by the guard on its first correct run:

> **The shipped Vortex draws Contrast under "Shape".** `contrast` inherits "Cyclone structure" from
> `innerVoid` in list order; `innerVoid` is `.main()` and `contrast` is Advanced; so no "Cyclone
> structure" separator is ever emitted on that page and a noise transfer-function exponent lands
> three rows under Wall thickness, filed as geometry.

Invisible in the source, where the rows read as one tidy list. Invisible in a screenshot unless you
already know where a row belongs. One `.sec()` fixes it.

**The first version of that guard was wrong and that is the more useful half.** It asserted that the
first row on each page declares a section, and went red on **five of six shipped kinds**. That is
not five defects; it is the family's deliberate convention of a few ungrouped rows before the named
sections. **A guard that fires on working code is not a guard** -- which is the lesson the kind-name
check twenty lines above it in the same file was bought with, and it was reproduced anyway. The
property that is both true and load-bearing is narrower: *a row may have no section, but it may not
have one the panel did not draw.*

Behaviour-neutral, checked rather than asserted: `_tc-2-classic-cone` rendered with the panel
changes stashed and unstashed gives sequence hash `4a4df759403590bb` both times. The first attempt
compared against a stale hash, saw a difference, and it was an unrelated relayout -- which is the
same habit as the light-key check where the hash *not* moving was the proof, used in the other
direction.

## 9. A pattern, named once rather than found six times

Six instances in one night, across four agents, all the same shape:

| # | the scan | what it reported |
|---|---|---|
| 1 | `git grep "kind": "vortex"` for a removal census | eight files authoring a Vortex; six held a `spatial::FieldKind::Vortex` driving particles. Would have deleted particle motion from six scenes. |
| 2 | the enum parser in `test_effect_conformance.cpp` | "a schema claims `Tornado`, which atmospherics.hpp does not declare" -- stopped by a `}` inside a comment naming `core/tornado.{hpp,cpp}` |
| 3 | the parity test's reachability probe | `cloudWidth moved 0 of 80 samples` -- blind to any feature wider than the one it was sized from |
| 4 | a rebase conflict resolver | swallowed `hasTornado`, `Tornado tornado{}` and both counters, because a comment block ran into the next one with no terminator where a line-oriented filter expected one |
| 5 | `"kind": "grid"` as a census of the ADR-032 solver's users | 183 hits, **zero** of them a simulated grid; all procedural point distributions (ADR-581) |
| 6 | `"fields"` in a scene file | `entity::FieldDesc` and `spatial::FieldSpec` share the word. A scene parsed, loaded, logged three fields by name, rendered, and simulated **nothing** |

**A scan that does not understand its own input reports its own blindness as a result.**

The split is the useful part. **Four of the six were caught only because something downstream
disagreed** -- a compiler, a probe's own control, a blown-out render -- and the disagreement
happening to be loud was luck. **Two were caught by a person parsing instead of matching.** Number
6 is the worst of them and is not a scan failure at all but a *schema* one: two subsystems sharing
an English word in the same file, where the symptom was a flat wash that reads as a tuning problem.

## 10. Phases 7 and 8: onto the foundation, into the hero shot, and what it cost

### 10.1 The integration, and three defects it surfaced

ADR-562's medium slots landed while this branch was on its provisional plumbing -- a parallel
uniform block and a per-effect frame slot, described from the start as a move rather than a
rewrite. The rebase took the foundation's side for every file it owned and the integration was done
once, against the final shape: `EffectBucket::Medium`, a `packMedium` beside `fill`, and the
Tornado's arm in `mediumShape`, `mediumEmissionAt` and `mediumInterval`. The ray interval is a
vertical cylinder whose radius is the Bezier **convex hull** of the three artist radii -- a provable
bound rather than an estimate -- and whose caps are the field's own compact support.

Three defects came out of it, all one family:

**`MediumSlot::kind` had zero readers.** Set on the CPU, compared by `frameDiffers`, and the
renderer copied only lanes -- so the tag selecting a density function reached no shader.
Built-but-unreachable *inside the foundation written to fix that family*, one day old. Found
independently on two branches; the central fix (lane 15, written in `buildAtmosphericFrame` after
each kind's `pack` returns, so no kind can forget) is `agent/fog`'s and this branch's own attempt
was reverted in favour of one fix rather than two.

**This branch's packer wrote a colour into lane 15 -- the tag's lane.** Thirteen field lanes plus
three of appearance is sixteen; lane 15 is reserved; so a tornado has sixty floats and wanted
sixty-one. `edgeWidth` was folded into a constant to free the last one. **One artist control
removed deliberately beats a packer that silently overwrites the tag selecting its own density
function** -- and the timing is the frightening part: had the tag landed *before* the packer, this
would have presented as **a tornado intermittently rendering as a comet**, which names a symptom
somebody would otherwise chase for a long time.

**The §68 wind lean was vortex-specific.** `buildAtmosphericFrame` wrote `leaned.vortex.field.center`
unconditionally -- correct for the two kinds that store in `e.vortex`, a silent no-op for a tornado.
`effect_conformance`'s `flow-reaches` named it within a minute of the kind existing, which is that
check paying out on a kind built after it. It is a per-kind hook beside `pack` now, because a disc
leans by **moving** and a tornado's axis is already a curve so it leans by **bending**. Adding the
hook then broke FOG, which had been relying on the unconditional arm; the agent who adds the branch
owns every kind it breaks, so that was fixed in the same commit.

### 10.2 The six renders, and the two moves that would have saved five of them

With the tag not yet on `main`, the march read a tornado's lanes through `vortexShapeAt`: lane 0's
`.w` is a **height** read as a radius, lane 1's `.w` is a **taper** read as extinction per metre. A
1400 m disc at 1.5 per metre is a blown-out white wash.

It was predicted, by name, in advance: *a tornado evaluated as a vortex will look like a tuning
problem and you will have no reason to suspect the dispatch.* **It still cost six renders.** A step
ladder at 32/96/160, an emission ladder, scattering to zero -- and reasoning through two documented
failure modes (ADR-374's in-scattering wash, ADR-461's under-sampling) and tuning against both,
before suspecting the dispatch at all.

**Knowing a hazard by name does not make it recognisable in the moment.** That is the argument for
structural fixes over checklists, and it was reached on two branches the same night from opposite
directions -- here, and in `agent/fog`'s finding that the producer/consumer split is the default
outcome of writing a producer and a consumer in separate commits rather than a lapse of care.

What broke it is cheap and general, and is recorded here because it is worth more than the fix:

> **When a knob does nothing, compare hashes across configurations that should differ wildly.
> Identity across unrelated inputs says the input is not reaching the computation at all.**

`e7ff2f282b5b08fa` came back from `emission 0.22 / scattering 0.15 / density 0.05` and again from
`emission 0.03 / scattering 0.0 / density 0.012`. Two configurations sharing no value cannot
produce one frame. That is a **category** signal rather than a magnitude one, and it is a cheaper
first move than any ladder.

Its companion, which is the same thought one step earlier:

> **The first question about an unresponsive control is not "what value" but "is this code
> running".**

Disabling the effect entirely -- which is what finally settled it -- should have been the first
move, not the seventh.

And the confirmation, in the other direction: once the tag was live, an emission ladder returned
**three different hashes**, which is the same test used to prove the input now *does* reach.

### 10.3 The hero shot

Both halves of the deliverable were replaced together so the shot has no gap. ADR-264 caught the
first attempt: only the scene was edited and the frame did not move, because the project authors
its own effect list.

The census was taken by **parsing, not grepping**, and the count is the record: **two** deliverable
files author an atmospheric Vortex; **six** others carry a `spatial::FieldKind::Vortex` driving
particles and are untouched; **twelve** `_`-prefixed arms belong to other branches' ADR evidence
(ADR-460/461, ADR-560) and are left alone, because deleting them destroys the evidence for three
records. A `git grep` of `"kind": "vortex"` returns eight files and would have deleted particle
motion from six scenes -- §9 row 1, and the reason this paragraph states its method.

The Vortex **kind stays**: `volumetric_fog_effect.cpp` stores in `e.vortex` at 57 sites.

The project's five routes are **retargeted rather than deleted** -- bass to thickness and debris,
mid to wobble, treble to striation contrast, beat to glow. The musical intent survives the change
of kind even though every leaf name does not.

Two tests encoded "the hero has a vortex". `test_vortex_effect` now counts five routes naming the
Tornado and still resolves every one. `test_tree_energy_reach` now finds whatever kind resolves
into `EffectBucket::Medium` and asks the registry for its factory -- it follows the **role** rather
than the kind, so it survives the next replacement as well as this one.

### 10.4 What the hero frame actually looks like, and the seam it exposes

Shape, placement and scale are right. A vertical luminous column on the right third of frame,
clear of the island exactly where §2's geometry put it (2380 m out, 22 degrees off a view axis
whose island occupies 15, 32.8 of a 36 degree frame vertically), with a visible waist, a flared
base and readable striations at `emission` 0.004.

**The values are not right, and this is recorded as unfinished rather than shipped.** It is too
bright and reads as glowing gas rather than smoke; the wall cloud is cut off by the top of frame,
which loses the thin-column-under-a-broad-cloud proportion §8.1 identified as the thing that sells
the silhouette; and the base flare reads as a pool of light rather than a dust skirt.

**That last one is not a brightness problem, and it is the seam §32 warned about.** The debris
skirt is a visual statement that *this column is touching the ground and tearing it up*. The Tree
of Life is an island in open space: **there is no ground**, so the term is asserting a relationship
the scene does not contain, and no amount of dimming or brightening will make it read, because the
problem is not its value.

Three ways out, genuinely different, and this record does not choose between them because it is an
art-direction decision rather than an engineering one:

1. **Drop the skirt in this scene.** A cosmic column with no ground contact is a coherent object --
   nearer a column of matter than a terrestrial tornado -- and §1 permits the effect to remain
   stylised for Tree of Life.
2. **Give it something to touch.** Land the base on the island or on something else. A composition
   change rather than an effect change, and it makes the skirt mean what it is drawn to mean.
3. **Keep it and accept the read** as unearthly rather than wrong.

§32 asks for EmberGen's physical vocabulary inside a cosmic art direction. The skirt is the exact
point where those two pull apart, and it is worth naming as a seam rather than tuning past.

### 10.5 Phase 8: 4.7x cheaper, like for like

The measurement §8.2 deferred, taken in a known state **before** any value moved -- because tuning
and measuring together makes neither attributable. Same project, camera, 32 steps, 1920x1080 with
the half-res volume target, **same tree**. Interleaved, `tools/gpu-lock.sh`, minima over repeats
(ADR-170):

| arm | `volume.march` |
|---|---|
| the shipped Cosmic Vortex, recovered from `fd71b736` | **6.16 ms** (6.16, 6.49, 6.16) |
| the Cosmic Tornado that replaced it | **1.31 ms** (1.31, 1.44, 1.57, 1.97) |
| no medium at all | **no `volume.march` pass exists** |

**4.7x.** §5 predicted 1.5 to 3 ms from ADR-374's coverage model and §8.2 measured 1.25 ms in the
lab while refusing to quote a ratio from it. The real ratio is **smaller than the lab's 6.5x would
have implied**, which is exactly what refusing to quote it protected against: the lab's column
covered about 15% of frame against a funnel filling two thirds, and a ratio between quantities
measured under different distributions is the ADR-389 family's mistake in a new place.

**The ray interval is separated rather than folded in.** ADR-460 measured this same vortex in this
same shot at **8.13 ms** before the per-slot interval existed; on this tree it is 6.16, which is
**-24%** and is consistent with the interval doing what it was built for. Other work landed between
those trees, so that is an **association and not an attribution**. What is clean is the 4.7x: both
arms on one tree, with the interval under both.

Frame medians: vortex 14.61-15.60, tornado 10.75-12.06, no medium 10.94-11.99. **The tornado is
roughly frame-neutral against having no placed medium at all.** ADR-374 called the thing it
replaces "+5.5 ms of a 13.5 ms frame -- the most expensive term in the scene"; it is now the
cheapest thing in it.

No combined shot was measured. `agent/fog` has since found that per-slot cost is **not additive** --
dense media in one frame subsidise each other through the transmittance early-out while thin
separated ones do not -- so nothing above rests on an additive assumption, and nothing is claimed
about combinations.

## Do NOT "fix" this later

**Vorticity confinement is absent from the analytic tier on purpose, and the missing slider is not
an oversight.** Written here, at the end, because this is where somebody looking for a shortcut
will read. Fedkiw/Stam/Jensen's `f = epsilon * h * (N x omega)` exists to put back energy that
**numerical diffusion** removed from a grid. An analytic field is not integrated and has no
numerical diffusion, so there is no energy missing and nothing to confine: wiring the term up over
`tornado.wgsl` would compute a force that is then added to a velocity field nothing integrates.
That is a control that does nothing, which ADR-421 records as worse than a control that is absent.

`Vorticity`, `Vorticity Scale` and `Vorticity Falloff` exist and do something -- they drive the
curl-noise amplitude masked by the analytic `|omega_z|`, and the suction vortices. If the grid tier
lands, the same three additionally drive a real confinement kernel over the grid, where the premise
holds. **The trigger for implementing confinement is the grid tier existing, not a reviewer noticing
the word is missing from a shader.**

## Revisit when

- ~~§38 Mode 1 is rendered.~~ **Done (§8.7): it reads, and `cloudAmount` 0 is byte-identical to the
  analytic field, so noise is provably not load-bearing.**
- ~~The coverage cost prediction is measured.~~ **Done (§8.2) in the lab and NOT like for like. The
  hero-shot measurement is Phase 7 and is the one that settles it.**
- ~~Mode 1 must pass a value-separation test, not only a silhouette test.~~ **Done, in both
  directions**: dark by extinction against a bright sky, and bright by emission against a dark one.
  Two coefficients, two directions, and it was made a Phase 2 gate rather than left to be
  discovered in Phase 5 lighting. The reasoning stands for anyone re-tuning: a dark smoke column
  against a dark wash has no silhouette however tall it is.
- The grid tier is attempted. **ADR-581 measured what §4 read off the code**: at `kCatchUpSteps =
  240`, `--range 6:6` and `--range 30:30` render byte-identically because both skip to a state that
  was never simulated. It does not violate ADR-360 and it does break `--range t:t`. If the tier is
  built, one of ADR-581 §4's four options has to be chosen first.

### What is still open at the end of Phase 8

- **The hero's art direction**, §10.4's list: too bright, the wall cloud cut off, and the debris
  skirt asserting a ground contact the scene does not have. The third is a decision between three
  named options rather than a tuning pass, and it belongs to the owner.
- **Phase 5, cinematic rendering.** Blocked on the shared self-shadow light march, which does not
  exist. Nothing is budgeted for it inside this slot.
- **The wedge** is the weakest of §47's seven and the diagnosis looks intrinsic: its funnel is as
  wide as its cloud, so the shoulder §8.1 names is gone by definition. It ships weak and stays in
  the showcase, because an honest weak case beats one tuned to a camera chosen for it.
- **Step redistribution.** The per-slot interval skips samples outside a medium but does not place
  them inside it, so a thin column is still limited by the global step count. The rope table in
  §8.3 and `agent/fog`'s 7% are the two arguments for doing it; it is a separate change.
- **Four medium slots, not eight.** §47's showcase authors seven and draws four. Raising the cap is
  a constant plus a measurement, and the per-slot cost curve is `agent/fog`'s to finish.

### What was still open at the end of Phase 6

- **Phase 5, cinematic rendering.** Blocked on the shared volumetric foundation's light march
  (§7 item 7), which the fog agent took. Nothing is budgeted for self-shadowing inside this slot.
- **Phase 7, Tree of Life integration and the Vortex removal.** Two files, both halves of
  `tree-of-life-floating-island`, censused by **parsing and not grepping** (§9 row 1 is what that
  costs). They land together with the Tornado in the hero shot so the shot has no gap. ADR-441
  waives the compatibility alias.
- **Phase 8, performance.** The like-for-like cost measurement in the hero shot, which is the one
  §8.2 explicitly does not claim.
- **The wedge** is the weakest of the seven and the diagnosis looks intrinsic. Recorded rather than
  dropped from the showcase, and not to be rescued by a camera chosen for it.
- **`AtmosphericCounts::dropped` still reaches no user interface**, which §8.8 turned from a code
  census into a blank deliverable.
