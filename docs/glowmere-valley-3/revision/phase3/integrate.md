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

**Re-validated across streams** (the r1 trace: `avgen_cast_trace --camera`, 226 s at 60 fps, 20 Hz, 31 min on a
machine at load 100-160; `build/gv3/int/tools/checks.sh r1`). The cast is gv3-world's closed-world trace of the
merged cast exactly (`cast-e3final.json`: every framing and on-screen moment identical): cameras do not steer it,
and gv3-look's changes do not either.

| Check | Result |
|---|---|
| E1-E5 (`ufo.py --trace-file`) | all five play: E1 beam 13.900, sweep 15.033; E2 cross 26.650; E3 lift 66.967, **bull-21** taken 71.883; E4 lift 103.883, cow-23 and cow-12 taken 111.917; E5 beam 170.350, lift 172.783, horse-11 taken 177.717; each craft held within 0.35-0.42 m |
| Watchers (`ufo-beats.json`) | E4: rook 111.20-112.60 (27 m), sage 113.05-117.30, ember 103.65-107.60 and 111.40-114.25. E5: rook 174.00-176.65, ember 171.25-176.75, **vane 172.30-180.20**, so 95.1 "Vane sees it" (174.02-174.94) holds |
| Follow stability (`tools/camera_stability.py`) | **19 of 22** pass (gv3-cut, source world: 21 of 22). Fail: s35 59.1 (eye height HF 2.41 cm), s54 95.3 (pitch 1.31, ADR-913's exception, and now yaw 0.128), s63 103.1 (travel 1.49) |
| Framing (`framing.py`) | every aimed or followed subject in frame 100% of its shot. It reports the aim node only: 59.1 aims at the scout and rides Sage, and **Sage is out of frame for the whole shot** (the search harness: head at x +0.83, chest below the frame). 38.1's Tide is in frame |
| Set-piece moments (`onscreen.py`) | every moment the cut means to show is on screen; E3's bull-21 in frame in 37.1 and 38.1 (the rigs aim live at the scout, so the 15 m move of E3's column needs no re-framing) |
| ADR-910 (`avgen_character_quality`) | aliens: longest stands rook 4.4, tide 4.3, sage 7.8, ember 7.2 s, vane 8.9 s (watching E5, allowed); reversals rook 3 (165.9, 173.0, 183.0 s at (-105..-111, -29..-40), new with the closed world), ember 4 (199-223 s on the west bank: navfix's ADR-932/936 case), sage 1, vane 1. Animals 0 reversals, 4 turns over 90°, 5.2 s on steep ground, 0 s uphill |
| World (`world.py --check --trace`) | **FAIL**: survey, filmed ground, falls, seams all hold, but gv3-cut's new cameras see the edge: 17 views with an open end (s44 77.1 7/7 views, up to 17 of 96 columns; s03, s24, s26, s65 one column each) and the river's mouth in 91 views of 12 shots (up to 18 columns in 71.1's 85 mm) |
| Moon (`build/gv3/int/tools/moon.py`) | the crisp moon in 7 shots; a second, soft disc 32.5° away in 11 (below) |

**The render and the sheets** (`build/gv3/int/r1/review/`, `tools/gv3/review.py`; copied to the review folder):
- **Two moons.** `shaders/skybox.wgsl` draws a crisp moon at the sky's sun direction; the visible procedural sky
  (`sky_background.wgsl`) is looked up through `envRotate`, i.e. `env/rotation`, -0.568 rad inherited from GV2,
  so its own soft disc sits 32.5° away. Both show in 17.1, 59.1 and 93.1 (and briefly 11.1); the soft one alone
  in 47.1, 65.1, 94.1, 95.3 (a 123 px disc behind the rising horse), 97.1, 101.1 and 109.1. An engine defect (the crisp disc ignores the
  rotation; GV2 multicam has it too), and the crisp disc's colour and size are hard-coded: not in the UI.
- **E2's moon** is the crisp disc in the top-left corner of a 24 mm lens, stretched by the lens and greyed by the
  pull-back's exposure (a fixed (1.8, 2.1, 2.4) under ev -1.85).
- **95.3** (s54, the E5 hero moment): the horse is a pale ghost seen through the beam's particle cloud; no gold,
  12.7% of the frame clipped. The beam's `audio.rms -> emissive` link (+1.5 on a set-piece base of 0.7) triples
  it in the loud riser, and the Critic sees no response from any of its four links.
- **Composition, the scatter the traces do not hold:** 19.1's push ends inside a fern (the ring-wave shot);
  25.1's lantern behind a big leaf; 29.1's canopy hides the elder (the brief's item); 31.1's spire lost among the
  lit hillside's mushrooms (the brief's item); 73.1 ends with a leaf over the lower half; tree trunks cross 104.1
  and 107.1 mid-arc; 38.1 is half a dark rock (the first pass's "s13 at 70 s" is gone: its span is now 38.1).
- **The world edge at full size:** 77.1 looks down the valley's axis from 28 m up the new north head, and the
  south end is a flat shelf with the river's mouth a rectangular, vertical-walled slot dead centre: it reads as
  the edge of the world. 106.1 shows the mouth as a slot at the left. In 81.1, 69.1 and 66.3 it reads as a
  distant pass; in 71.1 as a flat horizon behind the elder.
