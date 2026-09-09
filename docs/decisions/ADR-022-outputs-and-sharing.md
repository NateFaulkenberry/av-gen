# ADR-022: Outputs and sharing — multi-window presentation, output mapping, Syphon/NDI

- Status: Accepted (2026-09-09)
- Research: `docs/research/audiovisual-systems.md` (Resolume output/advanced output, TouchDesigner
  Window COMP and Syphon/Spout TOPs, Notch outputs), `docs/research/architecture-options.md`
  (Dawn's Metal/IOSurface interop), `docs/dependencies.md` policy

## Problem

A live tool has to put the picture where the show needs it: on a second display or projector
(possibly several), warped and blended for projection, and into other software (Resolume, OBS,
media servers) without a capture card. It must do that without disturbing the single-engine,
single-render architecture, and without licensing traps.

## Alternatives considered

1. One window, and let the OS mirror or the user drag it to a projector.
2. Render the scene once per output (per-output cameras).
3. Render once into a final texture, present it through a per-output *mapping* pass to any
   number of windows, and publish the same texture to Syphon (macOS) and NDI (chosen).
4. Link the NDI SDK directly; use the Syphon framework binary.

## Decision

- The renderer draws the frame into an offscreen final texture; the main window shows it with
  the UI on top; every additional output (`app::OutputManager`, project block `"outputs"`) is an
  SDL window on a chosen display (borderless fullscreen or sized) with its own `gpu::Surface`,
  presented through `rendering::OutputMapper`: crop rectangle, four-corner projective warp,
  soft-edge blend margins with gamma, brightness/gamma, flips. Mappings are data (JSON) and are
  edited numerically in the Outputs tab.
- Sharing (`share::TextureShare`): the final texture is copied into an IOSurface-backed WebGPU
  texture (Dawn SharedTextureMemory) and published to Syphon through the Syphon framework built
  from source (BSD-2, pinned commit), one frame of latency at most; NDI is supported only through
  a runtime-loaded `libndi` that the user installs (never linked or shipped), fed from an async
  readback ring.
- Events for all windows are pumped once per frame (`platform::Window::pumpEvents`); closing an
  output window disables that output rather than quitting.

## Rationale

Rendering once and mapping per output keeps GPU cost flat with the number of outputs and makes
projection workflows (crop a wide canvas across projectors, warp for keystone, blend the overlap)
a presentation concern, which is where every surveyed tool puts it. The IOSurface path is the
native zero-copy transport on macOS and is what Syphon itself uses, so publishing costs one
texture copy. NDI's licence is fine for applications but its SDK cannot be redistributed, so
runtime loading is the only posture compatible with the dependency policy.

## Consequences

- Positive: any number of outputs with warp/blend; Syphon out of the box; NDI when installed;
  outputs and mappings saved in the project.
- Negative: no per-output camera (one canvas, cropped); warp is a single homography (no mesh
  warp); blend is per side (no arbitrary masks); Syphon/IOSurface are macOS only (Spout on
  Windows would need the same shape with DXGI shared handles).
- Follow-ups: mesh warp and mask images per output, per-output colour calibration, Spout,
  NDI receive as a texture input for shader layers.
