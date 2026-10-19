> Owner's brief, saved verbatim on 2026-10-19.

# AV Gen: Stem-Aware Offline Analysis & Live Multichannel Audio
## Architecture Research, Planning, and Implementation Specification

### 1. Mission

Investigate, design, implement, and validate two complementary AV Gen capabilities:

1. **Offline stem-aware analysis:** Allow users to provide a stereo mix for playback plus multiple time-aligned audio stems for independent analysis. Use the resulting source-specific signals to drive AV Gen's existing modulation and scene systems.
2. **Live multichannel input:** Allow users to select an available macOS Core Audio input device, inspect its input channels, assign semantic source labels, and expose independently analyzed channels to AV Gen's existing signal system.

This task begins with research and a written implementation plan. Do not begin production implementation until the research, architecture audit, proposed design, risks, and test plan have been documented and reviewed.

The objective is better musical understanding and richer audio-reactive visuals, without compromising live audio reliability, rendering performance, or the existing stereo workflow.

### 2. Explicit scope and non-goals

**In scope**
- Offline analysis of externally supplied stems.
- Stereo mix playback, with stems used as analysis sources.
- Programmatically generated test audio and stems.
- Live input device discovery and channel enumeration.
- Per-channel source labeling and analysis.
- Reuse of AV Gen's existing signal bus, timeline, modulation, parameter, and scene architecture.
- Core Audio hardware testing with a Universal Audio Volt interface when connected.
- Performance profiling, automated tests, documentation, and graceful fallback.

**Out of scope**
- Implementing source separation or integrating a source-separation model.
- Reassembling stems into a playback mix.
- Building a DAW, multitrack recorder, or general-purpose mixer.
- Rewriting the rendering backend or replacing the existing audio architecture without evidence that it is necessary.
- Requiring a physical audio interface for automated tests.
- Requiring eight channels to be enabled when only two are needed.
- Adding unnecessary compatibility layers, abstractions, or user-facing controls without a concrete use case.

The existing BlackHole 2ch workflow must continue to work.

### 3. Phase 0 — Repository audit and deep research

This phase is mandatory and must precede implementation.

#### 3.1 Audit the actual repository

Trace the complete current audio pipeline from input capture to visual output.

Inspect and document:

- Audio-device discovery and selection.
- The existing BlackHole/stereo input path.
- Audio callback implementation and callback ownership.
- Audio buffer formats, sample rates, channel layouts, and buffer sizes.
- Audio-to-analysis handoff and any ring buffers or queues.
- Envelope, RMS, peak, FFT, spectral-band, onset/transient, and other existing analysis stages.
- Signal-bus registration, naming, update cadence, timestamps, and thread ownership.
- Timeline construction and offline rendering behavior.
- Parameter modulation and scene integration.
- Audio/MIDI enablement and runtime configuration.
- Existing test fixtures, integration tests, benchmarks, and CI.
- Existing CPU/GPU profiling and performance telemetry.
- Existing settings and device-persistence conventions.

Identify every place where stereo is assumed rather than merely supported.

Trace representative signals end-to-end. For example, establish how a current stereo low-frequency energy signal reaches a modulator and ultimately changes a scene parameter.

Document the relevant source files, classes, call paths, ownership boundaries, and thread boundaries. Do not infer the architecture from filenames alone.

Determine whether the existing architecture already supports multiple analysis sources internally and only lacks multichannel capture, or whether stereo assumptions exist throughout the signal pipeline.

#### 3.2 Research authoritative technical sources

Research current Apple Core Audio documentation and relevant implementation examples.

Investigate:

- HAL audio-device enumeration and stable device identification.
- AUHAL input capture or the appropriate existing abstraction if the repository already uses another supported Core Audio capture path.
- Input stream formats and channel-layout discovery.
- Interleaved versus non-interleaved audio.
- Sample-rate negotiation and conversion.
- Audio callback real-time safety.
- Audio-device change notifications and reconnection.
- Buffer sizing, latency, overload behavior, and xrun detection.
- Timestamping and synchronization between captured audio and analysis.
- Core Audio device lifecycle and error recovery.
- Whether the existing application permissions and packaging require any changes.

Prefer current Apple Developer Documentation and official sample code, supplemented by established engineering references where useful.

Do not introduce a new capture API solely because it appears in a tutorial. Compare the research against AV Gen's current implementation and justify the chosen approach.

