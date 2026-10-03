# Live scene optimization and scalability: the brief (the owner, 2026-10-03)

*A coordinator's header; the owner's words follow it, verbatim.*
- **Where:** worktree `../av-gen-opt`, branch `live/optimizer`, based on `live/quality` (`5fcd7ad4`). That branch,
  the live adaptive quality system (ADRs 1080-1089), is not merged yet. It already delivers much of Phase 4 and part
  of Phases 2-3, so this work builds on it.
- **Read next:** `01-research.md` (what exists, what is partial, what is missing) and `02-plan.md` (the
  implementation plan, staged).
- **Evidence:** `../av-gen-live/docs/live-quality/` (the reports, and the live-quality brief).

---

# AV Gen — Live Scene Optimization & Scalability System

## Status

Proposed architecture and implementation roadmap.

## Objective

Build a five-phase live-performance optimization system for AV Gen that allows increasingly sophisticated real-time scenes to run reliably on commodity touring hardware while preserving the visual character and reactive nature of the scene.

The intended primary use case is **not** stadium-scale media-server deployment.

The primary target is:

> A musician or small creative team can bring a laptop and projector/TV/LED display on tour and run sophisticated audio/MIDI-reactive 3D visuals interactively at a predictable frame rate.

The system should therefore optimize for:

* predictable frame time
* graceful degradation
* visual quality preservation
* live interactivity
* audio/MIDI responsiveness
* minimal setup
* commodity hardware
* scenes that remain editable and reactive
* eventual optional scaling to multiple GPUs/render nodes

The architecture should not prematurely turn AV Gen into a large distributed media-server product.

---

# Core Philosophy

AV Gen should distinguish four related but different concepts:

### 1. Profiling

**What is expensive?**

Measure CPU/GPU/frame-time behavior accurately.

### 2. Optimization

**What can we change to make this particular scene cheaper?**

Analyze the scene and identify targeted opportunities.

### 3. Scalability

**How can the same scene operate at different performance levels?**

Expose controlled quality dimensions such as resolution, LOD, shadows, particles, volumetrics, post FX, etc.

### 4. Runtime adaptation

**What should happen when actual performance changes during a live performance?**

Automatically reduce expensive features while protecting the frame-rate target.

These should not be collapsed into one giant system.

---

# Performance Model

AV Gen should treat frame time as the primary live-performance currency.

For target frame rate:

| Target  | Frame budget |
| ------- | -----------: |
| 120 FPS |      8.33 ms |
| 60 FPS  |     16.67 ms |
| 30 FPS  |     33.33 ms |

The system should maintain a distinction between:

* CPU frame time
* GPU frame time
* synchronization/wait time
* presentation/output time
* total frame time

The renderer must avoid reporting a scene as "60 FPS capable" simply because one subsystem is fast while another is the actual bottleneck.

The system should identify the **critical path**.

A useful conceptual model:

```text
CPU simulation
     │
     ├── audio analysis
     ├── MIDI
     ├── animation
     ├── scene update
     ├── particle simulation
     └── culling
             │
             ▼
        GPU submission
             │
             ├── geometry
             ├── shadows
             ├── materials
             ├── particles
             ├── volumetrics
             ├── post FX
             └── compositing
             │
             ▼
          Present
```

Do not assume GPU time is the only relevant metric.

---

# PHASE 1 — External Live Scene Optimizer

## Goal

Create an external developer/agent-facing tool analogous to the existing Cinematic Critic.

Its first responsibility is **measurement and diagnosis**, not autonomous scene modification.

The tool should answer:

> "Can this scene run at the requested live target, and what is consuming the budget?"

---

## 1.1 Interface

Create a tool/API that can be invoked against an AV Gen scene.

Conceptually:

```text
live_scene_profile
```

Example invocation:

```text
live_scene_profile(
    scene="Glowmere-3.scene.json",
    target_fps=60,
    resolution="1920x1080",
    duration_seconds=10
)
```

Optional parameters:

```text
quality_profile
warmup_seconds
camera
audio_enabled
midi_enabled
capture_render
verbose
```

The tool should support both:

### Fast mode

Short benchmark intended for agents during iterative scene construction.

Example:

```text
2–5 second warmup
5 second measurement
minimal diagnostics
```

### Deep mode

More expensive diagnostic benchmark.

Example:

