# Live adaptive quality system: the brief (the owner, 2026-10-03)

*A coordinator's header; the owner's words follow it, verbatim.*
- **Where:** worktree `../av-gen-live`, branch `live/quality`, from main `b3862d23`.
- **The evidence it builds on:**
  - `docs/investigations/live-projection-2026-10-02.md` and `live-render-path-2026-10-02.md`. Both are on the local
    branch `investigate/live-render-perf` in `../av-gen-qa-coord`, and copied into this folder.
  - Raw data: `~/Desktop/av-gen-review/29-live-projection-perf/` and `26-live-render-perf/`.
- **Measurement probes:** the investigation's probe code (`AVGEN_LIVE_FRAME_CSV`, `AVGEN_X_PROJECT_ANY`,
  `AVGEN_X_PROJ_W/H`) is in commits `8f953f53` and `319946ad` on that branch, and is NOT on main. Cherry-pick it into
  this branch for measuring if useful, but it must not merge as-is.
- **Review media and the final report:** `~/Desktop/av-gen-review/30-live-quality/`.

---

# AV Gen — Implement the LIVE Adaptive Quality System

## Context

A dedicated projection-performance investigation has now been completed.

The important finding is that AV Gen's existing adaptive resolution system is fundamentally working correctly:

* The real 1920×1080 projection path was measured.
* Adaptive `renderScale` directly controls the expensive internal render target.
* Scene rendering, SDF marching, volumetrics and post-processing all operate at the reduced internal resolution.
* Tonemap/upscale to 1920×1080 costs only ~0.1 ms.
* The projection mapper/copy is only ~0.06–0.09 ms CPU.
* Heavy scenes currently reach approximately:

  * Sonic: ~55 FPS native
  * Glowmere: ~22–25 FPS native
  * Liminal: ~13–14 FPS native
* With the existing adaptive controller:

  * Sonic remains around 55 FPS because the controller's 16.67 ms budget is poorly matched to a 120 Hz display.
  * Glowmere reaches ~56 FPS at the 0.5 resolution floor.
  * Liminal reaches ~60 FPS at the 0.5 resolution floor.
* The current controller does not oscillate.
* The current controller is therefore a good foundation rather than something that needs to be replaced.

The next step is to turn this into a proper **LIVE quality/performance system**.

Do not redesign the renderer.

Do not replace the adaptive controller.

Extend the existing architecture.

---

# 1. Primary Objective

Implement a LIVE quality controller that can maintain a user-selected performance target by progressively degrading the least-visible/highest-cost rendering features.

The conceptual model is:

```text
                         GPU over budget
                              │
                              ▼
                    ┌─────────────────────┐
                    │ Reduce render scale │
                    └──────────┬──────────┘
                               │
                         still over budget?
                               │
                               ▼
                    ┌─────────────────────┐
                    │ Reduce expensive FX  │
                    └──────────┬──────────┘
                               │
                         still over budget?
                               │
                               ▼
                    ┌─────────────────────┐
                    │ Reduce march quality │
                    └──────────┬──────────┘
                               │
                         still over budget?
                               │
                               ▼
                    ┌─────────────────────┐
                    │ Emergency quality   │
                    └─────────────────────┘
```

The controller should make **small, predictable quality sacrifices before allowing catastrophic frame-time misses**.

This is for LIVE performance.

Offline rendering must remain unaffected.

---

# 2. Preserve Existing Concepts

Do not create a parallel quality system if existing structures can be extended.

Reuse:

* `InteractiveResolution`
* `QualitySettings`
* `QualityTier`
* realtime quality settings
* preview quality settings
* adaptive canvas/render scale
* existing GPU timing
* existing volume quality controls
* existing motion blur controls
* existing DoF controls
* existing shadow/cascade controls

The goal is to evolve the existing system, not replace it with another framework.

---

# 3. Target FPS Must Be Explicit

The current controller uses a hardcoded 16.67 ms budget.

That is not appropriate for every display.

Introduce a LIVE performance target concept.

At minimum support:

```text
60 FPS
90 FPS
120 FPS
```

Potentially expose a Custom target later, but don't add unnecessary UI complexity if it isn't needed.

The target should determine the GPU budget.

Use approximately 12% headroom.

For example:

```text
60 FPS
16.67 ms nominal
~14.5 ms adaptive budget

90 FPS
11.11 ms nominal
~9.7 ms adaptive budget

120 FPS
8.33 ms nominal
~7.3 ms adaptive budget
```

Do not hard-code these numbers in multiple places.

Create one clear calculation.

The controller should know:

