# Song stream (ADRs 920–923): the agent's final report

Reported on 2026-09-26. Branch `agent/song`, final commit `388e0c6c` (10 commits on `e0657a26`).
The coordinator merged it into `integrate/revision` as `819d992a` (one index conflict). This is the
agent's report, lightly trimmed. The GV3 cut reports are in
[../audit/data/song/](../audit/data/song/): `final-gv3-cut-recommended.json` with the recommended
settings, and `final-gv3-cut-gv3band.json` with GV3's current band.

## What changed
- **Cuts:**
  - every cut is on the beat grid, and every section boundary on its downbeat (the engine's meter, ADR-896);
  - shot lengths come from the music and the shot: section density, subject motion, and whether a shot establishes scale;
  - Rising accelerates, Burst opens short and settles, Suspended holds. Rising and Burst may cut below "shortest shot";
  - the drop opens its own hard-cut shot on its downbeat.
- **Peaks:** a peak section opens on its event's subject, or on the film's hero, instead of the rotation.
- **Emphasis:** `camera/focus/emphasis` and `ShotSpan::emphasis` are cut.
- **Headless cut report:** `avgen_song_cut` runs on the CPU in about 5 s, and the app writes the same report with `--cut-report`.
- **`--song-plan` now actually cuts the plan it names.** Before, it was never cut on a project with sections.

## Where an artist finds the controls
- **Auto-director panel → Shot mode: Song → Shot timing → "shortest build":** the floor a rising section cuts down to (0.25 s minimum; never under one beat).
- **Auto-director panel → song section rows:** the last cut, e.g. "[6 shots, 1.8-3.7 s, rising]" or "peak on elder".
- **Sequence panel → section inspector → "cuts: …":** what the arc does, and "cut rate X% at its start, Y% at its end".

## Suites
- **CPU:** 3,576 cases, 3,555 passed, 19 skipped, 1 expected failure, 1 failure. The failure is the multicam "loads cleanly" test, which main `808f32e4` fixes.
- **GPU:** not run, since no rendering code changed.

## The Director's cut of GV3 on Rebuild (recommended settings)
- **Overall:** 72 shots and 71 cuts. All 71 are on beats and 64 on bar lines; all 12 section boundaries are on downbeats (mean error 8.9 ms, worst 18.9 ms).
- **Lengths:** CV 0.71, median 2.77 s, range 0.92–14.77 s.
- **Riser:** it cuts 8 → 4 → 2 → 2 beats after the break's 16-beat hold.
- **Drop:** a hard cut on bar 97 (177.73 s, one frame after the true downbeat), opening on `visitor` and held to bar 105.

| Section | Shots | Min / mean / max (s) |
|---|---|---|
| cold open | 1 | 7.87 |
| riff groove | 6 | 1.85 / 3.08 / 3.69 |
| pull-back | 1 | 3.70 |
| groove 2 | 4 | 7.38 |
| lift | 9 | 0.92 / 1.64 / 3.70 |
| arrival | 7 | 0.92 / 2.11 / 3.69 |
| plateau | 12 | 1.85 / 3.69 / 5.54 |
| lead forward | 7 | 0.92 / 2.11 / 3.69 |
| suspension | 1 | 14.77 |
| break | 1 | 7.39 |
| riser | 4 | 0.92 / 1.85 / 3.69 |
| drop | 18 | 1.84 / 2.46 / 3.69 |
| tail | 1 | 3.35 |

**With GV3's current band** (4.6–6.5 s, shortest build 2.0 s): 44 shots, CV 0.28, and no
acceleration in the riser. That band allows only 10–14 beats, which is why the settings should change.

## How GV3 should use it
- **Settings:** in the project's `autoDirector` block, `"mode":"song"`, `"autonomy":"expressive"`, `"minShot":1.8`, `"maxShot":7.5`, `"minBuildShot":0.45`.
- **Cameras:** set `"autoDirector": true` on at least camera 1 (Main) in the scene; GV3 has none eligible today.
- **Events:** add them to a song plan as `"events":[{"name","subject":<a hero>,"seconds","end"}]`. `horse-11` is not a hero, so make it one if the drop or the lift should go to it.
- **Command (CPU, about 5 s):**
  `build/release/tools/avgen_song_cut --project examples/world/glowmere-valley-3.json --director autonomy=expressive,minShot=1.8,maxShot=7.5,minBuildShot=0.45 [--song-plan P.json] --out cut.json [--save-project OUT.json --save-scene OUT.scene.json]`
- **Report** (`format` `avgen-song-cut`, version 1):
  - `settings`;
  - `grid`;
  - `sections[]`, each with its arc, cut rate, peak, and duration statistics;
  - `shots[]`, each with start, end, beats, bar and beat, subject, camera, kind, transition, arc, `endsOn`, and `why`;
  - `cuts[]`, each with `onBeat`, `onDownbeat` and `gridErrorMs`;
  - `stats`;
  - `warnings[]`.
- **Previews against finals:** no difference. If GV3 keeps its one-frame `CUT_LEAD`, subtract 1/60 s itself.

## Defects found, not fixed
- The engine's grid sits about 9 ms late on average (worst 18.9 ms), so 9 of 12 section cuts show one 60 fps frame late (ADR-896).
- GV3's scene has no camera available to the Auto-director, and its project is mode "edited", autonomy "locked".
- `horse-11` is not a hero.
- Fills and kick gaps within a section do not move cuts.
- 562 `"emphasis"` keys in 14 tracked projects are no longer read; they were left in place (the multicam file is off-limits).
- Catch2's default random order makes manual shards overlap, so use ctest.
