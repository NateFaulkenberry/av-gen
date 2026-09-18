# ADR-271: A UI edit lands in the project, and the number it leaves there has to be readable

**Status:** Accepted
**Date:** 2026-09-18

> I did change the beam but I did it through the UI - so if it got changed in the wrong spot then
> the UI is changing it the wrong spot so fix that

The owner set the tractor beam's radius with the emitter controls in
`WorldEditPanel::drawParticleSettings`. The panel showed metres. What persisted was

```
"particles/visitor-beam/extent": 0.053846150636672974     the scene authors [7.8, 7.8, 7.8]
"particles/visitor-beam/spread": 0.128                    the scene authors 0.042
```

Twice, on two nights, an agent read the first of those as ADR-264 session residue and reverted it.
Both times the reasoning was sound on the evidence available, and that is the finding: **from inside
the project file that number is indistinguishable from residue.** It is not a radius. It is not in
metres. Nothing in the document it sits in says what it multiplies. The only way to learn that the
owner had set a 0.42 m beam is to open a different file, find a 7.8, and do the arithmetic.

`test_beam_lab.cpp` then asserted that the beam's runtime radius equals the radius its scene
authors, and failed on the owner's own tuning. A guard built to catch a photograph of a run caught a
person authoring, and was believed over them.

---

## 1. The question that was actually being asked

Which target should a UI edit write? The panel's own comment states the choice and names its
blocker:

> The authored value stays where the scene put it, so this is a live adjustment rather than a
> re-authoring -- which is also the only form the undo system supports, since `EditCommand` carries
> parameter changes and whole nodes but has no entry for "a node's payload changed."

The obvious reading of the report is that this is the mistake, and that the fix is to re-author
`CompositionNode::particles` so the scene stays the source of truth. Three facts say otherwise, and
none of them is about particles.

**A project saves its scene by reference.** `Engine::saveProject` writes `assets.scene.path` plus a
hash of the bytes already on disk. Nothing writes a scene file except the explicit "Save Scene
As..." dialog and `--save-scene`. So a value re-authored into the composition is discarded by the
next Cmd-S -- which is ADR-225's defect exactly: *a setting the application does not keep is not a
setting.* To make re-authoring persist, the editor would have to rewrite a scene file on every drag.

**Scene files are shared.** `glowmere-stylized.scene.json` is the scene of both
`glowmere-stylized.json` and `glowmere-stylized-pbr.json`. An editor that re-authored the scene
would change a project the user did not open, and would have to re-run
`tools/refresh_scene_fingerprint.py` against every project that references it, on every drag.

**The repository has already answered this question, in the other direction, for the same class of
data.** ADR-207's world effects and ADR-230's atmospheric effects live on the `Composition` and are
saved *into the project* when the session's list diverges from the scene's, precisely because the
scene is by-reference and the render loads a project document. A particle field is the same kind of
thing.

So the project's `parameters` block is where a live adjustment belongs. That was never the mistake.
It is also, incidentally, what makes the edit keyable, modulatable, and undoable as an ordinary
`ParamChange` -- the blocker the panel's comment names turns out to be an argument *for* the
boundary rather than a cost of it.

## 2. What was the mistake: one of two adjacent controls was lying

```cpp
s.spread = p.spread->value();                // ABSOLUTE; seeded from the scene
s.extent = rest.extent * p.extent->value();  // MULTIPLIER over the authored value
```

Two sliders in the same panel, one replacing and one scaling, and both *displaying* an absolute
quantity -- "%.3f rad" and "%.2f m". A person reading the panel cannot tell them apart. A person
reading the file cannot make sense of one of them.

The family the multiplier belonged to is `lifetime`, `speed` and `size`, and that family is not
inconsistent: each of the three scales a **pair** -- `lifetimeMin`/`lifetimeMax`,
`speedMin`/`speedMax`, `sizeStart`/`sizeEnd` -- that one scalar cannot replace, and each says "x" on
the control that writes it. `extent` scales a single vector and its control says "m". It was the
only one whose displayed unit was not the unit it wrote.

### The fix

`particles/<name>/extent` is now **absolute, a `vec3`, seeded from the scene**, exactly like
`position` in the same registration list and `spread` two rows down. `applyParticleParameters` does
`s.extent = p.extent->value()`.

A `vec3` rather than a scalar because the field is one -- a sphere's radius, a disc's radius and a
box's three half-extents are the same three floats, and 21 scenes in `examples/` author a
non-uniform one, `[30, 18, 4]` of hyperspace streaks among them. The panel still offers one number,
so `scene::particleExtentFromRadius` keeps the authored ratio: the proportions are the scene's, the
size is the slider's. It lives in `scene/particles.cpp` and not in the panel because a panel is not
testable and this is the whole of what the control does to a number.

Writing the absolute value into all three components is the obvious way to stop multiplying and it
is wrong -- it flattens every non-uniform emitter in the repository the first time anybody touches
the control. That is an arm of the regression test rather than a note here.

## 3. What the unification costs

* **56** `"particles/*/extent": 1.0` keys dropped from six project files. `1.0` meant "as the scene
  authored it" and an absent key now means the same; the next save from the application writes the
  absolute value back.
