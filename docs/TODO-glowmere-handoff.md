# Glowmere Valley: Product Completion Handoff

Updated: 2026-09-10. This is the primary continuation checklist for the next agent.

> **PARKED, 10 September 2026.** Glowmere is a working, reproducible scene and is no longer the
> active project; the flagship effort has moved to *The Living Constellation*. It loads, saves,
> round-trips and renders deterministically, and its remaining defects are listed below rather
> than fixed. Do not delete it and do not break it: it is the control that engine work is measured
> against, and `glowmere-stylized-pbr.json` is its synchronized PBR comparison.
>
> Reproduce the current look:
> ```sh
> cmake --build build/release -j8
> ./build/release/src/avgen --headless --project examples/world/glowmere-stylized.json \
>     --frames 2700 --fps 30 --size 1920x1200 --tier realtime --capture out/glowmere.png
> ```
> M2 Max: 22.1 ms gpu frame median at 1280x800 realtime, 42.3 ms at 2880x1800.
>
> **Deliberately left unfinished:**
> - The hero's cap/stem junction is a cylinder meeting a disc with no flare, and the gill rhythm is
>   perfectly periodic. Both need geometry, not deformers.
> - Foliage edge crawl is improved, not solved. ADR-059's FXAA removes about 16% of the
>   frame-to-frame churn; 2x supersampling would remove 36%. There is still no MSAA and no TAA.
> - 23.7 FPS at 2880x1800 against a 60 FPS goal. The frame fits `14.5 ms + 5.4 ms/megapixel`, so
>   render scale is the lever and it is not built.
> - The Syphon burst failure is still undiagnosed.
> - Two routes are wired to audio; the full musical mapping was never authored.
> - `tools/make_glowmere_score.py` renders the rights-clean 90-second score. The wav is gitignored;
>   `assets/audio/manifest.json` carries the command, seed and sha256.

**Status: tested scene-development and painterly-style candidates, NOT a finished product.**
The visual bar, sustained target performance, soundtrack review and clean full regression gate
remain open. Checked items below describe implemented work, not final artistic acceptance.

## Goal and constraints

Turn Glowmere Valley into a finished, cinematic, ecologically rich, audio-responsive product
that is maintainable and runs interactively on M2 hardware. The latest user direction is a
high-end stylized alien ecosystem: strong silhouettes, sophisticated soft cel shading,
deliberate color hierarchy, painterly materials, atmospheric perspective, dense animated
ecology and selective bioluminescence. Broad modern stylized adventure-game rendering
philosophy is inspiration, not permission to copy Nintendo IP or assets.

The style change is an experiment: it must be more beautiful AND substantially cheaper to
render. Keep current-look and PBR comparisons, use identical camera paths, and measure rather
than assume that cel shading is faster. The working performance target is 60 FPS / 16.67 ms;
final resolution, acceptable tail latency and exact target M2 model must be explicitly recorded
for sign-off. Do not quietly lower the target to match the current result.

The user requested full tests for all changes. Continue implementation and validation, not
only planning. Avoid speculative engine expansion unrelated to a demonstrated product need.

## Start here

1. Inspect `git status --short`. The existing dirty worktree is the deliverable from both
   previous phases. Preserve it, including untracked files; do not reset, discard, or overwrite
   it. No commit or branch was created by the previous agent.
2. Read this document and [the measured style experiment](stylized-glowmere.md). The earlier
   [directed-candidate record](shot-glowmere.md) explains the first phase; its performance
   numbers predate a depth fix and are not the current baseline.
3. Open the painterly project and review the entire moving shot. Record defects with timestamps.
   Start with the noisy terrain/foliage and hero close pass, not a new global rendering feature.
4. Choose one local, falsifiable hypothesis, find the owning code or authored setting, and run
   a cheap discriminating check. Make a small edit and validate it before widening scope.
5. Update this checklist with evidence, not just completion marks. Keep matched comparisons
   synchronized when intentionally changing shared camera or scene data.

## Entry points and artifacts

| Purpose | File |
|---|---|
| Main candidate | [glowmere-stylized.json](../examples/world/glowmere-stylized.json) |
| Candidate scene | [glowmere-stylized.scene.json](../examples/world/glowmere-stylized.scene.json) |
| Same scene with PBR | [glowmere-stylized-pbr.json](../examples/world/glowmere-stylized-pbr.json) |
| Previous-look comparison | [terrain.json](../examples/world/terrain.json) |
| Previous scene | [terrain.scene.json](../examples/world/terrain.scene.json) |
| Shared shot light rig | [glowmere.rig.json](../examples/lightrigs/glowmere.rig.json) |
| Example browser entries | [index.json](../examples/index.json) |
| Asset setup and provenance | [assets.md](assets.md), [asset-library.md](asset-library.md) |

Local generated media live in `examples/world/renders/` and are gitignored. They may not exist
on another checkout; regenerate them with the commands below. The latest files on this machine:

