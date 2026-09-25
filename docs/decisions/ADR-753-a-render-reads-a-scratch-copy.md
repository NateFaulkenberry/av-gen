# ADR-753: A render or a preview reads a scratch copy, never the person's project

**Status:** Accepted
**Date:** 2026-09-24
**Related:** ADR-440 (dirty = no longer serialises to what it was opened as), ADR-264/271,
ADR-750/752 (the Director program); spec §8 ("preview must never silently save the user's project")
**Implemented by:** `Engine::writeProjectCopy`, `app::renderSourceFor` (`src/app/render_source.hpp`),
`Application::writeRenderSource`, `startRenderFromUi`, the render queue (`onEnqueueRender`/`onRunQueue`),
and `makeRenderJob`'s `outputBase`
**Tests:** `tests/unit/test_director_preview_isolation.cpp` (`[directing][preview]`)

## Context

A render builds its own offline `Engine` and loads a project **file**, which is what makes it
reproducible. The editor got that file by saving the person's own project over itself:
`startRenderFromUi` and the render queue called `saveProject(projectPath())`. The consequences:

- Every unsaved edit was written into the person's file whenever they pressed Render.
- The same save moved the ADR-440 baseline, so the "Save changes?" prompt that would have let them
  decline never appeared.
- The queue refused an unsaved session, only because it needed a file to save.

The Director will preview a plan by exactly this route: a scratch session loaded from a file. So the
rule a preview needs is the one a render should always have had.

## Decision

- `Engine::writeProjectCopy(path)` writes the document `saveProject` would write, **without
  adopting it**. The project path, the dirty baseline and the dirty state are untouched. Asset paths
  are made relative to `path`, so the copy loads from wherever it is written.
- A UI render and a queued render each write a fresh scratch copy (`renderSourceFor`: temp
  directory, tagged with a per-session serial and the process id) and load that. A queued render
  copies at queue time, which is what the old save-then-queue captured, and the queue no longer
  requires a saved project. The copy is deleted once the job has loaded it.
- The output still resolves against the person's project folder (`makeRenderJob`'s `outputBase`),
  so a relative output path lands where it always did.
- A Director preview (Slice 1, `CompilePreview`) will use `writeProjectCopy` to build its staging
  session. It must never call `saveProject`.

## Consequences

- Pressing Render no longer writes the person's project or clears their unsaved-changes state.
  Tested at the engine level: file bytes, project path and dirty state are all unchanged, and the
  copy loads with 0 warnings and carries the unsaved edit.
- Not verified in the running UI: the render button and the queue themselves. The logic they call is
  tested; the ImGui wiring is not.
- The path tracer (`startPathTraceFromUi`) still loads the last-saved file when a project is saved,
  so it does not see unsaved edits. It never wrote the person's file, so it is out of scope here,
  but it is inconsistent with the raster render and could use the same scratch source.
