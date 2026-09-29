# ADR-984: A set piece can give back what it lifted

**Status:** Accepted
**Date:** 2026-09-28
**Found by:** the GV3 targeted art pass, the Astronaut Musicians addendum §9-12
(`docs/glowmere-valley-3/art-pass/00-brief.md`): the riser's abduction takes the drummer instead of a horse, and
"once the shot no longer sees the drummer, restore him to his normal position behind the drum kit ... deterministic
and shot-specific ... no permanent transform state from the abduction leaks into subsequent shots"
**Amends:** ADR-928 (set piece templates), ADR-934 (a retired body leaves the world)
**Implemented by:** `stage::StepKind::Return`, `stage::Staging` (the binding's home, `restoreWritten`), the
abduction template's `returnSeconds` (`src/stage/setpiece.cpp`), `avgen_cast_trace` (`dissolved`, `returned`)
**Tests:** `tests/unit/test_staging.cpp`, `tests/unit/test_setpiece_templates.cpp` (`[adr984]`)

## Context

An abduction set piece ends each lifted body with `Retire` (ADR-934): hidden, out of the world, for good. The only
way back is `EntityWorld::reset`. That is right for a horse the saucer takes; it is wrong for a performer whose
seat must be filled again a few bars later, and there was no step that could fill it. A body released with
`Release` stays wherever the lift left it, in the air under the craft, turned by the lift's spin.

## Decision

1. **A `return` step** puts the role's body back where this run BOUND it -- the place and the facing it had when
   the query took it (`Staging::Binding` records both at bind time, and a checkpoint carries them like the rest of
   the run). It is a placement (ADR-911): one frame, so nothing that watches the body reads the jump as motion. Every
   parameter the scenario drove through the role goes back to its value before the scenario (the same record
   `Retire` replays, now `restoreWritten`), which is how the dissolve's `opacity` returns to 1. The body is then
   held there with no speed, so its gait comes to rest on its own clips, until the scenario releases it; after
   that it stands where it was put.
2. **The abduction template's `returnSeconds`** (a structure value, default 0 = retire as before): when it is more,
   each lifted body dissolves as before but is not retired; the departure gains a cue per body that waits that many
   seconds after the beam goes out and returns it. The set piece's timeline lasts until the last body is back.
3. The cast trace reports, per lifted body, when its dissolve ended (`dissolved`) and when and where it was put
   back (`returned`, `atReturn`), so a film can check both.

## Consequences

- GV3's riser takes the drummer from behind his kit on bar 93, he dissolves into the saucer on the drop, and six
  seconds later -- during a shot that does not see the kit -- he is behind it again, playing.
- Every abduction that does not ask for a return compiles to exactly the beats it did.
- A returned body is back in the world the whole time it was away (it was never retired): anything that looks for
  bodies may find it, hidden, while it waits. For GV3's drummer, who has no behaviours and stands in no query, that
  is nothing.

## Rejected alternatives

- **Releasing the body and letting a behaviour walk it home**: nothing would put back its facing or the lift's
  spin, and a walk is motion the next shot might see.
- **A hand-written scenario beside the set piece**: two clocks for one event, which is what set pieces replaced
  (ADR-928).
