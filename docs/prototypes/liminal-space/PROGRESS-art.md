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

- [x] Generator v1: seven worlds (corridor with storeys repeating forever, hall, enfilade, void stairhead, grow
  room, Penrose stairwell, the open), nine chapters, the walk integrated from a speed profile and fitted to the
  plan's timing sheet, the K0-K12 palette, thresholds of light at every swap, sparse routes (bass breathing,
  voice to beacon, treble tremble) scaled by a timeline `coupling` source.
- [ ] Stills across the song; look calibration (luminous air, plaster, no edges).
- [ ] Short test render (intro into bar 18; "let it go" into the build) + `liminal_critic.py temporal` + Critic.
- [ ] Full render v1 -> `~/Desktop/av-gen-review/24-liminal-space/all-you-got-v1.mp4`.
- [ ] Analysis, final pass, `all-you-got-final.mp4`.
- [ ] REPORT.md (the brief's 22 ten items) + copy in the review folder.

## Where things are

- **Worktree:** `/Users/natefaulkenberry/Documents/GitHub/av-gen-liminal`, branch `proto/liminal-space`. Commit
  only the art paths with `git commit -- <paths>`: `docs/prototypes/liminal-space/{ART-RESEARCH,SONG-ANALYSIS,
  DIRECTOR-PLAN,PROGRESS-art}.md` and `tools/liminal/`. The engineer builds and renders in the same worktree; the
  art side does not build or render in Phase A.
- **Governing documents:** `00-brief.md` (the owner's brief and lyrics) and `01-addendum-emotion.md` (the owner's
  emotional art direction; it wins where they differ). Read both in full before changing the plan.
- **The song:** `~/Desktop/All You Got.wav`. Never commit, copy, cache or upload it. Refer to it by path.
- **Analysis tools:** `tools/liminal/song_analysis.py` (numpy/scipy/matplotlib: tempo map, bar grid, per-bar
  features, self-similarity, novelty, plots) and `tools/liminal/lyric_times.py` (faster-whisper on the CPU, local,
  for timing the sung phrases). A scratch venv with both sets of dependencies is at
  `/private/tmp/claude-501/-Users-natefaulkenberry-Documents-GitHub-av-gen/fed9412c-8e5e-42c0-a62b-e703644796ad/scratchpad/venv`
  (it may be gone in a new session: `python3.12 -m venv v && v/bin/pip install numpy scipy matplotlib faster-whisper`;
  the uv Python is `~/.local/bin/python3.12`). Note: faster-whisper's own decoder fails with the installed PyAV,
  so `lyric_times.py` decodes with scipy and passes an array.
- **Review media:** `~/Desktop/av-gen-review/24-liminal-space/analysis/` (plots and `bars.json`).

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
