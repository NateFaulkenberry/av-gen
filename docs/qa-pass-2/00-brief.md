# AV Gen — Comprehensive QA, Debugging, Refactoring, Documentation & Repository Cleanup

## Mission

Perform a comprehensive engineering quality pass across the entire AV Gen repository.

This is not merely a code review, a linting exercise, or a documentation refresh. Investigate the actual state of the project, identify correctness problems and accumulated technical debt, remove obsolete material, simplify unnecessarily complicated implementations, refactor oversized files, repair broken workflows, and establish trustworthy end-to-end test coverage.

**The objective is to leave AV Gen measurably more correct, maintainable, understandable, testable, and reliable for live audiovisual performance.**

Work directly in the repository. After completing the investigation and establishing a plan, implement the improvements, run the relevant verification suites, fix regressions, and produce an evidence-based final report.

Do not stop after generating a list of recommendations.

---

# 1. Non-negotiable principles

1. **Understand before changing.** Establish the current architecture, build configuration, test infrastructure, repository state, and existing failures before modifying anything.
2. **Preserve working functionality.** Refactor internals without casually changing intended visual output, audio behavior, MIDI behavior, scene semantics, or user-facing workflows.
3. **Do not preserve obsolete behavior merely for compatibility.** AV Gen does not require unnecessary backwards-compatibility shims. Remove obsolete systems and their dependencies when they are demonstrably unused and no longer part of the intended product.
4. **Do not confuse deletion with cleanup.** Every deletion must be justified by evidence. A seemingly unused shader, asset, scene, test fixture, or resource may be loaded dynamically or referenced indirectly.
5. **Prefer simple, direct implementations.** Eliminate redundant abstractions, duplicated algorithms, unnecessary indirection, speculative generality, and framework-like machinery that has no meaningful use.
6. **No drive-by rewrites.** Refactor incrementally, with focused diffs and verification after each meaningful stage.
7. **No fake verification.** Never claim that a build, test, runtime workflow, or visual result passed unless it was actually executed and its result observed.
8. **No warning suppression as a substitute for fixing defects.** Fix root causes. Add narrow suppressions only when there is a documented, defensible reason.
9. **No gratuitous test removal.** Remove tests only when their behavior is obsolete, redundant, invalid, or superseded by stronger coverage. Replace deleted coverage when the underlying requirement remains valid.
10. **Maintain a clean repository.** Preserve unrelated user changes, avoid destructive Git operations, and never commit secrets, generated junk, local machine state, or unreviewed artifacts.

Use up to two parallel subagents for independent research or bounded audits if the available agent environment supports them. Assign explicit file or subsystem ownership, prevent overlapping modifications, and integrate their findings centrally. Do not parallelize tightly coupled implementation work simply to increase activity.

# 2. Phase 0 — Establish a trustworthy baseline

Before editing files:

## 2.1 Inspect repository and environment

Record:

- Current branch, commit, and working-tree status.
- Existing uncommitted changes, untracked files, and local modifications.
- Repository layout and major subsystems.
- Build system, compiler, language standard, dependency manager, and dependency versions.
- Available build presets, targets, test suites, CI workflows, and development scripts.
- Supported platforms and configurations.
- Existing documentation conventions and architecture decision records.
- Existing profiling, debugging, diagnostics, and test tools.
- Available audio hardware, MIDI devices, GPU, display, and runtime capabilities.

Identify whether the environment can perform actual GPU-backed rendering, audio-device integration, MIDI integration, and interactive runtime testing. Distinguish unavailable capabilities from actual failures.

Do not reset, stash, overwrite, or otherwise destroy existing user work.

## 2.2 Establish baseline build and test results

Run the project's canonical build and test commands.

At minimum, investigate:

- Clean or isolated build reproducibility.
- Debug and optimized build configurations where supported.
- Existing unit and integration tests.
- Static analysis and formatting checks already configured.
- Shader compilation and shader validation.
- Asset and scene validation.
- CI checks and platform-specific build targets.
- Application startup and basic runtime operation where possible.

