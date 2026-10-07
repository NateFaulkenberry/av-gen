# AV Gen — Chorus Field

(The owner's brief, verbatim, 2026-10-06. Coordinator's note on the track: §22 Phase 5 names *All You Got*, but the owner earlier said "no more All You Got for these scenes". That conflict is being confirmed with the owner. Until then, develop on `assets/audio/trench.wav` / `assets/audio/feline-footwear.wav`.)

## Procedural Fiber Field Visual Spike

We need to develop a new AV Gen scene concept called **Chorus Field**.

This is explicitly a **GPU capability showcase and reusable engine primitive investigation**, not another attempt to force a beautiful but bespoke concept into the engine.

The goal is to discover whether AV Gen can produce a genuinely striking, surreal, large-scale visual phenomenon from a **cheap massively-instanced GPU primitive + procedural spatial fields + audio-driven deformation**.

The result needs to be something I would actually be excited to put on a projector at a show.

---

# 1. THE CORE IDEA

Do NOT think:

> "Let's make a creature out of particles."

Think:

> "Let's create a physical-looking field of millions of tiny metallic filaments whose collective behavior can spontaneously form enormous structures, faces, waves, organisms, voids, vortices, and other surreal forms."

The individual element should be extremely simple.

The **collective behavior is the artwork**.

The closest conceptual reference is the successful **Echo Field / million reeds** experiment.

That experiment is important because it demonstrated something we should exploit:

**A simple GPU primitive becomes visually sophisticated when enough instances interact through coherent spatial rules.**

Chorus Field should push that idea into 3D.

---

# 2. FIRST: AUDIT WHAT ALREADY WORKS

Before designing anything substantial, inspect the existing AV Gen implementation of:

**Echo Field / million reeds**

Understand exactly why it works.

Identify:

* primitive representation
* instance generation
* GPU buffers
* vertex generation
* simulation/update path
* spatial calculations
* culling
* shading
* camera behavior
* audio inputs
* performance characteristics
* maximum useful instance counts
* what is genuinely cheap
* what is surprisingly expensive
* what visual qualities come from the primitive itself versus the scene implementation

Do NOT immediately replace or redesign this system.

The first objective is:

> **Generalize the smallest possible portion of the Echo Field technique into a reusable 3D procedural filament/fiber primitive.**

If Echo Field already contains functionality that can be extended directly, use it.

Avoid creating an elaborate new particle framework just because it sounds architecturally elegant.

---

# 3. IMMEDIATE VISUAL FEEDBACK IS REQUIRED

This is extremely important.

**Do not spend hours or days building infrastructure before showing me what it looks like.**

As soon as there is a crude but functioning version of the fiber field:

1. Render a still.
2. Render another still with radically different field parameters.
3. Render a third with a different camera/material configuration.
4. Show me the images/results.
5. Then continue iterating.

I want to be able to judge the visual direction **while the system is being developed**.

Do not wait until the scene is "finished."

The development loop should be:

**implement → render still → inspect → adjust → render still → inspect → expand**

not:

**implement everything → polish everything → render at the end**

---

# 4. THE FUNDAMENTAL PRIMITIVE

Investigate a GPU-efficient representation of a:

### Procedural Fiber

Each fiber should ideally be generated from a small amount of data such as:

* seed/index
* base position
* orientation
* length
* width
* curvature
* random phase
* material variation

The fiber can consist of a small number of procedural segments.

Do NOT create a unique heavyweight mesh for every fiber.

Possible representations to investigate:

* instanced short segmented geometry
* procedural vertex generation
* ribbons
* camera-aware strips
* thin tubular approximations
* line/segment primitives if visually sufficient
* a small fixed number of generated segments per fiber

Choose based on **actual visual quality + GPU performance**, not theoretical elegance.

The individual fiber should be cheap enough that we can have:

**100k → 1M → several million → potentially tens of millions**

depending on representation.

---

