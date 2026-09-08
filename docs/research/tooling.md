# Tooling Research: Build, Dependencies, UI, Testing, Logging, Math, Serialization, Reflection, Misc

Research date: 2026-09-08. Dev machine (verified locally the same day): macOS 26.6.2 (25G83),
Apple M2 Max, Xcode 26.6 (17F113), Apple clang 21.0.0 (clang-2100.1.1.101, libc++
`_LIBCPP_VERSION` 210106), CMake 4.0.1, Ninja 1.13.2. Targets: macOS first; Windows (MSVC) and
Linux (GCC/Clang) must remain possible. Permissive licenses preferred. Automated tests from day one.

Every version/date below was verified against the official repo or docs on 2026-09-08. Where the
GitHub REST API was used (`gh api repos/<owner>/<repo>/releases/latest`), the ISO timestamp is the
`published_at` field. Citation blocks use the mandated format: Source / URL / Accessed / Learned /
Relevance / Confidence.

---

## 0. Recommended tooling stack (summary)

| Area | Recommendation | Version (verified 2026-09-08) | License | Rationale (short) |
|---|---|---|---|---|
| Build | CMake ≥ 3.28 required, presets schema ≤ 10; dev on 4.0.1 | CMake 4.4.3 latest; 4.0.1 local | BSD-3 | 3.28 gives FetchContent `EXCLUDE_FROM_ALL` + `SYSTEM`; 4.0 policy break handled per-dependency |
| Dependencies | **CPM.cmake** over FetchContent, commit-hash pinned, `CPM_SOURCE_CACHE` | v0.43.1 (2026-07-06) | MIT | Zero-install for contributors, offline once cached, reproducible via SHA pins; CMake-4-ready since 0.42.3 |
| UI | **Dear ImGui (docking branch)** + **ImPlot** | 1.92.9b (2026-07-31); ImPlot v1.0 (2026-04-05) | MIT / MIT | Official Metal, Metal4, GLFW, SDL3, SDLGPU3, WGPU backends in-tree; ImPlot handles 100k+ point line plots |
| Windowing | **SDL3** (primary), GLFW kept as fallback option | SDL 3.4.16 (2026-09-02); GLFW 3.5.1 (2026-07-31) | zlib / zlib | SDL3 stable since 3.2.0 (2025-01-21); gives Metal view, audio, GPU API, ImGui backends in one dep |
| Testing | **Catch2 v3** (+ `catch_discover_tests` → CTest) | v3.16.0 (2026-08-25) | BSL-1.0 | Best CTest integration, built-in micro-benchmarks, active |
| Benchmarks | **nanobench** for hot loops; Catch2 `BENCHMARK` for regression checks | v4.6.0 (2026-08-14) | MIT | Single header, no CMake coupling |
| Logging | **spdlog** with **external fmt** | spdlog v1.17.0 (2026-01-04); fmt 12.2.0 (2026-06-16) | MIT / MIT | One fmt copy project-wide; async sink; never log from the audio callback |
| Math | **GLM** with `GLM_FORCE_DEPTH_ZERO_TO_ONE` | 1.0.3 (2025-12-31) | MIT (or Happy Bunny) | Header-only, GLSL-like, Metal [0,1] depth via one define |
| Config/JSON | **nlohmann/json** (project files); glaze as future fast-path | v3.12.0 (2025-04-11) | MIT | Ergonomic, `ordered_json`, JSON Pointer/Patch for migrations |
| Reflection/ECS | **None in 0.1**; hand-written parameter descriptors. Revisit EnTT `meta` later | EnTT v4.0.0 (2026-07-23) | MIT | C++26 reflection not in Apple clang 21; ECS adds concept load without payoff yet |
| File watching | **efsw** (or dmon.h if a single header is preferred) | efsw 1.7.2; dmon 1.3.x | MIT / BSD-2 | FSEvents on macOS, inotify on Linux, RDCW on Windows |
| Profiling | **Tracy** with `TracyMetal.hmm` GPU zones | v0.14.1 (2026-08-22) | BSD-3 | Metal GPU zones since 0.12.0; macOS binaries since 0.14.0 |
| C++ standard | **C++23** with an allow-list (no `std::generator`, no `std::stacktrace`, no modules) | — | — | Apple clang 21 has `print`, `expected`, `mdspan`, `flat_map`, deducing-this; MSVC/GCC 14+ match |
| Compiler cache | ccache via `CMAKE_CXX_COMPILER_LAUNCHER` | v4.14 (2026-08-23) | GPL-3.0+ (tool only) | Not linked; license irrelevant to the product |
| Sanitizers | ASan+UBSan preset; TSan preset for audio/render threads | Apple clang 21 | — | Darwin arm64 supported by all three |

---

## 1. Dependency management

### 1.1 CMake itself and the 4.0 compatibility break

- Source: CMake 4.0 Release Notes
- URL: https://cmake.org/cmake/help/latest/release/4.0.html
- Accessed: 2026-09-08
- Learned: "Compatibility with versions of CMake older than 3.5 has been removed. Calls to
  `cmake_minimum_required()` or `cmake_policy()` that set the policy version to an older value now
  issue an error." A new variable `CMAKE_POLICY_VERSION_MINIMUM` (and an environment variable of the
  same name) "was added to help packagers and end users try to configure existing projects that have
  not been updated." Projects may use `<min>...<max>` version-range syntax.
- Relevance: Any third-party dependency whose top-level `cmake_minimum_required(VERSION 3.x)` is
  below 3.5 fails to configure under our CMake 4.0.1 when pulled in via FetchContent/CPM. Old but
  common offenders: unmaintained single-purpose libs, some stb-style wrappers, older RapidJSON.
- Confidence: High (official release notes).

- Source: CMake release index / GitHub releases
- URL: https://cmake.org/cmake/help/latest/release/index.html ; https://github.com/kitware/cmake/releases
- Accessed: 2026-09-08
- Learned: Latest CMake is 4.4.3 (2026-08-25); the 4.x line went 4.0 → 4.4 during 2025–2026. Our dev
  machine has 4.0.1, so anything that needs 4.1+ (e.g. presets schema 11/12) must be avoided in
  committed files.
- Relevance: Sets the floor and ceiling for CMake features used in `CMakePresets.json`.
- Confidence: High.

Workaround policy for this project (decision, not a citation):
- Set `cmake_minimum_required(VERSION 3.28...4.4)` at the top level.
- Do NOT set `CMAKE_POLICY_VERSION_MINIMUM` globally. When a dependency breaks, pass it per-package:
  `CPMAddPackage(... OPTIONS "CMAKE_POLICY_VERSION_MINIMUM 3.5")`, or prefer a patched fork /
  `PATCHES` argument. The Fedora CMake 4.0 change page and the search results above note the global
  form "is not upstream friendly and can hide build failures"; keep the blast radius per-dependency.
  (Source: https://fedoraproject.org/wiki/Changes/CMake4.0 , accessed 2026-09-08, confidence medium —
  community packaging guidance, not CMake docs.)

### 1.2 FetchContent (built in)