Capture actual failures, warnings, flaky tests, skipped tests, and environmental limitations.

Classify every existing failure as one of:

- Confirmed pre-existing defect.
- Newly introduced regression.
- Environmental limitation.
- Unresolved or inconclusive.

Do not assume a failure is pre-existing without evidence.

Create a concise baseline report before starting remediation.

# 3. Phase 1 — Map the real architecture

Trace the real implementation rather than relying on existing documentation.

Produce a temporary subsystem map covering:

- Application startup and shutdown.
- Main loop and frame scheduling.
- Audio capture/playback and audio-analysis processing.
- Audio buses, signal sources, timelines, modulators, and parameter propagation.
- MIDI device discovery, input, learn mode, mappings, and persistence.
- Scene loading, scene lifecycle, generators, entities, effects, and scene-specific behavior.
- Asset loading, caching, ownership, and lifetime.
- GPU resource creation, binding, synchronization, and destruction.
- Rendering passes, intermediate buffers, post-processing, and presentation.
- Configuration, project state, serialization, and error handling.
- UI panels, diagnostics, logging, profiling, and developer tools.
- Build tooling, CI, automated tests, fixtures, and content validation.

For each subsystem, identify:

1. Its actual entry points.
2. Its public interfaces and ownership boundaries.
3. Its dependencies and downstream consumers.
4. Its state-management and threading assumptions.
5. Its failure modes.
6. Its existing tests.
7. Any discrepancies between implementation, tests, and documentation.

Pay particular attention to code that executes indirectly through registries, factories, reflection-like mechanisms, string identifiers, resource manifests, generated bindings, and scene configuration. Ordinary text search is not sufficient evidence that such code is unused.

Update the permanent architecture documentation only after the architecture has been verified.

# 4. Phase 2 — Comprehensive correctness and defect audit

Inspect all production code, not just recently changed files.

## 4.1 C++ correctness

Investigate:

- Undefined behavior and invalid assumptions.
- Out-of-bounds indexing and invalid iterator use.
- Use-after-free, dangling references, and dangling captures.
- Ownership ambiguities and resource lifetime errors.
- Incorrect move/copy behavior.
- Uninitialized state and invalid default values.
- Integer overflow, narrowing, and unsafe conversions.
- Invalid floating-point assumptions, NaNs, infinities, and division by zero.
- Incorrect container invalidation assumptions.
- Exception-safety and error propagation.
- Inconsistent `const` correctness and unnecessary copying.
- Thread safety, data races, deadlocks, and lock-order problems.
- Destruction-order and shutdown races.
- Incorrect error handling that silently produces invalid state.
- Excessive allocations in latency-sensitive paths.

Use sanitizers and static analysis when supported by the project's toolchain.

Do not perform superficial modernization solely to use newer C++ features. Every change should improve correctness, clarity, performance, or maintainability.

## 4.2 Audio and real-time processing

Treat the audio callback and other latency-sensitive processing paths as high priority.

Inspect:

- Allocations, deallocations, blocking operations, locks, and I/O in real-time callbacks.
- Buffer sizes, sample rates, channel counts, and channel-layout assumptions.
- Incorrect handling of partial buffers and discontinuities.
- Audio-analysis window boundaries and state reset behavior.
- FFT, onset, envelope, RMS, spectral, and other feature calculations.
- Smoothing and temporal-state correctness.
- Thread handoff and synchronization between audio and rendering.
- Audio-device disconnects, restarts, and device reconfiguration.
- Numerical stability and behavior during silence or near-silence.
- Stale signal values and inconsistent update timing.
- Parameter propagation delays and unintended frame latency.
- Audio-thread versus render-thread ownership of mutable state.
- Behavior with stereo mixes, multichannel inputs, and separated stems.

