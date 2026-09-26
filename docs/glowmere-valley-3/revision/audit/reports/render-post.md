# GV3 audit: offline render quality, draw distance and post stack

Read-only audit. I changed nothing in either checkout. My scratch outputs (help text, decoded stills, a horizon-probe JSON and a Python copy of `tonemap.wgsl`) are in `/private/tmp/claude-501/-Users-natefaulkenberry-Documents-GitHub-av-gen/004befa7-093f-41d0-b2da-3d5e53a50a3a/scratchpad/audit/offline-post/`. I rendered nothing. I did two CPU-only things: decoded frames from the final `.mov` with ffmpeg, and probed terrain heights with `avgen_world_preview`. The final render's own log is at `…/scratchpad/render-final.log`.

## Summary
- **The renderer's distance culls are already fully lifted in the final.** Log line 340 reads "procedural distance cull off, LOD rungs off, rig pose rate off, entity behaviour bands off", and line 2312 reads "coarse 0, skipped 0". Raising `cullDistance` or `viewDistance` values would change nothing.
- **What looks like limited draw distance comes from three things:**
  - Distances the engine treats as composition. By design the offline tier does not touch them (ADR-112; `docs/rendering.md:116-121`).
  - The edge of the world is visible in shot.
  - The moon's shadows stop at about 77 m.
- **`limits: "unlimited"` is not the intended offline setting.** It also removes the LOD ladder, which ADR-191 measured as the renderer's only prefilter: the offline arm's flicker went up 57% with it lifted.
- **Measured defect: every hard cut opens with one frame of maximum motion blur.**

## Current GV3 configuration vs maximum available

| Item | GV3 now | Max / intended offline |
|---|---|---|
| Output / supersample | 1080p, ×2 (3840×2160 internal). The resolve is one bilinear tap in linear, i.e. a 2×2 box (`tonemap.wgsl:144`, `scene_renderer.cpp:462-467`) | 3840×2160 via `--size`. ×2 is the ceiling (`render_settings.cpp:224`) |
| Tier | offline: 4096 shadow maps, PCF/PCSS 24, AO 6×12 at full res, full material tier (`render_quality.hpp:350-378`) | same |
| Limits | unlimited | `tier`, which gives `offlineDefault()` (`detail_limits.hpp:78-82`) |
| Cascades | 3. The scene override beats offline's 4 (`scene_renderer.cpp:2318`) | 4 |
| Shadow range | automatic ≈77 m, fading from 63 m (`shadow_math.cpp:44`, `shadow_renderer.cpp:199`). Terrain casts only within 150 m | authored per scene (`scene/shadowRange`) |
| Terrain | viewDistance 640, LOD on, lodDistance 86 | view distance ≥950 m (the grid spans [-320,352]², `terrain.cpp:63-78`), LOD off |
| Fog march | **off**: the project's `scene/volumeMaxDistance 0` overrides the scene's 320 (`volume_renderer.cpp:86`, ADR-705). Steps 12, noise and local lights are all inert | march on; 32+ steps |
| Horizon density / pooling / depth layers | 0 / 0 / none | available (`scene_types.hpp:626-638`, ADR-038) |
| Ecology light spill | ≤208 lights, nearest first, within 120 m, drawn from 9,182 emitters. I estimate that reaches only ~55 m | hard cap (`composition.cpp:102`) |
| Firefly swarms | 64 anchors within 50 m | hard cap (`particles.hpp:117-121`) |
| Rig pose rate | 30 Hz in a 60 fps film. The log flags "rate-limited 17 … characters slid or glided" (line 2313) | `updateHz 0` (every frame) |
| AA | FXAA 0.963 plus supersample. No MSAA/TAA/SMAA | same |
| Anisotropy | 8× fixed (`texture.cpp:358`) | 8× |
| Reflections | environment cube only (ADR-099) | no SSR or planar reflections exist |
| Codec | h264, quality 90 (32.7 Mbps), 8-bit BGRA | native `prores422`/`prores4444`; still 8-bit |

## Draw-distance root cause
1. **The world ends in the frame.** `WorldMap` is a fixed 640 m square centred on the origin (`world_map.hpp:131,194`) with no streaming.
   - I probed the engine's own heights along the view axes. In s06, s12 and s14 the skyline is the last terrain before the rim, 335–505 m out.
   - Stills at 24.5, 62 and 78 s show the valley simply stopping against the aurora.
   - No setting can draw terrain that doesn't exist, and there is no backdrop or horizon system.
