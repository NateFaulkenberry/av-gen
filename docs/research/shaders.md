# Shader Languages, Toolchains, Reflection, Hot Reload, and User-Exposed Parameters

Research note for the av-gen real-time audiovisual engine. Written 2026-09-08. All URLs accessed 2026-09-08 unless noted.

Target context: native C++ engine, macOS first (macOS 26, Apple M2 Max, Metal 4), Windows/Linux must remain possible. Custom shaders must eventually be a first-class, user-droppable feature whose parameters are exposed to the application UI.

Citation convention used throughout: each important claim carries a `[Sn]` marker pointing to the Sources list at the end. Each source entry records URL, date accessed, what was learned, relevance, and confidence/limitations.

---

## 1. Executive summary / recommendations

1. **Author shaders in one language, compile to SPIR-V, cross-compile to the backend.** SPIR-V is the only IR with a mature, multi-vendor ecosystem of front-ends (glslang, DXC, Slang, naga), optimizers (SPIRV-Tools), reflectors (SPIRV-Reflect, SPIRV-Cross) and back-ends (SPIRV-Cross -> MSL/HLSL/GLSL, naga/Tint -> WGSL). Every serious cross-platform engine surveyed here (sokol, bgfx, SDL_shadercross, Island, Filament) converges on this. [S2][S4][S6][S7][S28]
2. **For the Metal backend, use SPIRV-Cross to emit MSL, then compile MSL at runtime with `MTLDevice.makeLibrary(source:options:)`** during development/hot reload, and precompile to `.metallib` (`xcrun metal` / `xcrun metallib`) for shipping. This is the path with the longest production track record (Roblox, sokol, bgfx, SDL3 GPU). [S2][S12][S13][S27]
3. **Authoring language for engine-owned shaders: Slang, with GLSL 450 (Vulkan-flavoured) as the fallback/user language.** Slang is Khronos-hosted, Apache-2.0-with-LLVM-exception, ships in the Vulkan SDK, has the best reflection API of any option (including user-defined attributes usable for UI metadata), modules, and emits SPIR-V, HLSL, MSL and WGSL. Its *direct* Metal target is still officially "work in progress" as of v2026.17, so route Slang -> SPIR-V -> SPIRV-Cross -> MSL until the direct target is proven on our shaders. [S1][S1b][S1c][S1d]
4. **User-droppable shaders: adopt an ISF-style contract** (JSON metadata block + GLSL fragment body, well-known automatic uniforms) as the *user-facing* format, because it is the de facto standard in VJ/media-server software (VDMX, Resolume, Millumin) and thousands of ISF/Shadertoy shaders exist. Internally translate ISF/Shadertoy source into our canonical GLSL 450, compile via glslang -> SPIR-V, reflect with SPIRV-Reflect, and emit MSL via SPIRV-Cross. Parameter metadata comes from the JSON header (ISF) or from reflection plus optional sidecar/inline annotations (native shaders). [S16][S17][S18]
5. **Reflection: SPIRV-Reflect (single .c/.h, Apache-2.0) as the canonical backend-agnostic reflector**, with Metal's `MTLRenderPipelineReflection` used only for validation/debug. Do not depend on Metal reflection for parameter discovery, because it does not exist on other backends and is only available after pipeline creation. [S9][S14]
6. **Hot reload: efsw (MIT) file watcher -> debounce -> recompile off-thread -> validate -> atomic pipeline swap; on error keep the last good pipeline and display a fallback "error" shader with the diagnostic.** [S19][S20][S21]
7. **Compute**: write kernels in the same canonical GLSL/Slang; SPIRV-Cross maps `local_size` to MSL. Note Metal defines threadgroup size at dispatch time, not in the shader; our shader descriptor must carry the workgroup size from reflection (`OpExecutionMode LocalSize`) so the Metal backend can pass `threadsPerThreadgroup`. Indirect dispatch exists on all three APIs. [S22][S23][S24]

---

## 2. Shader languages

### 2.1 GLSL
- Khronos' OpenGL/Vulkan shading language. Vulkan-flavoured GLSL 4.50+ (with `layout(set=, binding=)`, separate `texture2D`/`sampler`) is the lingua franca of SPIR-V toolchains and is what sokol-shdc, bgfx and Island use as input. [S6][S28]
- Pros: enormous body of existing content (Shadertoy, ISF, TouchDesigner all use GLSL); glslang is the reference compiler and is extremely stable. Cons: no modules/generics; combined sampler model differs from Metal/HLSL (SPIRV-Cross handles it); binding model is Vulkan-centric.
- naga's GLSL front-end only supports GLSL 440+ with Vulkan semantics, so if we choose naga anywhere, our canonical GLSL must already be Vulkan-style. [S4]

### 2.2 HLSL
- Microsoft's language; DXC (DirectXShaderCompiler) has a production SPIR-V backend (`-spirv`) that is actively improved (Feb 2026 and Jul 2026 releases both list "SPIR-V backend improvements"). [S5][S5b]
- The glslang HLSL front-end is **deprecated as of April 2026** and will be removed in the next major version; bug reports are no longer accepted. Do not rely on glslang for HLSL. [S3]
- DXC ships as a shared library on macOS via the Vulkan SDK. SDL_shadercross uses DXC for HLSL -> SPIR-V/DXIL. [S7][S26]
- Relevance: HLSL is worth supporting as an *import* format for Windows-oriented content (Notch uses HLSL for custom shaders) but not as our authoring language on a macOS-first project. [S29]

### 2.3 MSL (Metal Shading Language)
- C++14-based language; spec version 4.1 accompanies Metal 4 / macOS 26. Resource binding uses `[[buffer(n)]]`, `[[texture(n)]]`, `[[sampler(n)]]` attributes; argument buffers group resources. [S12][S30]
- Threadgroup size for kernels is set at dispatch time (`threadsPerThreadgroup`), not in the shader source; `maxTotalThreadsPerThreadgroup` is 1024 on Apple silicon; `threadExecutionWidth` is 32. [S22][S23]
- MSL is a *target* language for us, not an authoring language, because it has no non-Apple consumer.

### 2.4 WGSL
- WebGPU's language; `@group/@binding` decorators, `@workgroup_size(x,y,z)` in-shader. Limits: default `maxComputeInvocationsPerWorkgroup` = 256. [S24]
- Relevant only if we ever ship a web build or use wgpu/Dawn as a backend; both naga and Tint can produce WGSL from SPIR-V. [S4][S8]