Verify that signals used by a frame are actually available when that frame consumes them. Investigate one-frame delays and ordering bugs rather than concealing them with additional buffering.

Do not reintroduce obsolete kick/snare/hi-hat inference or preserve abandoned signal semantics solely for compatibility. Validate audio features against the current intended signal model.

Add focused tests for buffer boundaries, silence, transient signals, deterministic synthetic inputs, timing, and state resets.

## 4.3 MIDI and controller input

Inspect:

- Device enumeration and hot-plugging.
- Connection and disconnection handling.
- Message parsing and channel filtering.
- Note, velocity, CC, pitch bend, aftertouch, and supported message types.
- Learn-mode capture and assignment.
- Mapping conflicts, scaling, ranges, and smoothing.
- Assignment persistence and restoration.
- Per-scene and per-project mapping semantics.
- Default scene mappings and user overrides.
- Input handling when a device disconnects during performance.
- UI feedback and MIDI monitoring.

Exercise the code using synthetic MIDI messages even when physical devices are unavailable.

Do not claim physical hardware coverage unless it was actually tested.

## 4.4 Rendering and GPU correctness

Audit:

- Render-pass dependencies and execution order.
- Resource formats, dimensions, usage flags, and binding compatibility.
- Texture and buffer lifetime.
- Depth, normals, motion information, and intermediate render targets.
- Resize and resolution-change handling.
- GPU synchronization and CPU/GPU ownership assumptions.
- Shader compilation, binding layouts, and uniform alignment.
- Out-of-bounds accesses and invalid dispatch dimensions.
- Render-target initialization and stale contents.
- Invalid material parameters and non-finite transforms.
- Resource leaks during scene changes and application shutdown.
- Error paths during device initialization and resource creation.
- Device-loss or GPU failure behavior where supported.
- Color-space, tone-mapping, alpha, and HDR/LDR conversion correctness.

Inspect all rendering passes and their consumers. Verify that intermediate outputs have the semantics their consumers expect.

Test scene transitions, repeated reloads, window resizing, and shutdown with graphics validation enabled where practical.

Do not rewrite the renderer wholesale. Prioritize reproducible defects and demonstrable improvements.

## 4.5 Scene and asset correctness

Build an inventory of all shipped scenes, generators, shaders, materials, assets, and resource manifests.

Check for:

- Broken references and missing files.
- Duplicate assets and duplicate implementations.
- Unused assets and unreachable scene code.
- Invalid parameters, IDs, ranges, and defaults.
- Missing registrations.
- Incorrect resource lifetime.
- Nondeterministic initialization.
- Invalid scene transitions.
- Incorrect audio/MIDI bindings.
- Asset-loading failures hidden behind silent fallbacks.
- Incorrect cleanup when a scene is unloaded.
- Excessive CPU or GPU work caused by avoidable duplication.

Create automated validation for scene definitions and asset references wherever practical.

Do not remove content merely because it is not used by the default startup scene. Establish whether it is intentionally shipped, user-selectable, or referenced indirectly.

## 4.6 UI, state, and persistence

Inspect:

- Invalid or stale references after scene changes.
- Incorrect UI update timing.
- Missing error feedback.
- Incorrect state synchronization.
- Invalid input handling.
- Configuration defaults and migration behavior where still relevant.
- Serialization and deserialization.
- Missing-field and malformed-file behavior.
- Resource and project state restoration.
- Device or scene changes while UI controls are active.
- Inconsistent naming and misleading labels.

Ensure saved settings and project data round-trip correctly. Remove compatibility logic only when the corresponding format or behavior is genuinely obsolete.

## 4.7 Performance and stability

Profile representative workloads rather than optimizing by intuition.

Prioritize:

1. Deadline misses and stuttering during live operation.
2. Audio-thread safety and audio glitches.
3. CPU/GPU bottlenecks.
4. Excessive allocations and repeated work.
5. Memory growth during long sessions.
6. Scene-switching and shader-compilation stalls.
7. Unnecessary per-frame calculations.
8. Expensive diagnostic work accidentally enabled in production.

