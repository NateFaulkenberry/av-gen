# ADR-002: Windowing and input

- Status: Accepted (2026-09-08)
- Research: `docs/research/tooling.md` §3, `docs/research/rendering.md` §3

## Problem

The application needs a native window with a Metal-backed surface for WebGPU, keyboard/mouse
input for the UI, HiDPI handling, and a path to multiple displays for live output later.

## Alternatives considered

1. SDL3 (zlib; 3.4.16 released 2026-09-02; stable since 3.2.0 in January 2025).
2. GLFW (zlib; 3.5.1 released 2026-07-31).
3. Hand-written Cocoa/AppKit layer in Objective-C++.

## Decision

**SDL3.** Create the window with `SDL_WINDOW_METAL | SDL_WINDOW_HIGH_PIXEL_DENSITY |
SDL_WINDOW_RESIZABLE`, obtain the `CAMetalLayer` via `SDL_Metal_CreateView` and
`SDL_Metal_GetLayer`, and pass it to WebGPU as `WGPUSurfaceSourceMetalLayer`. All SDL usage is
confined to `src/platform/`.

## Rationale

- Semver-stable, actively released, organisation-maintained; first-class CMake target
  `SDL3::SDL3`.
- `SDL_Metal_CreateView` gives the layer directly; no Objective-C++ glue required.
- Provides HiDPI, display enumeration, fullscreen/multi-display, gamepad, and clipboard, all of
  which a VJ tool will need; GLFW would need supplementing for multi-output workflows.
- Dear ImGui ships `imgui_impl_sdl3`.
- Keeps SDL_GPU and SDL audio available as fallbacks without a new dependency.

## Consequences

- SDL3 is a larger dependency than GLFW (build time roughly a minute on the dev machine).
- SDL's audio subsystem is not used; miniaudio owns audio (ADR-003). `SDL_INIT_AUDIO` is not
  requested.
- Event pumping happens on the main thread; the renderer runs on the main thread in 0.1.

## Rejected alternatives

- GLFW: fine for a single window, but Dawn's GLFW helper is optional code and multi-display and
  HDR handling are weaker. Kept as a documented fallback.
- Hand-written Cocoa: maximum control (EDR, ProMotion) but macOS-only and more code before first
  pixel.

## Revisit triggers

- Need for EDR/extended-range output control that SDL does not expose.
