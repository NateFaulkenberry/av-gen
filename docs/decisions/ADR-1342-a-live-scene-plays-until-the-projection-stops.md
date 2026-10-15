# ADR-1342: A live scene plays until the projection stops

- Status: Accepted (2026-10-14), the owner's request
- Amends: ADR-1088 (Start projection projects the open project as it is) and ADR-1063 (the live scene switcher).
- Code:
  - `src/app/transport.{hpp,cpp}`: `Transport::setEndless`;
  - `src/app/engine.{hpp,cpp}`: `Engine::projectIsLive`, `refreshTransport`;
  - `src/app/application.cpp`: `startProjection`, `stopProjection`, `switchLiveScene`, `serviceLiveScenes`.
- Tests:
  - `tests/unit/test_transport.cpp` ("An endless transport plays on past the end; a bounded one still parks there");
  - `tests/integration/test_project_system.cpp` ("A live project's transport is endless; any other project's is not").

## Context

In a live scene, the audio- and MIDI-reactive routes, states, particles and the live beat clock run whether or not the
transport plays: the frame time falls back to the wall clock. The keyframed timeline tracks do not. The timeline is
evaluated at the transport position, and in 25 of the 29 live scenes those tracks move the camera: Sonic Live, all 9
Sonic Abstract scenes, and 15 of the 16 Sonic VFX scenes. In several scenes the tracks also drive the world's travel
or the emitters that draw the main figure.

The owner reported three problems:

1. **The camera stopped after 30 s.** With Glitch Signal projected and the transport started, the camera moved for
   30 seconds and then stopped. The transport's duration is the end of the longest timeline track (30 s for Glitch
   Signal), and a transport that reaches its end parks there.
2. **Starting a projection took two steps.** Start projection opened the window, then the transport had to be
   started separately. Stop projection left it running.
3. **A scene switch froze the camera.** Loading a project parks the transport at 0. When live input was already on,
   nothing started it again, so a camera scene switched to mid-set sat on its first frame until Space was pressed.

Every camera track in the live scenes is already authored as a seamless loop: its `loopLength` equals its span and
its first and last keys match. The tracks loop at different lengths, though: Sacred Geometry's roll loops at 36 s
and its position at 44 s. A global transport loop would therefore put a jump in the tracks; an unbounded playhead
does not.

## Decision

- **A live project's transport is endless.** For a project with `sonic.live`, the transport plays on past its
  duration instead of parking at the end. A seek or a new duration no longer pulls the playhead back, and Play from
  past the end continues from there. Each looped track wraps by itself, so the camera keeps flying for as long as the
  scene plays. The duration still sizes the timeline the editor draws. A usable transport loop still wraps. Other
  projects are unchanged and still stop at their end.
- **Start projection also starts the transport** when the open project is live, and **Stop projection stops it**.
  Other projects keep their transport as it was.
- **A switch keeps playing.** A switch from one live scene to another, made while the transport plays or the
  projection runs, starts the new scene's transport once it has loaded. This covers the Scene dropdown, its arrows,
  PageUp/PageDown and MIDI program changes. A switch from a non-live project is already covered: live input turning
  on starts the transport.

## Consequences

- A projected live scene animates fully from one click until Stop.
- The time readout of a live scene counts past the project's length. The transport-bar position is therefore
  "seconds played", not "seconds into a piece".
- Astral Forge Live, Digital Mosh Live and THE RIFT LIVE never needed the transport: their cameras move from
  conductors, LFOs and integrated routes. For them the change is harmless.
