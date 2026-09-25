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
  request; the host renders it between frames, outside the ImGui pass. It blocks the editor for
  those seconds and says so ("n still(s) from a scratch copy in x s"). On the benchmark that was
  3.4 s for the scratch session and 2.4 s for the seek and frame, with other agents' suites
  loading the machine.
- **How it is shown.** One fixed 1024² atlas holds up to 28 stills. It is created once and written
  in place, which is ADR-320's rule for ImGui textures. Each shot row shows its still and the
  instant it came from.

## Consequences

- The shot is visible before it is accepted, with nothing touching the person's file.
- The test proves the still is the proposal's shot: the same instant of the project without the
  proposal renders visibly different. The measured mean absolute difference is 31.0 of 255
  against a threshold of 8; with the install on the scratch copy removed it is 0.24, and the test
  fails.
- **Cost:** a scratch session per request. A cached scratch session is the next step if the
  seconds matter; that is not done.
- **Known:** the scratch `Engine` also tries to open the OSC receiver, and logs "address already in
  use" because the editor holds the port. It is harmless, and the one load warning on the scratch
  copy. Not fixed here.