# 5. THE CRITICAL VISUAL TEST

We need to determine whether the field can create **large-scale emergent structure**.

A random cloud of fibers is NOT success.

A forest of grass is NOT success.

A generic particle system is NOT success.

The image needs to have **coherence at multiple scales**.

For example:

### Micro scale

Individual filaments are visible.

### Meso scale

Thousands of filaments organize into streams, tendons, sheets, vortices, folds, ribbons and bundles.

### Macro scale

Those structures collectively create an enormous recognizable or emotionally evocative form.

This should produce images where you can zoom out and think:

> "Holy shit, what am I looking at?"

rather than:

> "That's a bunch of particles."

---

# 6. SPATIAL FIELD SYSTEM

The fiber positions/orientations should be driven by combinations of inexpensive procedural fields.

Investigate combinations of:

* curl noise
* domain-warped noise
* vortices
* attractors
* repulsors
* radial fields
* directional flow
* spherical fields
* toroidal fields
* sinusoidal displacement
* traveling waves
* turbulence
* local coherence fields
* distance-based deformation
* layered noise

The important thing is that the fields should operate on **groups of fibers**, not independently randomize each fiber.

For example:

```text
global flow
    +
large vortex
    +
local curl noise
    +
audio displacement
    +
distance-based attraction
```

can create something much richer than:

```text
random particle positions
    +
random particle rotations
```

---

# 7. EMERGENT "ENTITY" BEHAVIOR

Here's the most important creative experiment.

I still want some of the **godlike / demonic / organic / alien** visual character from the failed Astral Forge concept.

But we must NOT explicitly render a creature.

Do not create:

* a face mesh
* an SDF face
* eyes
* horns as geometry
* wings as geometry
* a humanoid model
* a special hero object
* a raymarched creature

Instead, investigate whether **large-scale spatial fields can accidentally produce creature-like structures.**

For example, experiment with field constraints that create:

* two large attraction regions
* a central negative-space region
* a strong vertical flow
* lateral branching
* lower convergence
* mirrored or near-mirrored structures
* asymmetric distortion

At certain parameter combinations, the viewer might perceive:

* a face
* a skull
* a creature
* a deity
* a giant organism
* wings
* eyes
* tendons
* a mouth
* a mask

But those things should **never actually exist as modeled objects**.

The field should merely become capable of producing those perceptual interpretations.

This distinction is fundamental.

> **Do not render the mask. Build a field that can sometimes look like a mask.**

And then it should be able to dissolve back into pure abstraction.

That gives us something much more interesting than a procedural character.

---

# 8. NEGATIVE SPACE IS AS IMPORTANT AS THE FIBERS

Do not fill the entire frame.

Large regions of darkness/negative space should emerge between structures.

Experiment with:

* voids
* tunnels
* cavities
* gaps
* apertures
* dense/empty transitions
* silhouettes
* occlusion layers

The viewer should sometimes see an enormous dark form surrounded by luminous fibers.

Then the form should collapse and become something completely different.

This is one of the mechanisms that can make the system feel like a **world** rather than a visualizer.

---

# 9. MATERIAL DIRECTION

Do NOT make this look like grass.

Do NOT make it look like neon particle effects.

Do NOT default to rainbow particles.

The visual target is closer to:

* dark polished metal
* gunmetal
* black chrome
* silver
* oxidized metal
* subtle iridescence
* holographic diffraction
* engraved metal
* extremely thin reflective filaments
* microscopic highlights

Think:

**an impossible metallic organism assembled from millions of hair-thin reflective structures.**

The color should primarily come from:

* lighting
* reflections
* controlled spectral variation
* iridescence
* field-driven material regions
* restrained emissive accents

rather than arbitrary per-particle RGB noise.

---

# 10. DEPTH

The system must work as a genuinely 3D field.

Do not make this a flat 2D particle sheet facing the camera.

Fibers should occupy meaningful depth.

