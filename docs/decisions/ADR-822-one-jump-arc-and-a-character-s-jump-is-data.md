# ADR-822: One jump arc for the engine, and a character's jump is data

**Status:** Accepted
**Date:** 2026-09-24
**Related:** ADR-194 (`Airborne`, the autonomous hop), ADR-758/820 (performances), ADR-821 (clip
flights), ADR-441 (convert, don't alias); the Director's Slice 3 plan (M4)
**Implemented by:**
- `entity::JumpArc`, `planJump`, `minimumApex`, `checkArc`;
- `Airborne` flying the arc in closed form;
- `JumpSettings::maxApex` and `apexLimit()`;
- `EntityDesc::jump` (the `"jump"` block);
- `explore` reading the block, and not grounding in flight;
- `seq::Actor::airborne` (performer spans);
- `seq::jumpKeys`, `jumpSpan`, `jumpClipCue`, `jumpTimes` (`seq/jump.hpp`);
- `"jumpRunning": "Jump_running"` in the Glowmere clip maps.

**Tests:** `tests/unit/test_jump_arc.cpp` (`[motion][jump]`)

## Context

A director asked for "Rook jumps (over the Umbra cap), and the pulse fires at the peak" needs four
answers:
- how high Rook must go to clear the cap;
- whether he can;
- whether the ground lets him land;
- when the peak is.

The engine had one jump, `Airborne`: a hop that `explore` launches, integrated step by step, with a
horizontal speed chosen so the body "lands long or short". None of that could be asked in advance or
baked. The Director would have had to write its own ballistics, and a planned jump and an autonomous
hop would then have been two curves.

Building this found two further problems:

1. **Every autonomous hop since ADR-194 was a skid.** `explore::update` ran `applyGrounding` after
   every step, flight included, and `applyGrounding` *assigns* the height. The arc's height was
   written and then overwritten by the terrain's on the same step.
   - Measured: a 2.6 m hop peaked at **0.00 m**.
   - The existing tests counted `Jump` frames, which the skid had plenty of, and never asked how
     high the body went.
   - Glowmere's Ember and Vane are authored to hop 2.6 m on every beat in three scenes
     (`glowmere-valley-2`, `-song`, `-atmospherics`), and never left the ground.
2. **A character's jump lived on a behaviour.** `jumpApex`, `jumpGravity` and `landSeconds` were
   `explore` keys. A director's capability card and the autonomous hop would therefore each have had
   their own answer to "how high can Ember jump".

## Decision

### One arc, closed form

`planJump(from, to, apex, gravity)` is a parabola in the fraction `s` of the horizontal distance:

`y(s) = y0 + d·s + K·s(1−s)`, with apex `h = (d + K)² / 4K` and duration `T = sqrt(2K/g)`.

- It lands exactly on `to` and peaks exactly `apex` above `from`.
- Its launch speed satisfies `v0 = sqrt(2gh)`.
- It refuses an apex below the landing point.
- `Airborne` flies it in closed form (`at(elapsed)`), not by Euler steps. A play, a scrub and a
  bake of the same jump are the same curve to the bit, not three integrations that agree to a
  tolerance.
- A bank higher than the body's own apex is reached by rising 5 cm above the bank.

### The questions a validator asks

- **`minimumApex(from, to, centre, radius, top, clearance)`:** the least apex that clears a round
  footprint. The arc is concave, so clearing both edges of the covered stretch clears all of it.
  Tested: at 97% of the answer the arc clips the cap.
- **`checkArc`:** where the arc first meets a ridge under it, and how far the landing sits above or
  below the ground.

### A character's jump is one block on the entity

`"jump": {apex, maxApex, gravity, maxDistance, landSeconds, maxSeconds}`:
- Only non-default fields are written.
- A nonsensical block is refused.
- `apexLimit()` is `maxApex`, or `apex` when `maxApex` is absent.
- `explore` reads the block every step. Only `jumpRange` (how far this behaviour *chooses* to leap)
  stays on the behaviour, capped by the block's `maxDistance`.
- The three Glowmere scenes were converted: Ember's and Vane's `jumpApex`/`landSeconds` moved into
  their `jump` blocks, with `maxDistance` equal to their authored `jumpRange` of 7.
- Rook has no block, so his card reads the defaults: apex 1.1 m, limit 1.1 m. That is kept until
  the owner decides otherwise.

### A body in flight is not grounded

`explore` skips grounding while `Airborne::airborne()` is true, and marks the state airborne (no
ground plane) so foot IK does not plant on ground it has left. Landing re-grounds, as before.

### The Director's side, as helpers rather than arithmetic

- `jumpKeys(arc, launch)`: Linear keys at 60 Hz; the chord error on a 5 m jump is under 1 cm.
- `jumpSpan(arc, launch)`: the actor's `airborne` span. Inside it a performer keeps the actor's
  height instead of the terrain's. Without the span, the performer is snapped to the terrain;
  tested on Rook, it skids at 0.000 m above the ground.
- `jumpClipCue(clipSemantics, arc, launch)`: plays a jump clip once, with its *measured* takeoff
  and touchdown (ADR-821) laid onto the arc's launch and landing by its speed, and `then: gait`.
- `jumpTimes`: the takeoff, peak and touchdown in timeline seconds.

### Clip mapping

`Jump_running` is mapped as `"jumpRunning"` in the five aliens' clip maps in the four Glowmere
scenes. The Director's card can offer it, and nothing autonomous asks for it, so the film is
unchanged by the mapping.

## Consequences

- **Ember and Vane now visibly hop on the beat** in the three scenes that author it. That is what
  ADR-194 and their authors meant, and has never been on screen. It is a film change, flagged for
  the owner. The alternatives are to keep it, lower the apex, or drop `jumpSignal`.
- The multicam film does not author beat hops, so it is unchanged. The Director's benchmark runs on
  that film.
- `Airborne`'s horizontal speed now lands the body on its target rather than "slightly long or
  short". No existing test depended on the old overshoot.
