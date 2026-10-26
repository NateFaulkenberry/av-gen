# Aurora Borealis

A live scene: a slow flight down a winding mirror river through a valley of faceted indigo mountains at dusk, under a
sky of layered aurora ribbons. Pass 3 plays **Sabertooth**: each stem performs its own part of the frame. Examples >
Live > Sonic VFX > Aurora Borealis.

| Stem | Where | What |
|---|---|---|
| Cloud Diving | sky | the aurora's swells: brightness, height, width, folds, inner detail, and slowly its colour |
| Crash | sky | a fast shooting star (a pool of two) and a brief response: the aurora lifts, the crests flare, the river reflects it |
| Moog | left bank | pale pink glints on a scattering of creases per note; the ring keeps them gently lit |
| Kick | river | a magenta-pink beam racing down the river's own course (ADR-1662), lighting a faceted riverbed and the banks, rippling the mirror |
| Snare | river | the same in pale lilac twin filaments, faster and shorter |
| Toms | right bank and ranges | blue-violet flashes on whole planes, a new subset per note, sized by the hit |
| Tone block | right bank and ranges | small lavender-white crease glints stepping through the bar |
| Bass, HH | none | left unassigned (tested; see docs/prototypes/aurora-borealis/PROGRESS.md) |

The Feline Footwear version is the hidden entry "Sonic VFX - Aurora Borealis (Feline Footwear)"
(`aurora-borealis-feline.json`): the same scene and signals driven from its drums, bass and synth stems.

## Build it

```sh
python3 examples/aurora-borealis/build.py
```

Pure Python 3 (no Blender, no downloads), a few seconds. It writes:

| Output | Committed? |
|---|---|
| `examples/aurora-borealis/scene/aurora-borealis.scene.json`, its faceted GLBs (both banks, three ranges, two riverbeds) | no (gitignored) |
| `examples/aurora-borealis/aurora-borealis.json` (Sabertooth) and `aurora-borealis-feline.json` | yes: pure functions of the scripts |

The audio is local and never committed: `assets/audio/stems/sabertooth/` (the ten Sabertooth files) and, for the
Feline version, `assets/audio/feline-footwear.wav` and its three stems. Without them the scene still opens and plays
live input; keyed live channels (`kick`, `snare`, `toms`, `crash`, `cloud`, `moog`, `toneblock`) drive the same signals.

## The scripts

| File | What it holds |
|---|---|
| `build.py` | the camera loop, the stars' azimuth tracks, and the scene and project documents |
| `world.py` | the land: the river's centreline, the two valley banks, the riverbed, the hero peaks, the outer ranges |
| `look.py` | the art direction: palette, the two banks' programs (with the glints), sky, aurora, stars, the river beams, water, post |
| `music.py` | the instrument: the songs, the stems, the conditioned signals and the routes |

## Controls

CC 21-27 scale Cloud Diving, Crash, Moog, Kick, Snare, Toms and tone block. Each rests at full when the project loads
and picks up from there (ADR-1612).

See `docs/prototypes/aurora-borealis/` for the briefs, the research, the plans and the progress log.