Experiment with:

* layered depth
* foreground filaments
* enormous distant structures
* deep voids
* parallax
* camera penetration
* structures passing around the camera
* volumetric-looking density without requiring expensive volumetric rendering

The camera should sometimes feel like it is **inside the phenomenon**.

Other shots should pull far enough back to reveal the enormous collective structure.

---

# 11. AUDIO REACTIVITY

This must be fundamentally audio reactive.

Do NOT simply:

```text
bass → scale
kick → brightness
```

That would be a lazy implementation.

Use audio to influence the **physical behavior of the field**.

Potential mappings:

### Bass

Large-scale topology.

Can control:

* field strength
* attraction
* expansion/contraction
* global deformation
* density
* large structural breathing

### Kick

Discrete traveling deformation events.

Examples:

* compression wave
* radial shockwave
* topology displacement
* fiber eruption
* collapse/reformation

### Snare/transients

Local discontinuities.

Examples:

* field tearing
* sudden directional change
* branching
* local turbulence
* structural fracture

### Midrange

Coherence.

Higher mids could cause fibers to organize into larger structures.

Lower coherence creates chaotic fragmentation.

### High frequencies

Microstructure.

Use them for:

* filament vibration
* fine turbulence
* iridescence
* small-scale displacement
* shimmering detail

### Musical sections

The overall field configuration should be able to change dramatically between:

* intro
* verse
* build
* drop
* breakdown
* climax

without requiring completely separate scene implementations.

---

# 12. TEST MULTIPLE FIELD "PERSONALITIES"

Do not settle on the first interesting-looking field.

Create a parameterized system capable of generating substantially different visual states.

At minimum investigate:

### A. Flow

Huge coherent directional streams.

### B. Vortex

Massive rotating structures.

### C. Choir

Many vertical/organic fiber columns forming a collective body.

### D. Emergence

Fields create temporary face/entity-like structures.

### E. Collapse

The entire field contracts toward a point or surface.

### F. Explosion

A coherent structure violently disperses.

### G. Storm

High turbulence and fragmented structures.

### H. Void

Dense fibers orbit enormous empty regions.

### I. Bloom

Structures expand outward from a central region.

### J. Fracture

Coherent structures break into thousands of smaller strands.

These are not necessarily final scene modes.

They are **experiments to discover the visual vocabulary of the primitive.**

---

# 13. CAMERA EXPERIMENTS

Generate stills from several camera relationships.

Do not assume the best image is the obvious frontal shot.

Test:

* extreme wide shot
* close-up through fibers
* inside a vortex
* looking along a filament stream
* low angle
* distant silhouette
* camera passing through a structure
* macro view of individual fibers
* almost orthographic-looking composition
* deep perspective composition

The camera should be treated as part of the visual system.

---

# 14. THE "STILL TEST"

This is a major success criterion.

At several stages, freeze the system and render high-quality stills.

Ask:

> Would this image be interesting if I saw it on an art book page with no knowledge of AV Gen?

If the answer is no, the system isn't there yet.

We are NOT trying to create something that looks impressive solely because it moves.

The underlying frame needs to be compelling.

---

# 15. THE "ZOOM TEST"

Every promising image should pass three scales:

### Far

Does the overall composition look intentional?

### Medium

Do the structures form interesting patterns?

### Close

Are the individual fibers/materials beautiful?

If one scale is compelling but the others are noise, improve the system.

---

# 16. THE "NOT A VISUALIZER" TEST

Reject anything that resembles:

* generic music visualizer
* equalizer
* particle fountain
* fireworks
* galaxy particles
* neon tunnel
* random dots
* screensaver
* grass field
* generic audio-reactive blob
* stock VFX demo

The goal is **surreal physical phenomena**, not "cool particles."

---

# 17. PERFORMANCE IS PART OF THE DESIGN

This is AV Gen.

Do not build a beautiful prototype that requires the exact same special-purpose optimization problem as Astral Forge.