### 2.5 Slang
- Originated at NVIDIA; Khronos launched the Slang Initiative in Nov 2024 to host it. Apache 2.0 with LLVM exception. [S1c][S1d]
- Targets (per official docs, accessed 2026-09-08): D3D11/D3D12/Vulkan/CUDA "supported"; **Metal, WebGPU, OptiX, CPU are "work in progress"/experimental.** The Metal doc page nonetheless lists broad coverage: entry-point struct flattening, ParameterBlock -> argument buffers, mesh shaders, `SubpassInput` -> `[[color(n)]]`, specialization constants -> `function_constant`; unsupported: ray-tracing acceleration structures, per-sample `SubpassInputMS`, `SV_InnerCoverage`. [S1][S1b]
- Release cadence is roughly biweekly; v2026.17 (2026-09-04) added Metal texture mip-level queries; v2026.14.1 added Metal `printf`; v2026.16 restructured JSON reflection with scopes. Prebuilt binaries for macOS aarch64; included in Vulkan SDK 1.3.296.0+. [S1c]
- Reflection API is the richest of any tool: `ProgramLayout` -> `VariableLayoutReflection` with per-category offsets (Uniform bytes, DescriptorTableSlot, etc.), plus `-reflection-json`. User-defined `[Attribute]`s are reflectable, which is exactly what a "parameter metadata in the shader" workflow wants. [S1e]
- Modules (`import`), generics, interfaces, automatic differentiation.
- Risk: the 2026 Khronos Real-Time Shading Ecosystem Survey notes developers want more confidence in compiler stability. [S1f]

### 2.6 SPIR-V as IR
- Binary IR consumed by Vulkan/OpenCL, produced by glslang, DXC, Slang, naga, Tint. SPIRV-Tools (Apache 2.0) provides `spirv-opt`, `spirv-val`, `spirv-link`, `spirv-reduce`; new SPIR-V versions are supported "within days" via SPIRV-Headers. [S10]
- SPIR-V is the pivot for every cross-compiler below; treat `.spv` (plus reflection JSON) as our on-disk compiled shader artifact.

---

## 3. Cross-compilation toolchains (status as of 2026-09-08)

| Tool | Role | License | Latest seen | Maintenance | Notes |
|---|---|---|---|---|---|
| glslang | GLSL/ESSL -> SPIR-V (HLSL deprecated) | Multiple (BSD/MIT/Apache; see LICENSES dir) | 16.5.0 (2026-08-03) | Active, Khronos | C++ `TShader/TProgram` and C API `glslang_c_interface.h` [S3][S3b] |
| shaderc | Wrapper over glslang + SPIRV-Tools; `glslc` CLI + `libshaderc` | Apache 2.0 | No GitHub releases; tracks Vulkan SDK | Google-maintained, "no anticipated breaking changes" | Adds `#include` handling; ships in Android NDK [S3c] |
| SPIRV-Tools | opt/val/link/reduce | Apache 2.0 | Vulkan SDK tags | Active, Khronos | Required for `spirv-opt` before MSL emit [S10] |
| SPIRV-Cross | SPIR-V -> GLSL/HLSL/MSL + JSON reflection | Apache 2.0 | tag `vulkan-sdk-1.4.357.0` | Active, Khronos; C API is ABI-stable, C++ API is not | MSL argument buffers via `--msl-argument-buffers`; arrays of resources consume multiple MSL ids [S2][S2b] |
| SPIRV-Reflect | SPIR-V reflection (single .c/.h) | Apache 2.0 | No tagged releases; ships in Vulkan SDK | Active, Khronos | Descriptor bindings, push constants, block member offsets, I/O vars [S9] |
| naga | WGSL/SPIR-V/GLSL -> SPIR-V/MSL/HLSL/GLSL/WGSL | MIT OR Apache 2.0 | wgpu v30.0.1 (2026-08-22) | Active, gfx-rs | Rust; `naga-cli` for offline; GLSL front-end 440+ Vulkan only; MSL back-end "primary" [S4][S4b] |
| Tint | WGSL/SPIR-V -> SPIR-V/MSL/HLSL/GLSL/WGSL | BSD-3 (Chromium) | Rolling with Dawn | Active, Google | Needs depot_tools/gclient standalone; `tint_cmd`; sokol-shdc uses it for WGSL [S8][S6] |
| Slang | Slang/HLSL-like -> SPIR-V/DXIL/MSL/WGSL/GLSL/CUDA | Apache 2.0 w/ LLVM exception | v2026.17 (2026-09-04) | Very active, Khronos-hosted | Direct Metal target "work in progress" [S1][S1c] |
| DXC | HLSL -> DXIL/SPIR-V | LLVM (NCSA) | v1.9.2607 (2026-07-29) | Active, Microsoft | macOS dylib in Vulkan SDK [S5][S26] |
| Apple `metal`/`metallib` | MSL -> AIR -> .metallib | Proprietary (Xcode) | Xcode 26 | Apple | Offline only on Apple platforms [S12] |
| bgfx shaderc | bgfx GLSL dialect -> glsl/essl/spirv/metal/hlsl/pssl | BSD-2 | Rolling | Active | Requires `varying.def.sc`; uniforms must be float; too opinionated for us [S25] |
| sokol-shdc | Annotated GLSL 450 -> glsl/hlsl/msl/wgsl/spirv + bindings + reflection | MIT | Rolling | Active | Uses glslang + SPIRV-Tools + SPIRV-Cross + Tint [S6] |
| SDL_shadercross | SPIR-V/HLSL -> DXBC/DXIL/SPIR-V/MSL/HLSL, runtime or CLI | zlib | No tagged releases (rolling) | Active, SDL | Wraps SPIRV-Cross + DXC; returns SDL_GPU shader objects [S7] |

### 3.1 SPIRV-Cross MSL output quality
- Goal stated by the project is "readable and clean output ... that looks like it was written by a human"; MSL is a first-class target. [S2]
- Roblox's Metal team (zeux, "Three years of Metal") ran glslang -> spirv-opt -> SPIRV-Cross -> MSL in production for years, contributed patches, and found the resulting code "fast and stable". They deliberately did not adopt argument buffers. [S27]
- Known wrinkles: resource arrays consume multiple MSL binding ids (Vulkan uses one), so explicit binding remaps are needed; descriptor-set -> argument-buffer mapping is opt-in; combined image samplers are split into texture + sampler pairs. [S2]
- Confidence: high that this path is production-proven; medium on whether it covers Metal 4-specific features (argument tables, residency sets are host-side, not shader-side, so mostly unaffected).

### 3.2 naga vs Tint vs SPIRV-Cross
- naga is a Rust crate; embedding in a C++ engine means either shelling out to `naga-cli` at build time or building a small C FFI. Its MSL back-end is "primary" and used by wgpu on macOS, so quality is good, but the GLSL front-end is deliberately limited. [S4]
- Tint requires the Chromium build environment for a standalone build, which is heavy for a CMake project. [S8]
- SPIRV-Cross is a plain CMake C++ library with a stable C API. It is the pragmatic choice for a C++ engine.

