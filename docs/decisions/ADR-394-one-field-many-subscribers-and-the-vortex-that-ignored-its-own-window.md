# ADR-394: One field, many subscribers — and the vortex that ignored its own window

- Status: Accepted (2026-09-20)
- Extends ADR-055 (the wind field), ADR-388 (the vortex field), ADR-230/387/392 (the atmospheric
  family as the environmental simulation layer), ADR-091 (two-tier determinism), ADR-350 (a setting
  the application does not keep is not a setting), ADR-375 (reachable is not findable), ADR-382 (a
  path a panel computes needs a test that computes it the same way), ADR-385 (a stated reason is not
  evidence), ADR-389 (a coefficient is invalidated by a change to its quantity's distribution).

## The measurement that set the scope

The brief's §68 asks for one field with many subscribers, and states that each effect having its own
isolated wind handling is what should stop. Before building anything, that claim was checked against
the tree, because ADR-385's rule cuts both ways and a brief is also a stated reason.

It is true, and it is worse than "isolated". `wind::sampleWind` has **four** call sites in the whole
of `src/`: three in `spatial/vegetation_sim.cpp` and one in `rendering/debug_visualizer.cpp`. On the
GPU, `windSampleAt` is read only by `common.wgsl`'s mesh deformation and `procedural.wgsl`'s
instanced vegetation. Particles reach the same field by a different route — ADR-370 copies the
packed uniforms into `ParticleUniforms` and `shaders/particles.wgsl::particleWindAt` re-implements
the arithmetic — and `ParticleSystem::windInfluence` defaults to 0 and is set in **three** files in
the entire `examples/` tree.

Everything else has its own:

| system | samples the shared field? | its private motion numbers |
|---|---|---|
| vegetation, meshes | **yes** — the only genuine consumer | — |
| particles | opt-in copy, default off, 3 scenes | `turbulence`, `turbulenceScale`, `tumbleRate`, `turbCurl` |
| volumetrics / vortex | no | `swirl`, `rotationSpeed`, `turbulence`, `breathAmount`, `breathSpeed` |
| comet | no | `flowSpeed` |
| aurora | no | `flowSpeed`, `driftSpeed`, `verticalSpeed`, `turbulence` |
| water | no — **and it has a second "wind" of its own** | `WaterFlowSettings::windDirection`, a bare `vec2` unrelated to `wind::WindParams` |
| clouds | there is no cloud system | — |

So the sky, the water and the funnel each carry a private clock, and the only way to make two of
them agree is for an artist to type one number into several boxes. The thing that is missing is not
a field — there are two excellent ones — it is **any way for a third party to ask a question of a
field it does not own.**

## Decision

`src/world/world_effects/field_bus.{hpp,cpp}`: a publish/subscribe layer over the fields that
already exist, and deliberately nothing more.

A **publisher** hands the bus a name and the *packed uniforms* of a field it owns. Packed, on
ADR-055's precedent, so any future CPU/GPU comparison starts from bytes identical by construction.
A **subscriber** names a field in its own serialised data and gets a `FlowSample` at its own
position and time.

**The bus re-implements nothing.** `sample` dispatches to `wind::sampleWind` or
`vortex::sampleVortex`. This is the property the whole design rests on: ADR-055 and ADR-388 each pay
for a CPU/GPU parity test, and the defects agent is currently holding a third transliteration —
`particles.wgsl::particleWindAt`, which drops the `phase` field with no test tying it to the others.
A bus that transliterated the wind a fourth time would need a fourth parity test and would drift on
the day nobody wrote it. `test_field_bus.cpp` asserts the bus's answer is the same call's answer,
component by component, exactly.

### `FlowUnits`, because the two publishers genuinely differ

`wind::WindParams::speed` is documented as "dimensionless strength of the steady flow (0 calm, 1 a
fresh breeze)". `vortex::VortexSample::velocity` is metres per second of actual medium. A common
vocabulary that quietly called both of them "velocity" would be the fifth instance of this
repository's recurring unit bug (ADR-374's density, ADR-379's spill, ADR-381's comet-on-fog,
ADR-389's coefficient), and it would be introduced by the file whose purpose is to let strangers ask
each other questions.

So `FlowSample` carries `units`. A subscriber that integrates a displacement must check it; one that
only scales its own motion by a relative magnitude need not, and says so where it reads the sample.
Every subscriber the atmospheric family has is the second sort, which is exactly why one effect can
follow either a wind or a funnel without knowing which it got.

### The subscription is one shared table row, and that is the point

`AtmosphericEffect` gains `fields::Subscription flow` — a structural `field` name and a modulatable
`influence`. `influence` is **one row in `kSharedFloats`**, which is ADR-387's architecture used at
its strongest: one line gives all three kinds registration, apply, capture, a modulation target, a
timeline key, a preset member, a save entry and a panel row, and gives a fourth kind all of them on
the day it is added without anybody remembering to.

