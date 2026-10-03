# AV Gen: live projection performance and adaptive quality

*Measured 2026-10-02 on an Apple M2 Max with a 120 Hz LG UltraFine 5K, release build of main `8ae10a64` plus temporary
instrumentation, through the **real projection output** (a 1920×1080-pixel output window).*

- **No optimisation was done.** The probe code is on the local branch `investigate/live-render-perf`, commits `8f953f53` and `319946ad`.
- **Each run:** 720 live frames with the first 120 discarded, all under the GPU lock. Raw data is in `data/`; `data/tables.txt` has every run.
- **Run-to-run noise:** repeating the 1080p arm moved Glowmere from 22.3 to 25.5 fps, Liminal from 12.6 to 13.9 and Sonic from 55.7 to 54.9. Differences smaller than about 10% are noise.

## The answer, first

**Plug into a 1080p projector, run a heavy scene, and you get 13-25 fps at native resolution.** With the existing adaptive scale on, every heavy scene sits at its floor, rendering a quarter of the pixels, and lands at **about 56-60 fps**. That's the ceiling resolution alone can buy. Details in §8.

| scene | native 1080p | adaptive on (as shipped) | resolution needed for ~60 fps | ~90 fps | ~120 fps |
|---|---|---|---|---|---|
| Sonic Garden live | **55 fps** | 55 fps at 0.85 scale (1632×918) | 0.71-0.85 | 0.5 (measured 92-96 fps) | not by resolution: about 9 ms of the GPU frame is fixed |
| Glowmere Valley 2 | **22-25 fps** | 56 fps at the 0.5 floor (960×540) | 0.5 (measured 56-59 fps) | not by resolution: the GPU is still 16 ms at 0.5, and the CPU is 7.4 ms fixed | not reachable: the CPU alone is 7.7-8.7 ms |
| Liminal *All You Got* | **13-14 fps** | 60 fps at the 0.5 floor | 0.5 (measured 59-60 fps) | about 0.42 (modelled, below the current floor) | about 0.35 (modelled) |

## A. Current architecture: where resolution is decided

```
projection window: 960×540 points × backing scale 2 = 1920×1080 px      (OutputManager, Projection)
   │  while projecting, the canvas extent IS the projector's size (8ae10a64):
   │     cw,ch = output pixels × canvasRenderScale (Settings, default 1.0)            application.cpp ~4566
   ▼
renderer_->resize(cw, ch)   → outputWidth_ = 1920×1080
   │  internal size = output × QualitySettings::renderScale, which the ADAPTIVE CONTROLLER sets   scene_renderer.cpp resize()
   ▼
HDR + aux targets at the internal size (e.g. 960×540)
   ├─ shadows, prepass, AO, shadow mask          internal size (shadow maps fixed)
   ├─ scene pass, SDF ray march                  internal size
   ├─ volumetric march                           internal × volumeResolutionScale (0.5 at the realtime tier)
   ├─ post: bloom, DoF, motion blur, exposure, FXAA    internal size (PostProcessor gets hdr_.width/height)
   ▼
tonemap pass  → reads the internal HDR target, writes finalTexture at OUTPUT size (1920×1080). This is the upscale.  ~0.1 ms
   ▼
ImGui workspace (the canvas shows finalTexture) → main window present (Fifo, 2880×1800)
   ▼
OutputManager::presentAll: CreateView, a SECOND swapchain acquire on the projection window (Fifo),
   mapper draw (Fit/Fill/Stretch) at 1920×1080, a separate Submit, present
```

**Three answers from the code:**
- **Yes, "adaptive scale" controls the expensive work.** It's applied as `QualitySettings::renderScale` at `SceneRenderer::resize`, so the SDF march, the scene pass, the volumes and every post pass run at the reduced size. Only the tonemap and the projector copy run at output size.
- **`canvasRenderScale`, the manual slider, is a different lever.** It shrinks the final texture itself, so the projector's mapper does the upscale.
- **The live tier is fixed at `realtime`.** Volumes run at half the internal resolution with full steps. `QualityTier::Preview` exists, with quarter-resolution volumes, half steps, a 1024 shadow map and 2 cascades, but nothing switches to it at runtime.

## B. Measured results (projection output at the size shown)

**Columns:**
- **Interval:** the median frame interval.
- **1% low:** the mean of the slowest 1% of frames.
- **CPU:** the main thread's work, excluding the swapchain waits.
- **GPU:** the renderer's timestamp span. The UI and the projector copy are not included.

**Sonic Garden live** (`sonic-live.json`; 30 draws, 258k triangles, 8 lights)