Use the actual project target and available reference hardware as the performance baseline. Do not invent performance measurements.

Where profiling is possible, record baseline and post-fix results using the same scene, resolution, settings, and workload.

Do not trade visual quality for a benchmark improvement without explicitly validating the resulting behavior.

# 5. Phase 3 — Remove dead code and obsolete repository material

Perform a repository-wide reachability and relevance audit.

## 5.1 Dead and redundant production code

Identify:

- Unreachable functions and classes.
- Unused fields and parameters.
- Duplicate helpers and repeated algorithms.
- Abandoned implementations.
- Obsolete feature flags.
- Unused compatibility wrappers.
- Redundant configuration branches.
- Dead code paths and impossible conditions.
- Unused dependencies and libraries.
- Obsolete abstractions with only one trivial consumer.
- Duplicate error-handling and validation logic.

For each removal, establish that no production, build, test, tooling, scripting, or dynamic registration path depends on it.

Prefer removing the obsolete abstraction rather than adding another abstraction to manage it.

## 5.2 Tests and test infrastructure

Inventory all tests and classify them as:

- Valuable behavioral tests.
- Valuable regression tests.
- Integration or end-to-end tests.
- Duplicated assertions.
- Tests of obsolete behavior.
- Tests that assert implementation details rather than contracts.
- Flaky tests.
- Dead fixtures and obsolete mocks.
- Unmaintained or unreachable test targets.

Delete obsolete tests only when their requirements are no longer valid or their coverage is genuinely redundant.

When removing a test, explain what it covered and why the remaining test suite provides sufficient coverage or why the behavior is no longer required.

Never remove a failing test simply to make CI green.

Never replace meaningful assertions with weaker smoke tests.

## 5.3 Documentation and artifacts

Inspect every documentation file, including:

- README files.
- Architecture and subsystem guides.
- Build and setup instructions.
- API and developer documentation.
- Feature guides.
- Troubleshooting guides.
- Testing instructions.
- Performance reports.
- Architecture decision records.
- Plans and implementation specifications.
- Deprecated design documents.
- Generated documentation.
- Root-level Markdown files.
- CI and release documentation.

Classify each document as:

1. Current and accurate.
2. Current but incomplete.
3. Stale and requiring revision.
4. Historical and worth retaining.
5. Obsolete and safe to remove.
6. Duplicate of another maintained source.

For each document, verify its claims against actual code, configuration, tests, and workflows.

Specifically identify instructions referencing deleted files, nonexistent commands, retired architecture, abandoned features, obsolete dependencies, old branch names, incorrect performance figures, or unsupported platform claims.

Update documentation to reflect the implementation that exists, not the architecture the project was once intended to have.

Preserve useful historical records and rationale. Do not rewrite old ADRs to pretend past decisions never happened. Add a superseding decision when appropriate.

Remove obsolete plans and duplicated guides when they have no historical or operational value.

Avoid creating additional documentation merely to describe the cleanup itself.

## 5.4 Generated files and repository hygiene

Audit:

- Accidentally tracked build outputs.
- Generated files that should be reproducible.
- Duplicate generated artifacts.
- Temporary debugging output.
- Stale profiling captures.
- Crash dumps and logs.
- Orphaned assets.
- Unused test data.
- Incorrect ignore rules.
- Build products accidentally committed to source directories.

Retain required checked-in generated files when the project intentionally depends on them. Confirm that regeneration works before removing any generated artifact.

# 6. Phase 4 — Refactor oversized and overly complex files

Find large source files, headers, classes, and functions across the repository.

Do not use a single line-count threshold as the sole criterion. Consider responsibilities, dependency fan-out, cognitive complexity, compile-time cost, and testability.

Prioritize files that combine multiple independent responsibilities, have large conditional blocks, duplicate logic, or require extensive context to understand.

## 6.1 Refactoring approach

