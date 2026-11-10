# ADR-1964: Hop on the music, and plan on what no route moves

- Status: Accepted (Musical Geography Phase 7, 2026-11-09), scene/musical-geography.
- Code:
  - the hop's `trigger` parameter (`src/entity/behaviors.cpp`);
  - `world::GroundSource::planned`;
  - `TileGround::set(..., plan, ...)`;
  - `Composition::baseTileDynamics`.

## Decision

**A cue to hop.** The hop behaviour gets a `trigger` parameter (0..1). Its rising edge through 0.5 sets a resting body
off at once, with a hop as its first step. A walking body hops its next step instead of walking it. The music drives it
through an entity reaction (`beat.pulse → hop/trigger`, or an onset). Reactions are replayed by a seek (ADR-870), so a
scrub cues the same hops a play does. Hop size and landing force are `hop/apex` and `hop/strength`, which can be
reaction targets in the same way.

**Plan on the planning surface.** A project route on `env/tiles/*` (the swell on `response.low`, live rings on onsets)
is not replayed by a seek. A body that planned on the drawn ground would choose different steps on a scrub than on a
play.

So the ground offers two surfaces:
- `at`: the drawn surface, which the body stands on;
- `planned`: the same dynamics built from the parameters' bases, with no live rings but with the landings' springs
  (the characters' own, which a seek replays).

The hop scores its goals and runs TileNav on `planned`. Its xz path is therefore independent of what the audio does to
the ground. Only its height rides the music.

## Consequences

- Tests:
  - a 30 s rest ends on the cue's frame with a hop;
  - two runs whose drawn grounds differ by a tripled amplitude take the same xz path (to within 1e-4 m over 30 s).
- **A seek that lands mid-hop** puts the body at the height a play does. The arc is a function of the current
  instant: both ends are read on the drawn ground now, and the extra rise over a step up comes from the planning
  surface at launch. Nothing is remembered from a ground that the replay drew without the routes.
  - Test: a real route swells the ground. A cold seek into the middle of a hop, compared with a play to the same
    frame, agrees to within 1e-3 m.
  - Canary: the old arc, which remembered its launch height, was 0.17 m off in the same test.
- A walk's 80 ms ease down a step still remembers the last frame's height. A seek landing inside that ease can
  differ from the play for those 80 ms.
