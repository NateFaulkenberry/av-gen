# ADR-421: The stack eight scenes author and no panel could touch

- Status: Accepted (2026-09-20)
- Extends ADR-023 (procedural geometry), ADR-025 (fields), ADR-026 (splines), ADR-375 (reachable is
  not findable), ADR-382 (a path a panel computes needs a test that computes it the same way),
  ADR-225 (a setting the application does not keep is not a setting), ADR-182 (a probe that cannot
  fail proves nothing), ADR-392 (the enum closed by reading the header), ADR-420 (the field bus).
- Precedes the "Reality, Temporal & Digital Deformation" brief's §2, §3 and §55, and is the finding
  that re-scoped them.

## The finding that set this ADR's subject

The deformation brief asks for a **Deformation Stack**: objects passing through an ordered,
reorderable, individually-modulatable, deterministic, serialisable list of operators. Before
building one, the tree was checked, because a brief is also a stated reason (ADR-385).

It exists. `scene::Deformer` is an ordered stack of up to `kMaxDeformers = 8` slots, with seven
kinds (Bend, Twist, Sine, Noise, Displacement, Field, Path), each acting in local or world space,
evaluated per vertex on the GPU in `procedural.wgsl::deformChain` with a CPU reference
`scene::deformPoint` that is identical in meaning, serialised by `procedural.cpp:2737`/`:2963`, and
registered per slot as `procedural/<name>/deform/<n>/*` — so every number in it is already keyable,
modulatable and preset-able.

It is **authored in eight or more shipped scenes**: `benchmark`, `cathedral`, `lab`, `worlds`,
`chamber`, `wall`, `glowmere-valley-2-song`, `glowmere-stylized`.

And the entire user interface for it was this line, in the Inspector:

```cpp
ImGui::Text("%zu instances, %d deformer(s), %zu op(s), %zu effector(s)", ...);
```

So the brief's §3 is not missing. What is missing is the panel, and §6's Twist, Bend and Wave are
three of the seven kinds that already exist. That reframes the deformation work from construction to
exposure, and the same is true of §2: `spatial::FieldSpec` has 27 kinds, 10 falloffs, a transform, a
space and compound combining, with CPU↔GPU parity tests, and of §2's eleven shapes it is missing
six — which are table rows, not a system.

## The two halves of what was wrong, and they are different

**The numbers were reachable and unlabelled.** The Inspector's generic grouping splits a parameter
path at its first slash, so every `deform/<n>/<leaf>` landed under one tree node called `deform`,
flattened to `1/amount`, `1/axis`, `2/amount`. Nothing anywhere said that slot 1 is a Twist and slot
2 is a Bend. That is the owner's distinction exactly — *findable is not the same as
reachable-in-principle* — and it is ADR-375's travelling band of light with sixteen registered
parameters, on a feature that had already shipped in eight scenes.

**The structure was not reachable at all.** Which kind a slot is, which space it acts in, its order,
whether it exists: all four are read from the file by `registerProceduralParameters` and none had a
writer outside the scene parser. Nor did the per-kind fields registration does not cover at all — a
Sine's `displacementAxis`, which is *the direction it actually pushes vertices*; a Noise's `axisMask`
and `seed`; a Field deformer's field name; a Path deformer's spline. Those are visible in the picture
and were unreachable in the application.

## Decision

### `Composition::setNodeDeformers`, on `setNodeWindBody`'s precedent

Unregister, mutate, re-register. The parameters **are** the stack's interface — registration labels
every leaf by the kind of the slot it belongs to, and `applyProceduralParameters` reads `kind` and
`space` off the rest copy on every frame — so a kind change that did not re-register would leave a
Bend's arithmetic reading a parameter labelled `twist/amount`: correct in value and wrong in every
name an artist reads.

Both `node.procedural` (what `toJson` saves) and `node.proceduralRest` (what registration and the
per-frame apply read) are written. Writing one and not the other is the reader-without-a-writer this
repository keeps finding.

**It does not rebuild geometry, and that is what makes an editor affordable.** The deformers are
deliberately absent from `ProceduralGeometry::structuralHash` — a deformer is a vertex-stage
transform of geometry that has already been generated — so adding a Twist to one of Glowmere Valley
2's forty-two procedurals costs a re-registration and a flatten, not a regeneration. That was checked
before the panel was written, because the answer decided whether it could exist.

The stack is validated through `ProceduralGeometry::validate()` rather than by re-stating its rules
at the call site, which is the second list that goes stale. A stack the file format would refuse must
not be reachable through a panel either.

### The rows are per-kind, and that is the second rule

`registerProceduralParameters` writes the **same nine leaves for every slot whatever its kind**, plus
three more for a Path. So a Bend has a registered `speed` that its arithmetic never reads, and a
Noise has a registered `frequency` that does nothing.

Drawing all nine for every kind would put four controls in front of an artist that move nothing. That
is **worse than a missing control**, because a control that does nothing teaches somebody that the
system is broken, and they stop trusting the ones that work. So `ui::deformerRowsFor` names only the
leaves the kind's arithmetic reads, transcribed from the semantics comment above `struct Deformer`,
and `test_deformer_panel.cpp` holds it in both directions: every leaf it names must be a parameter
the object really registers (ADR-382, obtained from the registrar rather than from a table), and the
per-kind sets must match the ones the header documents.

### Three things the panel refuses to be silent about

- A leaf the table names and registration does not produce **says so** rather than drawing an empty
  box. That is the ADR-382 defect's own failure mode, made loud at the one place it can still occur.
