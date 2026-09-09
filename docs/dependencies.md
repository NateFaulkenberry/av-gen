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
| fastgltf | v0.9.0 (C++20 build; downloads simdjson) | MIT | glTF 2.0 / GLB import | fastest maintained C++ loader, extensions we need (lights, emissive strength); ADR-005 |
| stb (stb_image, stb_image_write) | commit 2c980bb (2026) | MIT / public domain | PNG/JPEG/HDR decode, PNG/HDR encode | single-header, ubiquitous, HDR radiance support |
| AVFoundation, CoreMedia, CoreVideo | macOS 26 SDK system frameworks (Apple platforms only; not fetched, not pinned) | Apple SDK licence: system frameworks, nothing redistributed, no attribution obligation beyond the SDK terms | native video export (milestone 1.0): ProRes 4444/422, H.264, HEVC into .mov/.mp4 with audio muxing; `probeVideo` | licence-clean, hardware-accelerated on Apple silicon, no third-party code; research `offline-rendering.md` §8.2 |
| POSIX sockets (OSC) | system C library (`<sys/socket.h>`, `<netinet/in.h>`, `<arpa/inet.h>`, `<netdb.h>`, `<poll.h>`; not fetched, not pinned) | part of the OS; no attribution obligation | OSC 1.0 over UDP (milestone 1.1): `control/osc` receiver thread and sender, IPv4 only for now | the protocol is small enough to implement directly (encoder, decoder, pattern matcher, ~700 lines with tests); no third-party OSC library to licence, pin or audit; research `audiovisual-systems.md` §18 |
| ffmpeg (optional, external process) | whatever the user has installed (`VideoSettings::ffmpegPath`, `AVGEN_FFMPEG`, `PATH`, `/opt/homebrew/bin`, `/usr/local/bin`); not pinned | LGPL-2.1-or-later by default; **GPL-2.0-or-later** when built with `--enable-gpl` (libx264, libx265); `--enable-nonfree` builds are undistributable | any encoder the user's build has (VP9, AV1, `prores_ks`, HAP, ...) and any container | never linked, never shipped, never downloaded: avgen spawns the user's binary as a separate process and pipes raw RGBA frames to its stdin, so no libav* code enters the application and the licence of the user's build stays theirs; research `offline-rendering.md` §8.1 |

Maintenance status and release dates were verified against the projects' GitHub APIs on
2026-09-08 (`docs/research/tooling.md`, `docs/research/rendering.md`).

Video output (ADR-020) deliberately links no encoder library. Should libav* ever be linked, the
research's LGPL checklist applies: an LGPL-only build (no `--enable-gpl`/`--enable-nonfree`),
dynamic linking, matching sources shipped or offered, attribution in the about box. H.264/HEVC
patent pools are a separate, licence-independent question for a commercial product.

Planned, not yet added: meshoptimizer, tinyexr, libktx, ozz-animation (0.3+);
efsw (0.4 hot reload); libebur128, pffft (analysis extras); Tracy (profiling).