```text
10–30 second measurement
multiple cameras if requested
GPU pass breakdown
CPU subsystem breakdown
resource statistics
frame-time distribution
worst-frame analysis
```

---

# 1.2 Benchmark Conditions

The optimizer must establish reproducible conditions.

Record:

* machine
* CPU
* GPU
* OS
* renderer backend
* build configuration
* resolution
* refresh rate
* target FPS
* scene
* camera
* quality profile
* window/fullscreen mode
* audio state
* MIDI state
* scene seed if applicable

Example:

```text
Machine:
Apple M2 Max

Renderer:
WebGPU / Dawn / Metal

Resolution:
1920 × 1080

Target:
60 FPS

Budget:
16.67 ms

Measurement:
10 seconds
```

The report must make clear that benchmark results are hardware-specific.

---

# 1.3 Warmup

Do not measure immediately after loading.

The benchmark should:

1. load scene
2. initialize GPU resources
3. compile/warm shaders
4. upload assets
5. initialize simulations
6. run warmup frames
7. begin measurement only after the scene reaches steady state

This prevents shader compilation and resource initialization from polluting the live frame measurement.

Also separately report startup/prewarm cost.

---

# 1.4 Frame-Time Statistics

Do not report only average FPS.

Record:

* mean frame time
* median
* P90
* P95
* P99
* maximum
* dropped/deadline-missed frames
* percentage under budget

Example:

```text
Target: 60 FPS
Budget: 16.67 ms

Mean:       14.8 ms
Median:     14.2 ms
P95:        16.1 ms
P99:        18.7 ms
Worst:      24.9 ms

Frames:
under budget: 98.7%
missed:       1.3%
```

For live performance, percentile behavior is often more useful than average FPS.

---

# 1.5 CPU Breakdown

Expose the major CPU contributors.

At minimum:

* audio analysis
* signal bus
* timeline
* animation
* entity updates
* procedural generation
* particle simulation
* physics/simulation
* culling
* scene traversal
* render submission
* synchronization
* resource management

Do not invent categories that cannot be measured.

If precise attribution isn't currently possible, explicitly label it as estimated/coarse.

---

# 1.6 GPU Breakdown

The renderer should eventually expose GPU timing for:

* depth/prepass if present
* geometry
* opaque materials
* transparent materials
* shadows
* particles
* volumetrics/fog
* reflections
* lighting
* post processing
* distortion
* bloom
* temporal effects
* compositing
* UI/output
* other/unknown

Use GPU timestamp queries or the best supported equivalent through Dawn/Metal.

If timestamp queries aren't currently available or reliable on a backend, report that limitation rather than fabricating subsystem measurements.

---

# 1.7 Resource Statistics

Report:

### Geometry

* draw calls
* visible entities
* visible meshes
* triangles
* vertices
* instances
* LOD distribution

### Textures

* texture count
* total texture memory
* largest textures
* render-target memory

### Materials

* material count
* shader variants
* expensive material categories

### Shadows

* shadow-casting entities
* shadow map resolution
* number of shadow passes
* number of lights casting shadows

### Particles

* active particles
* maximum particles
* simulation rate
* particle draw calls

### Post FX

* number of passes
* resolution of each pass
* estimated/actual GPU cost

---

# 1.8 Optimization Candidates

The tool should turn raw profiling into actionable findings.

Example:

```text
OPTIMIZATION CANDIDATES

1. Volumetric fog
   Cost: 3.2 ms
   Suggested change: half-resolution fog
   Estimated savings: 1.4–1.8 ms
   Visual risk: Low

2. Background shadows
   Cost: 2.1 ms
   Suggested change: disable shadows on 14 distant entities
   Estimated savings: 1.0–1.4 ms
   Visual risk: Low

3. Particle simulation
   Cost: 1.8 ms
   Suggested change: 70% simulation population
   Estimated savings: 0.6–0.8 ms
   Visual risk: Medium
```

Estimated savings must be clearly distinguished from measured savings.

---

# 1.9 Phase 1 Deliverable

A developer/agent tool capable of producing a report like:

```text
LIVE SCENE PROFILE

Scene: Glowmere-3
Resolution: 1920×1080
Target: 60 FPS
Budget: 16.67 ms

Measured:
Mean: 18.2 ms
P95: 21.1 ms
P99: 24.8 ms
Worst: 31.4 ms

STATUS:
OVER BUDGET

GPU:
Geometry       2.9 ms
Lighting       2.1 ms
Shadows        3.4 ms
Particles      2.2 ms
Post FX        4.7 ms
Fog            2.1 ms
Composite      0.8 ms

TOP OPPORTUNITIES:

1. Post FX resolution
2. Shadow caster reduction
3. Fog resolution
4. Particle simulation
```

