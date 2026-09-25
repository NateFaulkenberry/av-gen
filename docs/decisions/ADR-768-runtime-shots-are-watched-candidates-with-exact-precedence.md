# ADR-768: Runtime shots are watched candidates, and precedence with them is stated exactly

**Status:** Accepted
**Date:** 2026-09-25
**Related:**
- ADR-245 (camera precedence: locked authored > event > authored > default);
- ADR-767 (watching the film);
- ADR-756 (`CAMERA_CONFLICT`);
- spec §54 ("runtime candidate shots; authored-vs-runtime precedence").

**Implemented by:**
- `ObservedEvent::endSeconds` and the camera spans in `app::watchFromCopy`;
- `runtimeShots` in the `director.watch_events` result;
- the watched-precedence branch of the shot checks in `validatePlan`;
- Info findings shown but not marked in `planItemRows`.

**Tests:** the `[directing][events][camera]` benchmark case in `tests/unit/test_directing_events.cpp`;
the Info-row case in `test_director_panel_logic.cpp`

## Decision

- **Runtime cameras are recorded as candidates.** An event camera takes the frame while its
  scenario runs (UFO Watch during an abduction). That is the runtime proposing a shot. A watch now
  records every such span as an observed event, `camera/<name>`, with its scenario and its end.
  `director.watch_events` lists them as `runtimeShots: [{camera, scenario, from, until}]`.
- **Precedence is stated exactly when the film was watched.** When a plan carries an observation
  that covers a shot, the validator checks each runtime span the shot overlaps.
  - For an **unlocked** shot, it warns with `CAMERA_CONFLICT`, naming the camera, the scenario
    and the seconds, and `winner: runtime`. It suggests locking the shot, or adopting the runtime
    shot.
  - For a **locked** shot, it gives an Info (`winner: authored`): said on the row, but not marked
    as a warning.
  - A shot the watch did not cover keeps the old generic warning.
- **Adopting a candidate is ordinary content.** A plan shot on the runtime camera's own `rig`,
  started at `{"event": "camera/UFO Watch", "occurrence": 1}` and locked, makes the runtime shot
  authored. It is a camera-track entry on that rig at those seconds, and nothing new is needed.

## Consequences

- **Measured on the benchmark.** The watch finds 'UFO Watch' holding the frame for the abduction
  at 13.50–19.57 s. A chase on Rook placed 0.5 s into that span:
  - unlocked, is warned as losing to UFO Watch, and played, the frame 1 s in is UFO Watch
    (`event`);
  - locked, is told it keeps the frame, and played, the frame is `rook-over-beam` (`shot`).

  The validator's claim and the play agree both ways. The adopted plan puts a locked cut on UFO
  Watch at 13.50 s.
- **Proven red:** swapping the locked and unlocked branches fails the checks.
- **Limitation:** the watch is audio-off (ADR-767). A scenario that audio drives may run at other
  seconds in a render.
