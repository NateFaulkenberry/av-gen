# Build

## Requirements

- macOS 26 on Apple silicon for the default configuration (the prebuilt Dawn archive is arm64
  with a 26.0 deployment floor). Intel or older macOS: see "Dawn from source".
- Xcode 26 command-line tools (Apple clang 21), CMake 3.28 or newer (4.0.1 tested), Ninja.
- Network access for the first configure (dependencies are downloaded into `.cache/cpm`).

## Presets

| Preset | Build type | Extras |
|---|---|---|
| `debug` | Debug | |
| `release` | Release | |
| `relwithdebinfo` | RelWithDebInfo | |
| `asan` | Debug | AddressSanitizer + UndefinedBehaviorSanitizer on engine targets |

```sh
cmake --preset debug          # configure into build/debug
cmake --build --preset debug  # build everything
ctest --preset debug          # run tests (see docs/testing.md)
```

Binaries: `build/<preset>/src/avgen`, `build/<preset>/tests/avgen_tests`,
`build/<preset>/tests/avgen_render_tests`. `compile_commands.json` is exported in each build dir.

## Options

| CMake option | Default | Meaning |
|---|---|---|
| `AVGEN_BUILD_TESTS` | ON | build Catch2 test targets |
| `AVGEN_DAWN_FROM_SOURCE` | OFF | build Dawn from the pinned commit instead of the prebuilt archive |
| `AVGEN_ENABLE_ASAN` | OFF | sanitizers (set by the `asan` preset) |
| `AVGEN_WARNINGS_AS_ERRORS` | OFF | `-Werror` on engine targets |
| `CPM_SOURCE_CACHE` | `<repo>/.cache/cpm` | where dependency sources are cached |

## Dependencies (ADR-008)

All third-party code is fetched by `cmake/Dependencies.cmake` through CPM.cmake, pinned to a
tag or commit; the Dawn archive is pinned by URL and SHA256. A second configure is offline.
`docs/dependencies.md` lists each library with licence and reason.

## Dawn

Default: the nightly release `v20260907.201642` prebuilt macOS arm64 Release archive (static
`libwebgpu_dawn.a`, headers, CMake package config). The same commit is used for the from-source
path:

```sh
cmake --preset debug -DAVGEN_DAWN_FROM_SOURCE=ON
```

From-source needs Python 3 (Dawn's `DAWN_FETCH_DEPENDENCIES` script) and takes considerably
longer on the first build. It is the route for Intel Macs, macOS < 26, a Debug Dawn, and Tint's
SPIR-V reader (`TINT_BUILD_SPV_READER`). The option names passed are taken from Dawn's CMake
quickstart; this path is configured but had not been exercised at the time of milestone 0.1
(see the development log).

## Shaders at runtime

`shaders/*.wgsl` are loaded at runtime. The search order is `$AVGEN_SHADER_DIR`, `<exe>/shaders`,
`<exe>/../shaders`, `<exe>/../Resources/shaders`, then the source tree path compiled into the
binary (`AVGEN_SHADER_SOURCE_DIR`). Running from the build tree therefore needs no copying.

## Platform notes

- Windows/Linux: architecturally supported (Dawn D3D12/Vulkan, SDL3). Not built yet. Expected
  work: the Dawn from-source path (or the corresponding prebuilt archive), a `WGPUSurfaceSource*`
  branch in `gpu::Context::create` fed from SDL's native window handle, and MSVC `/std:c++latest`.
- App bundle, code signing and the microphone entitlement are not needed for 0.1 (no live input).
- Multiple windows (milestone 1.2 outputs): every `platform::Window` shares one SDL video
  subsystem (`SDL_InitSubSystem`, reference counted; `Window::displays()` initialises it
  transiently, so it works before any window exists and returns an empty list on a headless
  machine). The process-wide SDL queue is pumped once per frame by `Window::pumpEvents`, which
  routes window events by `SDL_WindowID`. Output windows are created with
  `SDL_CreateWindowWithProperties` (position centred on the chosen display, borderless,
  always-on-top, fullscreen) and fullscreen means SDL's desktop mode (no exclusive display mode)
  with `SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES` off, so an output per display does not start a
  Spaces transition or hide the other windows. Each output gets its own `gpu::Surface` on the
  one Dawn device; Metal allows any number of `CAMetalLayer` swapchains per device.
