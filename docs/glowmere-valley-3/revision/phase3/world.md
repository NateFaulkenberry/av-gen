# Phase 3, gv3-world: the world edge and the offline configuration

Stream gv3-world. Branch `gv3/world`, worktree `av-gen-gv3-world`, from `gv3/production` `334c4cf6`.
Engine: the shared build `040d6644` for the analysis, then `ec515c8b` (main plus characters) for
every trace and render. The world code (`src/world/`) is identical in the two builds except
`effects/history_bank`, so the terrain measurements hold on both.

Files: `tools/gv3/world.py` (`close_ends`, `Field`, the `--check` CLI), `tools/gv3/offline.py`
(new), and five lines of `tools/make_glowmere_valley_3.py` (the closure's call, `--final` and
`--final-trace`).

## W0: what the edge is, measured

**The geometry.** The WorldMap is a 640 m square. The terrain mesh stops at its chunk grid, which
is 14 chunks of 48 m: [-320, 352] on both axes (`terrain.cpp` `chunkGrid`). The valley's corridor
(300 m wide), river and banks all ran from z -352 to +352. So at both ends, the flattened valley
floor reached the edge low:
- north: 10-20 m, across x -130..20;
- south: -7..+5 m, across x -80..90.

The walls on either side stand 90-110 m high. Every wide looking up or down the valley met a flat
line of sky. In the south, that line was *below* eye level.

**The check.** `python3 tools/gv3/world.py --check [--trace cast.json]` marches 96 columns per view
over the world's heightfield. The heightfield is the engine's own heights (`avgen_world_preview`
probes) on a 2 m grid.

A column shows an **open end** when all of these hold:
- its skyline is ground within 6 m of the grid's boundary;
- that ground is gentle (rise under 0.2 per metre over the last 20 m);
- it is low (under 5 degrees above the eye);
- it is inside the frame.

That is the valley running out of the world. It excludes a wall's top cut by the boundary: those are
high and steep, and read as a hilltop from anywhere below. Both are in the first pass's film, and
nobody reads the second kind as an edge.

**The baseline** (the camera at every frame of the film, from `avgen_cast_trace --camera`, sampled
every 0.5 s): **275 of 452 views show an open end, in 21 of 40 shots.**
- North, the valley floor: s06, s09, s10, s12, s13, s14, s17, s18, s20, s21, s33, s37, s39 and s01's
  right edge.
- South: s04, s07, s08, s11, s22, s23 and s28.

The brief's six wides are all in the first list. So are follow shots that nobody lists: s09, s10,
s13, s18. The first pass's own stills agree, for example `revision/audit/reports/render-post.md`'s
frames at 24.5, 62 and 78 s.

## W1: why ridges alone cannot close it, and what can move

The engine's height (`world_map.cpp` `heightUncached`):

    h = mix(noise * roughness + sum(ridge raises), flattenTarget, flattenWeight)

and then the water cuts. Within about 54 m of the river, the banks (flatten 0.8) and the corridor
(0.55) together weigh 1 or more. **A ridge crossing the river is erased there**, whatever its
amplitude. So each end has to be closed by changing what flattens it.

Four constraints decide where each change can go.

1. **The altitude every biome reads.** The terrain material and the default biomes read altitude as
   (h - min) / (max - min) over a 97 x 97 survey of the map (`WorldMap::prepare`). If either extreme
   moved, every plant's biome weight would move, and plants would appear and vanish all over the
   valley.
   - The minimum is the river bed at (19.8, 316.7), -12.977 m.
   - The maximum is the north-west rim at (-310.1, -296.9), 118.893 m.
   - A first candidate that re-levelled the corridor moved the maximum to 121.7 m and 2,060 survey
     points. The corridor is therefore never touched. It is 300 m wide, and its far end reaches the
     north-west corner.
2. **The filmed valley.** A path's control point P_j reshapes its smoothed curve from
   mid(P_j-2, P_j-1) to mid(P_j+1, P_j+2): three passes of Chaikin, checked numerically. A point's
   level reads the curve only within 10 m of its nearest point.
   - Re-levelling the banks' second point (z -286) moved the ground 1.7 cm at (-80, -136), where
     bull-1 and horse-20 graze. Rejected.
   - Every edit is made at or beyond the path's second point from its end.
