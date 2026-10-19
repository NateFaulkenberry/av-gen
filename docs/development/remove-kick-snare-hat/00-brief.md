> Owner's brief, saved verbatim on 2026-10-19.

# AV Gen: Remove Legacy Live Kick / Snare / Hi-Hat Detection
## Blast Radius Analysis First, Then Controlled Removal

### Objective

Remove the existing kick, snare, and hi-hat detection feature from AV Gen's live panel.

The planned live multichannel audio architecture will allow users to identify and assign live audio inputs to meaningful sources. That makes the current dedicated kick/snare/hi-hat detection UI redundant as a permanent part of the live panel.

This task is specifically about removing the legacy feature cleanly, after determining its complete blast radius.

**Do not begin by deleting the UI or removing similarly named analysis code. Investigate the feature's dependencies first, document the impact, and formulate a removal plan before making changes.**

### Phase 1 — Analyze the blast radius

Perform a repository-wide audit of the existing kick/snare/hi-hat detection feature.

Search for all relevant references, implementations, registrations, configuration, and consumers, including:

- Live-panel UI components and controls.
- Kick, snare, and hi-hat detection algorithms.
- Associated state, parameters, settings, and configuration persistence.
- Signal-bus registrations and signal names.
- Transient detection, onset detection, spectral analysis, and thresholding dependencies.
- Audio callback or worker-thread processing related to the feature.
- Modulation sources, parameter bindings, and scene dependencies.
- Debug overlays, meters, diagnostics, and logging.
- Tests, fixtures, benchmarks, documentation, and screenshots.
- Serialization, project settings, presets, and default configurations.
- Any shared utilities used by other audio-analysis features.

Use symbol references, call-site inspection, and actual data-flow tracing. A text search alone is insufficient to establish whether a component is exclusively used by this feature.

Trace the feature from its source audio through analysis and state publication to the live panel and any downstream consumers.

Determine precisely:

1. Which code exists exclusively to support kick/snare/hat detection.
2. Which analysis utilities are shared with other functionality.
3. Whether any scenes or modulators currently consume the detector outputs.
4. Whether any configuration or saved project data references the feature.
5. Whether the detector runs continuously even when its UI is hidden or disabled.
6. Whether removing it affects audio-thread workload, CPU usage, or existing performance measurements.
7. Whether the existing feature exposes signals that could be used by scenes or external workflows.
8. Whether the live panel has any other controls or behaviors coupled to the detector.

Identify the actual source files, classes, functions, signal names, and dependencies involved.

Do not assume the implementation matches the feature's visible label. Follow the real execution path.

### Phase 2 — Produce a removal plan before editing

Write a concise but technically specific blast-radius report.

Include:

- A description of the current feature and its data flow.
- An inventory of code and UI to remove.
- Shared components that must remain.
- Downstream dependencies that would break if removed naively.
- Any project-format, configuration, or migration implications.
- Tests that must be updated or added.
- Expected performance implications.
- The proposed order of operations.

Classify findings into three groups:

**A. Remove**
Code, UI, state, configuration, and processing exclusively required by the obsolete feature.

**B. Preserve**
Shared analysis infrastructure and utilities required by other audio-reactivity features.

**C. Investigate before deciding**
Signals, configuration, or code with unclear consumers or possible scene dependencies.

Do not delete a shared analysis component merely because the kick/snare/hat detector happens to use it.

Do not add a replacement detector or begin implementing the planned multichannel input system as part of this task.

Once the report and removal plan are complete, proceed with the removal if the blast radius is understood and no unresolved dependency requires a design decision. If a significant ambiguity remains, report it before proceeding.

### Phase 3 — Remove the obsolete feature

Remove the dedicated kick/snare/hat detection feature from the live panel, including its associated UI, controls, state, and detector-specific processing where no longer needed.

The removal must include all exclusively owned implementation details, not just hide the controls.

Depending on the actual architecture, this may include:

- Detection-specific processing stages.
- Detector-specific thresholds and smoothing.
- Detector-specific event state and counters.
- Signal registrations and publications used exclusively by the feature.
- Unused settings and configuration.
- Detector-specific debug output.
- Obsolete tests and fixtures.
- Documentation describing the feature as part of the live panel.

These are investigation targets, not a prescriptive deletion list. Remove an item only after establishing that it is genuinely obsolete.

### Phase 4 — Preserve shared audio analysis

The following categories of functionality must remain intact wherever they serve other parts of AV Gen:

- General transient or onset analysis.
- RMS, envelope, peak, and spectral analysis.
- Existing stereo audio capture.
- The BlackHole 2ch workflow.
- The audio signal bus and modulation system.
- Audio-reactive scene behavior.
- MIDI processing and MIDI-reactive behavior.
- Audio-device selection and existing input configuration.
- Offline rendering and audio analysis.
- Any shared signal-processing utilities or infrastructure.

Do not remove general transient detection merely because the legacy kick/snare/hat detector consumes it.

Do not change the semantics or names of shared signals unless the audit proves that a signal is exclusive to the removed feature and no supported consumer relies on it.

Do not remove generic modulation sources simply because no current scene appears to use them; first inspect the relevant registration, configuration, and extension mechanisms.

### Phase 5 — Configuration and data integrity

Inspect how the existing feature's settings are persisted.

If dedicated detector settings are no longer needed, remove their active implementation and obsolete defaults cleanly.

If older project files or configuration may contain these settings, determine whether the existing deserialization logic safely ignores unknown fields.

Do not introduce a compatibility layer unless a real supported workflow requires it. Do not perform a broad configuration-format migration for this isolated removal.

Preserve unrelated settings and existing project data.

### Phase 6 — Tests and validation

Update or remove tests specifically associated with the obsolete detector, as justified by the blast-radius report.

Add or retain regression coverage to verify:

- The live panel no longer exposes kick/snare/hi-hat detection controls or status displays.
- No obsolete detector-specific processing remains active.
- Shared transient and spectral analysis still works for its other consumers.
- Existing audio capture and stereo analysis still function.
- Existing signal-bus and modulation behavior remains intact.
- Audio-reactive scenes continue to receive their required signals.
- MIDI behavior is unchanged.
- Configuration loading remains safe.
- The application builds cleanly.
- Relevant tests, static analysis, and CI checks pass.

Run the appropriate existing test suite and targeted tests. Do not claim the complete suite passes if it was not run.

If the repository has a reproducible live-render benchmark, compare performance before and after the change where feasible. Removing detector processing may reduce CPU work, but report measured results rather than assuming a speedup.

### Phase 7 — Final audit

Before declaring completion:

- Review the final diff.
- Search again for obsolete detector symbols, UI labels, and dead configuration.
- Confirm no orphaned signal registrations, callbacks, or unused state remain.
- Confirm shared analysis infrastructure was not accidentally removed.
- Confirm there are no unrelated refactors or formatting changes.
- Update relevant documentation.
- Verify the live panel remains coherent after the controls are removed.

### Final deliverables

Provide:

1. The blast-radius report.
2. The removal plan.
3. A summary of the code and UI removed.
4. A summary of shared functionality intentionally preserved.
5. Any discovered scene or modulation dependencies.
6. Tests and build checks performed, with actual results.
7. Any measured performance changes.
8. Remaining risks or follow-up work.

**Guiding principle:** Remove the dedicated live kick/snare/hat detection feature and its exclusive processing, not the general-purpose audio-analysis capabilities that make AV Gen reactive. The future source-assignment workflow is the architectural direction; this task should prepare the existing application for that direction without prematurely implementing it.
