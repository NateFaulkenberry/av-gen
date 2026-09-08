# Asset Formats, Loaders, Animation Runtime, IBL, and Asset Management

Research note for the av-gen real-time audiovisual engine. Written 2026-09-08. All URLs accessed 2026-09-08 unless noted.

Target context: native C++ engine, CMake 4.0, clang 21, macOS 26 / Apple M2 Max first, Windows/Linux must remain possible. Festival-quality 3D visuals.

Citation convention: `[An]` markers refer to the Sources list at the end; each entry records URL, date accessed, what was learned, relevance, and confidence/limitations.

---

## 1. Executive summary / recommendations

1. **glTF 2.0 (.glb preferred) is the canonical 3D interchange format.** Everything else (FBX, OBJ, USD) is converted to glTF either offline (gltfpack, Blender) or by an optional import plugin. glTF is a Khronos standard with 25 ratified KHR extensions and a rich C/C++ loader ecosystem. [A1][A2]
2. **Loader: fastgltf (MIT, C++17, v0.9.0)** as the primary in-engine glTF parser; **cgltf (MIT, single-header C, v1.15)** as a zero-dependency fallback/alternative if fastgltf's simdjson dependency or C++20 direction becomes a problem. Both cover the extensions we need (KHR_texture_transform, KHR_lights_punctual, KHR_materials_emissive_strength/transmission/volume, KHR_texture_basisu, EXT_meshopt_compression, KHR_draco via external decoder). [A3][A4]
3. **Mesh processing: meshoptimizer (MIT, v1.2, 2026-06-30)** for vertex cache/overdraw/fetch optimization, LOD simplification, meshlets, and EXT_meshopt_compression decoding. It is small, dependency-free and battle-tested. [A6]
4. **FBX: ufbx (MIT/Unlicense, single .c/.h)** if and when we need direct FBX import; do **not** take on Assimp (BSD-3, huge, frequent CVE fixes) unless we need its breadth. [A7][A8]
5. **Images: stb_image (public domain/MIT, v2.30) for PNG/JPEG/TGA/BMP/HDR-radiance; tinyexr (BSD-3, header-only) for EXR** (covers NONE/RLE/ZIP/ZIPS/PIZ; not DWAA/B44/PXR24). Use full OpenEXR (BSD-3, needs Imath+libdeflate) only if DWAA/B44 files show up. libjpeg-turbo / libpng only if decode throughput becomes a bottleneck. [A9][A10][A11]
6. **GPU-compressed textures: KTX2 + Basis Universal via libktx (Apache 2.0, v5.0.0-rc2 2026-08-17).** On Apple GPUs transcode UASTC -> ASTC 4x4 (native, zero-cost for UASTC HDR) and ETC1S -> ASTC/ETC2; on desktop BC7/BC6H. libktx has Vulkan/GL upload helpers but **no Metal upload helper**; we write our own (~100 lines). DDS support via a single-header reader (dds-ktx) or stb-style code only for legacy content. [A12][A13][A14]
7. **Skeletal animation: ozz-animation (MIT, v0.17.0 2026-08-01)** for sampling/blending/IK with GPU skinning via joint matrices, plus its `gltf2ozz` importer. glTF morph targets handled by us (simple weighted vertex displacement). [A15][A16][A17]
8. **IBL: use Filament's `cmgen` (Apache 2.0) offline** for equirect -> prefiltered specular cubemap (KTX) + irradiance SH (9 coefficients) + DFG LUT; ship results as KTX2. Implement a GPU-side runtime prefilter (compute) later for live-loaded HDRIs. cmft is unmaintained since ~2015; IBLBaker is a 2013-era Windows/D3D11 demo. [A18][A19][A20]
9. **Asset management: handle-based registry (index + generation), typed handles, path-relative references in project files, a stable per-asset GUID stored in the project manifest, async loading on a task pool with main-thread GPU upload, and file-watcher hot reload.** This mirrors Bevy, Unreal's AssetManager, and floooh's handle design. [A21][A22][A23]
10. **Milestone 0.1: no external asset loaders at all.** Procedural geometry (fullscreen triangle, quad, cube, sphere, torus, grid), procedural textures (noise, gradients, LUTs), and shaders only. **Milestone 0.2:** fastgltf + meshoptimizer + stb_image + tinyexr + libktx (transcoder only) + ozz-animation + cmgen-produced IBL assets. [Section 8]

---

## 2. glTF 2.0 as canonical 3D format

### 2.1 Core spec facts
- Khronos standard; JSON scene description + binary buffers; `.glb` container packs JSON + BIN chunks. Coordinate system: right-handed, +Y up, +Z forward (front), meters. PBR metallic-roughness base material; textures with sampler wrap/filter; nodes with TRS or matrix; meshes -> primitives with attribute accessors; cameras; animations; skins; morph targets. [A1]
- Skins: `joints` (node indices) + `inverseBindMatrices`; vertex `JOINTS_0` / `WEIGHTS_0` (4 influences per set; additional `JOINTS_1/WEIGHTS_1` sets for >4). `jointMatrix[j] = inverse(globalTransform(meshNode)) * globalTransform(joint j) * inverseBindMatrix[j]`; vertex shader blends `sum(w_i * jointMatrix[j_i]) * position`. [A16]
- Morph targets: `primitive.targets[]` maps `POSITION`/`NORMAL`/`TANGENT` to *displacement* accessors; `mesh.weights` defaults, `node.weights` overrides, animation channel path `"weights"` animates them; `rendered = base + sum(weights[i] * targets[i])`. [A17]
- Animation: channels target node `translation`/`rotation`/`scale`/`weights` with samplers using `LINEAR`, `STEP`, or `CUBICSPLINE` interpolation. [A1]
- Confidence: high (Khronos spec/tutorials); the spec HTML itself returned 403 on fetch, so details are corroborated from the official glTF-Tutorials repo and extension READMEs.

### 2.2 Extensions relevant to av-gen (status from the Khronos extension registry README, accessed 2026-09-08) [A2]

