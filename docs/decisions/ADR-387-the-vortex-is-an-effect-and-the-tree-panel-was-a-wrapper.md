# ADR-387: The vortex is an effect, and the Tree panel was a wrapper around one scene

- Status: Accepted (2026-09-19)
- Supersedes the Tree panel half of ADR-375. Extends ADR-230 (atmospheric effects), ADR-207 (world
  effects), ADR-371/374/379/381 (the vortex), ADR-011 (parameters), ADR-350 (reachability),
  ADR-382 (a path a panel computes needs a test that computes it the same way).

## Problem

ADR-375 shipped two panels: **Environment** and **Tree**. The owner's correction is an
architectural rule, and it is right:

> AV Gen's first-class UI represents reusable engine concepts. Scene-specific artistic compositions
> are data/configuration inside those systems, not new first-class panels.

Held against that rule, the Tree panel is a scene-specific wrapper, and the audit is worth stating
precisely because the answer was "a mixture" rather than "yes":

- **Wind.** The global field it showed is `scene/wind/*` — genuinely generic, ADR-055's weather,
  every scene has it. The *per body* half is also generic. But the panel was a **second place** the
  global wind could be set, beside the Parameters panel, and §18 asks for one authoritative source.
- **Falling leaves** and **Tree particles.** Two sections that found their subject by sniffing
  parameter paths for the substrings `leaf`/`leaves` and `mote`. A control whose existence depends
  on what somebody named a node is not a feature of the engine.
- **Tree energy** and **Canopy shimmer.** Node properties (`nodes/<n>/energy/*`), reached by
  arithmetic on a wind-body path — the arithmetic ADR-382 records getting five characters wrong and
  drawing nothing.
- **The vortex**, in the Environment panel, was the clearest case: an authored cosmic phenomenon
  with twenty-three controls, sitting on `scene::Environment` as a **singleton**, with no user-facing
  control at all until that panel existed.

So: one genuinely generic subsystem (the wind field), one genuinely authored phenomenon that was
modelled as a field on the world (the vortex), and three sections that were the Inspector's job
done by a panel that knew the scene's node names.

## Decision

### The vortex becomes an `AtmosphericEffect`, not a new system

`AtmosphereKind` gains `Vortex` beside `Comet` and `Aurora`. Every field of `Environment::Vortex`
moves to `world::Vortex` unchanged, and `Environment::Vortex` is deleted.

This is the whole of the architectural move, and it was chosen over inventing a parallel one
because **ADR-230's atmospherics family already is a table-driven universal modulation interface**.
Adding two `constexpr` field tables (`kVortexFloats`, `kVortexColors`) and a `floatFields(kind)`
case gives the vortex, with no bespoke code:

- a registered, modulatable, serialized parameter per field at `atmos/<name>/<leaf>`;
- a Parameters-panel row, a modulation target, a timeline key, a preset member, a save entry;
- apply and capture in both directions;
- default audio routes;
- a place in the World Effects panel;
- serialization as a scene-authored effect instance — §11's "no parallel serialization system",
  satisfied by not writing one.

It also answers what the vortex never had: it is now **multiple-instance-capable by construction**,
placed in the world rather than on the sky dome, and nameable by a particle system (ADR-380's
attractor binding now searches the effect list for the first live vortex).

The one deliberate asymmetry: `AtmosphericFrame` carries **one** vortex, not an array. ADR-374
measured the cost as pixel coverage of non-zero density, +5.5 ms of a 13.5 ms frame for one funnel;
a second would be a second full-screen march term. The resolve pass counts extra vortices as
`dropped`, the same way it reports any other effect it refused. The data model does not prevent a
second one; the renderer declines to draw it, and says so.

### Legacy data is migrated in three places, not one (§19)

A scene file carrying `environment.vortex` is read into a `world::Vortex` and converted into an
enabled `Activation::Always` effect named `vortex`, after the `atmosphericEffects` array is read so
the conversion knows which names are taken. The next save writes it in the new place, and
`environment.vortex` is gone from the document.

A legacy vortex with `radius == 0` is **not** migrated. Zero was ADR-371's "off" and is what every
scene in the repository but one has; turning those into disabled effect instances would put a row
in the World Effects panel for something nobody authored.

A project carries its own copy of `atmosphericEffects` and that copy **replaces** the scene's
(ADR-264), so the migration has to happen there too: a project saved before the vortex was an effect
carries a list that could not contain one, and it would throw away the vortex the scene's own
migration had just produced. Carried over by **kind**, because what is being migrated is a format
that predates the kind; a project that authors its own vortex is not legacy and is left alone.

**And the paths, which is the one that nearly shipped broken.** Renaming `scene/vortex/*` to
`atmos/<name>/*` orphans every route, timeline key, preset member, cue and macro that named the old
path. The shipped project has five such routes — bass → density and breath, mid → turbulence,
treble → filaments, progress → emission — and losing them does not fail, does not throw, and warns
only in a line nobody reads. The document is rewritten over its **whole tree**, string values and
object keys alike, because a parameter path appears in six places and a migration that knows about
one of them fails quietly in the other five.

#### A parameter path is three things at once, and a correct value is not a reached value

This is the general fact, and it is worth stating on its own because the next instance of it will
not look like a vortex. **A parameter path is three things at once: where a value is stored, what a
project's `parameters` block names, and what a modulation route targets.** Renaming one moves the
first. Nothing in the build, the loader or the renderer notices that the other two were left behind.

It was proved here the expensive way. A one-shot probe on the uniform upload reported every field
of the vortex arriving at the GPU correct to the last decimal — radius 200, density 0.0013, emission
0.04, every colour — while 95.35% of the frame's pixels were wrong. **A correct value is not a
reached value.** What had been renamed was not the number but the *permission to change it*.

