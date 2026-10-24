# ADR-1642: A material program reads textures, triplanar in world space

- Status: Accepted (eng/terrain-textures)
- Extends ADR-030 ("textures as inputs come later") and ADR-036 (the procedural `triplanar` op); follows ADR-050's
  rule that the interpreter loop's body sets every program's register budget.
- Asked for by the Living Prism aurora valley: rock and mossy-rock PBR on the canyon walls, moss on the ground and
  around crystal bases, and a bushy ground texture in the distance. Regions are masked by slope, height or noise.
- Code:
  - `src/scene/material_program.{hpp,cpp}`:
    - `MaterialTexture` (`name`, `path`, `srgb`, the runtime `resolvedPath`) and `MaterialProgram::textures`;
    - the ops `TextureTriplanar` and `TextureTriplanarNormal`, appended (`MaterialOp::texture` names the image);
    - validation (at most 4 texture ops and 8 images), JSON, the hash (unchanged for a program without textures);
    - the CPU reference (`textureOpValue`; `MaterialContext::tangent`/`bitangent`/`textures`);
    - packing (`MaterialProgramGpu::textureOps`, a new header lane: the header is 112 bytes, was 96).
  - `src/scene/scene.hpp` (`Scene::programTextures`, `programTextureVersion`); `src/scene/composition.cpp` (paths
    resolved beside the scene or `.material.json` file; `collectProgramTextures` loads the images through the
    registry's cache on each rebuild).
  - `src/gpu/texture.{hpp,cpp}` (`uploadTextureArray`: one mip-mapped 2D array; a smaller image is resampled up).
  - `src/rendering/scene_renderer.{hpp,cpp}`:
    - material bind group bindings 9 to 11 (an sRGB array, a linear array, a repeating trilinear sampler);
    - `uploadProgramTextures`; the packer's layer resolver;
    - `ensureTexturedLitPipelines` (`pbr-*-textured`), chosen per draw by `programSamplesTexture`;
    - `warnUntexturedPaths`.
  - `src/rendering/material_programs.{hpp,cpp}`; `src/scene/material_compiler.cpp`.
  - `shaders/material.wgsl`:
    - the `MAT_PROGRAM_TEXTURES` pipeline override;
    - `materialTexturePrepass`, `matTextureOp`;
    - `MatFields` widened to 8 slots, with the texture ops in slots 4 to 7.
  - `shaders/pbr_shade.wgsl` (the bindings; the context's tangent frame).
- Tests:
  - `tests/unit/test_material_program.cpp` (`[adr1642]`): JSON round-trip, validation, path resolution, the CPU
    planes and the tangent-frame answer, packing.
  - `tests/unit/test_composition.cpp`: a scene's program loads its images, and saves the authored path.
  - `tests/rendering/test_material_gpu.cpp` (`[adr1642]`):
    - CPU/GPU parity with real arrays (worst difference 2.7e-4);
    - a checker reaches an entity's pixels, is white without the image, and returns hash-identical when the image
      does;
    - hidden `[measure]`: the entity-path cost.
  - The interpreter harnesses (`test_material_gpu.cpp`, `test_material_perf.cpp`) declare the new bindings.

## Problem

Material programs took no texture inputs (ADR-030). The terrain's look is noise and ramps only: it cannot read as
rock or moss. The procedural `triplanar` op projects noise, not images.

## Decision

**Images named by the program, read by two ops, triplanar in world space.**
- **The list.** `"textures": [{"name", "path", "colorSpace": "srgb"|"linear"}]`. The path is relative to the file
  the program is in.
- **`textureTriplanar`:** the image at `worldPosition * f + k.xyz` on the three axis planes, blended by
  `triplanarWeights(normal, k2.x)`, returning rgba.
- **`textureTriplanarNormal`:** the same projection of a tangent-space normal map.
  - Each plane's x/y lands on that plane's world tangent and image-up axes. The plane uv's sign flips make `u × up`
    each face's outward normal, so an OpenGL-convention map lights correctly on every face.
  - The perturbation is added to the surface normal (UDN), then expressed through the inverse of the frame
    `pbr_shade` applies a program normal in (`ctx.tangent`, `ctx.bitangent`, `normal`; the cotangent frame, not
    orthonormal). `M · ts` is the blended world normal exactly.
  - `k2.y < 0` reads a DirectX map; `k2.w` is a strength.
- **Mip level.** `log2(footprint · f · width) + k2.z`. One pixel's world footprint picks the mip, so supersampling
  sharpens and distance does not shimmer.
- **Masks are ops the language already has:** slope (`gradient` on the normal), height, noise, decal box, field,
  camera distance, `mixBy`, layers. No mask concept was added.

**Sampled before the loop, like fields (ADR-050).** A texture op reads no register: it depends on the fragment only.
`materialTexturePrepass` samples each one once into `MatFields` slots 4 to 7, and the op inside the loop reads its
slot. At most 4 per program.

**A pipeline constant, not a branch.** The texture code, present in the shader at all, cost every program, textured
or not (measured below). `override MAT_PROGRAM_TEXTURES` (default false) compiles it out:
- every existing pipeline is built as before, and its texture ops read colour 1 and a flat normal;
- the entity pass has three textured variants, built on the first frame that draws an entity whose packed program
  has a texture op, and used only for those draws;
- terrain chunks are entities.

Procedural (instanced, and GLB parts), SDF and skinned surfaces have no textured variant. Their texture ops read the
defaults, and the log names the surface and its program once.

**One array per colour space.** Every image the scene's programs name is decoded once (the registry's cache) into
`Scene::programTextures`. The renderer lays them into an `RGBA8UnormSrgb` array and an `RGBA8Unorm` array at the
largest size among them (resampling a smaller one up), with full mips. The table's digest decides re-upload.

## Evidence (M2 Max, AC power, Release; honest timestamps, ADR-1600; media `~/Desktop/av-gen-review/88-terrain-textures/`)

**Unchanged by default.**
- The aurora valley (the scene branch's `aurora-valley-offline.json`, unchanged; offline tier, 2x supersample,
  1080p, t = 40 s) renders pixel-identical to main 44362df9: 0 differing pixels.
- Every existing material GPU test, including CPU/GPU parity of every op, passes.

**Parity.** Texture ops against the CPU reference over 10 contexts with skewed, unnormalised tangent frames, a
DirectX map at strength 1.5, and a missing texture: the worst difference is 2.7e-4.

**The cost of the texture code being present.** Two harnesses:
- the interpreter's compute A/B (`test_material_perf`, "A/B against a baseline shader", 5.18 M invocations,
  minimum of 9). The baseline is main's `material.wgsl` with only the new header lane added.
