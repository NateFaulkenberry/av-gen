# ADR-1143: Thin-film interference and anisotropic highlights in pbr_shade

- **Status:** Accepted (2026-10-05), proto/astral-forge-material (production step of THE ASTRAL FORGE,
  `docs/prototypes/astral-forge/04-architecture.md` "Production path" item 3)
- **Date:** 2026-10-05
- **Resolves:** the prototype's material (`prototypes/astral-forge/shaders/common.wgsl` `thinFilm()`, the
  anisotropic reflection in `surface.wgsl`; research `01-research.md` §D-F) had no home in the production
  PBR path: temper colours and brushed highlights could only be faked with a material program's colour.
- **Implemented by:**
  - `scene::ThinFilm`, `scene::Anisotropy` and `Material::thinFilm`/`anisotropy` in `src/scene/scene_types.hpp`;
  - the file blocks, the parameters and the CPU twin of the tint: `src/scene/material_optics.*`, called
    from `src/scene/procedural.cpp`, `src/scene/sdf_object.cpp` and `src/scene/composition.cpp`;
  - the packing: `src/rendering/optics_pack.hpp`, into `ObjectUniforms::optics` (entity, procedural and
    SDF renderers), with `kObjectStride` 512 -> 768 (`scene_renderer.hpp`, `sdf_renderer.hpp`);
  - the shading: `thinFilmTint`, `anisotropicGgxDV` and `specularLobeDV` in `shaders/lighting.wgsl`; the f0
    tint, the tangent and the IBL bent reflection in `shadeSurface` (`shaders/pbr_shade.wgsl`);
  - the path tracer's capability notes in `src/pathtrace/snapshot.cpp`.
- **Tests:** `tests/unit/test_material_optics.cpp` (`[material_optics][adr1143]`),
  `tests/rendering/test_material_optics_gpu.cpp` (`[gpu][material_optics][adr1143]`), the amended
  ADR-128 guard in `tests/unit/test_renderer_layout_guards.cpp`.
- **Example:** `examples/astral-forge/tempered-metal.scene.json`.

## Context

The Astral Forge prototype's colour language is the colour of heat-treated steel: an oxide film whose
thickness sets its hue through interference (straw, bronze, purple, blue), on a dark conductor whose
engraved or brushed grain stretches the highlight. Its research (§D, §F) found both are physics the
production renderer did not have: `pbr_shade.wgsl` evaluates an isotropic GGX lobe with a Schlick Fresnel on
a plain f0, and nothing in the engine carries a surface tangent.

Two things constrained where the new state could live:

- **`ObjectUniforms` was exactly full.** ADR-1071 took its last three padding vec4s and recorded that "the
  next lane anyone needs grows `kObjectStride`".
- **Materials are authored per owner.** Procedural nodes, SDF objects and terrain/orb nodes each parse their
  own `material` block (ADR-1071 established the pattern for `toon`).

## Decision

### The file blocks and the parameters

A material may carry two optional blocks. Absent is the default and the default is off:

```json
"material": { "baseColor": [0.56, 0.56, 0.58], "metallic": 1, "roughness": 0.3,
              "thinFilm":   { "thickness": 55, "ior": 2.4 },
              "anisotropy": { "strength": 0.8, "rotation": 0.0 } }
```

| parameter (under the owner's prefix) | file key | range (soft) | default |
|---|---|---|---|
| `material/thinFilm/thickness` | `thinFilm.thickness` | 0..2000 nm (0..400); 0 = off | 0 |
| `material/thinFilm/ior` | `thinFilm.ior` | 1..5 (1.2..3) | 2.4 |
| `material/anisotropy/strength` | `anisotropy.strength` | -1..1; 0 = off | 0 |
| `material/anisotropy/rotation` | `anisotropy.rotation` | -2pi..2pi radians (-pi..pi) | 0 |

The owners: a procedural node's material (`procedural/<node>/material/...`), an SDF object's
(`sdf/<name>/material/...`) and a terrain or orb node's `material` block (file only: composition registers
no material parameters for those nodes today, for any field). The parameters are registered whether or not
the file has a block, so either look can be switched on from the Parameters panel, a route, MIDI or the
timeline, and they are pure functions of the frame (seek equals play). A block is written back only when it
differs from the default, so every existing scene saves exactly the file it had. An unknown key or a
non-finite number is a load error naming the key (and, on a composition node, the node). glTF assets keep
their own materials (there is no node-level material override; ADR-1071 records the same gap).

### Where the state lives: `kObjectStride` grows to 768

