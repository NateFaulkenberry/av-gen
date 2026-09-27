# Phase 3, gv3-cut: the Director's cut, pacing, alien screen time, cameras

The stream's iteration log (briefs.md, Phase 3, "gv3-cut"). Each iteration records what changed,
what the evaluator measured, and what was decided. Worktree `av-gen-gv3-cut`, branch `gv3/cut`,
from `gv3/production` `334c4cf6`.

## How the cut is made now

**The times and arcs are the engine Director's.** The generator runs Song Mode on the project it is
building (`tools/gv3/songcut.py`: `avgen_song_cut` on a scratch copy, CPU, about 5 s) and gives each
span it lays down one authored composition (`tools/gv3/shots.py`, keyed by the span's bar and beat).
`shots.build()` refuses a span with no composition and a composition no span uses.

What GV3 tells the Director, in the engine's own vocabulary:

- **Treatments** (`songcut.TREATMENTS`, `TREATMENT_OF`): one shot intent per section. Six sections
  keep a built-in; seven get GV3's own, defined in the project's shot language, so they appear by name
  in the Sequence panel's treatment picker ("GV3: the riser's roll", ...), with the Director's reading
  of each beside it ("cuts: ...", "cut rate X% at its start, Y% at its end").
- **Settings** (`songcut.SETTINGS`): the project's `autoDirector` block, Song mode, Expressive,
  shortest shot 1.8 s, longest 7.5 s, shortest build 0.45 s (the song stream's recommendation). The
  Main camera stays available to the Auto-director, so Direct in the app lays the same spans.
- **Events** (`songcut.events`): E1-E5's moments as Song Mode plan events (`songPlan.events`), so a
  peak goes to the set piece playing in it. Read from the project's own `setpiece/<id>/<moment>`
  markers once gv3-cast compiles the UFO plan into it; until then, the plan's nominal moments as the
  engine compiled them.

Two edits on top, each recorded here with its evidence:

- **The grid.** The Director cuts on the tracked beats, about 9 ms late (worst 19 ms). Each cut is
  placed on the Director's bar and beat of the production's fitted grid (`music.py`), then led by one
  frame (`CUT_LEAD`), as the first pass did.
- **Trims** (`songcut.TRIMS`): a shot the Critic's novelty says has said its piece ends on an earlier
  beat, and the next one starts there.

**Reproducible.** `python3 tools/make_glowmere_valley_3.py` re-runs the Director and checks its cut
against `tools/gv3/song_cut.json`, the cut the compositions were written against. If another
stream's change moves a span, it says which, and keeps the recorded cut; `--recut` adopts the new one.

## Iteration 0: the baseline

The first pass's cut (`334c4cf6`), measured on the evaluator's baseline job `job_1a0def77d9084ad74`
(the delivered 1080p final) and on the camera pose track (`avgen_cast_trace --camera`).

- **Pacing.** 40 shots, median 3.69 s; three 8-bar takes on the plateau; the drop's last phrase one
  8-bar shot. 22.0 s of 225 s after a shot's last new information: s16 and s24 showed nothing new
  after their first frame, s08 held 2.5 s of its 7.4, s07 1.15 s, s27 1.85 s.
- **The Director with GV3's treatments as they were** (the song stream's recommended settings): 72
  spans, but the riser cut 8-4-2-2 beats (weaker than the first pass's 4-4-2-2-2-1-1), the
  suspension and the break were single holds of 8 and 4 bars, and groove 2 was four 4-bar takes.
- **Aliens lead 38%** (12 shots, 86 s).
- **Camera stability** (`tools/camera_stability.py` on the first pass's rigs, engine `040d6644`):
  13 of the 16 shots with a subject fail the bar. Pitch HF up to 1.22 deg (s38), 1.14 (s19), 0.77
  (s29); eye height HF up to 11.2 cm (s03), 9.0 (s28), 7.9 (s19).
- **Composition.** 13 composition and 12 visual-hierarchy findings in the baseline job; one "several
  saucer events are framed alike".

## Iteration 1: the Director's cut, with GV3's treatments

**The Director's cut, section by section** (beats per shot; "before" is the song stream's
recommended cut with the first pass's treatments, "after" is GV3's):

| Section | Arc before -> after | Shots before -> after | Beats before | Beats after |
|---|---|---|---|---|
| cold open | steady | 1 -> 1 | 17 | 17 |
| riff groove | steady | 6 -> 6 | 8 8 8 8 4 4 | 8 8 8 8 4 4 |
| pull-back | rising | 1 -> 1 | 8 | 8 |
| groove 2 | steady | 4 -> 8 | 16 16 16 16 | 8 8 8 8 4 12 8 8 |
| lift | rising | 9 -> 6 | 8 4 4 4 4 2 2 2 2 | 8 8 4 4 4 4 |
| arrival | rising | 7 -> 7 | 8 8 4 4 4 2 2 | 8 8 4 4 4 2 2 |
| plateau | steady | 12 -> 12 | 8 8 4 12 8 8 8 8 6 10 8 8 | the same |
| lead forward | rising | 7 -> 5 | 8 8 4 4 4 2 2 | 8 8 8 4 4 |
| suspension | suspended -> steady | 1 -> 2 | 32 | 16 16 |
| break | rising -> steady | 1 -> 2 | 16 | 8 8 |
| riser | rising | 4 -> 7 | 8 4 2 2 | 4 4 2 2 2 1 1 |
| drop | steady -> falling | 18 -> 15 | 8 4 4 4 4 4 4 8 4 4 4 8 4 6 6 4 8 8 | 8 4 4 4 4 4 4 4 4 4 4 8 8 16 16 |
| tail | burst | 1 -> 1 | 7.3 | 7.3 |

73 spans; every cut on a beat, 67 of 72 on a bar line, every section boundary on its downbeat;
lengths 0.46-7.87 s, median 3.69 s, CV 0.53 (the first pass's band gave 0.28). The drop is the film's
one peak and goes to the saucer through E5's departure (`setpiece/e5-centrepiece/depart` at 177.70 s).

Why each of GV3's treatments differs from the built-in it replaces:

- **Groove 2, "the valley wakes":** the built-in held four 4-bar takes, the first pass's shape, where
  s08 held 2.5 s past its last new information. Two-bar cuts, one longer travel.
- **The lift:** the built-in went to half-bar cuts after two bars, as frantic as the riser it leads
  to. Two 2-bar shots (the cairn; the crane that finds E3's column), then bar cuts.
- **The lead forward:** the built-in accelerated to half bars, the third section running that way.
  Two-bar takes on the aurora, tightening to bars only before the suspension.
- **The suspension:** Suspended holds one 8-bar shot. The saucer's approach has two stages, and the
  first pass's two 4-bar locked-off wides both kept introducing information to their last frame.
- **The submerged break:** one 4-bar hold became two 2-bar holds; the first pass's frame under the
  elder showed nothing new after its first frame.
- **The riser, "the roll":** the fastest a rising arc cuts. The Director lays it as 4-4-2-2-2-1-1 beats:
  the first pass's compression exactly, from the music.
- **The drop, "the light rebuilt":** a falling arc: a 2-bar payoff on the crash, bar cuts through the
  rebuilt valley, and two 4-bar wides for the final, thinner phrase -- the room the last wide's rhyme
  with the first grand wide needs.

**Where the Director's model and the plan disagree.** The plan wanted the arrival's grand wide held
for 4 bars and then 1-2 bar cuts. The Director's rule holds a section's opener at most 1.6 times its
pace (the scale term, ADR-921), so "4 bars, then 1-bar cuts" is outside it; the arrival keeps the
built-in "rising reveal" (2-2-1-1-1-1/2-1/2), and the grand wide is 2 bars. Its rhyme with the last
wide stands on the vantage, not the length.

**Alien screen time: 13%** (29.5 s; the budget is 20%, and the generator refuses a cut above it).
Aliens lead ten shots, each a live follow: Rook investigating (21.1), Vane watching the elder (29.1),
Tide seeing E3's column (38.1), Sage in the grove (66.3), Vane under the aurora (73.1), Tide looking up
(80.1), Ember watching the saucer (91.1), Vane seeing the lift (95.1), Ember looking up (103.1), and
Vane walking to where the horse was taken (111.1). They also appear in E4 (watching the pair rise) and
in the wides.

**The UFO events** (the plan's places and times; the setpieces stream's plan, compiled on a scratch
copy with the scout craft):

| Event | Moment | Framed by |
|---|---|---|
| E1, survey | the sweep, bars 9-13; the beam out on bar 13 | 11.1: the elder, and 340 m beyond it the thin beam sweeping (50 mm, low, from the south reach) |
| E2, flyby | the crossing, bar 15 + 0.3 s | 15.1: the first pass's frame, from 112 m north of the elder |
| E3, one animal, far | the beam at bar 36.4, the lift on bar 37 | 35.1: the crane rises to reveal the column; 37.1: the lift through a 100 mm; 38.1: Tide turned toward it |
| E4, two animals, near | the beam at bar 56.3, the lift on bar 57 | 54.1: the scout settles beside the elder (90 m); 57.1: the pair lifted (55 m); 59.1: Vane watches it |
| E5, the centrepiece | the approach from bar 81, the beam on bar 93, the horse gone on the drop, the exit on bar 99 | 81.1 and 85.1: the suspension's locked-off wides; 89.1, 91.1: the break; the riser's seven views; 99.1 and 101.1: the exit |

The E3 and E4 rigs aim live at the scout, so they frame the set piece wherever its region's subject
puts the craft (gv3-cast re-validates E4 after re-homing the animals).

**Camera.** Every rig with a subject names its kind of ADR-913 follow value (`rig.SMOOTHING`), and the
generator refuses one that does not: walkers 0.3 / 1.0 / 1 with the ground, riding with a walker
toward a fixed point or a craft 0.3 / 1.2 / 1, a flying craft 0.6 / 1.0 / 0, a lifted horse and a
static subject none. `followLagSeconds` is gone from GV3.

### Iteration 1, measured (engine `ec515c8b`: main plus characters)

The render and trace are of a **preview world**: the generator's project with gv3-cast's expected
changes applied to a scratch copy (`tools/gv3/preview_world.py`, evidence only: the characters
stream's tuned cast, the scout craft, the E1-E5 plan compiled in). Files in `build/gv3/cut/it1/`.

**Camera stability** (`tools/camera_stability.py` on `avgen_cast_trace --camera`; the bar: pitch and
yaw HF <= 0.1 deg, eye height HF <= 2 cm, subject off-centre <= 20%):

| | shots with a subject | pass | fail |
|---|---|---|---|
| before: the first pass's rigs, same engine (`build/gv3/cut/baseline2/stability-baseline.json`) | 16 | 5 | 11: s03, s09, s13, s19, s20, s25, s28, s29, s36, s38 on the nod or the bob (pitch HF up to 2.41 deg, eye height HF up to 13.4 cm) |
| after: iteration 1 (`build/gv3/cut/it1/stability-it1.json`) | 22 | 18 | 4, below |

Every walker follow passes (Rook, Sage, Vane three times, Tide, Ember: eye height HF 0.30-1.46 cm,
pitch HF <= 0.027 deg). The four failures:
- **59.1 (E4, Vane watching):** yaw HF 12.6 deg -- the rig aims at the scout, and E4 never played
  (below), so the aim swung to the hidden craft.
- **94.1 (up the beam):** yaw HF 0.137 deg, the hovering saucer's sway through a 20 mm at 26 m.
- **101.1 (the saucer, far, climbing away):** pitch HF 0.110 deg.
- **95.3 (the horse rises):** pitch HF 1.31 deg over 0.92 s. ADR-913's own exception: the rise is the
  shot, and the metric cannot tell a deliberate tilt from wobble in under a second (the first pass's
  s29 measures 2.41 deg on this engine).

**The set pieces, as they played** (`setPieces` in `build/gv3/cut/it1/cast-it1.json`):
- E1: beam 13.90 s, sweep 15.03-22.63 s over x -50..10 at z -190, 30 m up. Played.
- E2: cross 26.65 s. Played.
- E3: beam 65.62 s, lift 66.97 s, bull-10 taken at (-78.5, -265.8), retired 71.88 s. Played.
- **E4 did not play.** On the tuned cast the re-homed cows graze 35-40 m north of the plan's region
  centre (-55, 40), outside its 30 m radius, and only cow-19 is inside it. The scout took its transit
  at 93.8 s, entered its approach only at 110.5 s, found one animal and left at 117.5 s without
  beaming. **gv3-cast owns the fix** (the region to about (-70, 8), where cow-12 and cow-23 graze, or
  two animals named). gv3-cut's E4 shots aim live at the scout, so they follow the station wherever it
  moves, but they cannot be judged until E4 plays.
- E5: beam 170.35 s, lift 172.78 s, the horse retired 177.72 s. The station moved 9.5 m west with the
  tuned cast, to (-0.6, 28.0, 73.9); the riser's live aims follow it.

**On screen at each set-piece moment** (my projection of the rigs through the measured craft
positions, `build/gv3/cut/tools/onscreen.py`; the Critic's `framing[].on_screen` to confirm): E1's
beam and sweep in 7.1 (after the fix below), its approach in the cold open; E2 during its crossing in
15.1; E3's beam in 35.1 and its lift in 37.1; E5's approach in 81.1, beam in 93.1, lift in 94.1 and
departure in 97.1. Off screen: E1's and E3's departures, E3's approach (by choice).

**Fixed after iteration 1's render started** (`27e18605`, not in the it1 render): E1's wide moved to
the span its beam lights in (7.1), from the east bank, because from the first vantage the scout
hovered straight behind the elder's cap; the lantern moved to 11.1. E4's establishing shot aims live at
the scout (on the first-pass cast its station was 20 m west of the region centre, off the fixed frame).

### Iteration 1, the evaluator

**Job `job_1a0e399224ec50121`** (session `gv3-cut`, track `film`, label `it1`; preview, complete):
the it1 render (`build/gv3/cut/it1/cut-it1.mov`, 960x540) with inputs regenerated by the adapter
from the preview project and its trace (INTEGRATION_GUIDE §6; `build/gv3/cut/it1/critic/`).
133 issues (2 critical, 5 high, 53 medium, 73 low) over 73 shots. Pacing 0.98, musical sync 0.96;
weakest composition 0.65, cinematography 0.70, visual hierarchy 0.71.

**The same-engine "before":** job `job_1a0e3b6d031dcf565` (track `film-baseline`, label
`baseline-ec515c8b`; preview, complete): the first pass's cut rendered on engine `ec515c8b` at
960x540 (`build/gv3/cut/baseline2/cut-baseline.mov`, 13,530 frames, GPU errors 0), with inputs from
the adapter and flags identical to it1's (no set pieces in that project, so no event checks).
91 issues (1 critical, 7 high, 37 medium, 46 low) over 40 shots.

**The Critic's comparison, before -> it1** (`critic compare`, `cmp_1a0e3b8f1b2ee87fb`,
`build/gv3/cut/it1/cut-compare-baseline-it1.{txt,json}`):

| Dimension | before | it1 | |
|---|---|---|---|
| character staging | 0.868 | 0.918 | +5.0, improved |
| musical synchronization | 0.934 | 0.963 | +2.9, improved |
| pacing | 0.969 | 0.979 | +1.0 |
| composition | 0.637 | 0.647 | +1.0 |
| cinematography | 0.742 | 0.698 | -4.4, degraded |
| visual hierarchy | 0.746 | 0.708 | -3.8, degraded |
| temporal coherence | 0.909 | 0.876 | -3.3, degraded |
| lighting | 0.787 | 0.756 | -3.1, degraded |
| technical quality | 0.768 | 0.748 | -2.0, degraded |

61 findings resolved, 103 introduced, over 73 shots instead of 40. The per-shot lines of the
comparison pair shots by id, and the two cuts' ids name different shots, so only the dimensions
compare. **Novelty on the same engine:** the before holds 24.5 s after its shots' last new
information (4 shots static from their first frame); it1 holds 44.3 s (14 static). The regressions
are the compositions below, which iteration 2 re-sites or gives a real move.

What it found, and what I made of it:
- **Composition, confirmed on the sheets** (`build/gv3/cut/it1/review/`): Vane out of frame for all of
  29.1 (critical); horse-11 in front of the elder in 14.1; a near-black sky in 101.1; E4's shots aimed
  at a hidden craft (E4 never played); a fern, a trunk, a rock or a tree across 23.1, 61.1, 69.1 and
  113.1; the veil and the spire specks in 46.1, 104.1 and 106.1; 19.1 over the pool; Ember out of 91.1.
- **Novelty: 44.3 s of 225 held after the last new information, twice the baseline's 22 s.** The
  cause is mine: short hero shots that were stills or 1-2 m pushes, which the layout measure reads as
  static from their first frame (14.1, 25.1, 31.1, 39.1, 40.1, 47.1, 65.1, 89.1, 106.1, 107.1, and
  91.1 with no Ember in it). Trims would not help a shot that is static from its first frame; each got
  a real move instead (iteration 2). The riser's short shots also measure as held; they are the
  roll's compression and are left alone.
- **Repeats:** 47.1 and the tail (the first pass's s16/s40 pair again); the riser's last two beats
  (one frame twice); 54.1 and 75.1 on one line of sight. 41.1 and 117.1 is the rhyme, kept on
  purpose; the cold open and the tail "match" on their black frames.
- **Events on screen at each moment** (`measurements.events.types`, the coordinator's check): E3's beam
  (35.1) and lift (37.1); E5's approach (81.1), beam (93.1), lift (94.1), departure and the horse taken
  (97.1); E1's approach, far, in the cold open. Off: E1's beam and sweep (the it1 render predates their
  fix), E2's crossing at its first instant (the craft enters the frame 0.9 s later and is in it for
  1.28 s), E3's animal taken (fixed in iteration 2: 39.1).
- **Stability, and where the Critic and the pose track disagree.** The Critic's pixel classes: 3
  "shaky" and 5 "move with wobble" of 71 measured shots, against 5 and 3 of 38 in the same-engine
  before (10 and 2 of 38 in the first pass's 1080p final, on the older engine). Two of the three
  "shaky" (66.3 Sage, 111.1 Vane) are walker follows the pose track measures as steady (eye height HF
  0.84 and 0.89 cm, pitch HF 0.002 and 0.000 deg), and 111.1's pixel measure is identical to the first
  pass's s38 (0.00211) although its camera's pitch HF fell from 0.225 deg to 0.000. The image's global
  motion is contaminated by a large walking subject bobbing in a steady frame; the pose track, which
  reads the camera itself, is the measure of the camera. The Critic's scene-side "camera path jitter"
  (s35, s50, s69) comes from the adapter's own model of the rigs, which does not implement ADR-911's
  filter: `avgen_adapter.py` puts a follow rig's eye at the raw node plus the offset (lagged by
  `followLagSeconds` only) and its aim at the raw aim node, so a walker's stride is in the modelled
  camera and not in the engine's. The cast trace's `--camera` track has the engine's own eye and
  target at every frame; the adapter could read them (for the critic-adapter stream).
- **Not this stream's, recorded:** the aliens' bases float up to 0.53 m off the ground for 24-49% of
  their tracks (grounding, ember, vane, rook), and three alien liveliness routes show no response
  (gv3-cast and gv3-look).

### Iteration 2: the fixes (`dd2306b2`)

Every composition above re-sited or given a real move; E3's animal taken on screen (39.1); 94.1 at
12 m from the station; 96.4 from the south-west. Two framings were searched on the trace rather than
guessed: 29.1 (Vane on the right third, the elder on the left, for the whole span) and 91.1 (Ember's
shoulders lower left, the settling saucer upper centre).

**E4 on a stand-in.** So that E4's shots can be judged at all, the iteration-2 preview world moves E4's
region to (-71, 8), radius 18, where the tuned cast grazes cow-12 and cow-23
(`build/gv3/cut/ufo-e4-standin.plan.json`, evidence only). gv3-cast owns the real place; E4's shots aim
live at the scout and follow it. (gv3-cast's iteration 2, `0ff69eea`, WIP, moves the region to
(-69, 6), radius 20, for the same two cows: 3 m from the stand-in.)

### Iteration 2, measured on its trace (engine `ec515c8b`)

`avgen_cast_trace --camera` of the iteration-2 preview world (`build/gv3/cut/it2/cast-it2.json`,
19.5 min CPU). The render is queued behind the GPU lock (`render-it2.sh`, whole film, 960x540).

**E4 plays on the stand-in:** the scout's approach 93.88 s, hover 100.90, beam 102.53, lift 103.88
(cow-12 and cow-23), both taken 111.92, departure 111.93. The lift is on 57.1's cut (103.86 s). E1,
E2, E3 and E5 play as in iteration 1.

**Camera stability** (`build/gv3/cut/it2/stability-it2.json`): **19 of 23 shots with a subject
pass** (iteration 1: 18 of 22). 59.1 fell from 12.6 to 0.242 deg of yaw HF now that E4 plays, 94.1
from 0.137 to 0.102, and 101.1, re-sited without a subject, is no longer measured (iteration 1: 0.110
pitch). Every walker follow still passes. The four that fail, and why, from an offline model of the
engine's subject filter (`build/gv3/cut/tools/aimfilter.py`: ADR-911's critically damped kernel on the
trace's own tracks; with each rig's own values it reproduces the engine's eye and target to 0.02 m
and its HF to 0.001 deg):
- **54.1, E4's scout settles** (yaw HF 0.204): the live aim follows the scout's approach, 75 m in the
  shot, and its stop into the hover at 100.90 s (48 cm of position HF at that corner). That is the
  set piece's motion, not sway; a longer constant leaves 0.186 and trails the craft by 18 deg.
- **59.1, Ember watches the pair rise** (yaw HF 0.242): the eye. It rides with Ember, who walks in
  bursts (idle, walk and turn, up to 3.9 m/s), and looks at a target 33 m off, so her follow's
  13.9 cm of horizontal eye HF is a yaw. The aim's constant changes nothing; no constant and lead
  get under 0.1 without leaving the eye more than 3 m behind her. A composition problem.
- **94.1, up the beam** (yaw HF 0.102): the hovering saucer's sway, 3 cm at 25 m, through the craft
  kind's 0.6 s. The model gives 0.071 at 1.0 s horizontal and 1.5 s vertical (the aim then trails
  the saucer by 0.9 deg at most).
- **95.3, the horse rises** (pitch HF 1.31): ADR-913's exception, unchanged.

**On screen at each set-piece moment** (`onscreen.py`, the craft projected through the rigs at the
moment plus 0.1 s and 1 s; the adapter also places each moment's event at the craft):
- **On:** E1's approach (the cold open, 434 m), beam and sweep (7.1); E3's beam (35.1), lift (37.1)
  and departure with the animal taken (39.1); E4's beam (54.1) and departure (61.1, in passing);
  E5's approach (81.1), beam (93.1), lift (94.1) and departure (97.1).
- **Off:**
  - E4's lift in 57.1: the scout is at the top edge (NDC y +1.11). The 50 mm aims 12 m below a craft
    44 m off, so the craft is 15 deg above centre with a 13.5 deg half-field.
  - E2's crossing at its first instant: the craft is 13 deg outside the left edge; it enters 0.9 s
    later (27.57 s) and is in frame for 1.28 s.
  - By choice: E1's and E3's approaches and departures, and E4's approach.

## Iteration 3: the framings the iteration-2 trace found wanting (`630ca638`)

Each designed on the iteration-2 trace, whose cast iteration 3 does not change (the project differs
only in `cameras/*`; checked leaf by leaf): the offline filter model for the camera, and the framing
searches in `build/gv3/cut/tools/search3.py`.

- **A hovering-craft kind** (`rig.SMOOTHING["hover"]`: 1.0 s, 1.5 s vertical, no lead): for a live aim
  at a craft holding its station, and for an idle watcher's follow that aims at one (one filter serves
  both nodes). 94.1: 0.102 -> 0.071 deg of yaw HF, the aim trailing the saucer by under a degree.
  Not for a craft on the move: into its station the scout trails by 18 deg at 1 s.
- **54.1, E4's arrival: locked off.** A slow linear push through a 28 mm, aimed a quarter of the way
  from E4's region (`E4_PLACE` = gv3-cast's (-69, 6), 14 m up) toward the elder's cap (`6589a152`;
  aimed at the region itself, the Critic found the cap crossing the frame's right edge): the scout
  flies in from the upper left (-0.81, +0.92) and settles just left of centre (-0.10, +0.27), the
  elder whole on the right (its modelled bounds within 0.84 of the edge); the region's four compass
  points at the station's height are all in frame, so the station may be anywhere in it. No subject,
  no HF.
- **57.1, E4's lift:** the same eye, 40 mm for 50, on the hover kind: at the lift the scout is in the
  top of the frame (+0.89; the Critic's adapter projects the raw craft) and the pair on the ground at
  the bottom (-0.83); both animals are in frame for the whole rise. HF 0.016 pitch, 0.013 yaw.
- **59.1, the aliens watch: Sage, not Ember.** In the preview world Sage stops on the rise 39 m west
  of the station at 106.5 s, turns to face it by 108.5 s (her yaw 57.4 deg, the bearing 55.3) and
  stands through the lift, so the follow is still and only the scout's sway reaches the aim. Over her
  shoulder through a 20 mm, offset searched on the trace: her head and shoulders low on the right
  third (x 0.43, y -0.63), the scout and both animals in frame for the whole shot, the eye 1.7 m over
  the rising ground behind her. HF 0.003 pitch, 0.028 yaw. gv3-cast's `beam` reaction (aliens within
  80 m go to see E4) would have her walk toward the column instead: away from the lens, in frame.
  Walking, her follow would carry her stride into the yaw as Ember's did; scaling Ember's modelled
  0.107 deg on the hover kind (15 m from the aim point) by distance gives about 0.04 at 39 m. An
  estimate, to measure on gv3-cast's world.
- **15.1, E2's flyby: panned 16 deg east** of the first pass's frame. The saucer comes in from the
  east, slowly at first; it was 13 deg outside the frame when its crossing began. Now it is in frame
  from that instant (x -0.90; from 26.68 s) and for 1.92 s instead of 1.28 (every 60 fps frame of the
  trace), and the cap sits on the right third (+0.36) with the sky it comes out of on the left. The
  owner's frame, moved: judged on the clip below.

### Iteration 3, measured on its trace (engine `ec515c8b`)

`avgen_cast_trace --camera` of the iteration-3 preview world (`build/gv3/cut/it3/cast-it3.json`).
The cast is bit-identical to iteration 2's (every entity's every sample: largest difference 0.000000
m), and so are the set pieces' beats and the animals taken: only the cameras changed.

**Camera stability** (`build/gv3/cut/it3/stability-it3.json`): **21 of 22 shots with a subject
pass** (the first pass on this engine: 5 of 16; iteration 1: 18 of 22; iteration 2: 19 of 23). The one
failure is 95.3, the horse's rise, ADR-913's exception (pitch HF 1.31). As the model predicted:

| Shot | iteration 2 (pitch / yaw HF, deg) | iteration 3 | |
|---|---|---|---|
| 54.1, E4 arrives (s33) | 0.092 / 0.204, fail | locked off: no subject | |
| 57.1, E4's lift (s34) | 0.007 / 0.024 | 0.017 / 0.013 | the scout now in frame at the lift |
| 59.1, the aliens watch (s35) | 0.098 / 0.242, fail | 0.003 / 0.028 | Sage, not Ember |
| 94.1, up the beam (s52) | 0.020 / 0.102, fail | 0.011 / 0.071 | the hover kind |

**On screen at each set-piece moment** (`build/gv3/cut/it3/cut-onscreen-it3.txt`, identical to the
prediction): every moment the cut means to show is in frame at its instant: E1's beam and the start
of its sweep (7.1; the sweep goes on after the cut to 9.1), E2's crossing from its first instant
(15.1), E3's beam, lift and departure with the animal taken (35.1, 37.1, 39.1), E4's beam (54.1),
lift (57.1) and departure (61.1), E5's approach, beam, lift and departure (81.1, 93.1, 94.1, 97.1).
Off by choice: the approaches and departures of E1, E3 and E4, and E2's exit (the craft is gone).

### Iteration 3, the Critic on the scene (no pixels yet)

Inputs from the adapter on the iteration-3 project and trace (`build/gv3/cut/it3/critic/`: 73 shots,
21 events from the 5 set pieces), judged `--mode fast`: job `job_1a0e3ebb032e98ec4` (track `scene`,
label `it3b`; complete), 80 issues (2 critical, 3 high, 16 medium).

- **The Critic's own event framing** (`measurements.events.types.*.framing[].on_screen`) agrees with
  the projection above at every moment: on screen E1's approach, beam and sweep; E2's crossing (x
  0.048: entering at the left edge); E3's beam, lift, departure and animal taken; E4's beam (54.1),
  lift (57.1), departure and pair taken (61.1); E5's approach, beam, lift, departure and horse taken.
  Off: E3's and E4's approaches, E1's and E2's departures.
- **The two criticals are the adapter's, not the cut's:** "ember is out of frame for 100% of the shot"
  in 48.3 and 101.1 (and in iteration 1's job, s29 = 48.3). Both shots frame the ember-cap mushroom
  ("the ember-cap mushroom"), which is centred and lit in the frames (`it1/cut-it1.mov` at 88.6 s).
  `avgen_adapter.py`'s `SUBJECT_ALIASES` had no alias for `ember-cap`, so `\bember\b` mapped the text
  to the alien Ember. **Fixed by the coordinator in creative-critic `961e04c`** ("a mushroom named like
  an alien is the mushroom"). With the fixed adapter, the iteration-3 inputs name `ember-cap` in s29
  and s61 and nothing else changes, and the before's inputs are identical, so the before jobs stand
  and still compare like for like. Resubmitted: job `job_1a0e3f09cc6ad3a9b` (label `it3c`; complete),
  **78 issues (0 critical, 3 high, 16 medium)**, composition 0.70 -> 0.83. Iteration 1's F002 (s29)
  was the same false positive. The labels stay what they are.
- **54.1:** the one finding on iteration 3's changes that was the cut's (the elder's cap across the
  frame's edge), fixed as above; the job before the fix, `job_1a0e3e7b8b77dc917`, had it.
- **Left as designed:** "1 element outweighs the visitor" in 81.1 (the suspension's locked-off wide:
  the saucer is small and 363 m away on purpose) and "the visitor partly hidden by the elder's cap" in
  89.1 ("from under the elder: the saucer over its rim"); "1 element outweighs Vane" in 29.1 (Vane
  watching the elder, which is the frame). The low "abrupt camera acceleration" findings fall on the
  cut instants, where a linear move's keys begin and end.

## Iteration 2, judged on the whole film

The iteration-2 render (`build/gv3/cut/it2/cut-it2.mov`, 960x540, 13,530 frames, exit 0, GPU errors
0, the scene loaded) through the adapter with the same flags as it1 and the before. Job
`job_1a0e3f6314a75aa5f` (track `film`, label `it2`; complete): **131 issues, 0 critical** (it1: 2),
4 high, 50 medium, 77 low. Comparisons in `build/gv3/cut/it2/cut-compare-*.txt`.

| Dimension | before (first pass, same engine) | it1 | it2 |
|---|---|---|---|
| composition | 0.637 | 0.647 | **0.763** |
| character staging | 0.868 | 0.918 | **0.934** |
| visual hierarchy | 0.746 | 0.708 | **0.773** |
| pacing | 0.969 | 0.979 | 0.984 |
| musical synchronization | 0.934 | 0.963 | 0.939 |
| cinematography | 0.742 | 0.698 | 0.680 |
| lighting | 0.787 | 0.756 | 0.802 |
| technical quality | 0.768 | 0.748 | 0.787 |
| temporal coherence | 0.909 | 0.876 | 0.924 |

**Cinematography, what drives it** (the dimension's own drivers): 38 of 73 shots include a push-in
(`movement_monoculture`); four repeated framings (41.1/117.1 is the rhyme kept on purpose; 9.1/26.1,
9.1/49.1 and 77.1/113.1 are not); path jitter in 59.1 (fixed in iteration 3) and 109.1; and 39 low
"abrupt camera acceleration" findings. Of those 39, **7 were real** -- each keyed move started on the
beat while its shot starts a frame earlier (`CUT_LEAD`), so it held still for one frame and set off
at full speed -- and **32 were the adapter's**: it wrote camera sample times to 4 decimals while shot
bounds carry 6, and the analyzer differentiated across a few microseconds. Fixed on both sides: moves
are keyed over the shot's on-screen span (`shots.build(lead=CUT_LEAD)`, iteration 4), and the adapter
writes 6 decimals (creative-critic `111a283`, the coordinator). On the scene alone, the 7 fixed
lifted cinematography 0.722 -> 0.755 (`job_1a0e3feaec6c9dfec`).

**Novelty:** 34.2 s held after the shots' last new information (it1 44.3, the before 24.5); 8 shots
static from the first frame (before 4). Six account for 17 s: 89.1, 91.1 (Ember hidden, so nothing
changes), 31.1, 65.1, 47.1 and 107.1. `trim.py` proposes 65.1 -2 beats and 89.1 -4, both static from
their first frame, where a trim gives only a shorter still: not applied; they want moves that change
the picture.

**Seen on the sheets** (`build/gv3/cut/it2/review/`, and `framing.py --video` on the render):
- 29.1: horse-20 walked past the lens and filled the left half of the frame (the herd grazes 5-8 m
  from Vane's path); the Critic: "vane is likely partly hidden by horse-20" and "outweighs vane".
- 91.1: Ember in frame by projection, seen in none of the 6 checked frames: the bottom-left corner,
  behind the ferns.
- 111.1: the lens went through a large plant for two thirds of the shot (Vane seen in 2 of 6).
- Flags only for clipping (the look's): 95.3's glowing horse 15.4%, 47.1 6.7%.

Not the cut's, recorded: clipped highlights (look), aliens floating (gv3-cast), five routes with no
visible response (look and gv3-cast), shimmer (render).

## Iteration 3, judged on clips

The three spans iteration 3 changed, rendered from the iteration-3 world (`render-it3.sh`, one lock
hold each: E2 24.4-30.1, E4 98.2-115.0, the riser 170.2-176.9; all exit 0, GPU errors 0), each judged
against the same range of the before on its own track (`clip-e2`, `clip-e4`, `clip-e5riser`):

| Clip | before -> it3 (critical/high/medium) | improved | degraded |
|---|---|---|---|
| E2 | 0/0/1 -> 0/0/3 | visual hierarchy 0.95 -> 1.00 | musical sync, effects, technical: the new beam's route, clipping and shimmer |
| E4 | 0/0/4 -> 0/0/12 | cinematography 0.975 -> 1.000, staging 0.97 -> 1.00, environment 0.94 -> 1.00 | musical sync, effects (the scout-beam routes); composition and hierarchy, below |
| riser | 1/2/11 -> 0/1/10 | composition 0.67 -> 0.82, hierarchy 0.80 -> 0.92, lighting 0.72 -> 0.81, motion 0.91 -> 0.96 | nothing measurable |

Every event in the clips is on screen at its instant (E2's crossing, E4's beam, lift, pair taken and
departure, E5's beam and lift). The before's clips have no set pieces, so the route checks on the
beams exist only in the after. **E4's hierarchy finding** ("bright areas away from the elder dominate"
in 54.1) is the adapter again: "scout" has no subject alias, so it takes the elder, named in the
label, for the subject and the scout's beam for a distraction.

**Seen in the clips:**
- **57.1's eye stood in a plant**: at 105.5 s its leaves fill the left and centre of the frame, the
  beam and a cow behind them (iteration 2's eye, kept in iteration 3).
- **E2's pan brings the moon into the corner**: at the crossing's first instant the saucer lies on the
  moon's bearing, so every frame that holds it then holds the moon too. The moon (the light rig's disc,
  azimuth 64, elevation 22) is in frame in 8 shots of the cut, e.g. small and bright in 59.1; in E2 it
  reads as a flat grey ellipse. A look matter, recorded; the pan stays (a 12-degree pan leaves the moon
  out and the saucer in 0.28 s late).

## Iteration 4: the alien shots on both casts, the column, the keys (`d7c57449` to `4442ca71`)

**Both casts.** gv3-cast's iteration-4 trace (`av-gen-gv3-cast/build/gv3/cast/iter4/trace.json`,
9a6c34f2) is its world as it will merge (until navfix and gv3-world's river): the set pieces' moments
are identical to the preview world's to the hundredth; the aliens differ. Every framing of iteration 4
is searched on both at once (`build/gv3/cut/tools/search4.py`): it must hold whichever merges. Every
E1-E5 moment the cut shows is on screen on both (`build/gv3/cut/castworld/`).

- **Keys on the on-screen span** (`shots.build(lead)`): all 42 keyed moves start and end on their
  shot's first and last frame.
- **29.1:** Vane crosses the east meadow; the lens rides 11 m north-east of her and 6 m up, looking
  south-west past her to the elder's cap on the horizon. Nearest animal 6.7 m from the lens, none in
  front of her, on both casts (her path is the same on both).
- **91.1:** a fixed eye 18 m up the west bank behind Ember, 3 m up, 32 mm, a live aim at her led 15%
  toward the saucer: both in frame for the whole shot on both casts, where she walks different ways
  26 m apart (on gv3-cast's, iteration 2's rig lost her entirely). She is 0.19-0.25 of the frame's
  height, a figure on the bank; the saucer settles beside the elder, which any view of it from the
  west holds too, so the subject names all three.
- **111.1:** Tide, not Vane, walks the north end toward the lit spire: a chase 5 m behind her, 3.4 m
  up, 24 mm, on her own path 76% of the shot. Her path is the same on both casts; Vane's is not, and on
  neither did Vane walk toward the horse's place.
- **57.1:** from 4 m over the river 42 m east of the station (no undergrowth over water), 28 mm aimed
  10 m below the scout: the whole column in frame at the lift on both casts, even with the saucer's
  larger bounds the adapter takes for a scout with no hero record (the preview world has none).
- The aliens lead 13.1% still: Vane 3 shots, Tide 3, Ember 2, Rook 1, Sage 1.

**Moves that change the picture, and no repeated framings** (`d9e4bf35`):
- `arc()`: a truck round a still subject, the eye swinging about the vertical through it at its own
  distance, the target held. For the heroes whose few-metre pushes read as static: 31.1 (14 deg),
  43.1 (a 6.5 m truck across the wide), 46.1 (16), 47.1 (25, rising round the stem), 53.1 (6), 65.1
  (8, rising), 104.1 (20, rising), 107.1 (14). 75.1 cranes up 4.5 m from the ferns, tilting up to the
  aurora. 89.1 swings 30 deg round the elder's stem with its live aim, so the saucer slides out from
  behind the rim.
- 9.1 repeated the east-bank travel (26.1) and the orbit (49.1): the Critic calls two shots the same
  framing when their eyes are within 4% of the valley's spread of eyes (18 m), their views within 12
  deg and their fields of view within 8. A first try from the north-east put the elder as near the lens
  as the horse and hid the horse behind a rock (seen in the clip); the fix is the south-east vantage
  through a 55 mm (`fc93b071`), the horse on the lower third (0.13 of the frame) under the whole cap,
  13 deg of field of view from the 35 mm travel and orbit.
- 113.1 from 12 m up the north-east slope, a 10 m truck, the line of sight to the elder's stem and cap
  clear of the ground from both ends: from the north it repeated 77.1.
- 57.1 at 28 mm, 91.1 larger (above); 113.1's label no longer says "ridge", which the adapter reads as
  the ridge mushroom (a critical for one scene job).

**On the scene** (fast jobs, inputs from the adapter at `44d1111`):

| | it3 (`job_1a0e3f09cc6ad3a9b`) | it4 (`job_1a0e41d4c4ede104e`) |
|---|---|---|
| issues (critical / high / medium) | 78 (0 / 3 / 16) | 35 (0 / 2 / 13) |
| shots with a push-in | 38 of 73 | 28 of 73 |
| repeated framings | 3 (one the rhyme) | 1 (the rhyme) |
| abrupt camera accelerations | 39 | 1 |
| cinematography | 0.722 | 0.951 |
| composition | 0.829 | 0.843 |
| visual hierarchy | 0.912 | 0.918 |
| pacing | 0.984 | 1.000 |

**Measured on its trace** (`build/gv3/cut/it4/cast-it4.json`; the cast bit-identical to iteration 3's,
the set pieces' beats too): **21 of 22 shots with a subject pass the stability bar**, the one failure
95.3's rise (ADR-913's exception). The re-framed ones: 9.1 0.031 / 0.056 deg pitch / yaw HF, 29.1
0.000 / 0.000 (eye height HF 0.88 cm), 57.1 0.016 / 0.016, 91.1 0.018 / 0.020, 111.1 0.000 / 0.000
(1.40 cm). Every set-piece moment the cut shows is on screen at its instant
(`build/gv3/cut/it4/cut-onscreen-it4.txt`: 26 samples on, the off ones the approaches and departures
left out by choice).

Left as designed: 81.1's saucer small and far, 89.1's saucer behind the rim at the swing's start; 9.1's
"horse partly hidden by the elder" is the cap's bounding box (which reaches the ground), not the stem,
23 deg clear of the horse from this eye.

### Iteration 4, judged on clips

Nine clips of the changed spans (`render-it4.sh`, one lock hold each, every one exit 0 with GPU
errors 0), each judged against the same range of the before (`critic-it4.sh`). The before's inputs
were rebuilt with the adapter as it now is (`44d1111`), and iteration 3's three clips were re-judged
with it too, so every pair is judged by one adapter. Comparisons: `build/gv3/cut/it4/cut-compare-*.txt`.

- **Staging, cinematography and motion improve or hold in every clip** (cinematography 1.00 in six).
- **What degrades is mostly not the cut's:** the routes on the scout's beam where it is not lit, and
  the elder's heartbeat route (musical synchronization, effects: look and cast), Vane floating (cast).
- **The cut's own findings, left as designed:** "bright areas away from the scout" in 54.1 and 59.1
  (the beam and the elder's cap around a small craft 45-118 m off); the elder small in 77.1's
  north-end wide; 89.1's saucer partly behind the rim at the start of the swing that brings it out;
  the intent's "avoid constant camera movement" beside 65.1 (whose push became an arc, not a new move).

**Seen in the clips** (frames in `build/gv3/cut/it4/`): 47.1's gills wheeling overhead, 53.1, 65.1's
spores across the cap, 75.1's crane to the aurora over the river, 89.1's swing clearing the saucer,
91.1's Ember small but seen on the bank with the elder and the settling saucer beyond (iteration 2 hid
her), and 57.1's whole column from over the river all read as meant. 29.1: Vane walks the lower left
with the elder's cap above the trees at first; by its end a near tree's canopy fills the right third:
the scatter is not in any trace, so only a render shows it. Kept, recorded. 9.1's first try: above.

## The merge: gv3-cast's world and engine-3 (`54db99c8`)

`gv3/production` `274fe408` (main `876a11e2` with render ADR-917-919 and the gpu-lock fix, and
`gv3/cast`) merged into `gv3/cut` with no conflicts; the shared build is engine-3
(`av-gen-engine-3/build/release`, `876a11e2`; its `BUILD-READY` marker was not there, but no compile
was running and every tool was built at 14:54). The pipeline is now the generator and then
`tools/gv3/ufo.py`, which compiles gv3-cast's E1-E5 plan into the project; the preview world
(`preview_world.py`, the E4 stand-in) is no longer needed.

**The Director and the recorded cut.** On the merged project the live Director cuts seven spans
differently (53.1, 55.1, 65.1, 66.1, 68.1, 89.1, 90.1); the generator keeps the recorded cut and says
so. The cause, from the Director's own reasons (`build/gv3/cut/cut-song-cut-live-merged.json` against
`song_cut.json`): not the engine (engine-2 and engine-3 give the same new cut on the merged project),
not the set-piece events (identical), but the span lengths' subject term, "x0.90 for how much
'lantern-cap' moves" against "x1.15 for how much 'rook' moves": Song Mode scales each span by how much
the subject its own Auto-director camera would follow moves, and gv3-cast's cast changed those
subjects. GV3 never uses that camera -- every span has an authored composition -- so the change says
nothing about the music or the set pieces. **The recorded cut stays** (`--recut` would re-time E4's
arrival and the break for a camera GV3 does not have, and would move again with navfix and the river).
For the song stream: an option to leave the subject term out, or to read the subjects from the
authored rigs, would make an authored production's cut depend only on the music and its plan.

**Re-checked after the merge** (the generator, then `ufo.py`, on engine-3; whole-film trace
`build/gv3/cut/merged/cast-merged.json`): all five set pieces play at the moments the cut was framed for
(E4 now in gv3-cast's own region, taking cow-23 and cow-12; the stand-in is retired), and every moment
the cut shows is on screen (26 samples on; off only the approaches and departures left out by
choice). Every traced subject is in frame for the whole of its shot (`framing-merged.md`). The follow
bar: 20 of 22 at first -- 91.1's live aim at a walking Ember gave 0.105 deg of yaw HF on the final cast
(0.020 on the preview's) -- so a `watch` kind (0.6 s, 1.2 s, half the lead) for a fixed eye watching a
walker (`e185371e`): 0.080, measured on a trace window whose cast matches the whole-film play exactly.
**21 of 22 pass on the merged project**, the one failure 95.3's rise (ADR-913's exception).

## Review material

`~/Desktop/av-gen-review/18-glowmere-valley-3/revision/cut/`:
- `pacing-before-after.png`: shot lengths over the film, the first pass against the Director's cut.
- `sheets-before-first-pass/`, `sheets-after-iteration-2/`: the whole film, three frames a shot
  (`review.py`), with each shot's measurements in `report.md`.
- `changed-shots/`: every span iterations 3 and 4 changed, the first pass's frame at the same film time
  beside the newest render's (the iteration-4 clips, 9.1's and 107.1's final clips, iteration 3's).
- `set-piece-moments/`: E1-E5, fifteen moments, before and after.

**Honest reading of the sheets:** most changed spans are clearly better (E2, E4, E5's riser, 9.1, 47.1,
57.1, 75.1, 91.1, 113.1). Two are not yet: **29.1** (Vane walks the meadow among trees that hide the
elder she was meant to be seen against) and **31.1** (the spire, centred by projection, reads as one
mushroom among many on a busy lit hillside). Both need render-and-look iterations: the scatter is in no
trace.

## State (2026-09-27, about 15:30): the stream's work is done

**Branch `gv3/cut`**, merged with `gv3/production` `274fe408` (`54db99c8`); last commit this log. The
shared build is engine-3 (`876a11e2`).

**The cut:** `tools/gv3/song_cut.json`, the Director's Song Mode cut, 73 spans (settings
`songcut.SETTINGS`, GV3's treatments `songcut.TREATMENTS`, the set pieces' moments as plan events);
every span an authored composition in `tools/gv3/shots.py`. Aliens lead 13.1% (the generator refuses
more than 20%). Follow rigs: 21 of 22 pass the stability bar on the merged project. No trims
(`songcut.TRIMS` is empty: the shots the novelty measure found holding were static from their first
frame, and got moves instead).

**Evaluator jobs** (session `gv3-cut`): the before `job_1a0e3b6d031dcf565`; it1
`job_1a0e399224ec50121`; it2 `job_1a0e3f6314a75aa5f` (whole film, 0 critical, composition 0.637 ->
0.763 against the before); the clip tracks `clip-<range>` (the before and each iteration per range);
scene-only jobs on track `scene`, the last `job_1a0e4352f9bf3c090`.

**Open, for others:**
- The song stream: Song Mode's span lengths depend on the subject its own Auto-director camera would
  follow, so a cast change moves an authored production's cut (above). An option to leave that term out.
- The critic-adapter: its camera model is the raw node (no ADR-911 filter); `followNode`/`aimNode` rigs
  could read the trace's own camera track instead. Fixed during this stream by the coordinator: the
  ember-cap alias, the camera sample-time rounding, the scout's alias.
- gv3-look: the moon's disc reads as a flat grey ellipse where it is in frame (8 shots, e.g. E2); the
  clipped highlights on the horse's glow (95.3) and 47.1.
- The app: a custom treatment's dials have no editor (they are tuned in `songcut.TREATMENTS`).
- The arrival's grand wide is 2 bars, not the plan's 4 (the Director's opener rule).
- 29.1 and 31.1 want render-and-look iterations (trees hide the elder; the spire reads weakly).
- A whole-film render of the final cut, then `trim.py` on its Critic job, for trims of shots that
  change and then hold.
