# ADR-923: The cut report, and the plan named for a run is the plan cut

**Status:** Accepted
**Date:** 2026-09-26
**Found by:** the GV3 revision's wave 2 brief ("GV3 consumes each stream through JSON, so each stream
ends with a headless dump that a Python generator can read"), and, building it, the headless path GV3
depends on -- `avgen --project X --director mode=song --song-plan P --save-project OUT` -- which stored
`P` and never cut it on any project with sections.
**Follows:** ADR-249 (`--song-plan`), ADR-921 (the timing and its reasons), ADR-922 (peaks and events)
**Implemented by:** `SongDirection::report` (`src/app/song_director.cpp`); `directEngine`'s explicit
plan and direction out-parameters, `applyDirectorArgs` moved out of the application
(`src/app/camera_director.hpp/.cpp`); `--cut-report` and the command-line plan in
`Application::directCameraFromTrack` (`src/app/application.cpp`); `avgen_song_cut` (`tools/song_cut.cpp`)
**Tests:** `tests/unit/test_song_director.cpp` -- "The cut report carries every shot's span, subject,
arc and reason", "A plan handed to the director for a run is the plan it cuts"; the Rebuild tests read
their statistics from the report

## Context

Glowmere Valley 3's generator (`tools/gv3/`) builds its cut in Python and reads the engine's
decisions as JSON. Song Mode's decisions lived in `SongDecision`s and a debug log line: nothing a
generator could read, and no reason for any duration.

`songPlanForEngine` puts the film's own sections first -- a project's section timeline is what a
person is editing, and a saved plan is a snapshot of it (ADR-249's later fix). `--song-plan` loaded
its file into the engine's saved plan and then called the same function, so on any project with a
section timeline -- GV3's has thirteen -- the plan on the command line was saved and never cut,
though the code's own comment said "a plan on the command line replaces the project's".

## Decision

**The report** (`SongDirection::report()`, format `avgen-song-cut` version 1):

```
{ "format": "avgen-song-cut", "version": 1, "plan": name,
  "settings": {shortestShot, longestShot, shortestBuild, seed, autonomy},
  "grid": {beats, beatSeconds, tempoBpm, beatsPerBar, phraseBars, downbeatBeat, firstDownbeatSeconds} | null,
  "sections": [{index, label, intent, arc, authored: [start, end], start, end, density, musicEnergy,
                push, cutRate: [open, close], peak, peakSubject?, peakEvent?, firstShot, shots,
                durations: {min, mean, median, max, cv}, lengths: [...]}],
  "shots":    [{index, section, start, end, duration, beats, startBar, startBeat, subject, handoff,
                subjectReason, camera, cameraId, kind, transition, visible, arc, intent, peak,
                aim, rate, density, motion, scale, contrast, endsOn, why}],
  "cuts":     [{seconds, visible, sectionBoundary, bar, beat, onBeat, onDownbeat, gridErrorMs}],
  "stats":    {shots, cuts, visibleCuts, cutsOnBeat, cutsOnDownbeat, min, mean, median, max, cv,
               modalShare, camerasUsed},
  "warnings": [...] }
```

`why` is one sentence per shot ("rising: shot 3 of 4, the pace running from 4.63 s to 0.67 s shots
(cut rate 0.34 -> 0.70, floor 0.46 s); ends on the half-bar line at bar 96 beat 3"); `aim`, `rate`,
`density`, `motion`, `scale` and `contrast` are the rule's terms as they applied (ADR-921); `endsOn` is
what the shot's end landed on (`phrase`, `half-phrase`, `bar`, `half-bar`, `beat`, `section`, `end`,
or `free` without a grid); `modalShare` is the audit's measure of sameness, the largest share of shots
within 50 ms of one length.

**Two ways to get it, one cut.**

- `avgen --project X --director mode=song[,k=v...] [--song-plan P] --cut-report OUT.json
  [--save-project OUT] [--save-scene OUT]` -- the existing headless path, now writing the report after
  directing. Refused without `--direct`/`--director`, and refused for a mode that is not Song.
- `avgen_song_cut --project X [--song-plan P] [--director k=v,...] [--out OUT.json] [--save-project
  OUT] [--save-scene OUT]` -- the same cut in the offline engine on the CPU: no window, no GPU, no GPU
  lock, about 5 s for GV3 (4 s of it the project load and audio analysis; the cut takes 8 ms). The
  project's own Auto-director settings, `--director`'s over them, Song Mode forced.

Both parse `--director` through one function, `applyDirectorArgs`, moved from the application into
`camera_director`, so a setting has one spelling.

**The plan named for a run is the plan cut.** `directEngine` takes an optional plan and directs it
instead of `songPlanForEngine`'s; `--song-plan` passes its plan for the run's first cut only -- the
project keeps it as its saved plan, and later interactive re-cuts (Enable, a section edit) follow the
usual precedence, so a person's edits to the film's sections still take over. A plan handed in with no
events of its own still hears the film's (ADR-922). The camera track a Song Mode cut writes lives in
the scene document (ADR-245), so a headless run that should keep it saves the scene too.

## Consequences

- GV3's generator can read, per shot, where it starts and ends in seconds and in bars, who it is of
  and why, what its end landed on, and whether the cut is visible; per section, how it was cut; for the
  film, how many cuts land on a downbeat.
- **Changed behaviour:** `avgen --song-plan P` on a project with a section timeline now cuts `P` (it
  used to cut the timeline). No tracked project or script passes `--song-plan`; GV3's plan to is what
  this makes work.
- `avgen --cut-report` with no `--direct` or `--director` is refused at parse time, as
  `--post-stages` without `--render` is.
- The report's shape is versioned (`version: 1`); a field added later is additive.
