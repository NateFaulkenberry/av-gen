# ADR-2160: a package that needs no source tree: `AVGEN_DEV_SOURCE_PATHS`, install rules, and the documents an outside client reads

- Status: Accepted (2026-11-25), eng/engine-api-s7 (Engine API stage S7; 113-engine-architecture, stage plan in
  03-phase0-api-contracts/04-poc-plan.md S7 and section 3; owner spec 04-implementation/00-spec.md section 4, S7)
- Code: `CMakeLists.txt` (the option), `src/CMakeLists.txt` (the two compile definitions guarded),
  `cmake/Install.cmake` (the layout, VERSION.json, THIRD-PARTY.md), `cmake/third-party-notices.md`,
  `src/labs/case.cpp` and `src/app/application.cpp` (the two unguarded `AVGEN_SOURCE_DIR` uses),
  `src/app/main.cpp` (`serve`/`call` name the running executable, not `argv[0]`), `src/api/jsonrpc_server.cpp`
  (the protocol example's arguments), `tools/package_poc.sh`
- Docs: `docs/api/README.md` (installed as `share/avgen/docs/README.md`), `docs/api/gltf-export.md`,
  `docs/api/protocol.md` (regenerated)
- Tests: `tests/unit/test_install_layout.cpp` (`[api][package]`); `tools/package_poc.sh` checks the staged files

## Context

The PoC's last stage (S8) must drive the engine from a folder outside every checkout, with no `AVGEN_SOURCE_DIR`
and no repository path, by agents that have read only the package's documents. A build baked the checkout into
the binary twice (`AVGEN_SOURCE_DIR`, `AVGEN_SHADER_SOURCE_DIR`), two uses of the first were not guarded at all,
there were no `install()` rules, and the documents assumed a reader with the source: the glTF contract existed
only as the loader's behaviour.

## Decision

1. **`AVGEN_DEV_SOURCE_PATHS`, default ON.** OFF defines neither path, and needs `AVGEN_BUILD_TESTS=OFF` (the
   suites read `AVGEN_SOURCE_DIR` in hundreds of places; configure fails otherwise). Every use is now under
   `#ifdef` (the grep in "Verification"); a package finds its shaders at `<exe>/shaders` or `AVGEN_SHADER_DIR`.
   Dev builds are unchanged. The other source-path definitions (`AVGEN_*_FIXTURE_DIR`, `AVGEN_AUDIO_LAB_REPLAY`,
   the new `AVGEN_BINARY_DIR`) are on test targets only.
2. **`serve` and `call` name the running executable** (`_NSGetExecutablePath`, canonical) for the shader search,
   not `argv[0]`: started through `PATH`, `argv[0]` is a bare `avgen` and `<exe>/shaders` became `./shaders` of the
   working directory.
3. **The install layout** (`cmake/Install.cmake`): `bin/avgen`; `bin/shaders/**.wgsl` without
   `shaders/examples/` (the example projects' sample layers); `share/avgen/schemas/*.json`;
   `share/avgen/docs/README.md` and `share/avgen/docs/api/*.md`; `VERSION.json`, written at install time by running
   the installed binary's `--describe version` from `/`; `THIRD-PARTY.md`: CREDITS.md verbatim, then the licence
   file of every statically linked library read from its fetched sources at configure time, then
   `cmake/third-party-notices.md` for the three whose fetched files carry none (Dawn/Tint's prebuilt archive, the
   abseil inside it, fastgltf's bundled simdjson). A package build fails on a missing licence file; a dev build
   warns. Nothing from `assets/`, `examples/`, `docs/help`, labs or prototypes; no editor.
4. **`tools/package_poc.sh --out <dir>`** builds `avgen` alone in `build/package-poc` with
   `-DAVGEN_DEV_SOURCE_PATHS=OFF -DAVGEN_BUILD_TESTS=OFF -DAVGEN_BUNDLE_ID=com.avgen.avgen.poc
   -DCMAKE_BUILD_TYPE=Release -DAVGEN_PATHTRACE_DENOISE=OFF`, refuses an `--out` inside any git checkout, installs
   into `<out>/avgen-poc-<rev>/`, and fails on: a compile command still defining a source path; a staged file
   outside the layout, an asset-like path or extension, a symlink, a missing required file or a shader count that
   differs from the source's; `strings` hits for the source root, the checkout's parent, `$HOME`, the CPM cache or
   any `/Users/` path except the Dawn prebuilt's own CI paths (`/Users/runner/work/dawn/`, compiled into Google's
   archive); an `otool -L` entry outside `/System` and `/usr/lib`; `--describe version` from `/` with a scrubbed
   environment differing from VERSION.json. Then the tarball and its SHA-256.
5. **The package's entry point is `docs/api/README.md`**, written for a reader with no source access: starting
   `serve` and `call`, roots and paths, the envelope and errors, transactions, projects and scenes, asset ids,
   stems, signals and routes, render jobs and hashes, the GPU lock and the environment it reads, and a transcript
   recorded from this package under the sandbox. Its links are relative to the installed location.
6. **The glTF export contract is written down** (`docs/api/gltf-export.md`) from `assets/gltf_loader.cpp` and the
   parser's extension list, including what each unsupported feature does to an import.

## Rationale

A package is what S8's clients will have, so the checks are on the package: the allow-list on the staged files,
`strings` on the binary, and a sandbox that denies the whole GitHub folder while the package runs. The test in the
CPU suite reads the install rules CMake generated (every `cmake_install.cmake` reachable from the top), so a
future `install(DIRECTORY assets/ ...)` anywhere fails in the suite, before anyone packages.

## Consequences

- A package build cannot run the test suites (by design; the suites stay on the dev option).
- Features that read the source tree are absent in a package: the examples menu, help, labs, Studio's `assets/`
  read root. `serve` and `call` use none of them.
- `THIRD-PARTY.md`'s three stated notices were checked against the projects' published licences when written;
  they must be re-checked when Dawn, abseil or simdjson move.

## Rejected alternatives

- **Strip the paths at link time or with `-ffile-prefix-map`.** The paths are string literals the code uses, not
  debug info; mapping them would leave the code reading a wrong path.
- **A runtime switch instead of a build option.** A baked path that is "not used" still names the machine and is
  one `getenv` away from being used; the owner's requirement is no hidden fallback.
- **Package `shaders/examples/`.** It is example content, not engine library; the allow-list treats `examples/`
  as asset-like everywhere.

## Verification

See the S7 hand-back and the proof logs beside the package (113-engine-architecture/04-implementation/package/proofs/): the grep of every source-path use,
the `strings` and `otool` results, the sandbox runs (CPU `avgen call`, GPU `render.capture` through `serve`), the
log grep, the absence of a GPU device in the CPU run, and the cold-cache compile time.

## Revisit triggers

- A signed app bundle or notarised package (the evaluation's Phase 6).
- A packaged editor, which would need examples, help and a read root for its assets.
- Another platform: `otool`, `strings` and the Metal cache are macOS's.