#### 3.3 Research the Universal Audio Volt interface

The target hardware is a Universal Audio Volt interface connected to a macOS machine through Core Audio.

First determine the exact Volt model from the connected device's reported name and capabilities, if available. Do not assume that all Volt models have the same channel count.

Use Universal Audio's official setup documentation and the relevant model's documentation to establish expected input capabilities and any device-specific constraints.

When the hardware is connected:

- Verify that macOS discovers the interface.
- Enumerate its actual input channels through the application.
- Confirm the reported device format and available channel count.
- Test actual audio capture rather than treating device enumeration as proof of successful input.
- Check whether each physical input can be accessed independently.
- Determine what physical signal is needed to exercise each input.
- Verify behavior when the device is unplugged and reconnected.

Do not invent input signals, channel counts, or successful hardware test results.

**Important hardware distinction:** Volt models differ in their I/O configurations. Some desktop models expose only two input channels, while others provide four. An interface with two inputs cannot demonstrate eight independent physical inputs. Use the Volt to validate the actual hardware path it supports, and use a synthetic multichannel backend to validate higher channel counts.

Do not require the Volt to be connected during ordinary development, CI, or automated testing.

#### 3.4 Research performance and audio-analysis costs

Determine the actual cost of the existing analysis pipeline before deciding how many stages should run per channel.

Evaluate:

- Per-channel sample ingestion.
- Envelope, peak, RMS, and transient analysis.
- FFT and spectral analysis.
- Buffer-copying and channel deinterleaving.
- Worker scheduling and queue contention.
- Signal publication and synchronization.
- Audio callback deadlines.
- Interactions with CPU-heavy scene work and GPU-bound rendering.

Use measured results wherever possible. Clearly distinguish measured performance from estimates.

#### 3.5 Deliverable: research report

Write a report containing:

1. Current architecture and relevant code paths.
2. Stereo-specific assumptions and extension points.
3. Findings from current Apple documentation.
4. Findings specific to the detected Volt model, if hardware is available.
5. Proposed offline-stem architecture.
6. Proposed live-capture architecture.
7. Threading and buffer ownership design.
8. Source labeling and signal naming conventions.
9. Performance budget and benchmark methodology.
10. Risks, alternatives considered, and unresolved questions.

Cite external technical references with URLs and explain how they apply to the actual codebase.

### 4. Phase 1 — Formulate and review an implementation plan

After completing research, produce a concrete plan before writing production code.

Break the work into independently testable milestones. For each milestone, identify:

- Files and modules expected to change.
- Interfaces and data structures to add or modify.
- Dependencies and build-system changes, if any.
- Automated tests.
- Manual or hardware tests.
- Performance measurements.
- Failure modes and rollback strategy.
- Acceptance criteria.

Explicitly compare at least two plausible approaches where a meaningful architectural choice exists.

Prefer the smallest architecture that supports both offline and live sources through the existing signal infrastructure.

Do not begin implementation if the design requires guessing about a critical audio-thread constraint or device capability. Investigate that uncertainty first.

The plan must distinguish:
- Work that can be completed without hardware.
- Work that requires the Volt to be connected.
- Work that requires physical audio signals or external routing.
- Work that can be fully automated in CI.

### 5. Phase 2 — Offline stem-aware analysis

#### 5.1 User workflow

The user supplies:

- One authoritative stereo playback file.
- Zero or more analysis stems.

Each stem has a user-editable semantic label, such as:
- Drums
- Kick
- Snare
- Bass
- Synth
- Guitar
- Vocals
- Other
- Custom name

Do not hardcode a fixed number of stems or assume every project has the same arrangement.

For the first iteration, support the existing repository's appropriate audio-file formats. Prefer formats already supported by the current audio infrastructure rather than adding a new decoding dependency unnecessarily.

#### 5.2 Playback and analysis separation

The stereo mix is the playback source.

Stems are analysis-only sources. They must not be summed into the playback mix, substituted for it, or played as parallel tracks.

Maintain the existing playback path and rendering synchronization behavior.

#### 5.3 Validation and alignment

Before analysis, inspect each file's:

- Sample rate.
- Channel count.
- Duration and frame count.
- Decoding status.
- Timeline offset, if supplied.
- Format and supported bit depth.
- Validity of its samples.