3. **The cast.** bull-10 and bull-21 graze at z -287..-245, x -100..-67: in the valley's north end,
   300 m or more from any camera. The characters stream does not re-home them (only horse-2, horse-20,
   horse-22, cow-12 and cow-23 go to the meadows). Nobody else goes north of z -145 or south of z 92,
   apart from the saucer, which flies.
4. **Scatter order (what cannot be helped).** Scatter rows run north to south (`ecology.cpp`). A
   plant's hue jitter, glow multiplier and whether it is one of the specimens that stay dark are
   keyed on its *index* in its layer (`procedural.cpp`, `materialVariation`). Its position, scale and
   yaw are keyed on its cell. So any change to how many plants the northern rows accept re-deals the
   glow for every plant south of them: the same plants in the same places, a different lottery for
   which of them shine. This cannot be avoided by any closure of the north end in the terrain. It is
   measured in W3 and reported, not hidden.

## W2: the closure (commit `25c227dc`)

**North** (`world.py` `NORTH_*`):
- **Banks:** the banks' first point is replaced by two, at levels 70 and 62. The band therefore
  climbs north of the river's source instead of running flat to the edge.
- **River:** in `25c227dc` the river started at its second point, (-58, 12.8, -286), in a pool at
  the foot of the climb. That joined the valley's halves for the navigator (W2b, W2c). Since
  `7259efad` it keeps its whole course, with its first point moved to (40, 15, -350), so its gorge
  through the head turns east out of sight.
- **Head ridge:** `north-head`, width 70, amplitude 70, spans the V from x -140 to 50 along
  z -292..-306.
- **Shoulder:** `north-shoulder`, width 50, amplitude 30, covers the east shoulder the head ridge
  left open. A single feature has one amplitude, and carrying the head ridge further east lifted the
  east wall above the survey's maximum, to 143.8 m.

**South** (`SOUTH_*`):
- **Banks:** the banks end at their eleventh point, (45, -4.6, 244). Beyond it only the corridor
  flattens (0.55), so a ridge stands at 45% of its height.
- **Sill:** `south-sill`, width 60, amplitude 70, spans the end from x -110 to 200 along z 328..342.
  Its crest is about 17 m inside the edge, and the ground falls behind it.
- **River:** the river keeps its whole course. That course is what holds the survey's minimum. It
  leaves through a gorge in the sill. The gorge runs 14 degrees west of south and the cameras look 5
  to 12 degrees east of it, so its walls close every line of sight within the sill.
- A first sill that ran to x -200 lifted the south-west corner to 131 m. It was shortened.

**Measured** (`world.py --check`):