- `glowmere-stylized-review.mp4`: complete 90 seconds, 1280x720, H.264, 2,700 frames,
  zero GPU errors, sequence hash `42c44134a146b511`. Opened in the macOS video viewer.
- `stylized-opening.png`, `matched-pbr-opening.png`, `current-opening.png`: corresponding
  1440x900 frame-179 captures using the corrected depth path, roughly six seconds into the shot.
- `glowmere-review.mp4`: older prior-look review, hash `4d96632a62612712`.
- `style-close/` and `style-vista/`: 30-second and 82-second stills from an intermediate style
  revision, BEFORE final texture-RGB policy and retinting. Do not present these as final captures.

Rendering every frame verified execution. Continuous playback was not comprehensively reviewed
by the agent; no shimmer, LOD, musical-response or finished-composition sign-off is implied.

## Completed work

### First phase: world and authoring

- [x] A 90-second camera journey: low approach, crown underside pass, valley rise. Separate
  smooth position/target tracks, no loop, final pose held. All three projects share the tracks.
- [x] Procedural mushroom hero: sphere crown, curved spline-tube stem, 38 radial filaments,
  foreground foliage and spores. Current sphere is deliberately still identified as too regular.
- [x] Build-time ecology proximity relationships, deterministic XZ spatial hashing and dependent
  invalidation. Anchor layers must precede dependents; validation covers distance/fade/strength,
  unique names and references. Legacy no-rule placement streams remain unchanged.
- [x] Independent material-part multipliers for multi-material procedural mesh nodes: tint,
  emissive gain, roughness and opacity. Rest-relative application, texture bindings preserved,
  repeat-frame and unregister tests. NOT named terrain-scatter part controls or ordinary glTF parts.
- [x] Fixed project loading discarding a composition-owned HDR map when no override is supplied.
  Explicit overrides still win; reloading file scenes restores authored environments. Regression
  coverage includes file/inline scenes, override and scene switching.
- [x] Original crown, ground and tissue material programs; dedicated light rig; project-local
  emission overrides rather than changing shared foliage materials globally.
- [x] Bounded, smoothed audio routes: bass adds up to 0.16 crown emission for unit input,
  attack/decay 1.2/3 seconds; treble adds up to 24 to a 300/s spore rate, attack/decay 0.6/2 seconds.
  Camera and key light are not audio driven. No soundtrack bundled or auditioned.

### Second phase: stylized experiment

- [x] Opt-in `environment.stylized` strict boolean, default false, exposed as `scene/stylized`.
  Serialization, authored default and live switching tested. Existing spare `lightCounts.z`
  carries the renderer flag without changing GPU uniform layout.
- [x] Shared entity/procedural/SDF surface path: soft punctual-light bands, restrained highlight,
  hemisphere ambient, screen AO, subtle rim, existing shadows/fog/emission. PBR remains available.
- [x] Styled lit surfaces use authored factors/program colors instead of base-texture RGB;
  alpha and unlit texture behavior are retained. Normal/metalrough/occlusion maps and split-sum
  IBL are bypassed in the styled path. Emissive textures and material programs still execute.
- [x] Procedural depth now respects scalar/texture alpha cutouts. Entity and procedural vertex
  positions marked invariant across pipelines. This fixes a real invisible-rectangle occlusion
  defect; program-driven opacity is still NOT evaluated in depth, matching the entity limitation.
- [x] Analytic night sky with direction-anchored filtered stars and a disk aligned with the first
  directional light. No overlay for HDR/PBR. Disk currently lies outside the opening framing.
- [x] Palette/geometry iteration: violet crown, warm gills/filaments, cool foliage, cheaper
  Quaternius ferns/grass, twisted trees, broad fan plants, denser grass, restrained glow.
- [x] Reduced repeated vegetation material programs, zeroed aggregated ecology lighting, and
  reduced volume steps from 18 to 12. Preserved culling/LOD, wind and plant motion.
- [x] Separate painterly and matched-PBR projects registered under Look development.
- [x] New painted crown, ground and foreground-frond materials, with finite-output and
  underside-emission tests. All six retained Glowmere material programs are tested.
- [x] Repeated three-arm benchmarks, complete review movie and documented scope/limitations.

## Evidence snapshot

Apple **M2 Max**, release, realtime tier, 1440x900, first 10 seconds of the same camera shot.
Three interleaved repetitions per project, 300 frames each, 288 warmed frames. All nine runs
reported zero GPU errors. No concurrent build/GPU work. See [full metrics](stylized-glowmere.md)
for per-run values, pass costs, CPU samples, draws and reproduction details.

| Metric | Prior candidate | New scene PBR | New scene stylized |
|---|---:|---:|---:|
| Median wall ms | 58.05 | 31.05 | 31.18 |
| Median GPU ms | 53.94 | 26.28 | 26.41 |
| Median wall p90 ms | 73.31 | 37.40 | 38.47 |
| Visible / logical instances | 1714 / 81212 | 2255 / 116612 | 2255 / 116612 |
| Peak process RSS MiB | 1218.9 | 740.4 | 748.4 |

