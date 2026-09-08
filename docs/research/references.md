# Consolidated References

All sources cited across the Phase 0 research documents, grouped by document. Every entry was accessed on 2026-09-08 unless the entry says otherwise. The per-document Sources sections are reproduced verbatim so that this file can be searched in one place; the per-claim context (what was learned, relevance, confidence) lives in the originating document.

Precedence used throughout: official documentation > academic papers > source repositories > technical talks > reputable engineering articles > community discussion. Entries whose page did not render are marked in their originating document.


Total unique URLs across documents: 686.


## rendering.md (143 unique URLs)

All accessed 2026-09-08.

**Apple / Metal**
- [S1] apple/metal-cpp README and changelog — https://github.com/apple/metal-cpp
- [S2] Getting started with Metal-cpp — https://developer.apple.com/metal/cpp/
- [S3] apple/metal-cpp tags and commit dates (GitHub API) — https://api.github.com/repos/apple/metal-cpp/commits
- [S4] WWDC25 session 205 "Discover Metal 4" — https://developer.apple.com/videos/play/wwdc2025/205/
- [S5] Metal Shading Language Specification (PDF, listed as v4.1, dated 2026-06-04 in search metadata; not opened) — https://developer.apple.com/metal/Metal-Shading-Language-Specification.pdf
- [S6] MTLDevice `newLibraryWithSource:options:error:` — https://developer.apple.com/documentation/metal/mtldevice/makelibrary(source:options:)?language=objc
- [S7] Capturing a Metal workload in Xcode / Metal debugger — https://developer.apple.com/documentation/xcode/capturing-a-metal-workload-in-xcode and https://developer.apple.com/documentation/xcode/metal-debugger
- [S8] Apple Developer Forums, metal-cpp-extensions (AppKit/MetalKit headers shipped in LearnMetalCPP) — https://developer.apple.com/forums/thread/722886

**WebGPU (wgpu / Dawn / headers / spec)**
- [S9] gfx-rs/wgpu README — https://github.com/gfx-rs/wgpu
- [S10] gfx-rs/wgpu-native README — https://github.com/gfx-rs/wgpu-native
- [S11] wgpu-native releases — https://github.com/gfx-rs/wgpu-native/releases
- [S12] wgpu releases — https://github.com/gfx-rs/wgpu/releases
- [S13] W3C WebGPU specification, limits — https://www.w3.org/TR/webgpu/#limits
- [S14] wgpu `Features` docs — https://docs.rs/wgpu/latest/wgpu/struct.Features.html
- [S15] wgpu-native `ffi/wgpu.h` — https://raw.githubusercontent.com/gfx-rs/wgpu-native/trunk/ffi/wgpu.h
- [S16] webgpu-native/webgpu-headers README — https://github.com/webgpu-native/webgpu-headers
- [S17] wgpu issues #4491, #6744, #6546 — https://github.com/gfx-rs/wgpu/issues/4491 , https://github.com/gfx-rs/wgpu/issues/6744 , https://github.com/gfx-rs/wgpu/issues/6546
- [S18] google/dawn README — https://github.com/google/dawn
- [S19] google/dawn commits (GitHub API) — https://api.github.com/repos/google/dawn/commits
- [S20] google/dawn releases (GitHub API) — https://github.com/google/dawn/releases
- [S21] Dawn feature docs directory — https://github.com/google/dawn/tree/main/docs/dawn/features
- [S22] Dawn `multi_draw_indirect.md` — https://github.com/google/dawn/blob/main/docs/dawn/features/multi_draw_indirect.md
- [S23] Dawn SPIR-V reader overview and dawn-graphics thread "Loading Spirv shaders" — https://dawn.googlesource.com/dawn/+/HEAD/docs/tint/spirv-reader-overview.md , https://groups.google.com/g/dawn-graphics/c/_1dGRs6aKlY
- [S24] Dawn `docs/quickstart-cmake.md` — https://github.com/google/dawn/blob/main/docs/quickstart-cmake.md
- [S25] Dawn `docs/building.md` — https://dawn.googlesource.com/dawn/+/HEAD/docs/building.md

**bgfx / sokol**
- [S26] bgfx README and overview — https://github.com/bkaradzic/bgfx , https://bkaradzic.github.io/bgfx/overview.html
- [S27] bgfx `src/renderer_mtl.cpp`, `src/renderer_mtl.h` — https://raw.githubusercontent.com/bkaradzic/bgfx/master/src/renderer_mtl.cpp , https://raw.githubusercontent.com/bkaradzic/bgfx/master/src/renderer_mtl.h
- [S28] bkaradzic/bgfx.cmake — https://github.com/bkaradzic/bgfx.cmake
- [S29] bgfx API reference — https://bkaradzic.github.io/bgfx/bgfx.html
- [S30] bgfx `src/config.h` and `include/bgfx/defines.h` — https://raw.githubusercontent.com/bkaradzic/bgfx/master/src/config.h , https://raw.githubusercontent.com/bkaradzic/bgfx/master/include/bgfx/defines.h
- [S31] bgfx tools (shaderc) — https://bkaradzic.github.io/bgfx/tools.html
- [S32] floooh/sokol README — https://github.com/floooh/sokol
- [S33] "The experimental Sokol Vulkan backend" (2025-12-01) — https://floooh.github.io/2025/12/01/sokol-vulkan-backend-1.html
- [S34] sokol CHANGELOG — https://raw.githubusercontent.com/floooh/sokol/master/CHANGELOG.md
- [S35] "The sokol-gfx compute shader update" (2025-03-03) — https://floooh.github.io/2025/03/03/sokol-gfx-compute-update.html
- [S36] "The sokol-gfx 'compute milestone 2' update" (2025-05-19) — https://floooh.github.io/2025/05/19/sokol-gfx-compute-ms2.html
- [S37] `sokol_gfx.h` (master) — https://raw.githubusercontent.com/floooh/sokol/master/sokol_gfx.h
- [S38] sokol-shdc docs — https://github.com/floooh/sokol-tools/blob/master/docs/sokol-shdc.md

**Debug tooling**
- [S40] RenderDoc features/platform support — https://renderdoc.org/docs/getting_started/features.html