Provide explicit diagnostics for mismatched durations, sample rates, or suspected offsets.

Matching duration alone is not proof of alignment.

The initial implementation should support explicit alignment metadata or user-specified offsets if the architecture warrants them. It must not silently guess offsets from waveform correlation.

Where the existing timeline model allows it, represent offsets in a well-defined time or sample domain and convert carefully when sample rates differ.

Decide how to handle missing stem data at the start or end of the playback timeline. A stem with no data for a given interval must not produce fabricated transients or stale feature values.

Do not unnecessarily reject a valid project simply because one stem has a different sample rate. Determine whether safe offline conversion is supported by the existing audio infrastructure, and document the chosen policy.

#### 5.4 Analysis features

Reuse existing analysis code wherever practical.

Initially support per-stem features such as:

- RMS and energy envelope.
- Peak amplitude.
- Smoothed energy.
- Transient or onset strength, using existing capabilities where available.
- Spectral bands and spectral features, subject to the existing implementation and measured cost.

Do not promise semantic features such as reliable note transcription or instrument identification unless they are explicitly implemented and validated.

Each feature must have defined units, update cadence, timing semantics, and behavior at file boundaries.

#### 5.5 Integration with the existing signal system

Expose stem-derived signals through the same signal bus and timeline used by existing AV Gen modulators.

Use a consistent, collision-safe naming scheme. For example:

`stem.drums.rms`

`stem.drums.transient`

`stem.bass.rms`

`stem.synth.low`

These names are illustrative. Follow the repository's actual naming conventions if they differ.

Signal identifiers must remain stable during the project session, including when two stems have similar labels. Define how renamed stems, duplicate labels, removed stems, and missing stems behave.

Do not build a parallel modulation graph just for stem analysis.

#### 5.6 Deterministic offline processing

Offline analysis should be deterministic for the same input, settings, and software version, within documented numerical tolerances.

Avoid coupling feature extraction to rendering frame rate. The analysis timeline should have a defined temporal resolution and reliable timestamps.

Determine whether analysis should be performed:
- Before rendering and stored in memory or a cache.
- Incrementally during a preparation stage.
- On demand, if the existing architecture supports that safely.

Choose based on actual repository constraints and memory usage, not assumption.

### 6. Phase 3 — Programmatically generated test audio

Create a deterministic audio fixture generator, preferably in Python using established libraries already available in the development environment.

Do not require downloading commercial music, using proprietary stems, or invoking a cloud service.

The generator must create:
- A stereo reference mix.
- A drum stem.
- A bass stem.
- A synth stem.
- Optionally, one additional stem if useful for validating arbitrary source counts.

Generate the stereo mix by combining the known stems offline, with documented gain and panning. This is solely a test-fixture generation step. AV Gen itself must still analyze the supplied stems and play the supplied stereo mix independently.

The fixture should contain clearly identifiable events, for example:

- Kick impulses at known timestamps.
- Snare hits at different timestamps.
- A hi-hat pattern with known subdivisions.
- Bass notes with known start times and durations.
- Synth notes with both short attacks and sustained portions.
- Deliberate gaps and non-overlapping events.
- Some overlapping events to ensure simultaneous sources work correctly.

Use fixed seeds wherever randomness is involved.

Write accompanying fixture metadata describing the sample rate, expected durations, event timestamps, labels, gains, offsets, and generation parameters.

The generated files should be reproducible from a checked-in script. Prefer generating fixtures into a build/test directory rather than committing large redundant audio binaries.

Choose a reasonable sample rate, such as 48 kHz, unless the repository's existing conventions favor another rate. Make fixture duration long enough to exercise the complete workflow but short enough for rapid tests.

#### 6.1 Offline tests

Automated tests must verify:

- All expected files decode successfully.
- Stem and mix metadata is read correctly.
- Valid aligned inputs are accepted.
- Invalid or mismatched inputs produce actionable diagnostics.
- Deliberately offset stems are detected or represented according to the alignment policy.
- A stem's events appear in its own signals, not another stem's signals.
- Transients are detected within a documented tolerance of known event timestamps.
- Sustained energy follows the expected note intervals.
- The generated stereo mix is used for playback, with no accidental stem summing in AV Gen.
- Feature timestamps remain consistent throughout offline rendering.
- Missing or silent stems do not generate false events.
- Results are repeatable within defined tolerances.

