# Rainbow World

A live AV Gen scene. It is a painterly rainbow world, a closed twisting band of meadow land in an all-round sky,
travelled with a fisheye camera whose gravity turns with the land. Branch `art/rainbow-world`; ADR range 1540-1549.

| Doc | Content |
|---|---|
| [00-brief.md](00-brief.md) | The owner's brief, verbatim |
| [01-clip-analysis.md](01-clip-analysis.md) | `clip.mov` decoded and analysed: lens, rhythm, gravity changes, concealment |
| [02-research.md](02-research.md) | The research dossier with sources, annotations and licensing findings |
| [03-art-direction.md](03-art-direction.md) | Identity, world form (approaches debated), palette and looks, shapes, materials, lighting, motion, audio |
| [04-production-plan.md](04-production-plan.md) | Asset breakdown, licensing, AV Gen technical assessment, workflow, staged plan, uncertainties |
| [05-gate1-experiments.md](05-gate1-experiments.md) | E1 lens, E2 knot world, E3 rainbow look |
| [06-stage2-report.md](06-stage2-report.md) | Stage 2: the isolated prototypes, E4/E5 costs in AV Gen, the five weakest points |
| [07-stage3-report.md](07-stage3-report.md) | Stage 3: the look-dev scene in AV Gen, v1-v7, colour against the references, ADR-1540/1541/1542/1545 |
| [08-stage4-report.md](08-stage4-report.md) | Stage 4: the knot, the ride (gravity follows the land, rim crossings), richness, cost |
| [09-stage5-report.md](09-stage5-report.md) | Stage 5: the faster ride (owner), hanging gardens at the crossings, the cloud sea, clusters, ADR-1546, the GTAO stripes, cost |
| [10-stage6-report.md](10-stage6-report.md) | Stage 6: speed you can feel (streakers, low and fast), cloud form, deeper sky, less haze, ADR-1547 (rainbow shadows), ADR-1548 (leaf thinning), cost |
| [11-stage7-report.md](11-stage7-report.md) | Stage 7: build.py, the committed live project and Worlds index entry, Blender against AV Gen, seek equals play, the three tracks, CC 9 under audio |
| [12-final-report.md](12-final-report.md) | Stage 8 and the final report: what a fresh clone shows, optimisation, the final review, ADR 1540-1548, limitations |
| [13-brief-stem-reactivity.md](13-brief-stem-reactivity.md) | Rainbow World 2: the owner's brief for stem-driven reactivity, world FX and the slow frames (verbatim) |
| [14-stem-analysis.md](14-stem-analysis.md) | Feline Footwear's three stems analysed: files, grid, what each stem plays, the event map, the engine's features |
| [15-research.md](15-research.md) | The research dossier for the reactive pass |
| [16-effect-mapping.md](16-effect-mapping.md) | The effect mapping: three layers, each stem's effects, the hierarchy, determinism and live |
| [17-slow-frames.md](17-slow-frames.md) | The slow frames: diagnosis (not compiles, not contention), the leaf-card fix, what the effects cost |
| [18-final-report.md](18-final-report.md) | Rainbow World 2's final report: deliverables, profiles, limitations |
| [19-brief-color-vfx.md](19-brief-color-vfx.md) | The owner's colour-VFX brief (verbatim): colour, light and material response over geometry; the looks |
| [20-camera-note.md](20-camera-note.md) | The owner's two camera notes (verbatim): gliding, then a plane |
| [21-camera-research.md](21-camera-research.md) | The plane-like camera: springs, coordinated turns, the energy trade, earned rolls; baked, so seek equals play |
| [22-owner-fixes.md](22-owner-fixes.md) to [28-twirl-sky-sun.md](28-twirl-sky-sun.md) | Rainbow World 2's gates 3-8: the owner's verdicts, the modulation inventory, the scan, its fireworks, the twirl, the sky, the clouds, live popping |
| [29-brief-feline-video.md](29-brief-feline-video.md) | The owner's Feline Footwear video brief (verbatim): the offline and live versions, and the song's own video outside the repository |
| [30-feline-structure-and-plan.md](30-feline-structure-and-plan.md) | Feline Footwear's structure map (the synth's swells measured) and the video plan |