The four numbers are one new vec4, `ObjectUniforms::optics` = (thickness, ior, strength, rotation), at byte
512. `ObjectUniforms` is 528 bytes and `kObjectStride` becomes 768, the next multiple of the 256-byte
dynamic-offset alignment, in both `SceneRenderer` and `SdfRenderer` (the procedural renderer uses
`SceneRenderer`'s). `packOptics` writes zeros for any material with thickness 0 and strength 0, whatever
its ior and rotation, so "absent" and "explicitly zero" are the same uniform bytes; the shader gates on
`optics.x > 0` and `optics.z != 0`, both uniform per draw (ADR-118).

The alternative was a per-material uniform buffer in the group-2 material bind group (`materialBindGroup`,
`scene_renderer.cpp`). It is the larger and the less safe change:

- the group-2 cache is keyed by the material's textures and program; adding live values to the key means a
  route that sweeps `thinFilm/thickness` creates a new buffer and bind group every frame it moves, unbounded,
  or the renderer grows a per-frame material-uniform ring with its own lifetime rules;
- every pipeline that shares `materialLayout_` (entity, skinned, procedural, SDF raymarch and SDF mesh)
  changes its layout, and the material bind group would be a second, parallel place where a draw's material
  is described, beside `ObjectUniforms` -- two places to disagree;
- it buys nothing the stride does not: the values are per draw either way.

Growing the stride touches two constants, one struct and its WGSL mirror (both checked by
`test_renderer_layout_guards.cpp`), and its costs are bounded and measured below. It also leaves 15 free
vec4s in the slot, so ADR-135's per-draw tier lane now fits.

Costs of the stride: a 256-object buffer is 192 KB instead of 128 KB, and per-frame object uploads write
1.5x the bytes (Glowmere's 278 entities: 213 KB vs 142 KB). The 64 MiB ceiling now holds 87,381 slots
instead of 131,072, still five times the 16,383 entity indices the pick-id encoding can name, so the
ADR-128 relation ("the object buffer is not the binding limit") holds. ADR-128's guard pinned the initial
allocation as 128 KB; it now pins the slot count (256) and the bytes (192 KB).

### Thin film

`thinFilmTint(N.V, thickness, ior, lum(f0), metallic)` (WGSL in `lighting.wgsl`, CPU twin
`scene::thinFilmTint`) is the prototype's `thinFilm()` generalised:

- Airy reflectance of air | film | substrate at 16 wavelengths, 380..680 nm (the prototype used 8, which
  aliases above ~400 nm of film), the optical path `2 n d cos(theta_t)` with Snell's cos theta_t in the film;
- folded to XYZ with the Wyman, Sloan and Shirley (2013) fit of the CIE 1931 matching functions and to linear
  Rec.709, divided by the bare substrate's reflectance, so a zero film is exactly (1, 1, 1);
- the substrate is one real amplitude: `-sqrt(f0)` for a conductor (the metal's phase flip; the prototype's
  constant -0.62 is steel's), the Fresnel amplitude from the film into a dielectric of f0's ior otherwise,
  mixed by `metallic`.

It is evaluated once per fragment at N.V and multiplies f0 (clamped to [0, 1]) before the light loop, so it
reaches every light's Fresnel -- punctual, the representative-point sphere and tube lights, and the LTC
rect/disk lights' f0 term -- and the IBL split sum's `kS`. Belcour and Barla (2017, the basis of
`KHR_materials_iridescence`) is the reference; this is the prototype's approximation, documented as such:
it evaluates the film at the view angle rather than per light at V.H (exact for the mirror direction, so for
the IBL lobe and a sharp highlight), it does not prefilter the spectrum analytically (16 samples suffice up
to ~1000 nm), and the Schlick term still whitens the tinted f0 at grazing angles, where a real film keeps
shifting hue.

**The temper scale.** With iron oxide's ior of 2.4, the first-order temper sequence on steel spans about
20..90 nm of physical film: straw at 25-40 nm (hue ~42 degrees), bronze 45-50, purple ~55, blue 60-75,
pale blue ~90 (`test_material_optics.cpp` pins it: monotone hue from straw to blue, 0 nm neutral). The brief
expected straw -> purple -> blue over 150 -> 350 nm; physically that range is the second and third orders,
which repeat the sequence (orange 150, violet-blue 175, cyan 200, ..., magenta 275, blue 300) and are as
saturated in this model. 300 nm on steel is a blue (the GPU test's arm). An author who wants one sweep of
the classic sequence drives `thickness` over about 20..90 nm.

### Anisotropy

Anisotropic GGX after Burley (2012) with Kulla and Conty's (2017) parameterisation:
`alpha_t = alpha (1 + s)`, `alpha_b = alpha (1 - s)`, floored at 0.002, and the height-correlated
anisotropic Smith visibility (Heitz 2014). Positive strength stretches the highlight along the tangent,
negative across it. `specularLobeDV` switches on `ctx.anisotropy != 0` (a uniform branch) and otherwise is
the isotropic expression the lit path always evaluated; it serves `shadePunctual` (directional, point, spot
and the representative-point lights, which pass their widened alpha, so the sphere/tube normalisation is
kept) and `shadeUniform` (the fallback path).

**The tangent.** No mesh in the engine stores a tangent (`scene::Vertex` is position, normal, uv). A mesh
whose material has a normal map has a tangent frame -- the UV cotangent frame its normal map is decoded in --
and the lobe follows its U direction. Everything else -- SDF surfaces, procedural geometry, a mesh with no
normal map -- uses a reference: **the object's local +Y axis** (the model matrix's second column; its +X
where +Y is parallel to the normal) projected into the tangent plane. On a sphere that is the meridians, a
turned or brushed finish whose highlight runs pole to pole; it rotates with the object. Either tangent is
then turned about the normal by `rotation` (counter-clockwise looking down the normal), and the bitangent is
N x T. A procedural node's instances share their object's model matrix, so they share its reference axis,
not their own instance rotation.

**IBL.** The split sum's prefiltered cube and its LUT are isotropic. The reflection vector is bent towards
the plane containing the stretch direction (McAuley 2015, as Filament does), by `|s| * saturate(5 roughness)`;
a cheap approximation that elongates the environment's reflection along the right axis.

### What does not take it

- **LTC area lights stay isotropic**: their tables are fitted to the isotropic GGX lobe. They do take the
  thin film (through f0).
- **The toon branch (ADR-1071) and the scene-wide styled path skip the BRDF**, so they take neither: a toon
  or `environment.stylized` material with a film or an anisotropy shades exactly as it did.
- **The unlit path** takes neither.
- **The CPU path tracer** (`src/pathtrace/bsdf.hpp`) has neither term. `buildSnapshot` reports a
  `thin-film material` and an `anisotropic material` capability note (`Support::Degraded`) with the count of
  visible entities, procedural nodes and SDF objects that carry one, so a trace says it rendered the bare
  material instead of dropping the look silently.
- Water (`water.wgsl`) builds its own ShadeContext, zero-initialised, so it takes the isotropic lobe.

## Consequences

- **Defaults are byte-identical.** A material with neither block packs a zero lane and takes no new branch.
  `test_material_optics_gpu.cpp` renders an entity, a procedural node, an SDF object and an orb with the
  fields absent and explicitly zero (with a non-default ior and rotation) and compares bytes; the repo's
  golden and image tests pass unchanged.
- A pinned build and its own commit's shaders stay consistent; a new binary with old shaders (or the reverse)
  disagrees about `ObjectUniforms`' size, as with ADR-1071.
- Cost: measured in the final report of this change (shading pass with every sphere filmed and anisotropic
  vs neither); the film is 16 iterations of a few exponentials per fragment, once, and the anisotropic lobe
  replaces the isotropic one per light.

## Rejected alternatives

- **A per-material uniform in the group-2 bind group.** Weighed above: unbounded bind-group churn under
  modulation or a new ring buffer, a layout change on five pipelines, and a second home for a draw's
  material. The stride is smaller and safe.
- **Packing the four numbers into the three free scalar lanes** (`emission.zw`, `fxB.w`) with
  `pack2x16float` and a bitcast. It avoids the stride but stores bit patterns in float lanes (a NaN-quieting
  copy would corrupt them), hides two fields in lanes documented as zero, and leaves the next feature where
  this one started.
- **Belcour and Barla's analytic spectral integration** (the `KHR_materials_iridescence` reference code). More
  accurate at grazing angles and per light, but a different curve from the prototype that was judged, and the
  brief accepted the prototype's approximation if documented. A natural follow-up if the film is ever seen at
  grazing angles in close-up.
- **Storing a tangent in `scene::Vertex`.** Grows every vertex (terrain chunks included) for a feature most
  surfaces do not use, and SDF surfaces would still need the reference axis.
- **World +Y as the reference.** Simpler, but a rotating brushed object would slide its grain over itself.

## Revisit triggers

- An authored tangent field is wanted (the prototype's engraving direction, ADR-pending "line field" material
  op): the material program's result would then supply the tangent instead of the reference axis.
- Iridescence seen at grazing angles in close-up: move to Belcour and Barla's per-light evaluation.
- A glTF asset's `KHR_materials_iridescence` / `KHR_materials_anisotropy` should import: map
  `iridescenceThicknessMaximum`/`iridescenceIor` and `anisotropyStrength`/`anisotropyRotation` onto these
  fields (the tangent then wants the asset's TANGENT attribute).
- The path tracer gains a thin-film or anisotropic BSDF: remove the capability notes.