2. **Moon shadows stop at about 77 m.** That range is ADR-112's automatic value, and it is deliberately the same at every tier. The light rig's own description says the landscape's form comes from the moon raking across it; beyond 77 m it gets none (at 179.5 s the hillside is flat).
3. **Terrain LOD is never lifted.** `chunkLod` (`terrain.cpp:365-382`) draws the skyline at LOD 1–2, about 15–26 px per quad at both 1080p and 4K, so ridgelines are faceted.
4. **There is no aerial perspective.** Fog is closed-form toward navy, and post atmospheric is only 0.035, so the rim reads as a dark cut-out against a bright horizon. Engine gap: fog colour is a constant, so it cannot take the sky or aurora radiance.
5. **Light stops in bubbles around the camera.** The glow spill and fireflies only exist near the camera. In the wides this lights the foreground and leaves the subject (144–200 m away) unlit, which reverses the depth cue. This is inference.

## Recommended offline 4K configuration
```json
"render": {"width":3840,"height":2160,"fps":60,"tier":"offline","limits":"tier",
           "supersample":2.0,"codec":"prores422","quality":95,"aovs":""}
```
```
tools/gpu-lock.sh build/release/src/avgen --project examples/world/glowmere-valley-3.json \
  --render build/gv3/final/glowmere-valley-3-2160p.mov --size 3840x2160 \
  --tier offline --render-limits tier --supersample 2 --codec prores422
```
Check the log for three lines:
- "render scale 2.00: scene target 7680x4320 -> output 3840x2160"
- "LOD rungs kept"
- "rate-limited 0" at the end of the render.

Keep supersample an integer (1 or 2). The one-tap resolve is only an exact box at ×2.

Scene and project changes. These are composition, so the scene must author them:
- **Shadows:**
  - `environment.shadowCascades` 3 → 0, so the offline tier's 4 apply.
  - `scene/shadowRange` 0 → 160 as the base. That keeps ADR-112's 8 cm coarsest texel at 4096.
  - Key `scene/shadowRange` to 300 on the valley wides: s06, s12, s13, s14, s21, s22, s33, s34, s37, s39. That gives a ~15 cm texel.
  - `terrain.shadowDistance` 150 → 320.