The candidate reduces median wall time 46.3% with 43.6% more logical instances. **Matched PBR
ties stylized: no shader-only speedup was established.** The scene-level optimizations matter.
About 32 reciprocal-median FPS is not sustained 30 FPS or the 60 FPS goal. RSS is not separately
measured GPU memory; logical triangle/instance counts are not submitted post-LOD geometry.
CPU update samples are not whole-frame CPU latency. This is not base-M2 or whole-shot evidence.

Uncontended 1280x720 preview runs: wall medians 21.56/23.97/24.99 ms, p90 29.89/32.62/35.80 ms.
Median-of-medians is about 41.7 reciprocal FPS. Earlier preview runs overlapped the sanitizer
tail and were excluded. Offline encoding throughput is never an interactive FPS measurement.

Validation completed before this documentation-only handoff:

- [x] Full release CTest: 922 discovered, 917 passed, four skipped, ONE FAILED. The failure is
  Syphon frame-burst reception at `frame.has_value()`; isolated rerun also failed. It failed
  intermittently before the style work too, but its root cause is not established.
- [x] Four new GPU tests, 33 assertions: mode determinism/reversibility, texture policy,
  procedural alpha-depth visibility and direction-anchored sky.
- [x] Three-project shot contract: 13,664 assertions covering the 90-second route, clearance,
  continuity, valid materials/automation and correct environment/mode selection.
- [x] Six-material behavior test: 6,662 assertions in release and ASan/UBSan.
- [x] ASan/UBSan mode/shot/earlier-material run: 17,003 assertions across three cases.
- [x] Earlier phase ASan/UBSan ecology/composition/project selection: 53 cases, 23,707 assertions,
  before final style work. This is historical coverage, not a final whole-suite sanitizer run.

Four skips are optional Khronos sample imports (two), external ffmpeg and NDI runtime. Native
macOS H.264 export works without ffmpeg. Do not call the full suite green or hide the Syphon issue.

## Completion TODO

### P0: finish the image and moving shot

- [ ] Review the full current movie and live camera journey; create a timestamped defect list
  and final-revision captures at opening, close pass, vista and ending. Success: evidence covers
  the whole shot, not just the first favorable frame.
- [~] Resolve remaining terrain patterns and noisy foliage edges. Isolate geometry/depth,
  material, lighting and sampling causes with a minimal reproducer. Add a targeted regression
  for the confirmed cause; check PBR and styled paths, then inspect moving footage.
  - Terrain pattern: SOLVED earlier (styled AO applied to ambient at half depth).
  - Foliage *erosion*: SOLVED. The Quaternius foliage is `alphaMode: MASK` at cutoff 0.20, and
    a fixed cutoff eats more of the leaf at every mip level, so distant crowns dissolve. The
    cutoff now falls with the sampled mip level (`kAlphaCoverageFade`, shaders/pbr_shade.wgsl).
    Measured against a 2x supersampled render of the same frame, treeline band mean luminance:
    reference 0.11781, coverage fade on 0.11774 (error 0.00007), fade off 0.11712 (0.00068).
    Ten times closer to the truth. Changes 2.18% of pixels, none above the horizon.
  - Foliage *edge crawl* under motion: STILL OPEN. A frame-to-frame flip count in the treeline
    band moved 10.664% -> 10.639%, which is nothing: the metric is swamped by parallax and
    vegetation motion and cannot see the alpha edge. Crawl needs an AA answer (no MSAA, no TAA),
    not a cutoff fix. Do not claim it fixed.
- [~] Give the hero a less regular silhouette and authored detail hierarchy. Improve cap/stem
  junction, gill rhythm and close-up readability without replacing one cheap primitive with
  uncontrolled tessellation or full-scene expensive noise. Verify close-pass clearance and cost.
  - Silhouette: done earlier with two displacement deformers on crown and stem.
  - Cap/stem junction and gill rhythm: STILL OPEN, and neither is a deformer's job. At frame 600
    the stem still meets the cap as a plain cylinder against a disc with no flare, and the gills
    are perfectly periodic. Both need geometry.
- [ ] Replace repetitive forest silhouettes and sparse hillside composition with deliberate
  ecological groups and foreground/midground/background separation. Density is not sufficient
  evidence of richness; inspect actual screen coverage and preserve habitat rules.
- [ ] Strengthen the ending's visual destination and progression. If changing camera tracks,
  update comparison projects together and rerun the complete shot-contract test.
