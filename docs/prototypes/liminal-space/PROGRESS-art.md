# Liminal Euclidean World: the art agent's progress notes

*Kept current after every step so a cold successor can resume. The engineering agent's notes are `PROGRESS-eng.md`;
these are the art side's. Newest state first.*

## RESUME HERE (Phase B: building and rendering the video, 2026-09-30 night)

The art agent owns the worktree alone in Phase B (the engineer handed back at `2083bab4`). Deadline: a full,
art-directed render by the morning of 2026-10-01; safety net at 05:30 (render the best committed state as
`all-you-got-WIP-<sha>.mp4` with a README).

- **The generator:** `tools/liminal/make_all_you_got.py` writes `examples/liminal/all-you-got{,.scene,.rig}.json`
  from the section map. `python3 tools/liminal/make_all_you_got.py` prints the chapter table (swap times, local
  distance ranges against each path's length) and a check list of where the camera is at the plan's moments.
- **Validate without the GPU** (catches scene load errors, unknown targets, dead tracks):
  `./build/release/src/avgen --project examples/liminal/all-you-got.json --audit-routes /tmp/audit.json 2>&1 | grep -i "warn\|error\|loaded"`
  must end with `0 warning(s)`. A scene that fails to load silently renders the default orb scene.
- **SDF limits that bite:** 96 nodes per tree, at most 8 children per union (the generator's `union` nests),
  8 nested point ops. `count_nodes()` asserts them per world.
- **Stills:** `tools/gpu-lock.sh ./build/release/src/avgen --headless --project examples/liminal/all-you-got.json
  --render <dir> --format png --range 1:253 --size 640x360 --fps 0.25` (64 frames, one every 4 s).
- **The GPU is shared** with the Sonic agent, whose renders hold the lock for 5-15 minutes at a time. Queue GPU
  work in the background and author meanwhile.
- **Scratch:** `/private/tmp/claude-501/-Users-natefaulkenberry-Documents-GitHub-av-gen/fed9412c-8e5e-42c0-a62b-e703644796ad/scratchpad/liminal/`
  (the Sonic agent uses the same scratchpad root; keep to the `liminal/` folder).

### Phase B status

- [x] Generator v1 (commit `88b15d6d`): seven worlds, nine chapters, the fitted walk, K0-K12, thresholds of
  light, sparse routes scaled by a timeline `coupling` source.
- [x] Stills across the song (`scratchpad/liminal/s2`, one every 4 s): the structure works end to end.
  Calibrated (`15709f1d`): lamp panels plaster when dark, hall air, quieter teal, a small sun, denser void.
- [x] Short test renders in `~/Desktop/av-gen-review/24-liminal-space/tests/`: `test-a-intro-hall.mp4`
  (0-62 s) and `test-b-letgo-verse2.mp4` (86-118 s), 1280x720.
  - `liminal_critic.py temporal`: 0 snaps, 0 flicker; aggression-activity correlation 0.90 (A); one minor
    colour-too-fast at the riser's white-out; "let it go" 36 % below its neighbours (good contrast).
  - Creative Critic (job `job_1a0f54d3da394134a`, preview, test A): clipped highlights at the thresholds (47 %
    of pixels), camera wobble (medium), shimmer in the hall (the motes read as snow), flat low-contrast intro;
    audio reactivity not evaluated (an error in the Critic's analyzer).
  - Revised (`cc43f1b3`): sun patches in the hall (SDF soft shadow), finer motes, windows glow, the riser's
    light across four bars, the stairhead looks out at the figure and the void, thresholds as luminous haze
    (fog x3, +1.7 EV) instead of clipped white, contrast 1.12, less bob.
- [x] Full render v1 (`cc43f1b3`, 22:33-22:45, 12 min at 10.6 fps):
  `~/Desktop/av-gen-review/24-liminal-space/all-you-got-v1.mp4`, 1920x1080, 30 fps, h264 q90, the song muxed.
  - temporal: 7 snaps (the chapter swaps; one major at the grow room), aggression-unanswered at bars 5-8 (the bass
    pulse entry), "let it go" only 8 % below its neighbours, 21 colour moves (minor findings at thresholds).
  - Critic (job `job_1a0f55a9841d09e0f`): camera wobble in 41 % of shots (the guard pushing at the enfilade's
    screw seams, plus the bob), clipped highlights at the thresholds and the sun, flat low-contrast intro.
  - My viewing: a giant half-disc sun (sunSize is radians), the mannequin seen close twice (stairhead, top), grain
    on the hall's door jambs (the SDF soft shadow), a lavender hall, the look-down at bar 108 hitting the landing.
  - **Lesson:** interior light is the procedural sky's IBL, so the palette's zenith/horizon/ground roles are the
    ambient colour of each place; the rig's ambient does nothing without an ambient-role light.
- [x] Final pass (`ed1dce5d`): thresholds as thick bright air (fog x7, +1.1 EV), guard radius 0.14, bob 3-7 mm, a
  0.7-degree sun inside the final doorway, daylight from behind, the figure far down the stair and 4 m away at the
  top, warm hall light, sun patches on the hall floor, a dimmer and emptier let-it-go, the first corridor visible
  at bar 108, a look round at the release.
- [ ] Full render v2 (running from about 22:57): `all-you-got-v2.mp4`; script `scratchpad/liminal/full-v2.sh`.
- [ ] Analysis of v2; if it is better, it becomes `all-you-got-final.mp4`.
- [ ] REPORT.md (the brief's 22 ten items) + copy in the review folder.

### How to analyse a render

```sh
S=/private/tmp/claude-501/-Users-natefaulkenberry-Documents-GitHub-av-gen/fed9412c-8e5e-42c0-a62b-e703644796ad/scratchpad/liminal
V=~/Desktop/av-gen-review/24-liminal-space/all-you-got-v1.mp4
python3 tools/liminal_critic.py temporal --video $V --audio ~/Desktop/"All You Got.wav" \
    --sections tools/liminal/all-you-got.sections.json --video-start 0 --out $S/temporal-v1
python3 tools/liminal_critic.py inputs --project examples/liminal/all-you-got.json \
    --sections tools/liminal/all-you-got.sections.json --video $V --video-start 0 --out $S/critic-v1
cd ../creative-critic && .venv/bin/critic submit --inputs $S/critic-v1/inputs.json --mode preview --session liminal --track v1 --wait
$S/sheet.sh $V $S/v1-sheet.png 5 384 1 10 20 ...   # contact sheets of chosen seconds (no labels: ffmpeg has no drawtext)
```

## Status (Phase A complete, 19:20 on 2026-09-30; handed back to the coordinator)

- [x] Brief and addendum read in full.
- [x] `ART-RESEARCH.md`: ten rules, sources with tags ([P]/[S]/[SR]/[M]), what is adopted and rejected, and what
  carries over from the Procedural Space POC's look.
- [x] `SONG-ANALYSIS.md`, with `tools/liminal/all-you-got.sections.json` and plots in
  `~/Desktop/av-gen-review/24-liminal-space/analysis/` (01-05 and `bars.json`).
  - 116 bars; 109 BPM to bar 75, 111 BPM from bar 76 (165.14 s), a step.
  - C Dorian (Cm-Cm-Gm-F), lifting to F at bar 67; the final loop is a descending Ab-G-F-F.
  - Three texture families: sparse (intro, break, "is that all you", outro), the C groove, and the F world
    ("feel it grow" previews the ending).
  - The "let it go" drop is one bar (42), then a stripped groove to bar 48; the only drums-out passage is bars
    74-75. Vocal lines are placed, the hook and verse 2 are confirmed on short windows.
- [x] `DIRECTOR-PLAN.md` covers:
  - the idea and the film at a glance;
  - the world, the four motifs (the beacon, the stair, the figure, the sun patch) and the wandering camera;
  - the colour script K0-K12 (the sky withheld until bar 92);
  - light, objects and effects;
  - the film section by section, and the timing sheet (64 timed camera events);
  - the sound-to-behaviour vocabulary and the transitions (light thresholds);
  - the climax strategy and the shot tests;
  - the cell sheet (six designs with dimensions);
  - **the ranked needs from engineering (17)**, the risks and fallbacks, and a LIVE note.
- [x] The colour script as data (`palette_keys`, `palette_timeline`, `intent`, `shots`, `synch_points` in the
  sections file) and as a picture: `tools/liminal/colour_script.py` →
  `~/Desktop/av-gen-review/24-liminal-space/plan/colour-script.png`.

- [x] Calibration notes against the engineer's first example (`DIRECTOR-PLAN.md` §12b), and the needs statuses
  checked against ADR-1040 to 1043 as landed (§13).

Phase A commits, oldest first:
- `fb6fd9e7`
- `10705f89`
- `ab58d5d0`
- `0796ebf0`
- `1842620e`
- `eecaaf9c`
- `68540563`
- `12a8e3ec`
- `6fc4359b`
- `c9c63462`
- `22f3ce1a`
- `a9b688fd`
- and this notes update.

## Next (Phase B, a new handoff: implement the video on the engineer's infrastructure)

1. Read `ENGINEERING.md` and `PROGRESS-eng.md` for what landed. The engineer's vocabulary is `tools/liminal_sdf.py`
   and its example generator is `tools/make_liminal_example.py`; `examples/liminal/` is theirs.
2. Check the ranked needs (DIRECTOR-PLAN §13) against what landed, especially:
   - chapters (several worlds along one journey);
   - more than one material per world;
   - a look-at blend;
   - the sky;
   - the figure.

   Take the §14 fallbacks where a need is missing.
3. Write a generator `tools/liminal/make_all_you_got.py` that emits the music-video project from the sections
   file: cells A, B, C, D, P and E; journey keys from `shots`; palette states from `palette_keys` (converted from
   sRGB hex to LINEAR RGB); palette position from `palette_timeline`; routes with `spring` and `integrate` per §8.
4. Do short test renders of the four moments that carry the film: bar 18 (the hall), bar 42 (the stop), bars
   67-76 (the growth and the loop's start), and bar 92 (the release). Run them through the GPU lock and put them
   in `~/Desktop/av-gen-review/24-liminal-space/`. Then do the full render and the analyzer passes.