- the entity path (the hidden `[measure]` case): 4K, a ground entity filling the frame, a 12-op ground program.
  It shows the scene timing's median over 6 interleaved reps of 80 frames.

| version | compute A/B, terrain-shaped 12 ops | entity path, no texture op | entity path, + colour and normal op |
|---|---|---|---|
| baseline | 6.71 ms | 35.33-35.46 ms | — |
| first: pre-pass in every pipeline | 8.28 ms (+23%) | 37.27 (+1.98 ms) | 44.42 |
| pre-pass loops given bounds the compiler cannot unroll | 8.09 | 36.47 (+1.06) | 46.66 (the reads serialised) |
| **shipped: `MAT_PROGRAM_TEXTURES` override** | **7.01** | **36.06 (+0.6)** | **44.23** |

- **A null control** (both renderers on the same shader): +0.04 ms. The residual +0.6 ms is real.
- **The residual (1.7% of this frame) is not diagnosed.** I timeboxed the search. With the override off, each of
  these alone leaves it in place:
  - the pre-pass call deleted outright: +0.85;
  - the two dispatch arms removed: +0.5;
  - `MatFields` back at 4 slots: +0.77;
  - the tangent-frame copy removed: +0.88.

  It applies only to the program-driven pixels of an entity, which on the Living Prism's offline frame
  (0.28-0.30 s) is invisible.
- **Cost when used.** A colour and a normal op on a frame-filling 4K ground: +8.2 ms over the same program without
  them; about a quarter of that at 1080p.
- Compute A/B, all arms, shipped version:

  | program | before | after |
  |---|---|---|
  | empty | 0.783 ms | 0.790 ms |
  | 20 × Constant | 6.915 ms | 7.105 ms |
  | 20 × Mix | 7.409 ms | 7.317 ms |
  | 20 × Noise | 14.866 ms | 15.120 ms |

**The look.** A demo program on the aurora valley's terrain (generated tileable rock and moss sets, `demo-textures/`
and `demo_ground.material.json` in the media folder) shows textured moss on the flats and rock on the walls, the
mask being the normal's slope (`aurora-t40-before-after.jpg`). The real CC0 sets are the scene agent's to source
(the owner's scene spec §11). No texture was downloaded here.

## Consequences

- The scene agent can texture the valley: rock and mossy rock on steep walls, moss on the flats and around crystal
  bases (a `decalBox`, a `field` or a height band), and a coarser texture by camera distance.
- A scene's first textured frame builds three pipelines synchronously, once.
- **GLB crystals and scans (procedural parts) cannot sample textures**, and say so in the log.
- The material bind group has three more bindings. Every interpreter harness declares them.
- `MatFields` is 8 slots, so a program can carry 4 fields and 4 texture ops together.

## Rejected

- **Texture ops inside the loop**, evaluated where they stand: ADR-050's measured trap.
- **Branching on the program header at run time** (the first version): the code's presence alone cost +23% in the
  compute A/B and +2 ms at 4K, whether or not it ran.
- **A triplanar texture layer on the terrain material** (outside programs): it needs its own masks, a second
  mask language, and cannot share the program's slope and noise.
- **Each image as its own binding**: a fixed number of slots, and a bind group per program. One array per colour
  space binds once for the scene.

## Revisit triggers

- A GLB part or an SDF needs a texture: give the procedural renderer the same override variant (its lit,
  refraction and compiled-materials pipelines).
- The residual +0.6 ms matters in a live scene: bisect further, starting from main's shader and adding pieces.
- A set needs more than 4 texture ops per program: split it across layers, or widen the slots and measure.
