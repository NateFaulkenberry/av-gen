# ADR-014: User shader contract and shader layers

- Status: Accepted (2026-09-08)
- Research: `docs/research/shaders.md` §7 (ISF), `docs/research/architecture-options.md` §12,
  ADR-006

## Problem

Milestone 0.4 makes custom shaders a first-class feature: a user drops a shader file into a
project, its parameters appear in the UI and can be modulated like any other parameter, it can
read the audio analysis, run multiple passes with feedback buffers, and it reloads on save
without restarting.

## Alternatives considered

1. Raw WGSL modules with a fixed uniform struct the user must declare by hand.
2. ISF (JSON header + GLSL body) verbatim, translating GLSL to WGSL at load time.
3. ISF-style JSON header + **WGSL body** with a single `mainImage` entry point and an engine-
   generated prologue (chosen).
4. Shadertoy conventions (`iTime`, `iChannel0`) only.

## Decision

- A user shader is a `.wgsl` file with an optional leading `/*{ ... }*/` JSON header in the ISF
  vocabulary: `INPUTS` (`float`, `long`, `bool`, `color`, `point2D`, `event` with
  DEFAULT/MIN/MAX/LABEL) and `PASSES` (`TARGET`, `PERSISTENT`, `FLOAT`, `WIDTH`/`HEIGHT`
  expressions). The body defines `fn mainImage(uv, fragCoord) -> vec4<f32>`.
- The engine generates the module: a `Std` uniform (time, delta, frame, pass, sizes, audio
  levels, beat clock), an `Inputs` uniform struct laid out by WGSL rules, a sampler, the input
  image (post stage), an audio spectrum texture, one texture per named pass target, and the
  vertex/fragment entry points. Inputs become parameters at `shader/<layer>/<input>`.
- A layer runs at one of two stages: **Background** (drawn full-screen inside the scene pass
  before geometry, depth write off) or **Post** (reads the HDR scene image, writes a ping-pong HDR
  target before tone mapping). Persistent targets are double-buffered so a pass can read its own
  previous frame (feedback).
- Compile errors keep the last good pipelines, or an error pattern (magenta stripes) when there
  are none, and are shown in the UI with Tint's file:line diagnostics mapped through the
  generated prologue.
- Hot reload polls file modification times (0.25 s) for user shaders and the engine's own WGSL
  files; engine pipelines are rebuilt individually and keep their previous version on failure.
- Projects store layers as `{path, stage, enabled}`; their parameter values live with all other
  parameters.

## Rationale

- WGSL bodies need no translation on Dawn's prebuilt archive (no SPIR-V reader), keep Tint's
  diagnostics readable, and the ISF header preserves the widely known metadata contract, so a
  GLSL/ISF translation path can be added later as a front end without changing the engine side.
- Generating the prologue means the user never declares bindings and the engine controls layout,
  which is what lets inputs be parameters and lets the standard block evolve.
- Two stages cover the immediate use (generative backgrounds, full-screen post looks) and are the
  seam for the 0.6 post-processing stack and render graph.
- Polling avoids a file-watcher dependency for a handful of files; efsw remains the upgrade.

## Consequences

- Only `mainImage`-style full-screen shaders are supported; geometry or compute user shaders are
  later work. GLSL ISF files are not accepted yet.
- The standard uniform block is part of the contract; fields are only appended.
- Background layers ignore depth, so they cannot occlude geometry; use a Post layer to composite
  over the scene.
- Non-persistent targets keep stale content until their pass runs each frame.
