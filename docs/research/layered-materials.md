# Layered procedural materials and surface detail

Status: research (2026-09-09). Decision: ADR-036.

## 1. The problem in the current frames

Every procedural primitive shades as a perfectly smooth surface with one roughness. Real surfaces
vary at three scales at once, and the absence of that variation is what makes an instanced
cylinder read as a cylinder primitive rather than as a stone column.

| Scale | What it is | Representation |
|---|---|---|
| Macro | metres: which part of the object is weathered, oxidised, wet, painted | masks from world position, height, curvature, large noise |
| Meso | centimetres: panel seams, cracks, machining marks, chips | procedural patterns, decals, detail geometry |
| Micro | millimetres: roughness texture, micro-normal, pitting | shader-only noise on roughness and normal |

## 2. Layering model

Substance and Unreal both express a material as a base plus layers, each with a mask. That model
transfers directly onto the existing op program: a layer is a set of ops producing base colour,
roughness, metallic, normal and emission, and a mask op selects between the layer and what is
below. Keeping one interpreted program with layer semantics avoids a second material language,
which the brief forbids.

## 3. Geometric inputs materials should have

- **Curvature** and its signed parts (convexity, concavity) drive edge wear and cavity dirt. On a
  triangle mesh this is a preprocess; for procedural instances the cheap route is screen-space
  derivatives of the normal, which is stable enough for wear masks (Unreal uses the same trick for
  its "curvature from normal" material function).
- **Cavity and ambient occlusion** darken crevices; with GTAO available (ADR-034) the material can
  read the occlusion buffer directly.
- **Height** feeds parallax and blending: height-based blending between layers (blend by
  `max(h1 + m, h2)` rather than a linear lerp) is what makes a worn layer sit in the low spots
  rather than fading uniformly.
- **Normal variance** across a footprint drives specular anti-aliasing: filtering roughness by
  normal variance (Kaplanyan et al., "Filtering Distributions of Normals", 2016; also LEAN/Toksvig)
  is the standard fix for the shimmering that procedural micro-normals otherwise cause.

## 4. Mapping without UVs

Procedural geometry has trivial or no UVs. Triplanar projection (blend three world-space
projections by the squared normal) is the standard answer and is what Houdini, Unreal and Blender
all reach for. World-space and object-space variants matter: world-space keeps a texture stationary
while the object moves (good for terrain and architecture), object-space moves with it (good for
parts of a machine). Both must be available per layer.

## 5. Decals

A decal is a mask projected onto a surface: box-projected in world space, evaluated in the
material rather than as a separate draw. For procedural worlds the useful ones are seams, cracks,
grime pooling, glyphs and warning markings. They belong in the same op program with a projection op
plus a mask, so they participate in layering and masking rather than being a separate system.

## 6. Anti-aliasing procedural detail

Procedural noise aliases when it goes below a pixel. The fixes that matter here: fade detail
amplitude with screen-space derivative footprint, filter roughness by normal variance, and keep
noise frequencies tied to world scale rather than to object scale so instances of different size
do not shimmer differently.
