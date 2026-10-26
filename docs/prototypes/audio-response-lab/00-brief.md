<!-- Owner brief, verbatim, 2026-10-26. Queued for the next free agent slot. -->

# AV Gen: Audio Response Lab — Generic Musical Validation Scene

## Mission

Build a reusable, generic **Audio Response Lab** scene and a self-contained audio-generation and validation workflow to test the musical correctness of AV Gen's live audio-analysis and response systems.

This is an engineering diagnostic environment, not an artistic showcase.

The scene must make it easy to see whether signals are correct, whether they arrive on time, whether they are attributed to the right sources, and whether response controls behave predictably across different musical conditions.

The system must generate its own deterministic audio fixtures, run them through the actual production analysis pipeline, exercise the actual scene-response mapping infrastructure and produce a reproducible report.

Do not begin by implementing a new audio-analysis system. Audit and reuse the existing implementation first.

## 1. Research and implementation plan

Before changing code, inspect:

- Audio input and analysis architecture
- Audio buffering, sample-rate conversion and timestamps
- Source/stem configuration and role-specific analysis
- Onset detection and frequency-band analysis
- Beat tracking, phase estimation and confidence
- Envelope followers, smoothing and signal conditioning
- Response mappings and scene parameter controls
- Scene lifecycle and transitions
- MIDI input and control routing
- Existing automated tests, audio fixtures and profiling facilities
- Offline and live rendering paths

Document what already exists, what is testable and what requires new instrumentation.

Prefer the smallest implementation that gives meaningful end-to-end validation. Do not build a general-purpose audio editor, replace the renderer or duplicate existing signal-processing functionality.

## 2. Scene design: diagnostic, not decorative

Build a compact, fixed-layout 3D scene with a dark neutral background and clearly separated diagnostic objects.

Use simple, inexpensive geometry with distinct colors, shapes and labels. Avoid expensive materials, elaborate environments, complex post-processing and large particle simulations.

The scene must remain readable at 1080p30, even with every diagnostic control active.

### A. Continuous energy indicators

Create clearly identifiable indicators for:

- Overall energy
- Low-frequency energy
- Mid-frequency energy
- High-frequency energy
- Envelope level

Represent each signal using a combination of a meter, bar or other directly interpretable visual indicator.

The indicators must expose the actual normalized values used by the response system, not a separate approximation calculated by the scene.

### B. Onset event indicators

Create a dedicated visual event indicator for general onset detection.

Each detected onset should produce a short, unmistakable flash or marker, with a visible return to baseline.

Where useful, show the detected event timestamp, event strength and available confidence.

The event display must make missed events, duplicate triggers, latency and false triggers easy to identify.

Do not assume mix-level onset events are kick, snare or hi-hat classifications.

### C. Source and stem isolation indicators

When multiple audio sources or stems are configured, display each source independently.

Show source identity, available energy measures and source-specific event indicators.

Make it immediately apparent when an event in one stem incorrectly activates another source's indicator.

If a dedicated drums-role source supports kick, snare and hi-hat classification, show those signals separately. Do not require those identities from an ordinary stereo mix.

Handle unavailable signals explicitly rather than silently substituting unrelated signals.

### D. Beat and phase indicator

Display beat and phase information using a simple, readable indicator such as a phase dial or moving marker.

Show tracker state and confidence when available.

The indicator must expose uncertainty, acquisition, loss of lock and reacquisition rather than presenting every estimate as reliable.

Include tempo-change and syncopated test material so a tracker cannot pass merely by following a fixed periodic pulse.

### E. Response-control probes

Create several distinct diagnostic objects driven by the real AV Gen response mapping system.

Examples:

- A sphere whose scale follows smoothed bass energy
- A light or emissive surface that flashes on general onset events
- A color panel driven by sustained spectral characteristics
- A ring or marker driven by beat phase
- A separate object driven by a selected stem's energy
- A MIDI-controlled object when the existing MIDI mapping system is available

Use bounded, predictable responses with clear baselines and release behavior.

These objects are probes, not artistic effects. Their purpose is to make the relationship between a signal and a visual parameter unmistakable.

### F. Event timeline

Provide a compact diagnostic timeline or event strip that can display:

- Ground-truth fixture events
- Detected onsets
- Beat markers
- Source/stem events
- Selected response-parameter changes

