# ADR-013: Image-based lighting and environment maps

- Status: Accepted (2026-09-08)
- Research: `docs/research/rendering-techniques.md` (PBR, HDR), `docs/research/assets.md` §IBL

## Problem

Milestone 0.2 needs physically based materials lit by an HDR environment so imported glTF assets
look as intended, with results that are deterministic per GPU and cheap enough to rebuild when a
user drops in a new HDRI at runtime.

## Alternatives considered

1. Offline preprocessing with Filament's `cmgen` (prefiltered cubemap + SH + DFG LUT files).
2. Runtime GPU preprocessing with fragment passes (split-sum approximation, Karis 2013).
3. Runtime compute-shader preprocessing.
4. Spherical harmonics for diffuse instead of an irradiance cube.

## Decision

**Runtime GPU preprocessing with fullscreen fragment passes** (`rendering::EnvironmentProcessor`,
`shaders/environment.wgsl`):

- Equirect `.hdr` is uploaded as RGBA16Float with a CPU box-filtered mip chain (filterable on
  every WebGPU backend without the `float32-filterable` feature).
- Source cube 256² with a full mip chain, each mip rendered from the equirect's matching mip.
- Diffuse irradiance cube 32² by cosine-weighted Hammersley sampling (256 samples) of a blurred
  source mip.
- Specular prefiltered cube 128² with 6 mips (roughness 0, 0.2 … 1) by GGX importance sampling
  (128 samples) with pdf-based source-mip selection (Colbert & Krivanek) to suppress fireflies.
- Split-sum BRDF LUT 128² RG16Float (256 samples), computed once.
- The PBR shader combines `irradiance * kD * albedo + prefiltered(R, roughness) * (F0 * A + B)`
  scaled by `environmentIntensity`; the skybox samples the prefiltered cube (blur = mip).

## Rationale

- Fits the user workflow (drop an HDRI, see it immediately): 127 ms for a 1k HDRI on the M2 Max.
- Fragment passes are the most portable path in WebGPU today and reuse the same pipeline
  machinery as the scene passes; compute would not be faster for this size.
- Fixed Hammersley sequences make output deterministic for a given GPU (visual regression).
- An irradiance cube keeps the shader trivial; SH can replace it later without API change.

## Consequences

- Environment processing blocks the main thread at load time (tens of milliseconds); an async
  variant is straightforward when needed.
- Spherical harmonics, specular occlusion, and multi-scattering energy compensation are not
  implemented yet; roughness 1 relies on the coarsest prefiltered mip.
- Any scene with `environment.environmentMap` set is re-processed when the texture version
  changes; the renderer caches by (texture id, version).
