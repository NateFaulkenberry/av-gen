# Rainbow World 11: Stage 7 report (AV Gen integration)

Stage 7 of the brief makes the scene a real AV Gen example: built from the scripts in the repository, listed in the
Examples menu, and tested on the rendered output.

Review images are in `~/Desktop/av-gen-review/74-rainbow-world/stage7/`:

| File | What it shows |
|---|---|
| `01-blender-vs-avgen-great-tree.png` | The Great Tree: Blender preview, AV Gen before, AV Gen after, and three world frames |
| `02-blender-vs-avgen-tree-family.png` | Great Tree, Weeper and Blossom Puff, Blender against AV Gen at the same camera |
| `clip-feline-26-41s.mp4`, `03-feline-clip-sheet.png` | 15 s with Feline Footwear |
| `04-five-looks-fixed.png` | The five CC 9 looks after the Stage 6 note |
| `05-review-examples-frame.png` | `tools/review_examples.py`'s frame |

## 1. In the repository

| Path | What | In git |
|---|---|---|
| `examples/rainbow-world/build.py` | The build. It runs every Blender step from the scripts: adapted rocks, engine assets, the three other trees, then the world. A full build takes 90 s; `--scene-only` reuses the assets. | yes |
| `examples/rainbow-world/rainbow-world.json` | The live project: no audio file; the ride, the five looks, MIDI and audio routes | yes (generated, deterministic) |
| `examples/rainbow-world/scene/` | The generated scene: 91 nodes, 48 band chunks and the asset GLBs, 121 MB | no (`.gitignore`) |
| `build/rainbow-world/` | Intermediate work | no |
| `examples/index.json` | "Rainbow World": `live: true`, category **Worlds**, every MIDI mapping, and a `generate` line | yes |

**Reproducible.** A from-scratch build in a clean `build/rainbow-world/` produced a scene JSON identical to the
iterative Stage 6 build's (`==` on the parsed documents), and the same GLB set.

**Inputs.** Blender 5.x, and the six CC0 Poly Haven rocks already in `assets/terrain/rocks/`
(`tools/fetch_polyhaven.py`). Both are listed in `assets/rainbow-world.manifest.json`. No third-party binary and no
audio is committed.

**MIDI** (the index entry and the generator's header say the same):

| CC | Control |
|---|---|
| 1 | Rainbow glow |
| 2 | Lens: fisheye to rectilinear |
| 5 | Haze |
| 6 | Light |
| 7 | Colour |
| 9 | Look: Bright Noon, Golden Hour, After the Storm, Lavender Dusk, Prism |
| 10 | Look lock |

Every control rests at "as built", so with no controller the scene is the authored one. Unlocked, the look moves on
at phrase boundaries or on a big change in the music.

## 2. Blender against AV Gen

The look was built in AV Gen from Stage 3 on, so the comparison is between the Blender previews of the hero assets
and the same assets in the engine, at the same camera (`01`, `02`).

| Discrepancy | Fix |
|---|---|
| The Great Tree's pads read cooler and darker than in Cycles: blue-green against Blender's yellow-green lit tops | Canopy ramp lifted and warmed (`#2A6034` to `#CCE85A`). The lit tops now come out yellow-green (`01`, third panel). |
| Leaf-card edges are harder: alpha-tested, not blended | Kept. It is the engine's foliage path and reads as painted at ride distance. |
| The tree's shadow is its wood and canopy core, not every card | Kept. The cards are out of the shadow maps (ADR-1545), and the core carries the canopy's mass. |
| The Weeper and the Blossom Puff differ in colour | Intended. The Blender previews predate the lime and pink program colours. |

## 3. Tested on the rendered output

- **`tools/review_examples.py --only "Rainbow World"`: ok, exit 0.**
- **`tools/check_project_integrity.py`: 0 problems, exit 0.**
- **`[examples]` tests: 12 cases pass.** They cover the live index and switcher.
- **Seek equals play.** Frames at 100.5 s and 101.5 s rendered alone, and the same frames inside a continuous
  100-102 s render, are identical: maximum pixel difference 0.
  - The look selector is the exception (ADR-1420's shared palette system). It is performance state, like MIDI: a
    seek lands on the selected look, and a play may have advanced on phrase boundaries.
- **Audio.**
  - **Profiles.** I ran the live profile with each of the three test tracks (no audio file in the scene; each track
    loaded with `--audio`, Ultra 1080p, 20 s from 120 s):

    | Track | p50 | p95 | max |
    |---|---|---|---|
    | Feline Footwear | 27.8 | 34.3 | 39.9 |
    | Trench | 27.7 | 34.3 | 40.4 |
    | Hyperborea | 27.7 | 34.5 | 41.5 |
    | Feline, with CC 9 looks changing every 5 s | 27.1 | 34.4 | 42.5 |

    The audio costs nothing measurable, and look transitions cost nothing measurable. A step timeline stood in for
    the controller, and the sky and meadow colours were measured shifting through the transitions.
  - **Reaction.** With Feline, the arches' pulse first measured ±3%, lost in the camera's own motion. The held-sound
    and kick amounts are now doubled, and a kick also flashes the bloom. The bows' brightness now steps visibly on
    kicks: the largest frame-to-frame step went from 1.6 to 4.1, and the range from 9.6% to 11.9%.
- **The Stage 6 look note.**
  - Golden Hour and Lavender Dusk now change the light, not the grass: a cool violet fill and ambient, and blue-green
    meadow ramps.
  - Measured on the meadow:

    | Look | Hue | Saturation |
    |---|---|---|
    | Bright Noon (reference) | 0.22-0.25 | 0.58-0.60 |
    | Golden Hour, before | 0.12 (mustard) | 0.68-0.77 |
    | Golden Hour, now | 0.27-0.28 | 0.50-0.62 |
    | Lavender Dusk, before | 0.13-0.19 (khaki) | 0.34-0.37 |
    | Lavender Dusk, first pass | 0.44-0.46 (teal) | 0.41-0.47 |

    After that first pass, Dusk's ramp was pulled back toward green.
  - The rainbow fringe at the frame's left edge was the foot of a far arch poking out of a rim. The arches are now
    scaled to 0.82, so the feet land inside the face.

## 4. Open for Stage 8

- **GPU.** P95 is 34 ms against the 29.3 ms budget; the median meets it.
- **Golden Hour.** Its sunlit meadow is warm; that is intended.
- **Lavender Dusk.** It should be re-checked after its final ramp.