| | baseline | closed |
|---|---|---|
| views with an open end (traced camera, every 0.5 s) | 275 of 452, 21 shots | **0 of 452** |
| views with an open end (fixed and keyed rigs, 5 per shot) | 60 of 120, 14 shots | 0 of 120 |
| survey minimum | -12.977 | -12.977 (moves 0.058 mm, computed, see below) |
| survey maximum | 118.893 | 118.893 (bit-identical: no edited feature reaches it) |
| ground, z -190 to 170, 2 m grid | | unchanged to the mm everywhere |
| every rig key GV3 writes | | byte-identical (the generator's output differs only in the 3 new features and the 2 edited paths) |

**The survey minimum, exactly.** Trimming the south banks takes their level out of the flatten
average at the minimum point, so the target there rises 0.95 m. But the point is 0.13 m from the
river's centreline. The river's weight there is 0.99994, so the cut passes on only 6 x 10^-5 of it:
the minimum moves by 0.058 mm (-12.977452 to -12.977394). That shifts the altitude every biome reads
by 4 x 10^-7 of its range. The expected number of plants that change anywhere in the world is about
0.1. The maximum does not move at all.

**Other checks on the closed world:**
- `avgen_world_preview --seams`: 0 ground and 0 water seams.
- Slope p99 rose from 0.27 to 0.49 (the new head and sill). Biome shares moved: scree 13 -> 17%,
  forest 32 -> 30%, rim 11 -> 10%.

## W2b: the cast and the camera (found at the checkpoint; the cause and the fix are in W2c)

`avgen_cast_trace --camera` over the whole film, on engine `ec515c8b`, baseline against closed
(`build/gv3/world/cast-base.json` against `cast-v1.json`; the script is in the stream's scratchpad,
`world/trace_diff.py`).

| Entity | Max divergence | Mean | First divergence |
|---|---|---|---|
| bull-10 | 19.0 m | 12.0 m | 0.00 s (expected: grazes beside the new pool) |
| bull-21 | 23.1 m | 7.5 m | 0.00 s (expected, same place) |
| ember | 97.9 m | 40.5 m | 17.00 s |
| rook | 101.8 m | 39.7 m | 13.50 s |
| sage | 23.8 m | 4.5 m | 18.00 s |
| tide | 47.2 m | 6.5 m | 121.00 s |
| vane | 23.3 m | 5.7 m | 61.50 s |
| horse-2 | 0.031 m | 0.001 m | 201.50 s |
| horse-22 | 0.009 m | 0.000 m | 204.00 s |
| the other 7 animals and the saucer | 0 | 0 | never |

- **The camera moves in 4,042 of 13,531 frames, by up to 77 m.** Shot indices 8, 12, 17, 18, 19, 24,
  27, 35 and 37 are affected: s09, s13, s18, s19, s20, s25, s28, s36 and s38, every shot that follows
  an alien.
- **The aliens walk nowhere near the edited ground.** Their whole ranges lie inside z -100..91, and
  every height there is unchanged to the millimetre. So something global carries the edit into their
  decisions.
- **Suspects, not yet tested:**
  1. The nav grid's connectivity. The baseline log says "the walkable ground is in 4 disconnected
     pieces; 9805 of 20214 walkable cells (49%) cannot be reached from the largest", and new steep
     ground at the ends changes the pieces.
  2. The water the aliens are drawn to (07-technical §7.3): the river's north end is now a pool.
  3. The scatter's index-keyed glow (W1, point 4), if an alien's attention reads lit specimens.
- **Why it matters.** The first pass framed these follow shots against the baseline trace. The
  characters stream (ec515c8b) and gv3-cast will re-trace the cast anyway, but a distant terrain edit
  should not reroute the aliens.

## W2c: the cause was the river's head, not the ground (commit `7259efad`)

The nav grid's own log line gave it away:
- **baseline:** "the walkable ground is in 4 disconnected pieces; 9805 of 20214 walkable cells (49%)
  cannot be reached from the largest";
- **v1:** "8 of 20179 walkable cells (0%) cannot be reached".

The navigator walks anything up to `maxSlope` 0.55 (about 63 degrees). So **only water divides the
valley**, and the river, edge to edge, is what kept its two banks apart. Ending the river in a pool
inside the world opened a walkable way round its head. Nobody walks it, since the aliens' ranges
barely change, but the aliens' choices depend on what is reachable. So a destination across the
river that used to be refused now was not, and every alien's route changed.

Two things were ruled out on the way:
- a shared random stream (every entity has its own `rng_`);
- a global destination list (`Navigator::pickDestination` samples around the walker).

**The fix.** The river keeps its whole course. Only its first point moves, from (-44, 15, -352) to
(40, 15, -350). The gorge it cuts through the head then turns 45 degrees east of north, away from every
line of sight up the valley.
- With the first point left where it was, the straight gorge showed the edge in s12 and s37 (10
  views).
- With it at (10, -352), one view of s37 still did.
- At (40, -350), none.

The nav grid is divided again: "5 disconnected pieces; 9820 of 20153 walkable cells (49%)". The extra
small piece is at the north end, between the bent gorge and the edge.

`world.py --check` now also asserts that **the river runs edge to edge**. The whole-film trace of this
version (`cast-v2a.json`) is in W2d.

## W3: renders

Batch 1, queued behind the gv3-cut stream's full-film render (the lock was held for 25+ minutes):
1080p x2 stills of v1 at 78.0 (s14), 214.0 (s39), 62.0 (s12), 28.0 (s07), 159.0 (s23) and 199.0
(s37). Only the baseline's s14 at 78 s was rendered at the checkpoint. It shows the defect plainly:
the V ends in a flat line of ferns and trees against the aurora
(`build/gv3w/stills/base-s14-78.0/frame_000000.png`).

Batch 2 (`build/gv3w/jobs2.txt`, not yet run):
- the remaining before/after pairs: s06, s07, s12, s21, s22, s23, s33, s34, s37, s39;
- the march calibration: v1m025, v1m050 and v1m100 at 78 s, and v1m050 against v1 at 110 s;
- the shadows-only variant v1s at 78 and 214 s.

Scratch copies of the project live in `build/gv3w/<variant>/world/`. `materials`, `lightrigs`,
`entities` and `assets` are symlinks, and the song's path is absolute. The mirror script is the
scratchpad's `world/mirror.py`.

## F1: the offline configuration (commit `e95db493`)

`python3 tools/make_glowmere_valley_3.py --final [--final-trace cast.json]` applies
`tools/gv3/offline.py`. Nothing else does: previews keep their settings.

| Setting | Preview (as generated) | Final |
|---|---|---|
| render | 1920x1080 x2, offline, limits unlimited, h264 q90 | **3840x2160 x2, offline, limits tier, prores422 q95**, `build/gv3/final/glowmere-valley-3-2160p.mov` |
| shadow cascades | 3 (the scene's count beats the tier's) | 0 = the offline tier's 4 |
| `scene/shadowRange` | 0 = ADR-112's automatic, about 77 m | 160 m base; **300 m, step-keyed at the installed cut times, on shots whose frame is a fifth or more ground beyond 160 m** (measured by rays over the heightfield; with the trace, 21 of 40 shots: s02, s04-s14, s17-s19, s22, s23, s30, s33, s37, s39) |
| terrain `shadowDistance` | 150 m | 320 m |
| `nodes/valley/terrainViewDistance` / `terrainLod` | 640 m / on | 1000 m / off (LOD 0, 1.2 m quads, everywhere) |
| fog march: `scene/volumeMaxDistance`, `volumeSteps`, `volumeJitter`, `horizonDensity` | 0 (off), 12, 1.0, 0 | 220, 32, 0.5, 1.0 |
| `scene/volumeScattering` | 0.5, inert while the march is off | 0.5 (calibration pending) |
| the arc's `scene/volumeDensity` | look.py's keys | **untouched**: extinction is density x absorption and in-scattering density x scattering (`volume.wgsl`), so the veil stays the previews' and only the march's light is set |
| 17 rigs' `animation.updateHz` | 30 | 0 (every frame) |

Verified in the generated project: every value lands. The 17 shadow-range keys fall exactly on the
installed cut times, which already include the one-frame lead.

## F2: 4K ranges, the log lines and the cost (not yet run)

Batch `build/gv3w/k4jobs.txt` is 3840x2160 x2, prores422, `AVGEN_SHADOW_STATS=1`:
- v1final at 76-78 s (s14), 108-110 s (s18) and 178-180 s (s33);
- v1, the preview configuration forced to 4K, at 76-78 s, as the cost baseline.

The lines to read:
- "render scale 2.00: scene target 7680x4320 -> output 3840x2160";
- "LOD rungs kept";
- "rate-limited 0";
- the shadow lines' range 160/300 m and 4 cascades.

For "no pop-in": the procedural LOD ladder switches rungs with hysteresis, by projected size, and
does not cross-fade. Check frame differences in the far field of s14's truck.

## Open questions
- **The alien divergence (W2b):** find the coupling before this merges. If it is the nav grid's
  connectivity, the fix may belong in the engine, not here.
- **The fog march's look:** it lights the near 220 m with the moon and local lights instead of a
  flat navy veil. It needs calibration and gv3-look's agreement, because previews never show it.
- **The ecology-light options** (`ecologyGlowCell` 14, `ecologyLightRange` 250) re-cluster the glow
  spill in every shot, near ones included. Left out of `offline.py` unless an evaluation supports
  them.
- **The bare south crest:** scatter only covers the world's own [-320, 320], so the sill's crest at
  z 328-342 grows nothing. Judge it on the stills.

## Next steps (exact)
1. Run batch 1's remaining stills, then batch 2
   (`tools/gpu-lock.sh build/gv3w/batch.sh build/gv3w/jobs2.txt`). Make before/after sheets in
   `~/Desktop/av-gen-review/18-glowmere-valley-3/revision/world/`, and submit both sets to the Critic
   (session gv3-world, track world-edge, labels base and v1).
2. Isolate the divergence. Trace a north-only and a south-only variant (CPU, 24 min each, can run
   together), then, within the guilty end, the ridge features against the path edits. Read the nav
   grid log lines of each.
3. Calibrate `MARCH_SCATTERING` on the batch 2 stills, by mean luma in depth bands (terrain depth
   from `world.Field`).
4. Run the 4K batch (`tools/gpu-lock.sh build/gv3w/k4.sh build/gv3w/k4jobs.txt`). Record the log
   lines, the ms per frame, the peak memory (`/usr/bin/time -l`), and the whole film's estimate
   (13,530 frames).
