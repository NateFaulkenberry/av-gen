# ADR-366: The path tracer had no settings, only fields

Status: accepted
Date: 2026-09-19
Branch: `agent/mbackend`
Relates to: ADR-225 (a setting the application does not keep is not a setting), ADR-350 (the same
defect in `DayNightSettings`, and the two tests it prescribes), ADR-351 (the path tracer and its
naming), ADR-352 (the albedo probe), ADR-020 (`RenderSettings`, the shape this follows)

*Written as 363 and renumbered to 366 at merge: `main` landed its own ADR-363 (the multicam's
frame time) while this branch was in flight. Four collisions in one session now, and the cause is
structural — every agent branches from the same tip and mints the next free number independently.*

## What was wrong

ADR-351's path tracer is reachable: there is a `Renderer: (o) Realtime ( ) Path trace` radio at the
top of the Render panel, a `--pathtrace` flag, samples, bounces, a denoise checkbox, AOVs and the
ADR-352 probe. Everything a person can set, they can set.

**None of it was kept.** `pathtrace::TraceSettings` lived in `Application` member fields:

```cpp
pathtrace::TraceSettings uiPathTrace_;
double uiPathTraceSeconds_ = 0.0;
bool uiPathTraceDenoise_ = false;
bool uiPathTraceAovs_ = true;
```

and two of the eight were assigned at the binding site:

```cpp
uiPathTrace_.samplesPerPixel = 32;
uiPathTrace_.maxDepth = 3;
```

No project file, no scene file and no `AppSettings` carried a `pathtrace` key; grep for one found
nothing anywhere in `src/` or `examples/`. Set 512 samples and eight bounces, save, reopen: 32 and
3. **No reader and no writer** — ADR-350's defect exactly, one subsystem along, and written by
somebody who had the ADR in front of them.

Two smaller things in the same area, and they are the same defect at different scales:

* **The trace's output path was unreachable while a trace was selected.** `drawRender` returns
  before it draws the output-path field, and `startPathTraceFromUi` read
  `uiRender_.outputPath` — the *raster* job's. A person who had never selected Realtime and typed a
  path got their EXR in `$TMPDIR` with only a status line to say so.
* **`--pathtrace` could not be overridden by a project even once one existed**, because every
  `--pt-*` option was a plain value with a default indistinguishable from a typed one.

## Decision

`app::PathTraceSettings`, a peer of `RenderSettings` in the project document under `"pathtrace"`,
with a reader, a writer, validation, and its own output path.

A peer rather than a member: the two renderers share a resolution and nothing else, and nesting one
inside the other would have the raster job's `validate()` passing judgement on a sample count.

It is **not** `pathtrace::TraceSettings`. That is the renderer's argument; this is the authored,
persisted, user-facing set. The distinction earns its keep immediately:
`TraceSettings::russianRouletteCompensation` is documented as an intentionally-wrong control arm
that "must never be set in production", and a project file is precisely where a wrong value would
survive long enough to be believed. `strategy` and `russianRouletteDepth` are withheld for the same
reason. `captureFeatures` is **derived** rather than authored, because both the denoiser and the AOV
writer require it and a project that could record "denoise, but do not capture what the denoiser
reads" would be recording a failure.

`app::traceSettingsFrom(authored, width, height)` is the one translation between them. The Render
panel and `--pathtrace` both go through it, so a field that stops being carried across fails in one
place instead of in two that have drifted apart.

`--pt-*` options became `std::optional`, and `runPathTrace` now starts from
`engine_->pathTraceSettings()` and applies what was typed — the shape `renderSettingsFromOptions`
already had. Before this, `--pathtrace out.exr` on a project authored at 512 samples traced 32,
because 32 was a struct default nobody had asked for.

`platform::Window::SaveKind` gained `Exr`, because offering a person a `.mov` filter for a file the
tracer will write scene-linear float into is the silent-corruption case the error-handling rule
exists to prevent. `validate()` refuses any output path whose extension is not `.exr` for the same
reason.

## The seed is persisted, and it goes through the integer path

ADR-351 makes the image a pure function of the snapshot and the settings, and the seed is one of
them. A reproducible render whose seed is not written down is reproducible by nobody but the
process that made it, so it is in the file.

It is parsed as an integer rather than through the `double` path every other numeric field uses:
`0x853c49e6748fea9b` does not survive a round trip through a double, and a determinism seed that
changes when a project is saved is a guarantee that has quietly stopped holding. A test pins it.

## The tests, which are ADR-350's two

1. **Round trip, twice.** Every one of the nine fields is set to a value that differs from its
   default — asserted field by field first, because a writer that emits nothing at all passes a
   test whose input is the defaults — then parsed, re-emitted, and parsed again. ADR-350 insists on
   the second pass because a writer that echoes what it just read can still lose a value on the way
   back out.
2. **The block is on disk, and its absence means the defaults.** An engine sets the values, saves,
   and the raw JSON is read back to confirm `doc["pathtrace"]["samples"] == 512` — the writer half,
   which a reader-only test cannot see. Then a project whose `pathtrace` block has been *deleted* is
   loaded into the same engine, and the settings must come back as defaults rather than as the 512
   the process still had in hand. That is the control: without it, an engine that simply never
   cleared the struct would pass every other assertion.

Plus a third that exists because of how this class of bug propagates: `PathTraceSettings{}.seed`
must equal `pathtrace::TraceSettings{}.seed`. The constant is written as a literal in both files and
a silent disagreement would make a project's recorded seed differ from the one a default render
actually used.

## What is still not a setting

Recorded rather than fixed, so the next person does not have to find them again. On the raster side,
`RenderSettings::aovs` (ADR-242), `supersample` (ADR-212) and `encoderThreads` round-trip correctly
but have **no widget** — CLI only. The first two are properties of the deliverable rather than
diagnostics, and `supersample` is the documented answer to foliage undersampling, so they are UI
work owed. `disablePasses`, `qualityArms` and `postStages` are deliberately diagnostics and
deliberately not persisted.
