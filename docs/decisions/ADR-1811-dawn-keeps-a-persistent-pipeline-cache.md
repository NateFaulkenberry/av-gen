# ADR-1811: Dawn keeps a persistent pipeline cache

- Status: Accepted (eng/render-improvements, 2026-10-28). Render audit recommendation P2 (gap G5), approved by the
  owner.
- Code: `src/gpu/blob_cache.{hpp,cpp}` (`BlobCache`), `src/gpu/context.{hpp,cpp}` (`ContextDesc::pipelineCacheDir`,
  the `wgpu::DawnCacheDeviceDescriptor` chained into device creation), `src/platform/window.{hpp,cpp}`
  (`cacheDirectory`), `src/app/application.cpp` (`AVGEN_PIPELINE_CACHE`), `src/CMakeLists.txt` (`AVGEN_DAWN_COMMIT`).
- Tests: `tests/unit/test_blob_cache.cpp` (`[adr1811]`).

## Problem

The render audit measured 16-26 s of shader warm-up on the first run of a new build (Rift 26.0 s, Aurora 23.8 s,
Rainbow World 16.5 s) against 0.3-0.6 s on the next runs, and 7.6 s on every run of Rainbow World. AV Gen gave Dawn no
persistent cache, although the pinned Dawn (`v20260907.201642`, `c4e47b5e`) offers one: `WGPUDawnCacheDeviceDescriptor`
(`dawn/webgpu.h:883`, the load and store callbacks at `:1290-1291`).

## Decision

- **A file-backed store** (`gpu::BlobCache`) behind Dawn's load and store callbacks, in
  `~/Library/Caches/avgen/pipeline-cache/` (macOS's place for caches the system may purge; elsewhere
  `<preferences>/cache/pipeline-cache`).
- **Safe across builds and versions.** The cache lives in a subdirectory named for the Dawn commit, the adapter and the
  OS build (`BlobCache::directoryKey`), and Dawn's own `isolationKey` is the adapter's vendor, architecture, device,
  description and backend. A Dawn upgrade, another GPU or an OS update starts an empty directory. Dawn's keys cover the
  shader source and the pipeline state, so a new avgen build with the same shaders hits and one with changed shaders
  misses only on what changed.
- **Recovers from a corrupt cache.** Each file holds a magic, a format version, the full key, the value's size and an
  FNV-1a checksum of the value. A load that finds a truncated, foreign or damaged entry treats it as a miss, deletes
  the file and logs it; Dawn compiles afresh and stores a good entry. Two keys that share a file name (a hash
  collision) are told apart by the stored key. Stores go to a temporary file renamed over the entry, so a crash leaves
  the old entry or none. A temporary file left by a crash is removed when the cache opens.
- **Bounded**: when the cache opens above 1 GiB it deletes least-recently-used entries down to 768 MiB (a load touches
  its entry).
- **Thread-safe**: Dawn calls the callbacks from its workers (asynchronous pipeline creation, ADR-1562).
- **`AVGEN_PIPELINE_CACHE=off`** runs without it (the A/B arm; a way out if a cache is ever suspected);
  `AVGEN_PIPELINE_CACHE=<dir>` puts it elsewhere. Deleting the directory is always safe.
- **Tests never use it**: `ContextDesc::pipelineCacheDir` is empty by default and only the application sets it, so no
  test depends on what an earlier run left behind.
- The context logs the cache's hits, misses, stores and discarded entries when it is destroyed.

## Consequences

**Measured** on 2026-10-28, M2 Max, load average 3-4, `--live-profile --mode headless --quality ultra`, 1080p. A
"fresh build" is a binary with new content (a one-line stamp in `main.cpp`, new Mach-O UUID each time; removed
afterwards). Time to a rendered frame = scene load + first frame + pre-warm, in seconds; the order is the order run.

| Run | Rainbow World | Aurora Borealis | Dawn cache |
|---|---|---|---|
| s1 fresh build, cache off | 30.5 | 7.9 | off |
| s1 same binary again, cache off | 14.3 | 6.1 | off |
| s2 fresh build, empty Dawn cache | 14.0 | 18.6 | 61 / 271 hits, 217 / 9 misses |
| s3 fresh build, warm Dawn cache | **12.7** | **4.5** | 278 / 280 hits, 0 misses |
| s3 same binary again | **12.6** | **4.4** | all hits |
| s3 same binary, cache off | 14.1 | 6.0 | off |
| s4 fresh build, cache off | 14.2 | 6.0 | off |
| s4 same binary again, cache off | 14.2 | 6.0 | off |
| s5 fresh build, warm Dawn cache | 12.7 | 4.4 | all hits |

- **Acceptance met:** a fresh build's first run with the cache is within 2x of a warm run: 12.7 against 12.6 s on
  Rainbow World and 4.5 against 4.4 s on Aurora (1.0x).
- **What the cache buys is Dawn's own work** (WGSL to MSL translation and reflection): about **1.5 s per launch on
  Rainbow World and 1.6 s on Aurora**, on every launch, fresh build or not (s3 and s5 against s3-off and s4).
- **What it cannot buy.** Dawn's Metal backend caches the MSL it generates, not the compiled Metal binary; compiling MSL
  is the Metal compiler's job, and macOS caches that per bundle identifier (`com.avgen.avgen`, ADR-1560). Two
  observations show that is where the audit's 16-26 s went:
  - **a new binary is not cold by itself**: s4, a fresh build without the Dawn cache, started in the same 14.2 s and
    6.0 s as a repeat run, because the Metal cache already held those shaders;
  - **a Metal miss is expensive whatever Dawn does**: s2's Aurora run hit Dawn's cache 271 times and missed 9, and its
    first frame took 14.3 s (against 0.1-0.3 s warm), compiling those 9 pipelines in Metal. s1's 30.5 s Rainbow run was
    a Metal miss of the same kind.

  So the audit's first-run cost is a **Metal compiler cache miss**: new or changed shaders, or shaders the
  `com.avgen.avgen` cache no longer holds (it is shared by every worktree's build and by every branch's shaders, and
  macOS evicts it). The pinned Dawn exposes no Metal binary archive, so that part is not recoverable through
  `WGPUDawnCacheDeviceDescriptor`. Recorded as a limitation, not hidden behind the 1.0x figure.
- **Rainbow World's 7.1 s first frame is unchanged by either cache** (7.0-7.3 s in every row, cache or not, fresh or
  not), so it is not shader compilation. The audit read it as "translation no cache holds" [H]; this measurement
  rules a shader cache out. ADR-1814 found it: LOD meshes simplified on the main thread, every launch (now 1.7 s).
- Cache size after these runs: 7.2 MB for the two scenes.
- Pre-warm read 0.0 s in every run: these scenes' variants were all requested and built inside load and the first
  frame, so the live profile's `prewarmMs` is not where this cost shows. Compare load + first frame.
