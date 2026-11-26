# ADR-2176: a show is an audience output or performance mode; the transport playing and live input are not

- Status: Accepted (2026-11-26), eng/show-definition (owner decision, relayed by the coordinator). Supersedes ADR-2167
  decision 1 (what makes a show); everything else in ADR-2167 -- what a show holds, refuses and pauses, and the GPU
  lock it takes -- stands.
- Code: `api/show_state.{hpp,cpp}` (`ShowReason` is now `output | share | performance`; `showStateFor`),
  `api/session.{hpp,cpp}` (`LocalHost::setPerformanceMode`, `performanceMode`, `LocalHost::showState`),
  `api/session_tools.cpp` (the `session.info.show` schema text), `app/application.cpp` (a comment)
- Tests: `tests/unit/test_api_jobs.cpp` (`[adr2176]`), the GPU show test in `tests/rendering/test_engine_host_gpu.cpp`

## Context

ADR-2167 counted the transport playing and live audio input as shows, so that a projector mirrored from the editor's
display would be protected. The owner plays the transport constantly while editing, and live input and controllers
stay connected; under ADR-2167 every agent's GPU job stalled behind ordinary editing, and the owner's own renders
were held. The owner decided what a show is.

## Decision

1. **A show is either an audience output or explicit performance mode.** `ShowReason` is the closed set:
   - `output`: an output or projection window is open;
   - `share`: the frame is published over Syphon or NDI;
   - `performance`: the host is in performance mode.
   `active` is true when any holds. `transport` and `liveInput` are removed as reasons, with no alias: playing the
   transport, live audio input and live control input (MIDI/OSC) are not a show on their own. The transport and live
   input remain facts only for `remainingSeconds`, which is given only while a show is active and its transport plays
   a file to its end (no loop, no live input).
2. **Performance mode is a host flag.** `LocalHost::setPerformanceMode(bool)` (Studio's EngineHost reaches it through
   `local()`); `LocalHost::showState()` adds `performance` to the provider's reasons while it is on, and it makes a
   show on a host with no provider at all. It is the operator's statement that this is a show whatever is open (a
   projector mirrored from the main display, which no output window reveals). Nothing in Studio sets it yet: the
   future live workspace will. It is reported in `session.info.show.reasons`.
3. **No tool sets it.** A tool that lets a session put the host into a show (and so take the machine GPU lock and
   refuse every GPU job) is not trivially safe, so none is added. Proposed contract, for when one is wanted:
   `session.set_performance_mode {on: bool}` -> `{on, show}`; execution class `session`; permission `liveControl`
   (the operator's); Studio's own session only -- an external client is refused `PERMISSION_DENIED`, because a show
   is the operator's to declare; idempotent; logged; and `on: false` never ends a show that an output or share still
   makes.

## Consequences

- Playing a song in the editor no longer takes the machine GPU lock, holds the Render panel, or refuses the
  assistant's `render.*` and `performance.*`; agents' GPU suites no longer wait behind editing.
- A show projected by mirroring the main display, with no output window and no share, is not detected until
  performance mode is set (by the live workspace, when it exists).
- The measured gate of ADR-2169 is unaffected in substance: its live runs were shows by live input; under this
  definition they are not, so the frame loop simply skips the lock attempt (it measured 0.09-0.25 ms once per run).

## Revisit triggers

- The live workspace (it sets performance mode); a tool to set it (the contract above).
