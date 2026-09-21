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

- §38 Mode 1 is rendered. If the analytic structure alone does not read as a crude but unmistakable
  tornado, §5's density model is wrong and no later phase can repair it.
- **Mode 1 must pass a value-separation test, not only a silhouette test.** The shipped backdrop is
  a flat teal-to-green wash filling the lower two thirds of frame (§1). A dark smoke column against
  a dark wash has no silhouette however tall it is, and no amount of vertical extent repairs it.
  So Mode 1's pass condition is two-part: the shape reads as a tornado, **and** it separates in
  value from the backdrop behind it. This is a Phase 2 gate, deliberately not a Phase 5 lighting
  discovery.
- The coverage cost prediction in §5 is measured. If a tornado is not cheaper than the funnel, the
  ADR-374 coverage model needs re-deriving, not the tornado.
- The grid tier is attempted. If `kCatchUpSteps` makes a still frame unreproducible in practice as
  well as in principle, the grid is Cinematic-only or it is nothing.
