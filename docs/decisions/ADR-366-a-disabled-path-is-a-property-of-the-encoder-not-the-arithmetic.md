# ADR-366: A disabled path is a property of the encoder, not of the arithmetic

- Status: Accepted (2026-09-19)
- **Numbering:** ADR-360 was the stated high-water mark when this work started. By the time it
  was written, 363, 364 and 365 had all been claimed — 363 and 364 twice each, from different
  branches. 366 was free across every branch visible at the time. Expect it to be renumbered at
  merge; nothing in the tree references it by number except this file's own name.
- Builds on ADR-039 (image formation and the post chain), ADR-016 (post-processing), ADR-059
  (FXAA), ADR-079 (tilt-shift), ADR-182 (a diagnostic arm that cannot fail), ADR-345 (append,
  never insert), ADR-347 (fog takes its colour from the sky), ADR-350 (a system the application
  ran and did not keep), ADR-352 (the glTF BRDF gains energy at grazing).
- Work: the Image/Look framework, `docs/image-look-spec.md`; audit in `docs/image-look-audit.md`.

## Problem

`docs/image-look-spec.md` §60/§87 states the milestone: **with integration at zero, the image is
unchanged.** Not "close".

The obvious way to build the four §68 controls is to fold them into `fs_composite`, gated on
`if (amount > 0.0)`. The composite already runs every frame, already samples depth for the ADR-038
depth grade, and already binds the bloom and wide-tier textures. Four controls, no new passes, no
new allocations. It is the cheap design and it is the wrong one.

## Decision

**Nothing is folded into a pass that always runs. The passes that carry the cinematic integration
are not encoded at all unless an amount is off its default.**

`ImageLookIntegration::active()`, `atmosphericActive()` and `lookActive()` are the single place
that decision is made, and both the renderer and the tests ask them, so they cannot disagree about
what "at zero" means.

### Why the cheap design cannot make the promise

A `if (amount > 0.0)` inside `fs_composite` does not produce the same machine code as the
`fs_composite` that existed before it. The branch changes register pressure; the extra uniform
reads change scheduling; a driver is free to reassociate the surrounding arithmetic differently.
On a floating-point pipeline, "the same result" and "the same instructions" are not the same claim,
and only the second one survives a hash. The cheap design would have obliged us to weaken §60 from
"unchanged" to "within an epsilon", which is exactly the retreat the spec forbids.

Encoding is a property we control exactly. A pass that is not in the command buffer cannot perturb
anything, and the guarantee stops depending on a compiler's goodwill.

### What this costs

Two extra full-resolution round trips and two quarter-resolution ones **when the controls are on**,
where the folded design would have cost zero extra passes. That is the price of the milestone and
it is paid only by scenes that asked for the feature.

## The corollary the spec got wrong

The spec's §1 quotes `src/rendering/post_processor.hpp:17` — *"with everything off and a unit
exposure the input is returned unchanged"* — and says "that is §60. It exists."

It does not exist, and the header is wrong. `PostProcessor::run` **always** encodes the composite
and always returns a pool texture, never `in.sceneHdr`. And `fs_composite` at default grade
settings is not the identity function:

- `pow(x, 1.0)` lowers to `exp2(1.0 * log2(x))`, off by an ulp or two for most finite `x`. It runs
  twice — once for the log-space contrast, once for the gamma.
- `max(colour, vec3(1e-5))` lifts a true black pixel to 1e-5.
- the divide by `0.18` and the multiply by `0.18` do not cancel in binary floating point.

`docs/image-formation.md:36` states this correctly. The header does not. **§60 is therefore a
differential claim**: "unchanged" can only mean *identical to the same build with the feature
absent*, because the chain was never bit-identical to its own input. Comparing against
`in.sceneHdr` would be a probe that can never pass — ADR-182's failure seen from the other side.

## What was measured

Two independent proofs, because the in-process one and the end-to-end one fail in different ways.

