# ADR-375: Reachable is not findable

- Status: Accepted (2026-09-19)
- Closes the brief's §16. Extends ADR-011 (parameters), ADR-350 (a setting the application does not
  keep), ADR-207 (the bespoke-panel pattern), ADR-360 (the wind).

## Problem

Every control this work has added is reachable. Registering a parameter makes it a row in the
Parameters panel, a modulation target, a timeline key, a preset member and a save entry — that is
ADR-011, and ADR-350 is satisfied by it.

The brief asks for something else, repeatedly and in bold, and it is not the same thing. The
Parameters panel is one flat list grouped by path prefix, in registration order. So "how windy is
it" is a scroll through `scene/` between `stylized` and `volumeAbsorption`; the vortex's seventeen
controls sit in whatever order they were registered in, next to the fog's; and the falling leaves
are under `particles/falling-leaves/` among `colorStart` and `stretch`, which are the particle
system's vocabulary and not a leaf's.

**The test for §16 is not that a control exists. It is that somebody who has not read the brief can
find it**, which means grouping by how the work is thought about — wind, leaves, the vortex — rather
than by how the parameters happen to be named.

## Decision

Two panels, named as the brief names them.

**Environment** — Cosmic atmosphere (fog, ambient, sky, and the nebula layer's intensity and
saturation) and Cosmic vortex (shape, motion, density, colour hierarchy, breathing), with the
vortex's controls disabled while its radius is zero so the gate is visible rather than implied.

**Tree** — Wind, split into *what the air does* (the field: speed, direction, gusts, turbulence,
regional variation) and *what each tree does with it* (per body: strength and the trunk/branch/
foliage/flutter influences). Those were one undifferentiated list of paths and are two different
questions. Then Falling leaves, ordered emission → motion → card → colour, which is the order the
decisions are actually made in.

### An empty section says why it is empty

A panel that omits a heading tells somebody looking for a control that they are in the wrong place.
One that says *"This scene sheds no leaves. A particles node named `falling-leaves` with `shape2d`
`leaf` and a `canopySource` is what makes one"* tells them they are in the right place and the scene
has not asked for it. Phases 5, 6 and 7 get a collapsed heading that says they are not built and
why, for the same reason: an absence somebody can read is not the same as an absence.

### A wind body is creatable now, not only tunable

ADR-360 left `windAuthored` settable only by hand-editing the scene file, and flagged it in its own
consequences. `Composition::setNodeWindBody` adds or removes one; the Tree panel offers it for every
Group node that has none. It is a structural edit — the parameter set changes shape — so it
re-registers the node and the panel calls `Engine::rebind()`, exactly as the world-effect panel does
for its own structural edits.

**A body created this way starts at strength 1, not 0.** ADR-360 shipped one at 0 and the owner
reported the scene as unchanged. A button that does nothing when pressed is worse than no button.

## The measurement

`tests/unit/test_cosmic_panels.cpp`, 4 cases, 85 assertions.

A hand-written panel asks for parameters **by string**. A wrong path — a typo, or a rename
elsewhere — does not fail to compile and does not throw; the row simply does not draw, and the
section degrades into an empty box indistinguishable from "this scene has no wind". So every path
both panels reference is asserted to resolve in a scene that has the feature, each loop with a
`nonesuch` control so it cannot pass against a set that answers yes to everything.

This is the half ADR-350 never covered: it asserted that parameters were *registered*, never that
anything pointed at them.

`setNodeWindBody` is tested for creating, for the non-zero starting strength, for idempotence, for
removal without leaking a parameter — a stale `nodes/oak/wind/strength` is precisely ADR-264's
orphan — and for leaving the node's ordinary parameters alone, which is the control that would catch
an unregister taking too much.

The unregister uses an **exact suffix list**, never a prefix sweep, per ADR-207.

Driven in the real application with `--ui-script panels,sliders` over 240 frames: no errors, and no
ImGui id conflicts, which ADR-361's detector is still watching for.

## Consequences

- Twenty-one panels in the View menu now. That is a lot, and the alternative — folding these into
  the World panel's inspector — would have put them behind a selection, which is the wrong place for
  a question about the whole environment.
- The leaf section finds its system by the `/spawnRate` suffix on a path containing "leaf" or
  "leaves", because a nested composition prefixes the node name and an exact path would miss it. A
  scene with two leaf systems shows the first. Worth revisiting if anyone builds a second.
- These panels duplicate rows that also appear in the Parameters panel. Deliberate: the flat list is
  the complete index and the place you go when you know the path, and these are the place you go
  when you know the *question*.

## Revisit when

- Phases 5, 6 and 7 land; their sections are already stubbed and named so adding them is filling in
  a heading rather than deciding where they go.
