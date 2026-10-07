# AV Gen: First-Class Environment System + Bioluminescent World

(The owner's brief, verbatim, 2026-10-06.)

**The owner's clarifications, given when starting it:**
- Run it alongside Chorus Field.
- The bioluminescent scene must be **unrelated to Glowmere**. Do not reuse Glowmere's look, species or art direction.
- The track is **Trench** (`assets/audio/trench.wav`).

---

We are expanding AV Gen's visual model.

Up to this point, AV Gen has primarily been treated as a conventional audiovisual 3D scene engine: create a world from meshes, characters, animations, cameras, lights, materials, etc., then drive those elements through the audio → analysis → signal bus → modulation → parameters pipeline.

That remains an important capability.

However, we are now explicitly expanding AV Gen into a broader **audiovisual visual-performance platform** with three first-class visual modes:

1. **WORLD** — conventional authored 3D worlds/music videos
2. **ENVIRONMENT** — specialized, highly optimized audiovisual worlds designed primarily to be inhabited/performed rather than constructed object-by-object
3. **HYBRID** — specialized environments combined with conventional AV Gen scene content such as characters, props, animation and cameras

The first major Environment should be a spectacular **bioluminescent audiovisual ecosystem**.

This is not merely a scene and not merely an audio-reactive visualizer.

It should feel like a living alien ecosystem that is listening to the music.

---

# 1. PRODUCT GOAL

The long-term AV Gen concept is:

## WORLD

A user can build a music video.

They can create:

* terrain
* architecture
* environments
* characters
* animations
* props
* cameras
* lighting
* shots
* timelines
* transitions
* choreography

This is the conventional authored-world workflow.

Glowmere and similar projects belong here.

## ENVIRONMENT

A user can instead load an incredible audiovisual world that may use a completely different internal rendering/simulation strategy.

Examples might eventually include:

* Bioluminescent Ecosystem
* Astral Forge
* Echo Field / Million Reeds
* Digital Ocean
* Dream Collapse
* Particle Storm
* Living Mountain
* other specialized audiovisual worlds

These environments are allowed to use specialized GPU techniques, procedural generation, compute simulation, SDFs, procedural geometry, GPU instancing, custom lighting, custom atmosphere, etc.

The implementation does NOT have to resemble a conventional AV Gen scene internally.

## HYBRID

A specialized Environment can optionally coexist with conventional AV Gen scene content.

For example:

> A user places an animated character inside the specialized bioluminescent ecosystem.

Or:

> A band member/creature walks through the ecosystem while the environment itself responds to the music.

The important architectural principle is:

**The commonality between these modes is the audiovisual/control/output infrastructure, not necessarily the renderer.**

Do NOT force every visual experience through a single generalized rendering representation if doing so prevents the visual result we actually want.

---

# 2. THE FIRST ENVIRONMENT: BIOLUMINESCENT ECOSYSTEM

Build the first serious Environment around this artistic concept:

Imagine an enormous alien bioluminescent ecosystem at night.

Initially, it is relatively dark.

There is faint ambient illumination.

Tiny organisms occasionally pulse.

Plants and enormous organic structures subtly move.

The river is dark, reflective and barely visible.

The ecosystem feels alive even when almost nothing is happening.

Then the music begins building.

The bass does not simply make every object brighter.

Instead, **large regions of the ecosystem begin responding to the music**.

A pulse propagates spatially through the environment.

Vegetation begins illuminating along the path of that energy.

Different species respond differently.

Some plants brighten.

Others open or change color.

Tiny organisms begin awakening.

The river begins catching the accumulated light.

The canopy responds slightly later.

Particles/spores begin rising.

Small creatures begin moving.

Then the musical drop occurs.

## BOOM.

Thousands of organisms illuminate.

A large-scale wave of bioluminescence propagates through the environment.

The river catches the glow.

The canopy responds.

Particles erupt upward.

Flocks/swarms of bioluminescent creatures move through the scene.

The camera flies through dense vegetation.

The viewer passes from relative darkness into an enormous glowing ecosystem.

The next musical phrase causes another region of the world to awaken.

The world should feel as though it has a **biological relationship with the music**.

This is the target.

Do NOT reduce this concept to:

> bass → emission intensity

or:

> beat → random particle burst

We are trying to create an **audio-reactive world**, not an audio-reactive collection of objects.

---

# 3. DEEP ART RESEARCH IS REQUIRED

Before committing to the final visual implementation, perform serious visual research.

Do NOT rely on generic "glowing forest" imagery.

Research the visual language of:

* bioluminescent ecosystems
* deep ocean bioluminescence
* glowing fungi
* mycelial networks
* bioluminescent insects
* plankton blooms
* fireflies
* glowing algae
* fluorescent coral
* alien ecosystem concept art
* cinematic alien forests
* dense fantasy bioluminescent environments
* macro photography of fungi and organisms
* Avatar-style bioluminescent environments
* high-end environment concept art
* surrealist environmental painting
* dense botanical environments at night
* volumetric atmospheric environments
* cinematic environmental lighting

Also investigate how real bioluminescence behaves.

We want visual inspiration from reality rather than simply covering everything in neon.

Study:

* scale
* falloff
* clustering
* species variation
* organic repetition
* localized illumination
* indirect illumination
* translucency
* atmospheric scattering
* foreground/background density
* color relationships
* darkness
* visual hierarchy
* how small light sources collectively illuminate an environment

The artistic goal is **richness and biological plausibility**, not generic cyberpunk neon.

---

# 4. VISUAL REFERENCE GATHERING

Start collecting visual references immediately.

Do not wait until the implementation is complete.

Create a reference board or equivalent internal research artifact covering:

### A. Ecosystem density

We need environments with extremely rich visual information.

### B. Bioluminescence

Study how light is distributed rather than merely what colors are used.

### C. Composition

Find examples where:

* foreground organisms frame the shot
* midground contains major visual events
* distant environment establishes scale
* atmospheric depth creates enormous spatial richness

### D. Camera language

Research cinematic movement through dense environments:

* slow reveals
* low passes
* flying through vegetation
* macro-to-wide transitions
* rapid movement through dense spaces
* sweeping valley shots
* orbiting around major ecosystem events

### E. Color

Avoid default "green glowing forest."

Explore sophisticated palettes involving combinations such as:

* cyan
* deep blue
* violet
* magenta
* turquoise
* emerald
* warm biological accents
* occasional near-white emission

Darkness should remain important.

---

# 5. VERY EARLY STILL GENERATION IS MANDATORY

Do NOT spend hours or days implementing the entire system before showing us what it looks like.

Begin generating representative stills as early as possible.

The first objective is to answer:

> "Can AV Gen actually produce the visual richness we are imagining?"

Create several visual experiments/stills representing different interpretations.

For example:

1. Dark ecosystem at rest
2. Dense glowing vegetation
3. Large-scale bioluminescent wave
4. River illuminated by ecosystem activity
5. Dense canopy with thousands of organisms
6. Camera flying through vegetation
7. Massive ecosystem-wide musical event
8. Quiet aftermath

Use these stills to evaluate the artistic direction before committing to detailed implementation.

If the first results look like:

* a normal 3D forest
* a few glowing mushrooms
* sparse objects
* generic green neon
* empty cinematic void
* random particles
* primitive geometry
* a handful of obvious assets

then consider the experiment unsuccessful and iterate.

The target is **dense, immersive, spectacular environmental richness**.

---

# 6. THIS IS ALLOWED TO BE A SPECIALIZED RENDERING PATH

Do NOT assume the existing generalized scene renderer must handle everything.

Investigate what the visual target actually requires.

If the generalized renderer can accomplish it efficiently, use it.

If it cannot, introduce a specialized Environment rendering/simulation path.

Potential techniques to investigate include:

* GPU instancing
* indirect drawing
* compute-driven placement
* GPU procedural vegetation
* hierarchical culling
* impostors where appropriate
* procedural detail
* GPU particle simulation
* compute-based organism simulation
* field-based animation
* procedural terrain detail
* specialized emissive accumulation
* custom bloom/glow treatment
* volumetric particles
* atmospheric scattering
* specialized reflection/refraction
* procedural ecosystem generation
* spatial audio-response fields
* temporal accumulation where appropriate
* distance-based representation changes

Do not implement techniques merely because they are technically impressive.

Use them when they produce the desired visual result or enable the required density/performance.

---

# 7. THINK IN TERMS OF PERCEIVED COMPLEXITY

We do NOT necessarily need millions of expensive conventional meshes.

We need the viewer to perceive:

> millions of living things.

Use the GPU intelligently.

A single ecosystem region might contain:

* hero organisms
* medium vegetation
* dense procedural vegetation
* tiny organisms
* spores
* insects
* distant silhouettes
* atmospheric particles
* floating organisms
* environmental light sources

Different scales should use different representations.

The system should exploit the fact that the viewer does not need every tiny organism represented as a conventional scene entity.

---

# 8. AUDIO MUST DRIVE THE WORLD, NOT JUST OBJECT PARAMETERS

Build a meaningful mapping between music and ecosystem behavior.

Use AV Gen's existing:

Audio → Analysis → Signal Bus → Modulation → Parameters

architecture wherever appropriate.

Potential mappings:

### Bass

Large-scale ecosystem energy.

Potentially controls:

* propagation strength
* vegetation illumination
* large organism activity
* terrain/ecosystem pulses

### Low-mid

Medium-scale biological activity.

Potentially controls:

* plant movement
* organism emergence
* river response
* canopy response

### High-mid / high frequencies

Small-scale activity.

Potentially controls:

* spores
* insects
* tiny organisms
* flickering bioluminescent details

### Beat/transient

Discrete biological events.

### Musical energy

Overall ecosystem activity.

### Spectral relationships

Different species or regions can respond to different frequency ranges.

---

# 9. BUILD SPATIAL AUDIO-REACTIVE PROPAGATION

One of the most important experiments is a **spatial energy field**.

Do not simply illuminate every plant simultaneously.

When a musical event occurs:

1. Generate an energy impulse
2. Place the impulse in world space
3. Propagate it through the ecosystem
4. Have different organisms respond according to:

   * distance
   * species
   * threshold
   * local density
   * accumulated energy
   * cooldown/refractory behavior
5. Allow the response to decay naturally

The result should look like:

> music traveling through a living ecosystem.

Experiment with waves, fronts, branching propagation, localized pulses and large-scale ecosystem activation.

---

# 10. MAKE THE ECOSYSTEM FEEL ALIVE BETWEEN MUSICAL EVENTS

The environment cannot simply sit there waiting for beats.

Create a low-level autonomous ecosystem layer.

Examples:

* occasional organism pulses
* subtle plant motion
* drifting spores
* insects moving through vegetation
* distant creature silhouettes
* gentle water movement
* slow glowing cycles
* tiny localized interactions
* atmospheric movement

Audio should influence this baseline behavior without completely controlling it.

The world should appear alive before the music starts.

The music should **awaken and organize** that life.

---

# 11. CAMERA SYSTEM

The Environment should have an internal cinematic camera system.

Do not assume the user must manually fly the camera.

Provide procedural/cinematic camera behaviors such as:

* ecosystem fly-through
* river flight
* canopy flight
* low vegetation pass
* valley reveal
* orbit around major event
* macro organism shot
* wide ecosystem shot
* rapid musical flight
* slow atmospheric drift

Camera behavior should be influenced by musical energy.

For example:

Quiet section:

> slow drift / long lens / shallow movement

Build:

> increasing forward movement

Drop:

> dramatic traversal through the ecosystem

But do not make it cheesy or mechanically beat-synced.

The camera should feel cinematic.

---

# 12. LIVE + OFFLINE ARE BOTH FIRST-CLASS

This is not a live-only visualizer.

The Environment must be capable of:

### LIVE

* real-time audio input
* low latency
* MIDI control
* parameter control
* stable frame rate
* projector/output workflows
* live camera behavior

### OFFLINE

* deterministic playback
* high-resolution rendering
* higher quality settings
* higher ecosystem density
* improved lighting/atmosphere
* motion blur where appropriate
* accumulation/supersampling where appropriate
* EXR output
* final video rendering

The same Environment should conceptually be the same world in both modes.

Offline rendering should provide **more quality**, not a completely different artistic result.

---

# 13. ARCHITECTURE: BUILD TOWARD THE THREE-MODE MODEL

Do not create an isolated one-off hack that cannot become part of AV Gen.

Investigate and, where appropriate, establish abstractions for:

## World

Conventional scene-based rendering.

## Environment

Specialized audiovisual environment with its own simulation/rendering implementation.

## Hybrid

Environment + conventional AV Gen scene content.

The shared infrastructure should include, where practical:

* audio input
* analysis
* signal bus
* modulation
* parameters
* MIDI
* timeline
* camera/output
* render targets
* live/offline execution
* recording/export
* UI integration

But DO NOT create a giant generic abstraction layer merely for architectural purity.

Prefer simple interfaces.

Avoid backwards-compatibility shims unless required.

Do not prematurely generalize the Environment system around hypothetical future renderers.

Build what this first Environment actually needs, while keeping the architecture clean enough that Astral Forge, Echo Field, etc. could eventually use the same model.

---

# 14. PERFORMANCE IS A CORE REQUIREMENT

The environment must not simply look good in a screenshot.

We need a real-time target.

Measure:

* CPU frame time
* GPU frame time
* memory
* draw calls
* instance counts
* particle counts
* simulation cost
* bandwidth
* culling cost
* lighting cost
* atmosphere cost

Test at realistic live resolutions and 120 Hz where practical.

Do not optimize prematurely before there is enough visual complexity to measure.

But once the visual density is established, profile the actual bottlenecks.

If the existing renderer is the bottleneck, demonstrate why.

If the specialized path is required, demonstrate what it buys us.

---

# 15. DO NOT SACRIFICE THE ART TO PRESERVE THE CURRENT ARCHITECTURE

This is one of the most important instructions.

If the existing architecture makes the Environment visually mediocre, do not simply reduce:

* vegetation density
* organism counts
* atmospheric complexity
* lighting
* particle counts
* ecosystem scale

until it fits.

First determine whether the architecture should evolve.

We are explicitly exploring whether specialized visual environments should be a first-class AV Gen capability.

---

# 16. QUALITY BAR

The result should NOT look like:

* a Unity demo scene
* a generic Unreal environment
* a procedural tech demo
* a handful of glowing mushrooms
* a neon forest
* a music visualizer
* random particles over a landscape
* primitive geometry
* generic AI concept art translated into 3D
* empty "cinematic void"

It should feel like:

> **a vast, mysterious, living bioluminescent ecosystem that happens to respond to music.**

The environment should have:

* enormous perceived scale
* dense foreground detail
* rich midground activity
* distant environmental depth
* sophisticated lighting
* atmospheric layering
* biological variation
* restrained darkness
* beautiful color relationships
* meaningful motion
* cinematic composition
* extraordinary density

The visual references should be used to establish this quality bar.

---

# 17. ITERATIVE DEVELOPMENT LOOP

Use this loop continuously:

### Research

Study the artistic/technical problem.

↓

### Prototype

Build the smallest implementation capable of testing the idea.

↓

### Still

Render representative stills.

↓

### Critique

Determine what looks wrong.

↓

### Increase visual ambition

Add density/detail/lighting/behavior.

↓

### Performance test

Determine what actually costs time.

↓

### Optimize the implementation

↓

### Test audio behavior

↓

### Test live

↓

### Test offline

Do not disappear into architecture for hours without producing visual evidence.

---

# 18. KEEP A RUNNING TECHNICAL/ARTISTIC ASSESSMENT

Document:

### What the generalized renderer does well

### What it does poorly for this Environment

### What needs specialized GPU treatment

### What can remain conventional

### What visual compromises were avoided by specialization

### What new reusable Environment infrastructure was created

### What should remain specific to Bioluminescence

This will help us decide what the eventual Environment API should actually look like rather than designing it abstractly up front.

---

# 19. SUCCESS CRITERIA

The project succeeds when all of the following are true:

### Artistic

A still frame looks like a genuinely impressive high-end bioluminescent environment.

Not merely technically interesting.

It should make us want to explore the world.

### Density

The ecosystem feels extremely rich at multiple scales.

### Audio

Music produces large-scale environmental behavior rather than simple parameter modulation.

### Live

The environment can respond to live audio in real time at a useful frame rate.

### Offline

The same environment can produce substantially higher-quality renders offline.

### Control

The artist can meaningfully control the behavior without needing to understand the underlying GPU implementation.

### Architecture

The result establishes a credible foundation for AV Gen's future **Environment** mode without forcing every environment into the conventional World renderer.

### Most importantly

When the music drops and the ecosystem awakens, the reaction should be:

> **"Holy shit."**

That is the bar.

---

# 20. START NOW

Do not begin by writing a giant implementation plan and then spending the next several hours coding blindly.

First:

1. Inspect the current AV Gen architecture.
2. Identify the cleanest place for the World / Environment / Hybrid distinction.
3. Deep-research the visual/artistic references described above.
4. Establish an initial visual target/reference board.
5. Produce very early stills/prototypes.
6. Determine whether the current renderer can plausibly achieve the target.
7. If not, identify the smallest specialized rendering/simulation architecture needed.
8. Begin implementing the Environment.
9. Keep producing stills as visual checkpoints.
10. Only after the visual direction is clearly compelling should you push aggressively into optimization, live audio response and offline rendering.

**Do not settle for "good enough" because it is easier to implement.**

This experiment is specifically intended to determine whether AV Gen can become a platform for **spectacular specialized audiovisual environments**, not merely conventional 3D scenes with audio-reactive parameters.

Build toward that.
