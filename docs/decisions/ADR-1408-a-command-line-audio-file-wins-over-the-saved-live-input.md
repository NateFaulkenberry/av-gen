# ADR-1408: A command-line audio file wins over the saved live input

- Status: Accepted (2026-10-16), proto/cymatic-split (Cymatic World: a live scene measured against a test track)
- Code: `app::liveSonicOpensSavedInput` (`src/app/live_audio_policy.hpp`), called where `Application` turns live Sonic input on.
- Tests: `tests/unit/test_live_audio_policy.cpp`.

## Context

A live scene (`sonic.live`) carries no audio track: it plays whatever comes in. Cymatic World's owner wants it kept that way, and wants its renders, clips and profiles fed a known test track (Feline Footwear) on the command line instead.

`avgen --audio <file>` already loads a file. But when the project's live Sonic input switched on, the app also opened the capture device saved in the Live panel's settings, unless `--input` had named one. So `avgen --live-profile --project <live project> --audio <file>` measured the microphone, not the file ("Audio/MIDI: live input"). A headless `--render` was unaffected.

## Decision

An audio file named on the command line is the run's audio. Turning live Sonic input on opens the saved capture device only when:
- no live input is open;
- `--input` did not name a device;
- `--audio` did not name a file;
- a device is saved.

## Consequences

- `avgen --live-profile --project <live project> --audio <file>` profiles against the file ("Audio/MIDI: file, playing to the output device"), and so does the editor started with `--audio`.
- Nothing changes for a run without `--audio`: a live project opens the saved device as before.
- A live project stays track-free. Its tools feed test audio on the command line, never through the project file.
