# ADR-007: UI framework

- Status: Accepted (2026-09-08)
- Research: `docs/research/tooling.md` §2

## Problem

Milestone 0.1 needs a minimal real-time control interface (open, play/pause, seek, volume,
response gains) and debug plots (waveform, spectrum, bands) that live alongside the engine
without entangling it.

## Alternatives considered

1. Dear ImGui (MIT; 1.92.x; docking branch) + ImPlot (MIT; 1.0).
2. Nuklear.
3. RmlUi.
4. Qt.
5. Native AppKit.

## Decision

**Dear ImGui (docking branch) with ImPlot**, using the in-tree `imgui_impl_sdl3` and
`imgui_impl_wgpu` (Dawn mode) backends. UI code lives in `src/ui/` and reads engine state through
the parameter system and a read-only `EngineStatus` struct; it never calls the renderer or audio
DSP directly. ImGui renders as the final pass of the frame into the swapchain view.

## Rationale

- Immediate-mode UI is the standard for engine tooling; parameter panels can be generated from
  parameter metadata automatically, which is exactly the property the parameter system needs.
- Official backends for both SDL3 and WebGPU/Dawn exist and are maintained by the ImGui author.
- ImPlot handles 100k+ point line plots for waveform/spectrum displays.

## Consequences

- The docking branch is a separate git branch; pin a commit, not a tag.
- ImGui's WebGPU backend requires the `IMGUI_IMPL_WEBGPU_BACKEND_DAWN` define; it is set on the
  ImGui target, not globally.
- The production performance UI (1.x) may need a different toolkit; nothing in the engine depends
  on ImGui.

## Rejected alternatives

- Nuklear: fewer widgets, no plotting library.
- RmlUi/Qt: retained-mode complexity and (Qt) licensing for a debug UI.
- AppKit: macOS-only.
