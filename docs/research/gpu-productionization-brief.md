# AV Gen — GPU Procedural Systems Productionization + Flagship LIVE Scene

(The owner's brief, verbatim, 2026-10-05. The owner asked for the whole run to the end, with no checkpoint.)

## Mission

We have completed the gated GPU-world research spike.

Before doing anything else, **inspect the current repository state and determine exactly what has already been implemented and merged into `main` from Phases 0–5.**

Do not assume Phase 5 is merely a proposal. It may already contain implementation work.

The research produced compelling successful systems including:

* Echo Field
* Endless Meadow
* Mycelium
* GPU procedural populations
* GPU-generated procedural worlds
* stateful GPU simulation with deterministic checkpoint/seek behavior

The research conclusion was a **narrow GREEN**:

> AV Gen should not become a GPU-native engine and should not replace its conventional scene architecture. Instead, GPU-native representations should be introduced selectively for workloads that are fundamentally procedural, massively populated, audio-reactive, or stateful.

The immediate goal is therefore **not another broad architecture rewrite**.

The goals are:

1. Audit exactly what is already implemented.
2. Determine whether the current implementation is production-quality.
3. Deep-research and resolve any architectural questions that remain, especially the CPU/GPU bridge.
4. Implement the narrow production architecture only where justified by the research.
5. Create a flagship LIVE audiovisual scene that proves why this architecture matters artistically.
6. Evaluate that scene rigorously and iterate until it is genuinely impressive.

This should be treated as a serious engine + art R&D task.

---

# PHASE 0 — Repository Reality Check

Before modifying anything:

Inspect `main` and determine:

* What GPU-world research code exists?
* What has already been merged?
* Which Phase 5 recommendations were implemented?
* Which are only documented?
* Which prototypes remain isolated?
* Which production systems now depend on the new code?
* What tests exist?
* What benchmarks exist?
* What scenes/assets were created?
* What new shader/compute infrastructure exists?
* What changes were made to `ProceduralGeometry`?
* What CPU mirror exists?
* What GPU state exists?
* What checkpoint infrastructure exists?
* What serialization exists?
* What editor support exists?
* What offline-render support exists?
* What LIVE-render support exists?

Read the research artifacts and ADRs before drawing architectural conclusions.

Produce a concise status report in:

`docs/research/gpu-world-productionization.md`

with:

```text
IMPLEMENTED
PARTIALLY IMPLEMENTED
RESEARCH ONLY
MISSING
RISKY / NEEDS REVIEW
```

Do not duplicate existing infrastructure.

Do not rebuild something that already exists.

---

# PHASE 1 — CPU/GPU BRIDGE DEEP RESEARCH

Before extending the architecture, perform a deep research pass specifically on the bridge between CPU-owned authoring state and GPU-owned execution state.

This is the most important architectural review before productionizing the system.

Research established approaches in:

* GPU-driven rendering
* GPU procedural generation
* GPU simulation
* indirect rendering
* persistent GPU buffers
* GPU-resident simulation state
* procedural scene representations
* GPU picking/query strategies
* deterministic GPU simulation
* checkpoint/replay systems
* timeline-addressable simulations
* GPU/CPU synchronization
* WebGPU storage buffers
* WebGPU indirect drawing
* Metal resource synchronization
* Apple Silicon unified memory
* large GPU-resident populations
* render graph/resource lifetime management
* offline rendering of GPU-generated worlds

Use high-quality primary/technical sources where possible.

Look at established systems such as:

* Unreal Nanite
* Unreal Niagara GPU simulation
* Unity GPU-driven rendering / Entities Graphics
* TouchDesigner GPU particle/simulation workflows
* other serious realtime audiovisual engines
* academic/research work where useful

Do NOT copy architectures blindly.

The purpose is to identify proven patterns and failure modes.

## Questions that MUST be answered

### Ownership

What is authoritative?

Define precisely:

```text
CPU-authored state
GPU-derived state
GPU-simulated state
GPU-render-only state
```

What information is allowed to exist only on the GPU?

What information must always be recoverable from CPU-authored data?

---

### Synchronization

When does CPU data cross into GPU state?

How often?

What causes synchronization?

Can the architecture remain asynchronous during normal LIVE rendering?

Are there accidental GPU→CPU readbacks?

Identify every potential synchronization stall.

---

### Lifetime

Who owns:

* storage buffers
* indirect draw buffers
* simulation buffers
* checkpoint buffers
* procedural descriptors
* transient compute resources
* persistent simulation state?

How are they created/destroyed/resized?

What happens when the editor changes a procedural generator?

What happens when a scene is unloaded?

---

### CPU mirror

Determine whether the existing CPU mirror is appropriately sized.

It must NOT become:

> a second copy of millions of GPU entities.

Prefer compact descriptors and deterministic regeneration/query mechanisms.

Explicitly define what the CPU mirror represents.

---

### Picking / inspection

How does the editor identify a GPU-generated element?

Can the user select something conceptually without materializing the entire population?

Can the editor inspect:

* generator
* seed
* element ID
* world position
* local parameters
* derived state

without forcing a huge GPU→CPU transfer?

---

### Timeline

How do GPU systems behave under:

* play
* pause
* seek
* reverse
* loop
* frame stepping
* changing FPS
* changing audio position
* offline rendering?

Which systems are stateless?

Which are stateful?

Which require checkpoints?

---

### Determinism

Determine exactly what deterministic means for AV Gen.

Do not casually promise bit-exact cross-GPU determinism if WebGPU/Metal floating-point behavior makes that inappropriate.

Distinguish:

* deterministic on one GPU/backend
* deterministic on one machine
* deterministic across runs
* deterministic across Apple GPUs
* deterministic across backends
* visually equivalent

Document the actual guarantee.

---

### Offline rendering

How do GPU procedural systems participate in:

* path tracing
* high-resolution rendering
* AOVs
* frame-by-frame export
* arbitrary frame seeking
* motion blur
* deterministic renders?

What must be baked?

What can remain procedural?

Does the existing bake-to-scatter escape hatch remain sufficient?

---

### LIVE rendering

Determine how the architecture handles:

* live audio
* live MIDI
* rapidly changing parameters
* variable frame time
* dropped frames
* audio latency
* device changes
* projection output

LIVE mode must not accidentally introduce blocking synchronization.

---

### Memory

Establish explicit memory budgets.

Determine:

* maximum practical population sizes
* persistent GPU memory
* transient GPU memory
* checkpoint memory
* indirect buffers
* procedural descriptors

Use actual measurements on the target Apple Silicon hardware.

---

### Failure / fallback

What happens when:

* allocation fails
* population exceeds budget
* compute shader fails
* checkpoint is unavailable
* simulation state is invalid
* GPU device is lost
* offline renderer cannot execute a LIVE-only system?

Define graceful behavior.

---

# PHASE 2 — ARCHITECTURE DECISION

After the research, determine the smallest production architecture justified by evidence.

Do NOT create a monolithic `GPUWorld` abstraction simply because the prototypes were called GPU worlds.

Determine whether the correct model is something closer to:

```text
ProceduralGeometry
    ├── CPU generator
    └── GPU generator
```

plus independently managed:

```text
GPU Fields
GPU Populations
GPU Stateful Simulations
```

or whatever the research demonstrates is actually cleaner.

Require at least two independent use cases before creating a shared abstraction.

Do not abstract speculative commonality.

Produce an architecture diagram.

Produce explicit data-flow diagrams for:

```text
Authoring → GPU
Audio → GPU
MIDI → GPU
Timeline → GPU
GPU → Renderer
GPU → Editor/query
GPU → Checkpoint
Checkpoint → Seek
```

If the current implementation already satisfies these requirements, document that rather than rewriting it.

---

# PHASE 3 — PRODUCTION HARDENING

Only after the architecture review:

Harden the narrow GPU procedural system already justified by the research.

Priorities:

1. correctness
2. determinism where promised
3. editor integration
4. LIVE integration
5. offline integration
6. performance
7. diagnostics
8. testing

Add appropriate automated tests.

Add GPU validation tests where practical.

Add regression benchmarks for:

* Echo Field
* Endless Meadow
* Mycelium

Keep these as permanent architectural regression scenes.

For each, establish expected ranges for:

* CPU cost
* GPU cost
* memory
* population
* synchronization
* seek behavior

Do not optimize blindly.

---

# PHASE 4 — FLAGSHIP LIVE SCENE

Once the production GPU systems are stable enough, create a **new flagship LIVE audiovisual scene**.

This is not another technical demo.

This should become the LIVE equivalent of what GV3 is for offline/cinematic rendering.

The goal is:

> Create the most visually impressive realtime AV Gen scene we have produced so far, specifically designed to demonstrate the full LIVE engine.

It should react to the actual live audio signal and MIDI input.

It should feel like an audiovisual instrument rather than a music visualizer.

---

# ARTISTIC RESEARCH

Before designing the scene, perform a serious art-direction research pass.

Study high-end realtime audiovisual work involving:

* GPU particle fields
* fluid simulations
* procedural worlds
* audio-reactive environments
* feedback/trails
* volumetric effects
* generative geometry
* MIDI-controlled visuals
* live concert visuals
* festival-scale visuals
* cinematic realtime rendering

Look at work from ecosystems such as:

* TouchDesigner
* Notch
* Unreal Engine
* custom GLSL/GPU installations
* generative audiovisual artists
* realtime projection artists

Do not simply copy a recognizable existing artwork.

Extract techniques and visual principles.

The resulting piece should be recognizably **AV Gen**, not a TouchDesigner imitation.

---

# ART DIRECTION — PUSH PAST EVERYTHING WE HAVE DONE

The scene should be:

* colorful
* highly dynamic
* cinematic
* spatially deep
* particle-rich
* procedural
* audio-reactive
* MIDI-reactive
* visually dense without becoming visual noise
* capable of dramatic calm and explosive moments
* beautiful in still frames
* spectacular in motion

Avoid:

* generic spectrum visualizers
* equalizer bars
* generic neon tunnels
* stock particle fountains
* "cyberpunk" clichés
* random noise everywhere
* everything pulsing constantly
* indiscriminate audio modulation
* cheap-looking rainbow effects
* 1990s CG
* "more particles = better"

The visual system should have **composition, hierarchy, scale, depth, and intentional transitions**.

A viewer should be able to look at a frame and say:

> "What the fuck is that?"

rather than:

> "That's a cool audio visualizer."

---

# IMPORTANT CREATIVE PRINCIPLE

Do not map audio directly to everything.

The research should investigate the principle that strong audiovisual work often uses a small number of meaningful relationships rather than making every parameter bounce with the FFT.

Create distinct mappings such as:

```text
LOW / BASS
    mass
    scale
    gravity
    large deformation

MID
    movement
    structural transformation
    population behavior

HIGH
    emission
    particles
    fine detail
    sparkle

BEAT
    major state transition
    impulse
    camera/event
    large-scale visual event

MIDI
    direct performance control
    triggering
    scene mutations
    effect intensity
    camera / composition
```

Use modulation everywhere appropriate, but **use it compositionally**.

---

# EXPLOIT THE NEW GPU SYSTEMS

The flagship scene MUST use the new architecture for something genuinely meaningful.

At minimum, investigate combining:

### GPU procedural field

An enormous spatial structure that does not exist as conventional scene entities.

### GPU population

Hundreds of thousands or millions of visually meaningful elements.

### GPU stateful simulation

A population that evolves over time rather than simply being re-positioned every frame.

### Audio history

Use the Echo Field concept or an evolution of it so that the visual system can respond not merely to "current amplitude" but to temporal structure in the music.

### Procedural world

Use the Endless Meadow concept or something substantially more visually ambitious.

Do not simply recreate the research demos.

Combine and evolve the techniques into a coherent artwork.

---

# PROPOSED VISUAL DIRECTION

Explore a concept around a **living audiovisual world / synthetic ecosystem / cosmic-organic machine**.

The exact concept is yours to determine through research.

Potential ingredients:

* enormous luminous structures
* procedural landscapes
* rivers/fields of particles
* millions of tiny organisms
* large-scale organic forms
* crystalline structures
* fluid-like energy
* atmospheric haze
* volumetric light
* glowing biological networks
* procedural growth
* particle swarms
* trails and afterimages
* temporal echoes
* large-scale environmental transformations
* camera travel through the world
* sudden transformations on musical events

The scene should have a strong central visual identity.

Do not simply put every effect into one scene.

---

# SCENE STRUCTURE

Design the LIVE piece as a sequence of **visual states / movements**, not necessarily hard-coded song sections.

For example:

```text
STATE A
quiet / atmospheric / mysterious

      ↓

STATE B
structure begins growing

      ↓

STATE C
population awakens

      ↓

STATE D
large-scale rhythmic transformation

      ↓

STATE E
particle / fluid eruption

      ↓

STATE F
collapse / reset / transformation

      ↓

STATE G
new world state
```

Transitions should be driven by the music and/or MIDI performance.

The system should be able to respond gracefully to arbitrary live input rather than depending on a fixed prerecorded track.

---

# MIDI PERFORMANCE

The MIDI system should feel like an instrument.

Research the existing AV Gen MIDI capabilities first.

Then provide meaningful controls such as:

* scene intensity
* population behavior
* particle emission
* world deformation
* simulation force
* color/palette transitions
* camera movement
* post-processing intensity
* temporal trails
* event triggering

Avoid mapping every MIDI key to a random effect.

Create an intentional performance vocabulary.

---

# AUDIO REACTIVITY

Use the complete existing audio analysis/modulation stack.

Where appropriate, expose:

* transient detection
* beat
* RMS
* bass
* low-mid
* mid
* high-mid
* treble
* spectral bands
* envelopes
* smoothed signals
* onset events
* tempo/phase information where available

Use modulation curves and temporal shaping.

Avoid jittery direct mappings.

The result should feel musically intentional.

---

# POST-PROCESSING

Use AV Gen's existing cinematic post-processing capabilities aggressively but tastefully.

Investigate:

* bloom
* exposure
* tone mapping
* chromatic effects
* motion blur where appropriate
* depth-aware effects
* atmospheric effects
* glow
* temporal trails
* distortion
* lens effects
* color grading
* vignette
* depth of field if available
* feedback-like effects if architecturally appropriate

The goal is **cinematic cohesion**, not stacking every post effect.

The raw procedural world should already be compelling.

Post should elevate it.

---

# CAMERA

Do not leave the camera static.

Design a realtime camera language.

Use:

* slow cinematic travel
* orbital movement
* parallax
* dramatic scale reveals
* close passes through particle populations
* wide environmental shots
* occasional aggressive musical camera motion

Camera behavior should respond to the visual/music state rather than simply oscillating with a sine wave.

---

# PERFORMANCE TARGET

Target a genuinely useful LIVE performance budget.

Use the existing real projection output path for measurement.

Measure:

* frame time
* CPU time
* GPU time
* simulation cost
* procedural generation cost
* post-processing cost
* memory
* audio latency
* MIDI latency
* synchronization stalls

Do not sacrifice stability for a benchmark screenshot.

The scene should be capable of being performed.

---

# EVALUATION

Use all existing AV Gen evaluation infrastructure where applicable.

If the existing tools are insufficient, create focused evaluation tools rather than manually guessing.

At minimum evaluate:

## Technical

* sustained LIVE frame rate
* frame-time stability
* GPU/CPU budget
* no runaway memory
* no synchronization stalls
* audio responsiveness
* MIDI responsiveness
* deterministic behavior where required
* safe degradation under load

## Visual

Evaluate:

* composition
* depth
* color
* hierarchy
* motion
* particle quality
* lighting
* material quality
* post-processing
* visual coherence
* audio/visual correspondence
* novelty
* cinematic quality

The scene should be judged both as:

1. a technical LIVE-engine demonstration
2. an actual piece of audiovisual art

---

# CREATE A LIVE ART CRITIC IF NECESSARY

If the existing cinematic critic/evaluation tools are designed primarily around offline rendered clips, extend or create a LIVE equivalent.

It should be capable of evaluating:

* representative frame captures
* short LIVE render clips
* scene data
* signal/modulation mappings
* performance telemetry

Prefer a fast/slow evaluation mode if practical.

The evaluation should identify:

* visually weak moments
* over-modulation
* clutter
* repetitive behavior
* weak audio correspondence
* bad camera choices
* poor transitions
* excessive post
* performance bottlenecks

Then iterate.

---

# ART PASS

Do NOT consider the scene complete when the technology works.

After the technical implementation:

1. Render representative stills.
2. Render a short LIVE performance capture.
3. Evaluate them.
4. Identify the weakest visual aspects.
5. Perform an art-directed polish pass.
6. Repeat.

Pay particular attention to whether the scene looks impressive **when paused on a frame**.

If the scene only looks impressive because it is moving rapidly, the composition/material/lighting pass is not finished.

---

# FINAL DELIVERABLE

At completion provide:

### Engine

* productionized narrow GPU procedural architecture
* tests
* benchmarks
* documentation
* ADRs for important architectural decisions

### Research

Update:

`docs/research/gpu-world-productionization.md`

with:

* final CPU/GPU bridge design
* decisions
* rejected alternatives
* performance evidence
* determinism guarantees
* checkpoint strategy
* offline rendering strategy
* LIVE rendering strategy
* known limitations

### Art

Create a permanent flagship LIVE scene.

Give it a proper name.

Do not call it:

* GPU Demo
* GPU World Test
* Particle Test
* Audio Visualizer

It should have an actual artistic identity.

### Evaluation

Provide:

* performance report
* screenshots
* LIVE capture
* visual evaluation
* technical evaluation
* list of remaining weaknesses

---

# CRITICAL DEVELOPMENT RULE

Do not let the art project become an excuse to destabilize the engine.

If the new LIVE scene requires infrastructure that should become reusable, stop and identify the smallest reusable capability.

If it requires a one-off hack that would damage the architecture, keep it isolated or redesign it.

The goal is:

```text
NEW GPU CAPABILITIES
        ↓
REAL AV GEN SYSTEMS
        ↓
FLAGSHIP LIVE ARTWORK
        ↓
DISCOVER WHAT THE ARCHITECTURE SHOULD BECOME
```

not:

```text
ART DEMO
    ↓
GIANT ONE-OFF HACK
    ↓
TECH DEBT
```

---

# Final question the agent must answer

At the end, answer this bluntly:

> After seeing the productionized system and the flagship LIVE artwork, what did the GPU procedural architecture make possible that AV Gen could not reasonably have done before?

Do not answer with benchmark numbers alone.

Answer in terms of **creative capability**.

The ultimate test is whether this work has expanded what AV Gen can be.