### 3.3 Metal toolchain specifics
- Offline: `xcrun -sdk macosx metal -c file.metal -o file.air` then `xcrun metallib file.air -o file.metallib`; `-frecord-sources` and `-gline-tables-only` embed sources/line tables for GPU debugger. Load with `makeLibrary(URL:)`, `makeLibrary(data:)` or `makeDefaultLibrary()`. [S12]
- Runtime: `makeLibrary(source:options:)` (sync, throws) and the async completion-handler variant; `MTLCompileOptions` exposes `languageVersion`, `preprocessorMacros`, `mathMode`, `libraryType`, `optimizationLevel`, `enableLogging`. Runtime compilation is "expensive"; Apple recommends precompiling for shipping. [S13]
- Metal 4 (macOS 26): compilation moves to an explicit `MTL4Compiler` object created from the device; it inherits the calling thread's QoS, supports parallel pipeline compilation and pipeline serialization. Pipelines are built from `MTL4LibraryFunctionDescriptor`s, not `MTLFunction`s; argument tables and residency sets replace implicit encoder binding. Metal 3-style `makeLibrary` still exists for library creation; whether we adopt MTL4 pipelines is a backend decision (see the graphics-API research note). [S11][S11b]
- Reflection: `makeRenderPipelineState(descriptor:options:[.bindingInfo,.argumentInfo],reflection:)` returns `MTLRenderPipelineReflection` with `vertexBindings/fragmentBindings` (`MTLBinding`; `MTLBufferBinding.bufferStructType.members` gives name/type/offset). `vertexArguments/fragmentArguments` (`MTLArgument`) are deprecated. Available macOS 11+. [S14]

### 3.4 sokol-shdc in detail (as a reference design)
- Input: "annotated GLSL" (Vulkan GLSL 450), blocks `@vs/@fs/@cs/@program/@block/@include/@ctype/@module/@header/@image_sample_type/@sampler_type/@glsl_options/@hlsl_options/@msl_options`. [S6]
- Outputs (`-l`): glsl410, glsl430, glsl300es, glsl310es, hlsl4, hlsl5, metal_macos, metal_ios, metal_sim, wgsl, spirv_vk. Formats: sokol (C header), sokol_impl, sokol_decl, bare, bare_yaml, and bindings for Zig/Odin/Nim/Rust/D/C3/Jai.
- Reflection: `--reflection` generates runtime query functions; `bare_yaml` emits a YAML description of vertex attributes, uniform blocks (member name/type/offset), textures, samplers, storage buffers, per-stage. Uniform blocks are restricted to float/vec2-4/int/ivec2-4/mat4 (std140), arrays only of vec4/ivec4/mat4. [S6]
- Compute: `@cs` sections; shdc extracts `local_size` from GLSL and passes it to the Metal backend as `mtl_threads_per_threadgroup` because MSL cannot express it. Storage textures were not yet supported at the time of the compute update post. [S6b]
- Lesson: the "single GLSL source + tool-generated descriptor + reflection sidecar" pattern is exactly what we want, minus sokol-specific bindings.

### 3.5 SDL_shadercross
- Takes SPIR-V or HLSL, outputs DXBC/DXIL/SPIR-V/MSL/HLSL; runtime API returns SDL_GPU shader objects directly; also a CLI. Depends on SPIRV-Cross and DXC (dxcompiler + dxil dylibs). zlib license. No tagged releases as of 2026-09-08 (rolling main). [S7]
- Only useful if we build on SDL3 GPU; otherwise it is a thin wrapper we can replicate.

---

## 4. Shader reflection

Goal: given a compiled shader, automatically discover uniforms/parameters, their types, layouts, bindings, and any metadata, for (a) pipeline layout creation and (b) exposing user-editable parameters.

| Mechanism | Backend-agnostic? | What you get | When available |
|---|---|---|---|
| SPIRV-Reflect | Yes (needs SPIR-V) | Descriptor sets/bindings, push constants, UBO/SSBO block member names, types, offsets, array dims, I/O variables, entry points, execution modes; binding remap | Offline or at load, before pipeline creation [S9] |
| SPIRV-Cross reflection | Yes | `ShaderResources` (UBOs, SSBOs, images, samplers, push constants), type queries, decoration edit; JSON output via CLI `--reflect` | Same; already required for MSL emit [S2] |
| Slang reflection | Yes (target-specific layouts on request) | Full type/variable layout per target category, entry point params, user attributes, `-reflection-json` | Compile time [S1e] |
| naga | Yes | IR `Module` with globals/types; not a standalone reflection product | Compile time [S4] |
| Metal `MTLRenderPipelineReflection` | No | Bindings, buffer struct members with offsets, texture/sampler slots | Only after pipeline state creation [S14] |

- Names: SPIR-V from glslang keeps `OpName` debug names unless stripped; `spirv-opt --strip-debug` removes them. Keep names in dev builds; for shipped builds, keep the reflection JSON sidecar and strip the `.spv`.
- Metadata beyond type/offset (min/max/default/UI widget) is *not* representable in SPIR-V decorations in any standard way, so it must come from a sidecar/header (ISF JSON), from Slang user attributes, or from parsed source comments.

---

## 5. Hot reload patterns

Common structure found across Island (Vulkan, MIT), Anton Gerdelan's OpenGL tutorial, MonoGame, gfx-shader-watch, screen-13-hot: [S19][S20][S21]

1. **Detect**: file-system watcher (efsw: C++ cross-platform, FSEvents/kqueue on macOS, inotify on Linux, ReadDirectoryChangesW on Windows, MIT, has C API [S31]) or polling of mtimes. Watch the include/`import` closure too (Island tracks `#include`/`import` dependencies). Debounce ~50-100 ms because editors write files in several steps.
2. **Compile off the render thread**: glslang -> SPIR-V -> spirv-val -> (spirv-opt) -> SPIRV-Cross MSL -> `makeLibrary(source:)` (async variant) -> pipeline state. All of these are thread-safe as separate compiler instances.
3. **Validate** reflection against the previous version: same bindings, or migrate parameter values by name (keep values for parameters that still exist, default new ones).
4. **Swap atomically** at a frame boundary: the render thread reads a `std::atomic<Pipeline*>`/handle; old pipeline is retired after in-flight frames complete (Metal: after the command buffer completion handler; Vulkan: after fence). "hot()/cold()" split (screen-13-hot): `hot()` returns the newest successful build, `cold()` the last known good. [S21]
5. **Error recovery**: on compile failure keep the last good pipeline running (Anton, Shadertoy behaviour) and surface the diagnostics (file:line, context lines like Island) in the UI/log. If there is *no* previous good pipeline (first load fails), bind a built-in **fallback shader** (magenta/checker or "error" overlay) so the graph keeps running. [S19][S20]
6. **Separate code from data**: GPU buffers/textures persist across shader swaps; only pipelines/functions are replaced. [S21]

