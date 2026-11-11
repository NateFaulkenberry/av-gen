# ADR-1997: The aliens walk, crests throw them, they keep off water, on a fixed step

- Status: Accepted (Musical Geography CP-D r3, the owner's draft4 notes, 2026-11-10), scene/musical-geography.
  Replaces ADR-1964's hop cue and ADR-1990's choreographed hops (E); amends ADR-1957 and ADR-1958.
- Code: `Hop` (`src/entity/behaviors.cpp`); `GroundSource::landscape` (`src/world/ground_source.hpp`) and
  `TileGround::landscape` (`src/scene/tile_ground.*`); `TileNavRules::landOnWater` (`src/entity/tile_nav.*`).
- Tests (`tests/unit/test_tile_characters.cpp`, `[tiles][hop][geography][adr1997]`):
  - a crest throws a resting body, higher for a bigger crest, never under the threshold, at most once per cooldown, and
    it lands on the drawn top;
  - 30, 45 and 60 fps throw and walk alike (positions within 1 mm at the shared instants);
  - a body placed on the sea moves ashore, and no rest or landing in a minute of walking, hopping and throws is on
    water;
  - the existing seek tests run with throws on (`HopScene`): a seek lands where a play does, mid-flight included;
  - the ADR-1991 feet canary runs 40 s (the landings moved);
  - `[.][measure]`: the ground's rise under one alien against its throws, on a real scene file.

## Context

The owner, on draft4: the aliens stand in one place and jump, to the beat or at random. They should walk around, and
"react to the changes in the landscape. If a wave of energy goes through the land, have it toss the alien up, then
retain the landing animation." Some aliens stood in ponds and rivers.

## Decision

1. **Walking.** The hop behaviour already walked between nearby targets with pauses and turns (ADR-1958); the cast's
   cue-only setting (ADR-1990) had frozen it between cues. That setting is gone: `trigger`, `cueOnly`, `cueEvery`,
   `cueGap`, `airBeats` and `maxWait` are removed (ADR-441; nothing else used them). `runChance` (default 0.4) is new.
   The cast's clips already include `Walking` and `Running`.
2. **The crest toss.** On its feet (resting or walking), a body feels the landscape's upward speed under it: a central
   difference over one step on `landscape`, the planning surface without the bodies' own landing springs (so its own
   landing does not throw it again). Above `tossThreshold` (m/s, 0 = off) the throw arms; it comes at the crest's
   peak speed, once the rise is 10% under the best seen or under the threshold again, at `tossGain` × that speed (capped by `tossMax`, 9 m/s), so a
   bigger crest throws higher. `tossCooldown` (s) keeps one body from being thrown again too soon. The flight is
   ballistic under `gravity` (9.81). A resting body goes up and comes down where it stood; a walking one carries on
   along its walk. It lands with the hop's landing: the `Land` activity (the landing clip), the spring and the glow.
   The three are live parameters: `hop/tossThreshold`, `hop/tossGain`, `hop/tossCooldown`.
3. **Deterministic under a seek.** The throw reads the planning surface (ADR-1964): what no route moves. The flight
   is solved at the throw, step by step on that surface, so when and where it lands is what a replay finds. The drawn
   arc carries the difference between the drawn and the planning surfaces at both ends, and spreads the last step's
   rounding over the flight, so the body leaves the drawn top and lands exactly on it (ADR-1991).
4. **A fixed step.** The behaviour now advances on 60 Hz steps anchored on the timeline (step k at k/60 s), whatever
   the caller's frame rate; the arcs are closed forms of the render time. This is the frame-rate dependence noted for
   Phase 8, fixed here: walking used to integrate the frame's dt, and a hop landed on the first frame past its end.
5. **Water.** No step of a path may land on water (`landOnWater`, off), so a hop no longer lands on a sea slab;
   walking already refused water. Goals on water are never chosen, and `preferWater` is gone. A body placed on water
   starts on the nearest dry tile within 12 tiles. A throw whose course would end on water is steered to the nearest
   dry tile within 3 of its end (chosen rather than a hop out: one flight, one landing).

## Consequences

- The audio routes on the swell (`response.low` on the amplitude) do not reach the throws: a seek does not replay
  them, so the planning surface leaves them out. Throws follow the waves' authored rhythm. Hypothesis, not checked:
  the timeline's (arc) keys of the swell parameters do reach the planning surface (it reads the parameters' bases),
  so keying the swell through the arc would make the throws follow the song's sections.
- `preferWater` and the cue keys in a scene are ignored (the behaviour's settings are read by name); a reaction aimed
  at `hop/trigger` has no target.