| output | internal render | fps | interval | 1% low | CPU ms | GPU ms | largest passes (ms) |
|---|---|---|---|---|---|---|---|
| 1920×1080 | 1920×1080 | 55.7 / 54.9 | 16.7 / 17.8 | 34 / 22 | 1.9 | 16.8 / 17.0 | scene 12.3, shadow 1.4, shadow mask 0.9, DoF 0.9 |
| 1600×900 | 1600×900 | 65.8 | 8.8 | 26 | 2.0 | 14.4 | scene 10.6, shadow 1.4 |
| 1280×720 | 1280×720 | 60.0 | 16.7 | 17 | 1.9 | 12.4 | scene 8.7, shadow 1.7 |
| 960×540 | 960×540 | 60.0* | 16.7 | 17 | 1.8 (+14.8 output wait*) | 13.2* | scene 9.0, shadow 2.4 |
| 1920×1080, adaptive (16.67 ms) | 1632×918 (rung 0.85) | 54.5 | 16.7 | **195** (the rung change) | 2.0 | 15.1 | scene 11.3 |
| 1920×1080, adaptive (8.33 ms) | 960×540 | 92.3 | 8.6 | 25 | 2.9 | 9.4 | scene 6.4, shadow 1.4 |
| 1920×1080, fixed 0.71 | 1362×766 | 77.9 | 8.6 | 25 | 2.0 | 11.9 | scene 8.3 |
| 1920×1080, fixed 0.5 | 960×540 | 96.0 | 8.5 | 25 | 2.8 | 9.2 | scene 6.4 |

\* **The 960×540 window run is not trustworthy for GPU or frame rate.** The projection window's swapchain acquire blocked for 14.8 ms per frame. The most likely cause is macOS throttling a small window covered by the editor. The fixed-0.5 run at 1080p output measures the same internal size cleanly. See §F.

**Glowmere Valley 2** (180 draws, 256 entities, 64 procedurals, 76 lights)

| output | internal render | fps | interval | 1% low | CPU ms | GPU ms | largest passes (ms) |
|---|---|---|---|---|---|---|---|
| 1920×1080 | 1920×1080 | 22.3 / 25.5 | 43.9 / 37.9 | 70 / 56 | 8.7 / 11.5 | 43.2 / 37.0 | scene 25.5, volume 8.6, motion blur 4.4, DoF 1.4 |
| 1600×900 | 1600×900 | 34.9 | 27.9 | 37 | 8.2 | 27.0 | scene 15.1, volume 5.5, motion blur 2.9 |
| 1280×720 | 1280×720 | 43.5 | 21.8 | 34 | 8.1 | 21.0 | scene 12.3, volume 4.4, motion blur 1.7 |
| 960×540 | 960×540 | 59.4 | 17.0 | 26 | 7.8 | 15.6 | scene 9.8, volume 2.9, motion blur 1.0 |
| 1920×1080, adaptive (16.67) | 960×540 (floor) | 56.2 | 17.4 | 27 | 7.7 | 16.3 | scene 10.0, volume 3.1 |
| 1920×1080, adaptive (8.33) | 960×540 (floor) | 56.6 | 17.9 | 31 | 10.1 | 16.2 | same |
| 1920×1080, fixed 0.71 | 1362×766 | 42.0 | 22.7 | 34 | 8.2 | 21.8 | scene 12.6, volume 4.3, motion blur 2.1 |
| 1920×1080, fixed 0.5 | 960×540 | 57.1 | 17.4 | 27 | 8.0 | 16.1 | scene 10.1, volume 3.0 |

**Liminal *All You Got*** (`all-you-got.json` from 60 s; 7 SDF objects, a volumetric medium)

| output | internal render | fps | interval | 1% low | CPU ms | GPU ms | largest passes (ms) |
|---|---|---|---|---|---|---|---|
| 1920×1080 | 1920×1080 | 12.6 / 13.9 | 56.9 / 56.7 | 885 / 365 (start-up) | 2.9 / 2.3 | 56.7 / 56.5 | SDF 36.2, volume 13.8, motion blur 5.1 |
| 1600×900 | 1600×900 | 27.4 | 36.2 | 47 | 2.5 | 35.6 | SDF 21.7, volume 9.4, motion blur 3.5 |
| 1280×720 | 1280×720 | 39.3 | 25.2 | 43 | 2.7 | 24.5 | SDF 14.3, volume 7.3, motion blur 2.0 |
| 960×540 | 960×540 | 59.4 | 16.8 | 31 | 2.3 | 15.9 | SDF 8.8, volume 5.3, motion blur 1.1 |
| 1920×1080, adaptive (16.67) | 960×540 (floor) | 59.7 | 16.9 | 25 | 2.2 | 15.9 | SDF 8.8, volume 5.3 |
| 1920×1080, adaptive (8.33) | 960×540 (floor) | 59.3 | 16.9 | 25 | 2.2 | 16.0 | same |
| 1920×1080, fixed 0.71 | 1362×766 | 34.9 | 28.4 | 37 | 2.6 | 27.7 | SDF 16.4, volume 7.7 |
| 1920×1080, fixed 0.5 | 960×540 | 59.0 | 16.9 | 25 | 2.2 | 16.1 | SDF 9.0, volume 5.3 |

