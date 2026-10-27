# ADR-1804: The MIDI rig's common parameters are measured over scenes that really loaded

- Status: Accepted (eng/qa-pass-2, 2026-10-27). Amends ADR-1705's measured set.
- Code: `src/control/midi_rig.cpp` (`midiRigCommonParameter`).
- Tests: `tests/integration/test_midi_rig_engine.cpp` ("every live scene has the rig's common parameters").

## Problem

ADR-1705 measured the parameters every live scene has (179) by loading each live example. Two of the 30, Aurora
Borealis and Rainbow World, have generated, gitignored scenes. Where those files were missing, `loadProject` still
succeeded: a project whose scene fails opens on the default scene (ADR-1288), and only `requireProjectScene` refuses.
The test counted those as loaded, so the intersection was taken with the default scene's parameters, not the two
scenes'. With every scene's generated files present (this pass's worktree), the 30 share 287 parameters and the case
failed: the camera's own parameters, the environment, the scene's fog, volume, light and wind, and the root
transform are on every live scene and were not called common.

## Decision

- The test skips (and names) a live scene whose files are unavailable (`exampleStatus`), requires every other scene
  and its scene file to load, and judges completeness only when every live scene loaded. The safety direction (every
  common parameter exists in every scene that loaded) is always judged.
- `midiRigCommonParameter` names the measured 287: ADR-1705's set plus `camera/breath/`, `camera/shake/`,
  `env/dayNight/`, `env/sky/`, `scene/wind/`, `scene/glow-pools/` and the camera, environment, root and scene
  parameters listed in the code.

## Consequences

- The MIDI panel's destination picker, choosing for the rig, no longer marks these parameters "this scene".
- A future live scene without one of them fails the test, as ADR-1705 intended.
