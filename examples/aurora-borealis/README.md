# Aurora Borealis

A live scene: a slow flight down a winding mirror river through a valley of faceted indigo mountains at dusk, under a
sky of layered aurora ribbons. It follows the Feline Footwear drum, bass and synth stems (or, live, any input whose
channels are given the roles `drums`, `bass` and `synth`), and the mix alone otherwise. Examples > Live > Sonic VFX > Aurora
Borealis.

## Build it

```sh
python3 examples/aurora-borealis/build.py
```

Pure Python 3 (no Blender, no downloads), a few seconds. It writes:

| Output | Committed? |
|---|---|
| `examples/aurora-borealis/scene/aurora-borealis.scene.json` and its four faceted GLBs (about 2 MB) | no (gitignored) |
| `examples/aurora-borealis/aurora-borealis.json`, the live project | yes: a pure function of the scripts |

The project names `assets/audio/feline-footwear.wav` and the three stems in `assets/audio/stems/`. Those are local and
never committed. Without them the scene still opens and plays live input.

## The scripts

| File | What it holds |
|---|---|
| `build.py` | the camera loop (a constant-speed timeline down the river, pitched up so the sky takes the frame) and the scene and project documents |
| `world.py` | the land: the river's closed S-bend centreline, the valley band whose rows follow the river, the hero peaks, the three outer ranges |
| `look.py` | the art direction: palette states, the land's unlit painter's program, sky, aurora, water, post |
| `music.py` | the instrument: the stems by role, the drum feeds, the conditioned signals and the routes |

## Controls

CC 21, 22 and 23 scale the drums', bass's and synth's contribution. Each rests at full when the project loads and picks
up from there (ADR-1612).

See `docs/prototypes/aurora-borealis/` for the brief, the research, the plan and the progress log.