### 1. In-process, over the pre-tonemap float buffer

`tests/rendering/test_image_look_gpu.cpp`, via `renderToImageFloat` (which reads `hdrOutput_`, the
post chain's own output) and `gpu::hashImage`, over float bit patterns.

| arm | expectation | result |
|---|---|---|
| every amount zero, rendered twice | same hash | **same** |
| both *shape* parameters moved off their defaults, amounts still zero | same hash | **same** |
| `atmospheric = 0.6` | hash moves | **moves** |
| `colour = 0.8` | hash moves | **moves** |
| `localContrast = 0.7` | hash moves | **moves** |
| `lightWrap = 0.9` | hash moves | **moves** |
| the four moved hashes, pairwise | all different | **all different** |
| back to zero after all of the above | the original hash | **the original hash** |

One control at a time, deliberately. A single combined perturbation passes even if three of the
four are dead, which is the exact shape of the four subsystems that shipped unreachable in the
session before this one. The pairwise check catches the other version of the same failure — one
parameter wired to another's effect.

The final "back to zero" arm is not redundant: the look stage acquires four transient textures, so
"off after on" has a different pool-allocation history from "off first", and the pool's match key
includes the usage bits.

### 2. End-to-end, against a binary in which the feature does not exist

`examples/hero/hero.json`, one frame at t = 2.00 s, 1920x1080, Release, all arms serialised through
`tools/gpu-lock.sh`. The baseline is a *separate worktree checked out at the parent commit*
(`8f23d2ec`) and built from scratch, so it contains no `ImageLookIntegration`, no `post/look/*`
parameters and no `fs_look*` entry points. Compared by sha256 of the PNG.

| arm | binary | change | sha256 of frame 0 (first 24) |
|---|---|---|---|
| **A baseline** | parent commit `8f23d2ec` | none — the feature does not exist | `6a525a5756becac7700ea62d` |
| **B zero** | `agent/imagelook` | none — the feature exists and is at zero | **`6a525a5756becac7700ea62d`** |
| **C control (new)** | `agent/imagelook` | `post/look/colour = 0.6` | `3bca64b6993c087547c0bbb0` |
| **D control (old)** | `agent/imagelook` | `post/grade/saturation = 1.4` | `974ed699719682db2559088e` |

**A and B are byte-identical** — `cmp` reports no difference, on frame 0 and on frame 1. Adding the
cinematic integration to the engine changed nothing about a render that does not ask for it.

Both controls fire, and they are there for different reasons:

- **C** perturbs one of the *new* parameters. It is the arm that proves the probe can fail: if the
  new code were inert — never encoded, never reaching the shader — C would have matched B and the
  byte-identity result would have been worthless. It does not match.
- **D** perturbs an *old* parameter, one that exists in both binaries. It proves the render harness
  is sensitive to a project-parameter change at all, independently of anything this work added. A
  harness that produced one hash whatever you did to it would satisfy A = B for the wrong reason.

The parameter counts corroborate it from the other side, and they are the check that would have
caught a silent registration failure: the baseline loads **1025** parameters from `hero.json`; the
new binary loads **1032** from the same file. Exactly seven more, which is exactly the seven
`post/look/*` parameters — registered, reaching the application, and reported by the project loader
with `0 warning(s)`. Counted by name, not by file size: ADR-350's camera-bake loss shrank a file by
10,000 lines while its parameter count went *up*, so a size check would have said healthy.

## Consequences

- The guarantee is enforced by the encoder and is cheap to re-verify: `active()` is the only thing
  that has to stay honest.
- Anyone adding a fifth control must add it to `active()` or it will silently never render. That is
  the failure mode this design trades for; it is loud in the GPU test, which asserts that each
  amount trips the right pass.
- `post_processor.hpp:17`'s claim should be corrected to match `docs/image-formation.md:36`. Left
  as a recommendation rather than done here, because the header comment is load-bearing
  documentation for a chain this work did not otherwise change.
