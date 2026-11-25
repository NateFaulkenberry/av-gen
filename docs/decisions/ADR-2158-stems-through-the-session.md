# ADR-2158: audio stems through the session -- `audio.import_stem`, `audio.list_stems`, `audio.remove_stem`

- Status: Accepted (2026-11-25), eng/engine-api-s5 (Engine API stage S5; 113-engine-architecture, plan in
  03-phase0-api-contracts/04-poc-plan.md stage S5)
- Code: `ai/engine_tools.cpp` (`audio.import_stem`, `audio.list_stems`, `audio.remove_stem`; `writeProjectManifest`
  and `copyIntoProject`, lifted out of `asset.import` unchanged; `signalJson`/`signalsWithPrefix`, which
  `signal.list` now uses too; `isAudioExtension`, shared with `asset.list_importable`), `api/session.cpp`
  (`referencedOutside` reads the engine's stem list)
- Artefacts: `schemas/tool-catalogue.json`, `docs/api/tools.md`
- Docs: `docs/api/transactions.md` (two rows)
- Tests: `tests/unit/test_api_stems.cpp` (`[api][stems]`)

## Context

Stems have been in the engine since ADR-1520: `Engine::setStems` analyses each `audio::StemSource` (key, label,
file, offset, role, gainDb) beside the mix, publishes `stem.<key>.*` (and, ADR-1630, `role.<role>.*` for the first
source of each role), and the project saves them as `assets.stems[i]` with a `{path, size, sha256}` file
reference. Only Studio's sequence panel could add one. An external client had no tool: PoC step 5 (`audio.import_stem
{file, key, role}`) and the audio-reactive half of the PoC could not be written.

## Decision

1. **`audio.import_stem {file, key, role?, label?, offset?, gainDb?, source?}`** follows ADR-2156's file identity:
   `file` through `PathPolicy::forRead`, the copy into `<project>/assets/audio/stems/` through `forWrite` (id
   `asset://project/audio/stems/<file>`), written under a dot-name and renamed, de-duplicated by sha256 over the
   project's assets, and recorded in manifest v2 (`type: "stems"`, `import: {durationSeconds, sampleRate,
   channels, frames, format}`). A copy never replaces another file of the same name -- another stem's analysis
   would change under it -- so a name in use gets `-2`, `-3`, ... (`asset.import` overwrites; a stem may not).
2. **Installed through `Engine::setStems`**, the same path Studio uses: the current list plus one. `setStems`
   re-analyses only what changed. A stem that does not decode fails the call with `UNSUPPORTED`; the list is put
   back and the call's compensation log removes the copy.
3. **Validation at the source.** `key` must be `audio::validStemKey` (`[a-z0-9_]+`) and at most 64 characters,
   and unique in the project (`CONFLICT` otherwise -- `setStems` would silently re-key it, moving every route on
   it). `role`'s enum in the input schema and the tool's own check both enumerate `audio::kSourceRoleNames`, so
   a bad role is `INVALID_ARGUMENTS` with "choose one of: drums, bass, synth, vocal, other". `gainDb` is held to
   `analysis::kMinTrimDb..kMaxTrimDb`.
4. **The result is the bus.** `signals` is every `stem.<key>.*` entry as `signal.list` reports it (one helper,
   `signalJson`, serves both), `roleSignals` the `role.<role>.*` names, `analysis` the decoder's facts and every
   diagnostic (the file's, then against the mix), and `analysisMs` the host-thread time `setStems` took.
5. **Session write, `undoable=false`.** The stem list is not in the snapshot domain. Inside an explicit
   transaction it needs `irreversible: accept`. Its files go to the compensation log; `Session::referencedOutside`
   now counts a file the engine's stem list names as referenced (S1's rule 2, as it already did for the engine's
   audio), so a rollback keeps the copy the still-installed stem reads and says so in `files.kept`, rather than
   leaving the project naming a deleted file.
6. **`audio.list_stems`** (read) and **`audio.remove_stem {key}`** (session write, destructive). Removal is
   `setStems` without the entry; the file stays; the bus cannot undeclare, so `stem.<key>.*` stays listed and reads
   0, and the result counts the routes that now read 0.
7. **Save and open need no change**: `assets.stems[i]` already carried the file reference, and `loadProject`
   re-runs `setStems`. The round trip is tested through `project.save`/`project.open`.

## Alternatives considered

1. **Stage the copy until commit.** Rejected for the reason ADR-2156 gives: the stem is analysed and its signals
   routed inside the same transaction.
2. **Delete the file on rollback and leave the stem installed.** Rejected: the project would name a missing file
   on its next save.
3. **Make the import undoable by putting stems in the snapshot.** Rejected for S5: the snapshot is the parameter
   domain plus composition and sequence (S1); widening it is S1b's.
4. **Run the analysis off the host thread.** Not built (noted in the plan): `setStems` already analyses each stem
   on its own thread, but the host thread waits for them. Measured cost below.

## Consequences

- Measured host-thread cost of `setStems` for one new mono 44.1 kHz stem (release build, three runs each, on a
  machine at load average ~60): 8 s: 22-26 ms (the whole call 30-34 ms); 16 s: 55-60 ms; 60 s: 120-148 ms (the
  whole call 173-207 ms, the rest is the sha256 and the copy). About 2.3 ms per second of audio: a five-minute
  stem would hold the host for roughly 0.7 s.
- After a rollback the manifest no longer lists a kept stem file (its previous bytes come back); the next
  import of the same bytes re-adds the entry.
- `asset.import`'s duplicate scan hashes every file under `assets/`, stems included.

## Revisit triggers

- S6 jobs: the analysis as a job, the host answering meanwhile.
- A snapshot domain that includes the stem list (the import becomes undoable).
- A bus that can undeclare a name.
