# ADR-188: Parenting gets an appearance, and an offset you can type

**Status:** Accepted
**Date:** 2026-09-14

## The report

*"I have no way to change the offset position of spores to their parent in the UI — please fix this
and draw some type of indicator to make it more obvious it's connected to a parent."*

It arrived after two rounds of the same defect: Glowmere's spore fall was misaligned with the caps it
falls from, was fixed by parenting each `-spores` node to its `-cap` with a local offset, and was
reported misaligned again. The fix was right both times. What was missing was any way to see that it
had been applied, or to adjust it afterwards.

## Why it was invisible

**Parenting has no appearance of its own.** An emitter parented to a cap looks exactly like one
dropped at the same world position. The difference only shows when the cap moves — which, for a node
nobody has moved since, is never. So a correct parenting and a missing one are the same picture.

And the per-object inspector was gated on being a *hero*. A spore emitter is not a hero and never
will be; the panel's answer for it was "not a hero — star it to aim the Auto-director at it", which
is true and useless. The one number about that object that mattered had no control anywhere.

## What was built

**A tie you can see.** A line from the child to its parent's origin, a cross at the parent end, and
the parent's name. Drawn from the selection rather than always, for the same reason the hero markers
are: a world of parented emitters would be a cat's cradle over the scenery at all times. Its own
colour, because the question it answers is a third one — not "what is selected" or "what is the
camera about" but "what does this thing move with".

**An offset you can type,** shown for any node with a parent and silent for one without. The number
edited is the node's **local** position, which is what parenting means: world is parent × local. So
an offset typed here survives the parent moving, which is the whole reason the emitter was parented
rather than placed.

It writes through `setNodePosition` — parameter base and authored transform together — and opens a
`beginDrag` on the history, so it is one undo step per drag and not one per frame, exactly as the
gizmo is. Writing the world position instead would be the derived-copy mistake this project has made
repeatedly: the flattener recomputes world from parent × local on the next update, and the edit would
last exactly one frame.

## The related bound

Selecting a mushroom stem drew a selection box many times the stem, reported in the same session.
That was `Composition::nodeBounds` reading the procedural's **cull** bound — a sphere of the source's
diagonal around every instance, which is right for a test that may never drop something visible and
badly wrong for a box a person is shown. A 0.6 × 8 × 0.6 stem has a diagonal of about 4, so the
sphere is an 8 m cube.

`ProceduralGeometry` now caches a second, tight pair alongside it: the source's box transformed by
each instance's rotation and per-axis scale, built in the same loop. The conservative pair still
serves culling. The tight one is what a person is shown — the selection box, the gizmo's centre, and
the hero a camera is aimed at.

**Two bounds, because they answer different questions.** Conflating them is what produced both the
gizmo floating off its object *and*, after that was fixed, a box that dwarfed it.