Avoid brittle assertions that demand identical floating-point output across different hardware. Test meaningful properties and numerical tolerances.

### 7. Phase 4 — Live multichannel Core Audio

#### 7.1 Device abstraction

Audit whether the existing input abstraction can represent arbitrary input channel counts. Extend it only as necessary.

The input subsystem should expose:

- Available input devices.
- Stable device identifiers, not display names alone.
- Device names for presentation.
- Available input channels and stream formats.
- Current connection and capture state.
- Supported sample rates and channel counts.
- Relevant errors and device lifecycle changes.

The architecture must support a two-channel source and a multichannel source without requiring the rest of AV Gen to know the hardware-specific implementation.

Keep device enumeration, capture, analysis, and UI responsibilities separated according to existing project conventions.

#### 7.2 Capture and channel mapping

Allow the user to select an input device and assign its channels to named analysis sources.

For example, a four-input device might be mapped as:
- Input 1 → Kick / Drum Bus
- Input 2 → Bass
- Input 3 → Synth Left
- Input 4 → Synth Right

This is only an example, not a prescribed mapping.

Support mono channels and linked channel pairs. Decide how to handle a pair assigned to one semantic source: either analyze its channels independently and expose a documented combined signal, or provide a properly defined linked-source analysis path. Avoid silently averaging unrelated channels.

A physical mixer may provide discrete outputs for drum, bass, and synth buses. AV Gen must not assume that the interface's physical input channels contain those sources automatically. Document the external routing needed to supply them.

#### 7.3 Real-time safety

The audio callback must not perform heavy analysis, filesystem operations, blocking synchronization, or unbounded allocation.

Use a bounded, thread-safe handoff consistent with the current architecture. Preallocate buffers as appropriate.

The design must define:
- Buffer capacity and overflow policy.
- Behavior when the consumer falls behind.
- Audio timestamps and sequence handling.
- Handling of dropped or discontinuous buffers.
- Device stop/start and reconfiguration.
- Safe publication of features to the signal bus.

Do not let the UI or renderer block the capture callback.

A dropped analysis buffer must not cause stale transient signals to remain asserted indefinitely. Publish diagnostics for dropped buffers and recovery events.

#### 7.4 Analysis workload tiers

Start with inexpensive features:
- RMS and envelope.
- Peak.
- Basic transient detection.

Evaluate optional spectral analysis separately.

Do not blindly run the full analysis pipeline on every channel. Add per-source feature configuration only where it has a clear benefit and does not overcomplicate the UI.

The architecture should permit different analysis configurations per channel or source, subject to the existing signal system.

### 8. Phase 5 — Hardware-independent multichannel testing

Create a synthetic multichannel input backend or equivalent deterministic test harness, following the repository's existing test architecture.

It must be able to provide more channels than the connected hardware supports, including an eight-channel test configuration.

Generate known signals independently per channel so tests can establish whether:
- Channel 1 reaches the correct source.
- Channel 2 reaches a different source.
- Channels do not get accidentally swapped or summed.
- All enabled channels are processed.
- Disabling one source does not disrupt the others.
- The application handles absent channels and stopped capture safely.
- Eight-channel capture simulation does not rely on a physical eight-channel interface.

The synthetic backend must be test-only or otherwise explicitly identified as synthetic. It must not be mistaken for a real hardware device in the user-facing device list.

Where possible, make integration tests use the same channel-routing and analysis components as real capture, rather than a separate mock implementation that bypasses the code under test.

### 9. Phase 6 — Performance benchmarks

Benchmark the actual AV Gen application and audio subsystem on the development Mac, including the M2 Max configuration where available.

Measure:
- Audio callback execution time and deadline misses.
- Audio xruns or equivalent capture failures.
- Audio-thread CPU usage.
- Analysis-worker CPU usage.
- Queue occupancy and buffer drops.
- Memory consumption.
- Signal-publication latency.
- Main-thread CPU time.
- GPU frame time and frame pacing.
- End-to-end time from a known input event to its availability as a modulation signal.

Test at 48 kHz, and at other sample rates if supported by the selected device and relevant to the current architecture.

Run a baseline and progressively increase the workload:
1. Existing stereo input and analysis.
2. Four channels with basic analysis.
3. Eight channels with basic analysis.
4. Eight channels with spectral analysis on every channel.
5. Eight channels with selective spectral analysis.
6. The most demanding practical combination of scene, Audio, MIDI, and output configuration available.