`influence` defaults to 0 and 0 is off, so every scene and project in the repository loads and
renders exactly as before. Both ends are proved rather than asserted — see the conformance check
below.

### What each kind does with it

Shared, in `flowAmplitude` and `flowOffset`, because three private derivations of one field would be
§68's own complaint one layer down. `flowAmplitude` is `1 + influence * (strength + gust)`, a
unitless multiplier on whatever lateral motion the kind already has. `flowOffset` is a **spatial**
phase in radians — which is the property that makes this a field rather than a shared clock: two
effects in different parts of the sky get different offsets, and a shared clock would move them in
lockstep, which is the tell ADR-055 was written to remove from the meadow.

- **Comet**: the trail's wisp displacement and its flow phase. Deliberately *not* the trajectory.
  ADR-230 made the arc's endpoints exactly what an artist typed, because a control that is a
  suggestion is not a control; air that bent a comet's path four kilometres up would be a comet
  nobody recognises. What the medium moves is the tail, which is what a comet's tail is made of.
- **Aurora**: the curtain's undulation and its fold phase. Not its height or its colours — those
  answer to the music (ADR-230 §4.2), and a curtain that dimmed in a gust would be answering two
  masters with one number.
- **Vortex**: the funnel leans downwind, up to a fifth of its radius at the soft maximum. A
  **translation**, chosen over raising `turbulence` or `breathAmount` because ADR-389 is explicit
  that a coefficient tuned against a quantity is invalidated by any change to that quantity's
  distribution — and those two are inputs to the filament noise the march's 4.78 ms was tuned
  against. Moving where a shape is costs the march nothing; changing what it is costs it
  everything. It is bounded because ADR-387 warns that a preset which moves a `center` silently
  unanchors the funnel from the island it was placed on.

**No new GPU work.** The subscription is resolved on the CPU once per effect per frame and folded
into lanes the packed structs already carry (`CometGpu::shape.y/.w`, `AuroraGpu::shape.x` and
`flow.y`, `Vortex::center`). That is ADR-055's own rule — a transfer function is evaluated once per
draw so a shader is arithmetic and nothing else — applied to a field instead of to a plant. With
2.4 ms of GPU headroom and the CPU as the binding constraint at 18.42 ms against 14.68, a handful of
`sampleWind` calls per frame is the only affordable shape this could have taken.

### A dead name is loud, and that is half the design

This repository's signature defect is a name that resolves to nothing and is thereafter
indistinguishable from a setting nobody used: ADR-392's route aimed at three paths a vortex does not
register, ADR-375's sixteen parameters no panel named, ADR-385's function with no caller whose
comment said otherwise. A field bus is an open invitation to add a fourth.

So `resolve` returns `kNoField`, which is **-1 and not 0**, so a caller who forgot to check cannot
silently acquire the first published field. `FieldBus::unresolved` reports every subscription that
named a field the scene does not publish, by subscriber *and* by field, because "some effect wants a
field called `gale`" is a puzzle and "the comet `Opening Streak` wants a field called `gale`" is an
instruction. `Engine::publishFields` logs that list once per name — a message repeated at frame rate
is a message nobody reads, which is the same failure from the other end. And the panel shows the
dead name as its own marked entry rather than showing "nothing", because "nothing" would be a lie an
artist cannot act on.

The rendered consequence of a dead name is **still air, not absence**: the effect renders exactly as
an unsubscribed one. A scene must not fail to load because a vortex it names was switched off.

### Conformance check 6

A shared table row buys registration, apply, capture, a panel row and a save entry for every kind at
once — and buys **nothing** about whether a given kind's resolver reads the number. `flowInfluence`
could be registered, modulated, keyed, saved and reloaded for an aurora while `packAurora` never
touched it, and all five of ADR-392's checks would pass. That is ADR-387's "a correct value is not a
reached value" in the one shape the rest of that file cannot see.

So the check is a **difference, not an inspection**: build the frame the kind produces with no
subscription, build it again subscribed to a field that is genuinely blowing, and require the two to
differ; then build it a third time with the name present and the influence at 0, and require that
one to be identical. Both halves are needed. The first alone would pass a kind that responded to the
mere presence of a name, which would make every scene that records an unused subscription change the
moment it loaded.

It is kind-agnostic by construction — it compares whole frames — so a fourth kind is covered on the
day it is added, and covered correctly, which a per-kind lane comparison would not be.

ADR-182: it was made to fail before it was trusted. With `packAurora`'s two uses of the flow removed
it names the aurora and nothing else; with `resolveEffectFlow` stubbed it names all three.

## The two things found while wiring it

### `buildAtmosphericFrame` ignored the vortex's own activation, under a comment saying it did not

The loop that finds the live vortex walked `effects` a second time and tested only
`e.enabled && e.vortex.active()`, beneath this comment:

