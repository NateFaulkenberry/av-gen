# Phase 3 integration, gv3-int: the merged film, re-validated, previewed and evaluated

The integration stream's log (briefs.md, "Phase 3 integration: gv3-int"). Each round records what
changed, what was measured and what was decided. Worktree `av-gen-gv3-int`, branch `gv3/integrate`,
from `gv3/production` `b74c3429` (all four Phase 3 streams merged: cast, cut, look, world). Engine:
the shared build `av-gen-engine-3` (main `876a11e2`).

Evidence is in `~/Desktop/av-gen-review/18-glowmere-valley-3/revision/integrate/`. The Critic
session is `gv3-revision`, track `film` for whole films; clips go on per-category tracks.

## How a round is made

Nothing generated is committed. Each round is a snapshot under `build/gv3/int/<round>/`:

1. `python3 tools/make_glowmere_valley_3.py && python3 tools/gv3/ufo.py --no-trace` in the worktree;
2. `build/gv3/int/tools/snap.py <round>` copies the project, scene, intent tables and cut into
   `build/gv3/int/<round>/examples/world/`, a mirror of the repository's layout (the scene names
   `../materials/...` and `../../assets/...`, so `examples/<dir>` and `assets` are links), makes the
   song's path absolute, and restores the tracked first-pass copies (`git checkout --`);
3. every check and render reads that snapshot: `avgen --audit-routes`, `avgen_cast_trace --camera`
   (226 s, 60 fps, 20 Hz), `avgen_character_quality`, `tools/camera_stability.py`,
   `tools/gv3/framing.py --scene`, `tools/gv3/world.py --project --trace`, and the render
   (`build/gv3/int/tools/render.sh`, through `tools/gpu-lock.sh`, logging any other test binary or
   render that runs beside it).

`world.py` gained `--project` and `framing.py` `--scene` for this; both default to `examples/world`
as before.

## Round 1: the merged film as the four streams left it

**Generated** (`gv3/production` `b74c3429` on engine-3), log `build/gv3/int/gen-r1.log`:
- 73 shots, 225.50 s; the recorded Director cut (`song_cut.json`). The live Director now cuts 8
  spans differently (53.1, 55.1, 65.1, 66.1, 68.1, 89.1, 90.1, 107.1: gv3-cut's seven plus 107.1),
  from Song Mode's subject term (gv3-cut's finding: the cast moved, the music did not). The recorded
  cut stays, as gv3-cut decided.
- The reactivity: 59 of 96 proposed routes installed, 1 added; the generator's audit 93/93 routes,
  42/42 tracks live.
- The world closed (the north head and falls, the south sill, the river's flow pinned at 0.4116 m/s).
- `ufo.py --no-trace`: the plan compiles with nothing blocked; nominal moments E1 beam 13.865,
  E2 cross 26.636, E3 lift 66.951, E4 lift 103.871, E5 beam 170.339, lift 172.756, depart 177.699.

**The route audit on the project that renders** (`avgen --audit-routes`, `build/gv3/int/r1/audit.json`):
93/93 routes, 128/128 tracks (the camera tracks included), 7/7 effect defaults and 2/2 effects live.
**0 unknown parameters.** The load's warnings are the known two: `lodCount` (a setting the source's
procedural carries, 3 lines) and `groundGlow` having no effect under the authored ground program (an
engine defect on the open list). The nav grid is trusted: "971 sampled walks agreed with the world
exactly"; 5 pieces, 48% of walkable cells unreachable from the largest (gv3-world W6's figures).