- **Terrain:** `nodes/valley/terrainViewDistance` → 1000 and `nodes/valley/terrainLod` → false. That puts every chunk at LOD 0 (1.2 m quads).
- **Fog:**
  - `scene/volumeMaxDistance` → 220 (ADR-705's flagship retune)
  - `scene/volumeSteps` → 32
  - `scene/volumeJitter` → 0.5 (ADR-461)
  - `scene/horizonDensity` → 1.0 (ADR-705's "knob to reach for")
  - Bring the arc's base density down from 0.02 toward 0.009–0.012. Treat these as starting points and tune them with the evaluator.
- **Rigs:** set all 17 `animation.updateHz` from 30 to 0.
- **Optional:** `ecologyGlowCell` 9 → 14 and `ecologyLightRange` → 250. The 208-light cap still binds.
- **World edge (content decision for you and the world author):**
  - One option is extending `world.size` and the valley, river and wall paths beyond ±352.
  - The other is closing the valley ends with ridge features.
  - Fog only softens the edge.

Time and memory:
- The 1080p×2 final took 2,060.8 s (152 ms/frame, 770 s of it waiting on the GPU).
- 4K×1 has the same internal pixel count, so I infer about 35–45 minutes.
- 4K×2 is four times the internal pixels. I estimate 1.5–2.5 hours before the march, longer shadow range and LOD 0 terrain are added, and those add unmeasured cost. Time a 2 s range first.
- Render targets at 7680×4320 should be roughly 2.5–3.5 GB (inferred from the formats at `scene_renderer.hpp:770-779`), which fits in 64 GB.
- The device requests the adapter's limits (`context.cpp:180`), so 7680 px is within Metal's maximum.

## Post stack: current settings and changes
- **Current:**
  - Tone map: ACES (operator 0). The project overrides the scene's AgX, and AgX is the engine default (ADR-039).
  - Chroma retention 0.757; manual exposure −1.25 EV.
  - Bloom: intensity 0.912, threshold 1.761, emissionWeight 0.75, 6 levels.
  - Anamorphic on at 0.25; halation off; grain 0; vignette 0.6.
  - Grade: contrast 1.4, saturation 1.05.
  - Look: atmospheric 0.035 at 450 m; colour, localContrast and lightWrap all 0.
  - DoF off; motion blur 0.954 at 172°, 16 samples, tile size 20; FXAA 0.963.

1. **Tone map.** Switch `post/tonemap/operator` to 1 (AgX) and drop chroma retention to 0.5. `docs/image-formation.md:310-312` says ACES skews saturated emitters.
   - In my mirror of the shader, a beacon cyan at 6.15 comes out (40,232,255) under ACES and (97,226,249) under AgX.
   - Violet fungi at hue 257° come out at 268° under ACES and 261° under AgX.
   - Raise saturation to about 1.15 when switching (`image-formation.md:293-296`). Start contrast at about 1.35.
2. **Bloom.** The threshold is on luminance.
   - As it stands, a Glowmere violet (0.34,0.08,1) needs emissive intensity ≥10.3 before it starts to bloom at all, and ~31 for full weight, before arc boosts.
   - Change: threshold → 0.9, knee → 0.7, emissionWeight → 0.95 (let the emission mask select what glows), intensity → 0.6.
   - Levels → 8 at 4K×2. ADR-279 shows the reach is a pixel count; the look was tuned on ×1 previews, so the current final already had half that reach.
3. **Depth separation.**
   - Add ADR-038 `composition.layers`, all with density 1 and detail 1: 0–60 m contrast/saturation 1.0; 60–220 m 0.92; 220–700 m 0.78/0.82.
   - `post/look/colour` 0.12 (tonal cohesion).
   - `post/look/lightWrap` 0.15 (seats the white animals and aliens in the scene).
   - Atmospheric → 0.05 at 400 m. Check against F11 (the earlier lilac-wall regression).
4. **Filmic character.**
   - Grain 0.035. It also dithers banding in the 8-bit output.
   - Vignette 0.35 (the doc calls anything above 0.5 cheap).
   - Anamorphic 0.12 with stretch ×2 (20.8) to hold its on-screen length at 4K×2, or key it on only for the beam and the drop.
   - Optionally a subtle halation: 0.12, threshold 3, warmth 0.5.
5. **Antialiasing and motion.**
   - FXAA → about 0.5 at 4K×2. It runs at internal resolution; A/B it for flicker.
   - Motion blur samples 32, tile size 40. The tile size isn't scaled by `pixelScale` while `maxRadius` is (`post_processor.cpp:582-585`).
   - Until the engine fix lands, step-key `post/motionBlur/amount` to 0 on the first frame of every shot.
6. **Resolution-dependent look.** These all shift between 1080p×2 and 4K×2: bloom levels, anamorphic stretch, halation, motion-blur tiles, and the water ripple and sparkle pixel fades (`look.py` already re-tuned the water once for ×2).

## AOVs
`--aov normal,emission,depth,velocity,id,shadow` writes `frame_NNNNNN.<aov>.exr` next to a video, plus `materials.json` for the ids (`render_job.cpp:329-417`).
- Depth is in metres and velocity is in UV per frame. The shadow AOV is recomputed.
- AOVs are refused with supersample (`render_settings.cpp:238-242`). Run a 4K×1 evaluator pass: it is pixel-aligned with the 4K×2 beauty.
- `--format exr` gives the post-chain HDR before tone mapping, at internal resolution.
- `--post-stages <dir> --range t:t` dumps the bloom stages for one frame.
- Range renders cap the particle warm-up at 240 frames (`particle_renderer.hpp:202`). Spores live 20–30 s, so start evaluation ranges at least 30 s early.

## Engine gaps that block maximum quality
1. **Cuts are treated as motion.** Only a seek resets motion history (`scene_renderer.cpp:2521-2528`); `prevViewProj_` carries across cuts (4222). This was measured on the delivered film: in 7 of the first 8 cuts, the new shot's first frame has 20–40% of its neighbours' sharpness, and frame 1136 is a full-frame smear.
2. **The world is a fixed 640 m square** with no backdrop system.
3. **Post radii and resolve.** Bloom, anamorphic and halation reach and the motion-blur tiles are counted in pixels. Supersample is capped at ×2, and the resolve is a box filter in linear space.
4. **Hard light budgets:** 256 scene lights and 208 ecology lights, 64 firefly anchors, 8 shadow views. None scales with tier.
5. **No scene reflections** (ADR-099). No TAA. No 10-bit video path.
6. **Fog colour is a constant**, so aerial perspective cannot take the sky's radiance.
7. **The offline tier never raises authored sample counts.** Volume steps, the 8× anisotropy and the 128/256 px sky cube all stay as authored. The visible sky is magnified about 38× at 4K; I infer banding risk from that.
8. **The CPU path tracer can't render Glowmere yet:** that phase is not started (`docs/pathtrace/overview.md:39`).