> ADR-387: the first live vortex, copied through. Its `enabled`, activation and lifetime envelope
> were already applied by the resolve above, so a vortex inside a closed window arrives here
> switched off exactly as a comet does.

It had not, and could not: `resolveAtmosphericEffects` takes its effects by `span<const>` and cannot
write anything back, and the second walk consulted the resolve's counts not at all. A vortex with
`activation: window` ignored its window; one with a `fadeIn` did not fade in; one whose lifetime had
expired kept marching. This is ADR-385's stated reason that is not evidence, in the same family that
ADR was written about, in a comment rather than in code — the second time in two ADRs.

**No shipped scene exhibits it.** The one authored vortex in the repository — `Cosmic Vortex` in
`examples/treeisland/tree-of-life-floating-island.scene.json` — is `activation: always` with no
`timing` block, so its envelope is exactly 1 and this change leaves its frame arithmetically
untouched. That is *why* it survived: the feature nobody used was the only one that was broken, and
nothing in a suite of 2500 cases asked the question.

The fix is the one §68 needed anyway. `resolveAtmosphericEffects` gains a defaulted `vortices` span,
so a vortex is a first-class resolved record like the other two kinds, and the second walk that
re-derived what the first walk already knew is gone. The lifetime envelope now scales the two
per-metre coefficients (ADR-374) and nothing else: fading the colours would leave a full-strength
grey funnel and fading the radius would shrink it rather than dim it.

### `memcmp` over an aggregate compared padding, and reported a difference that was not one

The first version of check 6 compared two `AtmosphericFrame`s with `std::memcmp` over
`sizeof(AtmosphericFrame)`. It reported all three kinds as failing the "influence 0 is exactly off"
half. The difference was **two bytes, at offsets 10 and 11** — the padding between
`AtmosphericFrame::hasVortex` and `AtmosphericFrame::vortex`. Value-initialising the frame zeroes
its padding; the member-wise assignments afterwards leave it indeterminate, and the optimiser is
entitled to write through it with a wider store. It had, with the low half of a float.

That is the same class of wrong answer as a probe that cannot fail, reached from the opposite
direction: a probe that cannot **pass**. It was found by disbelieving a failure, which is the
discipline ADR-382 and the 0.000-frame-timeline finding both record, and it is worth writing down
because `memcmp` over a struct is the obvious way to compare two frames and it is wrong.

`conformance::frameDiffers` compares the payload blocks — each an array of `vec4`s or of `float`s
with no interior padding, pinned by the `static_assert`s beside their declarations — and the scalars
one at a time, with a `sizeof` assertion so the frame cannot grow silently past what it reads.

## Consequences

**The bus is reached from the shipping path, and that was the first thing built.**
`Engine::publishFields` is called from `updateAtmosphericEffects`, before the resolve that reads it.
A bus nobody published into would have been `defaultAtmosphericRoutes` wearing a newer word — correct
for a year with no caller — and the test that proves a subscription reaches a picture goes through
`buildAtmosphericFrame` rather than around it.

**The publish is of *authored* fields, before the resolve, and that ordering is a decision.** A
funnel's drawn centre may be leaned by its own subscription, which is not known until the resolve has
run; publishing the drawn funnel would need two passes, and a vortex subscribing to itself would need
a third. Publishing the authored field makes the bus a function of the scene rather than of the
frame's own output, which is the only version of this with no fixed point in it. The difference a
subscriber sees is exactly the lean — zero unless that funnel is itself subscribed, and bounded at a
fifth of its radius when it is.

**It is findable, not merely reachable (ADR-375, §77).** A *Field* section at the top of every
effect's Advanced panel: a combo of the names this scene actually publishes — never a text box,
because a typo and a field nobody has made yet look identical in one — and the influence slider,
drawn from a row table so `conformance::checkLeavesExist` computes the path the way the panel does
(ADR-382).

**The row table is in `ui/world_effects_panel.hpp` rather than beside the kind tables in
`ui/ui_logic.hpp`, and that is temporary.** `ui_logic.hpp` is being edited on `agent/eyecam`. The
table belongs with the others and should move when that branch lands; it is `inline` in a header for
the same reason they are, because the CPU test binary does not link the ImGui side of the editor and
a row list in a `.cpp` could not be held to the leaf check.

**What this does not do.** It does not touch `core/wind.cpp`, `shaders/wind.wgsl`,
`shaders/particles.wgsl` or `core/time.cpp` — all held by the defects agent. It adds no GPU work, no
shader, and no new pixel to any existing scene: `influence` is 0 everywhere in `examples/`. It does
not make particles, water or volumetrics subscribers; each is a separate wiring, and water in
particular has a second private `windDirection` that should become a subscription rather than a
field of its own. And it does not address the three transliterations of the wind arithmetic, which
is the defects agent's work and which the bus makes easier rather than obsolete — the bus dispatches
to the CPU one, so a corrected `particleWindAt` has one more thing agreeing with it rather than one
more thing to correct.