| Extension | Status | What it adds | Why we care |
|---|---|---|---|
| KHR_materials_emissive_strength | Ratified | `emissiveStrength` scalar multiplies `emissiveFactor * emissiveTexture`, allowing > 1.0 emission; not combinable with `KHR_materials_unlit` | HDR emissives feeding bloom; essential for stage/LED-style visuals [A2b] |
| KHR_texture_transform | Ratified | per-textureInfo `offset`, `rotation` (radians, CCW), `scale`, optional `texCoord` override; matrix = T * R * S; (0,0) is upper-left | Scrolling/tiling textures driven by audio; atlases [A2c] |
| KHR_lights_punctual | Ratified | `directional` (lux), `point` (candela, inverse-square), `spot` (candela, `innerConeAngle` default 0, `outerConeAngle` default pi/4); optional `range`; lights point down node -Z | Lets artists author lights in Blender and ship them in the scene [A2d] |
| KHR_materials_transmission | Ratified (2020) | `transmissionFactor` + `transmissionTexture` (R channel); requires opaque pre-pass / screen-space refraction; use `alphaMode: OPAQUE`; fixed IOR 1.5 unless KHR_materials_ior present | Glass/crystal looks; forces a "render opaques to texture, then transmissive" pass in our renderer [A2e] |
| KHR_materials_volume / ior / specular / clearcoat / sheen / iridescence / anisotropy / dispersion | Ratified | Extended PBR | Pick up as the PBR shader matures; loaders already parse them |
| KHR_materials_variants | Ratified | Named material sets per primitive | Cheap "looks" switching for live shows |
| KHR_texture_basisu | Ratified | Texture source is a KTX2 image (`image/ktx2`) with Basis Universal supercompression (ETC1S+BasisLZ or UASTC+optional Zstd); width/height multiples of 4; should contain full mip pyramid; optional PNG/JPEG fallback | Small downloads + GPU-native formats on Apple (ASTC) and desktop (BC7) [A13] |
| EXT_meshopt_compression | Ratified (multi-vendor) | bufferView-level compression modes ATTRIBUTES / TRIANGLES / INDICES with OCTAHEDRAL / QUATERNION / EXPONENTIAL filters; fallback buffers; layers over KHR_mesh_quantization; decode with meshoptimizer at ~1 GB/s | Best size/speed trade-off; decoder is already in our chosen mesh lib [A6b] |
| KHR_draco_mesh_compression | Ratified | Google Draco geometry compression | Common in web exports; requires the Draco C++ decoder (Apache 2.0, ~ large). Support via optional plugin only |
| KHR_mesh_quantization | Ratified | Allows non-float vertex attribute types (int16/normalized) | Required to consume gltfpack output; trivial in loaders |
| KHR_animation_pointer | Ratified | Animate arbitrary JSON pointers (material params, light intensity...) | Very relevant for authored parameter animation; check loader support before relying on it |
| EXT_texture_webp | Ratified (multi-vendor) | WebP images | Not needed; avoid |
| KHR_interactivity, KHR_physics_rigid_bodies, KHR_gaussian_splatting | Ratified / RC / draft | Behaviour graphs, physics, splats | Watch list; gaussian splatting is interesting for visuals later |

### 2.3 Loader comparison (C/C++)

| Library | Language / form | License | Latest release | Deps | Extensions | Notes |
|---|---|---|---|---|---|---|
| fastgltf | C++17 (C++20 modules optional), header+src | MIT | v0.9.0 (2025-07-08); "likely the last C++17 version" | simdjson (bundled) | Very broad incl. Draco, meshopt, basisu, texture_transform, lights_punctual, materials_*, animation_pointer, physics_rigid_bodies | SIMD-accelerated parsing; `std::expected`-style API; exporter included; used in vkguide and many hobby/pro engines [A3] |
| cgltf | C99 single header | MIT | v1.15 (2025-02-09) | none (jsmn embedded) | EXT_mesh_gpu_instancing, EXT_meshopt_compression, EXT_texture_webp, KHR_draco (parse only), KHR_lights_punctual, KHR_materials_* (anisotropy, clearcoat, transmission, volume, unlit, iridescence, dispersion, diffuse_transmission...), KHR_materials_variants, KHR_texture_basisu, KHR_texture_transform | Used by bgfx, Filament, raylib, Unigine; `cgltf_parse/load_buffers/validate/write`; no image decoding [A4] |
| tinygltf | v3: pure C11 (+ optional C++20 coroutine facade); v2 C++ in attic | MIT | v3.0.1 (2026-08-02); v3.0.0 (2026-03-23) | fast_float, dragonbox embedded; optional stb_image | Generic `extensions` maps rather than typed structs | Arena allocator, fuzzed, index-bounds validation on; big API change from v2 [A5] |
| Assimp | C++ | BSD-3 (+ ISC bits) | v6.0.5 (2026-04-30) | zlib, many | 40+ formats incl. FBX, glTF, USD (partial), VRML | Frequent security releases (buffer overflows in FBX/PLY parsers); heavy build; converts everything to its own `aiScene` losing glTF specifics [A8] |
| ufbx | C99 single .c/.h | MIT OR Unlicense | rolling `master` (no GitHub releases; semver in header) | none | FBX binary+ASCII from v3000 up, meshes, skinning, blend shapes, NURBS, lights, cameras, animation evaluation, embedded textures, geometry caches, PBR material unification, **OBJ/MTL** too | 592 tests, 95% branch coverage; thread-safe under C11 [A7] |
| tinyobjloader | C++ single header (+ new pure-C `tiny_obj_c`) | MIT | v2.0.0-rc branch; 2026-06 added C11 loader + tessellator, 2026-05 SIMD/multithreaded `LoadObjOpt` | none | OBJ/MTL | Fine for quick tests; ufbx also loads OBJ so we may not need it [A24] |

Assessment: fastgltf gives us the fastest parse and the richest typed extension coverage; cgltf gives us the smallest footprint and zero C++ standard risk. Both are MIT. The decision can be deferred to 0.2 implementation; the asset layer should wrap either behind our own `MeshAsset`/`SceneAsset` types.

### 2.4 meshoptimizer
- MIT; v1.2 (2026-06-30): MikkTSpace tangent generation, 20-45% faster vertex decoding; v1.1 (2026-04-02): meshlet codec (7-10 GB/s decode), opacity micromaps; v1.0 (2025-12-08): stabilized clusterization/simplification, `clusterlod.h` hierarchical LOD; v0.25 (2025-08-20): `meshopt_simplifyWithUpdate`, permissive simplification. [A6]
- Provides: index/vertex cache optimization, overdraw optimization, vertex fetch optimization, quantization helpers, simplifier (LOD chains, with attributes), meshlet builder (for mesh shaders / GPU culling), vertex/index codecs (EXT_meshopt_compression), `gltfpack` CLI (also does KTX2/BasisU texture encoding). [A6][A6b]
- Relevance: run `meshopt_optimizeVertexCache/Overdraw/VertexFetch` on every imported mesh; generate 2-3 LODs offline for heavy scenes; decode EXT_meshopt buffers on load. Confidence: high.

---

## 3. Image decoding