Ground-truth markers must be displayed only in diagnostic mode. Live mode must not receive hidden information from the test fixture or use future ground-truth events to improve its analysis.

Align the timeline to the actual audio clock and expose relevant timing offsets.

### G. Diagnostics overlay

Where existing UI infrastructure permits, expose:

- Current fixture and playback position
- Input sample rate and channel configuration
- Selected source or stem
- Analysis block/hop size where available
- Relevant signal values
- Detected event timestamps
- Beat tracker state and confidence
- Response-parameter values
- CPU/GPU frame times and missed deadlines

Keep the overlay compact. Prefer existing ImGui-based diagnostic conventions over building a new UI framework.

The scene must be useful even if the detailed overlay is disabled.

## 3. Deterministic audio fixture generator

Create a reproducible fixture generator that produces audio with known event timing and source identity.

First check for suitable existing test utilities. Reuse them where possible. If no appropriate generator exists, implement a lightweight deterministic generator using existing audio dependencies or a minimal purpose-built solution.

No cloud APIs, paid services or external generative-audio services should be required.

The audio does not need to be a finished song. It must be sufficiently musical and varied to exercise the real analysis pipeline.

Generate the following fixture categories.

### Sparse material

- Isolated bass notes
- Separated percussive events
- Sustained notes
- Long silent intervals

Purpose: measure onset precision, envelope release, baseline noise and false triggers.

### Dense material

- Overlapping bass and percussion
- High-frequency transients
- Sustained harmonic layers
- Closely spaced events

Purpose: measure event separation, masking, signal stability and frequency selectivity.

### Noisy material

- Broadband noise
- Noisy percussion
- Sustained noisy textures
- Multiple signal-to-noise ratios

Purpose: identify false triggers and unstable responses.

### Syncopated material

- Offbeat accents
- Irregular subdivisions
- Unexpected rests
- Displaced accents
- Rhythms that do not follow a four-on-the-floor pattern

Purpose: evaluate onset timing and beat/phase tracking without assuming a rigid pulse.

### Dynamic transitions

- Quiet introduction
- Gradual build
- Sudden loud section
- Abrupt drop
- Silence
- Recovery
- Tempo change

Purpose: evaluate adaptation, tracker lock, envelope behavior and stale response state.

### Source-isolation material

Generate independent stems with known event schedules, including overlapping events and silent sources. Mix them together while preserving the originals.

Purpose: compare source-specific analysis with ground truth and determine whether activity leaks between sources.

### Adversarial material

Include sustained tones, bass without percussion, percussion-like sounds outside expected frequency bands, very close events, repeated transients and low-level noise.

Purpose: identify cases that defeat simplistic detectors or cause unstable responses.

Use deterministic seeds and store each fixture's audio, generation parameters and ground-truth manifest together.

## 4. Ground-truth manifest

For every generated fixture, record:

- Sample rate and channel layout
- Duration and random seed
- Exact generated event onset sample indices
- Event category and source identity
- Intended beat times, tempo and phase where applicable
- Changes in energy and spectral content
- Silent intervals
- Ambiguous or deliberately adversarial events

Ground truth must describe what the generator actually produced, not what the detector later inferred.

Keep sample indices as the canonical reference for event timing. Convert them to timestamps using the known sample rate, and account for any documented input, analysis or scheduling delay.

## 5. Validate the actual production pipeline

Run generated audio through the same production analysis code used by live AV Gen.

Do not use fixture metadata to bypass analysis or manufacture successful results.

The diagnostic scene must consume the actual resulting analysis signals and drive its response probes through the actual response mapping system.

Controlled signal injection may be used for isolated unit tests of downstream mappings, but those tests must be reported separately from end-to-end audio tests.

Instrument the pipeline where necessary to record:

1. Ground-truth event time
2. Input audio time
3. Analysis event time
4. Conditioned signal value
5. Response mapping output
6. Scene parameter value
7. Rendered frame timestamp

Document the clock and timestamp conventions at each stage.

## 6. Automated measurements

Implement automated tests and report the following.

### Onset detection

- Precision and recall
- Median and 95th-percentile timing error
- Missed events
- Duplicate detections
- False events per minute during silence and sustained content

### Energy and envelopes

- Correlation with known reference envelopes
- Response delay
- Attack and release behavior
- Overshoot and baseline recovery
- Response to different input levels

### Frequency selectivity

