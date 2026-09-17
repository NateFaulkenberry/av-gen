# ADR-261: Most of the lab suite was already here under other names, so Phase 1 built the four things that were not

**Status:** Accepted — implemented
**Date:** 2026-09-17
**Context:** Phase 0 and Phase 1 of the Engineering Lab Suite: a fourteen-lab programme whose
largest risk is ten agents each inventing the same four subsystems
**Follows:** ADR-031 (debug drawing), ADR-077 (the numbers a frame may state about itself),
ADR-091 (two-tier simulation authority), ADR-182 (a probe that cannot fail proves nothing),
ADR-225 (a setting the application does not keep is not a setting), ADR-250 (the instrument is not
the engine)

## Context

The brief asked for a suite of fourteen specialised labs with "common diagnostic infrastructure",
and named six pieces of it: a registry, a launcher, a fixture system, a shared debug visualisation,
a deterministic configuration and a capture/diagnostic layer.

The reason this ADR exists is that **four of those six already existed**, and building a second
version of each would have been exactly the duplication the architecture phase was created to
prevent — committed by the architecture phase.

## The finding

| Asked for | Already in the tree | Since |
|---|---|---|
| shared debug drawing | `DebugDraw`, `buildDebugGeometry`, `DebugViewOptions` (21 switches) | ADR-031 |
| per-object "why isn't this rendered" | `RenderObjectDiagnostic`: `cullReason`, six per-plane `frustumMargins`, `visible`/`cameraCulled`/`submitted`/`finite` | renderer forensics |
| reproduction state and diff | `FrameSnapshot`, `writeSnapshot`, `compareSnapshots` — differences as sentences naming object and field | forensics Phase 9.1 |
| performance reporting | `RenderStats`, `GeometryCounters`, `CpuFrameBreakdown`, `FrameTimeline`, including the fields that say which numbers are stale | ADR-077 |
| isolation arms | `passArms()` / `qualityArms()`, one table read by the CLI, the panel and the bisection | ADR-117 |
| minimal fixtures | `examples/qa/` — a progressive isolation ladder with committed state baselines | renderer QA |
| a launcher | `examples/index.json` + `File > Examples`, already grouped, already carrying a `Lab` category with eight entries | ADR-019 |

## Decision

Phase 1 builds four things and nothing else, all in `src/labs/`, all GPU-free, all in the globbed
`avgen_core` so a lab agent adds a file and does not touch CMake:

1. **A registry** (`labs/lab.hpp`) — fourteen descriptors carrying the question, `owns`,
   `doesNotOwn`, and `decides` as `path:symbol`.
2. **A case format** (`labs/case.hpp`) — `examples/labs/<lab>/cases.json`, a fixture *path* plus the
   numbers that make one frame reproducible, and `reproduceCommand()`.
3. **Overlay profiles** (`labs/overlays.hpp`) — `overlaysFor(LabId)` returning a
   `DebugViewOptions`. Not a drawing system; a selection over the one that exists.
4. **The visibility reason vocabulary** (`labs/visibility_reason.hpp`) — thirteen codes, the words
   only; the Visibility Lab owns wiring them.

## Three things that were rejected, and why

**A lab runtime.** Every fixture is a scene or project the engine already loads. Nothing about a lab
needs new engine code to run, and a `LabScene` type would have been a second scene format.

**A golden-image store.** The repository has deliberately never had one. A renderer upgrade changes
pixels by design, so an image baseline is discarded on its first day; what must not change is the
*state* the renderer derives, and `examples/qa/baselines/*.snapshot.json` already records that.

**A `labs/` top-level tree (the brief's §31 sketch).** The Quality Lab is six phases of working code
at `tools/quality-lab/`; moving it to satisfy a diagram would have renamed working code and broken
every path in nine documents and two ADRs.

## What the registry is for, beyond listing

`decides` is the load-bearing field. An ownership map written only in prose drifts from the code it
describes, silently, because nothing checks it. `tests/unit/test_lab_registry.cpp` opens every
`decides` file and greps it for the symbol, so a rename fails the suite rather than making
`docs/engineering-labs.md` quietly wrong.

That test found its own first bug: `pathHalf` split `src/rendering/post_processor.cpp:PostProcessor::run`
on the **last** colon and cut the symbol in half.

## ADR-182, applied three times

1. **The reason vocabulary is checked against the renderer's own source.** The test reads
   `scene_renderer.cpp`, extracts every string literal on a line assigning `cullReason`, and
   requires the vocabulary to map all of them. The first version of that scan matched only
   `cullReason = "..."` and found **two** of the five reasons — one of the three assignments is a
   nested ternary choosing between three literals — and reported success. A scan that finds two
   things and passes is the thing this ADR's ancestor is about, and it cost a `CHECK(written.size()
   >= 4)` to notice.
2. **`OCCLUSION_CULLED` is not in the vocabulary**, because there is no occlusion culling in this
   engine — no HiZ, no depth pyramid, no query, no two-phase cull. The spec asked for the code. A
   reason that can never be returned is a diagnostic that cannot fail.
3. **The overlay-profile test asserts both halves.** It checks that no profile enables the two inert
   switches, *and* that the profiles are not simply all-default — otherwise the first assertion
   passes while saying nothing.

## ADR-225, and the two switches nobody noticed

`DebugViewOptions::lod` and `::culling` have checkboxes in `world_panel.cpp`, and
`debug_visualizer.cpp` **reads neither field**. Both draw nothing and have for as long as they have
existed.

No lab overlay profile turns them on, and a test enforces that. Enabling them would have made the
suite's own launcher the place a control-wired-to-nothing was hidden — which is the brief's §37 with
the subject changed: the diagnostics must not be made to agree with the renderer, and a launcher
that presents an inert control as a lab instrument is that failure in its most expensive form,
because the person who trusts it clears a subsystem that was never tested.

They are recorded as the Visibility and LOD Labs' first work items instead.

## Three corrections to the brief, from the audit

* **There is no occlusion culling.** See above.
* **There is no TAA.** `shaders/post.wgsl` antialiases with FXAA and says so. The Temporal Lab's
  real subjects are GTAO's history, the LOD ladder's spread and hysteresis, the one-to-three-frame
  cull readback lag, and the exposure meter.
* **Entity LOD is not live.** `RepresentationSelector` and `ImportanceEvaluator` exist, are tested,
  and `scene_renderer.cpp` calls neither. The ladder that runs is the scatter one in `cull.wgsl` and
  the terrain one in `world::chunkLod`.

## Consequences

A lab agent gets a home rather than a framework: register a lab, get its fixture and its overlays,
write cases in a file nobody else edits, and write regression tests as new files in a globbed tree.

The cost is one small refactor outside `src/labs/`: `DebugViewOptions` moved from `debug_draw.hpp`
into `rendering/debug_view_options.hpp`, so that choosing a set of overlays does not require linking
Dawn. `debug_draw.hpp` includes it, so every existing include still compiles. That is the same split
`renderer_diagnostics.hpp` already made, for the same reason.
