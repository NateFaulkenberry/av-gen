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

## Playing it (Rainbow World 2)

- **Build the scene first:** `python3 examples/rainbow-world/build.py` (Blender 5.x, about 90 s; `--scene-only` after
  the first full build). It writes `examples/rainbow-world/scene/` (gitignored) and both projects.
- **Live:** open Examples > Live > Rainbow World (`rainbow-world.json`), play any music into the live input.
  - On a plain stereo input the world follows the main mix (`response.*`, the beat clock).
  - With a multichannel interface, map channels in the Live panel's Inputs table to sources keyed `drums`, `bass`
    and `synth`. The stem routes switch on and the main-mix fallbacks stand aside (ADR-1570). Unmapped stems keep
    their fallback.
- **Offline, the Feline Footwear showcase:** `rainbow-world-feline.json` plays `assets/audio/feline-footwear.wav` and
  analyses the three stems in `assets/audio/stems/` (the owner's files, gitignored). Open it in the app, or render:

  ```
  build/release/src/avgen --project examples/rainbow-world/rainbow-world-feline.json \
      --render "$PWD/renders/rainbow-world-feline.mp4" --fps 30 --codec h264      # --range 38:58 for a part
  ```

  Without the stem files it still plays, on the fallbacks; each missing file is a project warning.
- **Other music offline:** the live project with `--audio <file>` (or the editor's audio) runs on the fallbacks.
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
