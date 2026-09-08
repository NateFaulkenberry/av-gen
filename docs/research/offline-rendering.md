# Research: Deterministic Offline Rendering

**Status:** research only — milestone 0.1 does NOT implement the full offline pipeline, but the constraints in §12 must be honoured from the first commit because they are cheap now and expensive to retrofit.
**Purpose:** define how the same engine that runs live at a festival renders bit-stable (or perceptually-stable) frame sequences for video production, and what that imposes on time, RNG, audio, GPU work, readback, encoding and testing.
**Target platform:** macOS 26, Apple M2 Max, Metal 4.
**Date of research:** 2026-09-08. All URLs accessed 2026-09-08 unless noted.

Format: *Source* · *URL* · *Accessed* · *What was learned* · *Relevance* · *Confidence / limitations*.

---

## 1. Time: `renderTime = frameIndex / fps`, never the wall clock

- **Source:** TouchDesigner documentation "Frame" and "Realtime Flag"; TouchDesigner forum "Offline rendering via movieout TOP"; Interactive & Immersive HQ "Export Movies in TouchDesigner".
- **URL:** https://docs.derivative.ca/Frame ; https://github.com/interactiveimmersivehq/Introduction-to-touchdesigner/blob/master/User_Interface/2-8-Realtime-Flag.md ; https://forum.derivative.ca/t/offline-rendering-via-movieout-top/875 ; https://interactiveimmersive.io/blog/touchdesigner-lessons/output-movies-from-touchdesigner-like-a-pro/
- **Accessed:** 2026-09-08.
- **What was learned:** TouchDesigner's whole offline story is a single switch: turn off the **Realtime flag** and the engine "prioritises frame rendering over real-world time", taking as long as each frame needs and never skipping. In realtime mode it drops frames to keep up with the clock (time-sliced CHOPs interpolate the gap). The timeline's FPS defines the cook rate; frame N is defined by the timeline, not by when it is cooked.
- **Relevance:** This is the model to copy. av-gen needs one `Clock` abstraction with two implementations: `RealtimeClock` (time = wall time, dt clamped, may skip) and `FixedStepClock` (time = frameIndex / fps, dt = 1/fps exactly, never skips). Every subsystem (simulation, animation curves, audio lookup, noise `time` uniforms, TAA jitter index) must read time and frame index from this object and nothing else.
- **Confidence / limitations:** High.

### 1.1 Fixed-step simulation

- **What was learned (synthesis):** Simulations that integrate state (particles, fluids, reaction-diffusion, physics) are path-dependent: Houdini's documentation community phrasing is that "frame 1042 is computed from 1041 … all the way back to frame 1" (https://www.artivoxa.com/the-ultimate-guide-to-rendering-motion-design-in-houdini-with-karma/ — accessed 2026-09-08; secondary source). Therefore offline rendering of frame N requires simulating frames 0..N in order with identical `dt`, and the live path should use a **fixed simulation step with accumulator** (e.g. 60 or 120 Hz) so that live and offline share the same integration behaviour, with rendering interpolating or simply using the latest state.
- **Relevance:** `dt` must be a parameter to every simulation pass, and the frame loop must support "simulate K fixed steps then render once" (live) and "simulate exactly S sub-steps per output frame" (offline). No pass may call a clock.
- **Confidence / limitations:** High (standard practice; Gaffer-on-Games style fixed timestep).

---

## 2. Deterministic random numbers