The system must remain fundamentally compatible with the existing renderer architecture.

Profile continuously.

Measure:

* instance count
* vertex count
* GPU time
* CPU time
* buffer upload cost
* simulation cost
* render-pass cost
* memory
* 1080p performance
* live-resolution performance

Test progressively:

```text
100K
250K
500K
1M
2M
5M
10M
```

or whatever counts make sense for the chosen representation.

Do not assume that more particles automatically produce a better image.

Find the point where **visual complexity per GPU millisecond** is maximized.

---

# 18. LIVE VS OFFLINE

The same procedural field should work in both modes.

### Live

Prioritize:

* deterministic behavior
* stable frame time
* low CPU overhead
* GPU scalability
* immediate parameter changes
* MIDI/audio control

### Offline

Allow:

* higher density
* higher-quality shading
* more segments
* higher-resolution output
* longer accumulation
* more expensive post-processing

But the underlying phenomenon should remain the same.

We are NOT creating a separate offline renderer.

---

# 19. ENGINE DESIGN

If the prototype succeeds, extract the reusable capability into something conceptually like:

## Procedural Fiber Field

Potential parameters:

```text
density
length
width
segments
curvature
orientation
coherence
flow_strength
curl_strength
vortex_strength
attraction
repulsion
turbulence
noise_scale
noise_strength
depth_distribution
field_scale
field_warp
audio_influence
material_variation
```

But do NOT blindly create a giant API up front.

Start with the smallest parameter set that produces compelling imagery.

Only promote parameters into the reusable engine abstraction when they prove useful.

---

# 20. IMPORTANT ARCHITECTURAL RULE

Do NOT solve this by adding a special-case:

> "ChorusFieldRenderer"

that only exists to render this scene.

That would recreate the Astral Forge problem.

The objective is to discover a **general visual primitive** that other AV Gen scenes can use.

A successful result should make it possible to build families of scenes from the same capability:

* Echo Field
* Chorus Field
* Neural Field
* Storm Field
* Metallic Field
* Choir Field
* Void Field
* Bloom Field
* Fracture Field

The scene is the demonstration.

The **fiber field is the real deliverable.**

---

# 21. RESEARCH

Before committing to the implementation, perform focused technical research into relevant techniques.

Investigate:

* GPU procedural strand/fiber rendering
* massively instanced geometry
* procedural curve generation
* GPU curl-noise fields
* particle advection
* vector-field-driven particles
* GPU-generated ribbons
* strand rendering
* thin geometry / hair-like rendering
* screen-space strand techniques
* GPU indirect drawing where relevant
* field-based emergent forms
* efficient 3D vector fields
* audio-reactive particle/field systems used in professional realtime visual systems

Look for techniques that can actually be adapted to AV Gen.

Do NOT turn this into a research report.

Research should answer:

> "What is the cheapest technically sound way to produce the visual phenomenon we want?"

---

# 22. DEVELOPMENT PHASES

## Phase 0 — Audit

Inspect Echo Field.

Document:

* what makes it fast
* what makes it visually compelling
* what can be generalized
* what should remain scene-specific

No major code changes yet.

---

## Phase 1 — 3D Fiber Spike

Create the smallest possible 3D fiber field.

No fancy scene.

No elaborate audio direction.

Just:

**millions of cheap fibers + procedural spatial fields + good lighting**

Immediately render stills.

### Gate:

The raw field must already look visually interesting before proceeding.

If it looks like grass, particles, or a screensaver, STOP and change the representation/field/shading.

---

## Phase 2 — Emergent Structures

Introduce combinations of:

* flow
* curl
* attraction
* repulsion
* vortices
* domain warping
* negative space

Start generating large-scale structures.

Render stills continuously.

Find at least **3 genuinely compelling visual configurations**.

---

## Phase 3 — Entity Emergence

Experiment with field configurations capable of producing:

