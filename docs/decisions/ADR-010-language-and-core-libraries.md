# ADR-010: Language standard and core libraries

- Status: Accepted (2026-09-08)
- Research: `docs/research/tooling.md` §5-7, §9

## Problem

Pick a C++ standard that Apple clang 21, MSVC 2022 and GCC 14 all support, and the logging,
math and serialisation libraries used throughout the engine.

## Decision

- **C++23 with an allow-list.** Used: `std::expected`, `std::span`, `std::string_view`,
  `std::format`/`std::print`, deducing `this`, `std::ranges`, `std::mdspan`. Forbidden until all
  three compilers ship them: `std::generator`, `std::stacktrace`, `std::move_only_function`,
  modules, `std::flat_map` (allowed once GCC 14 support is verified on a Linux CI image).
- **Logging: spdlog 1.17 with external fmt 12** (`SPDLOG_FMT_EXTERNAL`). One `avgen::log`
  namespace wraps the default logger; never called from the audio callback.
- **Math: GLM 1.0.3** with `GLM_FORCE_DEPTH_ZERO_TO_ONE` and `GLM_FORCE_RADIANS`; right-handed,
  +Y up, column-major, matching glTF and WebGPU's [0,1] clip depth.
- **JSON: nlohmann/json 3.12** for project files and parameter serialisation; a versioned
  envelope `{ "format": "avgen-project", "version": N, ... }` with ordered migrations.
- **Error handling:** `std::expected<T, Error>` for recoverable failures at module boundaries
  (file loading, device creation, shader compilation); exceptions only for programming errors
  and are not caught by engine code.
- **Threading:** `std::jthread`; `std::atomic` and SPSC ring buffers for audio; no mutex is held
  while calling into miniaudio or Dawn.
- **Style:** `.clang-format` (LLVM base, 4-space, 110 columns); `namespace avgen::<module>`;
  no global mutable state; ownership via `std::unique_ptr` and value types; RAII wrappers for
  every GPU and audio handle.

## Rationale

- `std::expected` alone justifies C++23 for a codebase whose boundaries are all fallible I/O.
- spdlog/fmt, GLM and nlohmann/json are the mature permissive defaults; each has a verified
  2025-2026 release and first-class CMake.

## Consequences

- MSVC needs `/std:c++latest` or `/std:c++23preview` depending on version; documented in
  `docs/build.md`.
- fmt is compiled once and shared by spdlog and engine code.

## Rejected alternatives

- C++20 only: loses `std::expected`, which would otherwise be reimplemented.
- Eigen: LGPL/MPL2 and heavier than needed for transform math.
- glaze: C++23 reflection-based JSON, attractive but younger; revisit for hot-path serialisation.
- quill/glog: glog archived; quill unnecessary for current volume.
