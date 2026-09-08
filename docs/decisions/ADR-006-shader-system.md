# ADR-006: Shader system

- Status: Accepted (2026-09-08)
- Research: `docs/research/shaders.md`, `docs/research/architecture-options.md` §12

## Problem

Engine shaders must be editable with a short iteration loop today, and custom user shaders with
exposed parameters must become a first-class feature (milestone 0.4) without redesign.

## Alternatives considered

1. WGSL files loaded at runtime (native WebGPU path).
2. Slang authored, compiled to WGSL or SPIR-V.
3. GLSL 450 via glslang -> SPIR-V -> Tint SPIR-V reader.
4. Embedded shader strings compiled into the binary.

## Decision

- **Engine shaders are WGSL files under `shaders/`, loaded from disk at runtime** by
  `gpu::ShaderLibrary`, which resolves an `#include "file.wgsl"` convention (simple textual
  include, no preprocessor) so noise/SDF/colour helpers are shared between vertex, fragment and
  compute stages.
- Shader compilation errors are logged with Dawn's diagnostics and the previous pipeline is kept;
  if none exists, a built-in magenta error shader is used.
- Uniform data crosses to shaders through explicitly laid-out structs mirrored in C++ with
  `static_assert` on size and offset; no reflection in 0.1.
- Hot reload (file watcher + recompile off the render thread + pipeline swap at frame boundary)
  is milestone 0.4 together with the user-shader format.
- **User-droppable shaders (0.4) adopt an ISF-style contract**: a JSON header declaring INPUTS
  with type/min/max/default/label and PASSES with persistence, followed by the shader body. WGSL
  bodies are supported natively; GLSL bodies (ISF and Shadertoy compatible) are translated by one
  of Tint's SPIR-V reader (from-source Dawn), naga, or Slang's WGSL target, chosen in 0.4 after
  prototyping all three on real ISF shaders. Parameters declared in the header become
  `Parameter`s in the engine's parameter system (ADR-011).

## Rationale

- WGSL is the only language Dawn's prebuilt archive accepts; it is a small, well-specified
  language with excellent diagnostics.
- Files-at-runtime gives the iteration loop now with two lines of code and makes hot reload a
  small addition.
- The ISF contract is the de facto standard for VJ software (VDMX, Resolume, Millumin) and
  thousands of shaders exist; supporting it is the difference between a shader "feature" and an
  ecosystem.

## Consequences

- WGSL lacks modules and generics; if engine shader code grows unwieldy, Slang -> WGSL is the
  documented upgrade path for engine shaders (user shaders unaffected).
- The shader directory is a runtime asset; the executable locates it relative to its own path
  or via `AVGEN_SHADER_DIR`.
- Reflection for user shaders will use Tint's inspector or the ISF JSON header; the engine never
  depends on Metal reflection.

## Rejected alternatives

- Embedded strings: kills the iteration loop.
- SPIR-V pivot as in the original shader research: Dawn's prebuilt Tint has no SPIR-V reader;
  the pivot moves to WGSL.
- Slang now: adds a large compiler dependency before it is needed.

## Revisit triggers

- Engine shader duplication that WGSL includes cannot manage.
- Slang's WGSL target proven on ISF-scale shaders.
