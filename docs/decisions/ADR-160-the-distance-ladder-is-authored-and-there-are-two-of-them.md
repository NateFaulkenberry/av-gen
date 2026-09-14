# ADR-160: The distance ladder is authored, all four rungs of it — and there are two ladders

**Status:** Accepted
**Date:** 2026-09-14

## Two complaints, one cause

Reported against Glowmere's wanderer: *"if I zoom out he'll appear in one spot at one distance, then
I'll zoom out and he'll move to another spot"*, and *"once I zoom out past a certain distance,
animations stop playing altogether"*.

A character in this engine has **two** distance policies, and they are separately authored:

| | decides | knobs |
|---|---|---|
| `SkinnedRig` | whether the skeleton is posed | `updateHz`, `nearDistance`, `farHz`, `cullDistance` |
| `EntityDesc` | whether the character is simulated at all | `fullDetailDistance`, `coarseInterval`, `cullDistance` |

Glowmere authors both, and they disagree. The entity simulates to **320 m**; the rig was posed to
**220 m**. The hundred metres between them is a character walking across the world in a frozen pose
— which is both reported symptom at once: the animation stops, and the thing keeps moving.

## Two of the four rungs could not be authored at all

Worse, and the reason nobody could see the disagreement: `nearDistance` and `farHz` lived on
`SkinnedRig` as compiled-in defaults and **nothing copied a scene's values over them**. Only
`updateHz` and `cullDistance` were carried from `NodeAnimation`. So Glowmere asks for `updateHz: 30`
and is served **20 Hz** past fifteen metres, by a ceiling that has no name in the scene file and no
way to raise. The authored number is a number with almost no effect.

## The decision

**All four rungs are authored on `NodeAnimation`**, carried to the rig on every rebuild, written back
by `toJson`, and read by `fromJson`. They belong to the *node* rather than to the rig for the reason
ADR-086 gives for copying rigs per node instance: a rig is an asset's skeleton, and a distance policy
is a scene's decision about one character in one world.

Unknown keys inside an `animation` block are now **named in a warning** rather than ignored. That is
the `"lodCount"` lesson (ADR-131): a key the parser does not read is a setting that reads as
configured in the file and is absent from the engine, and the only thing that finds it is somebody
eventually measuring the thing it was supposed to control.

**The two ladders are not merged.** They are legitimately different costs — posing is joints ×
instances of CPU every frame, simulating is a path query — and an author may reasonably want a
character simulated further than it is posed, because at 300 m a frozen pose is a few pixels and
nobody can see it. What is *never* right is the ordering: if the rig stops being posed **nearer**
than the entity stops walking, the band between them is a sliding character. So the composition
warns, naming both distances and the node, whenever `rig.cullDistance < entity.cullDistance`.

A warning rather than a clamp, because which of the two numbers is wrong is an art decision — the fix
may be to pose further or to simulate less far, and the engine does not know which. Naming both is
what lets somebody choose.

## What changed in the shipped scene

Glowmere's wanderer now authors `nearDistance: 25`, `farHz: 20`, `cullDistance: 320` — the rig is
posed as far as the entity is simulated, and the warning is silent. The near band was widened to 25 m
so the authored 30 Hz applies over the distance the character is actually looked at.

## Evidence

The warning fires on the unmodified shipped scene, naming 220 m and 320 m, and is silent after the
scene fix. `tests/unit/test_skeleton.cpp` moves all four rungs and checks the ladder responds to each,
including that the authored rate remains a *ceiling* rather than a suggestion. A new composition test
sets all four on a node and checks they reach the rig the node owns — the rig rather than the node,
because the node is what the file says and the rig is what the engine poses with, and the entire
defect lived in the gap between them — then checks they survive a save.

## What this does not fix

The entity's coarse stepping still advances the simulation in one chunk of accumulated `dt` past
`fullDetailDistance`. At Glowmere's 0.1 s interval and a walking pace that is centimetres and
invisible, but it is a jump rather than a slower continuous walk, and a scene authoring a long
interval would see it. Root motion (the other half of the animation review) is the setting where that
would start to matter.