- [~] Tune atmospheric perspective, shadow/fill balance and selective glow across the shot.
  Avoid turning every plant emissive or flattening depth with fog. Decide whether the sky disk
  should enter the composition; changing the shared rig also affects the prior-look comparison.
  - The scene was measurably flat, not subjectively so. Frames 120/600/1200 all had
    `shadow_frac = 0.0000` and `mid_frac` 0.98-0.999: no pixel anywhere in the shot fell below
    8% luminance. Cropping near ground against far ridge gave 0.296 vs 0.226 -- seven hundredths
    between a fern at the viewer's feet and a treeline four hundred metres away.
  - Three causes, all structural: the styled hemisphere ambient was a shader constant that
    outgunned the key by about 2.5x on flat ground (raising the rig key 9x moved mean 0.237 ->
    0.462, so the key was wired correctly and simply losing); the surface fog ignored the mist
    layer the volumetric marches; and `fogColor` sat at the same luminance as the ground, so
    mixing towards it could neither lift nor deepen the distance. See ADR-058.
  - Fixed by making the hemisphere authorable (defaults bit-identical: 0 of 2,304,000 pixels
    differ), integrating the mist layer analytically in `applyFog`, giving Glowmere its own rig
    (`glowmere-valley.rig.json`, key 4.5) so the shared terrain scene is untouched, lifting the
    fog colour, and turning on ADR-053's ecology light field so the world's own glow fills the
    shadows a lower ambient leaves.
  - Frame 1200, before -> after: shadow_frac 0.0000 -> 0.2099, mid_frac 0.9990 -> 0.7890,
    p01 0.1231 -> 0.0581, mean_saturation 0.578 -> 0.645, near->far gradient 0.070 -> 0.074.
    GPU median 20.8 -> 21.5 ms at 1280x800 realtime, ecology light field included.
  - Second pass, three findings:
    1. Darkening the world by 1.3 stops turned the bloom off for most emitters -- exposure is
       applied before bloom, so a threshold of 1.0 needed 2.46x more radiance than before.
       Emissive that stands for real light now carries the reciprocal (x2.46), which holds its
       absolute value across the exposure change while the lit world stays 2.46x darker.
    2. The canopy and pine "firefly" emissive is a shading trick, not a light, and scaling it
       with the rest bleached every tree crown to cream. It is held at its old value instead,
       and recoloured to warm amber (1.0, 0.82, 0.45) so it reads as fireflies rather than
       frost, which is what it was asked for. Frame 1200 saturation 0.617 -> 0.687.
    3. The terrain's colour is NOT reachable from `material.baseColor`. `paintedGround` writes
       `baseColor` from a `ramp` op, so the node's authored colour is dead data -- scaling it by
       0.5 and by 0.3 produced byte-identical frames, which is how it was caught. The pale wash
       lives in the material's three ramp constants; they are now x0.42. Worth knowing before
       anyone else tries to colour a terrain that has a program on it.
  - Also: the moon was near-white (0.74, 0.84, 1.0) and bleached what it lit; it is now
    (0.42, 0.62, 1.0) in Glowmere's own rig.
  - Whole shot, committed baseline -> now: shadow_frac 0.0000/0.0000/0.0000 ->
    0.287/0.356/0.239 at frames 120/600/1200; mid_frac 0.994/0.981/0.999 -> 0.711/0.642/0.759;
    p01 0.118/0.116/0.123 -> 0.054/0.048/0.055; mean_saturation ~0.58 -> ~0.72.
  - Still open here: the glow pools keep white cores where a cluster light is strong; and frame
    600's p99 fell 0.665 -> 0.504, because the hero's brightness used to come from moonlight on
    its cap rather than from its own emission. Whether that is a loss or the point is an art
    call that wants the user's eye.
- [ ] Review sustained motion for shimmer, LOD popping, exposure changes and vegetation motion.
  Check target output resolution and intended preview modes; successful encoding is not enough.
- [ ] Audition real audio/live input, silence and transients. Verify the bounded routes feel
  musical, do not clip highlights and behave predictably on seek/restart. Bundle a soundtrack
  only with suitable rights or user-provided material; document the reproducible audio setup.

### P1: meet the performance gate without losing the art

- [ ] Record the exact target device, resolution and accepted latency distribution with the
  user when needed. Keep 60 FPS / 16.67 ms as the working goal; base M2 is currently unverified.
- [ ] Profile expensive views across all 90 seconds, including sustained thermal runs. Report
  warmed frame median/p90/tail, GPU passes, meaningful CPU latency, draws, submitted work if
  available, visible/logical instances, shadow/particle costs and clearly labeled memory metrics.
- [~] Investigate the dominant scene pass first: it is about 23.5 ms of 26.4 ms GPU time in the
  current styled sample. Use measured material/overdraw/geometry attribution before choosing
  the next optimization. Do not assume the approximately 0.9 ms shadow pass is the main blocker.
  - The frame is roughly two thirds fixed and one third fill. `glowmere-stylized.json`, realtime
    tier, gpu frame median: 1440x900 21.43 ms, 1920x1200 25.69 ms, 2880x1800 42.27 ms. Fitting
    `t = a + b*Mpx` gives a = 14.5 ms resolution-independent and b = 5.4 ms per megapixel.
  - That is the whole of the reported "sub 20 fps at standard app size": at 2880x1800 the frame
    is 42.3 ms (23.6 FPS) and 28 ms of it is fill. Resolution scaling is the largest untried
    lever for that complaint, and it is a P1 decision, not an art one.
  - Three-arm benchmark, 300 frames, 1440x900, realtime, three interleaved rounds, quiet machine,
    gpu frame median: skyonly 4.06/4.00/4.06, terrain 37.81/37.81/37.62,
    glowmere-stylized-pbr 20.84/20.64/20.71, glowmere-stylized 20.84/20.97. Reproducible to
    +/-0.2 ms across rounds. The styled path costs the same as its PBR control, so the painterly
    look is free; `terrain.json` is 1.8x the product scene and is not the thing to optimise.
