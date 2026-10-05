# AV Gen Scene: DIGITAL MOSH — Surrealist Dreamscape

(The owner's brief, verbatim, 2026-10-05.)

## Mission

Create a genuinely ambitious AV Gen scene that explores a central question:

> **What would a dream look like if the dream itself became corrupted data?**

This should be a **visual journey through a surrealist dream world that gradually becomes infected by digital instability**.

The scene should exist somewhere between:

* a beautiful dream
* a Dalí painting
* a fever dream
* a psychedelic hallucination
* a corrupted simulation
* a broken video signal
* a living digital organism
* and, eventually, a nightmare

Do **not** simply build a conventional surreal landscape and add glitch effects on top.

The fundamental premise is that **reality itself is becoming corrupted**.

Geometry, materials, lighting, color, particles, reflections, temporal state, perspective, and spatial relationships should all eventually become susceptible to this corruption.

The result should feel like a **masterfully composed surrealist painting that has somehow become alive, animated, audio-reactive, and progressively infected by a digital rendering failure**.

This is intended to be a major showcase of AV Gen's new rendering capabilities.

---

# 1. DEEP RESEARCH PASS — REQUIRED

Before implementing the scene, conduct a serious visual/artistic research pass.

Do not rely on generic knowledge of "surrealism" or generic AI-generated descriptions.

Research the actual work, techniques, visual philosophies, compositions, and color usage of important Surrealist artists, particularly:

### Salvador Dalí

Study:

* The Persistence of Memory
* The Elephants
* The Temptation of Saint Anthony
* Dream Caused by the Flight of a Bee Around a Pomegranate a Second Before Awakening
* his treatment of landscapes
* melting/softening objects
* impossible physical behavior
* hyper-realistic rendering of impossible objects
* extremely controlled composition
* dream logic
* scale distortion
* barren negative space
* shadows and atmospheric perspective

The important lesson is **not simply "melting clocks."**

Study why the impossible objects feel psychologically convincing when surrounded by extremely convincing physical reality.

### Max Ernst

Research his use of:

* irrational combinations
* collage logic
* fragmented imagery
* unexpected object relationships
* dreamlike environments
* visual ambiguity
* transformation of recognizable objects into unfamiliar forms

### René Magritte

Study:

* object substitution
* scale
* visual paradox
* impossible relationships
* ordinary objects made uncanny
* controlled compositions
* visual ambiguity

### Yves Tanguy

Study:

* strange organic forms
* barren environments
* ambiguous scale
* mysterious landscapes
* biomorphic objects
* dreamlike spatial relationships

### Giorgio de Chirico

Research his use of:

* empty spaces
* uncanny architecture/spatial logic
* long shadows
* exaggerated perspective
* psychological isolation
* metaphysical environments

Do NOT copy his architecture literally. Extract the psychological principles.

### Surrealist theory

Research Surrealism's relationship with:

* dreams
* the subconscious
* automatism
* irrationality
* psychological symbolism
* juxtaposition
* altered perception

Also research modern interpretations of dream imagery and visual perception.

### Digital / glitch / datamoshing research

Separately research:

* datamoshing
* digital video corruption
* temporal displacement
* frame blending
* block displacement
* pixel sorting
* channel separation
* compression artifacts
* feedback systems
* digital image destruction
* temporal echo
* frame corruption
* shader-based glitch techniques
* procedural destruction
* GPU particle fragmentation
* fluid/mesh deformation
* image-space distortion

But don't blindly reproduce cliché "glitch art."

The goal is to understand **what digital corruption actually looks like and why it feels different from an ordinary VFX effect.**

### Research objective

Combine the research into an original visual language.

Do not produce a collage of references.

The scene should feel like:

**Surrealist painting + modern VFX + corrupted digital reality + psychological dream logic**

rather than "Dalí with RGB split."

Document the research findings and use them to drive implementation decisions.

---

# 2. THREE CREATIVE ROLES

Approach this scene simultaneously as:

## A MASTER PAINTER

Think about:

* composition
* color harmony
* visual hierarchy
* silhouette
* negative space
* atmospheric depth
* framing
* contrast
* rhythm
* visual balance
* controlled chaos

Every frame should be capable of functioning as a compelling still image.

## A MASTER VFX ARTIST

Think about:

* deformation
* fluidity
* particles
* fragmentation
* temporal effects
* procedural destruction
* volumetrics
* lighting
* reflections
* refraction
* compositing
* image-space manipulation
* GPU efficiency

Use the renderer's capabilities aggressively.

## A PSYCHOLOGY PROFESSOR

Think about:

* dream logic
* uncanny perception
* expectation violation
* cognitive dissonance
* repetition
* visual ambiguity
* isolation
* fascination
* anxiety
* loss of control
* subconscious symbolism

The goal isn't merely to make the viewer think:

> "That looks cool."

The goal is eventually to make the viewer feel:

> "Something is profoundly wrong with reality."

---

# 3. THE WORLD

Create a surreal landscape that initially feels physically believable.

It should NOT look like a generic fantasy world.

Avoid:

* medieval fantasy
* generic alien planets
* generic sci-fi environments
* generic cyberpunk
* excessive architecture
* videogame-like environments
* obvious "AI art" tropes

Instead construct a sparse, dreamlike world with a small number of extremely deliberate visual elements.

Possible ingredients:

* strange trees
* enormous organic forms
* isolated rocks
* surreal vegetation
* liquid-like terrain
* distant mountains
* strange pools
* impossible flowers
* floating objects
* bizarre organic structures
* enormous shadows
* atmospheric haze
* strange clouds
* objects whose scale is impossible to determine

Objects should remain recognizable enough for the viewer to establish expectations.

Then violate those expectations.

---

# 4. THE MOST IMPORTANT RULE: CONTRAST

Do NOT make everything surreal simultaneously.

Surrealism becomes powerful when **normality creates a baseline for impossibility**.

Early in the scene:

A tree behaves normally.

Then its trunk bends slightly.

Later, it stretches.

Later, it liquefies.

Later, its branches detach from the trunk.

Later, the fragments float upward.

Later, those fragments become particles.

Later, the particles become pixels.

Later, those pixels become another object entirely.

The viewer should gradually realize:

> **The rules governing reality are disappearing.**

Precision makes the impossible more disturbing.

Therefore, maintain periods of extremely beautiful, stable, physically convincing rendering between corruption events.

---

# 5. PSYCHOLOGICAL PROGRESSION

Build the scene as a journey.

## PHASE I — THE DREAM

Beautiful.

Quiet.

Hypnotic.

Almost peaceful.

Use:

* soft atmospheric perspective
* painterly compositions
* beautiful lighting
* restrained movement
* unusual but coherent objects
* subtle environmental motion

The viewer should initially think:

> "This is strange, but beautiful."

---

## PHASE II — THE UNCANNY

Introduce extremely subtle violations.

Examples:

* a shadow points in the wrong direction
* a tree bends slightly against gravity
* a cloud stops moving
* a reflection doesn't match reality
* a distant object briefly changes scale
* a river flows uphill for a moment
* an object appears twice
* a flower rotates toward the camera
* something in the background moves when nothing else does

Do NOT immediately explain these effects.

Let the viewer notice them subconsciously.

---

## PHASE III — THE INFECTION

Now digital corruption begins appearing.

One object becomes unstable.

Then another.

Then nearby objects begin exhibiting similar corruption.

The corruption should appear to **spread**.

Possible mechanisms:

* color contamination
* geometry deformation
* particle emission
* texture migration
* temporal echoes
* spatial displacement
* material instability
* fractured surfaces

Treat corruption almost like an infection.

---

# 6. DATA MOSHING AS A WORLD PROPERTY

This is critical.

Do not limit datamoshing to a post-processing filter.

Reality itself should behave as though its underlying representation is malfunctioning.

Develop multiple corruption domains.

### GEOMETRY CORRUPTION

* vertex displacement
* mesh stretching
* topology tearing
* surfaces folding
* geometry duplication
* fragments detaching
* objects partially exploding
* objects liquefying
* impossible deformation

### TEMPORAL CORRUPTION

* frame echoes
* temporal smearing
* previous states bleeding into current states
* objects existing in multiple temporal states
* brief temporal reversal
* frozen moments
* acceleration/deceleration discontinuities

### SPATIAL CORRUPTION

* distant objects appearing nearby
* pieces of terrain displaced elsewhere
* perspective discontinuities
* horizon deformation
* impossible depth relationships
* sections of the world appearing offset

### MATERIAL CORRUPTION

* textures migrating between objects
* materials changing identity
* color leaking between surfaces
* metallic reflections behaving incorrectly
* surfaces becoming liquid
* surfaces becoming translucent
* reflections showing alternate realities

### IMAGE-SPACE CORRUPTION

Use carefully:

* block displacement
* channel separation
* temporal image displacement
* feedback
* selective pixel displacement
* image tearing
* frame fragments
* compression-like artifacts

But avoid turning the entire scene into noisy glitch soup.

---

# 7. COLOR IS A PRIMARY SYSTEM

Treat color as one of the most important artistic systems in the scene.

Do NOT simply make everything maximally saturated.

Develop a deliberate color progression.

### DREAM PALETTE

Consider:

* warm cream
* dusty yellow
* ochre
* pale cyan
* soft blue
* lavender
* strange peach
* muted pink

These colors should create a beautiful, slightly unreal atmosphere.

### UNCANNY PALETTE

Introduce:

* unnatural turquoise
* violet
* cold cyan
* strange yellow-green
* subtle magenta

### CORRUPTION PALETTE

As instability increases:

* electric magenta
* ultraviolet violet
* intense cyan
* toxic chartreuse
* violent orange
* impossible reds

But preserve periods of restraint.

### COLOR CONTAGION

This is particularly important.

A corruption event should be able to introduce a color into the surrounding environment.

For example:

**magenta fracture**

→ magenta light

→ magenta particles

→ magenta atmospheric scattering

→ magenta vegetation

→ magenta reflections

→ neighboring geometry begins inheriting the color

Color should sometimes behave almost like a **psychological contagion**.

---

# 8. LIQUID / MELTING PHYSICS

Develop convincing transformations between:

**solid → soft → liquid → particles → digital fragments**

Avoid simple sine-wave vertex displacement.

The deformation should have intentional structure.

Examples:

* terrain sagging
* objects stretching under imaginary gravity
* surfaces flowing sideways
* objects becoming wax-like
* materials appearing to liquefy
* liquid surfaces rising into impossible forms
* melted objects reconnecting into other objects

Whenever possible, maintain convincing lighting and shading during deformation.

The viewer should feel that the renderer is actually capable of changing the physical state of the world.

---

# 9. FRACTURE / EXPLOSION

Develop the opposite transformation:

**solid → fracture → fragmentation → explosion → reconstruction**

Objects should occasionally:

1. develop cracks
2. fracture
3. separate into pieces
4. violently explode outward
5. suspend fragments in space
6. reverse direction
7. reconstruct
8. reconstruct incorrectly

A broken object might return as a different object.

This should feel like **data reconstruction**, not merely destruction.

---

# 10. AUDIO REACTIVITY — CORE REQUIREMENT

This scene must be designed from the beginning as an **audio-reactive audiovisual instrument**, not as a static scene with an audio-reactive layer added afterward.

Use AV Gen's existing signal pipeline.

The scene should respond meaningfully to:

* amplitude
* RMS
* transients
* kick
* snare/percussion
* bass energy
* low-mid energy
* midrange
* high frequencies
* spectral centroid
* spectral flux
* beat
* tempo
* musical sections
* sustained energy
* onset density

Create several layers of musical response.

### MICRO RESPONSE

Very fast:

* tiny geometry tremors
* particle movement
* shader distortion
* subtle color modulation
* surface ripples
* small glitch events

### RHYTHMIC RESPONSE

Beat / percussion:

* fractures
* pulses
* object deformation
* particle bursts
* color propagation
* temporal displacement

### MUSICAL RESPONSE

Phrases / sections:

* environmental transformations
* camera behavior
* palette transitions
* emergence of new objects
* increasing corruption
* changes in atmospheric density

### MACRO RESPONSE

Song structure:

**intro → dream**

**verse → uncanny**

**build → infection**

**drop → corruption**

**breakdown → temporary recovery**

**final climax → complete reality collapse**

Do not hard-code this to one particular song.

Build the scene so its behavior responds intelligently to arbitrary music.

---

# 11. LIVE + OFFLINE MUST BOTH BE FIRST-CLASS

The scene must work beautifully in:

### LIVE MODE

Prioritize:

* deterministic behavior
* bounded GPU cost
* graceful degradation
* stable frame rate
* low latency
* immediate audio response
* no expensive operations that cause frame spikes

### OFFLINE MODE

Allow significantly more ambitious rendering:

* higher particle counts
* richer volumetrics
* higher-quality deformation
* additional temporal effects
* more complex lighting
* higher-resolution image-space corruption
* additional secondary effects

Do not create two fundamentally different scenes.

Use quality tiers / scalable parameters so the **same artistic system** operates at different fidelity levels.

---

# 12. AUDIO-REACTIVE CORRUPTION

This is one of the biggest opportunities.

Musical energy should not merely make objects bounce.

Instead:

### Bass

Can affect:

* large-scale deformation
* terrain movement
* massive liquid waves
* deep atmospheric pulses
* large geometry distortions

### Kick

Can trigger:

* fracture events
* geometric shockwaves
* sudden temporal displacement
* object deformation

### High frequencies

Can drive:

* fine particle behavior
* pixel corruption
* edge distortion
* tiny fractures
* color noise
* shimmering surfaces

### Spectral changes

Can influence:

* palette
* material properties
* atmospheric density
* corruption type

### Transients

Can trigger discrete events.

### Musical intensity

Can determine how deeply corrupted the world becomes.

This should feel like **the music is causing reality to lose coherence**.

---

# 13. CAMERA

The camera should be cinematic and psychologically deliberate.

Early:

* slow
* floating
* dreamlike
* graceful
* observational

Middle:

* slightly less predictable
* subtle parallax distortions
* unexpected reveals
* increasing movement

Late:

* increasingly unstable
* impossible transitions
* rapid changes of spatial scale
* brief loss of orientation
* passing through corrupted geometry

But do NOT make the camera simply shake harder.

Camera instability should communicate **loss of spatial certainty**.

---

# 14. NEGATIVE SPACE

Use large areas of emptiness.

Do not fill every frame.

Sparse compositions will make corruption events dramatically more powerful.

A tiny strange object sitting alone in a vast landscape can be more psychologically powerful than 10,000 particles.

Then, when the scene reaches its climax, deliberately violate that restraint.

---

# 15. THE CLIMAX

Eventually the corruption should become systemic.

The world should begin collapsing through multiple representations:

**world**

↓

**geometry**

↓

**fractured geometry**

↓

**particles**

↓

**temporal fragments**

↓

**pixels**

↓

**color**

↓

**light**

The environment should essentially decompose into the fundamental pieces from which the renderer constructed it.

Then—

**everything suddenly becomes calm.**

Return to a beautiful, almost unchanged dream landscape.

But one tiny thing is wrong.

Something from the corrupted world remains.

This should leave the viewer uncertain whether the nightmare actually ended.

---

# 16. DO NOT MAKE THESE MISTAKES

Avoid:

* generic glitch shaders
* constant RGB splitting
* endless particle explosions
* random noise everywhere
* constant camera shake
* generic psychedelic visuals
* generic fantasy environments
* excessive architecture
* "everything is neon"
* meaningless procedural complexity
* effects that don't respond to music
* visual effects that overwhelm composition
* using every technique simultaneously
* making the entire scene chaotic from frame one

The scene needs **restraint, pacing, hierarchy, and contrast**.

Chaos is most effective when the viewer has first been given something coherent to lose.

---

# 17. TECHNICAL EXPECTATIONS

Use AV Gen's existing architecture rather than inventing an isolated rendering system.

Prefer:

* GPU-driven systems
* procedural geometry
* efficient instancing
* shader-based deformation
* GPU particles
* existing signal/modulation infrastructure
* parameterized scene systems
* deterministic random seeds
* scalable quality settings
* existing renderer capabilities

Avoid unnecessary CPU simulation when an equivalent GPU solution is appropriate.

Build reusable systems where doing so improves the scene rather than creating unnecessary abstractions.

The scene should be robust enough to serve as a **renderer capability demonstration**, not a one-off fragile hack.

---

# 18. VALIDATION

Do not judge the result solely by whether the scene runs.

Produce representative renders/stills/clips at multiple stages:

1. Dream
2. Uncanny
3. Infection
4. Corruption
5. Nightmare
6. Collapse
7. Recovery

Evaluate:

* composition
* color
* psychological effect
* visual originality
* audio responsiveness
* temporal coherence
* deformation quality
* material quality
* lighting
* particle quality
* glitch quality
* performance
* live-mode stability
* offline rendering quality

Critically inspect the results.

If something looks like a generic procedural/VFX demo, **iterate rather than rationalize it**.

If an effect is technically impressive but artistically meaningless, remove or redesign it.

---

# FINAL ARTISTIC TARGET

The finished scene should make the viewer feel like they have entered a beautiful dream and then slowly discovered that **the dream is made of corrupted information**.

It should begin as:

**beautiful**

then become:

**strange**

then:

**uncanny**

then:

**fascinating**

then:

**unstable**

then:

**overwhelming**

then:

**nightmarish**

then:

**empty**

And when it finally returns to beauty, the viewer should no longer trust it.

The ultimate goal is not:

> "Look how many effects AV Gen can render."

The goal is:

> **"I forgot I was looking at a rendering demo."**

Make it feel like a genuine audiovisual artwork.

Take the time to research, prototype, critique, and iterate. If the first implementation is technically successful but aesthetically generic, consider that a failed artistic pass and continue refining it.