| Library | Formats | HDR | License | Latest | Deps | Notes |
|---|---|---|---|---|---|---|
| stb_image | JPEG (baseline+progressive; no 12-bit/arithmetic), PNG 1/2/4/8/16-bit, TGA, BMP (no 1bpp/RLE), PSD (composited), GIF, HDR (Radiance RGBE), PIC, PNM (binary) | `.hdr` via `stbi_loadf` (float RGB), `stbi_hdr_to_ldr_gamma/scale`; 16-bit via `stbi_load_16` | public domain / MIT dual | v2.30 (2024-05-31) | none; optional SSE2/NEON (`STBI_NEON`) | `STBI_MAX_DIMENSIONS` guard; custom allocators; not hardened for hostile input beyond that [A9] |
| tinyexr | OpenEXR scanline + tiled, HALF/FLOAT/UINT, multipart, deep (read) | yes | BSD-3 | rolling (v1 header-only) | miniz bundled or system zlib | Compression: NONE, RLE, ZIP, ZIPS, PIZ, ZFP (exp). **Not** PXR24, B44, DWAA/DWAB. Optional C++11 threads/OpenMP; fuzzed [A10] |
| OpenEXR | Full EXR incl. DWAA/B, B44, PXR24, deep, multipart | yes | BSD-3 | v3.4.15 (2026-08-21); v3.4.14 fixed 15 CVEs | Imath, libdeflate (+ vendored OpenJPH) | `OpenEXRCore` C API available; use only if tinyexr's codec gaps bite [A11] |
| libjpeg-turbo | JPEG | no | IJG/BSD-3/zlib | 3.2.0 (2026-06-30) | none | 2-6x faster than stb for big JPEGs; TurboJPEG API [A25] |
| libpng | PNG | no (16-bit yes) | PNG Reference Library License | 1.6.58 (2026-04-15); 1.6.51 described as "most critical update in decades" (security) | zlib | Only if PNG throughput or exotic chunks matter [A25] |
| lodepng | PNG (decode+encode) | no | zlib | rolling (C repo) | none | Simpler than libpng, slower; useful for screenshots/encode [A25] |
| KTX2 / libktx | KTX2 containers, Basis Universal, Zstd/ZLIB supercompression, block-compressed and uncompressed formats, mipmaps, cubemaps, arrays | yes (UASTC HDR, BC6H, RGB9E5, RGBA16F) | Apache 2.0 | v5.0.0-rc2 (2026-08-17; final pending KTX spec rev 5); v4.4.2 stable (2024-10) | basisu transcoder (bundled), zstd | `ktxTexture2_TranscodeBasis`; Vulkan/GL upload helpers only, no Metal helper; `ktx create/encode/transcode/validate` CLI; Darwin arm64 binaries [A12][A14] |
| Basis Universal (standalone) | ETC1S, UASTC LDR 4x4, UASTC HDR 4x4/6x6, XUASTC, XUBC7; transcodes to BC1-7, ASTC, ETC1/2, PVRTC, BC6H, RGBA/half | yes | Apache 2.0 | v2.50 (2026-08-03, "XUASTC with in-loop deblocking, XUBC7, DDS support") | none for transcoder (single .cpp) | Apple guidance: target **ASTC 4x4**; UASTC HDR 4x4 *is* ASTC HDR so no transcode needed on Apple GPUs [A13] |
| DDS | DX9 header + DX10 (`DXGI_FORMAT`) header; BC1-7 | BC6H | Format is MS-documented; readers: dds-ktx (single header, no-alloc, MIT), DirectXTex (MIT, Windows-centric) | n/a | none | Support read-only for legacy artist content; prefer KTX2 [A26] |

Decisions: stb_image + tinyexr cover 0.2; libktx transcoder for GPU textures; libjpeg-turbo/libpng/OpenEXR are drop-in upgrades behind the same `ImageDecoder` interface if profiling demands.

Metal notes: MTLPixelFormat supports ASTC (`astc_4x4_ldr/hdr`, all block sizes) and BC1-7 on Apple silicon Macs (BC formats are supported on macOS Apple GPUs; ASTC is the native family), EAC/ETC2, RGBA16Float, RGB9E5Float. Choose transcode targets by `MTLDevice.supportsFamily`. Confidence: medium (from general Metal knowledge; verify against the Metal Feature Set Tables before 0.2).

---

## 4. Skeletal animation and morph targets

- glTF skinning/morph semantics: see 2.1. Our renderer needs: joint matrix palette buffer (SSBO / Metal buffer), vertex shader with `JOINTS_0/WEIGHTS_0` (and optional `_1`), morph target delta buffers (SSBO indexed by target, or a compute pre-pass that bakes blended positions into a per-instance vertex buffer, which is the more scalable approach for many targets). [A16][A17]
- **ozz-animation** (MIT): runtime = skeleton + compressed animation + sampling job + blending job + local-to-model job + two-bone/aim IK; SoA SIMD (SSE, NEON via generic path); offline `gltf2ozz` (uses tinygltf) and `fbx2ozz` (requires FBX SDK). v0.17.0 (2026-08-01): NaN fixes in two-bone IK, rest-pose utility, updated tinygltf; v0.16.0 (2025-01-19): root-motion extraction; v0.15.0 (2024-04): iframes for fast seeking, 17-25% smaller keyframes. Skinning: ozz outputs model-space matrices; multiply by inverse bind matrices to get the palette for GPU skinning (ozz also ships a CPU skinning job for reference). [A15]
- Relevance: gives us blending (crossfade between clips on beat), additive layers, IK for procedural motion, and a stable binary runtime format, without writing a sampler/blender ourselves. Risk: ozz's own skeleton format means the glTF skin joints must be remapped to ozz joint order at import; `gltf2ozz` exists for this but our loader path (fastgltf/cgltf) differs from ozz's (tinygltf); plan to write a small "fastgltf -> ozz RawSkeleton/RawAnimation" bridge using ozz's offline library rather than shelling out. Confidence: high on capabilities; medium on integration effort.
- Morph targets are *not* handled by ozz; keep a simple `MorphController` (weights array, animated by glTF `weights` channels).

### 4.1 Alternatives to ozz considered
| Option | Pros | Cons |
|---|---|---|
| Hand-rolled sampler on glTF channels | Zero dependency; direct use of glTF keyframes; CUBICSPLINE natively | Must write blending, additive layers, IK, compression ourselves; AoS quaternion slerp per joint is slower than ozz SoA |
| ufbx animation evaluation | Built-in `ufbx_evaluate_*` and CPU skinning for FBX scenes [A7] | FBX-only, not a general runtime |
| Assimp | Loads animation data for many formats [A8] | No runtime sampling/blending; heavy |
| ozz-animation | Runtime blending/IK/compression, MIT, SIMD, importer tools [A15] | Own data format and joint ordering; needs import bridge |

