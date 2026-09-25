# ADR-764: Preview stills come from a scratch copy, one small frame per shot, on request

**Status:** Accepted
**Date:** 2026-09-25
**Related:** ADR-753 (a preview reads a scratch copy), ADR-762 (the panel), ADR-320 (the render
preview's fixed atlas); spec §37, §55
**Implemented by:**
- `app::renderShotStills` and `app::proposedShots` (`src/app/director_stills.*`);
- `Application::serviceDirectorStills`;
- the panel's `Stills` button and its per-shot image.

**Tests:** `tests/rendering/test_director_stills_gpu.cpp` (`[directing][stills]`)
**Evidence:** `~/Desktop/av-gen-review/15-director-panel/05-preview-with-still.png`

## Decision

- **Where the pixels come from.** A still is rendered from a **scratch copy** of the project with
  the proposal installed:
  - `Engine::writeProjectCopy` writes it to the temp directory;
  - a separate offline `Engine` loads it, and the file is deleted once loaded;
  - `installCompilation` installs the proposal on it.

  The person's project is never loaded into, saved or edited. The test checks that its sequence,
  cameras, plan list, dirty state and path are all unchanged.
- **What is rendered.** One frame per proposed shot that is not blocked, at the shot's middle
  (`seekSeconds`, which is exact after ADR-800), at 256×144. It uses a **renderer of its own**, so
  the editor's renderer keeps its uploaded textures (a full re-upload on this scene is about
  0.9 s, from §37). The frame is taken through the film camera, so it shows the proposal's cut.
- **When.** Only when the person presses "Stills", or when a preview is made. The panel posts the
  request, and the host starts it between frames, outside the ImGui pass.
- **Without blocking the editor (`StillsSession`, amended 2026-09-25).** The first version was
  synchronous and blocked the editor for about 6 s.
  - **The scratch session is cached.** The key is the project path, its composition, and the
    history state with the panel's own preview taken off (`DirectorPanel::baseState`). Any edit,
    undo, load or save-as changes the key, and the next request rebuilds the session. A preview
    does not change it.
  - **The simulation runs on the session's own thread:** loading the copy, installing the
    proposal, and seeking to each shot. This is how `pathtrace::TraceJob` runs its offline
    engine. Only two things run on the main thread:
    - writing the copy, because that reads the live engine;
    - rendering each frame, because the GPU belongs to the main thread. The worker parks while the
      frame is taken.

    It does one shot per editor frame, and the panel shows the phase: "loading a scratch copy",
    "simulating to 01:32.500 for shot 1 of 1", "rendering".
  - **Measured in the running editor** (benchmark, one shot, `--profile-csv`, other agents' suites
    running):

    | Request | Wall time | On the editor's frames | Notes |
    |---|---|---|---|
    | First | 5.9 s | 1,026 ms | Almost all of it one ~1.0 s frame: the scratch renderer's first texture upload |
    | Cached | 63 ms | 11 ms | |

    The median frame stayed at 16.7 ms throughout. The remaining one-second hitch is uploading
    the scratch scene's 53 textures. Removing it would need either uploading off the main thread
    or sharing the editor's uploads, and neither is done.
- **How it is shown.** One fixed 1024² atlas holds up to 28 stills. It is created once and written
  in place, which is ADR-320's rule for ImGui textures. Each shot row shows its still and the
  instant it came from.

## Consequences

- The shot is visible before it is accepted, with nothing touching the person's file.
- The test proves the still is the proposal's shot: the same instant of the project without the
  proposal renders visibly different. The measured mean absolute difference is 31.0 of 255
  against a threshold of 8; with the install on the scratch copy removed it is 0.24, and the test
  fails.
- The scratch `Engine` no longer opens the OSC receiver (`Engine::setLiveControl(false)`,
  86d81b9a).
- **Known risk:** the worker's simulation adds to the TEMPORARY `probe2` counters, which the main
  thread also writes. The same is true of the path tracer's job. It is harmless while those
  counters are diagnostics only.
- **Found while measuring, and fixed:**
  - The panel's buttons sat below a long plan, out of view in a smaller window, so the UI script's
    clicks missed. The decision bar is now drawn first, under the request. The script also checks
    that the buttons are in view, not only that they were drawn.
  - A request refused while another ran was moved from, so it reran with no shots.
  - A new request could start in the same frame the last one finished, and so drop its stills.