```cpp
targetFps
targetFrameMs
qualityBudgetMs
```

and expose those values for diagnostics.

---

# 4. Do Not Couple Target FPS to Display Refresh

A 120 Hz display does not necessarily mean the user wants a 120 FPS render target.

A performer may want:

* 60 FPS on a 120 Hz display
* 90 FPS on a 120 Hz display
* 120 FPS on a 120 Hz display

The display refresh rate is environmental information.

The LIVE render target is a user preference.

Keep these concepts separate.

---

# 5. Replace "Resolution Only" With a Quality Ladder

Implement a quality state that combines existing quality controls.

Conceptually:

```text
ULTRA
HIGH
MEDIUM
LOW
EMERGENCY
```

Do not blindly use these names if the existing UI has better terminology.

The important thing is that each state represents a deterministic collection of quality settings.

Suggested initial configuration:

### ULTRA

```text
renderScale = 1.0

realtime volume resolution
realtime volume steps

motion blur ON
DoF ON
normal realtime shadows
normal realtime post
```

### HIGH

```text
renderScale = 0.85

realtime volume quality

motion blur ON
DoF ON
normal realtime shadows
```

### MEDIUM

```text
renderScale = 0.71

volumeResolutionScale = 0.25

normal/reduced volume steps

motion blur ON
DoF ON

normal shadows
```

### LOW

```text
renderScale = 0.5

volumeResolutionScale = 0.25
reduced volume steps

motion blur OFF
DoF OFF

reduced shadow cascades
```

### EMERGENCY

Start around:

```text
renderScale = 0.35–0.40

volumetrics OFF or minimal

motion blur OFF
DoF OFF

minimal shadow quality

minimal post-processing
```

These are starting points.

Where existing `QualitySettings` values don't map cleanly, inspect the implementation and use the closest existing controls rather than inventing duplicate settings.

---

# 6. IMPORTANT: Don't Assume Resolution Is Always the Best Lever

The measured scenes have different cost structures.

### Liminal

Approximately 97% of GPU time scales with resolution.

Therefore:

```text
resolution first
volume quality second
```

is appropriate.

### Glowmere

Approximately 86% of GPU time scales with resolution, but volumetrics and motion blur are significant.

A combination of:

```text
resolution
volume quality
motion blur
```

is appropriate.

### Sonic

Resolution only addresses approximately half of the GPU workload.

There is substantial fixed cost from:

* scene/vertex work
* shadows
* other fixed GPU work

Therefore blindly dropping resolution all the way to 0.5 is not necessarily the best quality/performance trade.

The architecture should allow a project to identify its preferred quality-lever ordering.

---

# 7. Add a Lightweight Per-Project Quality Hint

Do NOT build a complicated scene-analysis system yet.

Add a simple data-level hint.

Conceptually:

```text
liveQualityStrategy:
    resolution_first
    effects_first
    balanced
```

Or an equivalent structure.

For initial projects:

```text
Liminal      → resolution_first
Glowmere     → balanced
Sonic        → effects_first / balanced
```

This should influence which lever the controller tries first.

Do not create special-case code like:

```cpp
if (scene == "Glowmere") ...
```

The hint must be data-driven.

If the current scene/project format has an appropriate location for this metadata, use it.

---

# 8. Separate GPU-Budget Control From CPU-Budget Control

The existing adaptive controller correctly focuses on GPU time.

Keep it that way.

Do not make resolution dynamically respond to CPU time.

Instead:

```text
GPU over budget
    → reduce visual quality

CPU over budget
    → diagnostic / warning / separate future optimization path
```

This matters because Glowmere demonstrates that CPU work is largely resolution-independent.

At 960×540 it still has roughly 8 ms of main-thread work.

No amount of resolution scaling fixes that.

---

# 9. Fix the Two Known Glowmere CPU Problems

These are sufficiently measured that they should now be implemented.

## 9.1 Gate Diagnostic Record Generation

`RenderObjectDiagnostic` currently performs work every frame even though most of that information is only consumed by diagnostic/debug features.

Make diagnostic generation conditional on actual consumers.

Normal LIVE playback should not pay for it.

Preserve the diagnostic functionality when a consumer is active.

---

## 9.2 Cache CPU-Skinned Bounds

The previous investigation found CPU skinning of character bounds happening multiple times per frame.

Use the existing:

```text
rig.paletteVersion
```

or an equivalent reliable change token.

Cache the posed bounds.

Conceptually:

```text
palette unchanged
    → reuse bounds

palette changed
    → recompute bounds
    → update cache
```

Do not recompute CPU-skinned bounds merely because another system asks for them again during the same frame.

