# ADR-2109: A cloud block written for ADR-1289 keeps its march; a worktree can keep its own Metal shader cache

- Status: Accepted (2026-11-20), avagent/neon-dreams (Neon Dreams, iteration 8: the POC's health check)
- Extends: ADR-1289 (sculptural clouds), ADR-2100 (cloud environment v2), ADR-1560 (avgen's own Metal cache),
  ADR-1811 (Dawn's pipeline cache).
- Code: `src/scene/clouds.{hpp,cpp}` (`Clouds::evenSteps`, read and written), `src/rendering/cloud_renderer.cpp`
  (`march2.y < 0`), `shaders/clouds.wgsl` (`legacyDensity` and the even branch of the march), `src/CMakeLists.txt`
  (`AVGEN_BUNDLE_ID`), `cmake/avgen-Info.plist` (now a template).
- Tests: `[adr2109]` in `avgen_tests` (`test_clouds.cpp`); `[clouds]` in both suites.

## Context

1. **The biovalley clouds softened.** Clouds v2 (ADR-2100..2108) replaced the ADR-1289 march outright: half
   resolution with temporal accumulation, steps that grow with distance (`minStep`, `stepGrowth`), a texture noise
   volume instead of domain-warped value-noise fbm, and a different light march. Scenes authored for ADR-1289 (the
   biovalley `meadow` and `vista` shots, the only other `clouds` users) went through the new march with its defaults.
   Measured against main f7b4ce68 at 1280x720 (6 s, 16 frames): every difference was in the clouds, 1.03 % (vista) and
   1.09 % (meadow) of pixels more than 8/255 apart, and the masses lost their wispy, billowed edges (soft blobs).
   Rainbow World, Digital Mosh and Chorus Field were bit-identical to main.
   Full resolution and no temporal (`resolution 1`, `temporal 0`) alone did not restore it (1.15 %); finer steps
   neither (1.14 %). The noise model is the look.
2. **The ~100-200 s launch stall.** On a Metal cache miss the compiled-materials variants (ADR-1493/1812) take 60-170 s
   to compile, serially in `MTLCompilerService`; Dawn's own cache (ADR-1811) hit 317 of 317 in the same runs, as that
   ADR predicted. The miss was intermittent: the same biovalley vista shot, same binary, same materials, was cold
   again 50 minutes after it was compiled, with Digital Mosh and Chorus Field compiled in between (15 minutes of SDF
   and material variants). `com.avgen.avgen`'s Metal cache is shared by every worktree's avgen and macOS holds it
   near 1.1 GB: `functions.data` stayed at 1.11-1.15 GB while `functions.list` shrank (257,644 to 235,020 bytes)
   across those compiles. It is least-recently-used eviction: sixteen worktrees' scenes in one 1.1 GB cache.

## Decision

1. **`evenSteps`**: a clouds block that names none of `resolution`, `temporal`, `minStep`, `stepGrowth` and
   `maxDistance` was written for ADR-1289 and reads with `evenSteps` on (and `resolution` 1 unless given). The march
   then takes `steps` even steps across the span with no stride or skip, the density is ADR-1289's (the lobe union,
   uneroded, shaped by warped value-noise billows and eroded by a finer fbm, ported unchanged as `legacyDensity`), and
   the light march is ADR-1289's even taps through that density. Everything else in v2 (sea, glows, beams, halo,
   resolve) works on top. It is written as `"evenSteps": true`, so a save keeps it, and can be asked for explicitly.
   v2 blocks (any of those keys present, as every Neon Dreams build writes) are untouched.
2. **`AVGEN_BUNDLE_ID`** (CMake cache variable, default `com.avgen.avgen`): `cmake/avgen-Info.plist` is configured into
   the build directory with it. A worktree whose scene is expensive to compile can take a Metal cache of its own,
   e.g. `cmake --preset release -DAVGEN_BUNDLE_ID=com.avgen.avgen.neon-dreams`, so other worktrees' shaders cannot
   evict it. The default changes nothing.

## Consequences

- Biovalley vista and meadow against main: 0.028 % and 0.023 % of pixels more than 8/255 apart (from 1.03 % and
  1.09 %); by eye the same masses and edges. The remaining difference is the v2 resolve and ambient height range.
- Neon Dreams on its own cache: first launch 175 s (167 s of it the compiled-materials variants), the next launch
  6.6 s. The Neon Dreams worktree is configured with `com.avgen.avgen.neon-dreams`.
- The stall itself is not removed: any change to a scene's material program structure, or to `material.wgsl` /
  `pbr.wgsl`, still costs one cold compile of 1-3 minutes per scene. A Metal binary archive is the real fix and the
  pinned Dawn does not expose one (ADR-1811).
- A new bundle identifier is a new app to macOS's privacy settings: live audio input asks for microphone access again
  once.