- Correct response to known low-, mid- and high-frequency content
- Unintended cross-band response
- Response to broadband and noisy signals

### Beat and phase tracking

- Beat-time error
- Phase error
- Tracker acquisition time
- Loss-of-lock behavior
- Reacquisition after tempo changes
- Confidence behavior during ambiguous passages

### Source isolation

- Correct source attribution where the signal is supported
- False activity in silent stems
- Leakage between simultaneously active sources
- Behavior when only a stereo mix is available

### Response mapping

- Correct direction and magnitude of parameter changes
- Bounded output
- Smoothing and release behavior
- Correct transient duration
- Absence of stale values after silence or scene transitions

### Rendering and timing

- Time between detected event and response parameter update
- Time between parameter update and first corresponding rendered frame
- CPU/GPU frame times
- Missed deadlines
- Sustained 1080p30 performance on the designated reference hardware

Report distributions and raw measurements alongside summary metrics.

Set acceptance thresholds based on the actual implementation, sample rate, analysis hop size and documented latency. Do not invent arbitrary thresholds simply to make the suite pass.

## 7. Validate the visual response

For each fixture, run the Audio Response Lab scene and capture a synchronized diagnostic recording.

Produce a synchronized timeline containing:

- Audio playback
- Ground-truth event markers
- Detected events
- Relevant analysis signals
- Response-control values
- The rendered diagnostic scene

The video should show the actual scene driven by the production pipeline. Ground-truth information may be overlaid separately for comparison but must never control the live response.

Review whether the visible behavior matches the intended signal semantics.

For example:

- An onset should cause a discrete event rather than a prolonged flash.
- A sustained note should produce a sustained response rather than repeated transient triggers.
- A silent interval should return the scene to baseline.
- Syncopated events should produce accents at their actual times.
- A source-specific event should not activate unrelated source indicators.
- A tempo change should not cause the scene to continue following an obsolete phase estimate indefinitely.

Create automated checks where possible and a short human-review checklist for qualities that cannot be established reliably by numerical metrics alone.

## 8. Cross-scene response transfer

The Audio Response Lab is a reusable diagnostic environment, but the underlying validation must also exercise response controls in existing scenes.

Select at least two existing scenes with different visual characteristics and reuse compatible audio fixtures and response controls.

Test whether normalized signals, transient controls, smoothing and response ranges remain sensible when mapped to different visual parameters.

Do not require identical visual output across scenes. Require consistent signal meaning and predictable control behavior.

Report controls that are inherently scene-specific rather than pretending every control should transfer universally.

## 9. Deliverables

Provide:

1. The Audio Response Lab scene integrated into the standard AV Gen workflow.
2. A deterministic audio-fixture generator.
3. Generated audio fixtures and ground-truth manifests.
4. Automated tests covering every required fixture category.
5. A diagnostic timeline and synchronized visual captures.
6. A machine-readable results report and a readable summary.
7. Cross-scene response-transfer results.
8. A performance report for the reference hardware.
9. Documentation explaining how to add fixtures, run the suite and interpret failures.
10. Regression tests for any production code changed to support validation.

## 10. Acceptance criteria

The work is complete only when:

- The scene can be selected and run through the normal AV Gen workflow.
- The diagnostic indicators expose actual production analysis and response values.
- The generated fixtures are deterministic and have verified ground-truth manifests.
- The end-to-end suite exercises the real production audio-analysis and mapping pipelines.
- Sparse, dense, noisy, syncopated, dynamically changing and source-isolation cases are covered.
- Timing, false triggers, tracker lock and source leakage are measured explicitly.
- At least two additional existing scenes are evaluated for response-control transfer.
- Visual captures and diagnostic traces are synchronized and reproducible.
- Performance is measured against the 1080p30 target.
- Failures and limitations are reported honestly.
- The test infrastructure can be reused for future AV Gen audio-analysis and scene changes.

## Working principles

Build the diagnostic infrastructure first, then connect the scene, then run the fixtures.

Keep the scene intentionally simple so that visual behavior is easy to interpret. Do not spend time polishing the environment, building a complex world or introducing expensive effects.

Do not expand the analysis API just to increase the number of published signals. Prioritize correct musical interpretation, useful response controls and measurable end-to-end behavior.

The final outcome should tell us not just whether AV Gen detects audio, but whether it understands enough of the music to drive a responsive, stable and convincing live visual performance.