Metal-specific notes: `makeLibrary(source:options:completionHandler:)` avoids blocking; Metal compiles to AIR then to GPU binary on pipeline creation, so cache pipeline states per (shader, vertex layout, attachment formats). Metal 4's `MTL4Compiler` runs compilations at the calling thread's QoS, so run reload compiles on a utility-QoS thread and interactive ones on user-initiated. [S11]

---

## 6. How existing systems expose shader parameters to users

### 6.1 ISF (Interactive Shader Format) - VDMX, Resolume, Millumin, MadMapper, CoGe
- Spec (MIT, github.com/mrRay/ISF_Spec, current version 2.0) = GLSL fragment shader whose first token is a `/* ... */` comment containing a JSON dictionary. [S16][S17]
- Top-level JSON keys: `ISFVSN` (mandatory spec version, e.g. "2.0"), `VSN`, `DESCRIPTION`, `CREDIT`, `CATEGORIES`, `INPUTS`, `PASSES`, `IMPORTED`. [S16]
- `INPUTS` array entries: `NAME` (no whitespace; becomes the uniform name), `TYPE` in {`event`, `bool`, `long` (menu; needs `VALUES` + `LABELS`), `float`, `point2D`, `color`, `image`, `audio`, `audioFFT`}, optional `DEFAULT`, `MIN`, `MAX`, `IDENTITY`, `LABEL`. Colors use float arrays for min/max/default; `audio`/`audioFFT` use `MAX` as desired sample count and arrive as images. [S16]
- Conventions: filters must have an `image` input named `inputImage`; transitions have `startImage`, `endImage`, `progress`; everything else is a generator. [S16]
- Automatic uniforms injected by the host: `TIME` (float s), `TIMEDELTA`, `RENDERSIZE` (vec2 px), `FRAMEINDEX` (int), `DATE` (vec4 y,m,d,secs), `PASSINDEX` (int), plus varying `isf_FragNormCoord` (vec2, [0,0] bottom-left). [S17]
- Built-in functions the host provides (host find-and-replaces them with correct sampling code for 2D vs rect textures): `IMG_PIXEL(img, px)`, `IMG_NORM_PIXEL(img, uv)`, `IMG_THIS_PIXEL(img)`, `IMG_THIS_NORM_PIXEL(img)`, `IMG_SIZE(img)`. [S17]
- Multi-pass: `PASSES` array of `{TARGET, PERSISTENT, FLOAT, WIDTH, HEIGHT}` where WIDTH/HEIGHT are expressions over `$WIDTH`, `$HEIGHT` and input names; `PERSISTENT` buffers survive frames (feedback); `FLOAT` requests 32-bit targets. Optional matching `.vs` file for a custom vertex shader that must call `isf_vertShaderInit()` first. [S17]
- ISF 1.0 -> 2.0 renamed `vv_*` to `isf_*`, dropped `PERSISTENT_BUFFERS`, added audio types, `IMG_SIZE`, `TIMEDELTA/DATE/FRAMEINDEX`, `VSN/ISFVSN`. [S17]
- Host behaviour: on load the host declares uniforms for all `INPUTS`, generates a vertex shader if missing, declares the automatic uniforms, and rewrites `IMG_*` calls. This is a clean, well-documented contract that we can implement on top of GLSL 450 -> SPIR-V. [S17]
- Relevance: highest of any format for a festival-visuals tool; supporting ISF gives immediate access to the VDMX/Resolume shader ecosystem. Confidence: high (primary spec).

### 6.2 Shadertoy
- Entry point `void mainImage(out vec4 fragColor, in vec2 fragCoord)`; uniforms: `vec3 iResolution`, `float iTime`, `float iTimeDelta`, `float iFrameRate`, `int iFrame`, `float iChannelTime[4]`, `vec3 iChannelResolution[4]`, `vec4 iMouse` (xy current while down, zw click), `samplerXX iChannel0..3` (XX = 2D or Cube), `vec4 iDate`, `float iSampleRate`. Multipass via Buffer A-D + Common + Cubemap + Sound tabs. [S18][S18b]
- Porting pitfalls: fragCoord origin bottom-left, use `iChannelResolution[n]` not `iResolution` for channel sampling, channels may be textures/cubemaps/video/audio/keyboard. [S18b]
- Shadertoy has *no* user parameter mechanism beyond the fixed uniforms; parameters are typically hard-coded `#define`s or driven by `iMouse`. Supporting Shadertoy = providing the uniform set + a wrapper `main()`; parameters would need our own annotation layer.

### 6.3 TouchDesigner GLSL TOP / MAT
- User declares `uniform vec4 uColor;` etc. in GLSL 4.60 (Vulkan-backed since the 2022 release); values come from the TOP's "Vectors 1/2", "Arrays", "Matrices", "Constant Buffers" parameter pages, matched by uniform name. Samplers arrive as `sTD2DInputs[]`, `sTD3DInputs[]`, `sTDCubeInputs[]` arrays by dimensionality; built-in structs `uTD2DInfos[]` (`res` = 1/w,1/h,w,h; `depth`), `uTDOutputInfo`, `uTDCurrentDepth`, `uTDPass`; output via `TDOutputSwizzle()` and, for compute, `TDImageStoreOutput()`. [S15]
- Relevance: demonstrates the "reflect uniforms, show them as parameters keyed by name" approach; no metadata (min/max) in the shader itself; UI is generic.

### 6.4 Notch
- Custom Shader Post Effect / Effector nodes: shader is HLSL; "global single float variables are exposed as properties in the node attributes"; textures declared in the shader appear as input pins; the Effector supports floats, colours, checkboxes, and menus declared via property names in the script. Exposed parameters are then aggregated for media servers via the "Exposed Parameters" workflow. [S29]
- Relevance: same reflect-by-name model as TouchDesigner, HLSL-based, Windows-only.

### 6.5 Unity ShaderLab `Properties`
- Syntax `[attr] _Name ("Display", Type) = default`; types `Integer`, `Int` (legacy), `Float`, `Range(min,max)`, `Color`, `Vector`, `2D`, `2DArray`, `3D`, `Cube`, `CubeArray`; attributes `[HideInInspector] [NoScaleOffset] [Normal] [HDR] [Gamma] [PerRendererData] [MainTexture] [MainColor] [Header] [Space] [Toggle] [Enum] [KeywordEnum] [PowerSlider] [IntRange]`. Properties map to HLSL uniforms of the same name; per-material uniforms must live in one `CBUFFER` for SRP Batcher. [S32]
- Relevance: the most complete *inline* metadata design; `Range`, `[HDR]`, `[Toggle]`, `[Enum]` map directly onto UI widgets.

