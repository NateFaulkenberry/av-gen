# Cymatic World: colour, lighting and resonance art pass (report)

The brief is [03-brief-color-lighting-art-pass.md](03-brief-color-lighting-art-pass.md). This report follows its §14. The pass worked through Passes A to F in order, with one commit per pass on `proto/cymatic-split`. Two owner directions arrived during the pass and are folded in: the track changed to Hyperborea, and the scene was to have no crystals and no walls.

**Test audio.** Every render, still, clip and profile in this report uses Hyperborea: `assets/audio/hyperborea.wav` in the repo (gitignored), 203.7 s long. No All You Got audio was used. The timestamps were picked from a loudness and onset scan of the track:

| Condition | Time | What the music is doing |
|---|---|---|
| quiet | 10 s | intro, RMS 0.07 |
| dense | 48 s | full section, RMS 0.44 |
| sustained | 116 s | held section, RMS 0.34, few onsets |
| transient | 134 s | onset-heavy, 8 onsets in 2 s |
| 30 s clip | 50-80 s | dense, then a break (60-75 s), then the return |
| reframe checks | 10, 48, 60, 116, 134, 140 s | |

**Media.** Everything is in `~/Desktop/av-gen-review/58-cymatic-world/colour-pass/`:

| Folder | Contents |
|---|---|
| `a-diagnosis/` | final, albedo, light, emission, normal and depth images of the scene before the pass, plus `diag-sheet.png`; Hyperborea 116 s |
| `b-palettes/` | `palette-comparison-A-B-C-hyperborea-10s-116s.png` |
| `f-final/` | the before/after sheet; quiet, sustained, transient and dense stills at 1080p; `cymatic-world-hyperborea-50-80s.mp4` (1080p, with audio) |
| `g-reframe/` | the before/after reframe at six timestamps; captures at every live quality level |
| `superseded-not-hyperborea/` | earlier Trench and Night Shift working images, kept for the record. The two that used All You Got were deleted. |

## 1. What was wrong with the original

The diagnosis was taken from the scene's own buffers at Hyperborea 116 s, using `CW_DEBUG` program modes added in Pass A. Luma is mean and standard deviation on 0-255.

| Buffer | Luma | Finding |
|---|---|---|
| albedo | 19 (sd 13) | Near black and a single hue. Stone and grit had the same colour, so there was only one material. |
| light alone | 172 (sd 85) | Strong and raking, which was the one good thing. But it was white and single-hued, and the shadows were empty black: there was no fill, sky or rim. |
| emission | 6 (sd 20) | One hue (cyan), sparse and dim. It drew the nodal line but nothing else. |
| final | 41 (sd 37), saturation 76 | A desaturated grey-lavender. The light's colour never reached the image, and nothing in it varied with the resonance except the cyan line. |

Three other problems:
- **Distance.** The depth buffer shows the far walls getting no distance cue. They read as a flat black mass with a hard edge.
- **Blur.** Camera motion blur (`post/motionBlur/amount`) softened the whole frame. Every palette render was mush until Pass B turned it off.
- **Audio.** Only one route drove colour: `level` scaled the counter light. The field moved and the colour did not.

## 2. Palette

Three palettes were tested on the real geometry at quiet (10 s) and sustained (116 s): `b-palettes/palette-comparison-A-B-C-hyperborea-10s-116s.png`. Each one sets stone, grit, key, fill, rim, sky, haze, line, hollow and sheen colours. Hue rotation and rainbow cycling were ruled out from the start.

- **A, ultraviolet** (violet stone, indigo grit, blue key, violet fill). Rich, but monotone: the brief's "uniformly purple scene" failure.
- **B, plasma: chosen.**
  - Deep navy satin stone (`#081236`) and teal grit (`#0b4458`).
  - A raking cyan key (`#62ecff`) against a fuchsia fill (`#c13bff`) from the opposite quarter, with a pink rim.
  - Pale cyan-white nodal lines and a hot magenta (`#ff3fd2`) glow in the hollows.
  - It reads as one world with three meaningful hues: cyan for lit relief, navy for shadowed stone, and fuchsia for resonance.
- **C, spectral heat** (an orange rim and red-orange hollows). The warm accents read as fire and embers, which competes with the cool resonance idea.

`CW_PALETTE=A|C` in `tools/cymatic_world/build.py` brings back either alternative.

## 3. Lighting and materials

The ground is an unlit mesh, and all of its light is painted in its material program, `cwGround`. The pre-pass profiling showed the lit path costing about 7 ms at 1080p. No new scene lights were added. The two scene lights that did exist only lit the spires, so they went when the spires did (§10).

