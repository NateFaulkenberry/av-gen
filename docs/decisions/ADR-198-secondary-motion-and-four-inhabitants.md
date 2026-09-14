# ADR-198: Secondary motion, and four inhabitants instead of one placeholder

**Status:** Accepted
**Date:** 2026-09-14

## Secondary motion

The alien pack's clips are good and they are also **finite**: one walk cycle, played identically
every stride, on four characters at once. What makes a group read as alive is not more clips — it is
that no two bodies are doing exactly the same thing at exactly the same moment.

`liveliness` writes `MotionOffset`, the additive channel the entity layer already composes on top of
the authored pose, so the skeleton is untouched and an author's own rotation survives.

**What it deliberately does not do.** Lean-into-a-turn is `bank`, which already exists and does it
properly with a response time; a second knob for the same thing on the same body would fight it. And
"head movement" is not here, because **nothing can address a head**: `ISkeletonQuery` is declared,
stored, and never implemented, so every socket resolves against the entity origin. A whole-body nod
is what is honestly available, and that is what `nod` is.

**The bounce is the interesting one.** Its rate follows the body's own speed rather than a fixed
frequency, so a running character bobs faster than a walking one without anybody authoring the
relationship — and because `explore/speed` is a registered parameter that scenes already drive from
`audio.rms`, the bounce becomes music-reactive through the chain that exists rather than a second
one. **No signal is read in this file at all.** That is the whole audio story, and it is the mandate's
own instruction — do not create a second competing audio-reactive system — taken literally.

Per-body stride phase, rolled from the entity's own seeded stream: four characters walking at the
same speed with the same bounce would rise and fall together, which reads as one animation on four
puppets rather than as four creatures. That is the entire difference and it costs one number.

Every knob registered, because how much life a character has is a performance decision rather than a
property of the model. All default to **0** — a body that has been given the behaviour and no knobs
does not move, so a scene that gains one is unchanged.

## Four inhabitants

Glowmere Valley 2's single placeholder — `assets/imported/alien.gltf` at 0.08 scale, three clips, a
9.68 m body — is replaced by four characters of about **6 m**, each a different model, at a scale
derived per variant from its own measured height rather than one number applied to four bodies of
different sizes.

| | model | cruise | temperament |
|---|---|---:|---|
| **rook** | scout (bare head) | 5.6 m/s | the explorer — covers ground, runs often, rarely stops |
| **tide** | diver (helmet, spacesuit) | 3.2 m/s | the ambient one — slow, drawn to water and glow, stops a lot |
| **sage** | elder (exposed brain) | 4.0 m/s | the watcher — keeps near the others, reacts to impacts |
| **ember** | ranger (mask, armour, jet) | 4.6 m/s | the dancer — jumps on the beat, goes Crazy on a drop |

Built **entirely from knobs that already existed**, plus the two added this week. No new behaviour
was written for Glowmere and nothing in the engine names it. The difference between these four is
`explore`'s affinities, chances and speeds; a `clips` map that can finally say a turn is a turn and a
react is a gesture rather than a second idle; and per-character reactions through the ordinary signal
chain.

Measured over 120 simulated seconds: **rook 797.6 m, tide 342.5 m, sage 486.1 m, ember 573.6 m**,
with `tide` never once reaching a run and `rook` and `sage` both using one, and every body tracking
terrain from y 0.2 to 22.2. Four different creatures, not four instances.

## What the foot-slip diagnostic caught

The first gait numbers were derived as "a human's 1.6 m/s walk times the model scale" — 5.78 m/s for
a 3.6× body. ADR-161's diagnostic immediately said they were wrong:

> entity 'tide': travelling at 0.46 m/s against a walk clip authored for 5.76 m/s (0.1x foot slip);
> rate matching is on and saturated

The assumption inside that arithmetic was that these clips were authored for a person walking, and
they were not. The honest number for `walkSpeed` is **the speed the creature actually cruises at**:
then the clip plays at 1.0 when the body is doing what it normally does, and rate matching only has
to cover the slow-down into an arrival and the run.

The hysteresis bands scale with the body too. The defaults (`runEnter` 3.0, `runExit` 2.2) are
person-scale, and a six-metre creature is "running" at a speed a person would never reach — left
alone, all four would have sprinted from the moment they moved. Derived from each body's own cruise
and dash instead. No foot-slip warning survives 15 simulated seconds.

**This is what a diagnostic is for.** The numbers looked reasonable, the characters moved, and
nothing would have said the clips were playing at a tenth speed except the one line that compares
the two numbers that live in different files.

## Two limitations, both real

**A glTF node's surface cannot be tinted from the scene.** The engine says so on load — *"a 'material'
block on this kind is not used; its surface comes from the asset's own materials"* — so four
characters sharing one 128×128 atlas cannot be four colours without four atlases. Here they differ by
silhouette, which was the plan; it is a limit worth knowing before somebody wants a red one.

**`ember` did not jump in the probe**, and correctly: its `jumpSignal` is `audio.beat`, and a headless
probe with no track loaded has no beats. The hop is proven by its own unit test; in the scene it
needs the music.