Decision: hand-rolled sampling is acceptable for 0.2 if ozz integration slips (clips are usually short loops for visuals), but ozz is the target because beat-synced crossfades and additive layers are core to a live tool.

---

## 5. Asset management patterns

### 5.1 Handles instead of pointers
- floooh's design: a handle is an integer packing an *index* into a typed pool and a *generation* counter; destroying an item bumps the generation so stale handles fail validation (dangling detection); pools keep items contiguous, memory ownership stays inside the owning system. [A21]
- Bevy: `Handle<T>` is a reference-counted smart handle; `Assets<T>` uses generational indexing with slot reuse; `AssetPath` (virtual path + optional label) identifies sources; file_watcher feature drives hot reload via asset events; IO on a task pool. [A22]
- Unreal: `FPrimaryAssetId {Type, Name}` for stable IDs; `FStreamableManager` for async loading returning `FStreamableHandle`. [A23]

### 5.2 Recommended design for av-gen
```
struct AssetHandle { uint32 index:20; uint32 gen:12; };  // per-type pools (Mesh, Texture, Shader, Scene, Clip, IBL)
```
- **Registry** per asset type: `pool<T>`, `state[]` (Unloaded / Loading / Ready / Failed), `refcount[]`, `source` (path + GUID), `version` counter for hot reload consumers.
- **Identity**: project files store `{ "guid": "uuid-v4", "path": "assets/models/tree.glb" }`. GUID is the stable key (survives renames); path is the human-readable, git-friendly locator, **relative to the project root** so projects are relocatable (Bevy `AssetPath`, Godot `res://`, Unity meta-GUID all follow this shape). On load: resolve by GUID from the manifest, fall back to path, warn on mismatch. Do not embed absolute paths.
- **Async loading**: worker task pool does IO + decode + CPU processing (meshoptimizer, Basis transcode, ozz build); the render thread only performs GPU uploads (Metal `MTLBuffer/MTLTexture` creation can happen off-thread, but keep a single upload queue for simplicity in 0.2). Loading state is polled per frame; placeholders (magenta texture, unit cube, fallback shader) are bound until Ready, so a show never stalls.
- **Hot reload**: efsw watcher on the project asset dirs (see shaders.md S31); on change, load into a *new* pool slot, then swap the registry entry so all handles observe the new version; retire the old GPU resources after in-flight frames. Preserve user parameter values across reload by name.
- **Dependencies**: a `.glb` may reference external images/buffers, KTX2s reference nothing, ozz clips reference a skeleton; keep an explicit dependency list per asset so that reloading a texture re-validates materials but not meshes.
- **Cooking**: 0.2 stays source-only (load `.glb/.png/.exr/.ktx2` directly). A later "cook" step (gltfpack, cmgen, ktx encode) can write an `.avcache/` directory keyed by content hash.

### 5.3 Project file shape (proposal)
```json
{
  "version": 1,
  "assets": [
    { "guid": "2f1c9a0e-...", "type": "scene",   "path": "assets/models/stage.glb",
      "import": { "lods": [1.0, 0.5, 0.25], "generateTangents": true } },
    { "guid": "8b77e1d2-...", "type": "texture", "path": "assets/tex/noise.ktx2",
      "import": { "srgb": false, "mips": true } },
    { "guid": "c0ffee00-...", "type": "ibl",     "path": "assets/env/studio/",
      "import": { "source": "assets/env/studio.hdr", "size": 256 } },
    { "guid": "51ab3c44-...", "type": "shader",  "path": "assets/shaders/plasma.fs" }
  ],
  "graph": { "nodes": [ { "id": 1, "type": "MeshRenderer", "scene": "2f1c9a0e-..." } ] }
}
```
- Nodes reference assets by GUID only; the `assets` table is the single place paths live, so a move/rename is a one-line change and the importer can offer "relink missing asset".
- `import` settings live next to the path (Unity `.meta`/Godot `.import` style) so re-import after a source file change is deterministic.
- Confidence: this is a design proposal informed by [A21][A22][A23], not a cited fact.

### 5.4 Loading-time expectations (to guide async design)
- fastgltf parses JSON with simdjson and only touches buffers on demand; multi-MB GLBs parse in single-digit milliseconds, so scene *structure* can be available almost immediately while buffers/images stream in. [A3]
- meshoptimizer decoding runs at ~1 GB/s for EXT_meshopt buffers; vertex cache/fetch optimization is O(n) and fast, but the simplifier for LODs can take tens to hundreds of ms per large mesh; run LOD generation off-thread and cache. [A6][A6b]
- Basis transcoding is the slowest CPU step (ETC1S -> ASTC is fast, UASTC -> BC7 is heavier); keep it on workers and prefer UASTC-HDR-as-ASTC or pre-transcoded caches on Apple. [A13]
- EXR decode of a 4k HDRI with PIZ compression is hundreds of ms single-threaded in tinyexr; enable its threaded path. [A10]

---

## 6. HDR environment maps and IBL preprocessing

### 6.1 Pipeline
1. Load equirectangular `.hdr` (stb_image `stbi_loadf`) or `.exr` (tinyexr). [A9][A10]
2. Equirect -> cubemap (6 x N^2 sampling with inverse spherical mapping; do on GPU with a compute kernel or offline).
3. Specular prefilter: for each roughness level (mip), importance-sample the GGX lobe (split-sum approximation, Karis 2013) -> mip chain of the cubemap.
4. Diffuse irradiance: either an irradiance cubemap (cosine convolution) or **spherical harmonics order 3 (9 RGB coefficients)**, which is what Filament ships. [A18]
5. DFG/BRDF LUT (2D, roughness x NdotV), generated once.

### 6.2 Tools
| Tool | Status | Inputs | Outputs | License |
|---|---|---|---|---|
| Filament `cmgen` | Maintained with Filament | equirect (PNG/HDR/PSD/EXR), cross/strip cubemaps | mipmapped prefiltered cubemap in KTX/EXR/HDR/DDS/PNG; SH coefficients (`--sh-shader` emits shader code); DFG LUT; `--deploy` writes a full IBL package; `--size` default 256, `--ibl-samples` default 1024 | Apache 2.0 [A18] |
| cmft | Unmaintained (last activity ~2015) | equirect/cubemap/cross/strip in DDS/KTX/HDR/TGA | radiance/irradiance filtered cubemaps; OpenCL + multicore CPU; Phong/Blinn BRDF lobes (not GGX) | BSD-2 [A19] |
| IBLBaker | 2013-era Windows/D3D11 demo app, sporadic maintenance | HDR env | diffuse irradiance + specular prefiltered cubemaps; user BRDF | MIT [A20] |