The same family as ADR-385's stated reason that was not evidence, and as the reader with no writer:
a thing that is present, correct, and reaching nothing. What makes this one worth its own paragraph
is the shape of the alibi — the measurement that says "the value is right" is exactly the
measurement that cannot see the failure, and it is the first one anybody reaches for.

Two measurements are worth keeping, because they are what each half cost:

- scene migrated, project's effect list not: **97.96% of pixels different** — the funnel gone.
- both migrated, routes not: **95.35% of pixels different** — the funnel there and unmodulated,
  dimmest exactly where it is brightest. Three renders and a one-shot uniform probe confirmed every
  value reaching the GPU was correct to the last decimal before the routes turned out to be what
  had moved. **A correct value is not a reached value**, and the thing that had been renamed was not
  the number but the permission to change it.

### The Tree panel is deleted, and its sections go where the taxonomy puts them

| was | is |
| --- | --- |
| Environment ▸ Cosmic vortex | World Effects ▸ Atmospheric ▸ a vortex instance |
| Tree ▸ Wind (the field) | Environment ▸ Wind — the one authoritative global |
| Tree ▸ Wind (per body) | World ▸ Inspector, on the selected object |
| Tree ▸ Falling leaves | World ▸ Inspector, on the selected particle system |
| Tree ▸ Tree particles | ditto |
| Tree ▸ Tree energy, Canopy shimmer | World ▸ Inspector, on the selected node |

The Environment panel keeps only what every scene has. Its two `shader/glowmere-cosmos/*` rows went
with the Tree panel for the same reason the leaf-sniffing did.

### What made that possible: the Inspector became an editor

The World panel's Inspector could say *why* an object was moving and could not let anybody change
anything. That is half of why the Tree panel was written at all. It now lists the selected object's
exposed parameters, **grouped by the first path segment after its prefix** — so
`nodes/tree-of-life/wind/strength` appears under a "wind" group on that node, and
`particles/falling-leaves/spawnRate` appears on that system. Nothing in it knows the name of any
scene, node or effect; the grouping is read off paths that registration already produced.

The World overview gained an **Objects** list of the composition's nodes. Its absence was the other
half: every other kind of object in the scene could be selected there, and an authored node could
only be reached by clicking it in the viewport — which does not work for a group whose children are
what the click lands on.

One consequence is that the parameter widget — the kind switch, the component fan-out, the
base-not-final write-back — is extracted to `ui::drawParameterValue` and the Parameters panel calls
it. There was one implementation and one panel using it; now there is one implementation and two.

### §18: one global wind

`scene/windSpeed` and `scene/windDirection` are it. A per-body **response multiplier** is legitimate
and stays — it is a property of the object, edited where the object is. A second global is not, and
the test asserts that nothing else in the parameter set ends in `windSpeed`.

### §12: no `if scene == TreeOfLife`

There is none, and the way that is kept is that nothing added here can express one. The vortex is
found by kind, the wind bodies by declared `windAuthored`, the particle systems by being particle
systems, and the Inspector's groups by splitting a path.

## Consequences

**The Tree of Life is byte-identical.** Two frames at 960×540, t = 6.00 and 6.05, rendered by a
build of the merge base against a build of this branch: `0 of 518400 pixels differ, max channel
delta 0` on both frames, sequence hash `89be5e9d72ed8d49` in both arms. The same hash comes back
three ways — old code on old files, new code on old files (all three migrations firing), and new
code on the migrated files — which is what makes it a statement about the migration rather than
about one pair of runs. §10 is satisfied by the values being the same values, not by re-tuning to
match. The control that the comparison can fail: switching the vortex off in both arms gives a
different, also-identical hash, and each of the three migration halves, omitted, moved 95–98% of
the frame.

**Glowmere is byte-identical.** `glowmere-atmospherics` and `glowmere-valley-2-multicam`, three
frames each, same hashes before and after. §13's precedent — Aurora, Comets, Hero FX, Beam FX — is
untouched because nothing about them changed: the vortex was added beside them through the
mechanism they already use.

**The panel's rows are data.** `ui::vortexRows()` and `ui::vortexAdvancedRows()` are plain tables in
`ui_logic.hpp`; the panel walks them and so does the test. That is ADR-382's lesson applied rather
than agreed with: the defect it records could not be caught because the arithmetic lived inside an
ImGui call nothing could invoke. Renaming one leaf `filaments` → `filament` fails the test with the
path printed. The comet's and the aurora's rows are still written out inline; converting them is
mechanical and would bury what this is for.

**A vortex has no ground pool and the panel does not offer one.** ADR-230's ground glow lands on
terrain; this one hangs under a floating island with no terrain beneath it. Its light on the world
is ADR-379's `spill`, which is a field on the vortex. A combo that changed nothing would be worse
than no combo — and the same reasoning removes the rainbow rows, which a vortex registers none of.

**A second caller read the gate the fog-only way.** `VolumeRenderer::enabled(const Environment&)`
could no longer see the vortex, and the particle fog coupling (ADR-040) still called that overload —
so in the one scene whose `volumeDensity` is zero and whose volume pass exists only because of the
vortex, the coupling silently stopped being filled. It happens not to move this picture, because the
fog density it would carry is zero; it is fixed anyway, and recorded, because the next scene to put
a vortex in real fog would have found it as an image bug with no obvious cause.

**What this does not do.** §16's Canvas integration is not touched: another agent owns viewport
drawing, selection and picking, and the Objects list added here is a panel list, not a picker.
