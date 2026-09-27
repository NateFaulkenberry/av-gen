# Render stream (ADRs 917–919): the agent's final report

Reported on 2026-09-27. Branch `agent/render`, final commit `39388364`, with main `3f720bfa` merged.
**Not merged yet:** the full GPU suite on the final code has not run. Stills are in
`~/Desktop/av-gen-review/18-glowmere-valley-3/revision/render/`; evidence scripts and a GV3 snapshot
are in the worktree's `build/gv3-eval/`.

## What was built
- **ADR-917: the post chain's sizes follow the frame, from one reference height.**
  - `post/referenceHeight` (default 720). Every pixel-sized post value scales by chain height over reference.
  - A frame whole octaves finer than the reference is box-filtered down first, so bloom and halation run the reference's own pyramid.
  - The anamorphic reach and the motion-blur tiles scale; tiles over 40 px use a two-pass maximum.
  - The look stage's blur steps down an octave instead of truncating.
  - At its reference the chain is unchanged, bit for bit.
- **ADR-918: aerial perspective takes the sky's radiance.** `scene/fogSky` (0–1) and `scene/fogSkyDistance` (0 = automatic, three fog extinction lengths). A 128×32 sky map, including the aurora and comets, is built each frame; the fog fades toward it with distance, and near air keeps the fog colour.
- **ADR-919: the offline tier floors authored sample counts.** Fog march at least 32 steps, textures at 16× anisotropy, procedural sky cubes at 1024 px a face. Each floor is logged, and it never lowers anything.
- **The world edge:** report only. Growing `WorldMap` re-ranges the altitude survey, which moves biomes and scatter, and a backdrop ring would take 1–2 days. GV3 closes its valley ends instead (gv3-world did).

## Where the controls are in the UI
- **`post/referenceHeight`:** Parameters → `post` → the first row.
- **`scene/fogSky`:** Environment panel → "Sky and fog" → "Fog takes the sky's colour".
- **`scene/fogSkyDistance`:** the same section, "…all sky colour from".
- **The offline floors:** Render panel → tier row, "raises, never lowers: …".

## Tests and controls
Against a 1280×720 render boxed down to 320×180 (lower is better; the control is each frame as its own reference):

| Effect | New | Control |
|---|---|---|
| Bloom | 0.073 | 0.498 |
| Halation | 0.136 | 0.782 |
| Anamorphic streak | 0.051 | 1.439 |
| Motion blur | 0.094 | 0.355 |
| Look stage | 0.029 | 0.310 |

- **Fog from the sky:** a ridge 300 m out differs 0.058 from the sky behind it, against 0.865 for the constant colour. A wall 20 m out moves 0.043, against 3.0 when the sky's colour is used at every distance. The aurora reaches fogged geometry.
- **Floors:** the march goes from 12 to 32 steps offline. The sky's worst error at 4K density drops from 0.171 to 0.002.
- **Re-baselined:** three small-frame bloom tests pin their reference height; `test_terrain_fog_gpu` declares the new bindings.

## Suites
- **CPU** (`63dbe0a0`; later commits are documentation only): 3,706 cases, 3,686 passed, 1 failed, 19 skipped, exit 8. The failure is the 180 s wall-clock agent test, which passed alone in 123 s and is load-sensitive.
- **GPU:**
  - the first full run had 5 failures, the re-baselined tests;
  - every touched family has passed since;
  - **the full run on the final code has not run.** Use `AVGEN_AGENT=render tools/gpu-lock.sh build/render-gpu-full.sh full-3` in the worktree.

## Changes to existing scenes
- **ADR-917:** every bloom scene looks different at chain heights other than 720. Glows keep their frame fraction. The 29 scenes that also use the streak, halation, blur or look stage are listed in the ADR.
- **ADR-918:** none; the default is 0.
- **ADR-919:** every offline render changes where a floor applies: 51 of the 58 shipped files with a fog march, grazing-angle textures, and procedural-sky backdrops.

## How GV3 should use this
The same values serve the 960×540 ×2 preview and the 3840×2160 ×2 final:
- `post/referenceHeight` **1080**;
- `post/motionBlur/maxRadius` **60** (was 40);
- `post/motionBlur/samples` **32** (not scaled by ADR-917);
- `scene/fogSky` **1.0**;
- `scene/fogSkyDistance` **0** (automatic: 208–500 m over GV3's density arc).

**Keep** `bloom/levels` 6, `anamorphic/stretch` 10.386 and `motionBlur/tileSize` 20. **Don't** apply the audit's 8 levels, stretch ×2 or tile 40, which would now scale twice. In scene JSON the keys are `post.referenceHeight`, `environment.fogSky` and `environment.fogSkyDistance`.

**Measured on GV3:**
- **The preview is unchanged:** 0 of 518,400 pixels differ.
- **The final now matches the preview:** mean difference 2.20 → 1.36 (8-bit), 99th percentile 37.7 → 21.0.
- **Fog from the sky:** s14's far third 67.1 → 81.1, near third 53.7 → 58.1.
- **Cost:** 2 s of s14 at 4K ×2 take about 41–42 s, about 0.34 s a frame, so **the film is roughly 77–80 minutes**.
- **The render log should say** "post chain 4320 lines at reference height 1080 … box-filtered 2 octave(s) down", "texture filtering at 16x" and "sky's cube from 256 to 1024".

This makes these `render-post.md` recommendations unnecessary: bloom levels 8, stretch ×2, tile 40,
the resolution-dependent look, `volumeSteps` 32 at offline, fixed 8× anisotropy, gaps 6 and 7, and
gap 3's radii.

## Defects found, not fixed
- **A silent empty render:** `avgen --render` renders bare sky and exits 0 when the scene fails to load (e.g. "cannot open entity profile"). **Check the log of every GV3 render for scene-load warnings.**
- GV3's bloom threshold of 1.761 means s27's saucer underside does not glow; that is a look choice for gv3-look.
- Water's fixed 1080-row reference (ADR-915) is separate from `post/referenceHeight`. They coincide for GV3.