### 6.6 Unreal material parameters
- Node-graph, not text: `ScalarParameter`, `VectorParameter` (colour picker), `TextureSampleParameter2D`, `StaticSwitchParameter`/`StaticBoolParameter` (compile-time permutations), each with Parameter Name, Group, sort priority, slider min/max; Material Instances (constant/dynamic) override values; `SetScalarParameterValue` at runtime; Material Parameter Collections for globals (up to 1024 scalar + 1024 vector each, two per material). [S33]
- Relevance: the *grouping* and *static switch = permutation* concepts; the global "collection" concept maps to our audio/time globals.

### 6.7 Godot shader `uniform` hints
- `uniform type name : hint = default;` with hints: `source_color`, `hint_range(min,max[,step])`, `hint_enum("A","B:3")`, texture hints `hint_normal`, `hint_default_white/black/transparent`, `hint_anisotropy`, `hint_roughness_*`, `filter_*`, `repeat_*`, `hint_screen_texture`, `hint_depth_texture`, `hint_normal_roughness_texture`; `instance uniform` (per-instance, max 16, no textures), `global uniform` (project-wide); `group_uniforms Name;` / `group_uniforms Name.Sub;` for inspector sections; `/** doc */` comments become tooltips. Set via `set_shader_parameter()`. [S34]
- Relevance: the cleanest *text* syntax for inline metadata; a superset we could adopt in our canonical language via a preprocessor, since neither GLSL nor SPIR-V understands the hints.

### 6.8 Summary matrix

| System | Metadata location | Range/UI hints | Textures as params | Multipass/feedback | Automatic globals |
|---|---|---|---|---|---|
| ISF | JSON header comment | MIN/MAX/DEFAULT/LABEL/VALUES | yes (`image`) | yes (PASSES, PERSISTENT) | TIME, RENDERSIZE, ... |
| Shadertoy | none | none | fixed iChannel0-3 | Buffer A-D | iTime, iResolution, ... |
| TouchDesigner | none (reflection) | none | sTD*Inputs[] | via TOP chains | uTD* |
| Notch | none (reflection) | limited | pins | via node graph | node-provided |
| Unity | ShaderLab Properties | Range, attributes | yes | n/a | Unity globals |
| Unreal | node graph | slider min/max | yes | n/a | collections |
| Godot | inline hints | hint_range, hint_enum | yes with hints | n/a | global uniform |

---

## 7. Recommended "drop a shader + metadata -> app exposes parameters" pipeline

### 7.1 Canonical internal representation
- **`ShaderModule` artifact** = `{ spirv[], reflection.json, params.json, source_hash, stage, entry }`. Everything downstream (Metal, Vulkan, D3D12, WebGPU) is derived from SPIR-V + reflection. This keeps the user-facing pipeline backend-agnostic. [S2][S9]
- **Parameter schema (`params.json`)**, derived from ISF but generalized:
  ```json
  { "name":"speed", "type":"float", "default":1.0, "min":0.0, "max":10.0, "step":0.01,
    "label":"Speed", "group":"Motion", "widget":"slider", "binding":{"block":"Params","offset":16} }
  ```
  Types: `float, vec2, vec3, vec4, int, bool, color(vec4, sRGB flag), point2D, enum(int + labels), event/trigger, image, audio, audioFFT`. Every entry is resolved to a byte offset in a single std140 `Params` uniform block (or push constant) via reflection, so the UI writes into a CPU shadow buffer and the backend uploads it without knowing the semantics.
- **Automatic globals**: one engine-owned uniform block `Globals { time, timeDelta, frame, renderSize, date, passIndex, audio*, beat* }` bound at a fixed set/binding. Provide ISF names (`TIME`, `RENDERSIZE`...) and Shadertoy names (`iTime`, `iResolution`...) as `#define` aliases in the generated prologue.

### 7.2 Ingest paths (all end in canonical GLSL 450 -> glslang -> SPIR-V)
1. **ISF `.fs` (+ optional `.vs`)**: parse JSON header; emit prologue with `#version 450`, globals block, one uniform for each INPUT (scalars packed into the `Params` UBO, `image`/`audio` inputs as `texture2D` + shared sampler), `isf_FragNormCoord` varying, and `IMG_*` helper functions; rewrite `gl_FragColor` -> `layout(location=0) out vec4`; generate the fullscreen-triangle vertex shader if no `.vs`. PASSES map to our render-graph nodes; PERSISTENT -> ping-pong targets; FLOAT -> RGBA16F/32F. params.json is produced directly from INPUTS. [S16][S17]
2. **Shadertoy `.glsl`**: wrap with the iXxx uniform prologue and `void main(){ mainImage(outColor, gl_FragCoord.xy); }`; Buffer A-D become passes; params come only from an optional sidecar JSON (`shader.params.json`) or `//@param` line comments.
3. **Native engine shaders (Slang or GLSL 450)**: parameters are discovered by reflection (SPIRV-Reflect on the `Params` block), and metadata comes from (a) Slang user attributes `[Range(0,10)] [Color] [Group("Motion")]` reflected via the Slang API, or (b) for GLSL, a tiny line-comment DSL `//@range(0,10) @label("Speed")` preceding the member, parsed by our importer (Godot-style hints without needing compiler support). [S1e][S34]
4. **HLSL**: DXC `-spirv` then same path; treat as import-only.

### 7.3 Backend emission
- **Metal**: SPIRV-Cross MSL (`msl_options.platform=macOS, msl_version=3.x/4.x`, explicit binding remaps so `Params`/`Globals` land on fixed `[[buffer(n)]]` slots; textures/samplers assigned by reflected binding), `makeLibrary(source:)` async in dev, cached `.metallib` via `xcrun metal` for shipping. [S2][S12][S13]
- **Vulkan**: SPIR-V directly; layout from SPIRV-Reflect.
- **D3D12**: SPIRV-Cross HLSL -> DXC, or DXC from HLSL where available.
- **WebGPU (future)**: naga-cli or Tint SPIR-V -> WGSL offline. [S4][S8]

### 7.4 Why not Slang-direct-to-Metal today
- Officially "work in progress"; README lists Metal as experimental despite a detailed feature page; recent releases still fix Metal fragment-shader crashes (v2026.14). Re-evaluate quarterly; the moment `slangc -target metal` passes our shader corpus we can drop the SPIRV-Cross hop for engine shaders while keeping it for user GLSL/ISF content. [S1][S1b][S1c]

### 7.5 Why not naga/Tint as the primary cross-compiler
- naga is Rust-only (FFI or CLI), and its GLSL front-end is limited; Tint needs Chromium tooling to build. SPIRV-Cross is a CMake C++ library with a stable C API and is what the Vulkan SDK, sokol, bgfx, SDL and Roblox use. [S2][S4][S8]

---

## 8. Compute shaders across MSL, GLSL and WGSL