For each candidate:

1. Explain the existing responsibilities.
2. Identify natural ownership boundaries.
3. Map public interfaces and dependencies.
4. Identify circular dependencies and hidden shared state.
5. Define the intended smaller components.
6. Refactor incrementally.
7. Add or preserve focused tests.
8. Build and run relevant tests after each extraction.
9. Verify that runtime behavior remains equivalent where expected.

Prefer cohesive units organized around actual responsibilities.

Potential boundaries include:

- Parsing and validation.
- Resource loading and lifetime.
- State management.
- Domain logic.
- GPU resource management.
- Render-pass implementations.
- Scene-specific behavior.
- Serialization.
- UI presentation and input handling.
- Diagnostics and profiling.
- Test fixtures and reusable test utilities.

These are examples, not a mandate to create every possible layer.

## 6.2 Constraints

- Do not split files merely to achieve a preferred line count.
- Do not replace one large file with many tiny files.
- Do not create generic managers, factories, registries, or interfaces without a demonstrated need.
- Do not expose internal implementation details through public headers unnecessarily.
- Avoid circular includes and excessive transitive dependencies.
- Keep templates and inline definitions where they are needed.
- Preserve clear ownership and lifetimes.
- Minimize compile-time coupling.
- Avoid gratuitous renaming that obscures Git history.
- Do not perform massive formatting-only changes alongside functional refactors.

Prefer private implementation files and narrow interfaces where appropriate.

Produce a prioritized list of the largest and most problematic files, documenting which were refactored and which were intentionally left intact.

# 7. Phase 5 — Establish a meaningful testing pyramid

The existing suite must be evaluated for coverage quality, not just test count.

## 7.1 Unit tests

Add or strengthen tests for deterministic, isolated logic:

- Signal calculations and parameter transformations.
- Boundary conditions and numerical stability.
- Serialization and deserialization.
- Input mapping and MIDI message interpretation.
- Scene configuration validation.
- Resource identifiers and lookup.
- State transitions.
- Audio-analysis algorithms.
- Utility functions and error handling.

Test normal cases, boundary cases, malformed input, empty state, and failure behavior.

Avoid testing trivial getters and setters unless they enforce a meaningful contract.

## 7.2 Integration tests

Test actual interactions between subsystems:

- Audio analysis to signal sources.
- Signal sources through modulators to parameters.
- Parameter updates into generators and scene state.
- MIDI input through assignment and learn workflows.
- Scene loading through asset resolution.
- Scene switching and resource cleanup.
- Configuration save and restore.
- Shader compilation and render-pipeline initialization.
- Render-target creation, resizing, and recreation.
- Application startup and orderly shutdown.

Use the actual production components where practical. Avoid mocks that duplicate the very logic the test is intended to validate.

## 7.3 End-to-end application tests

Build a repeatable automated workflow that exercises the application from startup through shutdown.

At minimum, implement the feasible portions of this sequence:

1. Launch the application using a controlled test configuration.
2. Initialize the required runtime systems.
3. Load a known-good scene and its assets.
4. Supply deterministic synthetic audio or a known test recording.
5. Verify expected analysis signals and parameter propagation.
6. Exercise MIDI input using synthetic messages.
7. Run the scene for a controlled duration.
8. Verify that frames are rendered and no fatal errors occur.
9. Capture diagnostic information and a rendered output where possible.
10. Switch to another scene and verify resource cleanup.
11. Repeat scene loading and unloading to expose lifecycle bugs.
12. Exercise resize or supported configuration changes.
13. Shut down cleanly.
14. Inspect logs, assertions, sanitizers, and resource diagnostics for failures.

Use deterministic fixtures and reproducible seeds where applicable.

If an actual display or GPU is required, use an appropriate headless backend or CI-capable environment when supported. Do not assume that a mocked renderer validates real rendering.

## 7.4 Visual regression and rendered-output validation