- A Field or Path deformer naming a field or spline the scene does not have shows the dead name,
  marked, and says it displaces nothing — the same refusal ADR-420 makes for a subscription, and for
  the same reason: a typo and a thing nobody has made yet look identical in a text box, so both are
  combos over what exists.
- A **Path deformer in world space says it is skipped**. That is what the implementation does
  (`scene/procedural.hpp`: "World-space Path deformers are skipped") and what nothing told anybody.

The generic `deform` group is suppressed for procedurals, or the same parameters would be drawn
twice — once explained and once not.

### A deformer the Add button makes does something

Each kind is seeded at a small visible value. ADR-360 shipped a wind body at strength 0 and the
owner reported it as "it looks unchanged"; `setNodeWindBody` already makes this decision for the same
reason. It is checked by *deforming a point*, not by reading the seeded numbers back, which would
only confirm that a number is the number it is — with a control that an empty stack and a disabled
deformer both leave the point exactly where it was.

## Four overlays nothing could switch on, and one switch that drew nothing

A census of the twenty-eight `bool`s on `rendering::DebugViewOptions` found the same defect in both
directions at once, which is why they are one entry here rather than two.

**Implemented, read by `debug_visualizer.cpp`, and with no checkbox anywhere**: `wind`, `vortex`,
`lights`, `lightClusters`. Reachable only from `--debug-draw`, which is to say not from the
application. The wind arrow grid has been in that state since ADR-055, and it is the one thing in the
engine that answers *which way is the air moving here* — the question every subscriber to ADR-420's
field bus now raises. The brief's §54 says do not make the artist guess what an invisible field is
doing, and the visualiser was already written.

**A checkbox with no reader**: `culling`. The application already knew — `application.cpp` refuses to
name it on the command line because "a command-line name for an inert flag is a promise the
application does not keep" — and nobody applied that argument to the checkbox that did exist.
`labs/overlays.hpp` and `test_lab_registry.cpp` each carried a standing note about it, waiting for
the Visibility Lab to wire it up.

**Deleted rather than implemented.** What it promised — culled instances in red — is what the `lod`
overlay already draws, in purple. ADR-225's rule is that a setting the application does not keep is
not a setting, and the honest resolution of a three-year-old promise nobody kept is to stop making
it. Both standing notes now record what happened instead.

`tests/unit/test_debug_overlay_reachability.cpp` asserts the general rule those were two instances
of: **every switch on that struct has a reader and a checkbox.** It reads the three source files,
because there is nothing to call — an ImGui checkbox leaves no trace a CPU test can query and the
visualiser's reads are inside a function that needs a device — in the way `test_effect_conformance`
reads `atmospherics.hpp` and `test_deformer_panel` reads `procedural.hpp`. Two exceptions, each
naming the file where the flag is consumed instead, and the test checks that claim rather than
accepting it.

## Consequences

**Every probe was made to fail before it was trusted (ADR-182).**

- Adding `Melt` to `DeformerKind` compiles with **zero errors** — the `-Wswitch` diagnostic is a line
  in a build log — and fails `the panel's kind list is exactly the kinds that exist` by name,
  printing `melt`. That is the whole argument for reading the header rather than relying on the
  compiler, and it is now measured rather than asserted.
- Deleting the Wind checkbox makes the reachability case report `wind` by name.
- The word boundary in the reachability search is load-bearing and is itself checked: without it,
  `lights` matches `lightClusters`, and an overlay with no switch of its own would pass on its
  neighbour's.

**The kind name is compared without case, and that is a fact rather than a convenience.**
`deformerKindName` returns the JSON spelling (`"twist"`) because it is half of a file format, while
the enumerator is `Twist`. `drawParticleSettings` makes the same separation in the other direction
and says why: tying a caption to a serialiser's spelling means a rename in one silently changing the
other.

**`-Werror=format` is on.** The one instance in the tree was a live bug — `control_panel.cpp`'s
supersampling tooltip was printf-style with two literal per-cent signs in its text, so it read two
doubles off an empty varargs list every frame the pointer rested on that row. Zero instances remain.

The blanket `-Werror` stays off, and the census that settles it is recorded in `cmake/Warnings.cmake`
rather than left to be re-measured: **3211 warnings over `src` and `tests`, of which 2584 are
`-Wmissing-field-initializers` and 2577 of those are in `src/ui/ui_logic.hpp`.** This supersedes
ADR-393's 1549/1462, which counted `src` only and predates that header growing. A stale census in an
accepted ADR is the same shape as a stale sentence in one.

`-Werror=switch` is recorded as the natural next step and deliberately not taken. Its two instances
are `NodeKind::City` in `scene/composition.cpp` and `Drag::Marquee` in `ui/sequence_panel.cpp`, and
what those enumerators should do in those switches is a design decision owned by whoever owns those
enums — not a silencing for a passing branch to perform. ADR-392 and ADR-420 are both about a kind
falling through a switch, so this is the compiler offering to catch that whole class for free, and it
needs their answer rather than ours.

**What this does not do.** It does not add a deformer kind, a field shape, or a mask. It writes no
shader and changes no rendered pixel in any existing scene: every change is a control that did not
exist, a control that did nothing, or a test. The `EffectorOp::Velocity` trap is untouched and still
live — it exists on the CPU path and is skipped by the GPU pass (`procedural_renderer.cpp:1469`), so
it is the hook somebody reaches for first and it does nothing in a rendered frame. It is named in the
deformation contract's first sentence and is the next thing to make work or make fail loudly.
