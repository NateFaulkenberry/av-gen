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
| [019](ADR-019-project-system.md) | Project system: relative asset references, explicit migration, bundles, recent files | Accepted |
| [020](ADR-020-offline-rendering.md) | Offline rendering: render jobs, PNG sequences, native/ffmpeg video, queue, determinism | Accepted |
| [021](ADR-021-live-control.md) | Live control: OSC and MIDI as control signals and direct parameter control, live audio input | Accepted |
| [022](ADR-022-outputs-and-sharing.md) | Outputs and sharing: final texture, per-output mapping windows, Syphon (IOSurface) and runtime NDI | Accepted |
| [023](ADR-023-procedural-geometry.md) | Procedural geometry: sources, distributions, seeded variation, GPU-instanced draw, deformer stack as data | Accepted |
| [024](ADR-024-spatial-data-and-attributes.md) | Spatial data: typed point attributes as the procedural currency | Accepted |
| [025](ADR-025-fields-and-effectors.md) | Fields as spatial control signals, effectors, GPU-identical evaluation | Accepted |
| [026](ADR-026-splines.md) | Splines as first-class spatial data | Accepted |
| [027](ADR-027-sdf-architecture.md) | SDF: data tree, GPU sphere tracing, CPU surface nets | Accepted |
| [028](ADR-028-procedural-graph-and-invalidation.md) | Procedural graph as an authoring layer; structural-hash invalidation | Accepted |
| [029](ADR-029-gpu-procedural-execution.md) | GPU point processing, culling, LOD, hierarchical generation | Accepted |
| [030](ADR-030-procedural-materials.md) | Procedural materials as an interpreted op program; colour utilities | Accepted |
| [031](ADR-031-states-macros-authoring.md) | Scene states, world macros, layered authoring, inspection, debug drawing | Accepted |
| [032](ADR-032-simulation-and-volumes.md) | Simulated fields and volumetric atmosphere | Accepted |
| [033](ADR-033-lighting-architecture.md) | Clustered forward lighting, area lights, colour temperature, light rigs | Accepted |
| [034](ADR-034-shadows-and-occlusion.md) | Cascaded shadows, contact shadows, ground-truth ambient occlusion | Accepted |
| [035](ADR-035-render-passes.md) | Auxiliary render targets and a declared frame graph | Accepted |
| [036](ADR-036-layered-materials.md) | Layered materials, geometric inputs, triplanar mapping, decals | Accepted |
| [037](ADR-037-cinematic-camera.md) | Physical camera, exposure and camera behaviours | Accepted |
| [038](ADR-038-composition.md) | Procedural composition, visual hierarchy and negative space | Accepted |
| [039](ADR-039-image-formation.md) | Image formation, selective post and tone mapping | Accepted |
| [040](ADR-040-particles-and-motion.md) | Particle trails, velocity-aware rendering and motion blur | Accepted |
| [041](ADR-041-world-director.md) | World Director, look presets and musical phrasing | Accepted |
| [050](ADR-050-material-interpreter-cost.md) | What the material interpreter was actually spending: the register file and the field evaluator in the loop | Accepted |
