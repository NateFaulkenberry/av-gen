# Sonic Garden: VFX expansion brief (the owner, 2026-10-02)

*A coordinator's header; the owner's words follow it, verbatim.*
- **Base:** `proto/sonic-garden`, fast-forwarded to main `8ae10a64`. Main has the live Sonic system, projection,
  live AA, both SDF POCs, and the Liminal pass 2 systems (beat grid, text, line look, mosh, sweep).
  - Liminal pass 3 (ADR-1051 to 1055: the spatial validator, SDF rim, jump tracing, screen static, world wave) is on
    `proto/liminal-space`, NOT yet on main. Read it; don't depend on it unless you merge it in deliberately.
- **Staffing (the owner's standing rule):**
  - `sonic-engineer` (Opus medium): research reports 2, 4 and 5 (modulation and audio analysis, glitch/feedback
    techniques, automated evaluation); the response architecture and the sensitivity/transient model; technical
    feasibility; and implementation of the engine capabilities (transient/onset detection, the response model, live
    controls, entity- and environment-effect modulation, post effects, glitch/feedback, evaluator tooling,
    performance).
  - `sonic-art` (Opus max): research reports 1 and 3 (VFX scene construction, authoredness); the scene catalog; the
    12-20 authored scenes with scene-specific mappings; the test matrix; the evaluation-driven iteration; and the
    captures.
  - The coordinator hands work between them.
- **GPU:** all GPU work goes through `tools/gpu-lock.sh`, including every `avgen_tests` run. The owner's use comes
  first.
- **ADRs:** check `docs/decisions/README.md` and take the next free block. Ask the coordinator.
- **Review media:** `~/Desktop/av-gen-review/25-sonic-vfx/`.
- **Commit at every milestone**, and keep the progress notes resumable cold.

---

# Sonic Garden — VFX Expansion, Scene Variety & Live Musical Responsiveness

## Objective

Sonic Garden has reached the point where the underlying concept is clearly working: live MIDI and audio input can drive visual scenes in real time, and the current example scenes demonstrate several different mappings between sound and visual behavior.

The next phase should substantially expand this into something that feels less like a collection of technical demonstrations and more like a **hand-crafted audiovisual VFX playground / live visual instrument**.

The goal is not simply "more effects."

The goal is:

> **Create a collection of visually authored, highly distinctive environments that respond musically and expressively to very different kinds of live input.**

When cycling through Sonic Garden with different synth patches, MIDI performances, bass lines, melodies, chords, percussion, and electronic drums, it should feel like discovering a large collection of different audiovisual worlds rather than rotating through four variations of the same procedural scene.

The visuals should increasingly feel like something a VFX artist spent hundreds of hours designing, even though the underlying system remains procedural and real-time.

---

# 1. BEGIN WITH A DEEP RESEARCH PASS

Before making significant implementation changes, perform a substantial research pass.

Do not constrain the research to the techniques currently used in AV Gen.

Look outside the existing implementation and investigate how professional realtime VFX, motion graphics, music visualization, game VFX, demoscene graphics, and audiovisual installations create visually compelling environments.

Research at minimum:

### VFX / realtime references

Investigate techniques and visual examples involving:

* volumetric environments
* procedural landscapes
* atmospheric perspective
* particle fields
* fluid-like motion
* smoke and fog
* energy fields
* portals
* force fields
* magical/fantasy environments
* sci-fi environments
* abstract organic environments
* cosmic environments
* glitch environments
* digital corruption
* scanline / CRT aesthetics
* datamoshing aesthetics
* feedback effects
* feedback loops
* displacement
* chromatic aberration
* temporal distortion
* pixel sorting
* procedural destruction
* projection mapping
* audiovisual installations
* demoscene realtime graphics
* shader-driven environments
* music-reactive VFX

Look for actual techniques that can be reproduced in a realtime renderer rather than merely collecting aesthetic references.

### Important research question

Investigate what makes a procedural scene look:

**authored**

rather than:

**randomly generated.**

Pay particular attention to:

* composition
* hierarchy
* focal points
* repetition and variation
* controlled asymmetry
* scale
* depth
* silhouette
* negative space
* color relationships
* atmospheric layering
* foreground / midground / background
* visual rhythm
* motion hierarchy
* lighting hierarchy
* material differentiation
* deliberate placement
* controlled randomness
* environmental storytelling

The implementation should explicitly incorporate these principles.

---

# 2. RESEARCH HOW TO EVALUATE PROCEDURAL VFX

Do a second research pass specifically around **automated visual evaluation**.

AV Gen already has a creative/quality critique direction. Sonic Garden should become another place where this capability can be useful.

Investigate approaches for evaluating generated realtime scenes for properties such as:

### Composition

* Is there a clear focal point?
* Is the frame visually balanced?
* Is the composition overly symmetrical?
* Is there meaningful negative space?
* Are important elements competing for attention?
* Does the scene have foreground/midground/background separation?

### Visual hierarchy

* What does the eye notice first?
* Is there a secondary visual layer?
* Is everything moving equally?
* Is everything equally bright?
* Are important elements receiving more visual emphasis?

### Color

* Is the palette coherent?
* Are colors intentionally related?
* Is there sufficient contrast?
* Are highlights being overused?
* Does the palette reinforce the intended mood?

### Geometry

* Do objects appear intentionally placed?
* Are there obvious intersections?
* Are objects floating accidentally?
* Are repeated elements too uniform?
* Does the environment communicate a recognizable spatial structure?

### VFX quality

* Are effects layered rather than merely stacked?
* Do effects have a clear visual purpose?
* Does distortion enhance the composition?
* Are particles contributing to the scene or simply adding noise?
* Does post-processing reinforce the scene's aesthetic?

### Audio relationship

This is particularly important.

Evaluate whether:

* the visual response actually correlates with the music
* slow musical changes produce slower visual evolution
* transients create appropriately fast responses
* bass energy affects appropriate visual properties
* melodic information can create localized behavior
* rhythmic information creates rhythmic visual structure
* MIDI note density affects visual complexity
* velocity affects intensity
* pitch affects spatial/color behavior
* sustained notes behave differently from transient notes

Research whether existing VFX / audiovisual tools use similar analysis techniques.

Do not assume the existing AV Gen audio analysis model is sufficient.

---

# 3. EXPAND THE SONIC GARDEN SCENE LIBRARY

The current implementation has approximately four example scenes.

That is no longer enough.

Expand Sonic Garden into a substantially larger collection of scenes.

The scenes should be **genuinely different visual concepts**, not four scenes with different parameters.

Target approximately:

**12–20 distinctive scenes**

if the architecture allows it without creating unnecessary maintenance burden.

Quality matters more than hitting the exact number.

Each scene should have a distinct visual identity.

Possible categories include:

### Organic

* bioluminescent forest
* fungal ecosystem
* microscopic / cellular environment
* alien garden
* glowing roots
* drifting spores
* crystalline growth
* living energy ecosystem

### Cosmic

* stellar nursery
* black-hole-like environment
* gravitational lensing
* cosmic dust field
* asteroid cathedral
* impossible planetary landscape
* nebula environment

### Abstract

* liquid geometry
* flowing ribbons
* energy sculpture
* infinite tunnel
* geometric cathedral
* impossible architecture
* procedural sculpture

### Digital / Glitch

* corrupted world
* scanline environment
* fractured geometry
* data storm
* digital rain
* frame tearing
* unstable voxel-like environment
* feedback-driven world

### Atmospheric

* foggy monolith landscape
* lonely desert
* storm environment
* moonlit landscape
* underwater environment
* drifting cloudscape

These are examples, not requirements.

**Invent new concepts.**

The research phase should produce additional ideas that are outside the visual vocabulary already established by AV Gen.

---

# 4. DO NOT BUILD "RANDOM SHAPE WORLDS"

This is one of the most important requirements.

Avoid scenes that essentially consist of:

> random spheres + random boxes + random particles + random colors + audio modulation

That can demonstrate the engine but does not produce authored-looking VFX.

Instead, each scene should have a **visual thesis**.

For example:

> "A gigantic subterranean fungal organism whose breathing is driven by the bass."

or:

> "A collapsing digital cathedral in which high-frequency MIDI notes cause fragments of architecture to briefly reconstruct."

or:

> "A lonely landscape where sustained notes cause distant lights to appear while percussion causes environmental disturbances."

The scene should have:

* a deliberate composition
* a focal subject
* secondary elements
* environmental context
* depth
* lighting strategy
* color strategy
* motion strategy
* audio-reactivity strategy

Think like a VFX artist designing a shot.

---

# 5. USE ENTITY EFFECTS AGGRESSIVELY

Investigate the current Entity Effects architecture and make it a major component of Sonic Garden.

Do not limit modulation to:

* transform
* scale
* rotation
* color

Investigate using entity effects to control:

* emission
* material properties
* transparency
* dissolve
* displacement
* glow
* scale
* deformation
* rotation
* orbital motion
* turbulence
* attraction
* repulsion
* visibility
* particle emission
* local distortion
* light intensity
* light color
* shadow properties
* post-processing contribution
* animation rate

Investigate whether effects can themselves be modulated.

For example:

```text
audio
  ↓
signal
  ↓
entity effect intensity
  ↓
entity effect
  ↓
visual response
```

and:

```text
MIDI velocity
  ↓
effect intensity
  ↓
local distortion
```

and:

```text
bass envelope
  ↓
glow effect
  ↓
hero organism
```

The goal is to create **nested visual relationships**, rather than simply mapping audio directly to object transforms.

---

# 6. USE ENVIRONMENT-LEVEL EFFECTS

Investigate effects that operate on the entire environment.

Examples:

* fog density
* atmospheric color
* environmental lighting
* wind
* particle density
* environmental turbulence
* sky intensity
* ambient glow
* distant object motion
* volumetric effects
* world-space distortion
* environmental displacement

These should be independently modulatable.

For example:

```text
bass → atmospheric pressure
kick → environmental shockwave
high frequencies → particle activity
sustained notes → ambient illumination
MIDI velocity → environmental intensity
```

The important idea is that **the environment itself should feel alive**.

---

# 7. INVESTIGATE POST-PROCESSING AS A MUSICAL INSTRUMENT

Research and implement additional realtime post-processing techniques where appropriate.

Potential techniques:

* bloom modulation
* chromatic aberration
* lens distortion
* barrel/pincushion distortion
* radial distortion
* directional blur
* motion blur
* vignette
* film grain
* scanlines
* pixelation
* posterization
* color quantization
* hue rotation
* saturation modulation
* contrast modulation
* exposure modulation
* RGB channel separation
* displacement
* screen-space warping
* feedback
* temporal feedback
* frame blending
* frame echo
* glitch displacement
* digital corruption

Do not add these merely because they exist.

Each should have a visual purpose.

---

# 8. INVESTIGATE DATAMOSHING / GLITCH AESTHETICS

Research actual techniques behind:

* datamoshing
* frame displacement
* temporal feedback
* block displacement
* motion-vector-like distortion
* frame persistence
* image feedback
* RGB separation
* pixel displacement
* frame corruption
* temporal smearing

Determine which techniques can realistically be implemented in AV Gen's renderer.

Some effects may need to operate on:

```text
current frame
previous frame
previous N frames
velocity / motion information
depth
normal
motion vectors
```

Investigate whether existing AV Gen render targets/AOV infrastructure can support these techniques efficiently.

The goal is not to literally reproduce compressed-video artifacts.

The goal is to create **controlled digital instability as a visual effect**.

---

# 9. CREATE A REAL AUDIOVISUAL RESPONSE MODEL

The current behavior appears particularly well suited to slowly changing harmonic material.

That is useful, but insufficient.

Sonic Garden needs to distinguish between:

### Sustained / harmonic input

Examples:

* pads
* chords
* sustained synths
* drones

Possible response:

* slow environmental breathing
* color evolution
* large-scale movement
* atmospheric changes
* slow growth
* gradual distortion

### Melodic input

Examples:

* synth solos
* arpeggios
* lead lines

Possible response:

* localized pulses
* moving lights
* trails
* object attraction
* directional movement
* rapidly changing focal points
* pitch-dependent spatial behavior

### Bass

Possible response:

* large-scale displacement
* camera/environment pulse
* gravitational effects
* scale
* fog pressure
* ground deformation

### Drums

Especially investigate electronic drums.

Kick:

* large transient displacement
* camera/scene impact
* shockwave
* geometry pulse

Snare:

* sharp flash
* particle burst
* localized distortion

Hi-hat:

* fine particle activity
* small high-frequency motion
* granular effects

Percussion:

* small-scale environmental events

### MIDI

Investigate using:

* note-on
* note-off
* velocity
* pitch
* channel
* note duration
* note density
* polyphony
* pitch range
* velocity distribution
* note intervals
* rhythmic density

as distinct signals.

Do not collapse all MIDI information into one generic "MIDI intensity" signal.

---

# 10. ADD A LIVE SENSITIVITY / RESPONSE CONTROL

Investigate adding a user-facing sensitivity control to the Sonic Garden live panel.

The control should influence the strength of visual response without simply multiplying every value indiscriminately.

Consider a model such as:

```text
raw signal
    ↓
noise floor / threshold
    ↓
normalization
    ↓
sensitivity
    ↓
response curve
    ↓
attack/release
    ↓
modulation
```

This is preferable to simply:

```text
raw signal × sensitivity
```

because it allows the system to remain musically useful across different source levels.

Investigate whether Sonic Garden should expose controls such as:

### Sensitivity

Overall responsiveness.

### Transient sensitivity

How strongly short events trigger visual responses.

### Sustain sensitivity

How strongly sustained energy affects the scene.

### Smoothing

Controls temporal response.

### Attack

How quickly visual response appears.

### Release

How long visual response persists.

These do not necessarily all need to be exposed initially.

Research what controls would provide the most useful live-performance experience without turning the panel into an audio-engineering interface.

---

# 11. INVESTIGATE TRANSIENT DETECTION

The current system appears better at representing slowly changing signals than isolated fast events.

Research techniques for detecting:

* onset
* transient
* attack
* beat
* rhythmic pulse
* spectral flux
* energy spikes

Determine what is already available in AV Gen and what could be added.

A useful conceptual model could be:

```text
continuous energy
        +
transient energy
        +
spectral bands
        +
pitch / MIDI information
        ↓
visual modulation system
```

This would allow:

```text
slow chord → slow world evolution
single bass note → sharp visual impact
fast solo → rapid localized events
kick → large transient
hi-hat → fine-grained activity
```

This is probably one of the most important upgrades in this phase.

---

# 12. CREATE SCENE-SPECIFIC MODULATION DESIGN

Each scene should have a deliberate mapping between sound and visuals.

Do not give every scene the same generic:

```text
bass → scale
mid → color
high → rotation
```

Instead, define a modulation vocabulary appropriate to each environment.

For example:

### "Living Forest"

* bass → root pulse
* sustained notes → bioluminescent intensity
* pitch → color temperature
* MIDI velocity → creature movement
* high frequency → spores
* transient → canopy ripple

### "Digital Collapse"

* bass → geometry displacement
* kick → structural fracture
* snare → frame corruption
* MIDI velocity → glitch intensity
* note density → fragmentation
* sustained notes → reconstruction

### "Cosmic Nursery"

* bass → nebula density
* pitch → star color
* note duration → orbital motion
* transient → stellar burst
* polyphony → number of active celestial objects

These are examples.

Invent better ones.

---

# 13. BUILD VISUAL MOTION HIERARCHY

Research and implement the idea that not everything should move at the same rate.

A sophisticated scene might contain:

### Very slow

* sky
* distant environment
* large structures

### Medium

* environmental particles
* vegetation
* secondary objects

### Fast

* focal VFX
* energy
* particles
* transient effects

### Extremely fast

* flashes
* glitches
* shockwaves
* transient distortions

This is critical to making scenes feel composed rather than uniformly procedural.

---

# 14. BUILD COLOR DESIGN INTO THE SCENES

Research professional color design for VFX / motion graphics.

Each scene should have a deliberate palette.

Avoid random per-object colors.

Investigate:

* dominant color
* secondary color
* accent color
* highlight color
* background value
* contrast
* saturation hierarchy

Audio should modify the palette in controlled ways rather than randomly changing RGB values.

For example:

```text
bass → intensity
pitch → hue
velocity → saturation
transient → highlight brightness
```

rather than:

```text
audio → random color
```

---

# 15. BUILD SCENES AS COMPOSITIONS

Every scene should have some concept of:

```text
background
midground
foreground
focal subject
secondary subjects
atmosphere
post-processing
```

Where practical, encode this structure into scene data or metadata so the creative evaluation system can reason about it.

The renderer should not necessarily expose this as a rigid engine concept.

The important thing is that the scene authoring process uses these concepts.

---

# 16. ADD A SCENE QUALITY / CREATIVE EVALUATION LOOP

Investigate creating tooling that can automatically inspect a Sonic Garden scene.

The evaluator should ideally accept:

* rendered frame(s)
* short render clip
* scene JSON
* entity data
* camera information
* lighting information
* modulation mappings
* audio analysis data

Then evaluate dimensions such as:

### Visual composition

### Scene coherence

### Art direction

### Color design

### Depth

### Visual hierarchy

### VFX layering

### Audio/visual correspondence

### Motion quality

### Variety

### Originality

### "Procedural-looking" vs "authored-looking"

### Obvious technical defects

For example:

* intersections
* floating objects
* excessive repetition
* empty frame
* overcrowding
* excessive symmetry
* visual noise
* blown-out highlights
* muddy lighting
* competing focal points
* incoherent color
* effects with no apparent purpose

Investigate whether existing AV Gen creative critic infrastructure can be extended for this rather than creating an entirely separate system.

---

# 17. RESEARCH "AUTHOREDNESS"

This deserves explicit investigation.

The agent should research why procedurally generated environments often look obviously procedural.

Investigate concepts such as:

* controlled randomness
* semantic placement
* hierarchical composition
* visual storytelling
* environmental narrative
* intentional repetition
* motif development
* variation within constraints
* focal hierarchy
* asymmetry
* visual rhythm
* scale relationships

Then translate those findings into practical generation rules.

The goal is:

> procedural generation with artistic constraints

rather than:

> random generation with artistic colors.

---

# 18. PUSH BEYOND THE EXISTING AV GEN VOCABULARY

Do not assume the existing feature set represents the boundaries of what Sonic Garden can become.

If research uncovers a technique that AV Gen does not currently support, investigate whether it is worth adding.

Potential examples:

* GPU particle systems
* procedural signed-distance-field environments
* ray-marched effects
* feedback buffers
* temporal effects
* volumetric effects
* procedural texture generation
* screen-space simulation
* compute-driven effects
* GPU simulation
* motion-vector effects
* fluid approximations
* reaction-diffusion
* cellular automata
* procedural growth
* metaballs
* signed-distance-field deformation

Do not implement these simply because they sound impressive.

The criterion is:

> Does this unlock a new visual language for Sonic Garden?

If yes, investigate it.

---

# 19. PERFORMANCE REQUIREMENTS

Sonic Garden is specifically a LIVE experience.

Do not sacrifice live usability merely to make individual frames more impressive.

For every new effect investigate:

* GPU cost
* CPU cost
* memory cost
* render-target cost
* resolution dependence
* scalability
* whether it can be disabled dynamically

Where appropriate, create quality tiers.

For example:

```text
LIVE
PREVIEW
HIGH
```

or whatever fits existing AV Gen conventions.

The default live experience should remain responsive.

---

# 20. LIVE DEMO REQUIREMENT

At the end of this work, Sonic Garden should be usable as a genuine live demonstration.

Test it with multiple classes of input:

### Synth pads

### Chords

### Bass

### Monophonic leads

### Arpeggios

### Electronic drums

### Full drum loops

### Dense MIDI

### Sparse MIDI

### High velocity

### Low velocity

### Rapid note sequences

### Slow sustained notes

The goal is that these inputs should produce visibly different behavior.

A fast solo should not merely look like a slowly pulsing scene.

An electronic drum pattern should not merely increase object scale.

A sustained chord should not simply cause everything to rotate.

---

# 21. CREATE A DEMONSTRATION / TEST MATRIX

Build a repeatable test process.

For every scene, test several input types.

Record:

```text
Scene
Input type
Expected behavior
Observed behavior
Quality
Problems
```

This should help identify scenes that technically work but do not respond musically.

---

# 22. IMPORTANT DESIGN PRINCIPLE

Do not optimize for:

> "How many effects can we put on screen?"

Optimize for:

> "How convincing is the relationship between the music and the world?"

A beautiful static VFX scene that barely responds to music is incomplete.

A highly reactive scene that looks like random procedural geometry is also incomplete.

The target is:

**beautiful authored environment + sophisticated VFX + meaningful musical response.**

---

# 23. SUCCESS CRITERIA

At the end of the upgrade, I should be able to launch Sonic Garden, play a synth, change patches, play chords, play a bass line, play a lead, trigger electronic drums, and feed it different MIDI patterns and immediately encounter substantially different visual behavior.

I should not feel like:

> "I'm cycling through four visualizers."

I should feel more like:

> "I'm exploring a collection of audiovisual worlds."

The scenes should increasingly feel:

* authored
* cinematic
* atmospheric
* surprising
* musically expressive
* visually coherent
* distinct from one another
* less obviously procedural
* more like professional VFX work

And importantly:

**Do not simply make the existing four scenes more complicated.**

Use this phase to establish a much broader visual vocabulary for Sonic Garden.

---

# DELIVERABLES

Before implementation:

1. Research report covering VFX scene construction.
2. Research report covering audiovisual modulation techniques.
3. Research report covering procedural scene authoredness.
4. Research report covering realtime glitch/datamoshing/feedback techniques.
5. Research report covering automated visual/VFX quality evaluation.
6. Proposed scene catalog.
7. Proposed audio/MIDI response architecture.
8. Proposed sensitivity/transient model.
9. Technical feasibility assessment for any new renderer capabilities.

During implementation:

10. Expanded Sonic Garden scene library.
11. Entity-effect modulation.
12. Environment-effect modulation.
13. Additional post-processing effects.
14. New glitch/data-corruption/feedback vocabulary where technically appropriate.
15. Improved transient/onset responsiveness.
16. Sensitivity/response controls in the live interface.
17. Scene-specific audio mappings.
18. Creative evaluation tooling / integration.
19. Performance measurements.

Final:

20. Run the complete live test matrix.
21. Capture representative renders/clips from the new scenes.
22. Have the creative evaluator inspect the results.
23. Fix obvious composition, geometry, color, VFX, and responsiveness problems.
24. Provide a final report describing:

    * scenes created
    * new effects
    * new modulation capabilities
    * audio/MIDI improvements
    * evaluator capabilities
    * performance
    * remaining limitations
    * recommended next steps

## Final quality bar

Do not stop when the features technically work.

Iterate on the visual results.

The final question should be:

> **If someone saw this without knowing it was procedurally generated, would they reasonably think a VFX artist designed the scene?**

That is the direction we are trying to move Sonic Garden toward.
