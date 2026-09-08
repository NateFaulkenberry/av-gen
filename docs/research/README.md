# Research Index

Phase 0 research for the av-gen real-time GPU audiovisual engine. All documents were written on
2026-09-08. Each carries per-claim citations (source, URL, date accessed, what was learned,
relevance, confidence) and ends with a Sources list. `references.md` consolidates every source.

| Document | Scope | Outcome |
|---|---|---|
| [architecture-options.md](architecture-options.md) | Comparison of rendering *architectures* (in-house RHI on Metal, WebGPU, SDL_GPU, existing engines, Vulkan-everywhere), the layers above, and the decision | **WebGPU via Dawn behind a thin `gpu` module** |
| [rendering.md](rendering.md) | 15 rendering backends + GLFW/SDL3 evaluated against 12 requirements; comparison matrix; three finalists | Finalists: raw Metal RHI, WebGPU-native, SDL_GPU |
| [audio-analysis.md](audio-analysis.md) | Audio I/O and decoding libraries; analysis algorithms (STFT, bands, onsets, beat tracking, loudness); library survey; signal model; testing strategy | miniaudio + KissFFT/pffft + in-house DSP; `audio.*` signal catalogue with a fixed processing chain |
| [shaders.md](shaders.md) | Shader languages, cross-compilers, reflection, hot reload, user-shader formats (ISF, Shadertoy, SSF), compute | ISF-style user contract; see architecture-options §12 for the WGSL re-routing |
| [assets.md](assets.md) | glTF 2.0 and extensions; loaders; image decoders; GPU texture formats; animation; IBL tooling; asset management | glTF canonical; fastgltf + meshoptimizer + stb_image + tinyexr + libktx + ozz in 0.2; nothing external in 0.1 |
| [particles.md](particles.md) | GPU particle architectures, sorting, trails, fields, fluids; 15 architectural requirements for the renderer | Passes as peers, persistent/transient resources, indirect args, no readback in hot path |
| [rendering-techniques.md](rendering-techniques.md) | PBR, HDR, bloom, volumetrics, DoF, motion blur, TAA, SDF/ray marching, feedback, reaction-diffusion, fluids, frame graphs | Requirements matrix technique -> abstraction |
| [offline-rendering.md](offline-rendering.md) | Deterministic frame rendering: injected clock, seeded RNG, timeline-indexed analysis, readback, image/video output, visual regression | FrameClock + AudioFeatures::at(time) + render-job struct from day one |
| [audiovisual-systems.md](audiovisual-systems.md) | Notch, TouchDesigner, Unreal, Unity, Resolume, Processing, Hydra, Max/vvvv, Synesthesia, Milkdrop, Cables, Godot, ECS/reflection, Blender drivers, DAW modulation, OSC | 14 design lessons and a conceptual parameter/modulation data model |
| [tooling.md](tooling.md) | Dependency management, UI, windowing, testing, logging, math, JSON, reflection/ECS, file watching, profiling, sanitizers, C++ standard, Apple specifics | CPM.cmake, SDL3, ImGui+ImPlot, Catch2, spdlog/fmt, GLM, nlohmann/json, C++23 allow-list |
| [references.md](references.md) | Consolidated source lists from every document | |

## How decisions flow from research

```
rendering.md ──┐
particles.md ──┤
techniques.md ─┼──> architecture-options.md ──> ADR-001 (rendering), ADR-002 (windowing)
offline.md ────┤                               ADR-012 (time model)
shaders.md ────┴──────────────────────────────> ADR-006 (shader system)
audio-analysis.md ────────────────────────────> ADR-003 (audio engine), ADR-004 (analysis)
assets.md ────────────────────────────────────> ADR-005 (asset format)
audiovisual-systems.md ───────────────────────> ADR-011 (parameters and modulation)
tooling.md ───────────────────────────────────> ADR-007 (UI), ADR-008 (dependencies),
                                               ADR-009 (testing), ADR-010 (language/core libs)
```

## Known gaps (carried forward as follow-ups)

- Dawn Metal backend support for `multi_draw_indirect` with a GPU count buffer is unverified.
- Tint's `-ffast-math` behaviour for MSL output is unverified (matters for offline determinism).
- Slang's direct Metal and WGSL targets were not exercised on real shaders.
- Metal Feature Set Tables were not opened; several attachment/format claims are second-hand.
- Build times of Dawn from source were not measured.