- **Source:** O'Neill, M. PCG family — minimal C implementation; PCG website; John D. Cook's test write-up.
- **URL:** https://github.com/imneme/pcg-c-basic ; https://www.pcg-random.org/download.html ; https://www.johndcook.com/blog/2017/07/07/testing-the-pcg-random-number-generator/
- **Accessed:** 2026-09-08.
- **What was learned:** `pcg32` is a tiny (state + increment, 64-bit LCG + output permutation) generator with excellent statistical quality (passes NIST STS and DieHarder in Cook's tests), C89-portable, and — crucially — supports independent **streams** via the increment and cheap **seeding from (seed, stream)** pairs. That makes "seed = hash(globalSeed, frameIndex, systemId)" trivial, so any frame can be regenerated in isolation for CPU-side randomness. `std::random_device` is by definition nondeterministic and must never be used in the render path; `std::mt19937` is deterministic but its distribution adaptors (`std::uniform_real_distribution` etc.) are **not guaranteed identical across standard-library implementations**, so use hand-written conversions (e.g. `(x >> 8) * 0x1p-24f`).
- **GPU side:** shaders cannot carry PCG state per thread cheaply, so use stateless hash functions of `(particleIndex, frameIndex, salt)` — e.g. PCG-hash (`pcg3d`/`pcg4d` from Jarzynski & Olano 2020, "Hash Functions for GPU Rendering", JCGT; https://jcgt.org/published/0009/03/02/ — referenced from memory, not fetched, 2026-09-08). These are pure functions of their inputs and therefore reproducible across GPUs (integer arithmetic is exact).
- **Relevance:** RNG must be an injected object (`Rng` interface) seeded from the frame clock; the GPU shared shader library must provide integer hash RNG; no `rand()`/`random_device`/time-based seeding anywhere.
- **Confidence / limitations:** High for PCG facts; the distribution-portability caveat is well known (C++ standard leaves algorithms unspecified).

---

## 3. Audio analysis indexed by timeline position

- **Source:** Essentia documentation (onset detection tutorial; library overview); librosa overview.
- **URL:** https://essentia.upf.edu/tutorial_rhythm_onsetdetection.html ; https://records.sigmm.org/2014/03/20/essentia-an-open-source-library-for-audio-analysis/ ; https://medium.com/@noorfatimaafzalbutt/librosa-a-comprehensive-guide-to-audio-analysis-in-python-3f74fbb8f7f3
- **Accessed:** 2026-09-08.
- **What was learned:** Essentia (C++ with Python bindings, AGPL/commercial) and librosa provide frame-based analysis pipelines: frame cutter → window → FFT → magnitude/phase → onset detection functions (energy, spectral flux, HFC, complex domain) → beat tracking/BPM. All are deterministic functions of the audio samples and hop size; native C++ libraries are preferred in industry for speed.
- **Relevance (design):**
  - Live: analysis runs on the incoming audio ring buffer with inherent latency; features are time-stamped with the audio clock.
  - Offline: analysis must be **precomputed over the whole file** into an *analysis track* — an array of feature frames at a fixed hop (e.g. 512 samples @ 48 kHz ≈ 10.7 ms), plus discrete onset/beat events with sample-accurate times. The renderer queries `features.at(renderTime)` with interpolation, so frame N always sees the same feature values regardless of machine speed.
  - Both paths should expose the *same* `AudioFeatures` interface; only the provider differs (`LiveAnalyzer` vs `PrecomputedAnalysisTrack`). Store the analysis track on disk (e.g. JSON/CBOR/NPZ-like) so re-renders and tests do not depend on the analyzer version. Essentia's AGPL licence matters for a closed product; the FFT itself can be done with Accelerate/vDSP on macOS.
- **Confidence / limitations:** High on the library facts; the design is a synthesis.

---

## 4. GPU determinism caveats

- **Source:** "Deterministic Atomic Buffering" (MICRO 2020); Hacker News thread "GPUs are deterministic machines, even for floating point"; ML-determinism write-ups (Hawkeye, mlf-core, "How to defeat non-determinism in LLM inference").
- **URL:** https://microarch.org/micro53/papers/738300a981.pdf ; https://news.ycombinator.com/item?id=37007906 ; https://arxiv.org/pdf/2603.20421 ; https://arxiv.org/pdf/2104.07651 ; https://mlops.substack.com/p/how-to-defeat-non-determinism-in
- **Accessed:** 2026-09-08.
- **What was learned:**
  - A single GPU running a fixed kernel with fixed inputs, fixed dispatch geometry and **no timing-dependent operations** produces bit-identical results run to run. Nondeterminism enters through (a) **atomics whose accumulation order depends on scheduling** (float `atomicAdd` reductions, append-buffer ordering), (b) different kernels/drivers/hardware choosing different instruction schedules, fused multiply-add contraction, fast-math reassociation, or different accumulation structures (e.g. tensor cores), (c) work-distribution-dependent algorithms (decoupled look-back, wave-order-dependent compaction).
  - Deterministic Atomic Buffering (MICRO 2020) and the ML frameworks reach determinism by **replacing float atomics with ordered reductions** (sort-by-key + segmented reduce, or per-block partial sums combined in fixed order).
  - Across *different* GPUs or driver versions, bit-identity is generally unattainable for floating point; the realistic goal is **perceptual identity** with a tolerance (see §10).
- **Specific av-gen consequences:**
  1. Particle emission via atomic counters yields a scheduling-dependent index assignment. The particle *set* is the same, but per-index seeds, sort order (for equal keys) and ribbon connectivity can differ → visible flicker between runs. Deterministic mode must use prefix-sum compaction (particles.md §2.1) and seed randomness from a stable per-particle ID (spawn counter assigned in fixed order), not from pool index.
  2. Float atomics (P2G in MPM/FLIP, histogram bloom, luminance averaging for auto-exposure) must have an ordered-reduction alternative in deterministic mode.
  3. Auto-exposure and any other **frame-to-frame feedback** (TAA history, temporal fog reprojection, SSR history, reaction-diffusion, trails) make frame N depend on all previous frames → offline render must start from a defined state and use warm-up (§9).
  4. Metal compiles MSL per device at pipeline creation; `-ffast-math` is **on by default** in Metal (MSL spec; verify in Xcode build settings `MTL_FAST_MATH`) — deterministic builds should compile with fast-math disabled and with consistent FMA contraction settings so the same source yields the same numerics on the same GPU family.
  5. Do not use MetalFX temporal upscaling or frame interpolation in offline mode.
  6. Threadgroup sizes and dispatch geometry must be fixed constants per kernel (not derived from measured occupancy) so reductions combine in the same order.
- **Relevance:** Determinism is an *architectural* property (kernel design, seeding, state reset), not a flag applied at the end.
- **Confidence / limitations:** High on principles; the MSL fast-math default should be verified against the current Metal Shading Language Specification before relying on it.

---

## 5. Headless rendering into offscreen textures

- **Source:** Apple Metal Programming Guide (command organisation), Metal best-practices; Notch/TouchDesigner batch behaviour (§9).
- **URL:** https://developer.apple.com/library/archive/documentation/Miscellaneous/Conceptual/MetalProgrammingGuide/Cmd-Submiss/Cmd-Submiss.html ; https://manual.notch.one/0.9.23/en/docs/user-interface/exporting-video/
- **Accessed:** 2026-09-08.
- **What was learned:** Metal needs no window: create `MTLDevice`, `MTLCommandQueue`, render into `MTLTexture`s created with `MTLTextureUsage.renderTarget | .shaderRead` and `framebufferOnly = false` (drawables from `CAMetalLayer` default to `framebufferOnly = true` and cannot be read). Notch runs exports in batch mode without the application window when invoked from the command line; TouchDesigner offline export is the same engine with the realtime flag off.
- **Relevance:** The render graph's final sink must be abstract: `DrawableSink` (present) vs `TextureSink` (offscreen, readable). Nothing in the frame may depend on a `CAMetalLayer`, screen refresh, or `MTKView` callback. Offline resolution and pixel format (e.g. `RGBA16Float` scene-linear) may differ from the live window.
- **Confidence / limitations:** High.

---

## 6. Readback without stalls

- **Source:** Apple Metal Best Practices Guide — "Triple Buffering" and "Resource Options"; `MTLBlitCommandEncoder.synchronize(resource:)`; Apple forum "Do managed textures on Apple silicon function as shared textures?"; metashapes "Reading the depth buffer in Apple's Metal API".
- **URL:** https://developer.apple.com/library/archive/documentation/3DDrawing/Conceptual/MTLBestPracticesGuide/TripleBuffering.html ; https://developer.apple.com/library/archive/documentation/3DDrawing/Conceptual/MTLBestPracticesGuide/ResourceOptions.html ; https://developer.apple.com/documentation/metal/mtlblitcommandencoder/1400775-synchronizeresource ; https://developer.apple.com/forums/thread/710878 ; https://metashapes.com/blog/reading-depth-buffer-metal-api/
- **Accessed:** 2026-09-08.
- **What was learned:**
  - Apple's triple-buffering guidance (read in full): keep the CPU one or more frames ahead of the GPU with a ring of **3** dynamic buffers guarded by a `dispatch_semaphore` initialised to 3; each command buffer's `addCompletedHandler` signals the semaphore; 3 is called the "ideal" balance of utilisation, memory and latency; never allocate per frame.
  - Readback: encode a **blit from the render texture to an `MTLBuffer`** (`copy(from:texture … to:buffer … bytesPerRow …)`), commit, and only touch the buffer's contents after the command buffer completes (`waitUntilCompleted` or the completed handler). On Apple silicon's unified memory, **shared** storage is directly CPU-visible; **managed** resources behave like shared ("Metal may ignore synchronization calls completely" in the unified model) but `synchronize(resource:)` is still required for portability to discrete-GPU Macs. Texture `getBytes` works but forces a sync; blit-to-buffer is the recommended path.
  - Row pitch: `bytesPerRow` must satisfy the device's buffer-alignment requirements (query `minimumLinearTextureAlignment(for:)`; 256 bytes is a safe conservative value on macOS); the CPU-side encoder must handle padded rows.
- **Design for av-gen (synthesis):** a **readback ring of 3 staging buffers** (`storageModeShared`): frame N renders → blit into `staging[N % 3]` → completion handler enqueues (N, buffer) to an encoder thread; the CPU waits on the semaphore only when all 3 are in flight. This overlaps GPU rendering of N+1/N+2 with PNG/EXR encoding of N. Encoding is the bottleneck, not the readback (unified memory), so encoding must be on a thread pool. The offline loop does not need vsync; submit as fast as the ring allows.
- **Relevance:** Readback is the *only* GPU→CPU path in the engine; the abstraction must expose "blit texture to shared buffer + completion callback" and a frame-in-flight limit.
- **Confidence / limitations:** High for the Apple guidance; the Apple "Reading pixel data from a drawable texture" article did not render through the fetch tool, so exact API names are from the linked archive pages and general Metal knowledge.

---

## 7. Image sequence output: PNG / EXR

- **Source:** syoyo/tinyexr README and issues; nothings/stb discussion #1614, issue #605 and PR #1726; lvandeve/lodepng header.
- **URL:** https://github.com/syoyo/tinyexr ; https://github.com/syoyo/tinyexr/issues/15 ; https://github.com/nothings/stb/discussions/1614 ; https://github.com/nothings/stb/issues/605 ; https://github.com/nothings/stb/pull/1726 ; https://github.com/lvandeve/lodepng/blob/master/lodepng.h
- **Accessed:** 2026-09-08.
- **What was learned:**
  - **tinyexr** (BSD-3, single header): loads/saves OpenEXR; when saving with `TINYEXR_PIXELTYPE_HALF` the caller supplies float32 and tinyexr converts to half; supports multiple channels, ZIP/PIZ compression. Right choice for scene-linear HDR frames (16-bit half or 32-bit float), alpha, and AOVs.
  - **stb_image_write** (public domain): `stbi_write_png` **only supports 8-bit** channels (discussion #1614, issue #605; a 16-bit PR exists but is unmerged). Adequate for 8-bit display-referred output only.
  - **lodepng** (zlib licence): supports **16-bit-per-channel PNG** encode/decode; stores big-endian samples as PNG requires, so the caller must byte-swap. Right choice for 16-bit display-referred PNG.
- **Relevance:** Two output paths: (a) *display-referred* 8/16-bit PNG (after tone mapping) for quick review; (b) *scene-referred* half/float EXR (before tone mapping, optionally with AOVs: depth, velocity, emissive, particles-only) for compositing/grading in Resolve/Nuke. The render graph's final sink selects which intermediate is read back, so the tone-mapping pass must be optional/bypassable.
- **Confidence / limitations:** High (library docs/issues read).

---

## 8. Video encoding options and licences

### 8.1 FFmpeg / libav*

- **Source:** FFmpeg legal page (read in full); FFmpeg LICENSE.md; x264 licensing page.
- **URL:** https://www.ffmpeg.org/legal.html ; https://github.com/FFmpeg/FFmpeg/blob/master/LICENSE.md ; https://x264.org/licensing/
- **Accessed:** 2026-09-08.
- **What was learned:**
  - FFmpeg is **LGPL-2.1-or-later by default**; passing `--enable-gpl` (required for libx264, libx265, libxvid, frei0r, etc.) makes the whole build **GPL-2.0-or-later**; `--enable-nonfree` makes it undistributable.
  - LGPL compliance checklist for a proprietary app: build without `--enable-gpl`/`--enable-nonfree`; **link dynamically**; ship the exact FFmpeg source matching the binaries (or an offer); attribution in the about box/EULA; do not rename the libraries to hide them; publish any modifications (git diff).
  - Patent note: FFmpeg's authors explicitly say they are "not lawyers" and cannot say which codecs are patent-encumbered; H.264/HEVC patent pools are relevant for commercial products regardless of licence.
  - **Spawning the `ffmpeg` binary as a separate process** (piping raw frames to stdin, e.g. `-f rawvideo -pix_fmt rgba`) keeps the app and FFmpeg as separate programs; the legal page does not endorse or forbid this, and secondary sources describe it as widely practised but a grey area if the product *depends* on the GPL binary. Practical stance: ship no ffmpeg binary; let users point at their own install; or use an LGPL build with only LGPL/BSD encoders (e.g. `prores_ks`, `libvpx` is BSD, `libaom`/`libsvtav1` are BSD/BSD+patent grant).
- **Relevance:** Strongly prefer (1) image sequences + external tool, and (2) native VideoToolbox (§8.2) for in-app video. If libav* is linked later, LGPL-only + dynamic linking.
- **Confidence / limitations:** High (primary source). Not legal advice.

### 8.2 AVFoundation / VideoToolbox (native macOS)

- **Source:** Apple WWDC20 "Decode ProRes with AVFoundation and VideoToolbox"; `AVVideoCodecType.proRes4444` docs; DSR "prores-encoder-mac" sample; ffmpeg-cookbook VideoToolbox article.
- **URL:** https://developer.apple.com/videos/play/wwdc2020/10090/ ; https://developer.apple.com/documentation/avfoundation/avvideocodectype/prores4444 ; https://github.com/DSRCorporation/prores-encoder-mac ; https://ffmpeg-cookbook.com/en/articles/hardware-encode-videotoolbox/
- **Accessed:** 2026-09-08.
- **What was learned:** `AVAssetWriter` + `AVAssetWriterInputPixelBufferAdaptor` (or `VTCompressionSession` directly) encode H.264/HEVC and **Apple ProRes 422/4444** with no third-party licence, using the Apple-silicon media engine (hardware ProRes encode/decode on M-series). For ProRes 4444 the native pixel format is 16-bit 4:4:4:4 (`Y416`/`r4fl`-style buffers); CVPixelBuffers can be created from an `IOSurface` shared with a Metal texture (`CVMetalTextureCache`) to avoid CPU copies. ProRes is the delivery format festival/VJ pipelines expect (Resolume, Notch, Disguise ingest ProRes/HAP/NotchLC).
- **Relevance:** Native, licence-clean, hardware-accelerated; the correct default in-app encoder on macOS. Alpha output requires ProRes 4444 (or PNG sequence).
- **Confidence / limitations:** Medium-high; the IOSurface zero-copy path is standard but was not verified in a fetched Apple doc during this research.

### 8.3 Codecs used by the live-visual ecosystem

Notch exports H.264, HAP, HAP Q and NotchLC (https://manual.notch.one/0.9.23/en/docs/user-interface/exporting-video/ — accessed 2026-09-08); TouchDesigner's Movie File Out TOP writes QuickTime/MP4 with Animation, CineForm and HAP Q among others (https://docs.derivative.ca/Movie_File_Out_TOP — accessed 2026-09-08). **HAP** is a GPU-decodable DXT-based codec (BSD-licensed reference, Vidvox) and is the interchange format for media servers; worth supporting for output eventually.

---

## 9. How other tools render offline (and what they do about temporal effects)

### 9.1 Unreal Engine Movie Render Queue / Movie Render Graph

- **Source:** Epic documentation "Rendering High Quality Frames with Movie Render Queue" (read in full); "Cinematic Rendering Image Quality Settings"; community tutorial on warm-up; forum threads.
- **URL:** https://dev.epicgames.com/documentation/en-us/unreal-engine/rendering-high-quality-frames-with-movie-render-queue-in-unreal-engine ; https://dev.epicgames.com/documentation/unreal-engine/cinematic-rendering-image-quality-settings-in-unreal-engine ; https://dev.epicgames.com/community/learning/tutorials/l4OR/unreal-engine-movie-render-queue-warmup-and-first-frame-issues ; https://forums.unrealengine.com/t/is-there-a-way-to-not-render-warm-up-frames-in-movie-render-graph/2629276
- **Accessed:** 2026-09-08.
- **What was learned:**
  - **Temporal samples** slice the shutter-open interval into sub-frames; the engine *ticks forward* for each and accumulates — this is how MRQ gets high-quality motion blur (docs example: 1 spatial x 64 temporal). **Spatial samples** re-render the same instant with sub-pixel camera offsets (no tick) for anti-aliasing. Guidance: temporal for motion blur, spatial for AA; with high temporal counts, **override anti-aliasing to None** (disable TAA/TSR) because accumulation supersedes it.
  - **Warm-up:** "Render Warm Up Count" and "Engine Warm Up Count" (docs example 120 each; MRQ default 32) run frames before the first captured frame "when building the temporal history and simulations for them to settle" (auto-exposure, particles, TAA). Movie Render Graph exposes a single "Num Warm Up Frames".
  - Output: PNG 8-bit (with alpha), EXR 16-bit; tiled high-resolution rendering to exceed GPU texture limits.
  - The docs make **no determinism guarantee**.
- **Relevance:** Adopt all four ideas: sub-frame temporal accumulation for motion blur; spatial jitter accumulation for AA (which also replaces TAA offline and removes its history dependence); warm-up frames; tiled rendering for >8K output. The frame loop must therefore support "render this frame `S x T` times with (jitter, subTime) and accumulate in a float buffer" — the accumulate/resolve pass is a render-graph node.
- **Confidence / limitations:** High (official docs).

### 9.2 Blender (EEVEE / Cycles)

- **Source:** Blender manual, EEVEE "Motion Blur"; Cycles "Sampling" (seed, Animated Seed); bug #93534.
- **URL:** https://docs.blender.org/manual/en/latest/render/eevee/render_settings/motion_blur.html ; https://docs.blender.org/manual/en/latest/render/cycles/render_settings/sampling.html ; https://developer.blender.org/T93534
- **Accessed:** 2026-09-08.
- **What was learned:** EEVEE's motion blur is **accumulation motion blur**: the frame is split into `Steps` time steps and accumulated, with `Shutter` in frames (1.0 = 360°), optionally plus a post-process blur (0 = accumulation only). Cycles exposes an explicit integer **Seed** and an **Animated Seed** option that varies the seed per frame so noise is not static across the animation; a persistent-data bug once broke per-frame seeding, showing that per-frame seeding is expected behaviour.
- **Relevance:** Confirms the sub-frame accumulation approach and the "seed = f(baseSeed, frameIndex)" convention; av-gen should expose both a global seed and per-frame derivation.
- **Confidence / limitations:** High.

### 9.3 Notch

- **Source:** Notch manual 0.9.23 "Export Video" (read); Notch manual motion blur node.
- **URL:** https://manual.notch.one/0.9.23/en/docs/user-interface/exporting-video/ ; https://manual.notch.one/0.9.23/en/docs/nodes/post-fx/blur/motion-blur/
- **Accessed:** 2026-09-08.
- **What was learned:** Export offers a frame range, an optional **preroll** that pre-renders frames before the start "to help certain effects build up" (Notch's warm-up), **Motion Blur Frames** (number of overlapping sub-frames) and **Blur Amount** (shutter scale), **Antialiasing Passes** (re-render with sub-pixel offsets) and separate refinement passes for ray-traced content; batch/command-line transcoding without the UI. The motion-blur node documentation recommends 16-32 samples for fast motion.
- **Relevance:** Notch — the closest commercial analogue to av-gen — uses exactly the MRQ recipe: preroll + temporal sub-frames + spatial AA passes. The same live scene renders offline.
- **Confidence / limitations:** High (manual read).

### 9.4 TouchDesigner

See §1: realtime flag off + Movie File Out TOP; no built-in sub-frame accumulation (users stack frames manually).

---

## 10. Visual regression testing

### 10.1 Metrics: FLIP, SSIM, perceptual hashes

- **Source:** Andersson, P., Nilsson, J., Akenine-Möller, T., Oskarsson, M., Åström, K., Fairchild, M. "FLIP: A Difference Evaluator for Alternating Images", HPG 2020 / PACMCGIT 3(2); NVIDIA C++ source; Wang et al. 2004 SSIM; pHash.
- **URL:** https://dl.acm.org/doi/10.1145/3406183 ; https://developer.nvidia.com/blog/wp-content/uploads/2020/07/flip-author-version-reduced-file-size.pdf ; https://github.com/rotoglup/nvidia-flip-cpp ; https://ece.uwaterloo.ca/~z70wang/research/ssim/ ; https://phash.org/docs/design.html
- **Accessed:** 2026-09-08.
- **What was learned:**
  - **FLIP** produces a per-pixel error map approximating what a human notices when *alternating* (flipping) between two images, designed specifically for rendered image vs reference comparison; it accounts for viewing distance via pixels-per-degree; there are LDR and HDR variants; reference implementations exist in C++, MATLAB, NumPy and PyTorch (NVIDIA, BSD-3). The pooled value (mean or weighted) gives a scalar for thresholds.
  - **SSIM** (Wang, Bovik, Sheikh, Simoncelli 2004) compares luminance, contrast and structure in local windows; range [-1, 1]; ubiquitous but not tuned for rendering artifacts (fireflies, small shifts).
  - **pHash** (DCT of a 32x32 grayscale, top-left 8x8 coefficients binarised against the median → 64-bit hash, compared by Hamming distance) is robust to mild compression/resizing and sensitive to flips/crops/colour shifts — good for cheap "did the frame change drastically" checks, not for fine regressions.
- **Relevance:** Test tiers: (1) **bit-exact hash** (xxHash/SHA of the readback) on the *same* GPU + driver for deterministic-mode frames; (2) **FLIP mean below a threshold** (calibrated per test; start ~0.05-0.1 and tighten) for cross-GPU/cross-OS runs and for effects using float atomics; (3) pHash Hamming distance as a smoke check. Store FLIP error maps as CI artifacts.
- **Confidence / limitations:** High on metric facts; thresholds are engineering judgment to be calibrated.

### 10.2 Golden images and tolerance in practice

- **Source:** Chromium "GPU Pixel Testing With Gold"; Flutter golden tests with tolerance (Medium; tomasrepcik.dev).
- **URL:** https://chromium.googlesource.com/chromium/src.git/+/master/docs/gpu/gpu_pixel_testing_with_gold.md ; https://medium.com/mobilepeople/how-to-add-difference-tolerance-to-golden-tests-on-flutter-2d899c8baad2 ; https://tomasrepcik.dev/blog/2024/2024-09-19-flutter-golden-test-with-tolerance/
- **Accessed:** 2026-09-08.
- **What was learned:** Chromium's GPU pixel tests use Skia Gold with **fuzzy matching** and per-GPU/OS golden sets because "visually indistinguishable" images routinely differ by a few pixels/small RGB deltas across hardware; Flutter's Linux vs macOS golden drift is the same story, solved with a tolerance comparator. Chromium keys goldens by (OS, GPU vendor/model, driver) and uses inexact-matching parameters (max different pixels, max per-channel delta).
- **Relevance:** Keep goldens per (GPU family, OS major); make the comparator pluggable (exact / per-channel delta / FLIP); reduce test resolution (e.g. 320x180) to keep goldens small and fast; render tests headless through the same offline path as production.
- **Confidence / limitations:** High.

---

## 11. Frame pacing independence and batch/render queues

- **What was learned (synthesis of §1, §6, §9):** In offline mode there is no vsync and no `CAMetalLayer`; throughput is limited by GPU time and encode time, overlapped via the 3-deep readback ring. Render jobs should be described declaratively (project, scene, seed, fps, frame range, resolution, sub-frame counts, warm-up count, output format/path, deterministic flag) so they can be queued, resumed (skip existing frames), sharded by frame range across machines *only when the scene is stateless or state can be checkpointed*, and reproduced from the job file. Notch's Render Queue / Render Node and MRQ's queue are the reference UX. A job file is also the natural fixture for regression tests.
- **Confidence / limitations:** Design synthesis.

---

## 12. Architectural constraints for milestone 0.1

Cheap now, expensive later — these must be in 0.1 even though offline rendering itself is not:

1. **A single `FrameClock` object** with `frameIndex`, `time = frameIndex / fps` (offline) or wall-derived (live), and `dt`; injected into every system. **No subsystem calls a system clock.** Grep-able rule: `std::chrono`, `CACurrentMediaTime`, `mach_absolute_time` appear only inside the clock implementation and the profiler.
2. **Fixed-step simulation loop** with an accumulator in live mode and an exact sub-step count in offline mode; simulation passes take `dt` as a uniform.
3. **Injected, seedable RNG** (PCG32 on CPU; integer hash RNG in the shared MSL library on GPU) seeded from `(globalSeed, frameIndex, systemId)`; `std::random_device`, `rand()`, and `<random>` distributions are banned in engine code.
4. **Audio features behind an interface** (`AudioFeatures::at(time)`) with both a live analyzer and a precomputed analysis-track provider; the render side never reads raw audio.
5. **Offscreen-first rendering:** the frame renders into engine-owned textures with `framebufferOnly = false`; presenting to a `CAMetalLayer` drawable is a final blit/sink pass. Resolution and output format are per-sink properties.
6. **Readback API:** "blit texture → shared `MTLBuffer` + completion callback" with an N-deep (3) ring and frame-in-flight limiting; the encoder runs on worker threads. This is also how regression tests capture frames.
7. **Bypassable tone mapping / display transform** so scene-linear half/float frames can be exported (EXR via tinyexr) alongside display-referred PNG (lodepng for 16-bit; stb_image_write only for 8-bit).
8. **Determinism mode flag** plumbed through the renderer: selects prefix-sum compaction over atomic append, ordered reductions over float atomics, fixed threadgroup sizes, fast-math off at pipeline creation, MetalFX off, and stable per-particle IDs. 0.1 needs the flag and the pipeline-creation options, not every alternative kernel.
9. **Explicit history/state reset** (`resetTemporalState()`) on every stateful pass (TAA, temporal fog, feedback textures, particles, trails) so a job can start from a defined state and run warm-up frames.
10. **Sub-frame accumulation support in the frame loop:** render `S x T` samples per output frame with per-sample (jitter, subTime), accumulate into a float target, resolve — the accumulate/resolve are graph passes; projection jitter is a camera parameter.
11. **Declarative render job description** (fps, frame range, resolution, seed, sub-samples, warm-up, output) as a serialisable struct from day one; it doubles as the regression-test fixture format.
12. **Regression test harness plan:** headless render of tiny scenes via the same path, comparators (exact hash / per-channel tolerance / FLIP), goldens keyed by (GPU family, OS major), error maps saved as artifacts.
13. **Licensing posture:** no GPL linking; image sequences + native AVFoundation/VideoToolbox (ProRes) as the in-app video path; optional user-supplied ffmpeg as an external process; if libav* is ever linked, LGPL-only and dynamic.

---

## Sources

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