- [ ] Keep the current-look control and synchronized new-scene PBR control. The latter differs
  in surface policy AND sky overlays; isolate those separately for a strict shader-only claim.
  Compare identical cameras, simulation inputs, sizes and quality settings without concurrent work.
- [ ] Reinvest verified savings in visible ecological/art quality, then remeasure. Preserve
  raw run data and final scene settings so visual changes do not silently invalidate benchmarks.
- [ ] Add quality controls/adaptation only if evidence shows a product requirement that existing
  manual tiers cannot meet. Any fallback needs visual transition checks and explicit behavior.
- [ ] Establish the performance goal on actual target hardware. If inaccessible, leave that
  acceptance gate blocked rather than translating M2 Max results into an unsupported base-M2 claim.

### P1: correctness and delivery

- [ ] Diagnose the Syphon burst failure separately with a focused reproducer. See
  [test_texture_share.cpp](../tests/rendering/test_texture_share.cpp#L214). Determine whether
  this is transport/timing/environment behavior; do not weaken the assertion or repeatedly rerun
  until green. Fix a confirmed defect with coverage, or document a justified platform blocker.
- [ ] Add focused tests for each new fix, then run the full release suite and appropriate
  sanitizer coverage after the final substantive changes. Publish failures/skips honestly.
- [ ] Check asset completeness, licences and project-relative paths on a clean setup. Untracked
  source assets/configurations must be included in any eventual handoff or user-approved commit;
  ignored local movies are not a portable delivery mechanism.
- [ ] Validate open/play/seek/restart, project switching, PBR/style switching, audio setup,
  save/reload and native movie export. Update user-facing launch and asset instructions.
- [ ] Produce the final whole-shot movie, matched stills and measured report, then obtain
  visual acceptance. Only promote the candidate from Look development to a finished showcase
  when the actual image, performance and correctness gates are satisfied.

### Conditional backlog, not automatic scope

- [ ] Evaluate artist-exposed ramps/ambient controls only if the current shader constants block
  the chosen look. Stylistic hemisphere ambient currently ignores environment-map intensity.
- [ ] Evaluate named terrain-layer material-part controls if necessary for authoring. Existing
  part indices are surface-area ordered, not semantic material names; programs may override factors.
- [ ] Evaluate program-opacity depth parity only for a demonstrated material requirement; current
  alpha-depth fix deliberately covers scalar/texture alpha, not arbitrary material interpreter output.

Automatic frame-budget control, new occlusion culling, hero rigid-body physics and a proposed
1k-to-100k benchmark ladder were not implemented. They are not prerequisites by default: decide
from the agreed product requirements and evidence, not the desire to enlarge the engine.

## Session log: motion smoothness and the terrain stripes (agent, later 2026-09-10)

No commit or branch was made; the worktree is preserved as handed over.

### Vegetation motion reads as glitchy — partly diagnosed, partly fixed

The user reported motion as "jagged rocking back and forth", not a slow breeze, after an earlier
tuning pass had already moved Tier 0's analytic tip displacement to 1.1 direction reversals per
second. Both scenes were verified to carry those tuned values, so the complaint was about
something the analytic model does not see.

**A measurement trap worth recording.** The first attempt measured mean luminance of a screen
region across a rendered sequence of the 90-second shot. That is invalid here: the camera moves,
so it sweeps scene detail through a fixed window and swamps the vegetation. The control proved
it — with wind fully DISABLED the metric got *worse* (jerk 1.559 against 1.234 with wind on).
Use `--composition examples/world/glowmere-stylized.scene.json`, which renders the scene's own
static camera without the project timeline, and analyse each pixel's time series separately
rather than a region mean, which cancels opposing motion.

Measured that way over 45 frames at 640x400, counting only pixels that actually move:

| configuration | moving px | mean per-frame change | reversals/s |
|---|---:|---:|---:|
| wind disabled (spores only) | 146 | 0.69 | - |
| Tier 0 only | 242 | 0.96 | 5.01 |
| **Tier 0 + Tier 1 (as shipped)** | **712** | **4.31** | **7.86** |
| Tier 1, damping 1.8 | 419 | 2.89 | 6.37 |
| Tier 1, damping 3.0 | 270 | 1.11 | 4.96 |

**Tier 1 is the dominant source**: it quadruples the motion and adds reversals. Damping it to 3.0
converges it onto Tier 0, which is disabling it by another name rather than tuning it. **Tier 1
simulation is therefore switched off for the four layers in `glowmere-stylized.scene.json`.** The
still frame changes 3.57% — composition preserved, only motion. Tier 1 remains available and is
still enabled in `terrain.scene.json`; this is a per-scene art decision, not a revert of ADR-056.

**Not explained, and not claimed as fixed.** About 5 reversals/s of pixel-level flicker remains
with Tier 0 alone, and its *rate* does not fall when wind speed drops from 0.55 to 0.40 (5.01 ->
5.15) even though amplitude falls 5x. So that component is not the sway frequency. Two candidates
are untested: the spore particles inside the sample region, and sub-pixel edge crossing on
alpha-tested foliage. 2x supersampling with a static camera moved jerk only 1.918 -> 1.762, so
aliasing is a contributor of roughly 8%, not the cause. **The renderer has no MSAA
(`multisample.count = 1` everywhere) and no TAA**; that is a real, separate finding for the
"review sustained motion for shimmer" item, but supersampling evidence says it is not the main
cause of this complaint.

### Terrain pattern: SOLVED. It is screen-space AO printed into the styled ambient term

Not stripes: a faint regular **square lattice** on open ground, present in the styled path and
absent from the matched PBR control at the same camera and scene. Crop x 160-430, y 445-510 at
1440x900, frame 179, with a gentle contrast stretch.

**Cause.** In `pbr_shade.wgsl` the styled branch computes

```wgsl
let visibility = mix(0.35, 1.0, clamp(occlusion.visibility * programOcclusion, 0.0, 1.0));
let ambient = baseColor.rgb * hemisphere * visibility;
```

On a night landscape that hemisphere ambient is the dominant light on terrain, so screen-space AO
applied to it at full depth writes GTAO's own sampling pattern straight into the largest term in
the image. PBR does not show it because there ambient is one contributor among several. GTAO's
radius is in world units, which is why the pattern's pixel period scales with render resolution
and looked world-locked.

Proved by disabling AO (`--disable ao`): the lattice disappears entirely from that crop while
everything else in the frame is unchanged.

**Mitigation applied**, in the styled branch only: the visibility floor goes 0.35 -> 0.68, halving
AO's depth on ambient. The lattice becomes much fainter, contact darkening is retained, and the
frame is 2.0% brighter overall (mean level 66.01 -> 67.34, 13.5% of channels changed). Focused
tests pass: `[stylized]` 10 assertions, render `[stylized]` 33, `[integration][glowmere]` 13,662.

**Not a complete fix, and here is what is left.** The lattice is reduced, not eliminated, because
halving a linear term halves its noise. GTAO already jitters its rotation per frame and the AO
renderer already has a temporal filter, so a static camera converges -- but this shot has a moving
camera, reprojection rejects history, and the raw pattern survives. The real fix is stronger
temporal accumulation or a spatial denoise on the AO target. That is a change with cross-scene
regression risk and was deliberately not attempted here.

**Method warning, because it cost most of a session.** A scalar profile (deviation from a
9-pixel moving average along a scanline) is *not* sensitive to this lattice: five interventions
each moved it under 0.1 levels against a 5.47 baseline, including doubling terrain resolution,
flattening the ground ramp, removing `pebbles` and `grass`, widening the normal stencil, and
widening the `painterlyRamp` smoothsteps -- and disabling AO, the actual cause, also moved it only
0.06. The metric is not broken (forcing normals flat moves 13.37% of the frame); it simply does
not see a low-amplitude periodic lattice. **Compare matched crops visually.** Each of those five
interventions was verified to take effect, so they do still eliminate their hypotheses: mesh
resolution, the ground colour ramp, the height field at 1.25-3.75 m, and the punctual band ramp
are all innocent.

### Hero silhouette: broken up with deformers, junction still open

The crown was a scaled sphere and read as a lathe primitive: a mathematically perfect elliptical
rim. Two `displacement` deformers on `elder-crown` (0.115 at scale 1.45, 0.038 at scale 4.8, both
static) push vertices along their normals from low-frequency noise, so the rim undulates and the
dome gains soft lobes **without adding a triangle** -- the brief's constraint against replacing a
cheap primitive with uncontrolled tessellation. The 96x48 sphere already had the vertices to carry
it. Changed 11.06% of the close-pass frame; the gill sweep still reads cleanly.

`elder-stem` gets the same treatment at a finer scale (0.085 at 0.55, 0.03 at 2.1). It already
flared through its curve point scales; what it lacked was surface irregularity. Small effect
(0.50% of the frame) because the stem occupies little screen area.

Deformers cost vertex work and the stack is evaluated four times per vertex (position, two
epsilon neighbours for the finite-difference normal, and the previous frame for velocity), so
this is only affordable because both are single instances. Do not copy the pattern onto a scatter
layer without measuring.

**Still open: the cap/stem junction.** The stem still penetrates the gills as a hard intersection.
Fixing it properly needs geometry -- a flare or skirt where the two meet -- rather than a
deformer, so it was left rather than faked.

### Validation for this session

Full release CTest after the AO change: **922 discovered, 922 passed, 0 failed, 4 skipped**
(the usual two Khronos sample imports, ffmpeg and NDI).

**The Syphon frame-burst test passed in this run.** That does NOT resolve it. The handoff records
it as intermittent and as failing before the style work too, so one green run is not evidence of
a fix; its P1 entry stands untouched. Anyone citing this run should say "passed once", not
"fixed".

Focused: `[stylized]` 10 assertions, render `[stylized]` 33 assertions across 4 cases,
`[integration][glowmere]` 13,662 assertions. Release build is warning-free.

Not run this session: the ASan/UBSan passes. The handoff's gate asks for sanitizer coverage after
the final substantive changes, and the AO change qualifies, so that gate is still open.

## Ownership map and sharp edges

| Area | Primary files / tests |
|---|---|
| Habitat rules and spatial hash | [ecology.cpp](../src/world/ecology.cpp), [ecology.hpp](../src/world/ecology.hpp), [test_world.cpp](../tests/unit/test_world.cpp) |
| Scene parsing, parameters, parts, scatter rebuild | [composition.cpp](../src/scene/composition.cpp), [composition.hpp](../src/scene/composition.hpp), [test_composition.cpp](../tests/unit/test_composition.cpp) |
| Project environment handling and shot contracts | [engine.cpp](../src/app/engine.cpp), [test_project_system.cpp](../tests/integration/test_project_system.cpp) |
| Mode storage and packing | [scene_types.hpp](../src/scene/scene_types.hpp), [scene_renderer.cpp](../src/rendering/scene_renderer.cpp) |
| Surface shading | [pbr_shade.wgsl](../shaders/pbr_shade.wgsl), [lighting.wgsl](../shaders/lighting.wgsl), [test_gpu.cpp](../tests/rendering/test_gpu.cpp) |
| Alpha-depth and invariant positions | [procedural.wgsl](../shaders/procedural.wgsl), [common.wgsl](../shaders/common.wgsl), [test_procedural_gpu.cpp](../tests/rendering/test_procedural_gpu.cpp) |
| Analytic sky overlay | [skybox.wgsl](../shaders/skybox.wgsl), [test_sky_gpu.cpp](../tests/rendering/test_sky_gpu.cpp) |
| Material behavior | [test_material_program.cpp](../tests/unit/test_material_program.cpp) |

- Material programs use eight vec4 registers, at most 48 operations. Output channels replace
  scalar factors; texture and per-instance multiplication occur afterward. Repeated general
  material programs were costly. The simplified ground uses three ops, foreground frond five.
- Painted material files are `glowmere-painted-{crown,ground,frond}.material.json` under
  `examples/materials/`. A painted-canopy program was tried then deleted as unused; do not
  restore it accidentally. `paintedFrond` is foreground-only. Some legacy material references,
  such as the new scene's unused bush-glow registration, may remain; verify use before cleanup.
- The ecology layer named `pines` now uses TwistedTree_2. Do not infer the actual species/asset
  solely from a historical layer name. There are about 116,612 logical instances, not that many
  visible draws. Particle capacity remains 10,240.
- Styled ambient/ramp constants are not material-specific. Area lights still use LTC; not all
  lighting became a cheaper BRDF. The general material interpreter still runs when assigned.
- Composition destruction is not parameter unregistration. The engine resets top-level
  parameter ownership; detach restores authored defaults. Do not introduce ownership changes
  merely to satisfy an incorrect test assumption.

## Failed hypotheses and measurement traps

- Removing a tangent normal derived from biome UVs did not remove the ground bands. Root cause
  remains unresolved; do not report that hypothesis as a fix.
- Disabling shadows did not remove the foliage stippling and did not establish useful savings.
- The empty procedural depth fragment was a real defect. Alpha discard and invariant positions
  were added together; do not attribute all visual improvement to one independently proven cause.
  Correcting depth increased visible work, so earlier faster broken-depth timings are invalid controls.
- Cel shading alone on the original scene did not make it faster. New-scene PBR and style tie.
- Ignoring texture RGB without retinting made the scene pale. The current authored colors address
  that consequence; preserve intentional unlit and alpha behavior when changing texture policy.
- More fog made the scene flat. More instances did not by itself finish the composition.
- Catch2 test names containing commas split filters. Use the tags below.
- GPU test helpers can place a single instance offscreen if they assume multiple grid columns;
  the depth regression uses an explicitly centered one-by-one grid. Image8 uses `pixel(x,y)`.
- A missing project/composition can warn, render a fallback orb and still exit zero. Inspect
  warnings, project-load messages, GPU errors and the image, not just the process exit status.
- Runtime WGSL comes from the working tree. Pin `AVGEN_SHADER_DIR` for binary A/B comparisons;
  a newer-than-binary advisory is not itself a pipeline failure. Rebuild if layouts changed.
- `--fps` sets simulation cadence; `--quality` is encoding quality. Neither is the achieved FPS
  or scene rendering tier. Offline throughput and whole-process load time are not frame latency.

## Commands and local evidence

Run from the repository root. C++23/CMake/Ninja, Dawn WebGPU on Metal, Catch2. Presets include
release/debug/asan/tsan; ASan also enables UBSan. No new dependency or internet download was
introduced. `rg` is unavailable on this machine; use editor search or shell grep.

```sh
cmake --build --preset release -j 4
./build/release/src/avgen --project examples/world/glowmere-stylized.json --play --tier preview

./build/release/tests/avgen_tests '[stylized]'
./build/release/tests/avgen_tests '[integration][glowmere]'
./build/release/tests/avgen_tests '[material][glowmere]'
./build/release/tests/avgen_render_tests '[stylized]'
ctest --preset release --output-on-failure
ctest --preset release --output-on-failure -R 'Syphon server follows source size changes'

cmake --build --preset asan --target avgen_tests -j 4
./build/asan/tests/avgen_tests '[stylized],[glowmere]'

/usr/bin/time -l ./build/release/src/avgen --headless \
  --project examples/world/glowmere-stylized.json --frames 300 --fps 30 --size 1440x900 --tier realtime

./build/release/src/avgen --headless --project examples/world/glowmere-stylized.json \
  --frames 180 --fps 30 --size 1440x900 --tier realtime --capture /tmp/glowmere-review.png

./build/release/src/avgen --project examples/world/glowmere-stylized.json \
  --render renders/glowmere-stylized-review.mp4 --range 0:90 --size 1280x720 --fps 30 --codec h264
```

Repeat the benchmark for `terrain.json`, `glowmere-stylized-pbr.json` and
`glowmere-stylized.json` in interleaved rounds. The existing `tools/bench_ab.sh` operates on
static composition inputs; do not accidentally lose the project timeline when using it.
`--disable shadows,ao,volume,post` supports pass attribution, not a final quality comparison.
Rendering paths are relative to the project directory; capture paths are relative to the cwd.
macOS `/var` and `/private/var` aliases matter in path tests; use canonical filesystem comparisons.

Temporary evidence on this machine (not durable and not required on a fresh checkout):

- `/tmp/style-bench-{terrain,glowmere-stylized-pbr,glowmere-stylized}-{1,2,3}.log`
- `/tmp/style-preview-clean-{1,2,3}.log`
- `/tmp/glowmere-style-full-tests.log`, `/tmp/glowmere-style-asan.log`
- `/tmp/glowmere-style-movie.log`, `/tmp/glowmere-current-final.log`, `/tmp/glowmere-matched-final.log`
- Earlier probes: `/tmp/glowmere-style-before.png`, `/tmp/glowmere-no-shadow.png`,
  `/tmp/glowmere-depth-fix.png`, `/tmp/glowmere-material-probe.png`. These are historical, not
  final-quality references. No benchmarking/test terminal remains pending at handoff.

## Bootstrap prompt for the next agent

```text
Continue the Glowmere Valley finished-product task in this workspace. This is an implementation
handoff, not a request for another plan-only report.

Read docs/TODO-glowmere-handoff.md first, then docs/stylized-glowmere.md. Inspect git status and
preserve ALL existing modified and untracked work. Do not reset the repo, commit, or create a
branch unless I request it. The handoff contains implemented work, code owners, tests, measured
performance, known failures, failed hypotheses, commands and completion gates.

Main entry: examples/world/glowmere-stylized.json. Preserve the previous-look terrain.json and
the matched glowmere-stylized-pbr.json comparison. Finish an original, high-end stylized alien
ecosystem: strong silhouettes, soft cel lighting, painterly colors, rich animated ecology,
atmospheric depth, selective bioluminescence and restrained musical response. No copied IP.

Start by reviewing the full current shot and inspecting the terrain/foliage artifacts and hero
close pass. Record timestamps, choose one local falsifiable hypothesis, make a focused fix and
validate immediately. Then work through the prioritized handoff checklist. Do not add broad
engine features without evidence that they are needed to finish the product.

The current scene is NOT finished. It measures 31.18 ms median at 1440x900 on M2 Max; matched
PBR is 31.05 ms, so there is NO demonstrated shader-only speedup. The working target remains
60 FPS / 16.67 ms on agreed M2 hardware/resolution. Base-M2, whole-shot and thermal performance
are unverified. Do not confuse offline export rate or fixed --fps with achieved FPS.

Run focused tests for every change, full release regression tests and appropriate sanitizers
before sign-off. The last full suite had 917 passes, four skips and one unresolved Syphon burst
failure; do not call it green. Preserve reproducible visual and performance comparisons, review
motion and real audio, and keep the TODO updated with evidence and any genuine blockers.

Finish with a usable scene, review artifacts, honest test/performance results and explicit
remaining gates. Do not label another partial visual iteration a finished product.
```