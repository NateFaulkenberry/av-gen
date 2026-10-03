# ADR-1088: Start projection projects the open project, as it is configured

**Status:** Accepted (live quality, `docs/live-quality/00-brief.md` §18). Supersedes ADR-1026's decision 3 in part.
**Date:** 2026-10-03

## Context

ADR-1026's state machine opened the Sonic Live demo when the open project had no `sonic.live`, then waited for it to
load, and opening the window turned live Sonic input on. For a general audiovisual engine that is wrong: pressing
Start projection on Glowmere or Liminal replaced the project the performer had open. The projection investigation had
to add a probe (`AVGEN_X_PROJECT_ANY`) to project anything else.

## Decision

1. **`Projection` is Idle -> Running -> Idle.** `start()` takes no argument and always returns `OpenWindow`;
   `AwaitingProject`, `OpenLiveDemo`, `failed()` and the `loading`/`liveProject` observations are removed.
2. **`Application::openProjectionWindow` does not touch live input.** A `sonic.live` project turns its own input on
   when it loads (ADR-1025), so the Sonic Garden's live show works exactly as before through its own configuration.
3. **The Sonic Live demo is reached the normal way:** the Live panel's "Open live demo" button, then Start. The Start
   button's tooltip says so.
4. Every other way out of Running is unchanged (Stop, the window closing or Esc, the display unplugged, the window
   failing to open). Opening another project while projecting still does not stop the projection.

## Alternatives considered

- **Keep the swap behind a setting.** Special-case behaviour in the general path, which is what the brief rejects.
- **Ask ("This is not a live project. Open the Sonic Live demo?").** A modal before a show for a question whose
  answer is nearly always "no, project what I have open".

## Consequences

- `AVGEN_X_PROJECT_ANY` is no longer needed by any measurement.
- `tests/unit/test_projection.cpp` checks that Start opens the window at once and every way out.
