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
weakest composition 0.65, cinematography 0.70, visual hierarchy 0.71. (The baseline job, the first
pass's 1080p final with its own inputs, is not like for like; the same-engine "before" is rendering.)

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
  fix), E2's crossing at its first instant (the craft enters the frame a second later and crosses it
  for 1.7 s), E3's animal taken (fixed in iteration 2: 39.1).
- **Stability, and where the Critic and the pose track disagree.** The Critic's pixel classes: 3
  "shaky" and 5 "move with wobble" of 73, against 10 and 2 of 40 in the baseline job. Two of the three
  "shaky" (66.3 Sage, 111.1 Vane) are walker follows the pose track measures as steady (eye height HF
  0.84 and 0.89 cm, pitch HF 0.002 and 0.000 deg), and 111.1's pixel measure is identical to the first
  pass's s38 (0.00211) although its camera's pitch HF fell from 0.225 deg to 0.000. The image's global
  motion is contaminated by a large walking subject bobbing in a steady frame; the pose track, which
  reads the camera itself, is the measure of the camera. The Critic's scene-side "camera path jitter"
  (s35, s50, s69) comes from the adapter's own model of the rigs, which does not implement ADR-911's
  filter.
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
live at the scout and follow it.

## State at the session handoff (2026-09-27, about 12:00)

**Commits on `gv3/cut`:** `a50327ac` (the Director step, the compositions, ADR-913 kinds),
`2b7fd781` (this log), `e5c7a050` (shot ids s01.. as the Critic's adapter names them), `27e18605`
(E1 and E4 framing), and the checkpoint commit with `tools/gv3/trim.py` and `preview_world.py`.

**The Director's cut in use:** `tools/gv3/song_cut.json`, 73 spans, settings `songcut.SETTINGS`
(song, expressive, 1.8 / 7.5 / 0.45 s, seed 1), GV3's treatments `songcut.TREATMENTS`. Every span has
an authored composition in `tools/gv3/shots.py`; `python3 tools/make_glowmere_valley_3.py` reports
"re-run and identical to song_cut.json". No trims yet (`songcut.TRIMS` is empty).

**In flight when the session ended** (both from `build/gv3/cut/render-it1.sh`, under the GPU lock):
- `build/gv3/cut/it1/cut-it1.mov`, the iteration-1 preview, 960x540, full film (log `it1/cut-render-it1.log`);
- then `build/gv3/cut/baseline2/cut-baseline.mov`, the first pass's cut on the same engine (the "before").

**No evaluator job of the new cut yet.**

**Open questions:**
- E4's place (gv3-cast): see above.
- The arrival's grand wide is 2 bars, not the plan's 4 (the Director's opener rule).
- GV3's treatments are named and selectable in the Sequence panel, but a custom treatment's dials have
  no editor in the app; they are tuned in `songcut.TREATMENTS`. A UI gap to report, not to fix here.

**Next steps, in order:**
1. When `cut-it1.mov` exists, regenerate the Critic's inputs with the adapter (INTEGRATION_GUIDE §6),
   from the preview project and its trace:
   `~/Documents/GitHub/creative-critic/.venv/bin/python ~/Documents/GitHub/creative-critic/adapters/avgen/avgen_adapter.py --project build/gv3/cut/it1/ufo.json --cast build/gv3/cut/it1/cast-it1.json --lightrig examples/lightrigs/glowmere-valley.rig.json --world-preview build/release/tools/avgen_world_preview --climax drop --video build/gv3/cut/it1/cut-it1.mov --width 960 --height 540 --out build/gv3/cut/it1/critic`
   (no `--shot-plan` for it1: the render predates the s-ids; from iteration 2 on, regenerate and pass
   `docs/glowmere-valley-3/04-shot-plan.md` and `03-directives.md`). Submit `critic submit --inputs
   build/gv3/cut/it1/critic/inputs.json --mode preview --session gv3-cut --track film --label it1
   --wait --json` in the background; also `--mode fast`.
2. Read `measurements.events.types.*.framing[].on_screen` for every E1-E5 moment, and
   `measurements.novelty`: `python3 tools/gv3/trim.py <report.json>` proposes `songcut.TRIMS`.
3. Look at every shot: `python3 tools/gv3/review.py build/gv3/cut/it1/cut-it1.mov build/gv3/cut/it1/shots-it1.json build/gv3/cut/it1/review`
   and `tools/gv3/cuts.py` the same way; re-author compositions that do not read (the new hero
   vantages -- ridge 40.1, ember-cap 48.3, scree, veil, spire, cairn -- are unscouted).
4. The "before": the baseline render through the adapter (project `build/gv3/cut/baseline/gv3-baseline.json`,
   trace `build/gv3/cut/baseline2/cast-baseline.json`), label `baseline-ec515c8b`, then
   `critic compare <before> <after>`.
5. Stability: the craft kind's smoothing for hovering and departing crafts (94.1, 101.1): try a longer
   `followSmoothSeconds` for those two, re-trace their windows with `--start`.
6. Iteration 2: clips of the changed spans through the lock (7.1 for E1; E4's spans once gv3-cast
   places it), each submitted with `--video-start`.
7. Before/after stills and sheets to `~/Desktop/av-gen-review/18-glowmere-valley-3/revision/cut/`.
8. The final report (briefs.md: common rules, and gv3-cut).
