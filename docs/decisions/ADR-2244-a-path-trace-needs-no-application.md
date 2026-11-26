# ADR-2244: `--pathtrace` runs before the Application, and a trace takes an offline render's detail limits

- Status: Accepted (2026-11-26), eng/hybrid-rt (Hybrid RT H1; audit defects D15, D16)
- Code: `app/pathtrace_cli.{hpp,cpp}` (new), `app/main.cpp`, `app/application.{hpp,cpp}` (the old `runPathTrace` and
  `runTraceSequence` removed), `pathtrace/trace_job.cpp`, `app/trace_sequence.cpp`

## Context

1. **Startup.** `--pathtrace` went through `Application::init` first. That created a Dawn device, warmed the pipeline
   cache and loaded the whole project into the application's engine: about 8 s and several GB on Neon Dreams. Then
   `TraceJob` loaded the project again into the offline engine it traces. The tracer needs no GPU
   (`docs/pathtrace/overview.md`).
2. **Detail limits.** `TraceJob` and `TraceSequence` never set detail limits, so rigs posed at the live distance
   rates. A rig frozen by `cullDistance` reached the snapshot unposed and was refused. `RenderJob` has lifted them
   since ADR-186/191.

## Decision

1. **`runPathTraceCommand`** runs from `main` beside the route audit and the sonic trace. It reads the project's
   `render` and `pathtrace` blocks with the readers `loadProject` uses, and keeps the old exit codes and log lines.
   `--save-project`, `--save-scene`, `--export-bundle`, `--ai-*`, `--render` and `--queue` are refused with
   `--pathtrace` rather than ignored.
2. **Detail limits.** Both trace paths call `setDetailLimits(DetailLimits::offlineDefault())` after loading.

## Relation to ADR-2166 (S10, merged first)

ADR-2166 independently stopped `Application::init` creating a device for `--pathtrace`. What this ADR adds on top
is the rest: the trace no longer builds an Application or loads the project into its engine at all (the second,
unused copy of the scene and its load time), and the incompatible flags are refused. On the rebase, `--pathtrace` was
removed from `init`'s `drawsFrames` test, where it can no longer arrive.

## Consequences

A headless trace creates no GPU device and loads the project once. The editor's Render panel path is unchanged.
