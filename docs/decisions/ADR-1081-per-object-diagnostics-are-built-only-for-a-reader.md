# ADR-1081: Per-object renderer diagnostics are built only when something reads them

**Status:** Accepted (live quality, `docs/live-quality/00-brief.md` §9.1).
**Date:** 2026-10-03

## Context

`SceneRenderer::render` built a `RenderObjectDiagnostic` for every entity every frame: a name copy, an FNV hash of
the material, two posed-bounds evaluations (`entityCullBounds` unpadded and padded) and six frustum margins. Measured
by the render-path investigation: 1.51 ms of Glowmere's CPU frame, 0.46 ms of Liminal's. Its readers are the selected
entity's panel and trail in the editor (`diagnosticObject(selected)`, `TransformHistory`), renderer snapshots, probes
and GPU tests. None of them runs during ordinary live playback.

## Decision

1. `SceneRenderer::setDiagnosticRecords(bool)`, default **on**, so every test, probe and tool that builds a renderer and
   reads the records gets them exactly as before.
2. **The live editor turns it off** at start-up (`Application`, where the controller is configured).
3. **A named diagnostic entity turns the records back on** for every frame it is named
   (`diagnosticRecordsBuilt_ = diagnosticRecords_ || !diagnosticEntity_.empty()`). The editor names the selected
   entity every frame, so the panel and the trail keep working whenever something is selected.
4. **What still runs every frame:** the frame-level fields (camera, matrices, viewport; the aux debug view reads the
   near/far planes from them) and the non-finite model check, which refuses the frame and is not a diagnostic.
5. `diagnosticRecordsBuilt()` says whether the last frame built them.

## Alternatives considered

- **Build lazily on first read.** The records describe the frame as it was submitted (slots, offsets, cull reasons);
  rebuilding them after the fact would have to replay the submission.
- **Build only the selected entity's record.** Smaller still, but the trail and the panel are rarely open; the
  simple rule is enough and keeps `diagnosticFrame()` whole when anything reads it.

## Consequences

- `tests/rendering/test_gpu.cpp` `[live-quality]`: with records off the per-object list is empty and the image is
  byte-identical; naming an entity brings them back.
- Measured cost removed: see ADR-1082 and `docs/live-quality/REPORT.md` (in this worktree the timer fell from
  0.04-0.05 ms to 0.005 ms; the 1.5 ms figure needs the Glowmere characters, which this worktree's assets lack).