The goal is to remove approximately 3 ms of Glowmere's fixed CPU work.

After implementation, rerun the same Glowmere measurement to verify the actual improvement.

Do not claim a speedup until measured.

---

# 10. Make Quality Rung Changes Cheap

The current adaptive controller reallocates render targets when the resolution rung changes.

This caused a measured ~195 ms 1% low during a Sonic rung transition.

Investigate a render-target strategy that avoids expensive allocation/reallocation during LIVE playback.

Preferred architecture:

```text
allocate maximum required render targets once
        ↓
select active render rectangle / resolution
        ↓
render at selected dimensions
```

However:

**Do not implement this blindly.**

First inspect:

* texture allocation requirements
* mip requirements
* bind groups
* render-pass attachment assumptions
* post-processing dimensions
* history textures
* depth textures
* motion-vector textures
* SDF resources
* resize invalidation behavior

Determine the smallest safe change.

The requirement is:

> Changing LIVE quality must not cause a large synchronous allocation hitch.

If full-size allocation has unacceptable memory cost, find a smaller persistent allocation strategy.

---

# 11. Preserve Temporal/History Correctness

Whenever internal resolution changes, systems that depend on previous-frame data may become invalid.

Identify:

* motion blur history
* temporal effects
* previous-frame textures
* camera history
* velocity buffers
* exposure history
* any other resolution-dependent history

Make quality transitions explicit.

If a history buffer must be invalidated when resolution changes, do so intentionally.

Do not allow stale data to produce artifacts.

But also avoid rebuilding unrelated resources.

---

# 12. Add LIVE Quality Diagnostics

The performer needs to know what AV Gen is doing.

Add a compact Live quality/status display.

At minimum show:

```text
LIVE
Target: 60 FPS
Budget: 14.5 ms

GPU: 13.8 ms
CPU: 7.1 ms

Quality: HIGH
Render Scale: 0.85
```

If the UI already has a suitable location, use it.

Do not build a giant new diagnostics window.

Optional useful information:

```text
Output: 1920×1080
Internal: 1632×918
```

The critical values are:

* target FPS
* GPU ms
* budget ms
* current quality state
* render scale

---

# 13. Quality Transitions Must Have Hysteresis

Do not allow:

```text
HIGH
LOW
HIGH
LOW
HIGH
LOW
```

because GPU time fluctuates around a threshold.

Reuse the existing controller's successful characteristics:

* rolling median
* minimum frames between changes
* conservative upward promotion
* more aggressive downward degradation

Preserve the existing no-oscillation behavior.

The controller should:

### Degrade

When sustained GPU time exceeds the budget.

### Recover

Only when GPU time has comfortably fit under the budget for a sustained period.

Do not immediately recover after one good frame.

---

# 14. Don't Overreact to Short Spikes

A single expensive frame should not cause a quality change.

Use the existing rolling sample approach.

The previous implementation's median-of-20 strategy is reasonable.

Keep it unless measurements demonstrate a problem.

The goal is:

```text
sustained overload
    → quality reduction

brief spike
    → tolerate it
```

---

# 15. Do Not Add Temporal Upscaling Yet

Temporal upscaling is a future improvement.

Do not implement it in this task.

The current system already demonstrates that:

```text
1920×1080 output
+
960×540 internal render
+
normal upscale
```

can produce ~60 FPS on the tested heavy scenes.

Temporal reconstruction could make those lower resolutions look substantially better, but it is a separate rendering project.

Document it as future work.

Do not let it expand the scope of this implementation.

---

# 16. Do Not Implement Scene-Specific Shader Optimization

The measurements identify expensive passes:

### Liminal

* SDF
* volumetrics
* motion blur

### Glowmere

* scene pass
* volumetrics
* motion blur

### Sonic

* scene pass
* shadows

But this task is about the **quality control mechanism**, not optimizing those shaders.

Do not rewrite them.

Do not change their algorithms.

The quality controller should consume existing controls first.

---

# 17. Fix Projection Behavior

The investigation found an important architectural issue:

The projection output uses a second Fifo swapchain acquire on the main thread.

A projection surface can therefore potentially block the main loop.

The measured 960×540 test showed approximately 14.8 ms of projection-surface acquire wait.

Investigate and implement a safe policy so that a slow/blocked projection surface does not catastrophically stall the LIVE render loop.

Possible approaches include:

* acquire with a timeout
* skip the projection frame
* reuse the previous output frame
* decouple presentation

Choose the smallest architecture that solves the problem.

Do not redesign the entire output subsystem.