### 6.3 SH vs irradiance cubemap, and other runtime choices
- **SH order 3 (9 RGB coefficients)**: 27 floats in a uniform block, evaluated per pixel with ~15 MADs; exact for the cosine lobe up to low-frequency content, ringing on very high-contrast HDRIs (sun disks); this is Filament's shipping choice and what cmgen emits. [A18]
- **Irradiance cubemap (e.g. 32^2 faces)**: one texture fetch, no ringing, trivially generated by a compute convolution; costs a sampler slot and ~24 KB. Either is fine; SH is preferable when the same environment is applied to thousands of instances (no texture dependency) and for blending between environments (lerp coefficients).
- **Specular prefilter resolution**: 256^2 with 6-8 mip levels (roughness mapped to mip) is standard; use `--ibl-samples 1024` offline, 64-256 samples with mip-filtered importance sampling on the GPU for live prefiltering.
- **Rotation**: environment rotation to sync with camera/stage is a per-frame 3x3 in the shader for both SH (rotate coefficients or the lookup direction) and cubemaps.
- **Sky rendering**: draw the unfiltered mip-0 cubemap on a fullscreen triangle after opaques, or use a tone-mapped equirect directly.

Recommendation: standardize on cmgen output (KTX2 cubemap with mips + SH text) checked into the project as assets; write the GPU-side prefilter in our own compute shader later so live-loaded HDRIs can be used at show time (M2 Max can prefilter a 256^2 cubemap in a few ms). Note cmgen's KTX output is KTX1; convert to KTX2 with `ktx create` or load KTX1 via libktx (which supports both). Confidence: high on cmgen capabilities, medium on cmft/IBLBaker "unmaintained" judgement (based on repo activity summaries).

---

## 7. Format and library decision table

| Need | Choice | Alt | Rationale |
|---|---|---|---|
| 3D scenes/meshes | glTF 2.0 (.glb) via fastgltf | cgltf | Standard, extensions, MIT loaders [A2][A3][A4] |
| Mesh optimize/LOD/decode | meshoptimizer | - | MIT, v1.2, decodes EXT_meshopt [A6] |
| FBX | ufbx (plugin, later) | Assimp | Single file, MIT, robust; Assimp too heavy [A7][A8] |
| OBJ | ufbx (has OBJ) or tinyobjloader | - | [A7][A24] |
| LDR images | stb_image | libpng/libjpeg-turbo | Zero-dep; upgrade if throughput needed [A9][A25] |
| HDR images | stb_image (.hdr) + tinyexr (.exr) | OpenEXR | Cover 95% of HDRIs; OpenEXR for DWAA [A9][A10][A11] |
| GPU textures | KTX2 + Basis via libktx transcoder | raw DDS reader | Apple-native ASTC, desktop BC7; single container [A12][A13] |
| Skeletal animation | ozz-animation | hand-rolled | MIT, SIMD, blending/IK, v0.17 [A15] |
| Morph targets | in-house | - | trivial [A17] |
| IBL | cmgen offline (+ own GPU prefilter later) | cmft | Maintained, GGX split-sum, SH [A18] |
| Asset identity | GUID + project-relative path | path only | Renames survive, projects relocatable [A22][A23] |
| Asset lifetime | generational handles + refcount | shared_ptr | Cache-friendly, dangling-safe [A21] |

---

## 8. Milestone plan

### Milestone 0.1 (no external asset libraries)
- Procedural geometry generators: fullscreen triangle, quad, box, UV sphere/icosphere, torus, cylinder, plane grid, line/point clouds; all produce our internal `MeshData {positions, normals, uvs, tangents?, indices}` in glTF conventions (+Y up, right-handed, CCW front faces, meters) so 0.2 glTF imports drop in without a coordinate flip.
- Procedural textures: value/gradient noise, checker, gradients, 1D LUTs generated on CPU or by compute; a 1x1 white/black/normal placeholder set.
- Shader assets only (see shaders.md). Asset registry skeleton with handles, GUID/path records, and hot reload for shaders so the design is exercised before mesh/texture assets arrive.
- Rationale: keeps 0.1 focused on the render loop, Metal backend, audio->parameter plumbing and the shader pipeline; every library added later is behind an interface defined now.

### Milestone 0.2 (planned set)
- **fastgltf** (or cgltf) + **meshoptimizer**: `.glb/.gltf` scenes, nodes, PBR materials, KHR_texture_transform, KHR_lights_punctual, KHR_materials_emissive_strength, KHR_mesh_quantization, EXT_meshopt_compression; skins + morph targets + animations parsed.
- **stb_image**, **tinyexr**: PNG/JPEG/TGA/HDR/EXR to RGBA8/RGBA16F/RGBA32F with mip generation on GPU.
- **libktx** (transcoder-only build) for `.ktx2` + KHR_texture_basisu; ASTC on Apple, BC7/BC6H elsewhere; custom Metal upload path.
- **ozz-animation** runtime + import bridge; GPU skinning; morph target compute pre-pass.
- **IBL assets** produced by cmgen (`--deploy`), loaded as KTX cubemap + SH; environment lighting in the PBR shader.
- Async loading + placeholders; file-watcher hot reload for textures/meshes.
- Deferred to 0.3+: ufbx/FBX import, Draco, KHR_materials_transmission/volume rendering passes, runtime HDRI prefilter, cooked asset cache, USD.

---

## 9. Integration notes for 0.2

### 9.1 CMake / dependency acquisition
All chosen libraries are CMake-friendly or single-file; plan to vendor via `FetchContent` pinned to tags (or git submodules) rather than system packages, so Windows/Linux builds are reproducible.

| Library | Acquisition | Build shape | Pin |
|---|---|---|---|
| fastgltf | FetchContent, `spnda/fastgltf` | static lib target `fastgltf::fastgltf`; bundles simdjson | v0.9.0 [A3] |
| cgltf | copy `cgltf.h` (+ `cgltf_write.h`) | header-only, `#define CGLTF_IMPLEMENTATION` in one TU | v1.15 [A4] |
| meshoptimizer | FetchContent, `zeux/meshoptimizer` | static lib `meshoptimizer`; optional `gltfpack` tool target | v1.2 [A6] |
| stb_image | copy `stb_image.h` | header-only, `STB_IMAGE_IMPLEMENTATION` once; set `STBI_NEON` on arm64 | v2.30 [A9] |
| tinyexr | copy `tinyexr.h` + `miniz.c/.h` | header-only + miniz TU; or link system zlib | latest master [A10] |
| libktx | FetchContent, `KhronosGroup/KTX-Software` with `KTX_FEATURE_TOOLS=OFF`, `KTX_FEATURE_TESTS=OFF`, `KTX_FEATURE_GL_UPLOAD=OFF`, `KTX_FEATURE_VK_UPLOAD=OFF` | static `ktx` lib (includes basisu transcoder, zstd) | v4.4.2 (or v5.0.0 when final) [A12] |
| ozz-animation | FetchContent, `guillaumeblanc/ozz-animation` with `ozz_build_tools=OFF`, `ozz_build_samples=OFF`, `ozz_build_fbx=OFF`, `ozz_build_gltf=OFF` | static `ozz_base`, `ozz_animation`, `ozz_animation_offline` (for our own importer bridge) | 0.17.0 [A15] |
| ufbx (0.3+) | copy `ufbx.c/.h` | one C TU | master, semver in header [A7] |
| efsw | FetchContent, `SpartanJ/efsw` | static lib | latest tag (see shaders.md S31) |