| Aspect | GLSL (Vulkan) | MSL | WGSL |
|---|---|---|---|
| Entry | `void main()` with `layout(local_size_x=X, local_size_y=Y, local_size_z=Z) in;` | `kernel void f(...)`; threadgroup size given at dispatch (`threadsPerThreadgroup`), optionally `[[max_total_threads_per_threadgroup(N)]]` | `@compute @workgroup_size(X,Y,Z) fn f(...)` |
| Global id | `gl_GlobalInvocationID` | `[[thread_position_in_grid]]` | `@builtin(global_invocation_id)` |
| Local id | `gl_LocalInvocationID` | `[[thread_position_in_threadgroup]]` | `@builtin(local_invocation_id)` |
| Group id | `gl_WorkGroupID` | `[[threadgroup_position_in_grid]]` | `@builtin(workgroup_id)` |
| Shared mem | `shared` | `threadgroup` address space | `var<workgroup>` |
| Barrier | `barrier()` / `memoryBarrierShared()` | `threadgroup_barrier(mem_flags::mem_threadgroup)` | `workgroupBarrier()` |
| Limits | device `maxComputeWorkGroupInvocations` (>=128 min, typically 1024) | `maxTotalThreadsPerThreadgroup` = 1024 on Apple silicon; `threadExecutionWidth` = 32 | default limit `maxComputeInvocationsPerWorkgroup` = 256; per-dim 256/256/64 |
| Non-uniform grids | dispatch is in whole workgroups; bounds-check in shader | `dispatchThreads` (exact grid, non-uniform groups) or `dispatchThreadgroups` | whole workgroups only |
| Indirect dispatch | `vkCmdDispatchIndirect(buffer, offset)`; 3x uint32 | `dispatchThreadgroups(indirectBuffer:indirectBufferOffset:threadsPerThreadgroup:)` with `MTLDispatchThreadgroupsIndirectArguments {uint threadgroupsPerGrid[3]}` | `dispatchWorkgroupsIndirect(buffer, offset)`; 3x u32 |

Sources: [S22][S23][S24][S6b][S30]

