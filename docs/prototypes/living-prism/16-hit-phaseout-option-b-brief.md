# Owner's brief: finish the hit-detector phase-out (option B) and prepare the stem-analysis milestone (verbatim, 2026-10-23)

Coordinator notes: the stems are at `assets/audio/stems/feline-footwear (Drums|Bass|Synth).wav` (not `audio/feline-footwear-stems`); the mix is `assets/audio/feline-footwear.wav`. Branch/merge convention here: commit to the feature branch, never push to main or force-push; the coordinator merges after the owner's approval.

---

# AV Gen — Finish Hit-Detector Phase-Out and Establish the Next Audio Milestone

## Objective

Complete the current kick/snare/hi-hat phase-out using the revised recommendation, preserve existing scene behavior wherever possible, and prepare AV Gen for the next phase of stem-aware audio analysis.

This is a targeted implementation task, not authorization for a broad audio architecture rewrite, scene regeneration, or unrelated cleanup.

## 1. Finalize the hit-detector migration

The current implementation is built and has passed its existing checks, but it has not been merged pending review.

Use **Option B: replace only the mix-based kick detector. Preserve the existing mix-based snare and hi-hat detectors.**

The measured results on the current evaluation material are:

| Signal | Old mix detector accuracy | Proposed replacement accuracy | Decision |
|---|---:|---:|---|
| Kick | 54% | 67% | Use the beat-clock stand-in, gated by actual drum activity |
| Snare | 93% | 70% | Keep the existing mix detector |
| Hi-hat | 85% | 45% | Keep the existing mix detector |

With a drum stem available, retain the existing stem-specific kick, snare, and hi-hat behavior.

### Required work

1. Inspect the current branch, diff, tests, and implementation before changing anything.
2. Review the six existing side-by-side clips in `~/Desktop/av-gen-review/85-hit-phaseout/` if accessible. These compare old and new behavior over Feline Footwear, using the mix alone.
3. Modify the migration so that only the mix-based kick detector is replaced.
4. Preserve the existing mix-based snare and hi-hat detector implementations, thresholds, and behavior unless a change is strictly required to complete this scope.
5. Verify that the kick stand-in is gated by actual drum activity and cannot produce kick events solely because the beat clock advances through a breakdown or non-drum passage.
6. Preserve the stem-based behavior and existing timing semantics.
7. Inspect all 41 migrated live scenes for unintended signal-binding changes, including any references that should continue using the original snare and hi-hat signals.
8. Do not introduce backward-compatibility layers or duplicate signal paths unless a concrete dependency requires them.

Do not assume that passing tests proves the visual result is correct. Where practical, render comparable before/after clips and inspect timing, repetition, and visual response.

## 2. Glowmere Valley 3 — narrowly scoped repair

Glowmere Valley 3 has four small effects whose current signal bindings are affected by this migration:

- Motes
- Spores
- Fireflies
- Water sparkle

Rebind only these four effects to the existing general onset signal so they retain audio responsiveness.

### Hard constraints

- Do not rerun the Glowmere Valley 3 scene generator or its broader pipeline.
- Do not regenerate assets.
- Do not modify the scene layout, art direction, lighting, materials, or set pieces.
- Do not disturb the authored set-piece cues.
- Do not make unrelated changes to Glowmere Valley 3.
- Do not touch other Glowmere scenes.

Inspect the actual signal bindings before editing. Make the smallest possible change and verify that all four effects still respond correctly.

If the general onset signal causes obviously excessive triggering, document the observed issue and propose a targeted adjustment rather than silently expanding the scope.

## 3. Regression testing and merge readiness

Run the relevant checks after making the changes.

At minimum, verify:

- GPU suite: previously 687 passed, 1 skipped.
- CPU suite.
- Example review: previously all 111 examples passed.
- Project integrity checks.
- Relevant audio-analysis and signal-bus tests.
- All affected scene configurations and signal references.
- Stem-present and stem-absent behavior.
- No accidental changes to Sonic Garden or Sonic Live, which did not use the mix detectors.
- No unrelated scene, asset, generated-file, or configuration changes.

Review the final diff for accidental scope expansion.

Do not claim a check passed unless you actually ran it and observed the result. If an environment limitation prevents a check, report that explicitly.

### Merge policy

Prepare the revised change for review. Follow the repository's existing branch and merge conventions. Do not overwrite unrelated work, discard user changes, or force-push. If the current branch contains other changes, isolate the intended changes safely before proceeding.

Report the final commit and merge status accurately. If repository policy or the current workflow requires explicit approval before merging, stop at that boundary.

## 4. Prepare the next milestone: validate stem-aware audio analysis

After the migration is stable, investigate the existing stem-analysis implementation and prepare a concrete next-step plan. Do not begin a broad rewrite as part of this task.

The available test material is in:

`audio/feline-footwear-stems`

It contains synchronized stereo WAV stems for:

- Drums
- Bass
- Synth

The full mix remains the playback source. The stems provide aligned analysis inputs; do not replace stereo playback with stem playback.

### Evaluate the following

1. Verify stem alignment, duration, sample rate, channel layout, and synchronization with the mix.
2. Determine which existing signal-bus outputs should be driven by each stem.
3. Verify that drum events come from the drum stem, bass activity from the bass stem, and synth activity from the synth stem where appropriate.
4. Compare the resulting event timing and signal behavior with mix-only analysis.
5. Check that disabling or removing stems returns the system to sensible mix-only fallbacks.
6. Identify any latency, buffering, synchronization, or live-performance issues that could affect real-time operation.
7. Evaluate whether existing resonator and experimental modulation systems can use these signals productively without requiring scene-specific hacks.

Keep measured instrument identification separate from broad musical activity. A general onset, beat position, or frequency-band envelope must not be represented as a confidently identified kick, snare, or hi-hat.

Do not add machine-learning dependencies, source separation, new third-party libraries, or a replacement analysis framework without demonstrating a concrete need.

### Deliverable for this phase

Produce a short technical report containing:

- Existing architecture and relevant code paths.
- Current stem support and any gaps.
- Test results using Feline Footwear.
- Before/after comparisons where useful.
- Specific recommended changes, ordered by impact and implementation cost.
- A separate proposal for validating live multichannel inputs using the actual supported audio hardware.

Do not implement this larger follow-up phase until the migration is stable and the findings have been reviewed.

## 5. Final response format

Report:

1. What changed.
2. Why Option B was implemented.
3. How the four Glowmere effects were repaired without regenerating the scene.
4. Exact tests run and their results.
5. Any visual regressions or unresolved risks.
6. Final branch, commit, and merge status.
7. Recommended next steps for stem-aware and live-input validation.

Be concise but technically specific. Distinguish verified results from assumptions and recommendations.

**Priority order:** preserve existing work → make the minimal detector change → protect Glowmere → verify behavior → prepare the stem-analysis milestone.

Do not expand the scope merely because adjacent improvements are possible.