Licence summary for the 0.2 set: MIT (fastgltf, cgltf, meshoptimizer, ozz), public domain/MIT (stb), BSD-3 (tinyexr), Apache 2.0 (libktx, basisu). No copyleft. Attribution file required for BSD/Apache components.

### 9.2 glTF -> engine data mapping
- `fastgltf::Asset` -> `SceneAsset { nodes[], meshes[], materials[], textures[], lights[], skins[], animations[], cameras[] }` with our own POD structs; do not leak loader types outside the importer TU.
- Per primitive: interleave to our vertex layout (`pos f32x3, normal oct16, tangent oct16+sign, uv0 f16x2, uv1 f16x2, color u8x4, joints u8x4, weights u8x4`); run meshoptimizer `generateVertexRemap -> optimizeVertexCache -> optimizeOverdraw -> optimizeVertexFetch`; generate tangents (MikkTSpace via meshoptimizer v1.2) if missing; compute LODs with `meshopt_simplify` at 50%/25% triangle targets. [A6]
- Material: metallic-roughness factors/textures, normal scale, occlusion strength, `emissiveFactor * emissiveStrength` (KHR_materials_emissive_strength) stored as linear HDR RGB, alphaMode/cutoff, doubleSided, KHR_texture_transform per slot. [A2b][A2c]
- Lights: convert KHR_lights_punctual units (lux / candela) into our renderer's radiometric convention once; keep artists' intensities intact. [A2d]
- Skins: build ozz `RawSkeleton` from the joint node hierarchy, `RawAnimation` from channels (resample CUBICSPLINE to keyframes at 30/60 Hz since ozz stores keyframes), store inverse bind matrices alongside; joint order remap table saved in the asset. [A15][A16]
- Morph targets: pack all targets' POSITION/NORMAL deltas into one SSBO `[target][vertex]`; a compute pre-pass writes blended positions/normals into the instance's vertex buffer when weights change. [A17]

### 9.3 Texture upload on Metal (no libktx helper)
- Decode/transcode on a worker: for `.ktx2` call `ktxTexture2_NeedsTranscoding` then `ktxTexture2_TranscodeBasis(tex, KTX_TTF_ASTC_4x4_RGBA or KTX_TTF_BC7_RGBA, 0)` based on `MTLDevice.supportsFamily(.apple7)` etc.; iterate `ktxTexture_IterateLevelFaces` to get per-level, per-face pointers/sizes. [A12][A13]
- Create `MTLTextureDescriptor` with matching `MTLPixelFormat` (`astc_4x4_ldr`/`astc_4x4_srgb`, `bc7_rgbaUnorm(_srgb)`, `bc6H_rgbFloat`, `rgba8Unorm_srgb`, `rgba16Float`, `rgb9e5Float`), `mipmapLevelCount`, `textureType` (2D, cube, 2DArray), storage mode `.shared` for staging or use a blit from a shared `MTLBuffer` into a `.private` texture; `replace(region:mipmapLevel:slice:withBytes:bytesPerRow:bytesPerImage:)` with `bytesPerRow` = blocks-per-row * bytes-per-block for compressed formats.
- For LDR PNG/JPEG without mips, upload level 0 and generate mips with `MTLBlitCommandEncoder.generateMipmaps` (only for color-renderable/filterable uncompressed formats).
- Windows/Linux (Vulkan) can use `ktxTexture_VkUpload` directly. [A12]

### 9.4 Risk register
| Risk | Impact | Mitigation |
|---|---|---|
| libktx v5 API churn (`ktxBasisParams`) | Build breaks on upgrade | We only use the transcoder/reader path; encoder params are tool-side. Pin v4.4.2 until v5 final. [A12] |
| fastgltf drops C++17 | None for us (C++20/23 planned) | Track releases; cgltf as fallback. [A3] |
| tinyexr lacks DWAA/B44 | Some downloaded HDRIs fail | Detect compression in header; message user; optional OpenEXR backend later. [A10][A11] |
| Assimp/OpenEXR CVE cadence | Attack surface for untrusted files | Avoid Assimp; sandbox decode on a worker; fuzz our importer entry points. [A8][A11] |
| ozz joint order vs glTF joint order | Skinning bugs | Store remap table; unit-test with Khronos sample models (RiggedFigure, CesiumMan, Fox). [A15][A16] |
| cmgen outputs KTX1 | Loader mismatch | libktx reads KTX1 and KTX2; or convert with `ktx create`. [A12][A18] |
| Coordinate conventions between procedural (0.1) and glTF (0.2) | Flipped models | Adopt glTF conventions in 0.1 (Section 8). [A1] |

### 9.5 Test corpus
- Khronos glTF-Sample-Assets (Apache 2.0 / CC) for extension coverage: `EmissiveStrengthTest`, `TextureTransformTest`, `LightsPunctualLamp`, `TransmissionTest`, `AnimatedMorphCube`, `RiggedFigure`, `Fox`, meshopt/basisu variants of `Sponza`/`Bistro` via gltfpack. Confidence: high that these exist; enumerate exact names when building the test suite.
- HDRIs: Poly Haven (CC0) `.hdr` and `.exr` files for IBL tests.

---

## 10. Open questions
- fastgltf's next major will require C++20; we build with clang 21 so this is fine, but confirm CMake 4.0 + C++20 modules are not forced on.
- libktx v5 final vs v4.4.2: rc2 changes `ktxBasisParams` API; pin to v4.4.2 for 0.2 unless UASTC HDR is needed, then move to v5 when it ships.
- Verify Metal feature-set tables for BC6H/BC7 on Apple-silicon macOS (believed supported) to decide default transcode targets.
- Decide whether IBL SH is evaluated per-pixel in the shader (9 vec3 uniforms) or baked into a tiny irradiance cubemap.

---

## Sources

