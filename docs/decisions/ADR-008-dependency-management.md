# ADR-008: Dependency management and build

- Status: Accepted (2026-09-08)
- Research: `docs/research/tooling.md` §1, §9

## Problem

Reproducible builds from a clean checkout on macOS (CMake 4.0.1) with a path to Windows/Linux,
no manual installs for contributors, offline rebuilds, and pinned dependency versions.

## Alternatives considered

1. CPM.cmake (MIT; 0.43.1) over FetchContent.
2. Raw FetchContent.
3. vcpkg manifest mode.
4. Conan 2.
5. Git submodules.

## Decision

- **CPM.cmake**, vendored at `cmake/CPM.cmake`, with every package pinned to a tag or commit and
  URL downloads pinned with a SHA256 where the source is an archive (Dawn).
- `CPM_SOURCE_CACHE` defaults to `${CMAKE_SOURCE_DIR}/.cache/cpm` (git-ignored) so a second
  configure is offline.
- All dependencies are added with `SYSTEM` and `EXCLUDE_FROM_ALL`; each package's options are
  passed explicitly (no global `CMAKE_POLICY_VERSION_MINIMUM`; per-package only if a dependency
  needs it).
- `cmake_minimum_required(VERSION 3.28...4.4)`; `CMakePresets.json` schema 8 with `debug`,
  `release`, `relwithdebinfo`, and `asan` configure presets, Ninja generator.
- Every dependency is recorded in `docs/dependencies.md` with purpose, licence, version, and the
  reason for selection.

## Rationale

- Zero setup for contributors: `cmake --preset debug && cmake --build --preset debug`.
- Pinning plus a source cache gives reproducibility comparable to vcpkg without a second tool.
- CMake 4.0's removal of <3.5 compatibility is handled per dependency, keeping failures visible.

## Consequences

- First configure downloads ~200 MB (SDL3, Dawn archive, ImGui, Catch2, etc.).
- Dawn is consumed as a prebuilt archive by default; `AVGEN_DAWN_FROM_SOURCE=ON` switches to a
  CPM git checkout with `DAWN_FETCH_DEPENDENCIES=ON`.
- Binary caching of compiled dependencies is not provided; ccache via
  `CMAKE_CXX_COMPILER_LAUNCHER` mitigates.

## Rejected alternatives

- Raw FetchContent: same mechanism with more boilerplate and no source cache.
- vcpkg/Conan: extra tool and registry lag for fast-moving dependencies (Dawn, ImGui docking).
- Submodules: no version pinning story for archives; clone friction.
