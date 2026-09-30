# Liminal Euclidean World: the art agent's progress notes

*Kept current after every step so a cold successor can resume. The engineering agent's notes are `PROGRESS.md`;
these are the art side's. Newest state first.*

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

## Status (Phase A: research, song analysis, director plan; hand back by about 21:30 on 2026-09-30)

- [x] Brief and addendum read in full.
- [x] Research launched as two background research passes (visual/camera; audiovisual/colour), re-weighted to the
  addendum's emotional direction (loneliness, wandering, repetition, colour returning as feeling).
- [x] Song analysis, first pass: the WAV's cue chunk carries `Tempo: 109.0` at sample 0 and `Tempo: 111.0` at
  sample 7,926,605 (165.138 s), exactly the start of bar 76. The song is exactly 116 bars: 75 at 109 BPM and 41 at
  111 BPM. A line fitted to the kick onsets gives 108.99 and 110.99 BPM, so the markers are right and the downbeat is
  at 0 s.
- [ ] Lyric timing (local recogniser) and section map; `SONG-ANALYSIS.md`.
- [ ] `ART-RESEARCH.md` from the research reports, with adoptions.
- [ ] `DIRECTOR-PLAN.md` with the needs-from-engineering list.

## Next

1. Finish the section map from the per-bar table plus the lyric timing; write `tools/liminal/all-you-got.sections.json`
   and run `song_analysis.py --sections ... --lyrics ...` for the plots.
2. Write and commit `SONG-ANALYSIS.md` (draft first).
3. Write `ART-RESEARCH.md` when the research reports come back.
4. Write `DIRECTOR-PLAN.md`.