**Lighting** (Pass C):
- **Key.** A raking cyan key from low on one side: a saturated N·L.
- **Fill.** A complementary fuchsia fill from the opposite quarter, as a narrow grazing lobe (N·L⁴). The two flanks of a ridge take two colours.
- **Sky.** A hemisphere term (0.5 + 0.5·N.y), so shadows keep detail.
- **Rim.** A pink Fresnel rim, Fresnel³.
- **Haze.** Distance haze on `cameraDistance`, toward the haze colour (35-280 m to 0.7). With the new camera it only deepens the far side of the frame.

**Materials** (Passes C and F):
- **Two materials, from the field's own sand channel.** Scoured stone is navy satin and carries the key's sheen (key lobe^12 on stone only). Settled grit is matte teal and lies on the ridges.
- **Emission that follows the resonance.**
  1. The nodal line: crest × amplitude, pale cyan.
  2. A fuchsia glow only where three things are true: amplitude is high (smoothstep 0.8-2.4), the surface is stone, and it is on the inner wall of a cell (a band of the sand field, 0.035-0.08).
  - So the glow's edges follow the cell walls. Pass F tightened it after the coordinator noted that the earlier version read as blobs on flat ground.

**Cost.** The program finished Pass E at 48 ops (the cap) and cost 8.4 ms more than the pre-pass 25-op program. Every "scalar × colour" pair (a constant plus a multiply) became one `ramp` op, black → colour/2 → colour, which is exactly t·colour on [0, 1]. The stone-to-grit mix also became one ramp. That gives 40 ops with identical output (maximum pixel difference 1/255 against the 48-op render). Routes that scaled a colour now scale the ramp's `constant2` and `constant3` together.

## 4. Audio-to-colour and audio-to-emission

Pass D. Every route scales a term that is already spatially localised by the field, so nothing pulses the whole frame. Colour and brightness move on separate routes and separate time scales.

