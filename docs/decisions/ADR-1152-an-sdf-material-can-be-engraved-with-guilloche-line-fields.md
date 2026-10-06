# ADR-1152: An SDF material can be engraved with guilloche line fields

**Status:** Accepted (proto/astral-forge, the production path of THE ASTRAL FORGE, iteration 3, step 2)
**Date:** 2026-10-06
**Resolves:** "a line-field material op for engraving" (`docs/prototypes/astral-forge/06-iteration-2.md`, next
step 3), and ADR-1143's revisit trigger "an authored tangent field is wanted (the prototype's engraving
direction)".
**Implemented by:** `scene::Engraving`, `EngravingLayer`, `EngravingFamily` and `Material::engraving` in
`src/scene/scene_types.hpp`; the file block, the parameters and the CPU twin of the coordinates in
`src/scene/material_engraving.{hpp,cpp}`; `src/scene/sdf_object.{hpp,cpp}` (read, write, refuse on a meshed
object, parameters); the refusals in `src/scene/procedural.cpp` and `src/scene/composition.cpp`;
`src/rendering/sdf_renderer.cpp` (the engraved pipeline variant -- variants are now keyed by surface flags,
density and engraved -- and the layer records packed after the object's nodes); `shaders/sdf_raymarch.wgsl`
(`kSdfEngraved`, `sdfEngraveUv`, `sdfGrooveLayer`, `sdfEngrave`); `SurfaceDetail` and `surfaceDetail()` in
`shaders/pbr_shade.wgsl` (defined by every includer: `pbr.wgsl`, `procedural.wgsl`, `sdf_raymarch.wgsl`);
the path tracer's `engraved material` note in `src/pathtrace/snapshot.cpp`.
**Tests:** `tests/unit/test_astral_production_look.cpp` (`[adr1152]`: round trip and refusals by name, refused on
a meshed SDF and on a procedural node, parameters only when authored, the coordinates of each family);
`tests/rendering/test_astral_production_look_gpu.cpp` ("engraving: grooves add fine detail and colour, a
zero-weight engraving adds none"), `[gpu][engraving][adr1152]`.

## Context

The prototype's surface is engraved: guilloche, the rose-engine line work of watch dials and banknotes
(`01-research.md` §D). Three families of lines (`latent.wgsl` `engraveUV`, `surface.wgsl` `grooveFamily`):
rosettes round each eye, contour rosettes about a point behind the mask cut in noise panels, and engine-turned
waves between the panels. Each is drawn in four octaves (frequency x 4) that fade out by their pixel footprint,
as V-grooves that tilt the normal, with the strongest groove's direction as the anisotropy tangent, the
reflection smeared across the grooves, and a diffraction-grating term that splits each band into its spectrum.
In the iteration-2 comparison it is the single largest difference: production read as "a blobby chrome mask".

## Decision

**A material may carry `engraving`**; an SDF object's material is the one that draws it:

```json
"engraving": { "depth": 0.3, "crawl": 0.07, "grating": 1.0, "spacing": 1600, "panels": 0.55,
  "layers": [
    {"family": "rosette", "center": [-0.76, 0.72, 0.6], "petals": 12, "frequency": 12, "weight": 1,
     "depth": 0.35, "inner": 0.36, "outer": 0.85},
    {"family": "contour", "center": [0, 0.2, -3], "petals": 7, "frequency": 7, "depth": 0.28},
    {"family": "engine", "axis": [0.94, 0.30, 0.17], "frequency": 9, "weight": 0.25, "depth": 0.16} ] }
```

At most six layers. Each family gives surface coordinates (u, v) in the object's local space:

| family | u | v | waves per turn |
|---|---|---|---|
| rosette | angle about local z round `center` | radius in xy | `petals` |
| contour | angle in xy round `center` | 3-D distance | `petals` |
| engine | 3 x distance across `axis` | distance along `axis` | 1 |

Per octave o = 0..3 the lines are `L = f v + A sin(n u + 0.35 f v)` with `f = frequency x 4^o`,
`n = petals x 2^o` and A = 1.5 line spacings (faded to 0 near a rosette's centre): fine lines carrying a wave
whose phase drifts across lines -- the braid and moire of a rose engine, self-similar under zoom. An octave is
faded out between 0.12 and 0.35 line cycles per pixel (the footprint is the pixel angle times the hit distance),
so lines never alias. A V-groove 0.64 of a spacing wide tilts the normal across the line by
`slope x depth x visibility / 1.6^o`. Regions: a rosette is cut in its annulus [`inner`, `outer`] and polished
inside `inner`; contour layers are cut in noise panels of frequency `panels` (everywhere when 0) and kept 80%
off the rosettes; engine layers between the panels and off the rosettes. `crawl` turns the pattern
(radians, or line widths for engine) per second -- a pure function of time, so seek equals play.

**Into the shading** (`SurfaceDetail`, filled by the SDF pass before `shadeSurface`):

- the perturbed normal is the normal `shadeSurface` receives;
- the strongest groove's line direction is the **anisotropy tangent** (ADR-1143's reference axis is replaced
  wherever anything is cut; a negative `anisotropy.strength` stretches the highlight across the lines, as the
  prototype's reflection does);
- the band reflection (ADR-1151) is smeared across the grooves (five taps, spread `alpha + 0.22 x groove`);
- each band strip is **diffracted** by the grooves: `d (sin i + sin o) = m lambda` across them, orders 1..3,
  `spacing` nm, weighted by how close the half vector is to the plane across the lines, `x grating`, kept off
  the rosettes (where the line direction turns too fast per pixel and the spectrum aliases to confetti, as the
  prototype found).

**Reach.** `SurfaceDetail surfaceDetail()` is a hook every includer of `pbr_shade.wgsl` defines (the ADR-138
pattern): `pbr.wgsl` and `procedural.wgsl` return the zero record, a constant, so the new branches compile away
there; `sdf_raymarch.wgsl` returns its private record only in the **engraved variant** (`kSdfEngraved`, set
between the `@@SDF_SURFACE@@` markers like ADR-1150's density flag). SDF variants are now keyed by those surface
flags (density, engraved) for the interpreter and, with the tree, for compiled trees. The layer records ride in
the node buffer after the object's nodes (and ADR-1044's surface records), addressed by `surfaces.zw`, so
`SdfObjectUniforms` does not grow.

**Owners that cannot draw it refuse it by name**: a procedural node's material and a terrain/orb node's
material (`'engraving' is drawn on SDF objects only (ADR-1152)`), and a `renderMode: mesh` SDF object. A block
nothing draws would be ADR-704's silent no-op.

**Parameters**, registered only when the block is authored: `<owner>material/engraving/{depth, crawl, grating,
spacing, panels}` and `<owner>material/engraving/<k>/{frequency, weight}`, all uniforms.

## Consequences

- Without the block nothing changes: no record, no variant, no parameter; the default modules are
  byte-identical (ADR-1150's seven references, 0 differing channels). The hook returns a constant in `pbr.wgsl`
  and `procedural.wgsl`, measured by the same references (`hero.json`, `fungi.json`, `organic.json`).
- An engraved sphere has ~25x the fine-detail energy (mean squared Laplacian) of the plain one in the GPU test;
  the same engraving at weight 0 draws the plain sphere (mean |dL| < 0.5 code; the variant compiles slightly
  different code, so this is not a byte comparison); the grating adds light whose mean chroma is high (spectral,
  not white).
- **Object space, not "after the domain ops".** The prototype cuts its lines in the warped domain, so a fold
  carries them. Production evaluates them in the object's local space: they follow the object's transform
  (move, turn, scale) but not the tree's internal domain operations (twist, bend), which differ per branch of
  a tree and have no single inverse. A twisted object's engraving does not twist with it.
- Procedural nodes are not engraved: their per-draw uniform slot (`ProceduralUniforms`, 768 bytes) is full and
  `ObjectUniforms` belongs to the stride work in flight (ADR-1144..1149). They refuse the block.
- Cost: see ADR-1153's table.

## Rejected alternatives

- **A material-program op** (ADR-030's interpreter). The program runs in the generic material path with no
  pixel footprint or band environment; the grating and the tangent need both, and the program's tangent-space
  normal is in the UV cotangent frame, which an SDF surface does not have.
- **The layers in `SdfObjectUniforms`.** Seven free vec4s would hold three fixed layers and fill the slot;
  the node buffer holds six variable layers at no uniform cost.
- **A uniform branch instead of a variant.** The same drift ADR-1142 measured.

## Revisit triggers

- Engraving on meshes or procedural geometry: needs a per-draw lane (after the stride work) and a tangent
  frame.
- Lines that follow a tree's domain warps: evaluate them on a per-branch chain, as a compiled tree could emit.
- A seventh layer, or per-layer grating spacing.
