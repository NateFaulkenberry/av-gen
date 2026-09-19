# ADR-350: The denoiser ships as a prebuilt binary, because building it needs a compiler this project does not have

**Status:** Accepted
**Date:** 2026-09-18
**Amends:** ADR-348, which said this renderer would not bring oneTBB into the tree
**Relates to:** ADR-001 (Dawn's prebuilt precedent), ADR-008 (dependency policy), spec sections 51, 80, 81

## Problem

Phase 4 of the path tracer needs Open Image Denoise. The question was how to get it, and the
answer that looked obvious -- CPM, pinned tag, built from source, like almost everything else --
turns out not to be available.

## What building from source actually requires

Measured by trying it, on this machine, at v2.3.3:

**1. oneTBB, mandatory.** `devices/cpu/CMakeLists.txt` does `find_package(TBB ... REQUIRED)` with no
alternative tasking backend and no internal-tasking option. Embree has
`EMBREE_TASKING_SYSTEM=INTERNAL`; OIDN has nothing equivalent.

**2. ISPC >= 1.21, mandatory, as a binary download.** OIDN's CPU kernels are written in ISPC and its
CMake refuses to configure without the compiler:

> *This version of Intel(R) Open Image Denoise expects you to have a binary install of ISPC minimum
> version 1.21.0 ... Could not find ISPC. Exiting.*

That second one is the decisive fact. **ISPC is not a library; it is a third-party compiler
toolchain.** Nothing else in this project needs one to build. Adding it would mean either vendoring a
compiler binary through CPM or requiring every contributor and every CI runner to install one, for a
single optional feature.

## Decision

**Fetch the official prebuilt macOS arm64 archive, SHA256-pinned, exactly as ADR-001 does for Dawn.**

```
URL       https://github.com/RenderKit/oidn/releases/download/v2.3.3/oidn-2.3.3.arm64.macos.tar.gz
SHA256    b3c005ed437547fca5460ae43c8631ff46bc4ec4f9d5f219940ef941601a9d81
```

The archive carries its own `libtbb`, so TBB is never built here, and it carries the trained weights
-- which are the 49 MB inside `libOpenImageDenoise_core`. On Apple silicon OIDN runs its network
through **BNNS** (Accelerate), so no oneDNN is involved either.

**It is OFF by default** (`AVGEN_PATHTRACE_DENOISE`). A 51 MB download for an optional feature should
not be charged to a build that never asked for it. The option gates the fetch *and* the code:
without it `denoiseAvailable()` returns false and `denoise()` **fails with a message naming the
option**, rather than returning its input unchanged. A denoise that silently did nothing is
indistinguishable from one that ran and achieved little, and is far worse to debug than a refusal
(spec section 54).

## This amends ADR-348, which was wrong about TBB

ADR-348 states, of Embree: *"TBB is not used (`EMBREE_TASKING_SYSTEM=INTERNAL`), so no oneTBB
dependency and no second scheduler."* That remains true **of Embree**. It is no longer true of the
project once denoising is enabled: the OIDN dylib brings its own TBB and its own thread pool.

Is that the second uncontrolled pool spec section 36 forbids? **No, and the reason is timing rather
than intent.** Denoising is a discrete stage that runs *after* the render completes, on a
framebuffer. The tracer's threads have joined before OIDN's exist. The two pools are sequential, not
concurrent, and never contend. The prohibition is about two schedulers fighting over cores during a
render, and that does not happen here.

What would break this: denoising *during* progressive accumulation, to show a cleaned preview while
the render continues. That is a genuinely attractive feature and it is the point at which this
reasoning stops holding. Anyone adding it must bound OIDN's pool (`tbb::global_control`) against the
tracer's thread count, and should revisit this section rather than assume it still applies.

## Verified

A standalone spike, built outside AV Gen against the prebuilt dylib, before any integration: the CPU
device commits, and on a gradient with 8% salt noise the neighbour-to-neighbour roughness falls from
0.2299 to 0.0050 while the mean moves 0.5694 to 0.5753 and the gradient survives (0.34 left, 0.78
right). A filter that merely darkened the image would also have smoothed it; the mean and gradient
arms are what separate those.

In-tree, on a Cornell-style box at **8 samples per pixel**: roughness falls by more than half, mean
luminance moves 0.1183 to 0.1175, the red and green walls stay red and green, and -- the claim that
matters -- **RMSE against a 512-sample reference falls by more than 30%**. That last is the arm a
roughness test cannot make: a uniform grey image has zero roughness and is not a denoise.
`docs/pathtrace/phase4-denoise-before-8spp.png` and `-after-oidn.png`.

Licence verified from the fetched source, not assumed: **Apache-2.0** (`LICENSE.txt`). oneTBB, which
arrives inside the archive, is also Apache-2.0.

## Consequences

- Two build configurations now exist and **both are tested**. The denoise tests run either way: with
  OIDN they assert it denoises, without it they assert it refuses. A test that only ran in one
  configuration would leave the other free to rot, and the default is the one without.
- No source build on any other platform. A Linux or x86 build gets the warning and the
  feature reports itself unavailable. Adding those means more pinned archives, not a source build.
- The albedo and normal feature AOVs exist now rather than at Phase 6, because the denoiser needs
  them. They are captured from the first hit, opt-in via `TraceSettings::captureFeatures`, and a
  test asserts that capturing them leaves the beauty pass **bit-identical**.
