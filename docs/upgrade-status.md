# Cinematic / AI upgrade — status

Dated 10 September 2026. Milestones 1, 2, 3, 6 and 7 are built and tested; 4 is half built; 5, 8,
9 and 10 are not. This document says which is which and why, because the useful part of a status
report is the second list.

Suite: **976 tests, all passing** on a quiet machine. Six new subsystems, ~3,400 lines with tests,
five ADRs (061–065).

## Built

| # | Milestone | What exists | Where |
|---|---|---|---|
| 1 | Asset registry | `AssetDescriptor` with category, archetype, tags, four profiles (material, audio response, variation); reads the manifest that already exists, both flat and grouped | `src/assets/asset_library.*` |
| 1 | World recipes | Composition / ecology / atmosphere / lighting weights plus art direction; every weight defaulted so a three-line recipe is legal | `src/world/world_recipe.*` |
| 2 | World composer | Emits `world::ScatterLayer`, the type the ecology already places. Bands, focal subject, void regions, one ecology relation | `src/world/world_composer.*` |
| 3 | Cinematic director | Nine shot kinds, distances in subject radii, bakes to timeline keys | `src/app/cinematic.*` |
| 4 | Musical events | Eleven event kinds classified from signals that already exist; frame-rate independent | `src/signals/musical_events.*` |
| 6 | Job system | Queue, workers, staged progress, honest ETA, prompt cancellation, pause/resume | `src/app/job_system.*` |
| 7 | AI abstraction | Backend interface, registry, disk cache, realtime budget, built-in analytic backend | `src/ai/inference.*` |

## Not built, and why

**Milestone 4's visual event graph.** The classifier exists; nothing subscribes to it. The graph
the brief describes — composable responses with duration, easing, intensity, cooldown, probability
— overlaps the existing route, macro, state and cue machinery substantially. Building a second
event system beside the modulation one without resolving that overlap is how a codebase ends up
with two ways to do everything. That decision wants making before the code.

**Milestone 5, pass export.** Mostly already existed before this work: the renderer produces
normal+roughness, velocity, emission and object+material IDs today, in HDR, and `--debug-target`
displays them. What is missing is writing them to files alongside the beauty pass and exposing
depth as an exportable target. Small, and not done.

**Milestone 8, a real model.** No ONNX Runtime, no bundled weights. Two reasons, and the second is
the honest one: adding a large binary dependency before anything consumes it is the wrong order,
*and* this environment cannot download a model or verify its licence. The interface is ready — a
real backend is `implement InferenceBackend, register it before the built-in`. Until then the
built-in analytic backend is what runs, and it is labelled as analytic everywhere it appears.

**Milestone 9, the offline AI VFX pipeline.** Its two prerequisites (jobs, inference) are done. The
pipeline itself — pass preparation, inference, temporal consistency, compositing, encoding, with
progress and cancellation — is not started.

**Milestone 10, the Bioluminescent Valley showcase.** Not started. This is the one that exercises
everything together and it should be last, which is where it is.

## Architectural risks

**The composer must never fork the ecology.** It currently emits `ScatterLayer` and a test asserts
the type. The moment a second placement path exists, worlds start disagreeing about what a density
means, and the fork will be the copy without the water-avoidance fixes.

**Two event systems.** Named above. Unresolved.

**Two art-direction vocabularies.** `app::WorldDirector` has eighteen knobs driving live routes;
`world::ArtDirection` inside a recipe is the same idea at composition time. They should meet.
Currently they merely coexist.

**Nothing is wired into the application yet.** Every subsystem here is tested in isolation and
none is reachable from the UI or from a scene file. That is deliberate for milestones this early,
but it means "976 tests pass" is not the same claim as "it works in the app", and the two should
not be confused.

## Performance

Unchanged, by construction rather than by measurement: none of this runs per frame. The one piece
that could — realtime inference — defaults to a budget of 0 ms, which disables it entirely, and a
test pins that default.

The measured baselines from earlier work still stand: Glowmere 22.1 ms at 1280×800 and 42.3 ms at
2880×1800; the Constellation 13.1 ms and 22.7 ms across its whole ninety seconds.