No automatic modification is required in Phase 1.

---

# PHASE 2 — AV Gen Scalability Infrastructure

## Goal

Make the renderer capable of changing quality dimensions systematically without requiring scene-specific hacks.

This is the foundation for all later phases.

---

# 2.1 Quality Dimensions

Create explicit runtime controls for major expensive systems.

At minimum:

```text
resolution_scale
lod_bias
shadow_quality
shadow_distance
shadow_resolution
particle_quality
particle_simulation_rate
volumetric_quality
post_fx_quality
reflection_quality
distortion_quality
bloom_quality
transparency_quality
draw_distance
simulation_quality
```

Exact names should follow existing AV Gen architecture conventions.

Do not introduce redundant settings if equivalent controls already exist.

---

# 2.2 Quality Profiles

Provide named profiles.

Initial profiles:

```text
QUALITY
BALANCED
PERFORMANCE
```

Potentially:

```text
ULTRA
HIGH
BALANCED
PERFORMANCE
```

But do not create meaningless preset proliferation.

Each profile should simply be a collection of explicit settings.

---

# 2.3 Per-System Scalability

Avoid a single global quality knob that makes everything worse.

Instead:

```text
Scene
 ├── Geometry quality
 ├── Lighting quality
 ├── Shadow quality
 ├── Particle quality
 ├── Volumetric quality
 ├── Post FX quality
 └── Simulation quality
```

This allows the optimizer to spend quality selectively.

Example:

```text
Hero geometry: HIGH
Background geometry: PERFORMANCE

Hero shadows: HIGH
Background shadows: OFF

Fog: HALF RESOLUTION
Bloom: HALF RESOLUTION
Particles: 75%
```

---

# 2.4 Resolution Scaling

Implement render-resolution scaling independently from output resolution.

Example:

```text
Output:
1920×1080

Internal render:
1728×972

Post FX:
960×540

Volumetrics:
480×270
```

The output remains 1920×1080.

This should be a first-class capability rather than a collection of special cases.

---

# 2.5 LOD

Introduce or strengthen LOD infrastructure where appropriate.

LOD decisions should consider:

* camera distance
* screen-space size
* object importance
* object category
* animation state
* effect state

Do not blindly simplify hero objects.

---

# 2.6 Visibility/Culling

Strengthen:

* frustum culling
* distance culling
* screen-size culling
* effect-specific culling
* shadow caster culling

The optimizer should be able to identify objects that contribute little or nothing to the final image.

---

# 2.7 Per-Entity Importance

Introduce an optional concept of visual importance.

Example:

```text
hero
foreground
normal
background
ambient
```

This must not become a complicated manual authoring burden.

Defaults should be inferred where possible.

Importance can influence:

* LOD
* shadow casting
* update frequency
* particle density
* effect resolution
* simulation rate

---

# 2.8 Phase 2 Deliverable

AV Gen can run the same scene at:

```text
HIGH
BALANCED
PERFORMANCE
```

with predictable and measurable differences.

The scene itself remains reactive.

No baking of the entire scene is required.

---

# PHASE 3 — Integrated Live Performance Panel

## Goal

Expose performance and scalability information directly inside AV Gen.

This is the human-facing counterpart to the external optimizer.

---

# 3.1 Live Performance Panel

Create a dockable panel.

Example:

```text
LIVE PERFORMANCE

Target       60 FPS
Budget       16.67 ms
Current      14.8 ms
Headroom      1.87 ms

CPU           7.1 ms
GPU          14.8 ms

────────────────────────────

GPU

Geometry       2.8
Lighting       1.7
Shadows        2.1
Particles      1.4
Post FX        3.2
Fog            1.1
Composite      0.6

────────────────────────────

QUALITY

Overall        HIGH
Resolution     100%
Particles       90%
Shadows         HIGH
Fog             50%
Post FX         HIGH

[ OPTIMIZE ]
```

---

# 3.2 Frame-Time Graph

Provide a rolling frame-time graph.

Show:

* target budget line
* actual frame time
* spikes
* dropped frames