- **[A1]** Khronos glTF 2.0 Specification, https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html (fetch returned 403; content corroborated via the official glTF-Tutorials repository [A16][A17] and extension READMEs). Accessed 2026-09-08. Learned: coordinate system, PBR MR model, skins, morph targets, animation interpolation, GLB container. Confidence: high for well-known spec facts; the page itself could not be quoted directly.
- **[A2]** KhronosGroup/glTF extensions README, https://github.com/KhronosGroup/glTF/blob/main/extensions/README.md. Accessed 2026-09-08. Learned: 25 ratified KHR extensions (materials_*, texture_basisu, texture_transform, draco, mesh_quantization, animation_pointer, interactivity, lights_punctual, node_*, gaussian_splatting, xmp_json_ld); RC: materials_diffuse_transmission; review draft: physics_rigid_bodies, collision_shapes; ratified EXT: mesh_gpu_instancing, meshopt_compression, texture_webp. Confidence: high.
- **[A2b]** KHR_materials_emissive_strength README, https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_materials_emissive_strength/README.md. Accessed 2026-09-08. Learned: `emissiveStrength` multiplier, default 1.0, ratified, incompatible with unlit. Confidence: high.
- **[A2c]** KHR_texture_transform README, https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_texture_transform/README.md. Accessed 2026-09-08. Learned: offset/rotation/scale/texCoord, T*R*S order, ratified. Confidence: high.
- **[A2d]** KHR_lights_punctual README, https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_lights_punctual/README.md. Accessed 2026-09-08. Learned: light types, units (lux/candela), range, cone angles, -Z direction, ratified. Confidence: high.
- **[A2e]** KHR_materials_transmission README, https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_materials_transmission/README.md. Accessed 2026-09-08. Learned: transmissionFactor/Texture, opaque alphaMode, rendering implications, ratified 2020. Confidence: high.
- **[A3]** spnda/fastgltf releases via GitHub API, https://api.github.com/repos/spnda/fastgltf/releases and docs https://fastgltf.readthedocs.io/latest/. Accessed 2026-09-08. Learned: v0.9.0 published 2025-07-08 (Draco, physics_rigid_bodies, GODOT_single_root, C++23 monadics, "likely last C++17 version"); v0.8.0 2024-07-25; MIT; simdjson dependency; SIMD parsing; exporter. Confidence: high. Note: the HTML releases page fetch mis-reported years; the API dates are authoritative.
- **[A4]** jkuhlmann/cgltf README and releases API, https://github.com/jkuhlmann/cgltf, https://api.github.com/repos/jkuhlmann/cgltf/releases. Accessed 2026-09-08. Learned: MIT single-header C99, jsmn embedded, API functions, supported extension list, v1.15 (2025-02-09) added KHR_materials_diffuse_transmission and EXT_texture_webp; users bgfx/Filament/raylib/Unigine. Confidence: high.
- **[A5]** syoyo/tinygltf README and releases API, https://github.com/syoyo/tinygltf, https://api.github.com/repos/syoyo/tinygltf/releases. Accessed 2026-09-08. Learned: v3 is pure C11 with optional C++20 facade; v2 C++ moved to attic; v3.0.1 (2026-08-02), v3.0.0 (2026-03-23) custom SIMD JSON parser; MIT; arena allocator; fuzzing. Confidence: high.
- **[A6]** zeux/meshoptimizer releases via GitHub API, https://api.github.com/repos/zeux/meshoptimizer/releases. Accessed 2026-09-08. Learned: v1.2 (2026-06-30) MikkTSpace tangents, faster decode; v1.1 (2026-04-02) meshlet codec, opacity micromaps; v1.0 (2025-12-08) clusterlod.h, stabilized APIs; v0.25 (2025-08-20) simplifyWithUpdate, WebP in gltfpack. MIT. Confidence: high.
- **[A6b]** EXT_meshopt_compression README, https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Vendor/EXT_meshopt_compression/README.md. Accessed 2026-09-08. Learned: modes ATTRIBUTES/TRIANGLES/INDICES, filters OCTAHEDRAL/QUATERNION/EXPONENTIAL, fallback buffers, interaction with KHR_mesh_quantization, ~1 GB/s decode with meshoptimizer, ratified. Confidence: high.
- **[A7]** ufbx/ufbx README, https://github.com/ufbx/ufbx (releases API empty). Accessed 2026-09-08. Learned: MIT or Unlicense; single ufbx.c/ufbx.h; FBX binary/ASCII from v3000; meshes, skinning, blend shapes, NURBS, lights, cameras, animation evaluation, embedded textures, geometry caches, OBJ/MTL; thread-safe under C11; 592 tests, 95% branch coverage; semver on master. Confidence: high.
- **[A8]** assimp/assimp releases via GitHub API, https://api.github.com/repos/assimp/assimp/releases. Accessed 2026-09-08. Learned: v6.0.5 (2026-04-30) security hardening, aiBuffer, USD skinned mesh, VRML; v6.0.4 (2026-01-24); v6.0.3 (2026-01-19) FBX base64 overflow and double-free fixes; BSD-3 (+ISC). Confidence: high.
- **[A9]** nothings/stb stb_image.h header, https://github.com/nothings/stb/blob/master/stb_image.h. Accessed 2026-09-08. Learned: v2.30 (2024-05-31); format list and limitations; `stbi_loadf`, `stbi_load_16`, `stbi_hdr_to_ldr_gamma/scale`, `stbi_info`; public domain/MIT; STBI_MAX_DIMENSIONS; SSE2/NEON. Confidence: high.
- **[A10]** syoyo/tinyexr README, https://github.com/syoyo/tinyexr. Accessed 2026-09-08. Learned: BSD-3; header-only v1 with miniz/zlib; scanline+tiled, HALF/FLOAT/UINT, multipart, deep read; NONE/RLE/ZIP/ZIPS/PIZ/ZFP; no PXR24/B44/DWAA/DWAB; LoadEXR/LoadEXRWithLayer/ParseEXRHeaderFromFile; threads/OpenMP; fuzzed. Confidence: high.
- **[A11]** AcademySoftwareFoundation/openexr releases, https://github.com/AcademySoftwareFoundation/openexr/releases. Accessed 2026-09-08. Learned: v3.4.15 (2026-08-21) and v3.3.14 IDManifest fixes; v3.4.14 (2026-08-07) 15 CVEs; deps Imath, libdeflate, vendored OpenJPH; BSD-3; OpenEXRCore C API. Confidence: high.
- **[A12]** KhronosGroup/KTX-Software releases (API and page) and README, https://api.github.com/repos/KhronosGroup/KTX-Software/releases, https://github.com/KhronosGroup/KTX-Software/blob/main/README.md. Accessed 2026-09-08. Learned: v5.0.0-rc2 (2026-08-17) UASTC HDR, ktxBasisParams API breaks, legacy tools removed, Darwin arm64 binaries, pending spec rev 5; v4.4.2 (2024-10-04) stable; libktx features (KTX2, ETC1S/UASTC transcode, Zstd/ZLIB), OpenGL/Vulkan upload helpers only, no Metal helper; Apache 2.0; JS/Java/Python bindings. Confidence: high (Metal helper absence verified against README only).
- **[A13]** BinomialLLC/basis_universal README and releases, https://github.com/BinomialLLC/basis_universal, https://api.github.com/repos/BinomialLLC/basis_universal/releases; KHR_texture_basisu README https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_texture_basisu/README.md. Accessed 2026-09-08. Learned: Apache 2.0; v2.50 (2026-08-03) XUASTC, XUBC7, DDS; encoder/transcoder split, transcoder single .cpp; ETC1S/UASTC LDR/UASTC HDR 4x4 & 6x6; targets BC1-7/ASTC/ETC/PVRTC/BC6H; Apple: ASTC 4x4 native, UASTC HDR 4x4 is standard ASTC HDR; KTX2 constraints (multiples of 4, mip pyramid, ETC1S for color, UASTC for non-color). Confidence: high.
- **[A14]** KTX-Software release notes page, https://github.com/KhronosGroup/KTX-Software/releases. Accessed 2026-09-08. Learned: v4.4.0 aligned with KTX spec revision 4; `ktx compare`; rewritten JS binding. Confidence: high.
- **[A15]** guillaumeblanc/ozz-animation README and releases API, https://github.com/guillaumeblanc/ozz-animation, https://api.github.com/repos/guillaumeblanc/ozz-animation/releases. Accessed 2026-09-08. Learned: MIT; sampling/blending/IK; SoA SIMD; C++17 runtime; gltf2ozz/fbx2ozz; v0.17.0 (2026-08-01) IK NaN fix, rest-pose utility, tinygltf update; v0.16.0 (2025-01-19) root motion; v0.15.0 (2024-04-13) iframes; CPU skinning job and GPU matrices. Confidence: high.
- **[A16]** Khronos glTF-Tutorials, "Skins", https://github.com/KhronosGroup/glTF-Tutorials/blob/main/gltfTutorial/gltfTutorial_020_Skins.md. Accessed 2026-09-08. Learned: joints, inverseBindMatrices, JOINTS_0/WEIGHTS_0, 4 influences per set with JOINTS_1 extension, joint matrix formula, vertex shader blending. Confidence: high.
- **[A17]** Khronos glTF-Tutorials, "Morph Targets", https://github.com/KhronosGroup/glTF-Tutorials/blob/main/gltfTutorial/gltfTutorial_018_MorphTargets.md. Accessed 2026-09-08. Learned: targets array with displacement accessors, mesh/node weights, "weights" animation path, blend formula. Confidence: high.
- **[A18]** google/filament tools/cmgen README, https://github.com/google/filament/blob/main/tools/cmgen/README.md. Accessed 2026-09-08. Learned: inputs (equirect/cross, PNG/HDR/PSD/EXR), outputs (mipmapped prefiltered cubemap KTX/EXR/HDR/DDS/PNG, SH 9 coefficients via --sh-shader, DFG LUT), options (--format, --size 256, --ibl-ld, --ibl-samples 1024, --extract, --deploy); standalone CLI; Filament is Apache 2.0. Confidence: high.
- **[A19]** dariomanesku/cmft README, https://github.com/dariomanesku/cmft. Accessed 2026-09-08. Learned: radiance/irradiance filtering, equirect/cross/strip conversion, OpenCL + CPU, DDS/KTX/HDR/TGA, phong/blinn lobes, BSD-2, active through ~2015. Confidence: high on features; medium on "unmaintained" (inferred from activity).
- **[A20]** derkreature/IBLBaker README, https://github.com/derkreature/IBLBaker. Accessed 2026-09-08 (via search summary). Learned: 2013-era implementation of the UE4 PBR course notes, bakes diffuse irradiance + roughness-mip specular cubemaps; MIT; Windows/D3D11. Confidence: medium.
- **[A21]** Andre Weissflog, "Handles are the better pointers" (2018-06-17), https://floooh.github.io/2018/06/17/handles-vs-pointers.html. Accessed 2026-09-08. Learned: index+generation handles, dangling detection, pools, system-owned memory. Confidence: high.
- **[A22]** Bevy asset system overview (DeepWiki summary of bevyengine/bevy), https://deepwiki.com/bevyengine/bevy/4-asset-system and https://deepwiki.com/bevyengine/bevy/4.1-asset-loading-and-handles. Accessed 2026-09-08. Learned: Handle<T> refcounted, generational Assets<T> storage, AssetPath, IoTaskPool async loading, file_watcher hot reload. Confidence: medium (secondary summary of source).
- **[A23]** Unreal Asset Manager references: Tom Looman, "Asset Manager for Data Assets & Async Loading", https://tomlooman.com/unreal-engine-asset-manager-async-loading/; ikrima.dev asset manager notes, https://ikrima.dev/ue4guide/gameplay-programming/asset-manager/. Accessed 2026-09-08. Learned: FPrimaryAssetId {Type, Name}, FStreamableManager/FStreamableHandle async loading. Confidence: medium.
- **[A24]** tinyobjloader/tinyobjloader README, https://github.com/tinyobjloader/tinyobjloader. Accessed 2026-09-08 (via search summary). Learned: MIT; v2.0 RC on release branch; 2026-06-19 pure C11 `tiny_obj_c` + `tobj_tess`; 2026-05-22 `LoadObjOpt` SIMD/multithread. Confidence: medium.
- **[A25]** libjpeg-turbo releases https://github.com/libjpeg-turbo/libjpeg-turbo/releases (3.2.0, 2026-06-30); libpng https://github.com/pnggroup/libpng/releases and discussion #761 (1.6.58, 2026-04-15; 1.6.51 "most critical update in decades"); lodepng https://github.com/lvandeve/lodepng. Accessed 2026-09-08 (via search summaries). Confidence: medium.
- **[A26]** Microsoft, "Programming Guide for DDS", https://learn.microsoft.com/en-us/windows/win32/direct3ddds/dx-graphics-dds-pguide; septag/dds-ktx https://github.com/septag/dds-ktx; DirectXTK DDSTextureLoader wiki. Accessed 2026-09-08 (via search summaries). Learned: DDS header + DX10 header (DXGI_FORMAT), BC1-7, single-header no-alloc reader exists. Confidence: medium.
