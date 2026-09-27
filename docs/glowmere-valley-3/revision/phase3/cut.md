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

Measured: pending (render and trace queued).