- Source: CMake `FetchContent` module documentation
- URL: https://cmake.org/cmake/help/latest/module/FetchContent.html
- Accessed: 2026-09-08
- Learned: `OVERRIDE_FIND_PACKAGE` (added 3.24) makes later `find_package(<name>)` calls resolve to
  `FetchContent_MakeAvailable(<name>)`. `SYSTEM` (3.25) marks the subdirectory's include dirs as
  system (silences third-party warnings). `EXCLUDE_FROM_ALL` (3.28) keeps dependency targets out of
  `ALL`. `FETCHCONTENT_TRY_FIND_PACKAGE_MODE` (3.24) = `OPT_IN` (default) / `ALWAYS` / `NEVER`.
  `FETCHCONTENT_FULLY_DISCONNECTED` disables all downloads/updates ("no attempt is made to download
  or update any content"). `FETCHCONTENT_SOURCE_DIR_<name>` redirects a dependency to a local
  checkout for hacking. A dependency-provider hook (`CMAKE_PROJECT_TOP_LEVEL_INCLUDES`) lets a
  package manager intercept `find_package`/`FetchContent_MakeAvailable`.
- Relevance: These three options (3.24/3.25/3.28) are exactly why our minimum should be 3.28, not
  3.14/3.16. `SYSTEM` + `EXCLUDE_FROM_ALL` keep `-Werror` and the default build target clean.
- Confidence: High.

CMake integration quality: native. Weakness: no caching across build directories (each build dir
re-downloads unless `FETCHCONTENT_BASE_DIR` is shared), verbose declarations, no version-conflict
diagnostics.

### 1.3 CPM.cmake

- Source: CPM.cmake README and releases
- URL: https://github.com/cpm-cmake/CPM.cmake ; https://github.com/cpm-cmake/CPM.cmake/releases
- Accessed: 2026-09-08
- Learned: License MIT. Latest v0.43.1 published 2026-07-06 (GitHub API). v0.42.3 added CMake 4
  support in its test suite; 0.42.0 shortened `CPM_SOURCE_CACHE` hash paths; 0.43.1 added
  `CPM_<name>_SOURCE` explicit → env fallback. `CPM_SOURCE_CACHE` (option or env var) caches sources
  across build dirs and "enables offline configuration if dependencies were previously cached".
  `CPM_USE_LOCAL_PACKAGES` / `CPMFindPackage` try `find_package` first; `CPM_LOCAL_PACKAGES_ONLY`
  errors if not found locally. README recommends "specifying immutable git commit hashes instead of
  tags or branches" for reproducibility. Listed drawbacks: no prebuilt binaries, requires
  well-behaved upstream CMakeLists, first-version-wins in diamond graphs.
- Relevance: Thin wrapper over FetchContent: same integration quality, plus a source cache, a
  one-line declaration form, `OPTIONS` pass-through, `PATCHES`, and `SYSTEM`/`EXCLUDE_FROM_ALL`
  forwarding. Contributors need nothing installed beyond CMake + a compiler.
- Confidence: High for facts; the CMake-4 claim is based on the changelog line, not a full audit.

### 1.4 vcpkg

- Source: vcpkg docs (manifest mode, binary caching, triplets) and repo
- URL: https://learn.microsoft.com/en-us/vcpkg/concepts/manifest-mode ;
  https://learn.microsoft.com/en-us/vcpkg/consume/binary-caching-overview ;
  https://learn.microsoft.com/en-us/vcpkg/concepts/triplets ; https://github.com/microsoft/vcpkg
- Accessed: 2026-09-08
- Learned: Latest registry tag 2026.07.29 (published 2026-07-31), MIT. Manifest mode uses
  `vcpkg.json` with `builtin-baseline` (a registry commit SHA) plus `overrides` for pinning, and
  installs per-project into `vcpkg_installed/`; `vcpkg-configuration.json` adds registries and
  overlay ports/triplets. Binary caching stores built packages ("binary packages") in a directory,
  NuGet feed, GitHub Packages, etc., keyed by an ABI hash that tracks compiler/flags. Triplets: the
  repository's `triplets/` directory (verified via GitHub API) contains `arm64-osx.cmake` alongside
  `x64-linux`, `x64-windows`, `arm64-windows` etc.; `x64-osx.cmake` lives under `triplets/community/`
  along with `arm64-osx-dynamic`/`arm64-osx-release`. The docs still say the default on OSX is
  `x64-osx` unless `VCPKG_DEFAULT_TRIPLET` is set.
- Relevance: Strong choice when you need large prebuilt-ish deps (Qt, FFmpeg, Boost). Costs: every
  contributor must clone/bootstrap vcpkg, CMake must be invoked with the vcpkg toolchain file (which
  interacts with our own toolchain/preset setup), and first builds are slow without a shared cache.
  arm64-osx being a first-class triplet removes the old Apple-Silicon concern.
- Confidence: High for triplet directory listing (repo API); Medium for the "default is x64-osx"
  statement (docs page dated 2024; newer vcpkg binaries auto-detect the host — set the triplet
  explicitly in presets regardless).

### 1.5 Conan 2

- Source: Conan 2 docs introduction; PyPI; GitHub releases
- URL: https://docs.conan.io/2/introduction.html ; https://pypi.org/project/conan/ ;
  https://github.com/conan-io/conan/releases
- Accessed: 2026-09-08
- Learned: Conan 2.32.0 released 2026-08-31, MIT. Docs say "Conan needs Python>=3.8". Provides binary
  management (prebuilt packages per OS/arch/compiler config), `CMakeDeps` + `CMakeToolchain`
  generators, and remotes (ConanCenter).
- Relevance: Most powerful for binary reuse and cross-config CI; but adds a Python runtime,
  profiles, and a second build-system vocabulary for contributors. Overkill for a project whose deps
  are mostly header-only or small CMake libraries.
- Confidence: High.

### 1.6 Recommendation: CPM.cmake

Use **CPM.cmake** (vendored `cmake/CPM.cmake`, pinned to v0.43.1) as the single dependency
mechanism for milestone 0.1, with these rules:

1. Pin every package by immutable commit SHA (`GIT_TAG <sha>`) and record the human version in a
   comment; CPM documents this as the reproducibility best practice.
2. Set `CPM_SOURCE_CACHE` in `CMakePresets.json` (`$env{HOME}/.cache/CPM`) and document
   `CPM_SOURCE_CACHE` + `FETCHCONTENT_FULLY_DISCONNECTED=ON` for offline builds.
3. Pass `SYSTEM YES` and `EXCLUDE_FROM_ALL YES` for every dependency (needs CMake ≥ 3.28, which we
   require anyway).
4. When a dependency trips the CMake 4.0 policy error, prefer `PATCHES` or a pinned fork; only as a
   last resort add `OPTIONS "CMAKE_POLICY_VERSION_MINIMUM 3.5"` to that one `CPMAddPackage` call.
5. Keep the door open for vcpkg manifest mode later (e.g. if FFmpeg or a large codec lib is
   needed): `CPMFindPackage`/`CPM_USE_LOCAL_PACKAGES` lets vcpkg- or Homebrew-installed packages
   satisfy the same declarations without rewriting CMake.

Rationale: reproducibility (SHA pins + source cache), offline capability (cache + disconnected
mode), and contributor ease (nothing to install; no Python, no toolchain file). Conan/vcpkg win only
when binary reuse dominates, which is not the case for an ImGui/SDL/Catch2/spdlog-sized graph.

---

## 2. Immediate-mode UI

### 2.1 Dear ImGui

- Source: Dear ImGui repo, backends directory, CHANGELOG, docking branch (GitHub API)
- URL: https://github.com/ocornut/imgui ; https://github.com/ocornut/imgui/tree/master/backends ;
  https://raw.githubusercontent.com/ocornut/imgui/master/docs/CHANGELOG.txt
- Accessed: 2026-09-08
- Learned: MIT. Latest release v1.92.9b (2026-07-31; hotfix over 1.92.9 of 2026-07-25). 1.92.x
  cadence roughly every 1–3 months (1.92.0 2025-06-25 … 1.92.9 2026-07-25). Master commit
  2026-09-08; `docking` branch merged master on 2026-09-07 (branch is alive, not merged into
  master; no merge announcement in the changelog). Backends present in-tree: `imgui_impl_metal.mm/h`,
  `imgui_impl_metal4` (new in 1.92.9, "forked from Metal 3 backend", plus metal-cpp support via
  `IMGUI_IMPL_METAL_CPP`), `imgui_impl_sdlgpu3.cpp/h` (added in 1.92.0), `imgui_impl_wgpu.cpp/h`,
  `imgui_impl_glfw`, `imgui_impl_sdl3`, `imgui_impl_sdlrenderer3`, `imgui_impl_osx.mm`,
  `imgui_impl_vulkan`, DX9–12, OpenGL2/3, Win32, Android, Null. 1.92.0 introduced the dynamic font /
  `ImTextureData` texture-update protocol (`ImGuiBackendFlags_RendererHasTextures`), and the Metal,
  SDLGPU3, WebGPU, Vulkan, OpenGL3 backends all implement it (verified in `imgui_impl_metal.h` and
  `imgui_impl_wgpu.h` feature lists).
- Relevance: Every renderer we are considering has a first-party ImGui backend; Metal is the
  reference path on macOS. Docking is required for a panel-based AV tool UI, so we build from the
  `docking` branch at a pinned SHA.
- Confidence: High.

Notes on renderer integration (all verified 2026-09-08):

| Renderer candidate | ImGui backend | Where | Notes |
|---|---|---|---|
| Raw Metal | `imgui_impl_metal.mm` (+ `imgui_impl_metal4`) | ocornut/imgui `backends/` | Objective-C++; `MTLTexture` as `ImTextureID`; dynamic-font textures supported |
| SDL_GPU | `imgui_impl_sdlgpu3.cpp` | ocornut/imgui `backends/` (since 1.92.0) | Pairs with `imgui_impl_sdl3` |
| WebGPU (Dawn / wgpu-native) | `imgui_impl_wgpu.cpp` | ocornut/imgui `backends/` | Must define one of `IMGUI_IMPL_WEBGPU_BACKEND_DAWN` / `_WGPU` / `_WGVK`; wgpu-native latest v29.0.1.1 (2026-06-23, Apache-2.0/MIT) |
| bgfx | `examples/common/imgui/imgui.cpp` + `.sc` shaders | bkaradzic/bgfx (BSD-2) | Lives in bgfx *examples*, not a library target; must be copied/adapted and rebuilt when ImGui changes |
| sokol_gfx | `util/sokol_imgui.h` | floooh/sokol (zlib) | Embeds Metal shader bytecode; C++ or cimgui modes; master commit 2026-09-08 |

- Source: bgfx and sokol repos (directory listings via GitHub API; sokol_imgui.h header comments)
- URL: https://github.com/bkaradzic/bgfx/tree/master/examples/common/imgui ;
  https://github.com/floooh/sokol/blob/master/util/sokol_imgui.h
- Accessed: 2026-09-08
- Learned: as in the table. bgfx has no releases (rolling master, last commit 2026-09-07).
- Relevance: If bgfx is chosen, the ImGui glue is our maintenance burden; with sokol, SDL_GPU, wgpu or
  raw Metal it is upstream's.
- Confidence: High.

### 2.2 ImPlot

- Source: ImPlot repo and releases
- URL: https://github.com/epezent/implot ; https://github.com/epezent/implot/releases
- Accessed: 2026-09-08
- Learned: MIT. v1.0 published 2026-04-05 (first release since v0.17); last commit 2026-08-06. v1.0
  deprecates `SetNextLineStyle`/`SetNextFillStyle` etc. in favor of the `ImPlotSpec` API; adds
  per-index colors/sizes, `PlotPolygon`, `PlotBubbles`. README: line plots handle "tens to hundreds
  of thousands of points"; data striding is supported; warns that ImGui's default 16-bit `ImDrawIdx`
  limits dense plots — either `#define ImDrawIdx unsigned int` or rely on the backend's
  `ImGuiBackendFlags_RendererHasVtxOffset` (the Metal/WGPU backends set it).
- Relevance: Waveform (time-domain) and FFT (log-frequency) plots for the engine UI. ImPlot3D is a
  separate experimental project (not needed).
- Confidence: High.

### 2.3 Alternatives

- Nuklear v4.13.3 (2026-05-05), dual MIT / public domain; C99 single header, no docking, weaker text
  and table widgets. (Source: https://github.com/Immediate-Mode-UI/Nuklear LICENSE and releases,
  accessed 2026-09-08, confidence high.)
- RmlUi 6.3 (2026-08-22), MIT, C++17, HTML/CSS retained-mode; requires FreeType (replaceable). Great
  for skinnable end-user UIs, heavier for a developer/tool UI, no ImPlot equivalent. (Source:
  https://github.com/mikke89/RmlUi readme, accessed 2026-09-08, confidence high.)
- Qt 6.x: LGPLv3 / GPL / commercial; LTS only for commercial holders. Retained-mode, huge build,
  licensing friction for static linking. Not appropriate for an engine-embedded UI. (Source:
  https://doc.qt.io/qt-6/qt-releases.html and https://www.qt.io/blog/qt-6.10.1-released, accessed
  2026-09-08, confidence medium — licensing summary from Qt's own pages.)

Recommendation: Dear ImGui docking branch + ImPlot, both vendored through CPM at pinned SHAs
(ImPlot v1.0 tag, ImGui docking SHA of 2026-09-07 or later).

---

## 3. Windowing and input

### 3.1 GLFW

- Source: GLFW releases and docs
- URL: https://github.com/glfw/glfw/releases ; https://www.glfw.org/docs/latest/news.html ;
  https://www.glfw.org/docs/latest/group__native.html
- Accessed: 2026-09-08
- Learned: zlib license. 3.5.1 published 2026-07-31 (a bad `3.5.0` tag was skipped). 3.5.1 adds
  unlimited mouse buttons (`GLFW_UNLIMITED_MOUSE_BUTTONS`), `glfwGetEGLConfig`, `glfwGetGLXFBConfig`;
  drops Windows XP/Vista and original MinGW. 3.4 (2024) added runtime platform selection, Wayland +
  X11 enabled by default, libdecor, `glfwGetCocoaView`. Native access: define
  `GLFW_EXPOSE_NATIVE_COCOA` before `glfw3native.h`; `glfwGetCocoaWindow` returns the `NSWindow`,
  `glfwGetCocoaView` (3.4+) the `NSView`. For Metal, create the window with
  `GLFW_CLIENT_API = GLFW_NO_API` and attach a `CAMetalLayer` to the view (`view.wantsLayer = YES;
  view.layer = [CAMetalLayer layer]`).
- Relevance: Minimal, well-understood, excellent CMake package (`glfw` target). No audio, no GPU
  abstraction; Metal path requires a few lines of Objective-C++.
- Confidence: High.

### 3.2 SDL3

- Source: SDL releases (GitHub API), SDL3 wiki (GPU, Metal, Audio)
- URL: https://github.com/libsdl-org/SDL/releases ; https://wiki.libsdl.org/SDL3/CategoryGPU ;
  https://wiki.libsdl.org/SDL3/SDL_Metal_CreateView ; https://wiki.libsdl.org/SDL3/CategoryAudio
- Accessed: 2026-09-08
- Learned: zlib. First stable SDL3 release was 3.2.0 on 2025-01-21; 3.4.0 (2026-01-01) is the current
  feature line with 3.4.16 (2026-09-02) the latest bugfix (roughly monthly). 3.4.0 themes: GPU API ↔
  2D renderer interop, Emscripten, pen input, native PNG. SDL_GPU backends: Metal (macOS 10.14+,
  iOS/tvOS 13+), Vulkan (Windows/Linux/Android/Switch), D3D12 (Windows 10+/Xbox); shader formats MSL,
  SPIR-V, DXBC/DXIL, with `SDL_shadercross` for offline/online cross-compilation.
  `SDL_Metal_CreateView(SDL_Window*)` (since 3.2.0, main thread only) creates a CAMetalLayer-backed
  NSView; `SDL_Metal_GetLayer` returns the layer; on macOS the app must assign the `MTLDevice` itself.
  Audio: everything is an `SDL_AudioStream`; logical devices over physical ones with automatic
  migration when defaults change; `SDL_OpenAudioDeviceStream` opens+binds in one call (starts paused);
  callbacks available; recording devices supported; uncompressed PCM only.
- Relevance: Stable ABI-versioned release train, single dependency for windowing + input + Metal view
  + audio I/O + (optional) GPU abstraction; ImGui has SDL3 and SDLGPU3 backends. Audio path is a
  reasonable first backend before a dedicated CoreAudio/ASIO/WASAPI layer.
- Confidence: High (dates from GitHub API; API semantics from the wiki).

### 3.3 Native Cocoa/AppKit

- Source: CMake `enable_language` docs
- URL: https://cmake.org/cmake/help/latest/command/enable_language.html
- Accessed: 2026-09-08
- Learned: `OBJC` and `OBJCXX` are first-class CMake languages since 3.16; `.mm` files are compiled
  as OBJCXX when the language is enabled.
- Relevance: A pure-AppKit window layer is feasible (ImGui ships `imgui_impl_osx.mm`), but it is
  macOS-only and would need parallel Win32/X11/Wayland implementations later. Reserve for the thin
  Metal-view glue only.
- Confidence: High.

### 3.4 Recommendation

**SDL3** as the platform layer (window, input, Metal view, audio device I/O), with the renderer
choice (raw Metal vs SDL_GPU vs others) decided in the graphics research doc. GLFW stays a viable
alternative if the renderer decision ends up "raw Metal + custom audio backend" and we want the
smallest possible dependency; the ImGui backends make swapping cheap.

---

## 4. Testing and benchmarking

- Source: Catch2 releases and CMake integration docs
- URL: https://github.com/catchorg/Catch2/releases ;
  https://github.com/catchorg/Catch2/blob/devel/docs/cmake-integration.md
- Accessed: 2026-09-08
- Learned: BSL-1.0. v3.16.0 published 2026-08-25; commits 2026-09-04. Exports `Catch2::Catch2` and
  `Catch2::Catch2WithMain`; `catch_discover_tests()` registers each `TEST_CASE` with CTest by running
  the binary with `--list-tests`; docs use `cmake_minimum_required(VERSION 3.16)` and a FetchContent
  example. Built-in `BENCHMARK` macro. Available via vcpkg and Bazel too.
- Relevance: Best CTest integration among the three; `Catch2WithMain` avoids boilerplate.
- Confidence: High.

- Source: GoogleTest releases/README
- URL: https://github.com/google/googletest ; https://github.com/google/googletest/releases
- Accessed: 2026-09-08
- Learned: BSD-3. v1.18.0 published 2026-08-10; README: "requires at least C++17". `gtest_discover_tests`
  provided by CMake's `GoogleTest` module.
- Relevance: Solid; larger compile times per test file than Catch2 v3 amortized library; GoogleMock
  is a plus only if we mock heavily.
- Confidence: High.

- Source: doctest repo and releases (GitHub API)
- URL: https://github.com/doctest/doctest ; https://github.com/doctest/doctest/releases
- Accessed: 2026-09-08
- Learned: MIT. Release history: v2.4.12 (2025-04-28) after a long quiet period, then a burst of
  activity by a new maintainer (mitchgrout): v2.5.0 (2026-03-27), 2.5.1, 2.5.2 (2026-04-14), v2.5.3
  (2026-07-06) — fixes for VS 2026, GCC 17, libc++. Last commit on master 2026-03-27 (releases are
  cut from a dev branch). 121 open issues.
- Relevance: The "less active" concern from 2023–2025 is partially resolved: it is maintained again,
  but with a small bus factor. Fastest compile times; weaker CTest discovery (`doctest_discover_tests`
  exists in `scripts/cmake`).
- Confidence: Medium-high (activity is recent; sustainability unknown).

- Source: ApprovalTests.cpp
- URL: https://github.com/approvals/ApprovalTests.cpp
- Accessed: 2026-09-08
- Learned: Apache-2.0. Latest release v10.13.0 (2024-03-12); commits continue (2026-08-27). Works with
  Catch2, GoogleTest, doctest, Boost.Test, CppUTest, [Boost].UT. C++11+.
- Relevance: Golden-file ("approval") testing for serialized project files, generated shader text,
  and DSP output dumps. For golden *images* we will write a small helper (PNG via stb_image_write,
  per-channel tolerance + PSNR threshold, artifact upload on failure) rather than depend on a
  library; ApprovalTests can drive the file bookkeeping.
- Confidence: High for facts; the image-diff approach is a design choice.

Benchmarks:

- Google Benchmark v1.9.5 (2026-01-21), Apache-2.0; commits 2026-08-28. Full-featured, needs a
  library build, JSON output good for CI trend tracking. (Source: https://github.com/google/benchmark
  releases via GitHub API, accessed 2026-09-08, confidence high.)
- nanobench v4.6.0 (2026-08-14), MIT, single header, "C++11/14/17/20", robust statistics, CSV/JSON/
  markdown output. (Source: https://github.com/martinus/nanobench, accessed 2026-09-08, confidence high.)
- Catch2 `BENCHMARK` — in the test framework we already use; adequate for regression guards.

Recommendation: **Catch2 v3** with `catch_discover_tests` (CTest labels: `unit`, `golden`, `slow`),
ApprovalTests.cpp for text goldens, a tiny in-repo image-diff helper for render goldens, and
**nanobench** for DSP/render micro-benchmarks (kept out of the default CTest run; a `bench` CMake
target). Enable `CMAKE_EXPORT_COMPILE_COMMANDS` and run tests under the ASan/UBSan preset in CI.

---

## 5. Logging

- Source: spdlog repo, LICENSE, bundled fmt header (GitHub API)
- URL: https://github.com/gabime/spdlog ; https://github.com/gabime/spdlog/releases
- Accessed: 2026-09-08
- Learned: MIT (LICENSE verified; GitHub reports NOASSERTION because of the bundled fmt notice).
  v1.17.0 published 2026-01-04 (default branch `v1.x`, commits 2026-09-05). Bundled fmt header
  reports `FMT_VERSION 120100` (fmt 12.1.0). Options `SPDLOG_FMT_EXTERNAL` (use an external fmt) and
  `SPDLOG_USE_STD_FORMAT` (use `std::format`, C++20) exist. Async logger with a bounded queue and
  overflow policy; sinks for stdout/rotating files/etc.
- Relevance: Mature, ubiquitous. With `SPDLOG_FMT_EXTERNAL=ON` we keep exactly one fmt in the graph
  (fmt is also used directly for string formatting and by Catch2-adjacent tooling).
- Confidence: High.

- Source: fmt releases
- URL: https://github.com/fmtlib/fmt/releases
- Accessed: 2026-09-08
- Learned: MIT. 12.2.0 published 2026-06-16 (12.0.0 2025-09, 12.1.0 2025-10). 12.x: improved C++20
  module support with separate CMake targets, constexpr `fmt::format`, `FMT_STATIC_FORMAT`, faster
  double formatting, deprecation of implicit `format_string`→`string_view` to align with
  `std::format_string`, a C11 API (`fmt-c`).
- Relevance: `std::format`/`std::print` exist in Apple clang 21 (`__cpp_lib_print` 202207, verified
  locally), MSVC and GCC 14+, so `std::format` is a *viable* dependency-free alternative. fmt still
  wins on compile time, `fmt::format_to` into fixed buffers, named args, and being the same code on
  all three toolchains. Decision: use fmt 12 explicitly; do not mix `std::format` in the same
  translation units.
- Confidence: High.

- Source: quill README/releases
- URL: https://github.com/odygrd/quill
- Accessed: 2026-09-08
- Learned: MIT, "Ultra-Low-Latency Asynchronous C++17 Logging and Metrics Library"; v13.0.0 published
  2026-08-30; very active. Hot-path cost is a lock-free queue write; formatting happens on a backend
  thread.
- Relevance: The only candidate suitable for logging *from* a real-time audio callback. However, the
  correct engineering rule is "never log from the audio thread"; ring-buffered event counters are
  read by the UI thread instead. So quill is not needed in 0.1.
- Confidence: High.

- Source: glog README (GitHub API)
- URL: https://github.com/google/glog
- Accessed: 2026-09-08
- Learned: BSD-3. Repository is **archived**; README: "This project is no longer maintained and will be
  archived on 2025-06-30. Consider using ng-log (API-compatible, community-maintained) or Abseil
  Logging". Last release v0.7.1 (2024-06-08).
- Relevance: Eliminated.
- Confidence: High.

Recommendation: **spdlog 1.17 + external fmt 12.2**. Default sinks: colored stdout and a rotating
file in the platform log directory. Provide `AV_LOG_*` macros that compile out below a level in
Release. Audio thread: no logging; use lock-free counters/overrun flags surfaced by the UI.

---

## 6. Math

- Source: GLM releases, manual, license file
- URL: https://github.com/g-truc/glm ; https://github.com/g-truc/glm/blob/master/manual.md
- Accessed: 2026-09-08
- Learned: Dual "Happy Bunny License (modified MIT, no military use)" **or** plain MIT — we can pick
  MIT. Latest 1.0.3 published 2025-12-31; commits 2026-04-07. Default clip space is OpenGL's Z in
  [-1, 1]; `GLM_FORCE_DEPTH_ZERO_TO_ONE` switches to Z in [0, 1] (Direct3D/Metal/Vulkan convention);
  `GLM_FORCE_LEFT_HANDED` switches the default right-handed view/projection helpers to left-handed;
  `GLM_FORCE_DEFAULT_ALIGNED_GENTYPES` enables aligned vector types (SIMD-friendly; changes
  `sizeof`). CMake target `glm::glm` (header-only; also `glm::glm-header-only`).
- Relevance: Metal's NDC depth range is [0, 1]; without `GLM_FORCE_DEPTH_ZERO_TO_ONE`,
  `glm::perspective` maps the near plane to -1 and half the depth precision is wasted / clipping is
  wrong. Handedness stays right-handed (Metal does not mandate handedness; only the projection
  matrix does). Define both flags project-wide via `target_compile_definitions(... PUBLIC
  GLM_FORCE_DEPTH_ZERO_TO_ONE GLM_FORCE_RADIANS)`; do **not** force left-handed unless the renderer
  spec chooses it.
- Confidence: High.

- Source: Eigen GitLab releases (API)
- URL: https://gitlab.com/libeigen/eigen/-/releases
- Accessed: 2026-09-08
- Learned: 5.0.0 released 2025-09-30, 5.0.1 on 2025-11-11 (3.4.1 maintenance release 2025-09-29;
  3.4.0 dated back to 2021-08-18). Eigen is MPL2 with a few LGPL-licensed optional components that
  are excluded when `EIGEN_MPL2_ONLY` is defined.
- Relevance: Not for graphics math (verbose for vec3/mat4 game-style code). Reserve for DSP/linear
  algebra (filter design, least squares) if it appears; define `EIGEN_MPL2_ONLY`.
- Confidence: High for dates; Medium for the C++ standard requirement of 5.x (not verified; believed
  C++14).

- DirectXMath: MIT, "jun2026" release (2026-06-12), SIMD-first, Windows-centric conventions
  (row-major, left-handed helpers); usable on arm64 via NEON but its API style clashes with
  GLSL/MSL-like code. (Source: https://github.com/microsoft/DirectXMath, accessed 2026-09-08, high.)
- HandmadeMath: CC0, v2.0.0 (2023-02-20), last commit 2026-03-17; C single header, supports both
  depth conventions and handedness via function suffixes (`HMM_Perspective_RH_ZO`). Nice but small
  community. (Source: https://github.com/HandmadeMath/HandmadeMath, accessed 2026-09-08, high.)
- linalg.h: Unlicense, v2.2 (2023), last commit 2023-07-02 — effectively frozen. (Source:
  https://github.com/sgorsten/linalg, accessed 2026-09-08, high.)

Recommendation: **GLM 1.0.3** under MIT with `GLM_FORCE_DEPTH_ZERO_TO_ONE` + `GLM_FORCE_RADIANS`,
wrapped in an `av::math` namespace alias so a later swap (e.g., to HandmadeMath for C ABI) is local.

---

## 7. Serialization and configuration

| Library | Version (date) | License | C++ | Status | Notes |
|---|---|---|---|---|---|
| nlohmann/json | v3.12.0 (2025-04-11) | MIT | C++11 | active (develop commits 2026-09-07) | DOM API, `ordered_json`, JSON Pointer/Patch/Merge-Patch, `NLOHMANN_DEFINE_TYPE_*` macros |
| simdjson | v4.6.11 (2026-09-05) | Apache-2.0 | C++17 (On-Demand) | very active | Parse-only, extremely fast; no writer |
| RapidJSON | v1.1.0 (2016-08-25) | MIT (+JSON license bits) | C++03/11 | last commit 2025-02-05, no release since 2016 | SAX/DOM; mostly legacy |
| glaze | v8.3.0 (2026-08-29) | MIT (+embedded-forms exception) | **C++23** | very active | Compile-time reflection of aggregates; Clang 18+, GCC 13+, MSVC 14.50 with `/Zc:preprocessor`, "latest Apple Clang"; optional P2996 mode on GCC 16 `-freflection` / Bloomberg clang fork |
| toml++ | v3.4.0 (2023-10-13) | MIT | C++17 | commits 2026-07-21, no release in ~3 years | Best TOML impl; good for user settings |
| yaml-cpp | 0.9.0 (2026-02-04) | MIT | C++11 | active | YAML is a poor fit for machine-written project files |
| cereal | v1.3.2 (2022-02-28) | BSD-3 | C++11 | commits 2026-03-11, releases stalled | Binary/JSON/XML archives via `serialize()` members |

- Source: each repository's README/releases (GitHub API for dates)
- URL: https://github.com/nlohmann/json ; https://github.com/simdjson/simdjson ;
  https://github.com/Tencent/rapidjson ; https://github.com/stephenberry/glaze ;
  https://github.com/marzer/tomlplusplus ; https://github.com/jbeder/yaml-cpp ;
  https://github.com/USCiLab/cereal
- Accessed: 2026-09-08
- Learned: as tabulated. glaze README: "Requires C++23"; CI on Clang 18+, GCC 13+, MSVC Build Tools
  14.50 (VS 2026); "maintain compatibility with … the latest version of MSVC and Apple Clang (Xcode)".
- Relevance: Project files are human-readable, hand-editable, diff-friendly JSON; parse speed is
  irrelevant at KB–MB sizes. glaze's reflection removes boilerplate but its "latest Apple Clang only"
  policy and C++23 hard requirement are riskier for contributors on Linux distros with GCC 13/14.
- Confidence: High for versions; Medium for glaze compiler support nuance (README wording).

### Project-file format design (decision)

1. Top-level envelope: `{"format": "av-gen-project", "version": 1, "engine": "<semver>", "data": {...}}`.
   `version` is an integer schema version, bumped only on incompatible change; additive fields do not
   bump it.
2. Load path: parse → read `version` → run ordered migration functions `v1→v2`, `v2→v3` … on the raw
   JSON DOM (JSON Patch / manual edits) → deserialize the current struct. Migrations are pure
   functions on `nlohmann::json` and are unit-tested with golden fixtures (`tests/fixtures/project_v1.json`).
3. Unknown fields are preserved on round-trip (keep the raw `json` for extension blocks) so a newer
   file opened in an older build does not silently lose data; refuse to save over a newer version
   unless the user confirms.
4. Stable IDs (UUID/ULID) for nodes and parameters instead of array indices, so patches and undo
   history survive reordering.
5. Use `ordered_json` when writing so diffs are stable; pretty-print with 2 spaces; write to a temp
   file and atomically rename.

Recommendation: **nlohmann/json v3.12.0** now; optionally add **glaze** later for high-throughput
paths (e.g. OSC/IPC message parsing) once the compiler floor is confirmed C++23 on all CI images.
Use **toml++** only if a separate human-edited `settings.toml` is desired; otherwise JSON everywhere.

---

## 8. Reflection / ECS for parameters

- Source: EnTT v4.0.0 release notes and README (GitHub API)
- URL: https://github.com/skypjack/entt/releases/tag/v4.0.0
- Accessed: 2026-09-08
- Learned: MIT. v4.0.0 published 2026-07-23; README: "supports at least C++20". v4 `meta` changes:
  `name()` returns `std::string_view`, new `meta_base`, `meta_data::set_arity/get_arity`, multi
  meta-type per C++ type, `meta_type::id` renamed `alias`, custom getters/setters with extra args.
  Also removes several `core` helpers and adds an `stl` injection submodule.
- Relevance: `entt::meta` is a workable runtime reflection layer for parameters (name, type, range
  as `prop`), but v4 just landed with API churn; adopting it in 0.1 means tracking a fresh major.
- Confidence: High.

- flecs v4.1.6 (2026-06-29), MIT; C99 core with C++ API, built-in reflection (`ecs_meta`) and REST
  explorer. Heavier runtime than EnTT; excellent tooling. (Source:
  https://github.com/SanderMertens/flecs, LICENSE verified, accessed 2026-09-08, high.)
- RTTR v0.9.6 (2018-03-26), MIT; last commit 2021-08-10 — unmaintained. (Source:
  https://github.com/rttrorg/rttr, accessed 2026-09-08, high.)
- refl-cpp v0.12.4 (2023-03-10), MIT; last commit 2022-11-05 — frozen, header-only, macro-based
  compile-time reflection; still usable but no support. (Source: https://github.com/veselink1/refl-cpp,
  accessed 2026-09-08, high.)

### C++26 static reflection (P2996) status

- Source: Clang C++ status page; GCC C++ status page; local compiler probe; Bloomberg fork
- URL: https://clang.llvm.org/cxx_status.html ; https://gcc.gnu.org/projects/cxx-status.html ;
  https://github.com/bloomberg/clang-p2996
- Accessed: 2026-09-08
- Learned: P2996 was adopted into C++26 (Sofia, June 2025). Clang's status page lists "Reflection
  P2996R13: No" (mainline, up to Clang 24 entries). GCC: reflection available in **GCC 16** behind
  `-std=c++26 -freflection` (implements P2996R13 with a few `apply_*` traits missing). Bloomberg's
  `clang-p2996` fork remains the most complete Clang implementation; EDG has one. Locally, Apple
  clang 21 accepts `-std=c++26` but `__cpp_impl_reflection` is **undefined** (verified with
  `clang++ -std=c++23/-std=c++26 -E`).
- Relevance: Not usable for this project's compiler matrix (Apple clang 21, MSVC, GCC 14). Do not
  design around it; keep parameter metadata explicit so it can be generated by reflection later.
- Confidence: High (multiple official sources + local probe).

### Recommendation for milestone 0.1

Do **not** adopt an ECS or a reflection library in 0.1. Use a hand-written `ParamDesc` table
(`{id, name, type, min, max, default, flags}`) generated by a small macro or a `constexpr` array per
module; JSON (de)serialization and ImGui widgets are driven from that table. This is ~200 lines,
has no learning curve for contributors, and maps 1:1 onto whatever reflection mechanism (EnTT meta,
glaze, P2996) is adopted in a later milestone. Revisit EnTT v4 `meta`/registry when the node graph
needs component-style composition (likely milestone 0.3+).

---

## 9. Miscellaneous infrastructure

### 9.1 File watching (shader hot reload)

- Source: efsw README/LICENSE; dmon.h header; Apple FSEvents/kqueue docs (via search)
- URL: https://github.com/SpartanJ/efsw ; https://github.com/septag/dmon ;
  https://developer.apple.com/library/archive/documentation/Darwin/Conceptual/FSEvents_ProgGuide/KernelQueues/KernelQueues.html
- Accessed: 2026-09-08
- Learned: efsw: MIT, latest tag 1.7.2, commits 2026-08-29; backends inotify (Linux), FSEvents or
  kqueue (macOS), kqueue (BSD), ReadDirectoryChangesW (Windows), generic polling fallback; ships a
  CMakeLists (author: "I don't officially support [it] but it works"); FSEvents/Windows backends
  can't follow symlinks. dmon.h: BSD-2, single header, backends inotify/FSEvents/RDCW, v1.3.9 moved
  to `FSEventStreamSetDispatchQueue`, last commit 2026-02-23. Apple: FSEvents is directory-level and
  coalesces events with latency; kqueue needs one fd per watched file and is best for a handful of
  files.
- Relevance: Shader/preset hot reload watches a directory tree → FSEvents (via efsw or dmon) is
  correct; debounce (~50–100 ms) and re-read on the render thread's next frame. `std::filesystem`
  has no notification API; polling `last_write_time` every 250 ms is an acceptable zero-dependency
  fallback for CI.
- Confidence: High.

### 9.2 Tracy profiler

- Source: Tracy repo, NEWS file, `TracyMetal.hmm`, LICENSE
- URL: https://github.com/wolfpld/tracy ; https://raw.githubusercontent.com/wolfpld/tracy/master/NEWS
- Accessed: 2026-09-08
- Learned: BSD-3 (LICENSE file, "Copyright (c) 2017-2026"). v0.14.1 published 2026-08-22 (0.14.0
  2026-08-09, 0.13.x late 2025, 0.12.0 2025-05-30). NEWS 0.12.0: "GPU profiling is now available with
  Metal and CUDA"; "Tracing on Arm macOS will now have more precise timer readings"; 0.14.0: "Binary
  releases are now also provided for macOS (needs manual quarantine handling)"; 0.11.0 moved the
  server build to CMake. Client is a CMake target (`Tracy::TracyClient`, `TRACY_ENABLE` option).
  `public/tracy/TracyMetal.hmm` exists; header notes: tested on Apple Silicon; **zones are per command
  *encoder*, not per command** (hardware timestamp granularity); 4096-query buffers, double-buffered;
  call `TracyMetalCollect()` frequently; `#error TracyMetal requires ARC to be enabled`.
- Relevance: CPU zones on audio/render threads plus Metal encoder-level GPU zones cover our needs.
  The `.hmm` file must be included from an Objective-C++ TU compiled with `-fobjc-arc`.
- Confidence: High.

### 9.3 Sanitizers

- Source: Clang ThreadSanitizer docs (and AddressSanitizer/UBSan docs by extension)
- URL: https://clang.llvm.org/docs/ThreadSanitizer.html
- Accessed: 2026-09-08
- Learned: TSan supported platforms include "Darwin arm64, x86_64"; 64-bit only; ~5×–15× slowdown;
  all code should be built with `-fsanitize=thread`. ASan/UBSan are likewise supported on Apple
  Silicon with Apple clang.
- Relevance: Presets `asan-ubsan` (`-fsanitize=address,undefined -fno-omit-frame-pointer`) and `tsan`
  (separate build dir; cannot combine with ASan). TSan is the key tool for audio↔UI lock-free
  hand-offs. Note: sanitizer runtimes are dylibs from the Xcode toolchain; app bundles run fine
  locally but sanitized builds are not for distribution.
- Confidence: High.

### 9.4 Compiler cache, formatters, linters, compile database

- Source: ccache manual; CMake `<LANG>_CLANG_TIDY` docs
- URL: https://ccache.dev/manual/latest.html ; https://cmake.org/cmake/help/latest/prop_tgt/LANG_CLANG_TIDY.html
- Accessed: 2026-09-08
- Learned: ccache 4.14 (release 2026-08-23), GPL-3.0-or-later (a build tool; not linked into the
  product). Supports clang, GCC, MSVC. Recommended CMake use: `CMAKE_CXX_COMPILER_LAUNCHER=ccache`;
  for shared caches across build dirs set `base_dir` or `-fdebug-prefix-map`; with PCH set
  `sloppiness = pch_defines,time_macros`. CMake's `CMAKE_CXX_CLANG_TIDY` runs clang-tidy alongside
  compilation on Makefile/Ninja generators for C/CXX/OBJC/OBJCXX; `-p` mode (3.25+) uses the compile
  database; `SKIP_LINTING` source property (3.27+) opts files out.
- Relevance: ccache as an opt-in preset (`-DCMAKE_CXX_COMPILER_LAUNCHER=ccache`), never required.
  `.clang-format` (based on `LLVM`, 100 cols, `PointerAlignment: Left`) and `.clang-tidy` committed;
  clang-tidy run in CI as a separate job (not inline, to keep local builds fast). Always set
  `CMAKE_EXPORT_COMPILE_COMMANDS=ON` in presets for clangd/IDE.
- Confidence: High.

### 9.5 CMake presets

- Source: `cmake-presets(7)` manual
- URL: https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html
- Accessed: 2026-09-08
- Learned: Schema versions: 1 (3.19) … 8 (3.28), 9 (3.30), 10 (3.31), 11 (4.3), 12 (4.4).
  `CMakeUserPresets.json` is local (not committed) and implicitly includes `CMakePresets.json`.
  `inherits`, `condition` (`anyOf`/`allOf`, host OS checks), configure/build/test/package/workflow
  presets.
- Relevance: Commit `CMakePresets.json` with `"version": 8` (works on CMake 3.28+ and our 4.0.1):
  base preset (Ninja Multi-Config or single-config Ninja, `CMAKE_EXPORT_COMPILE_COMMANDS`,
  `CPM_SOURCE_CACHE`), then `debug`, `release`, `relwithdebinfo`, `asan-ubsan`, `tsan`, `xcode`
  (Xcode generator for Metal debugging/Instruments), and OS-conditioned `windows-msvc`,
  `linux-gcc`, `linux-clang`. Workflow presets chain configure→build→test for CI.
- Confidence: High.

### 9.6 C++ standard choice: C++20 vs C++23

Local probe (Apple clang 21, `-std=c++23`, `<version>` feature-test macros, run 2026-09-08):

| Feature | Macro | Apple clang 21 / libc++ 21.1 |
|---|---|---|
| `std::print` / `println` | `__cpp_lib_print` | 202207 ✔ |
| `std::format` + ranges formatting | `__cpp_lib_format`, `__cpp_lib_format_ranges` | 202110 / 202207 ✔ |
| `std::expected` | `__cpp_lib_expected` | 202211 ✔ |
| `std::mdspan` | `__cpp_lib_mdspan` | 202207 ✔ |
| `std::flat_map` | `__cpp_lib_flat_map` | 202207 ✔ |
| `ranges::to` | `__cpp_lib_ranges_to_container` | 202202 ✔ |
| Deducing `this` | `__cpp_explicit_this_parameter` | 202110 ✔ |
| `if consteval`, multidim `[]`, static `operator()` | — | ✔ (202106 / 202211 / 202207) |
| `std::generator` | `__cpp_lib_generator` | ✘ undefined |
| `std::stacktrace` | `__cpp_lib_stacktrace` | ✘ undefined |
| `std::move_only_function` | `__cpp_lib_move_only_function` | ✘ undefined |
| `std::spanstream` | `__cpp_lib_spanstream` | ✘ undefined |
| Static reflection | `__cpp_impl_reflection` | ✘ undefined (even with `-std=c++26`) |

- Source: local toolchain probe (commands recorded in this research session); libc++ C++23 status
  page; MSVC conformance page; MSVC STL changelog; GCC status page; CMake cxxmodules manual
- URL: https://libcxx.llvm.org/Status/Cxx23.html ;
  https://learn.microsoft.com/en-us/cpp/overview/visual-cpp-language-conformance ;
  https://github.com/microsoft/STL/wiki/Changelog ; https://gcc.gnu.org/projects/cxx-status.html ;
  https://cmake.org/cmake/help/latest/manual/cmake-cxxmodules.7.html
- Accessed: 2026-09-08
- Learned: libc++ status page agrees with the probe (print v18, expected v16, mdspan v18, flat_map
  v20–21, ranges::to v17; stacktrace "in progress"; no generator/move_only_function). MSVC core
  C++23: deducing this VS 2022 17.13, multidim subscript 17.12, `if consteval` and static
  `operator()` 17.14, `auto(x)` Build Tools 14.50 (VS 2026); STL: "C++23 feature complete" as of
  Build Tools 14.52 Preview (14.51 = VS 2026 18.6 current). GCC: C++23 core mostly complete in GCC
  13–14 (deducing this, multidim subscript in 13); C++20 becomes the default in GCC 16;
  libstdc++ `std::print` since GCC 14, `mdspan`/`flat_map` in GCC 15. C++20 modules: CMake 3.28+,
  Ninja/VS generators only, Clang 16+/GCC 14+/MSVC 14.34+; Apple Clang is not listed; `import std`
  only Clang 18.1.2+/GCC 15+/MSVC 14.36+ with Ninja.
- Relevance: C++23 is usable on all three toolchains **if** we restrict ourselves to the intersection.
- Confidence: High for Apple clang (local probe) and MSVC/libc++ (official pages); Medium for exact
  GCC 14 libstdc++ coverage of `flat_map`/`mdspan` (status page summary, not the libstdc++ table).

Recommendation: **`CMAKE_CXX_STANDARD 23`, `CMAKE_CXX_EXTENSIONS OFF`**, with a documented allow-list:
allowed — deducing this, `std::expected`, `std::print`, `std::format`, `std::mdspan`, `ranges::to`,
`std::flat_map`, `if consteval`, `std::to_underlying`, `std::unreachable`, `std::byteswap`;
forbidden until the floor moves — `std::generator`, `std::stacktrace`, `std::move_only_function`,
`std::spanstream`, C++20 modules, `import std`, extended floating-point types, static reflection.
Compiler floor: Apple clang ≥ 17 (Xcode 26), Clang ≥ 18 with libc++ 18, GCC ≥ 14, MSVC ≥ 19.44
(VS 2022 17.14) with `/Zc:preprocessor`. If GCC-14 Linux CI proves painful for `flat_map`, drop
that single feature rather than the standard.

### 9.7 Apple-specific

- Source: CMake `MACOSX_BUNDLE` property; Apple Hardened Runtime docs; Apple entitlement and
  usage-description docs (via search summaries); LLVM lld review D97994; Apple developer forum threads
- URL: https://cmake.org/cmake/help/latest/prop_tgt/MACOSX_BUNDLE.html ;
  https://developer.apple.com/documentation/security/hardened-runtime ;
  https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.security.device.audio-input ;
  https://developer.apple.com/documentation/BundleResources/Information-Property-List/NSMicrophoneUsageDescription ;
  https://reviews.llvm.org/D97994 ; https://developer.apple.com/forums/thread/741303
- Accessed: 2026-09-08
- Learned:
  - Objective-C++: enable `OBJCXX` (CMake 3.16+); `.mm` TUs get `-fobjc-arc`
    (`target_compile_options(... $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)`). ARC is required by
    TracyMetal and is the sane default; pass Metal objects to C++ as opaque `void*` (bridged) or use
    metal-cpp for a C++-only surface (ImGui 1.92.9 supports metal-cpp via `IMGUI_IMPL_METAL_CPP`).
  - App bundle vs plain executable: `set_target_properties(app PROPERTIES MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/cmake/Info.plist.in)` produces `app.app` and works with
    Ninja and Xcode generators. A bundle is needed for: Info.plist keys (`NSMicrophoneUsageDescription`,
    `NSHighResolutionCapable`), a Dock icon/name, and TCC permission attribution to *our* app rather
    than the terminal. Plain executables are fine for tests/CLI tools.
  - Code signing for local runs: on Apple Silicon "all code must be at least ad-hoc signed"; the
    linker (ld64/lld) ad-hoc signs by default; after post-link edits (e.g. `install_name_tool`,
    resource copies) re-sign with `codesign -s - --force --deep app.app`. No Developer ID needed for
    local runs. Gatekeeper/notarization only matter for distribution.
  - Hardened Runtime (`codesign --options runtime`) is mandatory for notarization. Under Hardened
    Runtime the microphone additionally requires the `com.apple.security.device.audio-input`
    entitlement in the signature; with only `NSMicrophoneUsageDescription`, TCC denies silently and
    never shows the prompt (multiple developer-forum/issue reports agree).
  - Entitlement/plist plan for later audio-input milestone: `NSMicrophoneUsageDescription` in
    Info.plist now (harmless), `com.apple.security.device.audio-input = true` in an
    `entitlements.plist` applied with `codesign --entitlements` (Ninja) or
    `XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS` (Xcode generator). Not sandboxed in 0.1.
- Relevance: Determines the CMake target layout (`av_engine` static lib, `av_app` bundle, `av_tests`
  plain executables) and the local dev-loop (ad-hoc sign, no Apple ID).
- Confidence: High for CMake/linker facts; Medium for the exact TCC behavior (consistent across
  Apple forum threads and third-party issue trackers, but the Apple doc pages themselves did not
  render for verification).

---

## 10. Open questions / follow-ups

1. Renderer choice (raw Metal vs SDL_GPU vs sokol/bgfx/wgpu) is decided in the graphics research
   document; this document only confirms each has an ImGui backend and Tracy support (Metal, Vulkan,
   D3D, WebGPU headers all exist in `public/tracy/`).
2. Whether to enable ImGui multi-viewports (docking branch feature) on macOS with SDL3 — needs a
   spike; not required for 0.1.
3. GCC 14 libstdc++ coverage for `std::flat_map` should be confirmed on the actual Linux CI image
   before it enters the allow-list.
4. vcpkg fallback: write the `vcpkg.json` manifest only when a dependency without a usable
   CMakeLists appears.

---

## Sources

All accessed 2026-09-08.

- CMake 4.0 release notes — https://cmake.org/cmake/help/latest/release/4.0.html
- CMake release index — https://cmake.org/cmake/help/latest/release/index.html
- Kitware/CMake releases — https://github.com/kitware/cmake/releases
- CMake FetchContent module — https://cmake.org/cmake/help/latest/module/FetchContent.html
- CMake presets manual — https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html
- CMake C++ modules manual — https://cmake.org/cmake/help/latest/manual/cmake-cxxmodules.7.html
- CMake enable_language — https://cmake.org/cmake/help/latest/command/enable_language.html
- CMake MACOSX_BUNDLE — https://cmake.org/cmake/help/latest/prop_tgt/MACOSX_BUNDLE.html
- CMake `<LANG>_CLANG_TIDY` — https://cmake.org/cmake/help/latest/prop_tgt/LANG_CLANG_TIDY.html
- Fedora CMake 4.0 change page — https://fedoraproject.org/wiki/Changes/CMake4.0
- CPM.cmake — https://github.com/cpm-cmake/CPM.cmake ; releases — https://github.com/cpm-cmake/CPM.cmake/releases
- vcpkg manifest mode — https://learn.microsoft.com/en-us/vcpkg/concepts/manifest-mode
- vcpkg binary caching — https://learn.microsoft.com/en-us/vcpkg/consume/binary-caching-overview
- vcpkg triplets — https://learn.microsoft.com/en-us/vcpkg/concepts/triplets ; https://learn.microsoft.com/en-us/vcpkg/users/triplets
- vcpkg repo (triplets directory) — https://github.com/microsoft/vcpkg
- Conan 2 introduction — https://docs.conan.io/2/introduction.html ; PyPI — https://pypi.org/project/conan/ ; releases — https://github.com/conan-io/conan/releases
- Dear ImGui — https://github.com/ocornut/imgui ; backends — https://github.com/ocornut/imgui/tree/master/backends ; changelog — https://raw.githubusercontent.com/ocornut/imgui/master/docs/CHANGELOG.txt ; releases — https://github.com/ocornut/imgui/releases
- ImPlot — https://github.com/epezent/implot ; releases — https://github.com/epezent/implot/releases
- bgfx ImGui example — https://github.com/bkaradzic/bgfx/tree/master/examples/common/imgui
- sokol_imgui.h — https://github.com/floooh/sokol/blob/master/util/sokol_imgui.h
- wgpu-native — https://github.com/gfx-rs/wgpu-native
- Nuklear — https://github.com/Immediate-Mode-UI/Nuklear
- RmlUi — https://github.com/mikke89/RmlUi
- Qt releases / licensing — https://doc.qt.io/qt-6/qt-releases.html ; https://www.qt.io/blog/qt-6.10.1-released
- GLFW releases — https://github.com/glfw/glfw/releases ; news — https://www.glfw.org/docs/latest/news.html ; native access — https://www.glfw.org/docs/latest/group__native.html
- SDL releases — https://github.com/libsdl-org/SDL/releases ; SDL_GPU — https://wiki.libsdl.org/SDL3/CategoryGPU ; SDL_Metal_CreateView — https://wiki.libsdl.org/SDL3/SDL_Metal_CreateView ; Audio — https://wiki.libsdl.org/SDL3/CategoryAudio
- Catch2 — https://github.com/catchorg/Catch2 ; CMake integration — https://github.com/catchorg/Catch2/blob/devel/docs/cmake-integration.md
- GoogleTest — https://github.com/google/googletest
- doctest — https://github.com/doctest/doctest ; releases — https://github.com/doctest/doctest/releases
- ApprovalTests.cpp — https://github.com/approvals/ApprovalTests.cpp
- Google Benchmark — https://github.com/google/benchmark
- nanobench — https://github.com/martinus/nanobench
- spdlog — https://github.com/gabime/spdlog ; releases — https://github.com/gabime/spdlog/releases
- fmt — https://github.com/fmtlib/fmt ; releases — https://github.com/fmtlib/fmt/releases
- quill — https://github.com/odygrd/quill
- glog (archived) — https://github.com/google/glog
- GLM — https://github.com/g-truc/glm ; manual — https://github.com/g-truc/glm/blob/master/manual.md
- Eigen releases — https://gitlab.com/libeigen/eigen/-/releases
- DirectXMath — https://github.com/microsoft/DirectXMath
- HandmadeMath — https://github.com/HandmadeMath/HandmadeMath
- linalg.h — https://github.com/sgorsten/linalg
- nlohmann/json — https://github.com/nlohmann/json
- simdjson — https://github.com/simdjson/simdjson
- RapidJSON — https://github.com/Tencent/rapidjson
- glaze — https://github.com/stephenberry/glaze ; P2996 docs — https://stephenberry.github.io/glaze/p2996-reflection/
- toml++ — https://github.com/marzer/tomlplusplus
- yaml-cpp — https://github.com/jbeder/yaml-cpp
- cereal — https://github.com/USCiLab/cereal
- EnTT — https://github.com/skypjack/entt ; v4.0.0 — https://github.com/skypjack/entt/releases/tag/v4.0.0
- flecs — https://github.com/SanderMertens/flecs
- RTTR — https://github.com/rttrorg/rttr
- refl-cpp — https://github.com/veselink1/refl-cpp
- Clang C++ status — https://clang.llvm.org/cxx_status.html
- GCC C++ status — https://gcc.gnu.org/projects/cxx-status.html
- libc++ C++23 status — https://libcxx.llvm.org/Status/Cxx23.html
- MSVC language conformance — https://learn.microsoft.com/en-us/cpp/overview/visual-cpp-language-conformance
- MSVC STL changelog — https://github.com/microsoft/STL/wiki/Changelog
- Bloomberg clang-p2996 — https://github.com/bloomberg/clang-p2996
- efsw — https://github.com/SpartanJ/efsw
- dmon — https://github.com/septag/dmon
- Apple kqueue vs FSEvents guide — https://developer.apple.com/library/archive/documentation/Darwin/Conceptual/FSEvents_ProgGuide/KernelQueues/KernelQueues.html
- Tracy — https://github.com/wolfpld/tracy ; NEWS — https://raw.githubusercontent.com/wolfpld/tracy/master/NEWS
- Clang ThreadSanitizer — https://clang.llvm.org/docs/ThreadSanitizer.html
- ccache manual — https://ccache.dev/manual/latest.html ; repo — https://github.com/ccache/ccache
- Xcode 26.6 release — https://developer.apple.com/news/releases/?id=06252026a
- Apple Hardened Runtime — https://developer.apple.com/documentation/security/hardened-runtime
- Apple audio-input entitlement — https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.security.device.audio-input
- Apple NSMicrophoneUsageDescription — https://developer.apple.com/documentation/BundleResources/Information-Property-List/NSMicrophoneUsageDescription
- Apple forum: mic prompt under Hardened Runtime — https://developer.apple.com/forums/thread/741303
- LLVM lld ad-hoc signing on arm64 — https://reviews.llvm.org/D97994