**Vulkan on macOS**
- [S41] KhronosGroup/MoltenVK README, releases, Whats_New — https://github.com/KhronosGroup/MoltenVK , https://github.com/KhronosGroup/MoltenVK/releases , https://github.com/KhronosGroup/MoltenVK/blob/main/Docs/Whats_New.md
- [S42] MoltenVK Runtime User Guide — https://github.com/KhronosGroup/MoltenVK/blob/main/Docs/MoltenVK_Runtime_UserGuide.md
- [S43] Mesa docs, KosmicKrisp — https://docs.mesa3d.org/drivers/kosmickrisp.html
- [S44] LunarG Vulkan SDK for macOS release notes 1.4.357.x and Getting Started — https://vulkan.lunarg.com/doc/view/1.4.357.1/mac/release_notes.html , https://vulkan.lunarg.com/sdk/latest/mac.txt
- [S45] Khronos Vulkan 1.4 press release — https://www.khronos.org/news/press/khronos-streamlines-development-and-deployment-of-gpu-accelerated-applications-with-vulkan-1.4
- [S46] MoltenVK issue #2560 (Metal 4) — https://github.com/KhronosGroup/MoltenVK/issues/2560
- [S47] Khronos conformant products (KosmicKrisp entry #958) — https://www.khronos.org/conformance/adopters/conformant-products/vulkan
- [S48] LunarG: KosmicKrisp conformance announcement; "State of Vulkan on Apple, Jan 2026" — https://www.lunarg.com/lunarg-achieves-vulkan-1-3-conformance-with-kosmickrisp-on-apple-silicon/ , https://www.lunarg.com/the-state-of-vulkan-on-apple-jan-2026/

**Direct3D 12**
- [S49] DirectX 12 Agility SDK downloads — https://devblogs.microsoft.com/directx/directx12agility/
- [S50] "Shader Model 6.9 retail and more" — https://devblogs.microsoft.com/directx/shader-model-6-9-retail-and-more/
- [S51] "Agility SDK 1.613.0" (work graphs retail) — https://devblogs.microsoft.com/directx/agility-sdk-1-613-0/
- [S52] microsoft/DirectXShaderCompiler releases — https://github.com/microsoft/DirectXShaderCompiler/releases

**OpenGL on macOS**
- [S53] macOS Mojave 10.14 release notes — https://developer.apple.com/documentation/macos-release-notes/macos-mojave-10_14-release-notes
- [S54] Porting your macOS apps to Apple silicon — https://developer.apple.com/documentation/apple-silicon/porting-your-macos-apps-to-apple-silicon
- [S55] Apple Support, Mac computers that use OpenCL and OpenGL graphics — https://support.apple.com/en-us/101525
- [S56] JUCE forum, "macOS Tahoe and OpenGL" — https://forum.juce.com/t/macos-tahoe-and-opengl/66921
- [S57] GLFW `docs/compat.md` @3.5.1 — https://github.com/glfw/glfw/blob/3.5.1/docs/compat.md

**GLFW**
- [S58] glfw/glfw releases and commits (GitHub API); glfw.org news — https://github.com/glfw/glfw/releases , https://www.glfw.org/
- [S59] GLFW `src/cocoa_window.m` @3.5.1; native access docs — https://github.com/glfw/glfw/blob/3.5.1/src/cocoa_window.m , https://www.glfw.org/docs/latest/group__native.html
- [S60] GLFW Vulkan guide — https://www.glfw.org/docs/latest/vulkan_guide.html
- [S61] GLFW window guide — https://www.glfw.org/docs/latest/window_guide.html
- [S62] GLFW build guide — https://www.glfw.org/docs/latest/build_guide.html

**SDL3 / SDL_GPU**
- [S63] SDL3 CategoryGPU — https://wiki.libsdl.org/SDL3/CategoryGPU
- [S64] libsdl-org/SDL releases — https://github.com/libsdl-org/SDL/releases
- [S65] `include/SDL3/SDL_gpu.h` — https://raw.githubusercontent.com/libsdl-org/SDL/main/include/SDL3/SDL_gpu.h
- [S66] `src/gpu/metal/SDL_gpu_metal.m` — https://raw.githubusercontent.com/libsdl-org/SDL/main/src/gpu/metal/SDL_gpu_metal.m
- [S67] SDL 3.4.0 release notes — https://github.com/libsdl-org/SDL/releases/tag/release-3.4.0
- [S68] SDL_GPUTextureFormat — https://wiki.libsdl.org/SDL3/SDL_GPUTextureFormat
- [S69] SDL_GPUSwapchainComposition — https://wiki.libsdl.org/SDL3/SDL_GPUSwapchainComposition
- [S70] SDL_GPUShaderFormat — https://wiki.libsdl.org/SDL3/SDL_GPUShaderFormat
- [S71] libsdl-org/SDL_shadercross README — https://github.com/libsdl-org/SDL_shadercross
- [S72] SDL_shadercross releases (none) and commits — https://github.com/libsdl-org/SDL_shadercross/releases

**Filament**
- [S73] google/filament releases and commits — https://github.com/google/filament/releases , https://github.com/google/filament/commits/main
- [S74] Filament BUILDING.md, README, `VulkanPlatformApple.mm` — https://github.com/google/filament/blob/main/BUILDING.md , https://github.com/google/filament/blob/main/filament/backend/src/vulkan/platform/VulkanPlatformApple.mm
- [S75] Filament issue #7995 (compute); `MaterialEnums.h`, `DriverEnums.h` — https://github.com/google/filament/issues/7995 , https://github.com/google/filament/blob/main/filament/backend/include/backend/DriverEnums.h
- [S76] Filament `RenderableManager.h` — https://github.com/google/filament/blob/main/filament/include/filament/RenderableManager.h
- [S77] Filament `RenderTarget.h`, `View.h` — https://github.com/google/filament/blob/main/filament/include/filament/RenderTarget.h
- [S78] Filament Materials guide; `third_party/` — https://google.github.io/filament/main/materials.html , https://github.com/google/filament/tree/main/third_party
- [S79] Filament discussion #7676 (custom post-processing) — https://github.com/google/filament/discussions/7676
- [S80] Filament `RenderableManager.h` (instancing) — as [S76]
- [S81] Filament `MetalDriver.mm` — https://raw.githubusercontent.com/google/filament/main/filament/backend/src/metal/MetalDriver.mm
- [S82] Filament `Engine.h`, `SwapChain.h`, `Renderer.h` — https://github.com/google/filament/tree/main/filament/include/filament

**OGRE-Next**
- [S83] OGRECave/ogre-next releases, tags, commits — https://github.com/OGRECave/ogre-next/releases , https://github.com/OGRECave/ogre-next/commits/master
- [S84] OGRE-Next "What's new in 4.0" — https://ogrecave.github.io/ogre-next/api/latest/_ogre40_changes.html
- [S85] `RenderSystems/Vulkan/CMakeLists.txt`; Setting up OGRE on macOS — https://github.com/OGRECave/ogre-next/blob/master/RenderSystems/Vulkan/CMakeLists.txt , https://ogrecave.github.io/ogre-next/api/latest/_setting_up_ogre_mac_o_s.html
- [S86] `HlmsComputeJob` docs; compositor docs; tutorials — https://ogrecave.github.io/ogre-next/api/latest/class_ogre_1_1_hlms_compute_job.html , https://ogrecave.github.io/ogre-next/api/latest/compositor.html , https://github.com/OGRECave/ogre-next/tree/master/Samples/2.0/Tutorials
- [S87] `VaoManager` docs; `OgreRenderQueue.cpp` — https://ogrecave.github.io/ogre-next/api/latest/class_ogre_1_1_vao_manager.html
- [S88] Samples/2.0/Showcase — https://github.com/OGRECave/ogre-next/tree/master/Samples/2.0/Showcase
- [S89] Hlms docs; `VulkanProgram` docs; ogre-next-deps — https://ogrecave.github.io/ogre-next/api/latest/hlms.html , https://ogrecave.github.io/ogre-next/api/latest/class_ogre_1_1_vulkan_program.html , https://github.com/OGRECave/ogre-next-deps
- [S90] `RenderSystem` docs; `CMake/Dependencies.cmake` — https://ogrecave.github.io/ogre-next/api/latest/class_ogre_1_1_render_system.html
- [S91] macOS setup page — as [S85]
- [S92] `AsyncTextureTicket` docs — https://ogrecave.github.io/ogre-next/api/latest/class_ogre_1_1_async_texture_ticket.html

**Diligent Engine**
- [S93] DiligentEngine / DiligentCore READMEs, releases, commits — https://github.com/DiligentGraphics/DiligentEngine , https://github.com/DiligentGraphics/DiligentCore
- [S94] `Graphics/GraphicsEngineMetal/readme.md` — https://github.com/DiligentGraphics/DiligentCore/tree/master/Graphics/GraphicsEngineMetal
- [S95] `DeviceContext.h`, `GraphicsTypes.h` — https://github.com/DiligentGraphics/DiligentCore/tree/master/Graphics/GraphicsEngine/interface
- [S96] `Shader.h`; `ThirdParty/`; `Graphics/ShaderTools/src` — https://github.com/DiligentGraphics/DiligentCore/tree/master/ThirdParty
- [S97] `RenderStateCache.h` — https://github.com/DiligentGraphics/DiligentCore/blob/master/Graphics/GraphicsTools/interface/RenderStateCache.h
- [S98] `doc/PerformanceGuide.md` — https://github.com/DiligentGraphics/DiligentCore/blob/master/doc/PerformanceGuide.md

**The Forge**
- [S99] ConfettiFX/The-Forge releases and commits — https://github.com/ConfettiFX/The-Forge
- [S100] The-Forge on Codeberg (README, contents, commits) — https://codeberg.org/The-Forge/The-Forge
- [S101] `Common_3/Graphics/Interfaces/IGraphics.h` @v1.63 — https://github.com/ConfettiFX/The-Forge/blob/v1.63/Common_3/Graphics/Interfaces/IGraphics.h
- [S102] FSL Programming Guide; `compilers.py` — https://github.com/ConfettiFX/The-Forge/wiki/FSL-Programming-Guide

**LLGL**
- [S103] LukasBanana/LLGL README, releases, commits, CMakeLists — https://github.com/LukasBanana/LLGL
- [S104] `sources/Renderer/Metal/`, `VKRenderSystem.cpp`, `BuildMacOS.command` — https://github.com/LukasBanana/LLGL/tree/master/sources/Renderer
- [S105] `include/LLGL/CommandBuffer.h`, `Format.h`, `MTDirectCommandBuffer.mm` — https://github.com/LukasBanana/LLGL/blob/master/include/LLGL/CommandBuffer.h
- [S106] `MTShader.mm`, `scripts/TranslateShaders.py` — https://github.com/LukasBanana/LLGL/blob/master/scripts/TranslateShaders.py

**NVRHI**
- [S107] NVIDIA-RTX/NVRHI README, releases/tags, commits, CMake, CI — https://github.com/NVIDIA-RTX/NVRHI
- [S108] NVRHI issue #68 "Metal support?" — https://github.com/NVIDIA-RTX/NVRHI/issues/68
- [S109] `include/nvrhi/nvrhi.h`, `doc/ProgrammingGuide.md` — https://github.com/NVIDIA-RTX/NVRHI/blob/main/doc/ProgrammingGuide.md
- [S110] NVIDIA-RTX/ShaderMake — https://github.com/NVIDIA-RTX/ShaderMake

**Magnum**
- [S111] mosra/magnum releases, commits, discussion #615, issue #453 — https://github.com/mosra/magnum/releases , https://github.com/mosra/magnum/discussions/615 , https://github.com/mosra/magnum/issues/453
- [S112] Magnum issue #254 (Metal backend) — https://github.com/mosra/magnum/issues/254
- [S113] Magnum macOS platform notes — https://doc.magnum.graphics/magnum/platforms-macos.html
- [S114] `src/Magnum/Vk`; Vulkan support page; example index; PR #234 — https://github.com/mosra/magnum/tree/master/src/Magnum/Vk , https://doc.magnum.graphics/magnum/vulkan-support.html , https://github.com/mosra/magnum/issues/234
- [S115] `GL::AbstractShaderProgram`, `GL::Buffer`, `GL::Mesh` docs — https://doc.magnum.graphics/magnum/classMagnum_1_1GL_1_1AbstractShaderProgram.html
- [S116] `GL::Framebuffer` docs — https://doc.magnum.graphics/magnum/classMagnum_1_1GL_1_1Framebuffer.html
- [S117] magnum-shaderconverter; ShaderTools namespace — https://doc.magnum.graphics/magnum/magnum-shaderconverter.html
- [S118] `GL::DebugOutput` docs — https://doc.magnum.graphics/magnum/classMagnum_1_1GL_1_1DebugOutput.html


## audio-analysis.md (106 unique URLs)

Accessed 2026-09-08 unless stated.

Audio I/O and decoding
1. miniaudio site and manual — https://miniaud.io/ , https://miniaud.io/docs/manual/index.html
2. mackron/miniaudio (GitHub) — https://github.com/mackron/miniaudio ; release mirror https://releasealert.dev/github/mackron/miniaudio ; issue #286 https://github.com/mackron/miniaudio/issues/286 ; `ma_decoder_config_init` https://miniaudio.docsforge.com/master/api/ma_decoder_config_init/
3. mackron/dr_libs — https://github.com/mackron/dr_libs , https://github.com/mackron/dr_libs/blob/master/dr_wav.h
4. libsndfile — https://en.wikipedia.org/wiki/Libsndfile , https://vcpkg.link/ports/libsndfile
5. PortAudio — https://files.portaudio.com/download.html , https://github.com/PortAudio/portaudio , https://github.com/PortAudio/portaudio/pulls , https://en.wikipedia.org/wiki/PortAudio
6. RtAudio — https://github.com/thestk/rtaudio , https://github.com/thestk/rtaudio/releases , https://github.com/thestk/rtaudio/blob/master/doc/release.txt
7. SDL3 audio — https://wiki.libsdl.org/SDL3/CategoryAudio , https://wiki.libsdl.org/SDL3/SDL_AudioStream , https://wiki.libsdl.org/SDL3/SDL_OpenAudioDeviceStream , https://wiki.libsdl.org/SDL3/SDL_SetAudioStreamGetCallback , https://github.com/libsdl-org/SDL/releases , https://en.wikipedia.org/wiki/Simple_DirectMedia_Layer
8. JUCE 8 — https://juce.com/legal/juce-8-licence/ , https://github.com/juce-framework/JUCE/blob/master/LICENSE.md , https://en.wikipedia.org/wiki/JUCE
9. Apple Core Audio — https://developer.apple.com/documentation/coreaudio/audiodeviceioproc , https://developer.apple.com/library/ios/qa/qa1715/_index.html , https://developer.apple.com/videos/play/wwdc2020/10224/ , https://developer.apple.com/documentation/audiotoolbox/adding-asynchronous-real-time-threads-to-audio-workgroups
10. libsoundio — https://github.com/andrewrk/libsoundio , https://github.com/andrewrk/libsoundio/releases , https://tracker.debian.org/pkg/libsoundio
11. Xiph codecs — https://github.com/xiph/vorbis , https://opus-codec.org/license/ , https://github.com/xiph/opusfile ; stb_vorbis https://github.com/nothings/stb/blob/master/stb_vorbis.c
12. Ross Bencina, "Real-time audio programming 101: time waits for nothing" (2011) — http://www.rossbencina.com/code/real-time-audio-programming-101-time-waits-for-nothing

Algorithms
13. Müller, FMP Notebooks: STFT https://www.audiolabs-erlangen.de/resources/MIR/FMP/C2/C2_STFT-Basic.html ; Onset detection https://www.audiolabs-erlangen.de/resources/MIR/FMP/C6/C6S1_OnsetDetection.html ; Beat tracking by DP https://www.audiolabs-erlangen.de/resources/MIR/FMP/C6/C6S3_BeatTracking.html
14. Peeters 2004, CUIDADO audio features — http://recherche.ircam.fr/anasyn/peeters/ARTICLES/Peeters_2003_cuidadoaudiofeatures.pdf
15. Jiang et al. 2002, spectral contrast — https://ieeexplore.ieee.org/document/1035731 ; librosa https://librosa.org/doc/main/generated/librosa.feature.spectral_contrast.html
16. Bello et al. 2005, onset detection tutorial — https://hajim.rochester.edu/ece/sites/zduan/teaching/ece472/reading/Bello_2005.pdf
17. Dixon 2006, "Onset Detection Revisited" — https://www.dafx.de/paper-archive/2006/papers/p_133.pdf
18. Böck & Widmer 2013, SuperFlux — https://www.dafx.de/paper-archive/2013/papers/09.dafx2013_submission_12.pdf , https://github.com/CPJKU/SuperFlux
19. madmom docs (onsets, beats) and repo — https://madmom.readthedocs.io/en/v0.16/modules/features/onsets.html , https://madmom.readthedocs.io/en/v0.16/modules/features/beats.html , https://github.com/CPJKU/madmom
20. Ellis 2007, beat tracking by DP — https://www.ee.columbia.edu/~dpwe/pubs/Ellis07-beattrack.pdf , https://www.tandfonline.com/doi/abs/10.1080/09298210701653344 ; librosa beat_track https://librosa.org/doc/0.11.0/generated/librosa.beat.beat_track.html
21. aubio tempo (Davies & Plumbley) — https://github.com/aubio/aubio/blob/master/src/tempo/beattracking.h , https://aubio.org/manpages/latest/aubiotrack.1.html
22. Böck, Krebs, Widmer 2015, tempo via comb filters — https://archives.ismir.net/ismir2015/paper/000196.pdf
23. Foscarin, Schlüter, Widmer 2024, "Beat This!" — https://arxiv.org/abs/2407.21658 , https://github.com/CPJKU/beat_this , https://github.com/mosynthkey/beat_this_cpp
24. Chroma — https://en.wikipedia.org/wiki/Chroma_feature , https://en.wikipedia.org/wiki/Harmonic_pitch_class_profiles , Müller & Ewert 2011 https://www.audiolabs-erlangen.de/content/resources/MIR/chromatoolbox/2011_MuellerEwert_ChromaToolbox_ISMIR.pdf
25. de Cheveigné & Kawahara 2002, YIN — http://audition.ens.fr/adc/pdf/2002_JASA_YIN.pdf , https://pubs.aip.org/asa/jasa/article/111/4/1917/547221 ; pYIN https://webspace.eecs.qmul.ac.uk/s.e.dixon/pub/2014/MauchDixon-PYIN-ICASSP2014.pdf
26. Envelope follower coefficients — https://www.kvraudio.com/forum/viewtopic.php?t=278170 , https://www.elementary.audio/docs/tutorials/envelope-generators (secondary to Zölzer, DAFX 2nd ed.)
27. Loudness — https://en.wikipedia.org/wiki/LKFS , ITU-R BS.1770-5 https://www.itu.int/dms_pubrec/itu-r/rec/bs/R-REC-BS.1770-5-202311-I!!PDF-E.pdf , https://www.forasoft.com/learn/audio-for-video/glossary/terms-audio/itu-r-bs1770 , libebur128 source https://raw.githubusercontent.com/jiixyj/libebur128/master/ebur128/ebur128.c
28. A-weighting — https://en.wikipedia.org/wiki/A-weighting , https://www.mathworks.com/help/audio/ug/audio-weighting-filters.html
29. Band presets (not fetched this session) — p5.sound `p5.FFT.getEnergy` https://p5js.org/reference/p5.sound/p5.FFT/

Libraries
30. KissFFT — https://github.com/mborgerding/kissfft , https://github.com/mborgerding/kissfft/releases
31. pffft (marton78) — https://github.com/marton78/pffft , https://github.com/marton78/pffft/blob/master/README.md
32. PocketFFT — https://github.com/mreineck/pocketfft , https://github.com/mreineck/pocketfft/blob/cpp/LICENSE.md
33. FFTW license — https://www.fftw.org/doc/License-and-Copyright.html
34. Apple vDSP — https://developer.apple.com/documentation/accelerate/vdsp/fft , https://developer.apple.com/documentation/accelerate/vdsp_fft_zip
35. muFFT — https://github.com/Themaister/muFFT ; meow_fft — https://github.com/JodiTheTigger/meow_fft
36. KFR — https://github.com/kfrlib/kfr/releases , https://www.kfrlib.com/purchase/
37. Intel IPP — https://www.intel.com/content/www/us/en/developer/articles/release-notes/ipp/2026.html , https://www.intel.com/content/www/us/en/docs/oneapi/installation-guide-macos/2024-0/overview.html , https://melatonin.dev/blog/using-intel-performance-primitives-ipp-with-juce-and-cmake/
38. aubio — https://aubio.org/ , https://github.com/aubio/aubio , https://github.com/aubio/aubio/releases
39. Essentia — https://github.com/MTG/essentia , https://pypi.org/project/essentia/
40. Gist — https://github.com/adamstark/Gist , https://github.com/adamstark/Gist/blob/master/LICENSE.txt
41. libebur128 — https://github.com/jiixyj/libebur128 , https://github.com/jiixyj/libebur128/releases
42. Q (cycfi) — https://github.com/cycfi/q , https://github.com/cycfi/q/blob/master/LICENSE , https://cycfi.github.io/q/q/v1.5-dev/index.html
43. chromaprint — https://github.com/acoustid/chromaprint , https://github.com/acoustid/chromaprint/blob/master/LICENSE.md , https://github.com/acoustid/chromaprint/releases


## shaders.md (53 unique URLs)

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


## assets.md (43 unique URLs)

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


## particles.md (41 unique URLs)

1. Wicked Engine, "GPU-based particle simulation" (2017). https://wickedengine.net/2017/11/07/gpu-based-particle-simulation/ — accessed 2026-09-08 (404 on live site; content via search abstract and https://deepwiki.com/turanszkij/WickedEngine/8.1-lua-integration and https://github.com/turanszkij/WickedEngine).
2. Hillaire, S., Egleus, A. "Frostbite GPU Emitter Graph System", GDC 2018. https://www.gdcvault.com/play/1025132/Frostbite-GPU-Emitter-Graph ; https://realtimevfx.com/t/gdc-2018-frostbite-gpu-emitter-graph-system/4003 — accessed 2026-09-08.
3. Epic Games, "Key Concepts in Niagara Effects". https://dev.epicgames.com/documentation/en-us/unreal-engine/key-concepts-in-niagara-effects-for-unreal-engine — accessed 2026-09-08.
4. Epic Games, "Overview of Niagara Effects". https://dev.epicgames.com/documentation/unreal-engine/overview-of-niagara-effects-for-unreal-engine — accessed 2026-09-08.
5. "How Simulation Stages Work in Unreal Engine GPU Emitters". https://dev.to/dinesh_04/how-simulation-stages-work-in-unreal-engine-gpu-emitters-149c — accessed 2026-09-08.
6. StraySpark, "Niagara VFX Beyond Particles". https://www.strayspark.studio/blog/niagara-vfx-advanced-simulation-stages — accessed 2026-09-08.
7. Epic Games, "GPU Particles with Scene Depth Collision" (UE 4.27 content example). https://docs.unrealengine.com/4.27/en-US/Resources/ContentExamples/EffectsGallery/1_E — accessed 2026-09-08.
8. Epic Games, "Using Particle Collision Mode for Distance Fields". https://docs.unrealengine.com/4.27/en-US/BuildingWorlds/LightingAndShadows/MeshDistanceFields/HowTo/DFHT_3/ — accessed 2026-09-08.
9. Epic Games, "GPU Raytracing Collisions in Niagara". https://dev.epicgames.com/documentation/en-us/unreal-engine/gpu-raytracing-collisions-in-niagara-for-unreal-engine — accessed 2026-09-08.
10. Unity, "Visual Effect Graph — Graph Logic and Philosophy". https://docs.unity3d.com/Packages/com.unity.visualeffectgraph@17.0/manual/GraphLogicAndPhilosophy.html — accessed 2026-09-08.
11. Unity blog, "New possibilities with VFX Graph in 2020 LTS and beyond". https://unity.com/blog/engine-platform/new-possibilities-with-vfx-graph-in-2020-lts-and-beyond — accessed 2026-09-08.
12. Unity forum, "VFX Capacity and performance". https://forum.unity.com/threads/vfx-capacity-and-performance.1013098/ — accessed 2026-09-08.
13. Unity Discussions, "VFX Graph stops rendering particles if strip capacity and capacity are mismatched". https://discussions.unity.com/t/vfx-graph-stops-rendering-particles-if-strip-capacity-and-capacity-are-mismatched/1526919 — accessed 2026-09-08.
14. Notch, "Particles, Simulations & Volumetrics". https://www.notch.one/features/particles-simulations-volumetrics — accessed 2026-09-08.
15. Notch Manual 2026.1, "Introducing Particles". https://manual.notch.one/2026.1/en/docs/learning/simulations/particles/overview-of-particles/ — accessed 2026-09-08.
16. Willems, S. Vulkan examples: computeparticles, computenbody. https://github.com/SaschaWillems/Vulkan/blob/master/examples/computeparticles/computeparticles.cpp — accessed 2026-09-08.
17. Apple, Metal Best Practices Guide: "Indirect Buffers". https://developer.apple.com/library/archive/documentation/3DDrawing/Conceptual/MTLBestPracticesGuide/IndirectBuffers.html — accessed 2026-09-08.
18. Apple, "Indirect command encoding". https://developer.apple.com/documentation/metal/indirect-command-encoding — accessed 2026-09-08 (page body did not render in fetch tool).
19. Apple, `MTLDispatchThreadgroupsIndirectArguments`. https://developer.apple.com/documentation/metal/mtldispatchthreadgroupsindirectarguments — accessed 2026-09-08.
20. Apple, WWDC22 "Go bindless with Metal 3". https://developer.apple.com/videos/play/wwdc2022/10101/ — accessed 2026-09-08.
21. Apple, WWDC25 "Discover Metal 4". https://developer.apple.com/videos/play/wwdc2025/205/ — accessed 2026-09-08.
22. Tellusim, "MultiDrawIndirect and Metal". https://tellusim.com/metal-mdi/ — accessed 2026-09-08.
23. Anagnostou, K. "Stream compaction using wave intrinsics" (2022). https://interplayoflight.wordpress.com/2022/12/25/stream-compaction-using-wave-intrinsics/ — accessed 2026-09-08.
24. Adinets, A., Merrill, D. "Onesweep: A Faster Least Significant Digit Radix Sort for GPUs" (2022). https://arxiv.org/abs/2206.01784 — accessed 2026-09-08.
25. AMD, "FidelityFX Parallel Sort". https://gpuopen.com/manuals/fidelityfx_sdk/techniques/parallel-sort/ — accessed 2026-09-08.
26. Linebender wiki, "GPU sorting". https://linebender.org/wiki/gpu/sorting/ — accessed 2026-09-08.
27. Kieber-Emmons, M. "Memory Bandwidth Optimized Parallel Radix Sort in Metal for Apple M1 and Beyond". https://betterprogramming.pub/memory-bandwidth-optimized-parallel-radix-sort-in-metal-for-apple-m1-and-beyond-4f4590cfd5d3 — accessed 2026-09-08.
28. Real Time VFX, "[Niagara 4.25] Ribbon Trail Mini Tutorial". https://realtimevfx.com/t/niagara-4-25-ribbon-trail-mini-tutorial/13043 — accessed 2026-09-08.
29. Jakobsson, S. "GPU Particle System". https://svantejakobsson.com/gpu-particle-system/ — accessed 2026-09-08.
30. Bridson, R., Hourihan, J., Nordenstam, M. "Curl-Noise for Procedural Fluid Flow", SIGGRAPH 2007. https://www.cs.ubc.ca/~rbridson/docs/bridson-siggraph2007-curlnoise.pdf — accessed 2026-09-08 (full text read).
31. Demeulenaere, P. "vfx-neighborhood-grid-3d". https://github.com/pauldemeulenaere/vfx-neighborhood-grid-3d/ — accessed 2026-09-08.
32. "Performance Evaluation of Boids on the GPU and CPU" (BTH thesis). https://www.diva-portal.org/smash/get/diva2:1191916/FULLTEXT01.pdf — accessed 2026-09-08.
33. Macklin, M., Müller, M. "Position Based Fluids", SIGGRAPH 2013. https://dl.acm.org/doi/10.1145/2461912.2461984 — accessed 2026-09-08.
34. Hu, Y. et al. "A Moving Least Squares Material Point Method" (taichi_mpm), SIGGRAPH 2018. https://github.com/yuanming-hu/taichi_mpm — accessed 2026-09-08.
35. Wu, K. et al. "Fast Fluid Simulations with Sparse Volumes on the GPU", CGF 2018. https://onlinelibrary.wiley.com/doi/abs/10.1111/cgf.13350 — accessed 2026-09-08.
36. Codrops, "WebGPU Fluid Simulations" (2025). https://tympanus.net/codrops/2025/02/26/webgpu-fluid-simulations-high-performance-real-time-rendering/ — accessed 2026-09-08.
37. Apple, Metal Feature Set Tables (PDF). https://developer.apple.com/metal/Metal-Feature-Set-Tables.pdf — referenced, not fetched, 2026-09-08.
38. Harris, M. "Fast Fluid Dynamics Simulation on the GPU", GPU Gems ch. 38 (ping-pong constraint). https://developer.nvidia.com/gpugems/gpugems/part-vi-beyond-triangles/chapter-38-fast-fluid-dynamics-simulation-gpu — accessed 2026-09-08.


## rendering-techniques.md (49 unique URLs)

1. O'Donnell, Y. "FrameGraph: Extensible Rendering Architecture in Frostbite", GDC 2017. https://www.gdcvault.com/play/1024612/FrameGraph-Extensible-Rendering-Architecture-in ; https://www.slideshare.net/slideshow/framegraph-extensible-rendering-architecture-in-frostbite/72795495 — accessed 2026-09-08.
2. Loggini, R. "Render Graphs" (2021). https://logins.github.io/graphics/2021/05/31/RenderGraphs.html — accessed 2026-09-08.
3. Google Filament, "Physically Based Rendering in Filament". https://google.github.io/filament/Filament.md.html — accessed 2026-09-08.
4. Karis, B. "Real Shading in Unreal Engine 4", SIGGRAPH 2013. https://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf — accessed 2026-09-08.
5. Khronos Group, "Khronos PBR Neutral Tone Mapper Released" (2024). https://www.khronos.org/news/press/khronos-pbr-neutral-tone-mapper-released-for-true-to-life-color-rendering-of-3d-products — accessed 2026-09-08.
6. Blender PR #118936, "Add Khronos PBR Neutral tone mapper". https://projects.blender.org/blender/blender/pulls/118936 — accessed 2026-09-08.
7. model-viewer, "PBR Neutral Tone Mapping" example. https://modelviewer.dev/examples/tone-mapping — accessed 2026-09-08.
8. AgX Tonemapping Unity port. https://github.com/unitycoder/AgX-Tonemapping-Unity — accessed 2026-09-08.
9. Selan, J. "Using Lookup Tables to Accelerate Color Transformations", GPU Gems 2 ch. 24. https://developer.nvidia.com/gpugems/gpugems2/part-iii-high-quality-rendering/chapter-24-using-lookup-tables-accelerate-color — accessed 2026-09-08.
10. Jimenez, J. "Next Generation Post Processing in Call of Duty: Advanced Warfare", SIGGRAPH 2014. https://www.iryoku.com/next-generation-post-processing-in-call-of-duty-advanced-warfare/ ; https://advances.realtimerendering.com/s2014/ — accessed 2026-09-08.
11. Bjørge, M. "Bandwidth-Efficient Rendering", SIGGRAPH 2015 (dual filter). https://community.arm.com/cfs-file/__key/communityserver-blogs-components-weblogfiles/00-00-00-20-66/siggraph2015_2D00_mmg_2D00_marius_2D00_notes.pdf — accessed 2026-09-08.
12. LearnOpenGL, "Phys. Based Bloom" (2022). https://learnopengl.com/Guest-Articles/2022/Phys.-Based-Bloom — accessed 2026-09-08.
13. Wronski, B. "Separable disk-like depth of field" (2017). https://bartwronski.com/2017/08/06/separable-bokeh/ — accessed 2026-09-08.
14. AMD, "FidelityFX Depth of Field". https://gpuopen.com/manuals/fidelityfx_sdk/techniques/depth-of-field/ — accessed 2026-09-08.
15. Sousa, T. "Graphics Gems from CryENGINE 3", SIGGRAPH 2013. https://www.slideshare.net/TiagoAlexSousa/graphics-gems-from-cryengine-3-siggraph-2013 — accessed 2026-09-08.
16. Pettineo, M. "How To Fake Bokeh". https://therealmjp.github.io/posts/bokeh/ — accessed 2026-09-08.
17. McGuire, M. et al. "A Reconstruction Filter for Plausible Motion Blur", I3D 2012. https://www.researchgate.net/publication/254007575_A_Reconstruction_Filter_for_Plausible_Motion_Blur — accessed 2026-09-08.
18. Guertin, J.-P., McGuire, M., Nowrouzezahrai, D. "A Fast and Stable Feature-Aware Motion Blur Filter", HPG 2014. https://casual-effects.com/research/Guertin2014MotionBlur/Guertin2014MotionBlur-small.pdf — accessed 2026-09-08.
19. Karis, B. "High Quality Temporal Supersampling", SIGGRAPH 2014. https://de45xmedrsdbp.cloudfront.net/Resources/files/TemporalAA_small-59732822.pdf — accessed 2026-09-08.
20. Tardif, A. "Temporal Antialiasing Starter Pack". https://alextardif.com/TAA.html — accessed 2026-09-08.
21. Jimenez, J. et al. "Practical Realtime Strategies for Accurate Indirect Occlusion", SIGGRAPH 2016. https://www.iryoku.com/downloads/Practical-Realtime-Strategies-for-Accurate-Indirect-Occlusion.pdf — accessed 2026-09-08.
22. Intel, XeGTAO. https://github.com/GameTechDev/XeGTAO — accessed 2026-09-08.
23. Stachowiak, T. "Stochastic Screen-Space Reflections", SIGGRAPH 2015. https://www.ea.com/frostbite/news/stochastic-screen-space-reflections ; https://advances.realtimerendering.com/s2015/ — accessed 2026-09-08.
24. Wronski, B. "Volumetric Fog", SIGGRAPH 2014; AC4 GDC slides. https://advances.realtimerendering.com/s2014/ ; https://bartwronski.com/wp-content/uploads/2014/03/ac4_gdc.pdf ; https://bartwronski.com/publications/ — accessed 2026-09-08.
25. Hillaire, S. "Physically Based and Unified Volumetric Rendering in Frostbite", SIGGRAPH 2015. https://www.slideshare.net/slideshow/physically-based-and-unified-volumetric-rendering-in-frostbite/51840934 ; https://www.shadertoy.com/view/XlBSRz — accessed 2026-09-08.
26. Hillaire, S. "A Scalable and Production Ready Sky and Atmosphere Rendering Technique", EGSR 2020. https://onlinelibrary.wiley.com/doi/abs/10.1111/cgf.14050 ; https://diglib.eg.org/items/8a3e5350-18b3-46bd-9274-3add5af88c75 ; https://trist.am/blog/2024/atmosphere-rendering/ — accessed 2026-09-08.
27. Perlin, K. "Improving Noise" (2002), reference implementation. https://mrl.cs.nyu.edu/~perlin/noise/ — accessed 2026-09-08.
28. Gustavson, S. "Simplex noise demystified" (2005). https://cgvr.cs.uni-bremen.de/teaching/cg_literatur/simplexnoise.pdf — accessed 2026-09-08.
29. McEwan, I. et al. "Efficient computational noise in GLSL" (2012). https://ar5iv.labs.arxiv.org/html/1204.1461 — accessed 2026-09-08.
30. Worley, S. "A Cellular Texture Basis Function", SIGGRAPH 1996. https://dl.acm.org/doi/10.1145/237170.237267 — accessed 2026-09-08.
31. Quilez, I. "fBM". https://iquilezles.org/articles/fbm/ ; "Domain Warping". https://iquilezles.org/articles/warp/ — accessed 2026-09-08.
32. NVIDIA GPU Gems 2 ch. 26, "Implementing Improved Perlin Noise". https://developer.nvidia.com/gpugems/gpugems2/part-iii-high-quality-rendering/chapter-26-implementing-improved-perlin-noise — accessed 2026-09-08.
33. Hart, J. "Sphere tracing", The Visual Computer 1996. https://link.springer.com/article/10.1007/s003710050084 — accessed 2026-09-08.
34. Scratchapixel, "Rendering Distance Fields: Sphere Tracing". https://www.scratchapixel.com/lessons/advanced-rendering/rendering-distance-fields — accessed 2026-09-08.
35. Harris, M. "Fast Fluid Dynamics Simulation on the GPU", GPU Gems ch. 38. https://developer.nvidia.com/gpugems/gpugems/part-vi-beyond-triangles/chapter-38-fast-fluid-dynamics-simulation-gpu — accessed 2026-09-08.
36. Sims, K. "Reaction-Diffusion Tutorial". https://karlsims.com/rd.html — accessed 2026-09-08.
37. Webb, J. "Reaction-Diffusion Playground". https://jasonwebb.github.io/reaction-diffusion-playground/ — accessed 2026-09-08.
38. Codrops, "Reaction-Diffusion Compute Shader in WebGPU" (2024). https://tympanus.net/codrops/2024/05/01/reaction-diffusion-compute-shader-in-webgpu/ — accessed 2026-09-08.
39. Apple, WWDC25 "Discover Metal 4". https://developer.apple.com/videos/play/wwdc2025/205/ ; summary https://dev.to/arshtechpro/wwdc-2025-discover-metal-4-23f2 — accessed 2026-09-08.
40. Apple, Metal Feature Set Tables. https://developer.apple.com/metal/Metal-Feature-Set-Tables.pdf — referenced 2026-09-08.


## offline-rendering.md (54 unique URLs)

1. Derivative, TouchDesigner "Frame". https://docs.derivative.ca/Frame — accessed 2026-09-08.
2. Interactive & Immersive HQ, "Realtime Flag" (Introduction to TouchDesigner). https://github.com/interactiveimmersivehq/Introduction-to-touchdesigner/blob/master/User_Interface/2-8-Realtime-Flag.md — accessed 2026-09-08.
3. TouchDesigner forum, "Offline rendering via movieout TOP". https://forum.derivative.ca/t/offline-rendering-via-movieout-top/875 — accessed 2026-09-08.
4. Derivative, "Movie File Out TOP". https://docs.derivative.ca/Movie_File_Out_TOP — accessed 2026-09-08.
5. Interactive & Immersive HQ, "Export Movies in TouchDesigner". https://interactiveimmersive.io/blog/touchdesigner-lessons/output-movies-from-touchdesigner-like-a-pro/ — accessed 2026-09-08.
6. Artivoxa, "The Ultimate Guide to Rendering Motion Design in Houdini with Karma" (simulation ordering remark). https://www.artivoxa.com/the-ultimate-guide-to-rendering-motion-design-in-houdini-with-karma/ — accessed 2026-09-08.
7. O'Neill, M. PCG minimal C implementation. https://github.com/imneme/pcg-c-basic ; https://www.pcg-random.org/download.html — accessed 2026-09-08.
8. Cook, J. D. "Testing the PCG random number generator" (2017). https://www.johndcook.com/blog/2017/07/07/testing-the-pcg-random-number-generator/ — accessed 2026-09-08.
9. Jarzynski, M., Olano, M. "Hash Functions for GPU Rendering", JCGT 9(3), 2020. https://jcgt.org/published/0009/03/02/ — referenced from memory, not fetched, 2026-09-08.
10. Essentia, "Onset detection" tutorial; SIGMM Records overview. https://essentia.upf.edu/tutorial_rhythm_onsetdetection.html ; https://records.sigmm.org/2014/03/20/essentia-an-open-source-library-for-audio-analysis/ — accessed 2026-09-08.
11. "LibROSA: A Comprehensive Guide to Audio Analysis". https://medium.com/@noorfatimaafzalbutt/librosa-a-comprehensive-guide-to-audio-analysis-in-python-3f74fbb8f7f3 — accessed 2026-09-08.
12. "Deterministic Atomic Buffering", MICRO 2020. https://microarch.org/micro53/papers/738300a981.pdf — accessed 2026-09-08.
13. Hacker News, "GPUs are deterministic machines, even for floating point…". https://news.ycombinator.com/item?id=37007906 — accessed 2026-09-08.
14. "Hawkeye: Reproducing GPU-Level Non-Determinism". https://arxiv.org/pdf/2603.20421 — accessed 2026-09-08.
15. "mlf-core: a framework for deterministic machine learning". https://arxiv.org/pdf/2104.07651 — accessed 2026-09-08.
16. "How to defeat non-determinism in LLM inference". https://mlops.substack.com/p/how-to-defeat-non-determinism-in — accessed 2026-09-08.
17. Apple, Metal Programming Guide "Command Organization and Execution Model". https://developer.apple.com/library/archive/documentation/Miscellaneous/Conceptual/MetalProgrammingGuide/Cmd-Submiss/Cmd-Submiss.html — accessed 2026-09-08.
18. Apple, Metal Best Practices Guide "Triple Buffering". https://developer.apple.com/library/archive/documentation/3DDrawing/Conceptual/MTLBestPracticesGuide/TripleBuffering.html — accessed 2026-09-08 (read in full).
19. Apple, Metal Best Practices Guide "Resource Options". https://developer.apple.com/library/archive/documentation/3DDrawing/Conceptual/MTLBestPracticesGuide/ResourceOptions.html — accessed 2026-09-08.
20. Apple, `MTLBlitCommandEncoder.synchronize(resource:)`. https://developer.apple.com/documentation/metal/mtlblitcommandencoder/1400775-synchronizeresource — accessed 2026-09-08.
21. Apple Developer Forums, "Do managed texture in Apple silicon essentially function as shared textures?". https://developer.apple.com/forums/thread/710878 — accessed 2026-09-08.
22. metashapes, "Reading the Depth Buffer in Apple's Metal API". https://metashapes.com/blog/reading-depth-buffer-metal-api/ — accessed 2026-09-08.
23. syoyo/tinyexr. https://github.com/syoyo/tinyexr ; issue #15 https://github.com/syoyo/tinyexr/issues/15 — accessed 2026-09-08.
24. nothings/stb, "16 bit PNG Support" discussion #1614; issue #605; PR #1726. https://github.com/nothings/stb/discussions/1614 ; https://github.com/nothings/stb/issues/605 ; https://github.com/nothings/stb/pull/1726 — accessed 2026-09-08.
25. lvandeve/lodepng header. https://github.com/lvandeve/lodepng/blob/master/lodepng.h — accessed 2026-09-08.
26. FFmpeg, "License and Legal Considerations". https://www.ffmpeg.org/legal.html — accessed 2026-09-08 (read in full).
27. FFmpeg LICENSE.md. https://github.com/FFmpeg/FFmpeg/blob/master/LICENSE.md — accessed 2026-09-08.
28. x264 licensing. https://x264.org/licensing/ — accessed 2026-09-08.
29. Apple, WWDC20 "Decode ProRes with AVFoundation and VideoToolbox". https://developer.apple.com/videos/play/wwdc2020/10090/ — accessed 2026-09-08.
30. Apple, `AVVideoCodecType.proRes4444`. https://developer.apple.com/documentation/avfoundation/avvideocodectype/prores4444 — accessed 2026-09-08.
31. DSR Corporation, "prores-encoder-mac". https://github.com/DSRCorporation/prores-encoder-mac — accessed 2026-09-08.
32. ffmpeg-cookbook, "VideoToolbox Hardware Encoding (macOS / Apple Silicon)". https://ffmpeg-cookbook.com/en/articles/hardware-encode-videotoolbox/ — accessed 2026-09-08.
33. Epic Games, "Rendering High Quality Frames with Movie Render Queue". https://dev.epicgames.com/documentation/en-us/unreal-engine/rendering-high-quality-frames-with-movie-render-queue-in-unreal-engine — accessed 2026-09-08 (read in full).
34. Epic Games, "Cinematic Rendering Image Quality Settings". https://dev.epicgames.com/documentation/unreal-engine/cinematic-rendering-image-quality-settings-in-unreal-engine — accessed 2026-09-08.
35. Epic Developer Community, "Movie Render Queue, Warmup and First Frame Issues". https://dev.epicgames.com/community/learning/tutorials/l4OR/unreal-engine-movie-render-queue-warmup-and-first-frame-issues — accessed 2026-09-08.
36. Unreal forums, "Is there a way to not render warm up frames in Movie Render Graph?". https://forums.unrealengine.com/t/is-there-a-way-to-not-render-warm-up-frames-in-movie-render-graph/2629276 — accessed 2026-09-08.
37. Blender Manual, EEVEE "Motion Blur". https://docs.blender.org/manual/en/latest/render/eevee/render_settings/motion_blur.html — accessed 2026-09-08.
38. Blender Manual, Cycles "Sampling". https://docs.blender.org/manual/en/latest/render/cycles/render_settings/sampling.html — accessed 2026-09-08.
39. Blender bug #93534, "Persistent Data stops Animated Seed from working". https://developer.blender.org/T93534 — accessed 2026-09-08.
40. Notch Manual 0.9.23, "Export Video". https://manual.notch.one/0.9.23/en/docs/user-interface/exporting-video/ — accessed 2026-09-08 (read).
41. Notch Manual 0.9.23, "Motion Blur" node. https://manual.notch.one/0.9.23/en/docs/nodes/post-fx/blur/motion-blur/ — accessed 2026-09-08.
42. Andersson, P. et al. "FLIP: A Difference Evaluator for Alternating Images", HPG 2020. https://dl.acm.org/doi/10.1145/3406183 ; https://developer.nvidia.com/blog/wp-content/uploads/2020/07/flip-author-version-reduced-file-size.pdf ; C++ mirror https://github.com/rotoglup/nvidia-flip-cpp — accessed 2026-09-08.
43. Wang, Z. et al. "Image Quality Assessment: From Error Visibility to Structural Similarity", IEEE TIP 2004. https://ece.uwaterloo.ca/~z70wang/research/ssim/ — accessed 2026-09-08.
44. pHash design. https://phash.org/docs/design.html — accessed 2026-09-08.
45. Chromium, "GPU Pixel Testing With Gold". https://chromium.googlesource.com/chromium/src.git/+/master/docs/gpu/gpu_pixel_testing_with_gold.md — accessed 2026-09-08.
46. Rakhimov, S. "How to add difference tolerance to golden tests on Flutter". https://medium.com/mobilepeople/how-to-add-difference-tolerance-to-golden-tests-on-flutter-2d899c8baad2 — accessed 2026-09-08.
47. Repčík, T. "Easy Flutter Golden Tests with Tolerance" (2024). https://tomasrepcik.dev/blog/2024/2024-09-19-flutter-golden-test-with-tolerance/ — accessed 2026-09-08.


## audiovisual-systems.md (104 unique URLs)

All accessed 2026-09-08. Confidence: H = official doc fetched; M = official page partially
fetched or reputable secondary/search snippet; L = third-party snippet or direct fetch blocked.

| Ref | Source | URL | Conf. |
|---|---|---|---|
| TD-1 | Derivative docs — CHOP | https://docs.derivative.ca/CHOP | H |
| TD-2 | Derivative docs — Export | https://docs.derivative.ca/Export | H |
| TD-3 | Derivative docs — Time Slicing | https://docs.derivative.ca/Time_Slicing | H |
| TD-4 | Derivative docs — Audio Spectrum CHOP | https://docs.derivative.ca/Audio_Spectrum_CHOP | H |
| TD-5 | Derivative docs — Palette:audioAnalysis | https://docs.derivative.ca/Palette:audioAnalysis | H |
| TD-6 | Derivative docs — Lag CHOP | https://docs.derivative.ca/Lag_CHOP | H |
| TD-7 | Derivative docs — Filter CHOP | https://docs.derivative.ca/Filter_CHOP | H |
| TD-8 | Derivative docs — Math CHOP | https://docs.derivative.ca/Math_CHOP | H |
| TD-9 | Derivative docs — Beat CHOP | https://docs.derivative.ca/Beat_CHOP | H |
| TD-10 | Derivative docs — Trigger CHOP | https://docs.derivative.ca/Trigger_CHOP | H |
| TD-11 | Derivative docs — Envelope CHOP | https://docs.derivative.ca/Envelope_CHOP | H |
| TD-12 | Derivative docs — GLSL TOP | https://docs.derivative.ca/GLSL_TOP | H |
| TD-13 | Derivative docs — Timeline | https://docs.derivative.ca/Timeline | H |
| TD-14 | Derivative docs — Animation COMP | https://docs.derivative.ca/Animation_COMP | H |
| TD-15 | Derivative docs — Movie File Out TOP | https://docs.derivative.ca/Movie_File_Out_TOP | H |
| TD-16 | Derivative docs — .toe | https://docs.derivative.ca/.toe | H |
| TD-17 | Derivative docs — MIDI Out / OSC In / DMX Out CHOP | https://docs.derivative.ca/MIDI_Out_CHOP ; https://docs.derivative.ca/OSC_In_CHOP ; https://docs.derivative.ca/DMX_Out_CHOP | M |
| NOTCH-1 | Notch Manual 1.0 — Nodes | https://manual.notch.one/1.0/en/topic/nodes | M |
| NOTCH-2 | Notch Manual 1.0 — Nodegraph | https://manual.notch.one/1.0/en/docs/reference/user-interface/nodegraph/ | M |
| NOTCH-3 | Notch Manual 1.0 — Sound Modifier | https://manual.notch.one/1.0/en/docs/reference/nodes/modifiers/sound-modifier/ | H |
| NOTCH-4 | Notch Manual 1.0 — Modifiers index | https://manual.notch.one/1.0/en/docs/reference/nodes/modifiers/ | H |
| NOTCH-5 | Notch Manual 0.9.23 — Math Modifier | https://manual.notch.one/0.9.23/en/docs/nodes/modifiers/math-modifier/ | H |
| NOTCH-6 | Notch Manual 0.9.22 — Beat Pulse Modifier | http://manual.notch.one/0.9.22/en/topic/nodes-modifiers-beat-pulse-modifier | H |
| NOTCH-7 | Notch Manual 2026.1 — Working With Audio | https://manual.notch.one/2026.1/en/docs/learning/working-with-audio/ | M |
| NOTCH-8 | Notch Manual 1.0 — Sound FFT Modifier | https://manual.notch.one/1.0/en/docs/reference/nodes/modifiers/sound-fft-modifier/ | M |
| NOTCH-9 | Notch Manual 2026.1 — Exposed Parameters | https://manual.notch.one/2026.1/en/docs/workflows/working-with-media-servers/exposed-parameters/ | M |
| NOTCH-10 | Notch Manual 2026.1 — Notch Blocks | https://manual.notch.one/2026.1/en/docs/workflows/working-with-media-servers/blocks/ | M |
| NOTCH-11 | Notch Blocks feature page; Smode Notch Block doc | https://www.notch.one/features/notch-blocks ; https://www.smode.io/doc/ref/compo/integrations/notch-block-.dfxdll-file.htm | M |
| NOTCH-12 | Notch Manual 0.9.23 — Export Video | https://manual.notch.one/0.9.23/en/docs/user-interface/exporting-video/ | H |
| NOTCH-13 | Notch Manual 2026.1 — Rendering Basics / Timeline | https://manual.notch.one/2026.1/en/docs/learning/rendering/rendering-basics/ ; https://manual.notch.one/2026.1/en/docs/reference/user-interface/timeline/ | M |
| UE-1 | Epic — Key Concepts in Niagara | https://dev.epicgames.com/documentation/en-us/unreal-engine/key-concepts-in-niagara-effects-for-unreal-engine | H |
| UE-2 | ibbles — Niagara user exposed parameters | https://github.com/ibbles/LearningUnrealEngine/blob/master/Niagara%20user%20exposed%20parameters.md | M |
| UE-3 | Epic — UProperties | https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-uproperties | H |
| UE-4 | Epic — Material Parameter Collections | https://dev.epicgames.com/documentation/en-us/unreal-engine/using-material-parameter-collections-in-unreal-engine | L |
| UE-5 | Epic — Cinematics and Movie Making (Sequencer) | https://dev.epicgames.com/documentation/en-us/unreal-engine/cinematics-and-movie-making-in-unreal-engine | M |
| UE-6 | Epic — Rendering High Quality Frames with MRQ | https://dev.epicgames.com/documentation/unreal-engine/rendering-high-quality-frames-with-movie-render-queue-in-unreal-engine?lang=en-US | H |
| UE-7 | Epic — Audio Synesthesia | https://dev.epicgames.com/documentation/en-us/unreal-engine/audio-synesthesia-in-unreal-engine | H |
| UE-8 | Epic — Add Spectral Analysis Delegate; Overview of submixes | https://dev.epicgames.com/documentation/en-us/unreal-engine/BlueprintAPI/Audio/Spectrum/AddSpectralAnalysisDelegate ; https://dev.epicgames.com/documentation/unreal-engine/overview-of-submixes-in-unreal-engine | M |
| UN-1 | Unity VFX Graph — Graph Logic and Philosophy | https://docs.unity3d.com/Packages/com.unity.visualeffectgraph@17.0/manual/GraphLogicAndPhilosophy.html | H |
| UN-2 | Unity VFX Graph — Properties; Blackboard | https://docs.unity3d.com/Packages/com.unity.visualeffectgraph@17.2/manual/Properties.html ; https://docs.unity3d.com/Packages/com.unity.visualeffectgraph@17.2/manual/Blackboard.html | H |
| UN-3 | Unity Scripting API — VisualEffect.SetFloat | https://docs.unity3d.com/ScriptReference/VFX.VisualEffect.SetFloat.html | M |
| UN-4 | Unity Timeline — assets and instances | https://docs.unity3d.com/Packages/com.unity.timeline@1.8/manual/tl-overview.html | H |
| UN-5 | Unity Manual — Playables API | https://docs.unity3d.com/Manual/Playables.html | M |
| UN-6 | Unity Shader Graph — Property Types | https://docs.unity3d.com/Packages/com.unity.shadergraph@17.0/manual/Property-Types.html | H |
| UN-7 | Unity Recorder 5.1 manual | https://docs.unity3d.com/Packages/com.unity.recorder@5.1/manual/index.html | H |
| UN-8 | Unity Cinemachine — camera control/transitions; blending | https://docs.unity3d.com/Packages/com.unity.cinemachine@3.1/manual/concept-camera-control-transitions.html ; https://docs.unity3d.com/Packages/com.unity.cinemachine@2.3/manual/CinemachineBlending.html | M |
| RES-1 | Resolume — Parameter Animation | https://resolume.com/support/en/parameter-animation | H |
| RES-2 | Resolume — Wire FFT | https://resolume.com/support/en/wire-fft | H |
| RES-3 | Resolume — ISF | https://resolume.com/support/en/isf | H |
| RES-4 | Resolume — OSC | https://resolume.com/support/en/osc | H |
| RES-5 | Resolume forum (avc format); Resolume-Composition-Converter | https://resolume.com/forum/viewtopic.php?t=12760 ; https://github.com/tijnisfijn/Resolume-Composition-Converter | M |
| ISF-1 | ISF Specification (mrRay/ISF_Spec) | https://github.com/mrRay/ISF_Spec | H |
| PR-1 | processing-sound Javadoc — FFT | https://processing.github.io/processing-sound/processing/sound/FFT.html | H |
| PR-2 | processing-sound Javadoc — BeatDetector | https://processing.github.io/processing-sound/processing/sound/BeatDetector.html | H |
| PR-3 | Minim — BeatDetect | https://code.compartmental.net/minim/beatdetect_class_beatdetect.html | H |
| HY-1 | Hydra — API reference | https://hydra.ojack.xyz/api/ | M |
| HY-2 | Hydra docs — Audio | https://hydra.ojack.xyz/docs/docs/learning/interactivity/audio/ | H |
| MAX-1 | Cycling '74 — Jitter Matrix user guide | https://docs.cycling74.com/userguide/jitter/matrix/ | H |
| VVVV-1 | The Gray Book — Spreads and Other Collections | https://thegraybook.vvvv.org/introduction/lo_9_2_Spreads.html | H |
| VVVV-2 | The Gray Book — The Language (beta vs gamma) | https://thegraybook.vvvv.org/reference/getting-started/beta/language.html | H |
| MAG-1 | Magic Music Visuals — User's Guide / Features (403; snippets) | https://magicmusicvisuals.com/downloads/Magic_UsersGuide.html ; https://magicmusicvisuals.com/features | L |
| SYN-1 | Synesthesia Docs — SSF | https://app.synesthesia.live/docs/ssf/ssf.html | H |
| SYN-2 | Synesthesia Docs — Standard Uniforms | https://app.synesthesia.live/docs/ssf/standard_uniforms.html | H |
| SYN-3 | Synesthesia Docs — Audio Uniforms | https://app.synesthesia.live/docs/ssf/audio_uniforms.html | H |
| SYN-4 | Synesthesia Docs — JavaScript Scripting | https://app.synesthesia.live/docs/ssf/script.html | H |
| SYN-5 | Synesthesia Docs index | https://synesthesia.live/docs/ | M |
| MD-1 | Geiss — MilkDrop Preset Authoring Guide | https://www.geisswerks.com/milkdrop/milkdrop_preset_authoring.html | H |
| MD-2 | Winamp Dev Wiki — MilkDrop Preset Authoring | http://wiki.winamp.com/wiki/MilkDrop_Preset_Authoring | M |
| CAB-1 | cables.gl docs index | https://cables.gl/docs | M |
| CAB-2 | cables.gl — Ports | https://cables.gl/docs/5_writing_ops/dev_creating_ports/dev_creating_ports | H |
| CAB-3 | cables.gl — Guidelines / Developing Ops | https://cables.gl/docs/5_writing_ops/guidelines/guidelines ; https://cables.gl/docs/5_writing_ops/dev_ops/dev_ops | M |
| CAB-4 | cables.gl — Real-Time Audio Analyzation | https://cables.gl/docs/8_audio/2_realtime_visualization/realtime_visualization | H |
| GD-1 | Godot — AudioEffectSpectrumAnalyzer | https://docs.godotengine.org/en/stable/classes/class_audioeffectspectrumanalyzer.html | H |
| GD-2 | Godot — AudioEffectSpectrumAnalyzerInstance | https://docs.godotengine.org/en/stable/classes/class_audioeffectspectrumanalyzerinstance.html | H |
| GD-3 | Godot Asset Library — Audio Spectrum Visualizer Demo | https://godotengine.org/asset-library/asset/2762 | M |
| GD-4 | Godot — Shading language | https://docs.godotengine.org/en/stable/tutorials/shaders/shader_reference/shading_language.html | H |
| GD-5 | Godot — Visual shaders | https://docs.godotengine.org/en/stable/tutorials/shaders/visual_shaders.html | H |
| GD-6 | Godot — GDScript exported properties | https://docs.godotengine.org/en/stable/tutorials/scripting/gdscript/gdscript_exports.html | H |
| ECS-1 | EnTT — entity.md | https://raw.githubusercontent.com/skypjack/entt/master/docs/md/entity.md | H |
| ECS-2 | flecs — Quickstart | https://www.flecs.dev/flecs/md_docs_2Quickstart.html | H |
| REF-1 | O3DE — Reflection | https://docs.o3de.org/docs/user-guide/programming/components/reflection/ | H |
| BL-1 | Blender Manual source — Drivers Panel | https://projects.blender.org/blender/blender-manual/raw/branch/main/manual/animation/drivers/drivers_panel.rst | H |
| BL-2 | Blender Python API — bpy.types.Driver | https://docs.blender.org/api/current/bpy.types.Driver.html | M |
| BL-3 | Blender Manual source — F-Curve Modifiers | https://projects.blender.org/blender/blender-manual/raw/branch/main/manual/editors/graph_editor/fcurves/modifiers.rst | H |
| BL-4 | site-builder.wiki — Audio Visualizer [Blender] | https://site-builder.wiki/posts/39611 | L |
| ABL-1 | Ableton Reference Manual 12 — Racks | https://www.ableton.com/en/manual/instrument-drum-and-effect-racks/ | H |
| BW-1 | Bitwig User Guide — Modulators | https://www.bitwig.com/userguide/latest/modulator/ | H |
| BW-2 | polarity.me — Bitwig Modulation System | https://polarity.me/posts/bitwig-guides/2022-04-12-the-modulation-system-bitwig-modulator-guide/ | M |
| VIT-1 | Vital User Guide — Modulation (D. Vogel) | https://davidmvogel.com/docs/Vital/UserGuide/Modulation | M |
| SL-1 | SynthLab SDK — Modulation Matrix | https://www.willpirkle.com/synthlab/docs/html/mod_matrix.html | H |
| MM-1 | Attack Magazine — What is a modulation matrix; ResearchGate paper | https://www.attackmagazine.com/technique/synth-secrets/what-is-a-modulation-matrix-how-does-it-work/ ; https://www.researchgate.net/publication/228732325_A_modulation_matrix_for_complex_parameter_sets | M |
| OSC-1 | OpenSoundControl 1.0 Specification | https://opensoundcontrol.stanford.edu/spec-1_0.html | H |
| OE-1 | 1€ Filter project page (Casiez et al.) | https://gery.casiez.net/1euro/ | H |


## tooling.md (93 unique URLs)

All accessed 2026-09-08.

- CMake 4.0 release notes — https://cmake.org/cmake/help/latest/release/4.0.html
- CMake release index — https://cmake.org/cmake/help/latest/release/index.html
- Kitware/CMake releases — https://github.com/kitware/cmake/releases
- CMake FetchContent module — https://cmake.org/cmake/help/latest/module/FetchContent.html
- CMake presets manual — https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html
- CMake C++ modules manual — https://cmake.org/cmake/help/latest/manual/cmake-cxxmodules.7.html
- CMake enable_language — https://cmake.org/cmake/help/latest/command/enable_language.html
- CMake MACOSX_BUNDLE — https://cmake.org/cmake/help/latest/prop_tgt/MACOSX_BUNDLE.html
- CMake `<LANG>_CLANG_TIDY` — https://cmake.org/cmake/help/latest/prop_tgt/LANG_CLANG_TIDY.html
- Fedora CMake 4.0 change page — https://fedoraproject.org/wiki/Changes/CMake4.0
- CPM.cmake — https://github.com/cpm-cmake/CPM.cmake ; releases — https://github.com/cpm-cmake/CPM.cmake/releases
- vcpkg manifest mode — https://learn.microsoft.com/en-us/vcpkg/concepts/manifest-mode
- vcpkg binary caching — https://learn.microsoft.com/en-us/vcpkg/consume/binary-caching-overview
- vcpkg triplets — https://learn.microsoft.com/en-us/vcpkg/concepts/triplets ; https://learn.microsoft.com/en-us/vcpkg/users/triplets
- vcpkg repo (triplets directory) — https://github.com/microsoft/vcpkg
- Conan 2 introduction — https://docs.conan.io/2/introduction.html ; PyPI — https://pypi.org/project/conan/ ; releases — https://github.com/conan-io/conan/releases
- Dear ImGui — https://github.com/ocornut/imgui ; backends — https://github.com/ocornut/imgui/tree/master/backends ; changelog — https://raw.githubusercontent.com/ocornut/imgui/master/docs/CHANGELOG.txt ; releases — https://github.com/ocornut/imgui/releases
- ImPlot — https://github.com/epezent/implot ; releases — https://github.com/epezent/implot/releases
- bgfx ImGui example — https://github.com/bkaradzic/bgfx/tree/master/examples/common/imgui
- sokol_imgui.h — https://github.com/floooh/sokol/blob/master/util/sokol_imgui.h
- wgpu-native — https://github.com/gfx-rs/wgpu-native
- Nuklear — https://github.com/Immediate-Mode-UI/Nuklear
- RmlUi — https://github.com/mikke89/RmlUi
- Qt releases / licensing — https://doc.qt.io/qt-6/qt-releases.html ; https://www.qt.io/blog/qt-6.10.1-released
- GLFW releases — https://github.com/glfw/glfw/releases ; news — https://www.glfw.org/docs/latest/news.html ; native access — https://www.glfw.org/docs/latest/group__native.html
- SDL releases — https://github.com/libsdl-org/SDL/releases ; SDL_GPU — https://wiki.libsdl.org/SDL3/CategoryGPU ; SDL_Metal_CreateView — https://wiki.libsdl.org/SDL3/SDL_Metal_CreateView ; Audio — https://wiki.libsdl.org/SDL3/CategoryAudio
- Catch2 — https://github.com/catchorg/Catch2 ; CMake integration — https://github.com/catchorg/Catch2/blob/devel/docs/cmake-integration.md
- GoogleTest — https://github.com/google/googletest
- doctest — https://github.com/doctest/doctest ; releases — https://github.com/doctest/doctest/releases
- ApprovalTests.cpp — https://github.com/approvals/ApprovalTests.cpp
- Google Benchmark — https://github.com/google/benchmark
- nanobench — https://github.com/martinus/nanobench
- spdlog — https://github.com/gabime/spdlog ; releases — https://github.com/gabime/spdlog/releases
- fmt — https://github.com/fmtlib/fmt ; releases — https://github.com/fmtlib/fmt/releases
- quill — https://github.com/odygrd/quill
- glog (archived) — https://github.com/google/glog
- GLM — https://github.com/g-truc/glm ; manual — https://github.com/g-truc/glm/blob/master/manual.md
- Eigen releases — https://gitlab.com/libeigen/eigen/-/releases
- DirectXMath — https://github.com/microsoft/DirectXMath
- HandmadeMath — https://github.com/HandmadeMath/HandmadeMath
- linalg.h — https://github.com/sgorsten/linalg
- nlohmann/json — https://github.com/nlohmann/json
- simdjson — https://github.com/simdjson/simdjson
- RapidJSON — https://github.com/Tencent/rapidjson
- glaze — https://github.com/stephenberry/glaze ; P2996 docs — https://stephenberry.github.io/glaze/p2996-reflection/
- toml++ — https://github.com/marzer/tomlplusplus
- yaml-cpp — https://github.com/jbeder/yaml-cpp
- cereal — https://github.com/USCiLab/cereal
- EnTT — https://github.com/skypjack/entt ; v4.0.0 — https://github.com/skypjack/entt/releases/tag/v4.0.0
- flecs — https://github.com/SanderMertens/flecs
- RTTR — https://github.com/rttrorg/rttr
- refl-cpp — https://github.com/veselink1/refl-cpp
- Clang C++ status — https://clang.llvm.org/cxx_status.html
- GCC C++ status — https://gcc.gnu.org/projects/cxx-status.html
- libc++ C++23 status — https://libcxx.llvm.org/Status/Cxx23.html
- MSVC language conformance — https://learn.microsoft.com/en-us/cpp/overview/visual-cpp-language-conformance
- MSVC STL changelog — https://github.com/microsoft/STL/wiki/Changelog
- Bloomberg clang-p2996 — https://github.com/bloomberg/clang-p2996
- efsw — https://github.com/SpartanJ/efsw
- dmon — https://github.com/septag/dmon
- Apple kqueue vs FSEvents guide — https://developer.apple.com/library/archive/documentation/Darwin/Conceptual/FSEvents_ProgGuide/KernelQueues/KernelQueues.html
- Tracy — https://github.com/wolfpld/tracy ; NEWS — https://raw.githubusercontent.com/wolfpld/tracy/master/NEWS
- Clang ThreadSanitizer — https://clang.llvm.org/docs/ThreadSanitizer.html
- ccache manual — https://ccache.dev/manual/latest.html ; repo — https://github.com/ccache/ccache
- Xcode 26.6 release — https://developer.apple.com/news/releases/?id=06252026a
- Apple Hardened Runtime — https://developer.apple.com/documentation/security/hardened-runtime
- Apple audio-input entitlement — https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.security.device.audio-input
- Apple NSMicrophoneUsageDescription — https://developer.apple.com/documentation/BundleResources/Information-Property-List/NSMicrophoneUsageDescription
- Apple forum: mic prompt under Hardened Runtime — https://developer.apple.com/forums/thread/741303
- LLVM lld ad-hoc signing on arm64 — https://reviews.llvm.org/D97994