Engineering consequences:
- Because MSL has no in-shader workgroup size, our `ShaderModule` reflection must carry `LocalSize` (SPIRV-Reflect exposes entry-point execution modes) and the Metal backend must pass it to `dispatchThreadgroups`. sokol-shdc does exactly this (`mtl_threads_per_threadgroup`). [S6b]
- Choose 64 or 256 as default 1D threadgroup size (multiples of 32 satisfy Apple's SIMD width and the WebGPU 256 cap). [S22][S24]
- Storage buffers: mark `readonly` vs read/write explicitly in GLSL so the reflector can drive hazard tracking on Metal (which needs explicit `MTLFence`/barriers between compute and render in the same command buffer). [S6b]
- Indirect args layout is 3x uint32 on all three APIs, so a single `DispatchArgs` struct in a GPU buffer is portable. [S23]
- Storage textures (`image2D` / `texture2d<float, access::write>` / `texture_storage_2d`) are supported by SPIRV-Cross -> MSL; sokol notes its own lack of support was a sokol limitation, not a toolchain one. [S6b]

---

## 9. Open questions / follow-ups
- Validate SPIRV-Cross MSL output under Metal 4 argument tables (bindings are by slot index, so should be unaffected).
- Benchmark `makeLibrary(source:)` latency for a 200-line fragment shader on M2 Max to size the hot-reload UX (expect tens to hundreds of ms).
- Decide whether `Params` is a UBO (std140, reflected offsets) or a push constant (Metal `setBytes`); UBO is simpler for hot reload because layout changes are just a new reflection.
- Track Slang Metal target: rerun our corpus against each `v2026.x` release.
- Track glslang HLSL removal (next major after April 2026 deprecation); we do not depend on it.

---

## Sources

- **[S1]** Slang user guide, "Supported Compilation Targets", https://shader-slang.org/slang/user-guide/targets.html. Accessed 2026-09-08. Learned: D3D11/12, Vulkan, CUDA supported; Metal, WebGPU, OptiX, CPU explicitly "work in progress"; Metal output is MSL compiled by Apple's compiler. Relevance: determines whether Slang can be our direct Metal path. Confidence: high (official docs), but page may lag actual compiler state.
- **[S1b]** Slang user guide, "Metal-Specific Functionalities", https://shader-slang.org/slang/user-guide/metal-target-specific. Accessed 2026-09-08. Learned: mapping of entry points, ParameterBlock -> argument buffers, mesh shaders, function constants; unsupported ray tracing structures, per-sample SubpassInputMS. Relevance: what a Slang->MSL path would cover. Confidence: high.
- **[S1c]** shader-slang/slang GitHub releases, https://github.com/shader-slang/slang/releases. Accessed 2026-09-08. Learned: v2026.17 (2026-09-04), v2026.16.1, v2026.16, v2026.14.1 (Metal printf), v2026.14 (Metal fragment param crash fixes, macOS signing); biweekly cadence. Relevance: maturity and velocity. Confidence: high.
- **[S1d]** shader-slang/slang README, https://github.com/shader-slang/slang. Accessed 2026-09-08. Learned: Apache 2.0 w/ LLVM exception; README lists Metal and WebGPU as experimental; prebuilt binaries x86_64/aarch64 Windows/Linux/macOS with slangc, shared lib, slang.h; in Vulkan SDK >= 1.3.296.0; module system with offline-compiled IR. Confidence: high.
- **[S1e]** Slang user guide, "Reflection", https://shader-slang.org/slang/user-guide/reflection.html. Accessed 2026-09-08. Learned: ProgramLayout / TypeLayout / VariableLayout, ParameterCategory offsets, cumulative offset computation, `-reflection-json`, usage tracking via IMetadata. Relevance: metadata extraction for parameter UI. Confidence: high.
- **[S1f]** Khronos, "2026 Real-Time Shading Ecosystem Survey Report", https://members.khronos.org/document/dl/36688 (via search summary; member-gated). Accessed 2026-09-08. Learned: adoption growing; developers want more confidence in compiler stability. Confidence: low-medium (only summary seen).
- **[S2]** KhronosGroup/SPIRV-Cross README, https://github.com/KhronosGroup/SPIRV-Cross. Accessed 2026-09-08. Learned: Apache 2.0; GLSL/HLSL/MSL/JSON outputs; readable output goal; `--msl-argument-buffers`; resource arrays consume multiple MSL ids; C API ABI-stable, C++ API not; reflection API. Relevance: primary MSL emitter. Confidence: high.
- **[S2b]** SPIRV-Cross tags via GitHub API, https://api.github.com/repos/KhronosGroup/SPIRV-Cross/tags. Accessed 2026-09-08. Learned: newest tag `vulkan-sdk-1.4.357.0`. Confidence: high.
- **[S3]** KhronosGroup/glslang README, https://github.com/KhronosGroup/glslang. Accessed 2026-09-08. Learned: HLSL front-end deprecated April 2026, to be removed at next major (>=18 months notice), no HLSL bug reports accepted; TShader/TProgram C++ API and C interface. Relevance: choose DXC for HLSL. Confidence: high.
- **[S3b]** glslang releases via GitHub API, https://api.github.com/repos/KhronosGroup/glslang/releases. Accessed 2026-09-08. Learned: 16.5.0 (2026-08-03), 16.4.0 (2026-07-14, descriptor heaps, compute derivative fixes); macOS universal binaries. Confidence: high.
- **[S3c]** google/shaderc README, https://github.com/google/shaderc. Accessed 2026-09-08. Learned: wraps glslang + SPIRV-Tools; glslc CLI; libshaderc C/C++ API; #include support; backward-compat promise; no GitHub releases (empty releases API). Confidence: high.
- **[S4]** gfx-rs/wgpu naga README, https://github.com/gfx-rs/wgpu/tree/trunk/naga. Accessed 2026-09-08. Learned: MIT/Apache-2.0; front-ends SPIR-V, WGSL (primary), GLSL 440+ Vulkan-semantics (secondary); back-ends SPIR-V, Metal, HLSL primary; WGSL, GLSL secondary; naga-cli usage. Confidence: high.
- **[S4b]** wgpu releases via GitHub API, https://api.github.com/repos/gfx-rs/wgpu/releases. Accessed 2026-09-08. Learned: v30.0.1 (2026-08-22); v30.0.0 (2026-07-01) added naga-types crate, MSL cooperative-matrix support. Confidence: high.
- **[S5]** DirectXShaderCompiler release v1.9.2607, https://github.com/microsoft/DirectXShaderCompiler/releases/tag/v1.9.2607. Accessed 2026-09-08. Learned: 2026-07-29; HLSL `auto`, `[[nodiscard]]`; SPIR-V resource heaps, inline SPIR-V on params, layout-rule fixes. Confidence: high.
- **[S5b]** DXC release v1.9.2602 (Feb 2026), https://github.com/microsoft/DirectXShaderCompiler/releases/tag/v1.9.2602 and Phoronix summary https://www.phoronix.com/news/DX-Shader-Compiler-Better-VLK. Accessed 2026-09-08. Learned: SM 6.9 production; "significant SPIR-V backend updates". Confidence: medium (secondary summary for Phoronix).
- **[S6]** floooh/sokol-tools, docs/sokol-shdc.md, https://github.com/floooh/sokol-tools/blob/master/docs/sokol-shdc.md. Accessed 2026-09-08. Learned: annotated GLSL 450 input, @-tags, output languages/formats, `--reflection`, bare_yaml, std140 uniform restrictions, dependency stack glslang/SPIRV-Tools/SPIRV-Cross/Tint. Relevance: reference architecture. Confidence: high.
- **[S6b]** floooh, "The sokol-gfx compute shader update" (2025-03-03), https://floooh.github.io/2025/03/03/sokol-gfx-compute-update.html. Accessed 2026-09-08. Learned: workgroup size in GLSL/HLSL/WGSL vs dispatch-time in Metal; shdc passes `mtl_threads_per_threadgroup`; readonly vs read/write storage buffer marking for hazard tracking; storage textures not yet supported in sokol at that time. Confidence: high.
- **[S7]** libsdl-org/SDL_shadercross README, https://github.com/libsdl-org/SDL_shadercross, and releases API https://api.github.com/repos/libsdl-org/SDL_shadercross/releases. Accessed 2026-09-08. Learned: SPIR-V/HLSL in; DXBC/DXIL/SPIR-V/MSL/HLSL out; runtime + CLI; deps SPIRV-Cross, DXC, vkd3d-utils; zlib; no tagged releases. Confidence: high.
- **[S8]** Tint (Dawn) via search results, https://dawn.googlesource.com/tint and https://github.com/klukaszek/tint-wasm. Accessed 2026-09-08. Learned: WGSL/SPIR-V readers, SPIR-V/MSL/HLSL/GLSL/WGSL writers, `tint_cmd`, build flags TINT_BUILD_*_READER/WRITER, requires depot_tools/gclient. Confidence: medium (README fetch 404'd; relied on search snippets and mirror).
- **[S9]** KhronosGroup/SPIRV-Reflect README, https://github.com/KhronosGroup/SPIRV-Reflect. Accessed 2026-09-08. Learned: Apache 2.0; single spirv_reflect.h/.c; descriptor bindings, push constants, block member offsets, I/O variables, binding remap; no external deps. Relevance: canonical reflector. Confidence: high.
- **[S10]** KhronosGroup/SPIRV-Tools README, https://github.com/KhronosGroup/SPIRV-Tools. Accessed 2026-09-08. Learned: Apache 2.0; spirv-opt/val/as/dis/link/reduce; C and C++ APIs; SPIRV-Headers dependency. Confidence: high.
- **[S11]** Apple, "Discover Metal 4" WWDC25 session 205, https://developer.apple.com/videos/play/wwdc2025/205/ (via search summary). Accessed 2026-09-08. Learned: MTL4Compiler separate from device, inherits thread QoS, parallel pipeline compilation, pipeline serialization, flexible render pipeline states. Confidence: medium (summary, not transcript).
- **[S11b]** Metal by Example, "Getting Started with Metal 4", https://metalbyexample.com/metal-4/. Accessed 2026-09-08. Learned: `device.makeCompiler(descriptor:)`, `MTL4LibraryFunctionDescriptor`, `MTL4RenderPipelineDescriptor` uses function descriptors, argument tables via gpuResourceID/gpuAddress, residency sets, command allocators; requires macOS 26. Confidence: medium-high (third-party but authoritative author).
- **[S12]** Apple, "Building a shader library by precompiling source files", https://developer.apple.com/documentation/metal/building-a-shader-library-by-precompiling-source-files. Accessed 2026-09-08. Learned: `xcrun -sdk macosx metal -c`, `xcrun metallib`, `-frecord-sources`, `-gline-tables-only`, `makeLibrary(URL:)/(data:)`. Confidence: high.
- **[S13]** Apple, `MTLDevice.makeLibrary(source:options:)`, https://developer.apple.com/documentation/metal/mtldevice/makelibrary(source:options:). Accessed 2026-09-08. Learned: sync/async variants; MTLCompileOptions fields; runtime compile is expensive. Confidence: high.
- **[S14]** Apple, `MTLRenderPipelineReflection`, https://developer.apple.com/documentation/metal/mtlrenderpipelinereflection. Accessed 2026-09-08. Learned: vertexBindings/fragmentBindings (MTLBinding), MTLBufferBinding.bufferStructType members with offsets; MTLArgument deprecated; `.bindingInfo/.argumentInfo` options; macOS 11+. Confidence: high.
- **[S15]** Derivative, "Write a GLSL TOP", https://docs.derivative.ca/Write_a_GLSL_TOP. Accessed 2026-09-08. Learned: uniform pages, sTD*Inputs[], uTD2DInfos, uTDOutputInfo, TDOutputSwizzle, GLSL 4.60 post-Vulkan, TDImageStoreOutput for compute. Confidence: high.
- **[S16]** ISF JSON Reference, https://docs.isf.video/ref_json.html. Accessed 2026-09-08. Learned: top-level keys, INPUT types and attributes, PASSES attributes, filter/transition conventions. Confidence: high.
- **[S17]** mrRay/ISF_Spec README, https://github.com/mrRay/ISF_Spec. Accessed 2026-09-08. Learned: MIT license; automatic uniforms, IMG_* functions, isf_FragNormCoord, .vs + isf_vertShaderInit, host conversion behaviour, 1.0 -> 2.0 changes. Confidence: high.
- **[S18]** Shadertoy "How To", https://www.shadertoy.com/howto (403 on fetch; uniform list corroborated via search snippets and Three.js manual https://threejs.org/manual/en/shadertoy.html). Accessed 2026-09-08. Learned: full uniform list and mainImage signature. Confidence: medium-high.
- **[S18b]** ShaderGif, "Shadertoy Uniforms Explained", https://shadergif.com/docs/shadertoy-uniforms-explained/. Accessed 2026-09-08. Learned: uniform declarations, channel types, porting pitfalls. Confidence: medium (secondary).
- **[S19]** Anton Gerdelan, "Hot Reloading Shaders", https://antongerdelan.net/opengl/shader_hot_reload.html. Accessed 2026-09-08. Learned: mtime polling or keypress; compile new program first, keep old on failure, then swap. Confidence: high.
- **[S20]** tgfrerer/island README, https://github.com/tgfrerer/island. Accessed 2026-09-08. Learned: MIT; watches GLSL/HLSL/Slang/SPIR-V and their include/import closure; shaderc or Slang; auto-rebuilds Vulkan pipelines; errors with file:line and context. Confidence: high.
- **[S21]** screen-13-hot README, https://github.com/attackgoat/screen-13/blob/.../contrib/screen-13-hot/README.md and mayhemcode "GPU Shader Hot-Reloading" (2026-01), https://www.mayhemcode.com/2026/01/gpu-shader-hot-reloading-benefits.html (via search). Accessed 2026-09-08. Learned: hot()/cold() pattern; is_dirty flag; separate code from data. Confidence: medium.
- **[S22]** Apple, "Calculating threadgroup and grid sizes", https://developer.apple.com/documentation/metal/calculating-threadgroup-and-grid-sizes. Accessed 2026-09-08. Learned: maxTotalThreadsPerThreadgroup, threadExecutionWidth, dispatchThreads vs dispatchThreadgroups. Confidence: high. Apple forum thread https://developer.apple.com/forums/thread/674385 corroborates 1024 max on M1.
- **[S23]** Apple, `dispatchThreadgroups(indirectBuffer:...)` / `MTLDispatchThreadgroupsIndirectArguments` (same doc set as S22). Accessed 2026-09-08. Learned: indirect dispatch args = 3x uint32 in a buffer. Confidence: high.
- **[S24]** WebGPU limits and workgroup size: webgpufundamentals "Compute Shader Basics", https://webgpufundamentals.org/webgpu/lessons/webgpu-compute-shaders.html; gpuweb issue #3917. Accessed 2026-09-08. Learned: `@workgroup_size`, default maxComputeInvocationsPerWorkgroup 256, advice to use 64. Confidence: high.
- **[S25]** bgfx tools docs, https://bkaradzic.github.io/bgfx/tools.html. Accessed 2026-09-08. Learned: bgfx GLSL dialect, varying.def.sc, $input/$output, float-only uniforms, profiles glsl/essl/spirv/metal/hlsl/pssl. Confidence: high.
- **[S26]** LunarG, "Getting Started with the macOS Vulkan SDK", https://vulkan.lunarg.com/doc/view/latest/mac/getting_started.html. Accessed 2026-09-08. Learned: SDK 1.4.335.0 docs; bundles slang, DXC, SPIRV-Reflect, SPIRV-Cross, glslang, SPIRV-Tools; supports macOS 15 and 26; Xcode 26. Confidence: high.
- **[S27]** Arseny Kapoulkine, "Three years of Metal" (2019-12-12), https://zeux.io/2019/12/12/three-years-of-metal/. Accessed 2026-09-08. Learned: production glslang -> spirv-opt -> SPIRV-Cross -> MSL pipeline; argument buffers skipped; code remained fast and stable. Confidence: high but dated (2019).
- **[S28]** Alain Galvan, "A Review of Shader Languages", https://alain.xyz/blog/a-review-of-shader-languages. Accessed 2026-09-08. Learned: syntax/binding differences across GLSL/HLSL/MSL/WGSL; recommends SPIR-V pivot + SPIRV-Cross, DXC `-spirv` for HLSL. Confidence: medium (blog).
- **[S29]** Notch manual 2026.1/2026.2, Custom Shader Post Effect and Custom Shader Effector pages, https://manual.notch.one/2026.1/en/docs/reference/nodes/post-fx/image-processing/custom-shader-post-effect/ and https://manual.notch.one/2026.2/en/docs/reference/nodes/cloning/effectors/custom-shader-effector/ (via search summaries). Accessed 2026-09-08. Learned: global float variables exposed as properties; textures become input pins; effector supports floats/colours/checkboxes/menus. Confidence: medium (page fetch returned only navigation).
- **[S30]** Apple, Metal Shading Language Specification v4.1 (PDF), https://developer.apple.com/metal/Metal-Shading-Language-Specification.pdf. Accessed 2026-09-08 (title only via search). Learned: current MSL spec version is 4.1. Confidence: medium.
- **[S31]** SpartanJ/efsw, https://github.com/SpartanJ/efsw. Accessed 2026-09-08. Learned: C++ cross-platform watcher, FSEvents/kqueue/inotify/Win32 backends with generic fallback, recursive watch, C API, MIT. Confidence: high.
- **[S32]** Unity Manual, "ShaderLab: defining material properties", https://docs.unity3d.com/Manual/SL-Properties.html. Accessed 2026-09-08. Learned: property syntax, types, attributes, CBUFFER requirement for SRP Batcher. Confidence: high.
- **[S33]** Epic, "Material Parameter Expressions in Unreal Engine", https://dev.epicgames.com/documentation/en-us/unreal-engine/material-parameter-expressions-in-unreal-engine. Accessed 2026-09-08. Learned: parameter node types, groups, static switches, material instances, parameter collections limits. Confidence: high.
- **[S34]** Godot docs, "Shading language" (uniform section), https://docs.godotengine.org/en/stable/tutorials/shaders/shader_reference/shading_language.html. Accessed 2026-09-08. Learned: full hint list, instance/global uniforms, group_uniforms, doc-comment tooltips, uniform size limits. Confidence: high.