Allow approximately 5–30 seconds of history.

---

# 3.3 GPU/CPU Toggle

Allow switching between:

```text
CPU
GPU
FRAME
```

to identify the current limiting subsystem.

---

# 3.4 Resource Inspector

Allow expanding expensive systems.

Example:

```text
SHADOWS — 2.1 ms

Directional:
  1.2 ms
  2048²

Point lights:
  0.7 ms
  6 active

Spot lights:
  0.2 ms
  4 active

Potential:
  Remove background casters
  Estimated savings: 0.8 ms
```

---

# 3.5 Optimize Button

The initial Optimize button should **not silently mutate the scene**.

First implementation:

```text
[ OPTIMIZE ]
```

opens:

```text
OPTIMIZATION REVIEW

Current:
18.2 ms

Potential:
14.9 ms

Suggested:

✓ Half-resolution fog
✓ Disable background shadow casters
✓ Reduce particle simulation to 80%

○ Reduce reflection quality

[ APPLY SELECTED ]
[ CANCEL ]
```

This provides human control.

---

# 3.6 Save Live Profile

Allow a scene to specify:

```text
Live Target:
60 FPS

Preferred Profile:
BALANCED
```

The actual scene remains editable.

---

# PHASE 4 — Automatic Runtime Adaptation

## Goal

Protect the requested live frame rate during actual performance.

This is different from scene optimization.

The optimizer asks:

> "How should we improve this scene?"

Runtime adaptation asks:

> "We're currently falling behind. What can we temporarily reduce?"

---

# 4.1 LIVE AUTO

Add:

```text
LIVE QUALITY:
MANUAL
AUTO
```

Manual:

> User controls quality.

Auto:

> AV Gen dynamically adjusts quality.

---

# 4.2 Hysteresis

Do not change quality every frame.

The system must avoid oscillation.

Bad:

```text
60 → 59 → 60 → 59 → 60
```

Good:

```text
Sustained overload
      ↓
reduce quality
      ↓
measure
      ↓
stable
      ↓
wait for recovery
      ↓
gradually restore quality
```

Use configurable thresholds and hold times.

---

# 4.3 Prioritized Degradation

Create an explicit degradation priority.

Example:

### First

* background particles
* background shadows
* volumetric resolution
* secondary post FX

### Then

* particle count
* reflection quality
* distortion resolution
* LOD bias

### Last

* hero geometry
* primary lighting
* core reactive effects
* output resolution

The exact priority should be configurable.

The optimizer should prefer reducing **low-perceptual-impact work**.

---

# 4.4 Recovery

When frame time becomes comfortably below budget:

Do not immediately restore everything.

Instead:

```text
reduce quality
    ↓
stable for N seconds
    ↓
restore one feature
    ↓
measure
    ↓
stable?
   yes → continue
   no  → revert
```

This makes runtime behavior predictable.

---

# 4.5 Hard Minimum

Every scene should have a minimum live quality level.

The system must not degrade indefinitely.

Example:

```text
Minimum resolution: 70%
Minimum particles: 30%
Minimum shadow quality: LOW
Minimum LOD: PERFORMANCE
```

If the scene still cannot meet the target, report:

```text
LIVE TARGET UNSUSTAINABLE

Current: 23.4 ms
Target: 16.67 ms

Minimum quality reached.
```

Do not destroy visual quality trying to meet an impossible target.

---

# 4.6 Live Mode Safety

Automatic adaptation must never:

* block the render thread
* allocate large resources unpredictably
* compile expensive shaders during performance
* rebuild major scene structures synchronously
* cause audio glitches
* interrupt MIDI processing

Any expensive preparation should happen during prewarm.

---

# 4.7 Phase 4 Deliverable

A musician can launch a scene:

```text
Target: 60 FPS
Mode: LIVE AUTO
```

and AV Gen will maintain the best available visual quality while attempting to remain inside the frame budget.

---

# PHASE 5 — Perceptual / Scene-Aware Optimization

## Goal

Move beyond generic graphics settings into **intelligent optimization based on what actually contributes to the image**.

This is the most sophisticated phase and should not block Phases 1–4.

---

# 5.1 Screen-Space Importance

For every relevant entity, estimate:

* projected screen area
* distance
* visibility
* camera-facing contribution
* motion
* visual prominence
* effect contribution

Conceptually:

