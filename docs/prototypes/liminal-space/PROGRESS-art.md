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
- [x] `ART-RESEARCH.md` committed (`10705f89`): ten rules, sources with tags, adoptions and rejections.
- [x] `SONG-ANALYSIS.md` committed (`ab58d5d0`) with `tools/liminal/all-you-got.sections.json` (14 sections, bars
  and seconds, placed vocal lines) and plots in `~/Desktop/av-gen-review/24-liminal-space/analysis/`.
  - 116 bars; 109 BPM to bar 75, 111 BPM from bar 76 (165.14 s), a step.
  - Key C Dorian (Cm-Cm-Gm-F), lifting to F at bar 67; the final loop is a descending Ab-G-F-F.
  - Three texture families: sparse (intro, break, "is that all you", outro), the C groove, and the F world
    ("feel it grow" previews the ending).
  - The "let it go" drop is one bar (42); the only drums-out passage is bars 74-75.
- [ ] `DIRECTOR-PLAN.md` with the ranked needs-from-engineering list (in progress, 19:00).

## Next

1. Write and commit `DIRECTOR-PLAN.md` (draft first, then refine).
2. Hand back to the coordinator by about 21:30 with the research adoptions, the section table, the plan per
   section, the ranked needs list and the commit shas.
3. Phase B (a new handoff): implement the video on the engineer's infrastructure (journey camera, palette block,
   stairs/screw/warp, spring/integrate), test renders, analyzer passes.
