# Sonic Visualizer: art pass, live anti-aliasing, mainline integration, live MIDI. The owner's brief (2026-09-30)

*A coordinator's header; the owner's words follow it, verbatim.*

- **Context.**
  - This follows the first Sonic Garden round, in `00-brief.md`: engineering at `0c440fa0`, art pass 1 at `04fe5113`,
    and an engineering follow-up (the twist-normal fix, the sky rebuild, the profile) running at 09:00.
  - The owner then decided to merge both POCs into main "for now".
- **Staffing (the owner's standing instruction for this project):**
  - The art work is done by `sonic-art` at Opus 5.5 max: Part 1, the art of the live demo scene (Part 15), and the
    live art direction (Part 16).
  - Everything else is done by `sonic-engineer` at Opus 5.5 medium: the AA research and implementation (Parts 2-7),
    the merge preparation (Part 8), the integration, live MIDI and audio (Parts 9-14), and the validation
    (Parts 17-18).
  - The coordinator hands work between them.
- **What only the owner can do.** Parts 15, 17, 18 and 22 need a physical MIDI keyboard and a synthesizer. Agents
  prepare everything and test with virtual MIDI and synthesized audio routed as live input. The owner does the final
  hands-on test.
- **ADRs:** the block 1020-1039 continues. When it runs out, ask the coordinator.
- **Review media:** `~/Desktop/av-gen-review/23-sonic-garden/`.
- **Notes:** `PROGRESS.md`.

---

# AV GEN — SONIC VISUALIZER

## Art Pass → Live Anti-Aliasing Investigation → Mainline Integration → Live MIDI

---

# OBJECTIVE

The Sonic Visual Interpretation POC has now demonstrated enough potential to continue development.

The current POC is visually primitive, but the underlying concept is promising:

* Audio affects visual character.
* MIDI affects musical structure and events.
* Different synth sounds can produce different visual behavior.
* The system has the potential to become a genuinely useful AV Gen capability rather than merely another generic audio visualizer.

The immediate work should happen in this order:

1. Finish the current art pass on the POC.
2. Investigate and improve live-playback anti-aliasing / edge quality.
3. Once the POC and AA investigation are complete, merge the work into `main`.
4. Integrate the Sonic Visual Interpretation system into the actual AV Gen application.
5. Make it possible to connect a physical MIDI keyboard and generate live-reactive visuals from MIDI + audio.
6. Verify the complete live workflow.

Do not skip directly to application integration before the current art pass is complete.

---

# IMPORTANT DEVELOPMENT PHILOSOPHY

This feature has now passed the most important conceptual test:

**It appears to be worth pursuing.**

Therefore, we can move from pure experimentation toward integration.

However, do not turn this into an enormous engineering project.

In particular:

* Do not create unnecessary compatibility infrastructure.
* Do not build a giant automated validation framework.
* Do not spend days proving existing projects are unchanged.
* Do not redesign AV Gen's renderer unless the AA investigation demonstrates a real need.
* Do not rewrite the existing modulation architecture.
* Do not replace existing audio analysis infrastructure without a concrete reason.
* Do not introduce machine learning simply because it is theoretically interesting.
* Do not create a giant visualization editor before the live workflow exists.

The user can visually inspect the application and existing projects.

The priority is:

**research → investigate → implement → visually evaluate → iterate**

---

# PART 1 — FINISH THE CURRENT ART PASS

Complete the art pass already underway on the Sonic Visualizer POC.

Do not abandon the current artistic direction simply because the implementation is primitive.

The objective is to determine what visual language the system wants to become.

Focus on:

* stronger materials
* more deliberate geometry
* better lighting
* more interesting spatial composition
* more sophisticated color relationships
* better environmental atmosphere
* stronger differentiation between sonic character types
* less "debug visualizer" appearance
* more cinematic presentation
* more coherent motion
* better relationships between MIDI events and continuous timbral response

Avoid:

* generic equalizer visuals
* excessive particle explosions
* everything pulsing constantly
* constant camera movement
* arbitrary rainbow color changes
* visual noise used as a substitute for artistic direction

The POC should begin to look like an **AV Gen scene that happens to be controlled by sound**, rather than a technical visualization demo.

---

# PART 2 — LIVE PLAYBACK ANTI-ALIASING INVESTIGATION

After the art pass is complete, investigate the remaining live-playback rendering quality problem.

## Problem

Offline renders have demonstrated that AV Gen can produce substantially better edge quality.

Live playback still exhibits visible:

* jagged edges
* aliasing
* possibly shimmering
* possibly thin-geometry instability
* potentially different edge quality from offline rendering

This discrepancy needs investigation.

Do not assume the solution is simply "enable MSAA."

First determine:

**Why does the offline render path look better than the live viewport path?**

Investigate whether the difference comes from:

* different render resolution
* different render targets
* MSAA/sample count
* resolve behavior
* post-processing
* temporal accumulation
* viewport scaling
* swapchain/backbuffer format
* tone mapping
* render pipeline differences
* dynamic resolution
* different camera jitter
* different shader permutations
* different framebuffer formats
* missing post-processing passes
* live renderer shortcuts
* presentation scaling
* compositing
* edge-sensitive geometry/material behavior

---

# RESEARCH REQUIREMENT

Perform a focused research pass before implementing a solution.

Do not simply choose the first familiar AA technique.

Compare the relevant approaches for AV Gen's architecture:

* MSAA
* FXAA
* SMAA
* TAA
* TAAU
* temporal supersampling
* temporal upscaling
* spatial upscaling
* render-at-higher-resolution + downsample
* hybrid approaches
* potentially MetalFX
* potentially custom resolve techniques

The research should focus specifically on:

**Apple Silicon + Metal + WebGPU/Dawn + real-time 3D rendering + AV Gen's forward/clustered rendering architecture.**

---

# RESEARCH RESOURCES

Use these resources as starting points and continue researching independently.

## Apple Metal — MSAA

Apple's official MSAA documentation:

https://developer.apple.com/documentation/metal/improving-edge-rendering-quality-with-multisample-antialiasing-msaa

Study:

* 2x / 4x / 8x MSAA
* multisample color textures
* multisample depth
* resolve operations
* custom resolves
* HDR-aware resolves
* Apple Silicon behavior
* tile-based rendering implications

Apple specifically documents that MSAA improves primitive edges by using multiple color/depth samples per pixel. Their example also demonstrates that custom resolve behavior can matter when working with HDR rendering.

---

# Apple Metal — Tile-Based Rendering / MSAA

https://developer.apple.com/documentation/metal/tailor-your-apps-for-apple-gpus-and-tile-based-deferred-rendering

Investigate:

* tile shaders
* MSAA on Apple GPUs
* sample storage
* bandwidth implications
* custom resolve opportunities
* Apple Silicon GPU architecture

Apple specifically discusses custom multisample algorithms and the relationship between sample count and primitive-edge quality.

---

# WebGPU Specification — Multisampling

https://www.w3.org/TR/webgpu/

Investigate:

* `sampleCount`
* multisampled textures
* render-pass multisampling
* resolve targets
* format restrictions
* sample-count compatibility
* limitations imposed by WebGPU

WebGPU defines `sampleCount > 1` as a multisampled texture, so inspect how Dawn exposes this capability to AV Gen and what the current renderer is actually doing.

---

# Apple MetalFX

https://developer.apple.com/documentation/metalfx

Specifically:

https://developer.apple.com/documentation/metalfx/applying-temporal-antialiasing-and-upscaling-using-metalfx

Investigate:

* temporal scaling
* temporal anti-aliasing
* history buffers
* motion/depth requirements
* Apple Silicon support
* whether MetalFX is applicable to AV Gen's rendering architecture
* whether it would be appropriate for live viewport rendering

Do not implement MetalFX automatically.

Determine whether it actually fits AV Gen.

Apple describes `MTLFXTemporalScaler` as an effect that generates a higher-resolution result by analyzing multiple input textures over time.

---

# Unreal Engine — Anti-Aliasing Research Reference

Use Unreal's documentation as a reference implementation study.

Anti-aliasing overview:

https://dev.epicgames.com/documentation/en-us/unreal-engine/anti-aliasing-and-upscaling-in-unreal-engine

Temporal Super Resolution:

https://dev.epicgames.com/documentation/en-us/unreal-engine/temporal-super-resolution-in-unreal-engine

Temporal upscalers:

https://dev.epicgames.com/documentation/en-us/unreal-engine/temporal-upscalers-in-unreal-engine

Research:

* TAA
* TAAU
* TSR
* spatial AA
* history buffers
* reprojection
* motion vectors
* ghosting
* history rejection
* thin geometry
* temporal flickering
* camera cuts
* dynamic resolution

Unreal's documentation is particularly useful because it discusses both the benefits and failure modes of temporal methods. Temporal methods use current and previous frames, but require careful handling of motion, reprojection, history rejection, and ghosting.

Do not copy Unreal's architecture.

Use it as a reference for understanding the problem.

---

# SMAA RESEARCH

Research SMAA as a possible relatively lightweight spatial solution.

A useful implementation reference is:

https://docs.rs/bevy/latest/bevy/anti_alias/smaa/index.html

SMAA is interesting because it occupies a middle ground:

* better edge treatment than basic FXAA
* less temporal complexity than TAA
* no history/ghosting problem
* compatible with post-process architectures

The Bevy documentation summarizes the tradeoff between SMAA, MSAA, FXAA, and TAA.

Also research the original SMAA paper/implementation:

https://github.com/iryoku/smaa

---

# FXAA RESEARCH

Research FXAA as the simplest baseline.

It is not necessarily the final solution, but it provides a useful control condition.

Questions to answer:

* Does FXAA meaningfully improve AV Gen's current live output?
* What does it cost?
* Does it blur the highly stylized geometry?
* Does it interact badly with HDR?
* Does it affect Glowmere-style emissive edges?
* Does it improve thin geometry?
* Does it introduce unacceptable softness?

---

# TEMPORAL AA RESEARCH

Research TAA carefully.

Do not assume TAA is automatically better.

Investigate:

* jittered projection
* history buffers
* motion vectors
* camera motion
* object motion
* disocclusion
* history rejection
* ghosting
* rapidly changing procedural geometry
* audio-reactive deformation
* particle systems
* emissive effects
* transparency
* thin geometry

This last category is especially important for AV Gen.

A conventional game engine may have relatively stable geometry.

AV Gen intentionally creates geometry and deformation that can change rapidly in response to audio.

Therefore temporal accumulation may behave differently.

The Sonic Visualizer itself is a useful stress test for TAA because the scene may contain:

* rapidly changing geometry
* particles
* pulsing materials
* camera motion
* procedural deformation
* emissive objects
* changing silhouettes

---

# CRITICAL RESEARCH QUESTION

Determine whether AV Gen should eventually support multiple AA modes rather than committing to one.

For example:

```text
OFF
MSAA
FXAA
SMAA
TAA
```

or some subset.

Do not implement every mode merely because it exists.

Determine which modes actually make sense for:

* live playback
* offline rendering
* low-resolution preview
* high-quality preview
* final rendering

---

# PART 3 — COMPARE LIVE VS OFFLINE PIPELINES

Before changing AA, trace the actual rendering paths.

Document:

```text
LIVE PLAYBACK
    ↓
?
    ↓
?
    ↓
?
    ↓
DISPLAY
```

and:

```text
OFFLINE RENDER
    ↓
?
    ↓
?
    ↓
?
    ↓
OUTPUT
```

Determine precisely where they diverge.

Compare:

* render target resolution
* sample count
* color format
* depth format
* camera
* viewport size
* projection
* post-processing
* tone mapping
* bloom
* temporal effects
* resolve
* upscaling
* output conversion
* presentation

This investigation may reveal that the best solution is not actually a new AA algorithm.

For example, the live renderer might simply be missing a stage already present in the offline renderer.

---

# PART 4 — AA EXPERIMENT

Once the pipeline differences are understood, implement the smallest useful experiment.

Prefer a configurable approach.

For example:

```text
Anti-Aliasing

Off
MSAA 2x
MSAA 4x
FXAA
SMAA
TAA
```

Only expose the modes that are actually implemented.

Do not create fake options.

The first comparison should use the Sonic Visualizer scene because it contains exactly the sort of geometry and motion that matters.

Compare:

* static camera
* moving camera
* rapidly moving geometry
* thin geometry
* emissive geometry
* high contrast edges
* dark backgrounds
* bright objects
* audio-reactive deformation

---

# PART 5 — PERFORMANCE

Live playback is different from offline rendering.

The target is not maximum possible image quality.

The target is:

**excellent edge quality at interactive frame rates.**

Measure enough to determine whether a technique is practical.

Do not optimize prematurely.

At minimum inspect:

* GPU frame time
* CPU frame time
* frame rate
* memory impact
* render-target memory
* additional passes
* resolution scaling

Do not sacrifice the entire live renderer to eliminate every last jagged edge.

---

# PART 6 — VISUAL QUALITY CRITERIA

When evaluating AA, inspect specifically:

### Static edges

Do diagonal edges become smooth?

### Thin geometry

Do thin objects flicker or disappear?

### Motion

Do edges remain stable when the camera moves?

### Procedural deformation

Does audio-reactive geometry shimmer?

### Emissive objects

Do glowing edges remain sharp without producing ugly halos?

### High contrast

Does dark geometry against bright backgrounds remain stable?

### Fine detail

Does the AA method blur small features?

### Temporal behavior

Does the image exhibit:

* ghosting
* trailing
* smearing
* history lag
* accumulation artifacts

The goal is not simply:

"fewer jaggies."

The goal is:

**stable, sharp, attractive live imagery.**

---

# PART 7 — DO NOT DAMAGE THE OFFLINE RENDERER

The live AA investigation must not degrade the existing high-quality offline rendering path.

Ideally:

```text
LIVE
→ optimized interactive AA

OFFLINE
→ highest-quality render path
```

The two paths do not need identical settings.

They should share architecture where practical but can use different quality configurations.

---

# PART 8 — AFTER AA INVESTIGATION: MERGE

Once:

1. the art pass is complete
2. live AA has been investigated
3. the preferred live solution is implemented
4. the POC is visually satisfactory

prepare the branch for integration into `main`.

Before merging:

* inspect changed files
* remove temporary experimental code
* remove debug-only behavior that should not ship
* ensure the feature can be disabled
* ensure the project still builds
* run the relevant existing tests
* inspect the diff
* confirm no accidental unrelated changes
* verify the POC project remains loadable

Do not create a giant release-hardening exercise.

The user will visually inspect the application after integration.

If a regression is discovered, fix it or revert the relevant commit.

---

# PART 9 — INTEGRATE SONIC VISUALIZATION INTO AV GEN

After merging, make the feature available from the actual AV Gen application.

The user should not need to manually run a POC project.

The desired workflow is approximately:

```text
Launch AV Gen

↓

Enable Sonic Visualization / Live Input

↓

Connect MIDI keyboard

↓

Connect audio source / synthesizer

↓

Play notes

↓

AV Gen receives MIDI

↓

AV Gen receives audio

↓

Audio Analyzer interprets timbre

↓

Musical Analyzer interprets notes

↓

Sonic Character + Musical Context

↓

Visual Interpreter

↓

Existing AV Gen signals/modulators

↓

Live scene reacts
```

---

# PART 10 — LIVE MIDI

The first real integration target should be a physical MIDI keyboard.

The user should be able to:

1. connect MIDI keyboard
2. launch AV Gen
3. select the MIDI input device
4. enable live MIDI
5. play notes
6. see note-driven visual behavior

At minimum support:

* note on
* note off
* velocity
* channel
* pitch

Where existing infrastructure allows it.

Do not require MIDI 2.0 for this first integration.

Design internal structures so richer MIDI information can be added later.

---

# PART 11 — LIVE AUDIO

The visualizer also needs actual audio information.

Investigate the cleanest existing AV Gen audio input architecture.

Possible sources:

* system audio
* audio input device
* microphone
* external synth
* DAW routing
* audio interface
* existing AV Gen audio stream

Do not assume a particular routing mechanism.

First inspect the existing application.

The key requirement is:

**the analyzer must receive the actual sound being produced by the synthesizer.**

MIDI tells AV Gen:

> "I played C3."

Audio tells AV Gen:

> "That C3 was a warm, bright, distorted, resonant, noisy, soft, aggressive, etc. sound."

Both are necessary.

---

# PART 12 — MIDI + AUDIO SYNCHRONIZATION

Investigate synchronization carefully.

The system should avoid a situation where:

```text
MIDI note event
```

and:

```text
audio response
```

appear visually disconnected.

The visual interpretation should account for the latency between:

* MIDI event
* synth sound generation
* audio input
* audio analysis
* visual update

For the first implementation, do not attempt perfect sample-accurate synchronization unless the existing architecture makes it straightforward.

Instead:

* measure the apparent latency
* keep the system responsive
* ensure visual reactions feel synchronized to the musician

This is a live instrument, so perceived responsiveness matters more than theoretical architectural perfection.

---

# PART 13 — LIVE MODE UX

Create the smallest useful interface.

Potentially something like:

```text
LIVE SONIC VISUALIZATION

Audio Input:
[ Built-in Output ▼ ]

MIDI Input:
[ My MIDI Keyboard ▼ ]

Status:
● Audio Receiving
● MIDI Receiving

Visualization:
[ Enabled ]

AA:
[ SMAA ▼ ]

Sensitivity:
[───────●──]

Smoothing:
[────●─────]
```

Do not build the final UI prematurely.

A functional debug-oriented UI is acceptable initially.

The most important thing is:

**I plug in my keyboard and it works.**

---

# PART 14 — LIVE SYNTH WORKFLOW

The ideal initial experiment is:

```text
MIDI Keyboard
       │
       ├────────→ Synthesizer
       │              │
       │              ↓
       │           Audio Out
       │              │
       │              ↓
       │        AV Gen Audio Input
       │
       ↓
    AV Gen MIDI
       │
       └──────────────┐
                      ↓
              Sonic Visualizer
```

This allows the user to actually play the synthesizer and see the resulting sonic character become visual.

---

# PART 15 — FIRST LIVE DEMO

After integration, create a very simple live demo scene.

The scene should contain:

* central visual object
* several secondary objects
* background environment
* simple lighting
* atmospheric effect
* camera

It does not need to be a finished music video.

The objective is experimentation.

Play:

### Low soft notes

Observe:

* scale
* warmth
* movement

### High bright notes

Observe:

* brightness
* geometry
* color
* spatial response

### Chords

Observe:

* density
* polyphony
* complexity

### Rapid arpeggios

Observe:

* rhythm
* pitch movement
* repeated patterns

### Distorted sound

Observe:

* roughness
* sharpness
* geometry deformation
* intensity

### Filter sweep

This is especially important.

Play one sustained note and sweep a synth filter.

The MIDI event does not change.

The **audio timbre changes continuously**.

Therefore the visuals should also evolve continuously.

This is probably one of the most compelling demonstrations of the entire system.

---

# PART 16 — LIVE ART DIRECTION

The live system should not feel like:

```text
NOTE ON
→ flash
```

over and over.

It should have multiple timescales.

For example:

```text
NOTE
→ immediate event

TIMBRE
→ evolving visual identity

RHYTHM
→ movement/pattern

AMPLITUDE
→ energy

PITCH
→ vertical/spatial position

FILTER
→ brightness/color/detail

DISTORTION
→ roughness/fragmentation

RESONANCE
→ ringing/pulsing behavior

RELEASE
→ visual decay/trail
```

This is where the system can become genuinely expressive.

---

# PART 17 — FILTER SWEEP TEST

Make this an explicit validation experiment.

Use:

* one MIDI note
* sustained note
* static pitch
* static velocity

Then sweep:

```text
low-pass filter
```

from dark/mellow to bright/open.

The visual system should transition smoothly from something like:

```text
dark
soft
warm
smooth
```

toward:

```text
bright
sharp
detailed
energetic
```

without the MIDI event changing.

This demonstrates that the system actually responds to **sound**, not merely MIDI.

---

# PART 18 — DISTORTION TEST

Repeat the experiment while increasing distortion.

The system should respond to the changing timbre.

Potential visual evolution:

```text
clean
→ smooth
→ more harmonically complex
→ rough
→ aggressive
→ fragmented
```

Again, do not hardcode those exact mappings.

The purpose is to see whether the Sonic Character model provides useful continuous control.

---

# PART 19 — FINAL ARCHITECTURAL MODEL

The eventual architecture should resemble:

```text
                    LIVE MIDI
                       │
                       ▼
                Musical Analyzer
                       │
                       ▼
                Musical Context
                       │
                       │
                       ▼
                   ┌───────┐
                   │       │
LIVE AUDIO ───────→│ Sonic │
                   │ Model │
                   │       │
                   └───┬───┘
                       │
                       ▼
               Visual Interpreter
                       │
                       ▼
                Existing Signals
                       │
                       ▼
                  Modulators
                       │
                       ▼
                  Parameters
                       │
                       ▼
                   Generators
                       │
                       ▼
                    Scene
                       │
                       ▼
                   Renderer
                       │
                       ▼
              Live AA / Upscale
                       │
                       ▼
                    Display
```

The important conceptual separation is:

**Musical information**

versus

**Sonic information**

versus

**Visual interpretation**

versus

**Rendering.**

Do not collapse those layers together.

---

# PART 20 — FUTURE DIRECTION

Do not implement these now unless the live system naturally demands them.

Potential future capabilities:

* track-aware visualization
* individual instrument identities
* persistent timbral visual identities
* audio source classification
* instrument classification
* per-track Sonic Character
* MIDI 2.0 expression
* MPE
* plugin integration
* DAW synchronization
* tempo/beat/phrase-aware visual behavior
* Director-driven visual grammar selection
* Creative Critic evaluation of live visual output
* learned audio-to-visual mappings
* user-defined sonic-to-visual mappings
* preset visual grammars

The architecture should not prevent these possibilities.

But do not build them now.

---

# 21 — FINAL ACCEPTANCE CRITERIA

The work is complete when:

## Art

The POC has progressed beyond a primitive technical visualization and demonstrates a coherent visual identity.

## Live AA

The live renderer has been investigated sufficiently to identify the primary source of its jagged edges.

A practical AA approach has been implemented or the existing rendering configuration has been corrected if that was the underlying problem.

Live playback should exhibit visibly improved edge quality.

## Performance

The live renderer remains interactive.

## Integration

The Sonic Visualization system is available from the main AV Gen application.

## MIDI

A physical MIDI keyboard can be selected and produces visual responses.

## Audio

A live audio source can be selected and analyzed.

## Combined behavior

MIDI and audio influence different aspects of the visual response.

## Timbre

Changing the synth sound changes the visual character.

## Musical content

Changing notes/rhythm/chords changes the visual behavior.

## Continuous timbre

Changing a synth parameter such as filter cutoff changes the visual behavior even when MIDI remains unchanged.

## Responsiveness

Playing the keyboard feels sufficiently immediate.

## Existing AV Gen

Existing projects remain usable.

Do not require a massive formal regression process to establish this.

Load important existing projects and visually inspect them.

---

# 22 — MOST IMPORTANT FINAL TEST

Once everything is integrated, do not immediately build another elaborate test scene.

Connect an actual synthesizer.

Connect the MIDI keyboard.

Play it.

Try:

* bass
* pad
* lead
* pluck
* FM
* distorted
* filtered
* resonant
* noisy
* sustained
* percussive

Then ask:

**Does AV Gen feel like it is actually seeing the sound?**

That is the ultimate test.

If the answer is yes, the Sonic Visual Interpretation system has moved from an interesting POC to a genuinely new AV Gen capability.

---

# 23 — AGENT INSTRUCTIONS

Work autonomously through the following sequence:

1. Finish the existing Sonic Visualizer art pass.
2. Research real-time AA and the live/offline renderer differences.
3. Inspect AV Gen's current render paths before modifying AA.
4. Compare MSAA, FXAA, SMAA, TAA and temporal/upscaling approaches where relevant.
5. Implement the most appropriate practical improvement rather than blindly choosing a fashionable technique.
6. Visually inspect the Sonic Visualizer under live playback.
7. Confirm that the POC remains compelling.
8. Clean up the branch.
9. Merge the completed work into `main`.
10. Integrate the Sonic Visualizer into the actual AV Gen application.
11. Add live MIDI input.
12. Add/select live audio input.
13. Connect both to the Sonic Visual Interpretation system.
14. Create a simple live demonstration scene.
15. Test with an actual MIDI keyboard and synthesizer.
16. Iterate on responsiveness and visual mappings as needed.

Do not stop after the research phase.

Do not stop after the AA investigation.

The ultimate objective of this task is:

**I should be able to launch the main AV Gen application, connect my MIDI keyboard and synthesizer, play notes, change the synth's sound, and watch AV Gen generate live-reactive visuals that respond not only to what notes I play but to what those notes actually sound like.**