```text
Entity A
screen coverage: 34%
distance: 8m
visible: yes
hero: yes

Entity B
screen coverage: 0.3%
distance: 80m
visible: yes
hero: no
```

Entity B becomes a strong optimization candidate.

---

# 5.2 Contribution-Based Optimization

Instead of:

> "Reduce all particles by 50%."

Prefer:

> "Reduce particles that contribute less than N pixels."

Instead of:

> "Disable all shadows."

Prefer:

> "Preserve shadows from hero objects; remove low-contribution shadow casters."

Instead of:

> "Reduce geometry globally."

Prefer:

> "Increase LOD bias for objects occupying <0.5% of the frame."

---

# 5.3 Hero Preservation

The system should identify or accept explicit hero objects.

Hero objects receive protection from aggressive optimization.

Example:

```text
HERO

Mushroom:
  geometry: preserve
  shadow: preserve
  animation: preserve
  particles: preserve
  reactive effects: preserve
```

Background objects become the primary optimization target.

---

# 5.4 Visual A/B Evaluation

When safe, the optimizer should be capable of rendering:

```text
ORIGINAL
OPTIMIZED
```

and comparing them.

The goal is not to claim that an algorithm can perfectly quantify artistic quality.

Instead, expose objective differences:

* pixel difference
* structural difference
* edge difference
* luminance difference
* temporal difference

and optionally provide the result to the Cinematic Critic.

This creates an extremely powerful loop:

```text
Live Optimizer
      ↓
Candidate optimization
      ↓
Render
      ↓
Cinematic Critic
      ↓
Visual impact assessment
      ↓
Accept / reject
```

The two tools should remain separate.

The Live Optimizer measures performance.

The Cinematic Critic evaluates visual quality.

---

# 5.5 Optimization Search

Eventually the optimizer can test multiple candidates.

Example:

```text
Current:
18.7 ms

Candidate A:
fog 50%
estimated: 16.9 ms

Candidate B:
background shadows OFF
estimated: 16.8 ms

Candidate C:
particles 70%
estimated: 17.4 ms

A + B:
15.7 ms

A + C:
16.1 ms

B + C:
15.9 ms
```

It can then identify combinations that achieve the target with minimal visual degradation.

This should only be implemented after profiling and scalability controls are trustworthy.

---

# 5.6 Agent Integration

Expose the entire system to AV Gen's existing agent tooling.

The agent should be able to request:

```text
profile scene
```

```text
optimize scene for 60 FPS
```

```text
optimize scene for 120 FPS
```

```text
find largest performance bottleneck
```

```text
apply low-risk optimizations
```

```text
benchmark before/after
```

Example agent workflow:

```text
1. Build scene
2. Render
3. Cinematic Critic
4. Fix visual problems
5. Live Scene Optimizer
6. Fix performance problems
7. Re-render
8. Cinematic Critic
9. Repeat
```

This should become part of the normal scene-development loop.

---

# Scene Optimization Report

The final report should be machine-readable and human-readable.

Example:

```text
AV GEN LIVE SCENE REPORT
────────────────────────────────────

Scene:
Glowmere-3

Machine:
Apple M2 Max

Target:
60 FPS

Resolution:
1920×1080

Budget:
16.67 ms

Measured:
14.8 ms

P95:
15.9 ms

P99:
16.4 ms

Deadline misses:
0.1%

────────────────────────────────────

GPU

Geometry       2.8 ms
Lighting       1.7 ms
Shadows        2.1 ms
Particles      1.4 ms
Post FX        3.2 ms
Fog            1.1 ms
Composite      0.6 ms

────────────────────────────────────

TOP OPTIMIZATION OPPORTUNITIES

1. Background shadows
   Potential: 0.8–1.1 ms
   Risk: Low

2. Fog resolution
   Potential: 0.6–0.9 ms
   Risk: Low

3. Particle simulation
   Potential: 0.5–0.8 ms
   Risk: Medium

────────────────────────────────────

LIVE PROFILE

Resolution       100%
LOD              HIGH
Shadows          HIGH
Particles         90%
Fog               50%
Post FX           HIGH

STATUS:
✓ TARGET ACHIEVED
✓ HEADROOM AVAILABLE
```

---

# Architectural Requirements

## Do not build this as one monolithic subsystem

The intended architecture is:

