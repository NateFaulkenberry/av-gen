# Procedural materials

Status: research (2026-09-09). Decision: ADR-030.

## 1. Reference systems
- **Unreal material graph**: expressions over inputs (world/local position, normal, UV, object/instance id, per-instance custom data, time, parameters) producing base colour, metallic, roughness, emissive, opacity, normal, world position offset; compiled to HLSL. (https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-materials — accessed 2026-09-09.)
- **Blender shader nodes**: Texture Coordinate / Geometry / Attribute inputs, Noise/Voronoi/Gradient/Wave textures, ColorRamp, Map Range, Math, Mix, Fresnel, Layer Weight → Principled BSDF.
- **Notch materials**: layered PBR with procedural inputs and *field* inputs (colour and value from fields).
- **ISF / Shadertoy**: per-pixel programs with time and audio inputs — our shader layers already do this for 2D.

## 2. Options for av-gen
1. Generated WGSL per material (compile a program into code, cache pipelines): maximum performance, but pipeline permutations, compile latency on edit, and a second shader-generation path.
2. **Interpreted op list in the fragment shader** (a uniform array of ops with a small register file), one pipeline: edits are uniform writes, hot, deterministic, no permutations; cost ~1 ALU per op per pixel, negligible at ≤ 16 ops.
3. A texture-baked lookup (bake ramps/noise to textures): fast but static.

Chosen: option 2 now (with ramps/palettes as small textures later), option 1 as the future optimisation when a program is frozen.

## 3. Inputs and ops
Inputs: world position, local position, normal (world), uv, object id, instance id, normalised instance index, four instance attributes (the record's randoms/extra), time, audio (rms, bass, mid, treble, centroid, flux, onset, beat phase), field samples (up to 4 field slots), noise. Ops: gradient (axis or direction), noise (fbm), voronoi (F1), fresnel, ramp (3-stop colour), remap, multiply, add, mix, power, smoothstep, threshold, hue shift, saturation, value. Outputs: base colour, metallic, roughness, emission (colour × intensity), opacity, normal perturbation (later).

## 4. Colour
OKLab/OKLCH (Björn Ottosson, https://bottosson.github.io/posts/oklab/ — accessed 2026-09-09) for perceptually even hue/lightness manipulation; HSV/HSL for authoring familiarity; palettes as 4-stop gradients with cycling (Quilez's cosine palettes https://iquilezles.org/articles/palettes/ — accessed 2026-09-09) are compact and audio-drivable (offset = hue, cycle = beat phase).