Where the infrastructure permits, create a small representative scene-validation suite.

Validate:

- Successful scene initialization.
- Non-empty rendered output.
- Expected output dimensions and pixel format.
- Absence of completely black, entirely invalid, or non-finite output when not expected.
- Major rendering-pass availability.
- Stable output for deterministic fixtures.
- No obvious regressions in representative scenes.

Use image comparisons with documented tolerances rather than brittle exact pixel equality when GPU differences are expected.

Do not treat a successful screenshot capture as proof that visual quality is correct. Record which scenes have been inspected by a human and which have only automated checks.

## 7.5 Stress, soak, and lifecycle testing

Create repeatable tests for:

- Repeated scene transitions.
- Repeated resource creation and destruction.
- Long-running playback.
- Bursty audio transients and silence.
- MIDI device disconnect/reconnect simulations.
- Rapid parameter changes.
- Repeated resizing.
- Invalid or missing assets.
- Malformed configuration files.
- Application shutdown during active work where safely testable.

Track memory use, errors, and timing throughout the run.

Use a practical duration for routine CI and a longer optional soak run for release validation.

## 7.6 Test suite quality

Ensure:

- Tests are registered and actually executed by the canonical test command.
- CI cannot silently skip the main test suite.
- Failures return a nonzero exit status.
- Tests clean up temporary files and runtime resources.
- Test fixtures are deterministic.
- Parallel test execution does not introduce shared-state races.
- Slow tests are separated appropriately from fast unit tests.
- Test output identifies the failing subsystem and relevant context.

Report test coverage using meaningful measures where available. Do not optimize for a coverage percentage at the expense of assertion quality.

# 8. Phase 6 — Improve developer experience and continuous integration

Inspect all supported developer workflows.

Verify that the documented commands for configuring, building, testing, formatting, validating shaders, and running the application work from a clean checkout.

Where appropriate:

- Consolidate duplicate scripts.
- Remove unused scripts.
- Make error messages actionable.
- Ensure scripts propagate failures.
- Validate required tools and dependencies early.
- Make generated artifacts reproducible.
- Avoid machine-specific absolute paths.
- Improve incremental-build behavior.
- Ensure CI executes relevant validation rather than merely compiling one target.
- Add missing validation to existing CI without creating a redundant parallel CI framework.
- Document platform-specific limitations and prerequisites.

Preserve the project's supported toolchain. Do not introduce a new package manager, test framework, or build system without a compelling reason.

# 9. Phase 7 — Prioritized remediation

After the audit, build an actionable plan ranked by severity and impact.

Use these categories:

**P0 — Critical**
- Crashes, memory corruption, undefined behavior.
- Audio-thread hazards that can cause glitches or deadlocks.
- Data loss or destructive persistence bugs.
- Security vulnerabilities.
- Severe rendering or resource-lifetime failures.

**P1 — High**
- Incorrect audiovisual behavior.
- Broken core workflows.
- Significant frame-time or memory regressions.
- Incorrect signal timing or MIDI behavior.
- Missing critical test coverage.
- Unreliable builds or release-blocking failures.

**P2 — Medium**
- Duplicated or unnecessarily complex implementations.
- Oversized files with clear separation boundaries.
- Stale operational documentation.
- Redundant dependencies and unused code.
- Missing integration tests.

**P3 — Low**
- Minor naming inconsistencies.
- Cosmetic organization issues.
- Nonessential documentation improvements.
- Small readability improvements without material risk.

Prioritize actual impact over the volume of changes.

Implement P0 and P1 items first. Then complete the highest-value P2 cleanup. Do not spend the entire task renaming files while correctness problems remain.

For each item, record its evidence, intended change, affected areas, risk, and verification method.

# 10. Phase 8 — Execution discipline

Work in bounded stages:

1. Baseline and architecture audit.
2. Critical correctness fixes.
3. Test infrastructure and regression coverage.
4. Dead code and obsolete artifact removal.
5. Large-file refactoring.
6. Documentation refresh.
7. CI and developer-workflow improvements.
8. Full verification and final review.