* face-like forms
* skull-like forms
* giant organisms
* wings
* tendons
* eyes/voids
* masks

WITHOUT explicitly modeling any of them.

This phase is successful if the viewer can perceive something recognizable that does not actually exist in the geometry.

Render stills.

---

## Phase 4 — Material / Depth

Push:

* metallic response
* reflections
* iridescence
* diffraction
* dark/light contrast
* depth
* camera penetration

Again, render stills.

Do not hide weak geometry behind post-processing.

---

## Phase 5 — Audio

Introduce actual *All You Got* analysis.

Make audio alter:

**field topology first**

and appearance second.

Render short clips and still frames from significant musical events.

---

## Phase 6 — Performance Scaling

Benchmark:

* 1080p
* live canvas resolution
* 120 Hz target
* increasing fiber counts

Find the best visual/performance operating point.

---

## Phase 7 — Generalization

Only after the visual phenomenon is compelling:

extract the reusable Procedural Fiber Field capability.

Document which pieces belong in the engine and which belong in scene configuration.

---

# 23. CONTINUOUS VISUAL CHECKPOINTS

This requirement is non-negotiable.

At minimum, produce visual checkpoints after:

1. first functioning fiber field
2. first coherent field
3. first vortex/flow experiment
4. first emergent macrostructure
5. first entity-like emergence
6. first material pass
7. first audio-reactive pass
8. performance-optimized pass

Do NOT wait until the end.

If the tooling allows automated still generation, create a small contact sheet of parameter variations so we can rapidly judge which direction is worth pursuing.

---

# 24. CREATIVE BAR

The bar is high.

I don't want:

> "technically impressive particle simulation."

I want:

> **"What the fuck is that?"**

The best result should feel like an impossible physical phenomenon.

Something between:

* living metallic matter
* a gigantic organism
* an alien intelligence
* a god appearing inside a storm
* microscopic structures scaled to impossible size
* a psychedelic engraving coming alive
* an enormous field temporarily developing consciousness

But none of those things should be explicitly modeled.

The viewer should be unable to determine whether they're looking at:

**matter, energy, organism, machine, weather, sculpture, or some completely new category of thing.**

That ambiguity is desirable.

---

# 25. ABSOLUTE DON'Ts

Do NOT:

* build a conventional creature
* build a character
* build a face mesh
* use SDF raymarching for a hero object
* create a special-purpose hero renderer
* create architecture
* create buildings
* create rooms
* create corridors
* create temples
* create ruins
* create generic sci-fi structures
* make random spinning objects
* make a generic particle visualizer
* make everything neon
* make everything rainbow
* hide weak geometry behind bloom
* spend days on infrastructure before producing images
* optimize a boring visual into a faster boring visual
* create a giant new subsystem without proving the visual need

---

# 26. SUCCESS CRITERIA

The spike succeeds only if all of these become plausible:

### Visual

At least several stills are genuinely striking.

### Emergence

Large-scale structures arise from simple elements.

### Depth

The phenomenon feels truly 3D.

### Audio

Music changes the physical behavior rather than merely the brightness.

### Performance

The primitive scales massively on Apple Silicon GPU hardware.

### Generality

The same primitive can produce substantially different scenes.

### Engine fit

It uses AV Gen's existing GPU architecture rather than requiring another bespoke renderer.

### Creative potential

I can look at the result and immediately think:

> "There are 20 scenes I could build from this."

That last criterion matters enormously.

---

# 27. FINAL DEVELOPMENT PRINCIPLE

Do not ask:

> "How do we make AV Gen render the cool thing I imagined?"

Ask:

> **"What incredibly cool visual phenomenon naturally falls out of the things AV Gen can render extremely cheaply?"**

Echo Field suggests that the answer may be:

**massive populations of simple geometric elements + coherent spatial fields + emergent structure.**

Let's find out.

And **show me the images early enough that I can kill a bad direction before we spend two days building it.**
