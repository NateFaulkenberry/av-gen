# Dependencies

Policy: ADR-008. Every entry is pinned in `cmake/Dependencies.cmake`. "Why" summarises the
research conclusion; details and alternatives are in `docs/research/`.

| Library | Version (pinned) | Licence | Purpose | Why this one |
|---|---|---|---|---|
| Dawn | nightly `v20260907.201642` (commit c4e47b5e), prebuilt macOS arm64 Release archive, SHA256-pinned | BSD-3 | WebGPU implementation (Metal backend) | Chrome's Metal backend, standard `webgpu.h`, compute/indirect/readback, prebuilt CMake package; see ADR-001 |
| SDL3 | release-3.4.16 | zlib | window, Metal layer, input, file dialog | semver-stable, organisation-maintained, `SDL_Metal_CreateView`; ADR-002 |
| Dear ImGui | v1.92.9b-docking | MIT | immediate-mode UI | official SDL3 and WebGPU/Dawn backends; ADR-007 |
| ImPlot | v1.0 | MIT | waveform/spectrum/band plots | pairs with ImGui, handles large line plots |
| miniaudio | 0.11.25 | MIT-0 / public domain | decoding (WAV/FLAC/MP3), output device, WAV encoding | one permissive header for I/O and decode on all platforms; ADR-003 |
| KissFFT | 131.2.0 (compiled directly, float) | BSD-3 | real FFT | scalar and portable, bit-identical goldens; ADR-004 |
| fmt | 12.2.0 | MIT | formatting | used by spdlog and `Error` |
| spdlog | v1.17.0 (external fmt) | MIT | logging | mature, async-capable |
| GLM | 1.0.3 | MIT | math | GLSL-like, `GLM_FORCE_DEPTH_ZERO_TO_ONE` for WebGPU depth |
| nlohmann/json | v3.12.0 | MIT | project/parameter serialisation | ergonomic, `ordered_json`, migrations via JSON Pointer |
| Catch2 | v3.16.0 | BSL-1.0 | tests and benchmarks | best CTest integration; ADR-009 |
| CPM.cmake | v0.43.1 (vendored `cmake/CPM.cmake`) | MIT | dependency management | pinning + source cache without a second tool |

Maintenance status and release dates were verified against the projects' GitHub APIs on
2026-09-08 (`docs/research/tooling.md`, `docs/research/rendering.md`).

Planned, not yet added: fastgltf, meshoptimizer, stb_image, tinyexr, libktx, ozz-animation (0.2);
efsw (0.4 hot reload); libebur128, pffft (analysis extras); Tracy (profiling).