The desired behavior is:

```text
renderer healthy
projection surface slow
        ↓
LIVE renderer continues
        ↓
projection may miss/reuse a frame
```

rather than:

```text
projection surface slow
        ↓
main thread blocks
        ↓
entire AV Gen experience stalls
```

---

# 18. Fix Projection Project Selection

The investigation found that `Application::startProjection` currently swaps in the Sonic Live demo for projects that don't have `sonic.live`.

This is not appropriate for a general audiovisual engine.

Change projection startup so that it projects the currently open project/output.

Do not hard-code Sonic-specific behavior into the general projection path.

Preserve the existing Sonic functionality through normal project/output configuration rather than a special-case fallback.

Add/update tests around this behavior.

---

# 19. Test Matrix

After implementation, rerun the representative scenes.

At minimum:

### Sonic

```text
1080p
adaptive target 60
adaptive target 90
adaptive target 120
```

### Glowmere

```text
1080p
adaptive target 60
adaptive target 90
adaptive target 120
```

### Liminal

```text
1080p
adaptive target 60
adaptive target 90
adaptive target 120
```

Record:

* output resolution
* internal resolution
* quality rung
* GPU ms
* CPU ms
* FPS
* 1% low
* quality transitions
* transition hitch
* visual artifacts

Compare against the baseline measurements.

---

# 20. Success Criteria

The implementation is successful if:

### Adaptive control

* target FPS is configurable
* GPU budget derives from target FPS
* headroom is applied consistently
* controller remains stable
* no quality oscillation occurs

### Quality ladder

* resolution remains the first/primary lever where appropriate
* additional existing quality controls can engage when resolution reaches its useful floor
* heavy scenes can continue degrading after reaching 0.5 scale
* quality changes are deterministic
* LIVE only

### Performance

* Glowmere's known diagnostic/bounds CPU waste is removed or measurably reduced
* quality transitions do not produce ~200 ms allocation hitches
* projection cannot unexpectedly stall the entire render loop

### UX

The performer can see:

```text
target
budget
GPU time
current quality
render scale
```

without opening a developer-only profiler.

### Regression safety

Offline rendering remains unchanged.

Existing quality settings remain functional.

Existing projects continue to load.

---

# 21. Do Not Chase 120 FPS Yet

A key architectural point:

The purpose of this task is **not** to make every AV Gen scene hit 120 FPS.

For example, Glowmere currently has approximately 8 ms of fixed CPU work even at low resolution.

That is a separate optimization problem.

The goal here is to make LIVE performance degrade gracefully:

```text
GPU overloaded
    ↓
reduce resolution
    ↓
reduce expensive effects
    ↓
reduce expensive march quality
    ↓
preserve composition and visual identity
```

rather than:

```text
GPU overloaded
    ↓
resolution reaches 0.5
    ↓
nothing else happens
    ↓
frame rate remains bad
```

---

# 22. Implementation Philosophy

Prefer:

* small changes
* existing systems
* data-driven configuration
* measured behavior
* deterministic quality states
* explicit budgets
* cheap transitions
* LIVE-only behavior

Avoid:

* renderer rewrites
* speculative abstractions
* generalized "quality framework" architecture
* scene-specific conditionals
* duplicated settings
* premature temporal reconstruction
* shader rewrites
* broad refactoring

The system should feel like a natural evolution of `InteractiveResolution`, not a new subsystem bolted onto AV Gen.

---

# 23. Final Deliverable

Before declaring this complete, provide:

## Architecture summary

What changed and where.

## Quality-state table

Show every rung and the exact `QualitySettings` values it controls.

## Controller behavior

Explain:

* budget calculation
* downgrade threshold
* upgrade threshold
* hysteresis
* minimum time between changes
* floor/ceiling behavior

## Before/after benchmark

For Sonic, Glowmere and Liminal.

## CPU improvement

Specifically measure the diagnostic/bounds changes on Glowmere.

## Transition cost

Measure the cost of changing quality rungs during playback.

## Projection behavior

Verify that a slow projection surface cannot stall the main render loop.

## Regression testing

Run the relevant existing test suites and report failures honestly.

## Remaining limitations

Explicitly identify what remains:

* CPU-bound scenes
* temporal reconstruction
* deeper SDF quality controls
* incremental scene updates
* draw batching
* shader optimization

Do not implement those unless they are required for correctness.

---

## Most important design constraint

**AV Gen should now think of LIVE rendering as a performance-budgeted rendering mode, not simply "offline rendering with lower settings."**

The renderer already has the fundamental capability.

We are making the quality controller intelligent enough to use it.
