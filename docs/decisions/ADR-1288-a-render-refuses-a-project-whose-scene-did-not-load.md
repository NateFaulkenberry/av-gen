# ADR-1288: A render refuses a project whose scene did not load

- Status: Accepted (2026-10-09), proto/bio-valley (spec 07 §9)
- Code: `src/app/engine.{hpp,cpp}` (`Engine::requireProjectScene`), `src/app/application.cpp` (`makeRenderJob`, the
  headless and `--render` project load).
- Tests: `tests/integration/test_project_system.cpp` (`[adr1288]`).

## Context

`Engine::loadProject` fails only when the project document is invalid. When the scene it names fails to load, it
records `projectSceneError` and leaves the default orb scene in place. That is right for the editor: the author sees
the error in the status bar and can fix the file.

It was wrong for everything that writes frames. In the valley art pass, one ecosystem layer named a missing emitter
template. The whole scene was refused, and `avgen --render` drew the orb and grid for every frame and exited 0. A
broken scene looked like a finished render.

## Decision

`Engine::requireProjectScene()` turns `projectSceneError` into a failure that names the cause. It is checked:
- by `Application::makeRenderJob`, which serves `--render`, the render queue and the in-app render;
- by the project load at start-up when the run is headless or `--render`, so the process exits non-zero (1) with
  `the project's scene did not load ...: <cause>`.

The interactive editor still opens such a project, with its status message (QA pass, 2026-09-28).

## Consequences

- A render of a broken project writes no frames and exits non-zero. Scripts that checked only the exit code now see
  the failure.
- A project with no scene at all (audio only) is unaffected: there is no scene error to report.