Use the same scene, resolution, and settings across comparable runs. Include at least one demanding live scene, not just an empty test scene.

Compare results to the pre-change baseline. Establish realistic acceptance thresholds from measurements rather than inventing a universal CPU budget before profiling.

The feature must not introduce recurring audio dropouts, unbounded buffer growth, or unexplained frame-time regressions.

If a performance regression appears, identify its source and optimize the measured bottleneck before adding compensating complexity elsewhere.

### 10. Phase 7 — UI and usability

Follow AV Gen's existing UI design and settings conventions.

For live multichannel input, provide a compact configuration surface that supports:
- Device selection.
- Visible input-channel enumeration.
- Per-channel source labels.
- Mono or linked-pair assignment.
- Basic signal meters.
- Capture status and useful errors.
- Clear indication when a configured device is disconnected.

Do not build a full digital mixer UI. AV Gen needs enough routing and monitoring functionality to configure visual analysis sources reliably.

For offline stem import, show:
- Stereo playback file.
- Imported stems and their labels.
- Alignment and format status.
- Any warnings or invalid inputs.
- Analysis preparation status.
- Access to the resulting stem signals through existing modulation workflows.

Reuse established AV Gen interaction patterns. Avoid duplicate settings, redundant controls, or a large collection of new feature toggles without demonstrated need.

### 11. Phase 8 — Failure handling and backward compatibility

Preserve the existing two-channel BlackHole workflow and other supported input paths.

When a multichannel device is unavailable:
- Keep the application usable.
- Do not crash or hang.
- Display a clear connection or capture error.
- Allow the user to select another device.
- Do not silently substitute an unrelated device without communicating the change.

When a stem is missing or invalid:
- Report which file failed and why.
- Allow the user to correct or remove it.
- Avoid stale or fabricated analysis signals.
- Preserve the stereo playback source when it remains valid.

Handle device disconnection, format changes, callback errors, and shutdown without use-after-free, deadlock, or unsafe buffer access.

Do not add backward-compatibility shims unless they are needed to preserve an actual supported workflow. Prefer the simplest clean extension of the existing architecture.

### 12. Phase 9 — Documentation and final audit

Update the relevant documentation to cover:
- Architecture and data flow.
- Supported offline input formats and alignment policy.
- Test-fixture generation.
- Signal naming and timing semantics.
- Live device selection and channel assignment.
- Volt hardware test instructions.
- Required external routing for independent musical sources.
- Performance benchmark procedures.
- Known limitations and unsupported configurations.

Run the full relevant test suite, build, static analysis, and CI checks according to repository conventions.

Inspect the final diff for dead code, duplicated analysis logic, unnecessary dependencies, and unneeded abstractions.

Report which scenes and modulation workflows were actually tested. Do not claim that every scene was verified if only a subset was exercised.

### 13. Final deliverables

The work is complete only when the following are available:

1. Research and architecture report.
2. Reviewed, milestone-based implementation plan.
3. Offline stereo-mix-plus-stems import and analysis.
4. Deterministic Python-generated audio and stem fixtures.
5. Automated offline analysis tests.
6. Live multichannel Core Audio capture and source mapping, subject to the approved plan.
7. A synthetic multichannel test harness supporting eight channels independently of physical hardware.
8. Performance benchmarks and before/after measurements.
9. Volt hardware validation results when the interface is available.
10. Updated documentation and a final implementation summary.

Clearly identify anything not completed, hardware tests that could not be performed, and unresolved issues.

### 14. Execution instructions to the agent

**Begin with research. Do not start by editing code.**

First inspect the repository and current build/test state. Then perform the technical research, trace the relevant code paths, and write the architecture report and implementation plan.

After the plan is complete, proceed through the milestones in order, validating each against its acceptance criteria.

Do not fabricate research findings, device capabilities, test results, or performance numbers. If the Volt is not connected, continue with the synthetic test backend and explicitly mark hardware validation as pending.

Keep changes focused. Reuse existing infrastructure where appropriate, measure before optimizing, and preserve the existing stereo input path throughout the work.

The ultimate goal is not simply to increase the channel count. It is to give AV Gen access to independently meaningful musical signals, with reliable timing, a maintainable architecture, and no avoidable sacrifice in live performance quality.
