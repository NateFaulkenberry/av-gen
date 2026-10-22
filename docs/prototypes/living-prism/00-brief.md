# The Living Prism: owner's architectural milestone brief (verbatim, 2026-10-22)

Preceding line from the owner: "Sure you can fix that and also start on this" ("that" = the engine's one-frame timeline display lag).

AV Gen Architectural Milestone: The Living Prism

Live Rendering Pipeline Audit, Research, and Implementation Plan

Mission

Treat The Living Prism as the next architectural milestone for AV Gen’s live rendering pipeline.

This milestone is intended to establish whether AV Gen’s existing rendering, audio analysis, MIDI, modulation, VFX, and performance systems can work together to support a highly expressive, visually sophisticated, real-time 3D audiovisual environment.

Your immediate assignment is to investigate, research, evaluate, and plan before implementing. Do not jump straight into building a scene or adding rendering features.

We already have substantial functionality across the engine and live scene collection. Many capabilities that might initially look like gaps may already exist in a shared subsystem, a scene-specific implementation, an experimental system, or a partially completed feature. Determine what we actually have before recommending changes.

You may use two sub-agents in parallel. Delegate substantial, complementary investigations to them, independently verify their findings, and synthesize their work into one coherent recommendation. Do not let the parallel investigations become duplicate codebase surveys.

At the end of this milestone, present your findings and proposed implementation plan to me. You are not authorized to begin broad implementation until I have reviewed the plan. You can make targeted investigative changes or create temporary test harnesses when necessary, but do not modify production behavior merely to make the audit easier.

Your final deliverable should tell me when the architectural foundation is ready for the next phase: a separate, detailed art-direction and scene-construction specification for The Living Prism.

⸻

1. Context: What We Are Building Toward

The Living Prism is a planned live audiovisual scene intended to exercise AV Gen’s full live rendering pipeline.

The eventual scene will be a coherent, spatially rich 3D environment. Its visual identity will combine environmental depth, expressive materials, light, color, and carefully designed real-time VFX. Music will transform the environment through coordinated changes to its surfaces, illumination, motion, atmosphere, and localized visual events.

The intended experience is not a conventional waveform visualizer, a collection of unrelated spinning objects, or a static environment with an audio-reactive post-processing filter. It should feel like a real-time visual instrument whose environment has its own identity and whose behavior responds meaningfully to music.

A rough conceptual direction is a vivid, imaginative crystalline landscape containing a mixture of natural and fantastical forms, reflective or translucent surfaces, emissive materials, atmospheric depth, and interconnected environmental features. Musical activity can propagate through the world, changing its appearance and leaving temporary visual traces.

Examples of the eventual artistic behavior might include:

* Percussive events launching localized impacts or traveling visual fronts.
* Bass energy producing large-scale environmental movement or broad waves of light.
* Sustained tones changing materials, illumination, and color over time.
* Melodic activity triggering spatially meaningful events.
* Audio character influencing the palette and visual texture.
* MIDI providing deliberate, performable control over visual parameters.
* Previous musical events leaving temporary effects that decay or evolve.
* Complex effects combining into a coherent composition rather than competing for attention.

These are conceptual examples, not a finalized feature checklist. Do not turn this description into a rigid scene design or prematurely prescribe the exact visual implementation.

The detailed art direction, environment layout, assets, composition, camera choreography, specific palette, and final effect design will be supplied later in a separate scene specification. Do not spend this milestone constructing the actual environment or deciding its final visual appearance.

The purpose of this milestone is to make sure the underlying engine and shared systems can support the scene effectively.

Non-goals

Do not:

* Build the finished Living Prism scene.
* Spend significant time generating or acquiring its final assets.
* Redesign the entire renderer simply because an alternative architecture looks attractive.
* Add every conceivable audio-analysis feature.
* Introduce a new modulation, particle, VFX, or quality-control framework without investigating what already exists.
* Treat the feature suggestions in this prompt as proof that those features are missing.
* Refactor functioning systems solely to make the architecture look more uniform.
* Optimize from intuition alone without profiling, evidence, or a reproducible test.
* Sacrifice the existing live-performance experience in pursuit of an offline-rendering feature.
* Make speculative changes to unrelated scenes or production systems.

⸻

2. Operating Rules

2.1 Investigate before designing

Start by establishing the actual state of the repository.

Inspect the current branch, worktree state, recent changes, relevant documentation, architecture decision records, scene definitions, rendering code, signal-processing code, and tests.

Identify the real source of truth for each subsystem. Documentation can be stale, experimental code may not be connected to production paths, and a scene’s visible behavior may depend on a combination of shared and local implementations.

Trace representative features from their source to their final visual effect. For example:

audio input → analysis → signal bus → modulation/response → parameter → scene/entity behavior → renderer

This is a conceptual trace, not an assumption that every feature uses exactly this route. Follow the actual implementation and document deviations.

Use source references, symbol names, file paths, relevant configuration, and tests to support your conclusions.

2.2 Separate facts from recommendations

Label findings clearly as:

* Verified existing: Implemented and reachable through the relevant live execution path.
* Existing but limited: Implemented, but with material limitations or incomplete integration.
* Experimental: Present but not established as production-ready.
* Duplicated or fragmented: Similar capabilities exist in multiple places with meaningful differences.
* Missing: No suitable implementation was found after a documented search.
* Unknown: Evidence is insufficient to reach a conclusion.

Do not call a feature implemented merely because a class, shader, configuration key, or design document exists. Verify that it is used and identify the conditions under which it works.

Do not call a feature missing merely because its name differs from the terminology used in this prompt.

2.3 Research the problem, not just the code

Deep-research relevant real-time rendering, audiovisual performance, and GPU optimization topics.

Prioritize primary technical sources: GPU vendor documentation, graphics API documentation, rendering research papers, engine technical documentation, and authoritative documentation from relevant software projects.

Use current documentation where APIs, techniques, or platform behavior may have changed. Cite the source and publication date where relevant. Distinguish published technical evidence from your own inference.

Research only topics that could materially influence the architecture or implementation plan. Do not pad the report with generic descriptions of graphics techniques.

2.4 Preserve the working live renderer

AV Gen is a C++23, WebGPU/Dawn-based engine targeting macOS Apple Silicon, with Metal underneath on the primary platform. It uses SDL3 and an existing audio/MIDI and scene pipeline.

Confirm the actual current implementation rather than treating this summary as exhaustive.

The live-performance use case is central. Improvements must account for frame-time predictability, GPU cost, CPU cost, memory and bandwidth, latency, and visual quality under sustained load.

Do not propose a renderer rewrite or a move to another graphics API without overwhelming evidence that a specific, important limitation cannot be solved incrementally.

2.5 Produce a plan before broad implementation

Complete the audit and research first.

You may implement small, isolated investigative probes if necessary to measure behavior. Keep them separate from production changes where practical and document their purpose.

Do not start the Living Prism scene implementation, introduce a large new subsystem, or undertake broad refactoring until the findings and proposed plan have been reviewed.

⸻

3. Parallel Investigation: Two Sub-Agents

Use both available sub-agents. Give them distinct responsibilities and ask each for evidence-based findings, not generic suggestions.

You remain responsible for repository integration, verification, and the final recommendation.

Sub-Agent A: Rendering Architecture and GPU Performance

Investigate the live rendering pipeline, shared VFX systems, and performance behavior.

Cover:

1. Render graph or pass organization, render targets, formats, resolution, and presentation.
2. Lighting, shadows, reflections, ambient occlusion, fog, transparency, emissive materials, and post-processing.
3. Geometry submission, instancing, culling, LOD, batching, draw calls, and resource lifetime.
4. Particles, GPU simulation, procedural geometry, animation, and other potentially expensive systems.
5. CPU/GPU synchronization, command submission, GPU timing, memory bandwidth, render-target bandwidth, and resource allocation.
6. Dynamic resolution and adaptive-quality behavior, including how quality changes affect the image.
7. Existing performance tooling, frame-time instrumentation, benchmarks, stress tests, and representative scenes.
8. The actual cost of the existing visual effects that could be useful for The Living Prism.

Inspect relevant profiling results and known bottlenecks if available. Run representative tests where practical, using the existing test and profiling workflows.

For each significant capability, establish whether it is implemented, how it is reached, its current limitations, and what evidence supports that conclusion.

Identify which prospective improvements would provide the greatest visual benefit per unit of GPU and CPU cost.

Do not assume that a technique is worth implementing merely because it is used by another renderer.

Deliverable: A rendering and performance audit, a prioritized gap analysis, evidence-backed recommendations, and proposed performance validation criteria.

Sub-Agent B: Audio, MIDI, Modulation, and Live Scene Integration

Investigate the path from musical input to visual response.

Cover:

1. Live audio input and channel handling.
2. Audio analysis, frequency bands, transients, onset detection, beat/tempo tracking, and higher-level sonic characterization.
3. MIDI note events, velocity, CC bindings, pitch/register/chord information, MIDI clock, and control mapping.
4. Signal bus semantics, naming, units, ranges, update rates, smoothing, and latency.
5. Modulation routing, parameter mapping, envelopes, LFOs, thresholds, curves, accumulation, decay, and reset behavior.
6. Scene-specific signals, world-level macros, state machines, long-timescale behavior, and cross-system interactions.
7. Existing support for assigned live inputs or synchronized stem analysis, including partial implementations and dependencies.
8. Diagnostic tooling and tests for confirming that a signal actually reaches the intended visual parameter.
9. Repeated or overlapping implementations of similar audio-response behavior across scenes.

Trace multiple representative behaviors end to end. At least one should be a simple transient response, one should be a continuous response, one should be MIDI-controlled, and one should involve longer-timescale scene state or accumulated effects.

Determine where response semantics are shared and where scenes implement their own behavior. Identify whether that division is deliberate and beneficial or causes meaningful duplication.

Do not assume that adding more signal names will improve audio reactivity. Evaluate the quality, timing, stability, composability, and observability of the existing signals first.

Deliverable: An audio/MIDI/modulation audit, an integration map, an identified-gap list, and prioritized recommendations for making musical responses more expressive and dependable.

Lead Agent: Integration and Independent Verification

While the sub-agents investigate their domains, independently establish the overall repository and architecture context.

You are responsible for:

* Reconciling conflicting findings.
* Verifying important claims against the actual implementation.
* Identifying issues that cross the rendering and audio/modulation boundaries.
* Examining the scene collection as an integration test of shared systems.
* Researching architectural alternatives where the agents identify a real decision.
* Ensuring the proposed plan fits the current architecture and existing engineering priorities.
* Identifying any changes that would introduce unnecessary complexity or duplicate existing functionality.

Do not simply concatenate the two reports. The final recommendation must explain how the systems interact and which changes, if any, should be made to the shared architecture.

⸻

4. Repository and Documentation Audit

Before recommending implementation work, establish an accurate map of the relevant code.

Inspect the existing documentation and architecture decision records. Use the Live Scene Guide as an entry point, but verify its claims against the code.

Identify:

* The current live-rendering execution path.
* The audio analysis and signal-publication path.
* The MIDI event and control path.
* The modulation and parameter-update path.
* The relationship between world-level, entity-level, material-level, and renderer-level behavior.
* The shared VFX systems available to scene authors.
* The current scene loading, updating, and rendering lifecycle.
* Existing quality tiers and how they are selected.
* Existing diagnostics and profiling facilities.
* The relevant tests, fixtures, and performance benchmarks.
* Experimental or abandoned implementations that could be mistaken for production functionality.

Create a concise subsystem map with links to the important code and documentation.

Identify the appropriate extension points for The Living Prism without implementing the scene.

If documentation and implementation disagree, document the discrepancy and identify which source is authoritative for current behavior.

⸻

5. Rendering Capability Matrix

Build a capability matrix grounded in the repository.

Include, at minimum:

* Opaque and transparent geometry.
* Emissive materials and bloom or glare.
* Lighting and shadowing.
* Reflections and indirect-lighting approximations.
* Fog and atmospheric depth.
* Color grading and palette control.
* Distortion, chromatic aberration, scan effects, and image-space treatments.
* Particles and GPU/CPU simulation.
* Instancing, culling, LOD, and geometry batching.
* Procedural geometry and surface deformation.
* Animation and skinned geometry where relevant.
* World-level and entity-level visual effects.
* Reusable event-driven VFX.
* Traveling waves, ripples, impact effects, and spatial propagation.
* Persistent trails or accumulated visual effects.
* Scene transitions and layered compositing.
* Adaptive quality and dynamic resolution.
* Profiling, diagnostics, and automated performance validation.

These are investigation categories, not a requirement to implement each one.

For every category, include:

Field	Required information
Capability	The concrete feature or system being assessed
Status	Verified existing, limited, experimental, fragmented, missing, or unknown
Evidence	Source files, symbols, tests, documentation, or profiling data
Current use	Where and how it is actually used
Limitation	A demonstrated shortcoming, if any
Living Prism relevance	High, medium, low, or not required
Proposed action	Reuse, improve, integrate, refactor, research further, or leave alone
Expected benefit	Specific visual, performance, reliability, or authoring improvement
Cost and risk	Estimated engineering effort, runtime cost, and regression risk
Confidence	High, medium, or low, with a short explanation

Do not fill the matrix with generic assumptions. If a category is irrelevant or cannot be evaluated, explain why.

Prioritize meaningful integration gaps over missing features that would add little to the intended scene.

⸻

6. Performance Investigation and Budget

The scene is intended for live audiovisual performance. Establish what the current engine can reliably deliver before proposing expensive new work.

The current target platform is Apple Silicon macOS using WebGPU/Dawn and Metal, but consider how architectural decisions affect the eventual portability of the system.

Use the existing benchmark setup wherever possible. Do not invent performance measurements or compare results gathered under incompatible conditions.

6.1 Establish a baseline

Identify representative scenes that exercise different rendering paths and workload profiles.

Where practical, measure:

* CPU frame time.
* GPU frame time.
* Total frame pacing and missed deadlines.
* Render resolution and output resolution.
* Major rendering-pass costs.
* Draw calls and geometry submission overhead.
* Particle counts and simulation costs.
* Shadow, reflection, volumetric, and post-processing costs.
* Memory usage and relevant GPU bandwidth pressure.
* Quality-tier changes and their effect on the output.
* Any known CPU/GPU synchronization stalls.

Use repeatable test durations and document hardware, resolution, scene settings, quality tier, and relevant environmental conditions.

If the existing tooling cannot measure a required metric, identify the missing observability rather than presenting a guess as a measurement.

6.2 Establish the actual frame-time target

Determine the project’s intended live frame rate and corresponding frame-time budget from current configuration, documentation, and user-facing requirements.

Do not assume a universal target or equate a reported FPS with stable delivery.

For example, a 60 Hz output has a 16.67 ms frame interval, while 120 Hz has an 8.33 ms interval. These are illustrative conversions, not a declaration that every AV Gen scene must target 120 Hz.

Distinguish CPU time, GPU time, presentation timing, and end-to-end audio-to-visual latency.

6.3 Evaluate adaptive quality

Determine what adaptive quality already does and how it chooses its settings.

Investigate:

* What metrics drive quality changes.
* Whether the system uses stable measurements or reacts to short spikes.
* Whether it can oscillate between quality tiers.
* Whether it can lower render resolution or adjust individual effect costs.
* Whether different scenes degrade gracefully or lose their visual identity.
* Whether the renderer exposes enough information to explain why quality was reduced.
* Whether expensive systems can provide their own cost/quality trade-offs.
* Whether quality changes themselves create noticeable stalls.

Do not propose a new adaptive-quality system until the current implementation has been understood and tested.

6.4 Evaluate performance options by cost and benefit

For each promising improvement, estimate its likely benefit and risk.

Consider options such as:

* Improved batching or instancing.
* Better culling and LOD.
* Reduced redundant CPU work.
* GPU-based simulation where it is demonstrably beneficial.
* Lower-bandwidth render targets or more efficient pass organization.
* Selective shadowing or reduced shadow update frequency.
* Lower-cost reflection and volumetric techniques.
* Dynamic resolution.
* Effect-specific quality settings.
* More useful profiling instrumentation.

These are candidates for evaluation, not predetermined solutions.

Account for visual trade-offs. A cheaper technique that destroys the look of a scene is not automatically an improvement.

⸻

7. Audio-Reactive Capability Audit

Evaluate the existing audio and MIDI systems against the needs of a sophisticated live scene.

7.1 Signal quality and semantics

Determine the actual behavior of the existing signal families, including audio analysis, sonic character, note-related signals, visual signals, macros, LFOs, and response envelopes.

For important signals, establish:

* Meaning and units.
* Numeric range and normalization.
* Update rate and latency.
* Whether the value is instantaneous, smoothed, event-based, or accumulated.
* Smoothing, attack, release, decay, and retrigger behavior.
* Behavior during silence or missing input.
* Whether the signal is suitable for continuous modulation, event triggering, or both.
* Whether it is stable and predictable enough for performance use.

Look for overlapping signals with inconsistent semantics, duplicate implementations, unused but expensive processing, and concepts that are exposed without a clear supported use.

Do not consolidate signals merely because their names appear similar. Demonstrate semantic overlap first.

7.2 Modulation architecture

Determine whether the current modulation system can reliably support many simultaneous routes without creating avoidable CPU overhead or unpredictable behavior.

Evaluate the existing mechanisms for:

* Source selection.
* Input normalization and remapping.
* Curves and thresholds.
* Smoothing and envelopes.
* Output ranges and clamping.
* MIDI control.
* Accumulation, decay, and reset.
* Event-to-parameter and continuous-signal-to-parameter routing.
* World, entity, material, and renderer-level targets.
* Debugging and runtime inspection.

Identify which mechanisms are already shared, which are scene-specific, and which are duplicated.

If the current system already provides these capabilities, recommend reuse rather than a replacement framework.

If a real gap exists, describe the smallest architectural change that addresses it.

7.3 Instrument-aware input and stems

Investigate the current implementation and status of separate live audio inputs and synchronized stem analysis.

The intended direction is to allow the user to designate sources such as drums, bass, and synth, and to use aligned stem files for analysis while playback remains the normal stereo mix.

Determine:

* What input-routing and channel-selection support already exists.
* How audio timestamps and analysis updates are synchronized.
* Whether stems can be aligned reliably with playback.
* How per-input signals enter the existing bus.
* Whether the architecture can support multiple sources without duplicating the entire analysis pipeline unnecessarily.
* What happens when an input disappears, changes, or is silent.
* Whether live and offline/stem-derived analysis can expose consistent signal semantics.
* Which changes are necessary for this milestone versus a separate audio-input milestone.

Do not implement a new stem system merely because it would benefit the scene. First establish the current state, dependencies, and correct project sequencing.

7.4 Musical structure and temporal memory

Investigate existing beat, tempo, phrase, note, chord, scene-state, and accumulated-effect systems.

Determine whether current systems can express:

* Short transient responses.
* Sustained modulation.
* Beat-synchronized events.
* Phrase-scale changes.
* Long-lived scene state.
* Accumulated activity with decay.
* Spatial events that propagate through the world.
* Controlled manual intervention.

For every apparent gap, decide whether it belongs in the shared signal/modulation infrastructure, a reusable VFX primitive, or a scene-specific implementation.

Avoid adding high-level musical inference unless it solves a demonstrated problem that existing signals cannot address.

⸻

8. Reusable VFX and Scene Integration

Investigate whether AV Gen has enough reusable VFX infrastructure to construct The Living Prism without accumulating a large collection of bespoke systems.

Consider the following conceptual effect categories:

* Localized impacts and bursts.
* Traveling fronts and spatial waves.
* Ripples on surfaces or through a world.
* Glints, sparkles, and emissive accents.
* Beams and light trails.
* Particles and persistent trails.
* Surface/material modulation.
* Color and palette transitions.
* World-scale environmental movement.
* Temporary accumulated effects that decay over time.

For each category, identify existing implementations and assess whether they are:

1. Reusable and production-ready.
2. Reusable but missing useful controls.
3. Available only as scene-specific code.
4. Experimental.
5. Absent.

A scene-specific implementation is not automatically a problem. The question is whether multiple scenes would benefit from sharing the same underlying mechanics.

8.1 Identify the right abstraction boundaries

For promising shared systems, determine:

* What should be a reusable low-level effect.
* What should be a reusable response or event primitive.
* What should remain a scene-authored decision.
* How effects receive their signals and parameters.
* How their resource use is controlled.
* How they integrate with adaptive quality.
* How they are tested independently of a complete scene.

Avoid building a universal effects framework with unnecessary indirection.

The goal is a small, coherent vocabulary of useful effects, not a general-purpose graphics editor hidden inside the engine.

8.2 Investigate temporal memory and spatial propagation

The Living Prism concept suggests that musical events can leave visual traces, propagate through connected structures, and gradually decay.

Determine whether existing particles, trails, shaders, simulation systems, textures, buffers, or scene-state mechanisms can already achieve this.

If a shared spatial propagation or temporal-memory primitive would materially reduce duplicated work, assess possible implementations and their costs.

Compare techniques only where relevant. For example, a shader-driven ripple, a GPU simulation, and an event traveling across a graph of scene elements solve different problems. Do not assume they should be unified.

Identify the simplest approach that meets the intended requirements, and clearly distinguish a reusable architectural capability from the specific visual effect that uses it.

⸻

9. Deep Research Requirements

Conduct focused technical research to inform the actual architectural decisions.

Research at least these areas, to the extent that code inspection identifies relevant gaps:

A. Real-time rendering quality and performance

Investigate techniques for expressive lighting, emissive materials, bloom, atmospheric depth, reflections, shadowing, transparency, and post-processing under constrained frame budgets.

Prioritize approaches compatible with AV Gen’s existing WebGPU/Dawn/Metal implementation.

B. Efficient reusable VFX

Investigate approaches to GPU particles, instancing, procedural effects, traveling waves, persistent trails, and temporal accumulation.

Focus on the techniques that could realistically be reused by multiple AV Gen scenes.

C. Frame pacing and adaptive quality

Research practical approaches to frame-time measurement, GPU timing, dynamic resolution, quality scaling, and avoiding unstable quality oscillation in real-time applications.

D. Audio-reactive performance systems

Research relevant techniques for stable transient detection, beat confidence, latency-aware signal smoothing, instrument-aware analysis, and synchronized stems.

Distinguish established approaches from research prototypes and avoid proposing expensive analysis without a demonstrated need.

E. Architecture and authoring

Where useful, compare how established tools such as Notch, TouchDesigner, Resolume, and Synesthesia handle reusable effects, parameter mapping, diagnostics, quality control, and live performance workflows.

Do not treat their feature lists as an implementation plan. AV Gen has its own engine architecture and goals.

For each research topic that materially influences a recommendation, include:

* The technical problem.
* The relevant technique or alternative.
* Reliable sources.
* Benefits and drawbacks.
* Compatibility with AV Gen’s existing architecture.
* Expected implementation complexity.
* Expected runtime cost or performance implications.
* Why the approach is preferable to the realistic alternatives.

Research should lead to concrete decisions, not an encyclopedic survey.

⸻

10. Identify Refactoring and Removal Candidates

Explicitly look for architectural problems that may have accumulated as the scene collection and rendering pipeline developed.

Potential issues include:

* Multiple implementations of the same response behavior.
* Overlapping signal concepts with inconsistent ranges or timing.
* Scene-specific code duplicating shared renderer functionality.
* Multiple competing ways to perform the same modulation task.
* Experimental systems that are not clearly isolated from production systems.
* Features with significant maintenance costs but little demonstrated value.
* Unnecessary per-frame CPU work.
* Expensive features that lack controls for adapting to the live budget.
* Dead code, unused configuration, obsolete pathways, or misleading documentation.
* Systems that have grown more complex than the problems they solve.

For each candidate, provide:

1. Evidence that the issue exists.
2. Its practical cost or consequence.
3. A description of the simplest corrective action.
4. The expected benefit.
5. Risks and migration implications.
6. Whether the action should happen before, during, or after the Living Prism milestone.

Do not remove a feature just because it is unused in the current scene collection. It may be useful elsewhere.

Do not propose a broad rewrite when a narrow correction would work.

Do not create backwards-compatibility shims for obsolete pathways unless they are genuinely needed. Where old behavior can be safely retired, prefer a clean implementation and explicit migration over permanent duplicate systems.

The objective is to improve the engine’s overall coherence, not to conduct a cleanup for its own sake.

⸻

11. Build a Requirements-to-Capabilities Map

Translate the eventual scene concept into an architectural readiness matrix.

Use these broad requirements:

1. A richly layered 3D environment.
2. Emissive and reflective material response.
3. Expressive lighting and atmospheric depth.
4. Strong, controlled palette modulation.
5. Localized transient effects.
6. Continuous audio-driven environmental changes.
7. MIDI-controlled visual parameters.
8. Events that propagate through spatial structures.
9. Temporary visual memory and decay.
10. Persistent particles or trails where appropriate.
11. A camera that can move through the environment smoothly.
12. Multiple simultaneous modulation routes.
13. Stable frame pacing at the target live output.
14. Graceful quality reduction under load.
15. Clear diagnostics for audio signals and rendering cost.
16. Reliable behavior during silence, abrupt transitions, and unusual input.

For each requirement, state:

* Which existing subsystem supports it.
* Whether it is already demonstrated in a working scene.
* What is missing or uncertain.
* Whether a new shared capability is required.
* Whether the remaining work is architectural, scene-specific, or primarily art/asset production.
* The expected performance implications.
* The test that would prove readiness.

This matrix should prevent us from accidentally building infrastructure for a requirement the engine already meets, or assuming a visual effect will be cheap because it is simple to describe.

⸻

12. Create a Prioritized Implementation Plan

After completing the investigation and research, propose a staged plan.

Do not start executing this plan yet. Present it for review.

Organize recommendations into these categories:

P0 — Required foundation

Work that blocks the intended scene or makes the live pipeline unreliable.

Examples might include a real correctness issue in signal routing, a major rendering bottleneck, missing essential observability, or a shared capability that is demonstrably required.

Only classify a change as P0 if the evidence supports that priority.

P1 — High-value improvements

Work that substantially improves visual quality, performance, audio responsiveness, or authoring efficiency at reasonable cost.

P2 — Optional enhancements

Useful improvements that are not required to demonstrate the pipeline.

Defer or reject

Features that should remain untouched, be postponed, or be explicitly rejected because they duplicate existing functionality, have weak benefits, impose excessive cost, or distract from the milestone.

For every proposed change, include:

* Problem being solved.
* Existing implementation and why it is insufficient.
* Proposed solution.
* Alternatives considered.
* Expected visual benefit.
* Expected performance benefit or cost.
* Engineering effort estimate.
* Regression risk.
* Relevant dependencies.
* Validation strategy.
* Whether it is a prerequisite for The Living Prism.

Estimate engineering effort using an explicit scale such as S, M, L, and XL, with a brief definition of each. Where uncertainty is high, give a range and explain what must be investigated before the estimate can be trusted.

Identify dependencies and recommend an order that minimizes rework.

Prefer a small number of high-value, independently testable changes over a long list of loosely related enhancements.

⸻

13. Testing and Acceptance Criteria

Define how we will know the architectural milestone has succeeded before implementing it.

At minimum, propose acceptance criteria for:

Functional correctness

* Representative audio signals can reach their intended destinations.
* MIDI control can alter a visual parameter predictably.
* Event-driven and continuous modulation behave correctly.
* State, accumulation, decay, and reset behavior are deterministic enough for performance use.
* Missing or silent input does not cause pathological behavior.
* Existing representative scenes continue to function.

Performance

* Baseline measurements exist for representative scenes.
* New shared effects can be profiled independently where practical.
* CPU and GPU frame times are measured separately.
* Quality reduction has understandable, testable behavior.
* Stress tests can expose regressions that average FPS would conceal.
* The final targets reflect the actual intended output mode and hardware.

Do not invent numeric performance thresholds. Derive them from the project’s established targets and measured baseline, and propose explicit thresholds for my approval where none currently exist.

Authoring and diagnostics

* A developer can determine whether an audio or MIDI signal is arriving.
* A developer can trace an important modulation route.
* A developer can identify the major rendering cost when performance degrades.
* Shared capabilities can be reused without duplicating substantial logic.
* Relevant documentation and tests explain the intended behavior.

Integration readiness

The engine should be able to support the planned scene without requiring an unplanned architectural rewrite once the detailed art specification arrives.

This does not mean all possible VFX need to exist before the scene begins. It means the architecture and essential shared systems are sufficiently understood, and any remaining implementation work is clearly identified.

⸻

14. Required Final Deliverables

Return one integrated report containing these sections:

1. Executive summary: What is already strong, what is genuinely missing, and the most important recommendation.
2. Verified architecture map: The current live rendering and audio-reactivity pipeline, with source references.
3. Rendering capability matrix: Existing systems, limitations, gaps, evidence, costs, and relevance.
4. Audio/MIDI/modulation audit: Signal semantics, routing, timing, input handling, and opportunities.
5. Performance report: Baselines, known bottlenecks, profiling gaps, and adaptive-quality assessment.
6. Reusable VFX assessment: Which effects should be shared, improved, or remain scene-specific.
7. Research findings: Relevant techniques, cited sources, alternatives, and resulting recommendations.
8. Refactor/removal candidates: Evidence-backed opportunities to simplify or consolidate the architecture.
9. Requirements-to-capabilities map: How the existing engine supports the planned scene and what remains.
10. Prioritized implementation plan: P0, P1, P2, and defer/reject recommendations with dependencies and effort estimates.
11. Testing and acceptance plan: How the changes and eventual scene will be validated.
12. Risks and open questions: Anything that needs my decision before implementation.
13. Milestone readiness verdict: Whether the architecture is ready for the scene specification, ready after specific prerequisite work, or not ready.

Include an appendix with key files, symbols, relevant tests, benchmark commands, research references, and any temporary investigative artifacts created.

The report should be detailed enough that we can review the architecture and make informed implementation decisions without repeating the entire investigation.

Avoid unsupported claims, generic filler, and lists of features that aren’t connected to evidence or the scene’s needs.

⸻

15. Final Instructions and Stop Condition

When the report and plan are complete:

1. Summarize the findings and the highest-priority decisions.
2. Clearly separate confirmed facts from estimates and recommendations.
3. Tell me which changes you recommend implementing first and why.
4. Identify any decisions that require my approval.
5. State whether you are ready for the separate Living Prism art-direction and scene-construction specification.

Stop after presenting the audit and proposed plan. Do not begin the broad implementation phase until I approve it.

The intended outcome is not merely a new scene. It is a well-understood, reusable, measurable live-rendering pipeline that can support The Living Prism and make future ambitious AV Gen scenes easier to build, more musically expressive, and more reliable in live performance.