**What these numbers don't measure:**
- **GPU memory:** not readily available through the current instrumentation.
- **`engine.update`, `applyParameters`, `render.record` and `Submit`:** in `data/tables.txt`. Every one of them stays flat across resolutions; see §E.

## C. Adaptive scaling analysis

**What controls it:** `InteractiveResolution::note(gpuMs, wallMs)` in `src/app/interactive_resolution.{hpp,cpp}`.
- **Inputs:** the median of the last 20 GPU samples (the renderer's timestamp span) against `adaptiveCanvasBudgetMs`. That budget defaults to **16.67 ms (60 fps)** whatever the display's refresh rate.
- **When it acts:** only when the GPU is at least 70% of the wall clock; otherwise it treats the frame as CPU-bound and holds.
- **Timing:** it waits at least 30 frames between decisions.
- **Dropping:** up to two rungs per decision.
- **Raising:** one rung, and only if the predicted cost fits within 80% of the budget.
- **Rungs:** 1.0, 0.85, 0.71, 0.58 and 0.5. **The floor is 0.5** (`adaptiveCanvasFloor`); the command-line minimum is also 0.5.

**What it controls:** `QualitySettings::renderScale`, which really is the expensive work: the internal HDR target, the SDF march, the scene, the volumes and all of post. **Verified:** the internal target follows the rung, and the passes shrink accordingly.

**Does it work for projection? Yes.** While projecting, the canvas extent is the projector's size, and the controller scales that. Measured: a 1920×1080 output rendered internally at 960×540.

**Does it protect frame rate? Only up to 60 fps, and only down to its floor.**
1. **The 16.67 ms budget doesn't match a 120 Hz display.** Sonic's GPU at 0.85 scale is 15.1 ms, under budget, so the controller stops. Fifo at 120 Hz then makes any frame over 16.67 ms take 25 ms, so the result is 54.5 fps with judder, not a clean 60. The same scene at a 0.71 scale gives 78 fps, and at 0.5 gives 96.
2. **The heavy scenes hit the floor and stay there.** Glowmere and Liminal both sit at 0.5 with their GPU still at 16 ms, right on the budget. **No second lever engages:** no tier change, no volume reduction, no effect reduction. The 0.5 floor exists because the controller's own measurement says lower scales stop paying off. For Liminal (97% per-pixel) that's untrue; for Glowmere it's true.
3. **It can't help the CPU,** by design. Glowmere's CPU of 7.7-8.7 ms is unaffected by resolution, so 120 fps can't be reached at any scale.
4. **Every rung change reallocates the render targets** and resets screen history. Sonic's 1% low of **195 ms** in the adaptive run is the switch from 1.0 to 0.85.
5. **Dynamic behaviour** (Liminal from 40 s, 2,400 frames):
   - It went 1.0 → 0.71 → 0.5 within 2.3 s of the first frame, then held 0.5 for the whole run with the interval steady at 16.6-17.8 ms.
   - **No oscillation.**
   - **Not tested: the climb back up.** The content never got light enough for it to try. The raise rule (fit within 80% of the budget, one rung at a time) looks stable on paper, but this run doesn't show it working.
6. **The timestamp span it reads can overstate per-frame GPU cost when frames overlap on the GPU.** In this sweep the span matched the frame interval closely in GPU-bound runs. In the earlier editor runs it sometimes exceeded the interval. Minor.

## D. GPU scaling (fits of ms = fixed + per-megapixel × Mpx over the six internal sizes, 0.52-2.07 Mpx)

| scene | pass | fixed (ms) | per Mpx (ms) | at 1080p | resolution-dependent share |
|---|---|---|---|---|---|
| Liminal | whole GPU | 1.7 | 25.5 | 54.6 | **97%** |
| | SDF march | ~0 | 17.1 | 34.6 | ~100% |
| | volume march | 2.4 | 5.3 | 13.4 | 82% |
| | motion blur | ~0 | 2.6 | 5.1 | ~100% |
| Glowmere | whole GPU | 5.8 | 16.9 | 40.8 | **86%** |
| | scene pass | 4.0 | 9.4 | 23.6 | 83% |
| | volume march | 1.1 | 3.4 | 8.2 | 87% |
| | motion blur | ~0 | 2.2 | 4.3 | ~100% |
| | shadows, particles | 0.9 | ~0 | 1.0 | fixed |
| Sonic | whole GPU | ~9 | ~3.6 | ~16.6 | **~45%** (a poorer fit: max residual 2.3 ms, since the 540 window run is suspect) |
| | scene pass | 5.9 | 3.0 | 12.2 | 51% |
| | shadows | ~1.5 | ~0 | 1.2-2.4 | fixed |

**So resolution is an excellent lever for Liminal, a good one for Glowmere's GPU, and a weak one for Sonic,** whose cost is half vertex and shadow work.

## E. CPU scaling (fixed cost that stays when resolution drops)

| scene | main-thread CPU work at 1080p → 540p | of which `engine.update` | `applyParameters` | `render.record` | Submit (Dawn) |
|---|---|---|---|---|---|
| Sonic | 1.9 → 1.9 ms | 0.5 | 0.45 | 0.45 | 0.6 |
| Glowmere | 8.7 → 7.8 ms | 3.2-3.7 | 0.6 | 2.7-3.1 | 1.2-1.4 |
| Liminal | 2.9 → 2.3 ms | 0.7-0.9 | 0.45-0.57 | 0.7-0.9 | 0.4-0.5 |

- **CPU is essentially flat across resolutions:** 74-91% of it is fixed.
- **Glowmere's 7.4 ms fixed** caps it at about 135 fps CPU-side. Combined with the 16 ms GPU floor at 0.5, 90+ fps isn't available to resolution or tiers alone.
- **The cause:** the previous report traced about 3 ms of it to CPU-skinned bounds and the always-on diagnostic record. Fixing those is the CPU half of reaching 90 fps in Glowmere.

## F. The projection path, measured

- **Upscale:** the tonemap pass reads the internal target and writes the output-size final texture. **About 0.1 ms** at 1080p from 960×540, so it's not a cost.
- **Projector copy:** the projection window gets its own CreateView, swapchain acquire, mapper draw, Submit and present every frame. **CPU about 0.06-0.09 ms** in every normal run.
  - The mapper's GPU time isn't timestamped, but it's one full-screen textured draw.
- **Hazard: the copy is not isolated from the main frame.** In the 960×540-window run, the projection surface's **acquire blocked for 14.8 ms per frame** and capped the whole app at 60 fps. The second Fifo acquire sits on the main thread with no timeout.
  - The likely trigger is macOS throttling a small, covered window. That's an artifact of testing on one display, but a real projector that is slow, mis-clocked or hidden would stall the editor and the performance the same way.
- **The projection button only projects Sonic projects.** `Application::startProjection` swaps in the Sonic Live demo for any project without `sonic.live`. Projecting Glowmere or Liminal for these measurements needed a probe (`AVGEN_X_PROJECT_ANY`). Today's real route for a non-Sonic project is a project output from the Outputs panel.

## G. Recommended live quality architecture (the smallest coherent system)

**One controller, one budget, an ordered ladder of levers.** It extends `InteractiveResolution`; it doesn't replace it.

1. **A budget from the target, not a constant.**
   - **The target:** the performer picks it (60, 90 or 120 fps; default 60). The budget is the display's vsync multiple under that target, minus about 12% headroom: on 120 Hz, a 60 fps target gives a budget of about 14.5 ms, not 16.67.
   - **Why:** this alone turns Sonic's 55 fps with judder into a clean 60+ (0.71 was measured at 78 fps).
2. **Ladder rungs, each a bundle of existing `QualitySettings` fields, in this order:**
   1. **ULTRA:** 1.0 scale, the realtime tier.
   2. **HIGH:** 0.85 scale.
   3. **MEDIUM:** 0.71 scale plus volumes at quarter resolution (`volumeResolutionScale` 0.25; the `volumequarter` arm already exists).
   4. **LOW:** 0.5 scale plus the preview tier's volumes (0.25 resolution, 0.5 steps), motion blur off, DoF off and 2 shadow cascades.
   5. **EMERGENCY:** a 0.35-0.4 scale, volumes off and post reduced to bloom and tonemap.
   
   **Estimated effect** from the measured pass costs at 0.5, adding up the passes each rung removes (not measured in combination):
   - **Glowmere LOW:** about 16 → 12 ms, roughly 70-80 fps. The CPU (7.7 ms) is the next wall.
   - **Liminal LOW:** about 16 → 11-12 ms.
   - **Liminal EMERGENCY at 0.4:** about 8-9 ms.
3. **Per-scene lever order: a project-level hint, data only, saying which lever goes first.**
   - **Liminal:** resolution first (97% pixel-bound), then the volume march.
   - **Glowmere:** volume and motion blur first (they look smaller than the scene loss), then resolution.
   - **Sonic:** resolution helps only to about 0.71, then shadows and DoF.
   
   **This is a direct consequence of §D:** a single global resolution scale works for Liminal, is nearly enough for Glowmere's GPU, and is the wrong lever for Sonic's fixed cost.
4. **Make rung changes cheap and visible.**
   - **Cheap:** allocate the targets at full size once and render into a sub-rectangle, so a rung change doesn't reallocate (no 195 ms hitch).
   - **Visible:** show the current rung, the GPU ms and the budget in the Live panel, under names a performer understands ("Live quality: High (0.85) — GPU 15 ms / 14.5 ms").
5. **Keep the projector copy from blocking the frame:** acquire with a timeout, or skip a frame.

## H. Concrete next steps

**Tier 1: very high confidence, low complexity.**
- **Derive the adaptive budget from the display refresh and a chosen target with headroom,** and expose the target in Live settings. Measured: Sonic goes from 55 to 78-96 fps at the scales the controller would then pick.
- **Show the controller's rung and the GPU ms against the budget in the Live panel.**
- **Let Start projection project the open project,** not only Sonic Live (or say clearly in the UI that it switches projects).
- **Guard the projection surface's acquire** with a timeout or a frame skip.
- **The two CPU fixes from the previous report:** gate the diagnostic record on a consumer, and cache posed bounds by `paletteVersion`. Predicted to save about 3 ms of Glowmere's 7.7 ms fixed CPU.

**Tier 2: high confidence, moderate complexity.**
- **Extend the controller from one dimension (scale) to the ladder above,** reusing existing `QualitySettings` fields: volume resolution and steps, motion blur, DoF, cascades.
- **Lower the floor to about 0.35 for pixel-bound scenes,** where the measured slope still pays (Liminal).
- **Per-project lever-order hints.**
- **Target reuse:** render into a sub-rectangle so rung changes don't reallocate and reset history.

**Tier 3: architectural.**
- **Temporal upscaling** (TAA-style reconstruction to output size), so the 0.5-0.7 rungs keep image quality. This is what makes resolution a lever performers won't notice.
- **A live quality control on the SDF march itself:** a step budget, or a half-resolution march with depth-aware upsampling. Today internal resolution is its only lever.
- **The incremental-update work and draw batching from the previous report,** for the CPU side of 90-120 fps in character-heavy scenes.

## The most important question

> *If I plug AV Gen into a 1920×1080 projector and run a representative heavy scene in LIVE mode, what is the current path, what frame rate should I expect, and what mechanism should control quality when the GPU exceeds the frame budget?*

**The path.**
- The projection window's size, 1920×1080 pixels, becomes the canvas extent.
- The adaptive controller scales that into the internal HDR target (rungs 1.0 down to 0.5), and the scene, SDF march, volumes (at half that again) and all of post run there.
- The tonemap pass upsamples into a 1920×1080 final texture (about 0.1 ms).
- The editor shows it, then the output manager copies it into the projector's own swapchain and presents.
- Two Fifo swapchains per frame (editor and projector), all on the main thread.

**What frame rate to expect on this M2 Max today.**
- **Native resolution:** Glowmere 22-25 fps, Liminal 13-14 fps, Sonic 55 fps.
- **With adaptive scaling on (the default):** Glowmere about 56 fps and Liminal about 60 fps, both pinned at the 0.5 floor (960×540 internal). Sonic is about 55 fps with judder, because the 60 fps budget doesn't match the 120 Hz display.
- **What isn't on offer:** expect a steady 50-60 fps on heavy scenes at a quarter of the pixels, upscaled without temporal reconstruction. Nothing heavy reaches 90.

**What should control quality.** The existing GPU-time controller, extended in four ways:
1. **A budget** derived from the display refresh and a performer-chosen target with headroom.
2. **An ordered ladder** that, after resolution, steps the effects measured to cost the most: volume resolution and steps, motion blur, DoF, shadows, plus a lower resolution floor for pixel-bound SDF scenes.
3. **Per-project hints** for which lever goes first.
4. **Rung changes that don't reallocate.**

**Two limits it can't fix:**
- **CPU-bound cost:** Glowmere's about 8 ms fixed needs the CPU fixes.
- **Image quality at low scales:** needs temporal upscaling.