* **One** behaviour change, one line: `constellation.json` routes `audio.bass` into
  `particles/coreKnot/extent` with `op: add, amount: 1.3`. Against a 1.0 multiplier that peaked at
  2.3x of a 1.5 m sphere -- 3.45 m. Against an absolute 1.5 it would have peaked at 2.8 m, so
  `amount` is now `1.95` and the peak is 3.45 m as before.
* Nothing else. Every other `extent` in the repository was authored in a scene and overridden
  nowhere.

The owner's tuning is expressed where the corrected architecture puts it, in the unit they set it
in: `glowmere-valley-2-multicam.json` carries `particles/visitor-beam/extent: [0.42, 0.42, 0.42]`
and `spread: 0.128`. No scene file changed, so no fingerprint needed refreshing -- which is itself
the argument for the boundary, stated as a diff.

## 4. What happened to the guard

ADR-264's production arm asserted that the beam's runtime radius equals the radius its **scene**
authors. It caught the residue on its first live encounter, which is the most a regression test can
be asked to do, and then it caught a person and was believed over them.

A project's `parameters` are *meant* to sit over its scene. The invariant that survives is narrower
and is the one the residue actually violated while the tuning does not: **the emitter runs at the
radius the parameter stack holds.** The residue broke it through a hidden `cbrt(0.061)` = 0.3936
from a saved `nodes/visitor-beam/scale` -- a beam width no file states and no panel shows. A typed
0.42 m does not break it.

That change is proved rather than asserted. `The beam probe still sees a node scale that resizes the
emitter` writes `[1, 0.061, 1]` into a scratch copy of the shipped project and watches the probe
report 0.3936x against a clean copy of the same file: one second of simulation, because the residue
is a load-time fact and does not need an abduction to be visible.

### And one assertion is gone, deliberately

The production arm no longer checks that every mesh corner of the lifted animal is inside the
emitter's radius. A 0.42 m disc at 0.128 rad is a beam that tapers up to the saucer rather than a
column, and every animal it lifts is wider than its mouth. Containment is a claim about the beam's
**width**, the width is now the author's, and the owner has changed it; a test asserting it in
production is a test asserting an art direction.

It stays in the lab, where the width is fixed at the 7.8 m ADR-218 sized from the cast and where a
claim about it belongs. What production keeps is the stronger and more relevant arm, the one a magic
offset cannot pass and the one the original report was about: the animal hangs on the axis, and no
further from it than its own size.

This is the one place this document overrules a predecessor rather than extending it, and it should
be read as such.

## 5. So it cannot come back

`tests/integration/test_project_particle_authoring.cpp` drives what the panel calls with nothing in
between -- `particleExtentFromRadius`, then `beginDrag` / `setBaseComponents` / `commitDrag` -- and
asserts the whole boundary rather than the symptom: the edit is still there in the engine an offline
render builds from the saved project, it is written into that project **as the number the slider
showed**, the scene file is not touched byte for byte, and the drag is one undo.

ADR-182, measured. With `extent` put back to the multiplier it was:

```
baseRadius(render, "visitor-beam") ... 1.0f is within 0.001 of 7.8   FAILED
REQUIRE( saved.is_array() ) .......... false                         FAILED
3 cases | 2 passed | 1 failed;  42 assertions | 40 passed | 2 failed
```

Both failures are the report. The parameter holds `1.0` where a radius belongs, and the document
holds `0.42 / 7.8` = `0.053846150636672974` -- byte for byte the number that was in the owner's
project and that was twice reverted as residue. After the fix, 49 assertions in 3 cases, all
passing.

Three control arms pass on both sides, which is what makes the two failures mean something: an
untouched project still reports the 7.8 m its scene authors; `spread`, absolute already,
round-trips identically either way, so the difference is `extent`'s semantics and not the save path;
and a 30 x 18 x 4 box halved through the radius control comes out 15 x 9 x 2 either way.

## Consequences

- `src/scene/particles.{hpp,cpp}`: `extent` is `Parameter<glm::vec3>`, absolute, seeded from the
  scene; `particleExtentFromRadius`.
- `src/ui/world_edit_panel.cpp`: the radius control writes metres and says so.
- Six project files lose 56 stale `extent` multipliers; `constellation.json`'s bass route is
  rescaled; `glowmere-valley-2-multicam.json` carries the owner's beam.
- `tests/unit/test_beam_lab.cpp`: the production arm compares runtime against the parameter stack,
  containment moves to the lab, and a control case proves the node-scale residue still fails it.
- `tests/integration/test_project_particle_authoring.cpp`.

## Revisit triggers

- **A UI control that edits a node's payload and is not a parameter.** Everything in the particle
  panel is a registered parameter, which is why the project could absorb this. A control that edited
  a field with no parameter behind it would face the blocker the panel's comment named for real, and
  the answer would then be to register the field rather than to write the scene.
- **A second project wanting a different beam.** The four Glowmere projects share a scenario and not
  a scene file, so each can carry its own now. If two projects ever share one scene *and* want
  different emitters, that is this mechanism working, and the scene's value becomes a default rather
  than a truth.
- **Anything that makes the editor write a scene file.** Then re-authoring becomes reachable, and
  the question of which target a UI edit writes is open again -- with the sharing and fingerprinting
  costs in §1 as the price of the other answer.
