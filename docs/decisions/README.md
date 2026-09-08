# Architecture Decision Records

Each record follows: Status, Context/Problem, Alternatives considered, Decision, Rationale,
Consequences, Rejected alternatives (with the decisive reason), and Revisit triggers. Records are
immutable once Accepted; a change is a new record that supersedes the old one.

| ADR | Title | Status |
|---|---|---|
| [001](ADR-001-rendering-backend.md) | Rendering backend: WebGPU via Dawn behind a thin `gpu` module | Accepted |
| [002](ADR-002-windowing.md) | Windowing and input: SDL3 | Accepted |
| [003](ADR-003-audio-engine.md) | Audio decoding and playback: miniaudio | Accepted |
| [004](ADR-004-audio-analysis.md) | Audio analysis: in-house STFT pipeline over a pluggable FFT (KissFFT first) | Accepted |
| [005](ADR-005-asset-format.md) | Asset format: glTF 2.0 canonical; procedural-only in 0.1 | Accepted |
| [006](ADR-006-shader-system.md) | Shader system: WGSL files at runtime now; ISF-style user shaders later | Accepted |
| [007](ADR-007-ui-framework.md) | UI framework: Dear ImGui (docking) + ImPlot | Accepted |
| [008](ADR-008-dependency-management.md) | Dependency management: CPM.cmake with pinned versions | Accepted |
| [009](ADR-009-testing-framework.md) | Testing: Catch2 v3 + CTest; GPU-free by default | Accepted |
| [010](ADR-010-language-and-core-libraries.md) | C++23 allow-list; spdlog/fmt, GLM, nlohmann/json | Accepted |
| [011](ADR-011-parameters-and-modulation.md) | Parameter and modulation model | Accepted |
| [012](ADR-012-time-model.md) | Time model: injected RenderTime, sample-indexed analysis | Accepted |
| [013](ADR-013-image-based-lighting.md) | Image-based lighting: runtime split-sum preprocessing on the GPU | Accepted |
| [014](ADR-014-user-shader-contract.md) | User shader contract: ISF-style header + WGSL body, background/post layers, hot reload | Accepted |
| [015](ADR-015-gpu-particles.md) | GPU particles: compute pool with dead/alive lists, indirect draw, curl noise | Accepted |
| [016](ADR-016-post-processing.md) | Post-processing chain (bloom, grading, lens, DoF, motion blur, tone operators) over a transient pool | Accepted |
| [017](ADR-017-scene-composition.md) | Scene composition: flattened node compositions, nested scene files, asset registry | Accepted |
| [018](ADR-018-timeline.md) | Timeline: keyframe automation writes finals before routes; cues recall presets | Accepted |
