# ADR-1630: A source says what it is, and only a drums source names a hit

- Superseded in part by ADR-1784 (2026-10-26): no role names a hit; the drums role runs no classifier.
- Status: Accepted (eng/stem-roles; the stem-analysis milestone's recommendations 1, 2 and 4,
  `docs/prototypes/living-prism/17-stem-analysis-milestone.md` §4; merge after the owner's approval)
- Extends: ADR-1520 (stems), ADR-1521 (live input sources), ADR-1580 (feeds), ADR-1602 (`signal.*`), ADR-1620 (a mix
  has no kick).
- Supersedes in part: ADR-1520 and ADR-1521's per-source `hits` flag. It is removed with no shim (ADR-441).
- Code:
  - `src/audio/source_role.hpp`: `SourceRole` (`drums`, `bass`, `synth`, `vocal`, `other`), `classifiesHits`;
  - `src/audio/stem_source.hpp`, `src/audio/input_source.hpp`: `role` replaces `hits`, and `hits()` is `role ==
    drums`;
  - `src/analysis/stem_analysis.{hpp,cpp}`: `StemAnalysisConfig::classes` (with `hits`, whether kick/snare/hat reach
    the frame); `src/analysis/channel_analysis.{hpp,cpp}`: `Source::classes`;
  - `src/signals/stem_signals.{hpp,cpp}`: the class names are declared for a drums source only; `declareRole`,
    `roleNames` and `copyFrom` provide `role.<role>.*`;
  - `src/app/engine.{hpp,cpp}`: `stemHitsTier`, `declareRole`, `publishRoles` (the play path, the input path and the
    seek replay, whose role ids are samplable), `feedStem` and `liveSourceIndex` (feed keys by key or by role), and the
    class refusal in `rebuildFeeds` and `rebuildChannelAnalysis`. The project reads and writes `role`, and `hits` is a
    load warning;
  - `src/audio/feeds.{hpp,cpp}`: `"role.<role>"` values (`feedRole`, `roleFeedKey`) and `feedHitIsClass`;
  - `src/app/settings.cpp`: `live.inputSources[].role`; `src/app/application.cpp`: `--input-sources
    <key>@<ch>[:<role>]` (the numeric form's `h` now means the drums role);
  - `src/ui/live_panel.cpp`: the Inputs table's **Role** column replaces **Hits**; `src/ui/sequence_panel.cpp`: the
    stem's role; `src/ui/feeds_row.cpp`: `roleCombo`, and roles in the feed lists;
  - `tools/drum_signals.py`: `signal.kick(Env)` reads `role.drums.kick`; new `signal.bassLevel` and
    `signal.synthLevel`;
  - `src/app/sonic_trace_cli.cpp` and `--sonic-live-log` include the `role.*` columns.
- Tests: `[adr1630]` in `tests/integration/test_stem_roles.cpp`, and the role edits in `test_stem_feeds.cpp`,
  `test_stem_signals.cpp`, `test_stems_offline.cpp` and `test_app_settings.cpp`.
- Probe: `docs/prototypes/living-prism/probes/B5-stem-roles/` (`make_projects.py`, `analyse.py`).

## Problem

The milestone report (17 §3, gaps 1 and 2) found three problems:

- **Identity and activity were one vocabulary.** Any source with `hits` on published `stem.<key>.kick/snare/hat`,
  including a bass or synth stem. Measured on Feline with `hits` on every stem (B5, main): the bass stem published 379
  "kicks" (0.22 precise against the drum stem's) and the synth stem 42 "hats". Nothing in a project said "this source
  is drums".
- **The key name stood in for the identity.** `signal.kick` and the drum feeds found the drums by the key `drums`. A
  drum bus keyed `kit` or `in3` was invisible to every scene.
- **A drums source without `hits` was a silent kick.** `signal.kick` picks an active `stem.drums.kick`, and without the
  classifier it never fires. Measured: 0 kicks over the whole song with the three stems and no flag, against 406 with
  it.

## Decision

**A source has a role, and the role gates measured identity.**

- `role` is one of `drums`, `bass`, `synth`, `vocal`, `other`. It applies to stems (`assets.stems[].role`) and live
  input sources (`live.inputSources[].role`, and the Inputs table's Role column).
- It is explicit only. With no role, a source is `other`. AV Gen never infers the role from the key or the audio. An
  unknown role is a load warning and reads as `other`.
- **Only a drums source runs the kick/snare/hat classifier, and it always does** (recommendation 2: no flag, no silent
  kick). Only a drums source has `stem.<key>.kick/snare/hat` on the bus.
- **Every other role publishes activity:** onsets, levels, bands and the onset register. Never a class name.
- A non-drums source that a response feed names still runs the hits tier, for the feed's `low`, `onset` and levels.
  The classifier's verdicts on it go nowhere.
- `hits` is gone, in the project and in settings. A project that still has it gets a load warning saying what
  replaced it. Nothing is inferred from it.

**`role.<role>.*` publishes the first active source of each role.**

- It is the same vocabulary as `stem.<key>.*`, copied value for value and event for event.
- **Order:** live input sources first, while an input is open (the stems read inactive then), then the stems, each in
  list order.
- It reads inactive when no source of the role is active.
- It is declared once a source has the role, and stays declared (the bus cannot undeclare). The class names exist on
  `role.drums` only.
- It is a pure function of the stems at the instant, so a seek's replay rebuilds it exactly as a play publishes it.

This is the one new signal family. It exists because a role cannot be keyed any other way: live, the keys are the
performer's. Without it, "the drum source" could only be found by key, which is gap 1.

**Feeds can name a role.**

- `"hits": "role.drums"` is the first source of that role: offline the first stem, live the first input source.
- A class (kick, snare, hat) fed from a source whose role is not `drums` is refused with a project warning. The kick
  then fires nothing; the snare and hat read the mix. `low`, `onset` and the levels take any role.

**The source-aware signals key on the role** (`tools/drum_signals.py`; recommendation 4):

| signal | with a source of the role active | otherwise |
|---|---|---|
| `signal.kick`, `kickEnv` | `role.drums.kick` | `beat.pulse` x 0.7 while `signal.drumming` (ADR-1620) |
| `signal.bassLevel` | `role.bass.level` | `response.bass` |
| `signal.synthLevel` | `role.synth.level` | `response.sustain` (the mix's sustained, non-percussive energy) |

## Evidence (B5: Feline Footwear and its stems, 60 fps; the reference is the drum stem's own classifier)

| run | `signal.kick` | the drum stem's classes | class names on the bass / synth stems |
|---|---|---|---|
| main, `hits` on every stem | 406 (1.00 / 1.00) | 406 / 235 / 545 | bass: 379 kicks, 5 snares, 14 hats; synth: 7 / 7 / 42 |
| main, the three stems without `hits` | **0** (the silent kick) | none | none |
| main, no stems | 358 (0.68 / 0.60, the stand-in) | | |
| this branch, roles only (no flag anywhere) | **406 (1.00 / 1.00)** | 406 / 235 / 545 | **not on the bus** |
| this branch, the drum stem a silent file | 0: the stem is active and silent, so the stand-in stands aside | none | not on the bus |
| this branch, no stems | 358 (0.68 / 0.60), as main | | |

**`role.<role>.*`:**
- On the roles run, `role.drums.kick/snare/hat` are 406 / 235 / 545, event for event the drum stem's.
- `role.bass.onset` (465) and `role.synth.onset` (296) are the stems' own.
- `[adr1630]` checks `role.drums.*` against `stem.<key>.*` on every frame, with the drum stem keyed `kit`, and checks
  that a seek's replay equals the play.
- **Live** (the synthetic 8-channel device): `role.drums.*` and `role.bass.*` follow input sources keyed `kit` and
  `click`. The bass input has no class names.

**The per-role levels:**
- With the stems, `signal.bassLevel` and `signal.synthLevel` correlate 1.000 with `stem.bass.level` and
  `stem.synth.level`.
- With no stems, they correlate 1.000 with `response.bass` and `response.sustain`.
- The stems' levels correlate only 0.54 and 0.47 with those mix stand-ins, which is why a scene names the idea once.

**Nothing else moved** (`--sonic-trace --trace-finals`, 30 fps, the whole song):

| project | before (main) | after (roles) |
|---|---|---|
| Living Prism offline (Feline, 3 stems) | 256 columns | 250 shared columns identical on all 6,317 frames, all 4 route finals included |
| the owner's Feline Footwear (Rainbow World offline, 3 stems) | 886 columns | 880 shared columns identical on all 6,317 frames, all 630 route finals included |
| Event Horizon (live, migrated to `role.drums.kick`, played on the Feline mix) | 255 columns | 255 of 255 identical, all 64 route finals included |

- The only columns that differ are the six bass and synth class names that left the bus, and the new `role.*`
  columns.
- **`--audit-routes` over all 45 live projects:** every summary is identical on main and on this branch: 3,256
  routes, 575 dead, 513 hazard and 2,168 live. Tracks, effects and every rule's count are identical too. There are no
  new dead routes.

## Migration (no shim, ADR-441)

- **The 35 projects** that read `signal.kick` from the drum signals: the generator's `stem.drums.kick` input becomes
  `role.drums.kick` (39 lines). The Sonic VFX (30 files) and Sonic Abstract (19 files) generators regenerate them byte
  for byte. The rest (Chorus Field, Digital Mosh, Cymatic World and the flow experiments) take the same token from the
  same module.
- **The projects with stems:**
  - Living Prism offline (`build_scene.py` and its project) gains `"role"` on each stem;
  - so does the owner's Feline Footwear project on the Desktop (`make_project.py` and both projects, with
    `*.bak-pre-role` copies beside them).

  Neither reads a class name, so the drum stem's classifier costs analysis time and changes nothing they publish.
- **Tools:**
  - `tools/sound_to_light.{sh,py}` use `--input-sources drums@2:drums`;
  - B4's `make_feline.py` and the other historical probes keep `hits`, as records of the runs they made.
- **Settings:** the owner's saved input map has one source with no hits flag. It loads as `other`, so nothing changes.

## Consequences

- A drum stem or a drum input always pays for the classifier: about 80-100 us more per hop live (ADR-1520 §9.1), and
  about 0.4 s more at load for a 3.5 min stem.
- `stem.<key>.kick` on a non-drums source is no longer on the bus. A route on it is unresolved, which the audit
  reports. None of the committed projects had one.
- A live drum bus must be given the drums role to drive `signal.kick`. The Inputs table shows the role, and
  `--input-sources` takes it.
