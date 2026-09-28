# Glowmere Valley 3: a music video to "Rebuild"

A 3:45 animated music video set in Glowmere Valley. It was directed, built, rendered and revised
inside AV Gen, and it is the production record the brief asks for ([00-brief.md](00-brief.md) §27).

| | |
|---|---|
| project | `examples/world/glowmere-valley-3.json` (+ `.scene.json`) |
| derived from | `examples/world/glowmere-valley-2-multicam.{json,scene.json}`, never modified |
| generator | `tools/make_glowmere_valley_3.py`, assembling `tools/gv3/` (music, directives, shots, look, cast, world) |
| audio | "Rebuild", `~/Desktop/Rebuild.mp3` (not in the repository) |
| render | `avgen --project examples/world/glowmere-valley-3.json --render <out.mov>` |

## Running it

What the repository cannot carry, because it is licensed or copyrighted:

- **The song**, at `~/Desktop/Rebuild.mp3`. The project refers to it by a relative path from
  `examples/world/`, so it resolves only with the repository checked out at the owner's depth
  (`~/Documents/GitHub/<repo>`). Anywhere else, repoint `assets.audio.path` in the project.
- **The characters and animals**: `assets/aliens/alien-{scout,diver,elder,ranger,pilot}.glb` and
  `assets/farm/{horse,cow,bull}.glb`, the commercial packs Glowmere Valley 2 also needs. A worktree
  gets them from main's `tools/link-worktree-assets.sh <worktree>`, run from main.
- **The plants and trees**: `assets/quaternius/` (CC0, gitignored as a bulk download like the other
  asset packs; `assets/glowmere.manifest.json` names what the scene places).

Without them the project still opens: each missing file is a load warning, and the song's absence
means silence.

### The committed project is r7b, and the generator reproduces it (QA pass, 2026-09-28)

The two files in `examples/world/` are **r7b**, the revision the owner reviewed (the review folder's
`project-for-review/`). They were produced, not copied: on the QA branch,

```
cmake --build build/release --target avgen_world_preview avgen_cast_trace   # the generator probes the ground
python3 tools/make_glowmere_valley_3.py && python3 tools/gv3/ufo.py --no-trace
```

writes a scene byte-identical to r7b's and a project whose only difference is the song's path
(`../../../../../Desktop/Rebuild.mp3`, the Glowmere family's convention, where the review copy had
the absolute `/Users/<owner>/Desktop/Rebuild.mp3`). It also rewrites `03-directives.md` and
`04-shot-plan.md`, identical to r7b's. Checked with `avgen --project ... --audit-routes`: 0 errors,
routes 105/105, tracks 130/130, effects 18/18 live, 73 shots, 225.5 s, and an audit report identical
to the review bundle's.

- r7b is the **preview** mode. The 4K final (`--final --final-trace <cast.json>`) needs a whole-film
  cast trace; r7b's is 6.4 MB of `avgen_cast_trace` output (`build/gv3/int/r7b/cast.json` in the gv3-int
  worktree), a build artifact regenerated from the project by `avgen_cast_trace --project <p> --seconds 226 --fps 60 --hz 20 --camera --out <cast.json>` (17 minutes of CPU), so it is not committed.
- The review folder's `bundle/` (an `--export-bundle` of r7b) is not in the repository: its `assets/`
  holds copies of the purchased models and the song.

Open `examples/world/glowmere-valley-3.json` and press play: the film opens on black and cuts in on
the first kick at 0.48 s, so the paused first frame is black by design. Everything else (the world,
the heroes, the saucer, the cut, the look and its modulation) is in the two project files.

## The record

| | |
|---|---|
| [00-brief.md](00-brief.md) | The owner's brief, verbatim |
| [01-music.md](01-music.md) | The music analysis: the grid, the thirteen segments, the motifs, and why AV Gen's own sections were not used |
| [02-art-direction.md](02-art-direction.md) | The concept, the visual arc, colour, and the camera, effects and modulation philosophies |
| [03-directives.md](03-directives.md) | The production's own directives: intent, camera and look, per segment (generated) |
| [04-shot-plan.md](04-shot-plan.md) | Every shot: time, purpose, subject, camera, movement, music, effects, modulation (generated) |
| [05-quality.md](05-quality.md) | How quality is judged, and every finding: issue, severity, fix, validation, state |
| [06-iterations.md](06-iterations.md) | The iteration history |
| [07-technical.md](07-technical.md) | Engine and tooling changes made for the production |
| [08-final-evaluation.md](08-final-evaluation.md) | The end-to-end evaluation, written last |
| [revision/](revision/) | **The second pass, in progress.** The owner's revision brief, the capability audit, the evaluator gate, the engine plan, and [PROGRESS.md](revision/PROGRESS.md), the operational state file to read first |

## How it is made

The film is data, not a recording of a session.

- **The generator** reads the multicam project and writes GV3. The data it reads from:
  - the grid (`music.py`);
  - the directives (`directives.py`);
  - the cut (`shots.py`);
  - the look and its modulation (`look.py`);
  - the saucer's staging (`cast.py`);
  - the world fixes (`world.py`).
- **What it writes is all the engine's own content:**
  - a camera collection with one locked shot per span;
  - timeline keys;
  - modulation routes and sources;
  - a staging scenario;
  - parameters.
- **So the project renders and edits in the app like any other.** The generator is how it was
  authored, not something it depends on.

The loop, for every iteration:

```
python3 tools/make_glowmere_valley_3.py                       # the project and the plan
build/release/tools/avgen_cast_trace --project examples/world/glowmere-valley-3.json \
    --seconds 226 --fps 60 --hz 20 --out build/gv3/cast-vN.json       # where the cast actually goes
tools/gpu-lock.sh build/release/src/avgen --project examples/world/glowmere-valley-3.json \
    --render "$PWD/build/gv3/vN.mov" --size 960x540                     # an ABSOLUTE output path
python3 tools/gv3/review.py build/gv3/vN.mov build/gv3/shots.json build/gv3/review-vN   # sheets, measures
python3 tools/gv3/cuts.py build/gv3/vN.mov build/gv3/shots.json build/gv3/review-vN     # every cut
python3 tools/gv3/framing.py build/gv3/cast-vN.json --video build/gv3/vN.mov           # framing, occlusion
```

A relative `--render` path resolves against the project's folder, not the working directory.

Then look at every sheet, write the findings into [05-quality.md](05-quality.md), change the data,
and go again. Previews render at 960×540 but always at 60 fps, the final frame rate: a 30 fps
simulation is a different film ([07-technical.md](07-technical.md) §7.5).
