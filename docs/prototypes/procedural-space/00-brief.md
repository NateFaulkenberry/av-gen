# Procedural Space POC: the owner's brief (2026-09-29)

*A coordinator's header; the owner's words follow it, verbatim.*

- **Branch and worktree:** `proto/procedural-space` in `../av-gen-space`, from main `a557d63b`. Main stays the
  owner's.
- **Deadline:** a POC ready for the owner's review meeting on the morning of 2026-09-30.
- **Staffing (the owner's instruction):**
  - The art work and art passes are done by a dedicated art agent at Opus 5.5 max effort: visual development
    (Phase 7), the presets (Phase 8), creative evaluation (Phase 11), and the brief's "keep experimenting"
    directive (§40-41).
  - Everything else (the architecture investigation, the research, the engine and shader implementation, audio
    integration, profiling, integration validation and tests) is done by an engineering agent at Opus 5.5 medium
    effort.
  - The coordinator hands work between them.
- **The GPU:** the owner is rendering until about 23:45 on 2026-09-29. Until then, no GPU work of any kind (no
  renders, no GPU tests, no app runs); Phases 1-2 need none. Afterwards, all GPU work goes through
  `tools/gpu-lock.sh`.
- **ADRs:** the block **1000-1019** is assigned to `proto/procedural-space`.
- **Notes:** progress is kept in `docs/prototypes/procedural-space/PROGRESS.md`, current enough that a fresh agent
  can resume. Review media goes in `~/Desktop/av-gen-review/22-procedural-space/`.

---

# AV Gen — Procedural Space / Euclidean Architecture Live-Reactive POC

## 1. Objective

Build a self-contained experimental proof of concept exploring:

> **A procedurally generated 3D space whose geometry and spatial rules respond to live audio.**

The initial visual idea is loosely described as:

**"procedurally rendered Euclidean space"**

but do not interpret that phrase as a rigid technical specification.

The real goal is to discover whether mathematical/procedural manipulation of 3D space can produce a compelling audiovisual environment for AV Gen.

The environment might ultimately resemble:

* impossible architecture
* an endlessly repeating building interior
* a mathematically distorted cathedral
* recursive rooms
* folding corridors
* radial architecture
* geometric chambers
* a procedural tunnel
* a space that continuously reorganizes itself
* something substantially stranger that emerges from the experiments

The POC should prioritize **visual discovery** over prematurely deciding what the final visual concept is.

This is NOT a finished music video.

This is NOT a generalized procedural modeling system.

This is NOT a request to add a permanent "Euclidean Space" feature to AV Gen.

It is an experiment intended to answer:

1. Can AV Gen produce compelling procedural architectural space?
2. Can the geometry itself respond meaningfully to music?
3. Can mathematical transformations create visually interesting "impossible architecture"?
4. Can this run interactively on Apple Silicon through AV Gen's existing WebGPU/Dawn renderer?
5. What is the most appropriate architectural home for this capability inside AV Gen?
6. Does the resulting visual language justify developing a future music-video concept?
7. If successful, what reusable AV Gen primitive should this eventually become?

---

# 2. IMPORTANT: This is an architectural investigation before it is an implementation task

Do NOT assume that the correct implementation is:

```text
new "Euclidean Space" panel
```

Do NOT add a first-class permanent AV Gen UI panel called:

```text
Euclidean Space
```

Do NOT create a large new subsystem merely because the experiment is interesting.

Instead, first inspect the existing AV Gen architecture and determine the most appropriate way to host an experimental procedural-space implementation.

Possible approaches may include:

* an experimental scene generator
* a specialized procedural generator
* a scene-level generator/effect
* a renderer-side procedural pass
* an experimental scene type
* a temporary developer/demo scene
* a reusable generator that eventually becomes part of the existing generator architecture
* another existing AV Gen abstraction that the agent discovers

The agent must investigate the repository before deciding.

The final implementation should use the **least invasive existing architecture that still allows the experiment to be genuinely useful**.

The architectural decision must be documented before implementation.

---

# 3. Phase 0 — Repository architecture investigation

Before modifying code:

Study:

* existing generator architecture
* existing procedural generators
* scene representation
* scene serialization
* renderer architecture
* render passes
* existing fullscreen GPU passes
* existing shader organization
* existing WebGPU/Dawn abstractions
* existing parameter/modulation system
* existing audio signal bus
* existing experimental/demo scene patterns
* existing ImGui panel architecture
* existing developer/debug UI
* existing rendering/export path
* existing test architecture

Specifically answer:

### A. Where should an experimental procedural-space renderer live?

### B. How can it be instantiated without becoming a permanent first-class AV Gen feature?

### C. How can it receive existing AV Gen audio/modulation signals?

### D. How can it participate in scene loading and rendering?

### E. How can the experiment be removed or promoted later without architectural damage?

### F. Can the existing generator abstraction represent this cleanly?

### G. Would an SDF ray marcher be better represented as:

```text
generator
```

or:

```text
render pass
```

or:

```text
scene type
```

or something else?

Do not assume the answer.

The agent should inspect the codebase and make the decision based on existing architecture.

---

# 4. Required research pass

Before implementation, perform a focused technical research pass.

Research at minimum:

## Signed Distance Fields

Understand:

* signed distance functions
* implicit surfaces
* sphere tracing
* ray marching
* SDF primitives
* SDF composition
* CSG
* smooth CSG
* distance-field gradients
* numerical surface normals

## Domain manipulation

Research:

* translation
* rotation
* scaling
* mirroring
* repetition
* infinite repetition
* polar repetition
* domain folding
* twisting
* bending
* coordinate displacement
* recursive transformations

## Procedural geometry

Research:

* procedural architectural generation
* implicit geometry
* functional solid modeling
* fractal architecture
* recursive spatial structures
* mathematical repetition
* impossible architectural forms

## GPU rendering

Research:

* WebGPU ray marching
* WGSL SDF implementation
* GPU sphere tracing
* ray-marched normals
* SDF ambient occlusion
* SDF soft shadows
* ray-marched fog
* performance optimization
* adaptive ray steps
* over-relaxed sphere tracing

## AV / audiovisual possibilities

Research how procedural geometry can be driven by:

* FFT bands
* envelopes
* transients
* beat events
* smoothed continuous signals
* discrete musical events

The research should inform the implementation, not become an academic exercise.

---

# 5. Research references

These are required reading/research references.

## Inigo Quilez

Quilez's work is one of the primary references for SDF-based procedural graphics.

### Distance Functions

[Inigo Quilez — Distance Functions](https://iquilezles.org/www/articles/distfunctions/distfunctions.htm?utm_source=chatgpt.com)

Study:

* analytic SDF primitives
* distance functions
* transformations
* composition

### Ray Marching Distance Fields

[Inigo Quilez — Ray Marching Distance Fields](https://iquilezles.org/www/articles/raymarchingdf/raymarchingdf.htm?utm_source=chatgpt.com)

Study:

* sphere tracing
* ray marching
* hit detection
* numerical stability
* shading

### 3D Distance Functions

[Inigo Quilez — 3D Distance Functions](https://iquilezles.org/articles/distfunctions3d/?utm_source=chatgpt.com)

Study:

* boxes
* cylinders
* toruses
* architectural primitives
* combinations

Do not copy the implementation blindly.

Use these as mathematical references.

---

# 6. The Book of Shaders

### Shapes / Distance Fields

[The Book of Shaders — Shapes and Distance Fields](https://thebookofshaders.com/07/?utm_source=chatgpt.com)

Useful for understanding:

* distance fields
* coordinate manipulation
* combining fields
* polar coordinates
* repetition
* procedural patterns

### Noise

[The Book of Shaders — Noise](https://thebookofshaders.com/12/?utm_source=chatgpt.com)

Useful for understanding:

* procedural noise
* cellular/Voronoi concepts
* distance-based patterns
* spatial subdivision

---

# 7. Open-source WebGPU/SDF examples

These are particularly relevant because AV Gen already uses WebGPU/Dawn/WGSL.

### WebGPU Menger Sponge Raymarcher

[N0Xl0US — WebGPU Raymarch Examples](https://github.com/N0Xl0US/raymarch_examples?utm_source=chatgpt.com)

This is highly relevant.

It demonstrates:

* WebGPU
* WGSL
* SDF ray marching
* 128-step sphere tracing
* analytic SDFs
* tetrahedron normals
* ambient occlusion
* soft shadows
* procedural shading

Use this as a concrete implementation reference, not as a dependency.

### Fractal WebGPU

[zordone — Fractal WebGPU](https://github.com/zordone/fractal-webgpu?utm_source=chatgpt.com)

Relevant for:

* Mandelbulb SDF
* WebGPU
* WGSL
* SDF combinations
* animated procedural parameters
* interactive controls

It demonstrates that complex implicit geometry can be ray-marched directly in WGSL.

### wgpu-raymarcher

[wesfly — wgpu-raymarcher](https://github.com/wesfly/wgpu-raymarcher?utm_source=chatgpt.com)

Relevant for:

* SDF primitives
* smooth blending
* lighting
* shadows
* reflections
* materials
* animation
* performance controls

### WebGPU Raymarching Tutorial

[WebGPU Tutorial — Raymarching](https://github.com/lobiklukas/webgpu-tutorial/tree/main/src/routes/06-raymarching?utm_source=chatgpt.com)

Useful for:

* camera ray construction
* sphere tracing
* numerical normals
* simple lighting
* ray-marching architecture

---

# 8. Other useful implementation references

### libfive

[libfive — Functional Solid Modeling](https://github.com/libfive/libfive?utm_source=chatgpt.com)

libfive is particularly useful as an architectural/reference project because it represents solid geometry functionally and supports:

* CSG
* transformations
* procedural geometry
* functional representations
* C++ integration

Do NOT add libfive as an AV Gen dependency for this POC unless the research discovers a compelling reason.

The purpose is to study how a higher-level procedural geometry representation could work.

### OpenVDB

[OpenVDB](https://www.openvdb.org/?utm_source=chatgpt.com)

[OpenVDB GitHub](https://github.com/AcademySoftwareFoundation/openvdb?utm_source=chatgpt.com)

OpenVDB is useful background research for:

* volumetric representations
* sparse spatial structures
* CSG
* signed distance fields
* dynamic topology
* GPU-oriented NanoVDB concepts

Do NOT add OpenVDB to the POC.

It is research context rather than a recommended dependency.

### OpenVDB documentation

[OpenVDB documentation](https://www.openvdb.org/documentation/?utm_source=chatgpt.com)

Particularly relevant is its treatment of signed-distance representations and spatial transforms.

---

# 9. Existing WebGPU/Dawn references

### Google Dawn

[Google Dawn](https://github.com/google/dawn?utm_source=chatgpt.com)

Dawn is the existing WebGPU implementation used by AV Gen.

It supports native backends including Metal, making it directly relevant to AV Gen's Apple Silicon architecture.

### Dawn architecture

[Dawn Architecture Overview](https://github.com/google/dawn/blob/main/docs/dawn/overview.md?utm_source=chatgpt.com)

Useful for understanding:

* WebGPU abstraction
* native backend translation
* Metal backend
* shader translation
* pipeline architecture

### WebGPU Native Examples

[WebGPU Native Examples using Dawn](https://github.com/samdauwe/webgpu-native-examples?utm_source=chatgpt.com)

Useful for:

* native WebGPU
* Dawn
* compute shaders
* rendering
* performance
* procedural GPU examples

It includes native examples of GPU-driven and procedural techniques.

---

# 10. Research deliverable

Before implementation, produce a short internal research report containing:

## Findings

What techniques appear most relevant?

## Candidate approaches

Compare:

### A. SDF ray marching

### B. Procedural mesh generation

### C. GPU-generated meshes

### D. Compute-generated geometry

### E. Hybrid SDF + raster

### F. Other approach discovered during research

For each discuss:

* visual flexibility
* topology changes
* performance
* implementation complexity
* AV Gen integration
* audio-reactivity
* offline rendering
* future extensibility

Then recommend an approach.

Do not automatically select SDF ray marching simply because this specification discusses it.

---

# 11. Likely preferred approach

The initial hypothesis is:

> **SDF ray marching directly in WGSL may be the best POC implementation.**

Reasons:

* geometry can change topology continuously
* no CPU mesh rebuilding
* procedural transformations are inexpensive to describe
* repetition can create effectively enormous environments
* CSG is straightforward
* audio can modify the mathematical parameters directly
* WebGPU is already present
* WGSL is already present
* the technique is naturally GPU-parallel

However, this is a hypothesis.

The research and architecture pass must validate it.

---

# 12. Do not build a first-class "Euclidean Space" system

This is a hard requirement.

Do NOT create:

```text
Euclidean Space panel
```

as a permanent top-level AV Gen feature.

Do NOT add:

```text
Euclidean Space
```

to the primary user-facing feature hierarchy merely for this experiment.

Do NOT create a giant dedicated UI containing every SDF parameter.

The experiment should use whatever existing AV Gen abstraction the architecture investigation identifies as most appropriate.

For example, if the existing generator architecture naturally supports procedural GPU generators, implement it there.

If an experimental scene is cleaner, use that.

If a render-pass abstraction is cleaner, use that.

The agent must decide based on actual repository architecture.

---

# 13. Experimental UI

A temporary/developer-oriented control surface is acceptable if necessary for experimentation.

It should preferably be:

* scene-specific
* generator-specific
* developer-only
* inspector-based
* parameter-driven
* or otherwise consistent with existing AV Gen architecture

Do not create permanent top-level product UI solely for the POC.

The experiment should eventually be removable without leaving a large UI feature behind.

---

# 14. Initial procedural vocabulary

If SDF ray marching is selected, implement a small expressive vocabulary.

Required primitives:

* sphere
* box
* rounded box
* plane
* cylinder
* torus

Optional:

* capsule
* cone
* triangular prism

The box should be the primary architectural primitive.

---

# 15. Required SDF operations

Implement:

### Boolean

```text
union
intersection
subtraction
```

Conceptually:

```text
union        = min(a, b)
intersection = max(a, b)
subtraction  = max(a, -b)
```

### Smooth operations

If useful:

```text
smooth union
smooth subtraction
smooth intersection
```

with controllable blend radius.

---

# 16. Required domain operations

These are more important than having dozens of primitives.

Investigate and implement:

* translation
* rotation
* scaling
* mirroring
* Cartesian repetition
* polar repetition
* twisting
* bending
* coordinate displacement
* symmetry/folding

The implementation should allow these operations to be combined.

---

# 17. Architectural experiment

The initial procedural environment should read as:

> **a gigantic impossible architectural interior**

rather than:

> "a collection of primitive SDF objects."

Start with:

* floor
* ceiling
* walls
* columns
* doorways
* corridors
* chambers
* repeated structural elements

Then progressively distort the coordinate domain.

The geometry should retain enough architectural cues that the viewer understands it as space.

---

# 18. Experimental presets

Create at least five configurations.

## Infinite Hall

Long corridor with repeated:

* columns
* doorways
* ceiling structures
* floor structures

---

## Recursive Cathedral

Large room containing increasingly nested architectural structures.

Avoid simply making a Menger sponge.

The objective is recursive architecture.

---

## Folding Space

A recognizable room/corridor progressively bends, twists or folds.

---

## Radial Architecture

Use polar repetition for:

* columns
* doorways
* chambers
* architectural rings

---

## Geometry Explosion

Start simple.

As musical intensity increases, progressively increase:

* repetition
* recursion
* symmetry
* extrusion
* twisting
* radial structure

---

# 19. Audio reactivity

Reuse AV Gen's existing:

* audio analysis
* signal bus
* modulation
* smoothing
* transient/beat detection

Do not build a second audio system.

The geometry should respond to music at the **mathematical-rule level**.

---

# 20. Suggested signal mapping

These are starting points, not hard requirements.

### Bass

Drive:

* scale
* extrusion
* structural depth
* corridor width

### Low-mid

Drive:

* bend
* twist
* deformation

### Mid

Drive:

* repetition density
* column spacing
* radial repetition

### High-mid

Drive:

* fine geometric complexity
* subdivision
* surface displacement

### Treble

Drive:

* emission
* surface detail
* edge illumination

### Beat/transient

Drive discrete transformations:

* symmetry changes
* rotation phase
* repetition count
* architectural state changes

### Overall energy

Drive:

* deformation intensity
* lighting
* atmospheric intensity

---

# 21. Critical artistic requirement

Avoid turning this into:

> "everything bounces to the music."

The desired effect is:

> **The music appears to change the rules governing the space.**

For example:

```text
Bass
    → architecture slowly expands

Mids
    → architecture bends

Beat
    → symmetry changes

High frequencies
    → fine detail emerges

Drop
    → spatial configuration changes
```

Use smoothing for continuous signals.

Use discrete events for discrete structural changes.

---

# 22. Camera

Support:

* forward movement
* orbit
* stationary observer
* free camera if existing AV Gen camera controls can be reused

Do not make the camera the primary source of visual motion.

One especially important experiment:

> Keep the camera almost stationary while the architecture reorganizes around it.

This may produce a stronger visual concept than a conventional fly-through.

---

# 23. Rendering

If SDF ray marching is selected, implement:

* fullscreen rendering
* camera ray generation
* sphere tracing
* configurable maximum steps
* configurable maximum distance
* configurable surface epsilon
* numerical normals
* basic lighting
* ambient occlusion if inexpensive
* optional soft shadows
* distance fog
* emissive material

Do not attempt to build a complete path tracer.

---

# 24. Performance

Instrument:

* CPU frame time
* GPU frame time
* ray-march pass time
* average ray steps
* maximum ray steps
* resolution
* render scale

Support at least:

```text
1.0
0.75
0.5
0.33
```

internal render scale.

The POC should remain interactively usable at a lower preview resolution if necessary.

---

# 25. Ray-marching safety

Explicitly bound:

```text
max steps
max distance
surface epsilon
```

Prevent pathological rays from consuming unbounded work.

If transformations invalidate strict SDF distance guarantees, investigate appropriate conservative stepping.

Document any approximations.

---

# 26. Visual direction

The first visual treatment should be:

* dark
* deep blue/purple
* cyan/blue luminous geometry
* restrained green accents
* dark surfaces
* emissive edges
* atmospheric depth

Avoid:

* rainbow gradients
* generic cyberpunk
* excessive bloom
* noisy fractal sludge
* random geometry
* gray programmer-debug aesthetics

The objective is:

> **mysterious mathematical architecture**

---

# 27. Determinism

The experiment must be deterministic given:

```text
seed
time
parameters
audio signals
```

Avoid uncontrolled randomness.

This is important for:

* render reproducibility
* debugging
* Creative Critic evaluation
* future offline rendering
* comparing iterations

---

# 28. Scene representation

The agent should use the existing AV Gen scene representation if possible.

Do not create a parallel scene format solely for this experiment.

If the chosen architecture requires a new experimental scene representation, document why.

The procedural parameters should ideally remain ordinary AV Gen parameters that can eventually be driven by:

* modulation
* automation
* MIDI
* timeline
* Director
* manual control

---

# 29. Shader architecture

If ray marching is selected, keep the WGSL implementation modular.

Conceptually separate:

```text
SDF primitives
SDF operators
domain transformations
scene definition
ray marcher
normal estimation
lighting
materials
```

Do not create one giant unmaintainable shader.

Follow existing AV Gen shader conventions wherever possible.

---

# 30. Do not introduce unnecessary dependencies

Do NOT add:

* libfive
* OpenVDB
* another rendering API
* another shader framework
* another audio-analysis library

unless the research explicitly demonstrates that the dependency solves a problem that cannot reasonably be solved using existing AV Gen infrastructure.

The default implementation should be:

**existing AV Gen C++ + existing WebGPU/Dawn + WGSL.**

---

# 31. No mesh extraction initially

Do not implement:

* marching cubes
* dual contouring
* surface nets
* CPU mesh extraction

unless the research determines that direct procedural rendering cannot satisfy the POC.

The first experiment should determine whether the implicit/GPU representation itself is viable.

---

# 32. Hybrid possibility

Document a potential future architecture such as:

```text
                    Procedural Space
                          |
             +------------+------------+
             |                         |
       Raster Geometry             SDF Space
             |                         |
        existing AV                 ray-marched
             |                         |
             +------------+------------+
                          |
                        Scene
```

This could eventually allow normal AV Gen assets to coexist with mathematical procedural environments.

Do not implement the hybrid architecture unless the POC actually needs it.

---

# 33. Live behavior

The POC must work in real time.

Test with:

1. no audio
2. simple tones
3. drum-heavy material
4. dense EDM
5. complex music

The scene must respond immediately to changing parameters.

Do not require pre-rendering.

---

# 34. Offline rendering

After live preview works, determine whether the chosen implementation can participate in AV Gen's existing render pipeline.

Verify, where practical:

* live preview
* timeline playback
* offline frame rendering

If offline rendering is not yet compatible, document the limitation.

Do not build a separate renderer solely to solve this POC.

---

# 35. Creative Critic compatibility

If practical, make representative captures compatible with AV Gen's existing Creative Critic workflow.

Preserve enough information to identify:

* procedural configuration
* preset
* seed
* camera state
* major spatial parameters
* audio state
* render settings

Do not create a new critic.

---

# 36. Testing

Add appropriate automated tests for the non-visual components.

Potential tests:

* parameter serialization
* deterministic parameter initialization
* preset loading
* modulation mapping
* SDF mathematical functions where testable
* CPU reference SDF functions if useful
* scene integration
* shader compilation/validation if existing infrastructure supports it

Do not create enormous test suites for shader mathematics if the repository does not have a sensible infrastructure for it.

---

# 37. Development sequence

## Phase 1 — Repository architecture investigation

Do not modify the repository.

Determine:

* appropriate integration point
* existing abstractions
* renderer requirements
* parameter/modulation path
* scene integration
* UI strategy

Deliver an architecture recommendation.

---

## Phase 2 — Research

Study:

* SDFs
* ray marching
* domain transformations
* procedural architecture
* WebGPU/WGSL implementations
* alternative approaches

Deliver a short research report.

---

## Phase 3 — Minimal technical experiment

If SDF ray marching is selected:

Render:

```text
sphere
box
plane
```

inside a minimal AV Gen-integrated procedural space.

Verify:

* camera
* ray generation
* ray marching
* shading
* WebGPU pipeline
* performance

---

## Phase 4 — Architectural primitives

Build:

```text
floor
walls
ceiling
columns
doorways
corridors
```

---

## Phase 5 — Domain operations

Add:

```text
repetition
symmetry
polar repetition
twist
bend
folding
```

---

## Phase 6 — Audio integration

Connect:

```text
bass
mid
treble
beat
energy
```

through existing AV Gen infrastructure.

---

## Phase 7 — Visual development

Add:

* lighting
* AO
* fog
* emission
* optional soft shadows

---

## Phase 8 — Presets

Create the five experimental configurations.

---

## Phase 9 — Performance profiling

Measure and optimize demonstrated bottlenecks.

Do not optimize speculative bottlenecks.

---

## Phase 10 — AV Gen integration validation

Verify:

* scene loading
* live preview
* playback
* parameter control
* timeline interaction
* rendering

---

## Phase 11 — Creative evaluation

Capture representative examples.

Run the existing Creative Critic/quality-analysis workflow if compatible.

Document what actually looks compelling.

---

# 38. Required experiment report

At completion, provide:

## Architecture decision

Explain:

* where the implementation lives
* why that location was chosen
* what existing AV Gen abstraction it uses
* why a first-class "Euclidean Space" panel was NOT created
* what would be required to promote this into a production feature

## Research findings

Summarize:

* SDF suitability
* ray-marching suitability
* alternatives considered
* important mathematical techniques
* relevant performance considerations

## Technical results

Report:

* CPU frame time
* GPU frame time
* ray-march time
* average ray steps
* maximum ray steps
* preview resolution
* render scale
* memory if available

## Visual results

For each preset:

```text
Infinite Hall
Recursive Cathedral
Folding Space
Radial Architecture
Geometry Explosion
```

describe:

* visual character
* architectural coherence
* audio reactivity
* motion
* strongest behavior
* weakest behavior
* performance

## Architectural recommendation

Answer:

> If this experiment is successful, what should this capability actually become inside AV Gen?

Possibilities include:

* procedural generator
* procedural environment
* shader-based generator
* implicit geometry generator
* render effect
* hybrid generator
* another abstraction discovered during the investigation

Do not assume the answer beforehand.

---

# 39. Success criteria

The POC succeeds if:

### Technical

* integrates into AV Gen without a parallel application
* uses existing WebGPU/Dawn infrastructure where appropriate
* responds to live audio
* responds to parameters
* remains interactive at a useful preview resolution
* is deterministic
* can be profiled
* does not introduce unnecessary dependencies
* does not create a permanent first-class UI feature prematurely

### Visual

At least one experiment should produce:

* convincing 3D depth
* architectural coherence
* substantial procedural complexity
* meaningful geometry transformation
* obvious musical responsiveness
* visually interesting composition

### Conceptual

The experiment should demonstrate:

> **AV Gen can treat the transformation of mathematical space itself as an audiovisual instrument.**

That is the actual objective.

---

# 40. Artistic directive

Do not stop at the first working corridor.

The first implementation will probably look like a technical demo.

Keep experimenting.

Try:

* extreme repetition
* nested architecture
* symmetry breaking
* radial structures
* folding
* twisting
* recursive scaling
* subtraction
* smooth blending
* architectural deformation
* stationary-camera transformations
* dramatic musical state changes

Try configurations that look strange.

Try configurations that initially seem "wrong."

The goal is to discover a visual language, not merely demonstrate that an SDF renderer works.

---

# 41. Most important conceptual goal

The final visual should ideally move through something like:

```text
normal architecture
        ↓
strange architecture
        ↓
impossible architecture
        ↓
mathematical space
        ↓
complete abstraction
        ↓
architectural reformation
```

The music should control this progression.

The viewer should feel:

> **The music is changing the rules of reality.**

rather than:

> **Objects are bouncing to the beat.**

---

# 42. Final instruction

Treat this as a **research-driven visual experiment**.

Do not assume:

* SDF is definitely the answer
* ray marching is definitely the answer
* a Euclidean Space panel is the answer
* a procedural generator is definitely the answer
* the final visual should be a tunnel
* the final visual should be fractal
* the final visual should be architectural

Investigate first.

Choose the smallest architecture that allows the experiment to answer the artistic and technical questions.

If the experiment produces something genuinely compelling, preserve the successful configuration and explain exactly what mathematical/procedural techniques created it.

Only then should AV Gen consider promoting the capability into a reusable production feature.