The order may be adjusted when dependencies require it, but explain meaningful deviations.

After each stage:

- Inspect the diff.
- Build affected targets.
- Run the relevant tests.
- Investigate regressions immediately.
- Confirm no unrelated changes were introduced.
- Update the remediation checklist.

Use small, logically coherent commits if explicitly authorized by the surrounding workflow. Otherwise, leave changes uncommitted and report their state.

Do not hide failing tests, weaken assertions, skip validation, or classify unverified behavior as complete.

If the available environment prevents a required test, implement the necessary automation where possible and clearly identify the remaining execution requirement.

# 11. Phase 9 — Final verification and independent review

Before declaring completion:

## Build and test verification

- Re-run all canonical builds and tests.
- Run the relevant sanitizer and static-analysis checks.
- Re-run shader and asset validation.
- Run integration and end-to-end workflows.
- Verify clean startup and shutdown.
- Check the final repository status.
- Inspect the entire diff for accidental changes.
- Search for stale references to removed code, commands, files, and features.
- Verify that all newly added tests are registered.
- Confirm that documentation commands match the actual build system.
- Confirm that generated files and artifacts are handled correctly.

Run a final review focused specifically on regressions introduced by the cleanup.

## Independent review

If a second agent is available, assign it a read-only review of the final diff and test evidence. Ask it to look for:

- Incorrect deletions.
- Lost behavior or missing coverage.
- Overengineering introduced during refactoring.
- Hidden lifecycle and concurrency bugs.
- Stale documentation.
- Tests that pass without exercising real behavior.
- Incomplete failure handling.
- Changes that make live performance less reliable.

Resolve substantive findings and rerun affected tests.

# 12. Required deliverables

Produce a concise but evidence-based final report containing:

## A. Executive summary

What materially improved, what major defects were fixed, and what remains unresolved.

## B. Baseline versus final verification

Include actual results for:

- Build targets.
- Tests passed, failed, skipped, or unavailable.
- Sanitizer/static-analysis checks.
- Shader and asset validation.
- Integration and end-to-end workflows.
- Runtime stability and performance measurements where available.

Distinguish verified facts from estimates and unverified assumptions.

## C. Defects fixed

For each significant fix, include the root cause, affected subsystem, and regression test.

## D. Dead code and artifact cleanup

List major removed code, dependencies, obsolete tests, assets, and documentation, with the reason each was safe to remove.

## E. Refactoring report

List the major files refactored, their new responsibilities, and how the refactoring improved cohesion or testability.

Include any high-priority large files intentionally left unchanged and explain why.

## F. Documentation audit

List documents updated, removed, or retained as historical material. Identify remaining documentation gaps.

## G. End-to-end testing

Describe the actual workflows exercised, what was automated, which hardware-dependent paths were tested, and what remains unverified.

## H. Remaining issues

List unresolved defects, environmental limitations, deferred work, and the exact steps needed to finish verification.

## I. Final repository status

Report the final branch, whether the working tree is clean, and whether changes were committed.

# Definition of done

The task is complete only when:

- All identified critical defects have been fixed or explicitly documented with a defensible blocker.
- The canonical build and test workflow has been executed.
- Meaningful end-to-end coverage exists for the core application workflows to the extent the environment permits.
- Tests are not silently skipped or weakened to manufacture a passing result.
- Dead code, obsolete documentation, and redundant artifacts have been removed where their irrelevance was established.
- Large files with clear, independent responsibilities have been refactored where doing so is safe and beneficial.
- Documentation accurately reflects the current implementation.
- The final diff has been reviewed for regressions.
- Every claimed result is supported by actual evidence.
- Remaining limitations are explicit and actionable.

**Begin with repository inspection and baseline verification. Produce the findings and implementation plan, then carry the work through to verified completion. Do not stop at the audit report.**
