# ADR-1981: Asset hashes cached by size and modification time

- Status: Accepted (2026-11-09), eng/app-stalls.
- Code: `cachedSha256` / `assetRefJson`, in `src/app/engine_project.cpp`.

## Context

In long live runs, frames of 260–300 ms (`ui.build` 230–260 ms) appeared every ~2.5 s once the song had ended. A
process sample named the work: `Application::sampleProjectDirtyIfIdle` calls `Engine::sampleProjectDirty`, which
calls `projectDocument`, which calls `assetRefJson`, which ran `sha256File` on every referenced asset. That includes
the 40 MB song, about 250 ms each time.

The dirty sampler is held off while the transport plays (ADR-952). It runs when the transport is idle, every ten
times its own cost, so the moment playback stopped it re-hashed the song every ~2.5 s, for as long as the app stayed
idle.

## Decision

`assetRefJson` takes the hash from a process-wide cache keyed by the file's path, size and last-write time. A file
whose size or modification time changes is hashed again. A file whose time cannot be read is hashed every time, as
before. The cache is guarded by a mutex, so it is safe from any thread.

## Consequences

Empty-scene control from the end of the song (`--start-at 200`, 30 s):

| | Frames over 100 ms | Frames on time |
|---|---|---|
| Before | 9 | 98.6% |
| After | 0 | 99.8% |

The worst frame went from 293 ms to 48 ms.

A test (`[integration][project]`) checks that a second document repeats the hashes, and that a shader rewritten to
the same length with a new modification time gets its new hash while the audio's stays.

A file changed in place without its modification time changing would keep its old hash until its size changes. Every
editor and every copy updates the modification time.
