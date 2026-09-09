# Metallic Reassembly: how the shot is built, and what was broken

Fifty-eight seconds. A machine holds itself together, comes apart, hangs in pieces, pulls itself
back, locks, and throws itself open again. Seven states on the project's cue timeline drive it.

| Beat | Time | disassembly | tumble | energy |
|---|---|---|---|---|
| Assembled | 0 s | 0.00 | 0.00 | 0.15 |
| Tension | 8 s | 0.06 | 0.05 | 0.50 |
| Disassembly | 16 s | 0.55 | 0.40 | 0.60 |
| Float | 26 s | 0.85 | 0.70 | 0.35 |
| Reassembly | 38 s | 0.25 | 0.15 | 0.70 |
| Lock | 48 s | 0.00 | 0.00 | 0.90 |
| Explosion | 54 s | 1.00 | 1.00 | 1.00 |

## The mechanic was never wired

The three macros existed as knobs, every state moved them, and **nothing routed them to
anything**. The `burst` repulsor, the `gather` attractor and the `tumble` curl field all sat at
their authored strengths for the whole shot, so the machine that comes apart never came apart: at
the Float beat, with `disassembly` at 0.85, the object rendered fully assembled. Five routes now
carry the macros to the fields, and `gather` has a resting strength so there is something for the
pieces to reassemble towards.

Two related authoring bugs went with it. A **field and a procedural object were both named
`core`**, so they collided in the parameter namespace, `procedural/core/*` never registered, and a
route to it had been dead since the scene was written; the object is now `heart`. And
`audio.onset` was adding **600 sparks per onset** on top of the spawn rate, which at this tempo is
over a thousand a second — the machine was permanently inside a confetti cannon. Sparks and arcs
now follow the `energy` macro, so idle is nearly quiet and the lock throws everything.

## Composition and lighting

- **A floor.** Without one the object hung in a void: nothing caught its shadow and nothing said
  how big it was. A 240-unit plate at 1.6% albedo, with distance fog towards near-black so the far
  ground falls away instead of ending at a bright horizon line.
- **IndustrialCold** rig: hard cold overheads with warm sodium practicals underneath. The warm
  interior of the machine against the cold key is the whole colour story.
- **Manual exposure** at a 50 mm equivalent. The meter was opening onto a black frame.
- **Volumetric density 0.02 → 0.0016.** At the old value the haze was a grey veil over everything;
  see trap 4 in `docs/shot-hyperspace.md`.
- The camera circles as the object opens, rises while the pieces float, comes back down and in for
  the lock, and is thrown back out by the burst — the one moment it reacts to the subject.
- The cue presets also carried `camera/position` and `camera/target`. The timeline replaces them,
  so they were dead weight, and dead weight of exactly the kind that costs an hour later.

## What still does not work

- The ring helix above the machine reads as chrome noodles: smooth, featureless tubes that do not
  belong to the same made object as the machined plates. It needs a material and a profile.
- The pieces are uniformly sized and uniformly distributed through the float. Real debris clumps.
- The floor is a featureless plate. It catches a shadow, which is most of its job, but it has no
  surface of its own.
- The camera arc stays within a narrow band of heights and distances. It is a move, not yet a shot
  with a wide and a close.