## Playing it (Rainbow World 2)

- **Build the scenes first:** `python3 examples/rainbow-world/build.py` (Blender 5.x, about 90 s; `--scene-only` after
  the first full build). It writes `examples/rainbow-world/scene/` (gitignored): the live scene, the offline scene
  and the offline scene's video hooks. Until then both menu entries show "(needs generating)" with this command.
- **Live:** open Examples > Live > Worlds > Rainbow World (`rainbow-world.json`, no audio file), and play any music into the
  live input.
  - On a plain stereo input the world follows the main mix (`response.*`, the beat clock).
  - With a multichannel interface, map channels in the Live panel's Inputs table to sources keyed `drums`, `bass`
    and `synth`. The stem routes switch on and the main-mix fallbacks stand aside (ADR-1570). Unmapped stems keep
    their fallback.
- **Offline:** Examples > Offline > Worlds > Rainbow World (offline) (`rainbow-world-offline.json`). It is the same
  world at full quality, with no live ceilings, playing `assets/audio/feline-footwear.wav` (the mix only, gitignored).
  - It is song-agnostic: it reacts as the live version does, with the latched scans, and bakes no song's timing.
  - A project that names stems (`assets.stems`, keys `drums`, `bass`, `synth`) gets the stem routes.
  - Its scene adds the world distortion (the `distort` macro, 0 at rest). It publishes
    `scene/rainbow-world-offline.video.json`, for a song's own video to bake its scans, camera and looks over it.
  - The Feline Footwear video is such a project. It lives outside the repository, in `~/Desktop/Feline Footwear/`
    (29).

  ```
  build/release/src/avgen --project examples/rainbow-world/rainbow-world-offline.json \
      --render "$PWD/renders/rainbow-world-offline.mp4" --fps 30 --codec h264      # --range 38:58 for a part
  ```
- **Other music offline:** the offline project with `--audio <file>` (or the editor's audio).
- **Re-run the analysis:** `python3 docs/prototypes/rainbow-world/scripts/stem_analysis.py build/rw2/stems`, and
  `avgen --project <project with the stems> --sonic-trace out.csv` for the engine's own stem, beat and envelope
  columns (ADR-1571).
- **MIDI:** unchanged (CC 1 glow, 2 lens, 5 haze, 6 light, 7 colour, 9 look, 10 look lock).

The owner's references (`~/Desktop/rainbow world/`) are not committed, because their source and licence are unknown.
The review media is in `~/Desktop/av-gen-review/74-rainbow-world/`, and scratch is in `build/rw/`.

To regenerate the Gate 1 sheets, run from the worktree root (the paths are relative to `build/rw/`):

```
ffmpeg -i "~/Desktop/rainbow world/clip.mov" -fps_mode passthrough build/rw/clip/all/f_%04d.png
ffprobe -v error -select_streams v -show_entries frame=pts_time -of csv=p=0 "<clip>" > build/rw/clip/pts.txt
python3 docs/prototypes/rainbow-world/scripts/clip_sheet.py build/rw/clip/all build/rw/clip/pts.txt out.png --every 0.25
python3 docs/prototypes/rainbow-world/scripts/clip_motion.py build/rw/clip/all build/rw/clip/pts.txt --step 0.2
/Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup -P docs/prototypes/rainbow-world/blender/exp_fisheye.py -- build/rw/exp
python3 docs/prototypes/rainbow-world/scripts/remap_fisheye.py build/rw/exp/rect_140.png out.png --in-fov 140 --fit diag
/Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup -P docs/prototypes/rainbow-world/blender/exp_knot.py -- build/rw/exp_knot
/Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup -P docs/prototypes/rainbow-world/blender/exp_rainbow.py -- build/rw/exp_rainbow
```
