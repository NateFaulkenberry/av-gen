# ADR-1063: The live scene switcher: step through the Sonic VFX scenes while playing

- **Status:** Accepted (2026-10-02), proto/sonic-garden (VFX expansion; the coordinator's request for the brief's
  §20 live demo)
- **Code:**
  - `src/app/live_scenes.*` holds the decisions (GPU-free).
  - `Application::switchLiveScene` and `serviceLiveScenes` hold the wiring, along with the PageUp/PageDown case in
    `handleTransportShortcut` and the response carry in `performOpen`.
  - `ControlHub::takeProgramChange` latches the program change.
  - The Live panel's Scene row.
- **Tests:** `tests/unit/test_live_scenes.cpp` (`[live][adr1063]`). The panel was photographed in
  `~/Desktop/av-gen-review/25-sonic-vfx/eng/live-panel-response.png`.

## Context

The art agent ships 12-20 scenes as separate `sonic.live` projects in a new Examples category, "Sonic VFX". A
performer must step through them without the File menu, keeping live input and a running projection.

## Decision

1. **The list:** Sonic Live, then every "Sonic VFX" example, in `examples/index.json`'s order. The panel shows each
   name without a leading "Sonic VFX - ".
2. **Three ways to switch:**
   - the Live panel's **Scene** row (previous/next arrows and a list);
   - **PageUp / PageDown**, while live input runs or a listed scene is open;
   - a **MIDI program change**: program n opens scene n mod count, while live input runs.
3. **No prompt between listed scenes.** A switch from one listed scene to another opens directly; a performer mid-set
   is not asked whether to save an example. A switch from any other project goes through the unsaved-changes prompt,
   as every open does (ADR-440).
4. **What carries:**
   - **Live input** carries by itself: a `sonic.live` project keeps it on, and `setLiveSonic(true)` on a running
     session does nothing. The live note track, the input device and the runtime continue.
   - **The projection** carries by itself (ADR-1026: the machine's output, kept across loads).
   - **The performer's response** (`sonic/response/*`, project parameters) would not carry, so the switcher carries
     it as offsets from each scene's own defaults, applied before the new project's dirty baseline. Differences for
     the 0..1 controls, ratios for Attack and Release. A room that needs +0.15 sensitivity gets it in every scene, and
     a scene authored hotter stays hotter.
5. A scene load clears the parameters, so the response parameters are now registered just before the project's
   saved parameters are applied (`Engine::loadProject`), as well as in `setSonic` and each frame.

## Consequences

- An edit made to a listed scene during a set is not saved when switching away from it. That is deliberate for live
  use, and the scenes are examples.
- **Not tested on hardware:**
  - a real program change (no MIDI device on this Mac); the hub's latch is three lines and the mapping is unit-tested;
  - the switch timing: a scene load takes as long as opening the project (the audio-free live scenes are the fast
    case).
