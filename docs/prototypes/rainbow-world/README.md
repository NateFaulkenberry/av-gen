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