```text
                    AV GEN
                      │
          ┌───────────┴───────────┐
          │                       │
      Renderer               Scene System
          │                       │
          └───────────┬───────────┘
                      │
              Performance API
                      │
          ┌───────────┴───────────┐
          │                       │
     Profiling                Scalability
          │                       │
          └───────────┬───────────┘
                      │
              Live Performance
                      │
          ┌───────────┴───────────┐
          │                       │
      Runtime                 External
      adaptation              optimizer
                                  │
                                  ▼
                            Agent tooling
```

---

# Important Architectural Constraint

Do not introduce distributed/multi-GPU rendering as a Phase 1–5 requirement.

The system should be designed so that future render distribution is possible, but the initial architecture should optimize:

> **one good GPU + predictable live performance**

rather than:

> **assume a render farm exists.**

A future architecture could support:

```text
GPU 0
Main scene

GPU 1
Secondary effects

GPU 2
Secondary camera/output
```

or multiple render nodes.

But this should remain a future scalability layer.

---

# Touring-Use Performance Target

AV Gen should explicitly optimize for this scenario:

```text
Laptop
  │
  ├── Audio interface
  ├── MIDI controller
  └── HDMI/USB-C
          │
          ▼
      Projector
```

The system should be capable of:

* 1080p live output
* 60 FPS
* audio-reactive visuals
* MIDI control
* dynamic cameras
* procedural effects
* 3D environments
* live entity effects
* post processing

without requiring a dedicated media-server cluster.

4K and 120 FPS can be treated as higher performance targets rather than baseline requirements.

---

# What Should NOT Be Done

Do not:

* optimize exclusively for average FPS
* automatically reduce quality without explaining why
* globally reduce every quality setting equally
* sacrifice hero objects before background work
* bake away live audio/MIDI responsiveness
* require pre-rendered video for ordinary live scenes
* make the editor dependent on the optimizer
* make the optimizer dependent on the Cinematic Critic
* build multi-GPU infrastructure prematurely
* create dozens of arbitrary quality settings
* make optimization destructive by default
* claim visual equivalence without evidence
* claim estimated performance savings as measured results
* optimize benchmark scenes in ways that don't represent actual live use

---

# Implementation Priority

## Phase 1 — MUST HAVE

**External profiling/diagnostic tool**

Deliver:

* reproducible benchmark
* frame-time statistics
* CPU/GPU measurements
* render-pass timing
* resource statistics
* optimization candidates
* machine-readable output
* fast/deep modes

This is the immediate priority.

---

## Phase 2 — MUST HAVE

**Renderer scalability infrastructure**

Deliver:

* quality dimensions
* resolution scaling
* LOD controls
* shadow scalability
* particle scalability
* volumetric scalability
* post-FX scalability
* draw-distance/culling controls
* quality profiles

This provides the controls Phase 1 needs to make useful recommendations.

---

## Phase 3 — SHOULD HAVE

**Integrated Live Performance panel**

Deliver:

* live frame budget
* CPU/GPU timing
* rolling frame graph
* quality controls
* resource inspection
* optimization recommendations
* manual optimization application

---

## Phase 4 — SHOULD HAVE

**Runtime Live Auto**

Deliver:

* target FPS
* adaptive quality
* hysteresis
* degradation priorities
* recovery
* minimum quality
* no render-thread stalls
* performance-safe transitions

---

## Phase 5 — LATER / ADVANCED

**Perceptual scene-aware optimization**

Deliver:

* screen-space importance
* hero preservation
* contribution analysis
* optimization candidate search
* A/B rendering
* Cinematic Critic integration
* agent-driven optimization loops

This phase should not block the earlier phases.

---

# Definition of Success

The system is successful when an AV Gen scene can go through this workflow:

```text
Artist/Agent creates scene
             ↓
      Cinematic Critic
             ↓
       Visual corrections
             ↓
   Live Scene Optimizer
             ↓
    Performance diagnosis
             ↓
     Apply safe changes
             ↓
      Benchmark again
             ↓
       Under budget?
          /       \
        yes        no
         │          │
         ▼          ▼
     Ship scene   Optimize more
                       │
                       ▼
                Live Performance
                       │
                 LIVE AUTO
                       │
                       ▼
              Touring performance
```

The ultimate goal is not the lowest possible frame time.

The goal is:

> **Maximum perceptual visual quality per millisecond of available compute, while preserving AV Gen's identity as a live audiovisual performance instrument.**

That should be the guiding principle for all five phases.
