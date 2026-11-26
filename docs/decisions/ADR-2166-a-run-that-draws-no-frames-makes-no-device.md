# ADR-2166: a run that draws no frames of its own creates no GPU device; `--frames 0` draws none

- Status: Accepted (2026-11-26), eng/engine-api-s10 (Engine API stage S10; the evaluation's Phase 2 "lazy device",
  01-evaluation section 6.2 item 1; 03-boundaries.md section 2.1, `context_` "lazy in EngineHost")
- Code: `app/application.cpp` (`init`: `drawsFrames`, `initLiveRenderer`, `validateRendererOptions`; `runHeadless`:
  `--frames 0`), `api/engine_host.cpp` (the device on the first job)
- Measured: ADR-2169 (logs and timings, before and after)

## Context

The evaluation found `gpu::Context::create` unconditional in `Application::init`: `--pathtrace` (which the help calls
"no GPU"), `--ai-script` and the render commands all created a device and compiled the live renderer's pipelines
(72 s on a cold Metal cache) before doing anything -- and `--render`/`--queue` then built a second renderer inside
their `RenderJob`, never using the first. There was also no way to run a headless `--ai-script` or `--save-project`
without the frame loop: a headless `--frames 0` meant "the default, 120".

## Decision

1. **`init` makes the device and the live renderer only for a run that draws frames itself**: the editor, the
   headless frame loop (and its `--capture`), and the headless live profile. For `--render`, `--queue`, `--pathtrace`
   and `--headless --frames 0` it makes neither; it logs `headless: this run draws no frames of its own; no GPU
   device until a render job needs one`. A render job (ADR-2165) creates the device through the host, under the
   machine lock, when it starts; a CPU-only run never does.
2. **The renderer's command-line options are still checked** in a run that does not build it
   (`validateRendererOptions`): an unknown `--tier`, `--quality-arm`, `--disable` phase or `--debug-target` fails
   exactly as before.
3. **`--frames 0` in a headless run draws no frames** and exits 0 (after `--ai-script`, before `--save-project`,
   `--save-scene` and `--export-bundle`, which run as before). `--capture` with `--frames 0` is an error (exit 2).
4. The audits (`--audit-routes`, `--propose-reactivity`, `--sonic-trace`, `--validate-space`, `--trace-jumps`)
   already returned before the application existed; they are unchanged and verified device-free.
5. The live renderer's construction moved, unchanged and in the same order, into `initLiveRenderer`.

## Consequences

- `--render` and `--queue` start without the live renderer's pipeline compile.
- `--pathtrace` and `--frames 0` runs work on a machine whose GPU another process is using, and touch no device.
- A headless `--frames 0` that relied on 120 frames now renders none (no caller in the repository did).

## Revisit triggers

- A headless run that needs the live renderer for something other than drawing frames.