| Layer | Signal | Target | Envelope | What you see |
|---|---|---|---|---|
| persistent | `response.intensity` (the song's build) | grit **hue and value**: teal `#0b4458` toward `#0f6b82` | attack 4 s, decay 9 s | The ridges' material shifts colour as a section builds. A colour change, not a brightness pulse. |
| persistent | `response.sustain` | hollow glow | ×(1 + 1.2 s), 1.5 s / 4 s | Held sound fills the ringing cells with fuchsia. |
| transient | `response.kickEnv` | hollow glow | ×(1 + 1.6 s), 0 / 380 ms | Kicks flare the cells that already ring. |
| transient | `response.snareEnv` | nodal line | ×(1 + 0.9 s), 0 / 260 ms | Snares brighten the lines. |
| transient | `response.hatEnv` | stone sheen | ×(1 + 0.5 s), 0 / 180 ms | Hats glint the satin highlights only. |
| transient | `response.flux` | fuchsia fill | ×(1 + 0.8 s), 300 ms / 1.5 s | Spectral change lifts the counter light on the far flanks. |
| live | CC 8 `control.light` | fill and line | | The performer's light. |

The field's own routes are unchanged: intensity → modes, and sustain → memory. Because the field places the emission, a louder passage does not just get brighter. It rings more cells, so more walls glow, and the figure itself carries the response.

**Quiet passages.** At quiet (10 s) the image is still fully coloured: cyan relief, navy stone, and fuchsia in the few cells that ring. Saturation is 207 against the baseline's 64. The routes only add on top of that floor.

## 5. HDR, bloom and tone mapping

Pass E. The before/after values:

| Setting | Before | After | Why |
|---|---|---|---|
| `post/tonemap/chroma-retention` (AgX) | default | 0.9 | The hottest nodal lines keep their cyan instead of clipping to white. This was the largest Pass E effect. |
| `post/bloom/threshold` | 1.0 | 1.2 | Only genuinely bright features bloom. |
| `post/bloom/intensity` | 0.18 | 0.22 | |
| `post/bloom/radius` | default | 0.6 | Tight, so neighbouring cells stay separate. |
| `post/motionBlur/amount` | default | 0 | It was blurring the whole frame. |
| `post/dof/physical` | default | off | |

The line and sheen intensities were eased so that the brightest features sit just above the bloom threshold rather than far past it. Exposure (+0.2), contrast (1.22), vignette (0.45) and grain (0.012) are unchanged. Comparing bloom settings made little difference beyond this; chroma retention mattered most.

## 6-7. Screenshots

- **Before/after**, same timestamps, both on Hyperborea: `f-final/before-after-hyperborea-quiet10-sustained116-transient134-dense48.png`.
  - Before is the scene at `1a2342ff`, the terrain-only cut before the pass.
  - After is the final scene: the colour pass plus the reframe.

| Condition | Before luma (sd) / saturation | After luma (sd) / saturation |
|---|---|---|
| quiet 10 s | 27 (32) / 64 | 31 (39) / 207 |
| sustained 116 s | 41 (37) / 76 | 57 (57) / 184 |
| transient 134 s | 38 (37) / 74 | 50 (52) / 191 |
| dense 48 s | 31 (39) / 65 | 57 (58) / 183 |

- **Quiet, sustained, transient and dense**, 1080p: `f-final/after-*-hyperborea-*.png` and `after-conditions-sheet.png`.
- **The 30 s clip, Hyperborea 50-80 s**: `f-final/cymatic-world-hyperborea-50-80s.mp4`. It covers the dense section, the break at 60-75 s where the field and its glow relax, and the return.

## 8. Performance

Measured with `tools/live_scene_profile.py` at 1080p and a 30 fps target, Hyperborea 116 s, on the same machine. "Before" is `1a2342ff`.

| Run | Before | After (final) |
|---|---|---|
| headless, Ultra, 1920×1080: GPU median | 27.5 ms | 32.1 ms |
| headless, Ultra: CPU work | 2.1 ms | 0.7 ms |
| live, High (1632×918, scale 0.85): GPU median | 23.3 ms | 26.4 ms |
| live, High: CPU work | 4.8 ms | 1.3 ms |
| live, High: deadline misses | 0 | 2 (1.3%) |

Live 1080p at every quality level (final, 134 s):

| Quality | GPU median | Deadline misses |
|---|---|---|
| Emergency | 17.5 ms | 2.6% |
| Low | 16.1 ms | 1.3% |
| Medium | 21.6 ms | 0.7% |
| High | 26.3 ms | 2.0% |
| Ultra | 32.8 ms | 16% |
| **Auto → High** | **26.0 ms** | **0%** |

Auto holds 30 fps at High.

**The regression and why.** It is about +3 ms at High and +4.5 ms at Ultra, all in the ground's fragment program (the `geometry/opaque` scene pass). There are three causes:
1. The program grew from 25 to 40 ops. The interpreter costs about 0.36 ms per op at High; it was +8.4 ms at 48 ops before the `ramp` rewrite.
2. Removing the spires and lights saved their (small) cost.
3. The new camera fills the whole frame with ground. Previously, wall and sky pixels ran a cheaper path.

No dynamic lights, full-screen passes or field evaluations were added. The field is still evaluated once per pixel, for the shading channel.

## 9. What helped most, what is left, and the next experiment

**What helped most, in order:**
1. **Painting a complementary light in the program** (Pass C). The raking cyan key and fuchsia fill turned a grey relief into a coloured, three-dimensional surface. This is the single largest change.
2. **The reframe into the ground** (owner direction). The frame is now all figure: no wall band, no sky, no dead space.
3. **Emission placed by the field** (Passes C, D, F). The fuchsia only on the inner walls of hard-ringing cells is what makes the resonance itself read as luminous.
4. **Motion blur off** (Pass B). It was a free sharpness gain over the whole frame.
5. **AgX chroma retention 0.9** (Pass E). Hot lines stay cyan.

**Remaining shortcomings:**
- **Sawtooth aliasing on crater rims** where the relief's crisp crest line is sampled by the mesh (130k vertices). Visible in the dense frames. Softening the crest (`reliefLineSharpness` 0.6 → 0.45) did not visibly help and changes the shape, so it was reverted. The likely fix is a denser mesh in the inner 40 m, or a crest term in the shading channel in place of the geometry.
- **The foreground is soft** at the new camera distance, because the mesh is coarser than one pixel there. The same mesh-density fix would address it.
- **Cost.** About 3 ms over the baseline at High. A compiled, non-interpreted path for this one program would recover most of it, but that is engine work, outside this pass.
- **Live captures are dimmer than headless renders at the same timestamp.** The live run follows the audio from its start position, so its responses had less time to build. Worth a side-by-side check in the app.

**The next most valuable experiment:** let the fuchsia hollow glow **persist and fade per cell**, giving the glow a memory of where the music has been. The field already has a memory channel (`sustain → memory`). Driving the hollow emission from it would let a quiet passage show the ghost of the last loud one, which is exactly the kind of persistent-versus-transient layering the brief asks for.

## Doc edits waiting for owner approval (not made)

- **02-report.md and PROGRESS.md:**
  - the terrain-only scene;
  - the Crystals and Creature cut;
  - this colour pass, with a link to this report;
  - the reframe, and the spires and lights removed.
- **docs/resonance-field.md:** drop the druse-coupling example. It has no scene now.
- **ADR-1402:** note that the probes and source drive are kept, tested, and currently have no scene user.
- **README / examples rows:** the Cymatic World description, which the generator already rewrites into `examples/index.json`.
