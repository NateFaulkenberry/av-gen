# ADR-1390: A project opens with its own view state, reset in one place

**Status:** Accepted (the owner's project-switch report, 2026-10-15).
**Date:** 2026-10-15

## Context

After opening the Lighting Lab, Astral Forge drew straight red, green, blue and pale-yellow lines through the forge.
They were the Lighting Lab's overlays (`worldAxes` and each light's arrow). Engineering Labs wrote its overlay profile
into the World panel's `DebugViewOptions` when it opened its fixture, and nothing ever wrote it back: every later
open kept it, and a render started from the editor copied it into the deliverable (`job->setDebugOptions`).

`Engine::loadProject` already resets the state the engine owns (ADR-896, e3dc8e9e). The overlays, the selections, the
viewport's pins and the renderer's diagnostic subject are held by the editor, which had no reset at all except the
world editor's history in `openAny`. Opens also come from places that do not go through `openAny`: an assistant's
`project.open` tool calls the engine directly.

## Decision

- `Engine::projectGeneration()` moves whenever a project or scene document **replaces** the open one (`loadProject`,
  `loadComposition`, `newProject`), and not for a rollback or a re-flatten (which `sceneGeneration` also counts).
- `app::ProjectSwitch` (pure data, `src/app/project_switch.hpp`) is observed once a frame against it. On a change it
  returns the overlays the new project opens with: the session's (`--debug-draw`), or a lab's, which the lab now
  *expects* before opening its fixture instead of writing into the panel. A failed open cancels the expectation.
- `Application::resetProjectViewState` applies them and resets everything else the editor holds about the project:
  the World panel's selection, generator pick and route focus; the world editor; the sequence strip's, composition
  panel's and graph editor's selections; the viewport's selected node, pending pick, gesture and "look through camera
  N" pin (camera ids are per project; the Editor/Film choice is the person's and stays); the transform trail; the
  Director stills; the Live Performance figures and the optimizer's undo; the renderer's diagnostic entity and
  temporal history.

Not reset, deliberately: layout, panel visibility, the preview mode, the live quality setting, the audio input and
MIDI source, the placement tool, the camera lock -- choices about the session, not the project.

## Consequences

- A lab's overlays last exactly as long as its fixture. The `--lab-case` start-up path is unchanged (start-up loads are
  taken as handled with `sync`).
- `tests/rendering/test_project_switch_gpu.cpp` renders a probe fresh and after every lab and live scene in one editor
  and requires identical bytes; it runs this rule, not a copy of it.
- Related: ADR-1391 (the overlay's joint markers), ADR-1392 (memory across switches), ADR-1393 (Astral Forge's re-seed).
