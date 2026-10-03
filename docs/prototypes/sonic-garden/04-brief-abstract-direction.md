# Sonic Garden: stop the current art pass; abstract direction (the owner, 2026-10-02)

*A coordinator's header; the owner's words follow it, verbatim. This GOVERNS Sonic Garden's art from now on. It supersedes `03-brief-art-restart.md` (whose realistic sixteen places are rejected) and the art half of `02-brief-vfx-expansion.md`.*
- **What to make:** 8 prototypes, not 16. Each is a distinct, abstract, stylized visual language that can later grow into several scenes.
- **What still applies from 03:** the composition discipline (a still frame that works before audio, a deliberate camera, hierarchy), reinterpreted as below.
- **Engineering stays:** ADR-1060..1070, the kit, the tools and the evaluator. The restart agent's style-neutral tools (the review tool, the camera aim solver) can be reused.
- **Review media:** `~/Desktop/av-gen-review/28-sonic-abstract/`. For each prototype: a still frame, a reactive clip, and a modulation map (what each audio dimension drives, including STRUCTURAL parameters).
- **Staffing:** a fresh `sonic-art` agent (Opus max). Engine needs, such as cel or flat shading, outlines or line rendering if they're missing, go to the coordinator, who can start a `sonic-engineer`.
- **GPU:** shared with the Liminal art agent. Everything goes through `tools/gpu-lock.sh`, one job per hold.
- **Commit at every milestone;** keep `PROGRESS-abstract.md` resumable cold.

---

# Sonic Garden — STOP THE CURRENT ART PASS

## This is a direction correction, not another polish pass

**Stop work on the current 16-scene art pass immediately.**

The current direction has drifted into realistic environment generation:

* cenotes
* moors
* Kyoto gardens
* fjords
* Victorian glasshouses
* Gothic ruins
* realistic cities
* realistic storms
* realistic lava fields
* realistic astronomical environments

This is **not the direction we want.**

Do not continue developing those scenes.

Do not try to make the current scenes more realistic or more detailed.

Do not spend time improving their textures, vegetation, architecture, terrain or environmental realism.

**Discard the current 16-scene art direction and rethink Sonic Garden from first principles.**

---

# What Sonic Garden Actually Should Be

I want Sonic Garden to feel like **interactive digital art** rather than a realistic 3D environment simulator.

The visual target is much closer to:

* sophisticated music visualizers
* abstract game worlds
* experimental music videos
* generative art
* psychedelic digital art
* cel-shaded game environments
* vector graphics
* sacred geometry
* mathematical animation
* surreal spaces
* abstract architecture
* low-resolution stylized games
* graphic-design-driven 3D
* neon kinetic sculpture
* dreamlike impossible spaces

Think:

> **simple things arranged in extremely cool ways**

rather than:

> realistic things rendered convincingly.

The renderer does not need to prove that it can reproduce reality.

It needs to create **beautiful, stylized visual systems that respond dramatically to music.**

---

# Reduce the Scope

Do NOT attempt 16 scenes right now.

Create **8 genuinely different visual directions**.

The objective of this pass is exploration and quality, not quantity.

I would rather have:

**8 spectacular, distinctive abstract visual worlds**

than:

**16 mediocre pseudo-realistic environments.**

Each of the 8 should establish a reusable visual language that we can later expand into multiple scenes.

---

# Core Design Principle

The environment does not need to represent a real place.

A "place" can be:

* a geometric space
* a color field
* a tunnel
* a field of shapes
* an impossible structure
* a mathematical pattern
* an abstract garden
* a floating arrangement of objects
* a vector landscape
* a sacred-geometry construction
* a surreal room
* a procedural sculpture
* a giant kinetic organism
* an infinite grid
* a world made from lines and planes

The viewer does not need to ask:

> "What real location is this?"

They should ask:

> **"What the hell am I looking at? This looks awesome."**

That is much closer to the target.

---

# Visual Quality Target

We should explicitly prefer:

### Stylization over realism

### Composition over asset density

### Color over texture detail

### Geometry over photorealistic materials

### Lighting over texture maps

### Motion over environmental realism

### Graphic shapes over realistic objects

### Strong silhouettes over physically accurate surfaces

### Deliberate abstraction over procedural randomness

### VFX over texture clutter

### Audio responsiveness over environmental simulation

---

# Important: Simple Geometry Is Fine

Do not assume that visual sophistication requires complicated models.

A scene made from:

* spheres
* planes
* rings
* tubes
* extruded shapes
* cubes
* low-poly meshes
* lines
* particles
* procedural curves
* simple plants
* simple architectural forms

can look incredible if the composition, color, lighting, animation and VFX are excellent.

A highly detailed realistic model can look terrible if the composition is weak.

**Prefer the former.**

---

# Explore These Visual Languages

The 8 prototypes should deliberately explore substantially different visual approaches.

Do not make 8 neon environments with different objects.

Each should have a distinct visual grammar.

## Direction 1 — Sacred Geometry Garden

Explore:

* concentric circles
* mandalas
* radial symmetry
* nested polygons
* rotating rings
* golden-ratio-like structures
* recursive geometry
* crystalline forms
* geometric flowers
* impossible geometric gardens

The environment could essentially be a giant living geometric diagram.

Music could control:

* radial expansion
* rotation
* symmetry
* number of rings
* polygon sides
* color
* emission
* line thickness
* deformation
* particle release
* depth

This should feel **beautiful and hypnotic**, not like a technical geometry demo.

---

## Direction 2 — Neon Vector World

Explore a world constructed primarily from:

* glowing lines
* vector curves
* flat geometric planes
* outlined objects
* grids
* wireframe structures
* simple silhouettes
* bold color blocks

Think **graphic design turned into a navigable 3D world**.

Avoid generic Tron aesthetics.

The goal is more:

> experimental music-video vector art

than:

> cyberpunk city.

Use extremely responsive line animation, geometry deformation, color transitions and beat-synchronized construction.

---

## Direction 3 — Cel-Shaded Dream World

Explore deliberately stylized low-poly geometry with:

* strong silhouettes
* flat shading
* graphic shadows
* simplified materials
* exaggerated colors
* outlined forms where appropriate
* low-poly plants
* strange creatures
* impossible architecture

This can have the visual simplicity of a game while still looking extremely intentional.

**Do not attempt realistic textures.**

If a surface can look better as a flat graphic color field, use that.

---

## Direction 4 — Infinite Color Geometry

Create an abstract world where the primary subject is **space itself**.

Examples:

* giant colored planes
* floating slabs
* portals
* ribbons
* impossible stairways
* rotating structures
* tunnels
* nested rooms
* geometric voids

Use:

* strong color
* gradients
* volumetric atmosphere
* bloom
* depth
* distortion
* animated geometry

The environment should feel like moving through a piece of generative artwork.

---

## Direction 5 — Organic Digital Garden

This is the one place where "Garden" can become literal, but **not botanically realistic**.

Create strange stylized organisms:

* glowing flowers
* tentacles
* mushrooms
* abstract trees
* jelly-like forms
* crystalline plants
* floating seeds
* vines
* spores
* alien organisms

Keep geometry simple.

Make the visual interest come from:

* shape
* color
* motion
* scale
* repetition
* procedural growth
* lighting
* audio response

The entire garden could breathe, pulse, bend and bloom with the music.

---

## Direction 6 — Particle / VFX World

Here the environment itself can be extremely minimal.

Use a small amount of geometry combined with sophisticated VFX:

* ribbons
* trails
* particle streams
* volumetric shapes
* sparks
* energy fields
* fluid-like motion
* metaball-like forms if practical
* swirling fields
* distortion
* chromatic effects
* feedback-like post effects where available

The important distinction:

**This should look like intentional digital artwork, not "particles sprinkled over an empty level."**

The particles themselves should form compositions.

They can create:

* arches
* tunnels
* waves
* organisms
* flowers
* spirals
* galaxies
* fields
* curtains
* explosions
* flowing rivers of light

---

## Direction 7 — Impossible Architecture

Create spaces that could not exist physically.

Examples:

* rooms inside rooms
* staircases that fold into themselves
* walls becoming floors
* floating doorways
* impossible corridors
* looping structures
* giant geometric chambers
* architecture composed from simple shapes

Keep the actual geometry relatively simple.

The visual sophistication should come from:

* composition
* repetition
* scale
* perspective
* lighting
* color
* animation

This is an opportunity to explore surreal spatial design without trying to simulate a realistic building.

---

## Direction 8 — Abstract Cinematic Void

Create something extremely simple but visually powerful.

Examples:

* one enormous shape
* a few floating forms
* a giant ring
* a glowing sphere
* a field of suspended objects
* a distant geometric structure
* a colored horizon

Then make the visual experience come from:

* enormous scale
* atmosphere
* camera movement
* lighting
* color
* subtle deformation
* particles
* post-processing
* audio response

The composition must be extremely deliberate.

This should demonstrate that Sonic Garden can produce a **stunning image without needing hundreds of objects.**

---

# Every Prototype Must Be Highly Modulate-able

This is extremely important.

We are not just making pretty static scenes.

We are building **audio-reactive visual instruments**.

For each prototype, identify a large number of meaningful modulation targets.

Potential targets include:

### Geometry

* position
* rotation
* scale
* deformation
* extrusion
* thickness
* repetition
* symmetry
* radial distance
* wave amplitude
* noise amount
* subdivision/detail

### Color

* hue
* saturation
* brightness
* gradient position
* palette interpolation
* accent color

### Lighting

* intensity
* color
* radius
* direction
* flicker
* pulsing
* moving lights

### VFX

* particle count
* velocity
* emission
* size
* lifetime
* turbulence
* attraction
* trails
* distortion
* opacity
* bloom
* fog
* post-processing intensity

### Camera

* position
* target
* FOV
* orbit
* subtle shake
* depth of field where useful
* movement through the environment

### Structural parameters

This is particularly interesting.

Audio should be able to change the **structure of the artwork itself**.

For example:

* number of geometric segments
* number of rings
* symmetry order
* tunnel width
* number of organisms
* branch count
* particle streams
* repetition count
* pattern frequency
* geometry density

This is much more interesting than simply making everything glow brighter on the beat.

---

# Use Multiple Audio Dimensions

The prototypes should demonstrate the richness of the modulation system.

Do not map everything to amplitude.

Explore:

### Bass

* scale
* expansion
* geometry displacement
* large light pulses
* environmental breathing

### Midrange

* structural movement
* rotation
* color shifts
* secondary object movement

### High frequencies

* particles
* sparks
* line activity
* fine geometry
* highlights
* distortion

### Onsets

* explosions
* blooms
* geometry changes
* color flashes
* particle bursts

### Spectral centroid / brightness

* palette
* atmospheric brightness
* geometry sharpness
* VFX character

### Spectral bands

Use different frequency regions to control different parts of the artwork simultaneously.

### Rhythm / tempo

* periodic structural movement
* synchronized rotation
* repeating patterns
* animation cycles

### MIDI

If available:

* notes create objects
* velocity controls scale/intensity
* pitch controls vertical position/color
* chords create geometry
* sustained notes control environmental states
* modulation wheel controls large-scale deformation
* keyboard activity drives visual density

The goal is for Sonic Garden to feel like an **instrument**, not merely a music visualizer.

---

# Explore Graphic Rendering Techniques

Do not restrict the scenes to physically based rendering.

Where the engine supports it, explore:

* cel shading
* flat shading
* hard lighting
* toon-like lighting
* outline techniques
* emissive graphic materials
* vector-like line rendering
* simple gradients
* procedural color fields
* geometric masks
* stylized fog
* posterized lighting
* silhouette rendering
* intentionally low-resolution aesthetics
* graphic post-processing

The goal is **stylized visual quality**, not realism.

---

# "Low-Res Game" Is Acceptable — Bad 3D Is Not

There is a huge difference between:

> intentionally stylized low-resolution graphics

and:

> poor-quality 3D assets with ugly textures.

The former can be beautiful.

The latter is exactly what we are trying to avoid.

If geometry is simple, it should look **deliberately simple**.

Think:

> coherent art style

not:

> we didn't have enough polygons.

---

# VFX Should Become Part of the Art Direction

Don't treat VFX as decoration added after the environment is built.

For these prototypes, VFX can be the primary visual language.

Examples:

* a field of particles forming a geometric flower
* light ribbons creating a tunnel
* particles flowing along sacred-geometry paths
* a landscape made entirely from glowing lines
* geometry dissolving into particles
* a geometric object exploding into color and reforming
* an environment that breathes in waves
* synchronized rings expanding through space
* a field of objects responding to spectral energy

These can be far more visually interesting than realistic terrain.

---

# Quality Bar

Before accepting any prototype, ask:

### Does the first frame look cool?

Not:

> Is it technically sophisticated?

Not:

> Does it contain many features?

Not:

> Is it physically believable?

Simply:

> **Would someone see a screenshot of this and think "holy shit, that's cool"?**

If not, redesign it.

---

# The 8 prototypes should be visually interesting even when frozen

This is still a requirement from the previous pass, but reinterpret it correctly.

We do NOT want:

> realistic environment that looks good as a photograph.

We want:

> **stylized digital artwork that looks good as a still frame.**

Then animation and audio transform it further.

---

# Avoid These Directions

Do not generate:

* realistic forests
* realistic deserts
* realistic ruins
* realistic cities
* realistic temples
* realistic mountains
* realistic farms
* realistic oceans
* realistic architectural scenes
* photorealistic materials
* realistic environmental textures
* generic sci-fi environments
* generic cyberpunk environments
* generic fantasy environments

Unless a small element is deliberately used as part of a heavily stylized composition.

**Sonic Garden is not trying to be a realistic world generator.**

---

# Do Not Make Eight Variations of Neon

This is equally important.

The answer is not:

> realistic environments → neon environments.

Explore genuinely different visual languages:

* sacred geometry
* cel shading
* vector art
* abstract architecture
* organic procedural forms
* particle sculpture
* impossible space
* graphic color-field compositions

Each should feel like a different visual artist could have designed it.

---

# Prioritize Visual Experiments Over Content Volume

The goal of this pass is to discover:

> **What visual language makes AV Gen uniquely good?**

We do not yet know the answer.

That's why only 8 prototypes are needed.

Build each one far enough that we can evaluate its visual identity and modulation potential.

Do not spend time making 16 shallow scenes.

---

# Final Objective

At the end of this pass, I want 8 prototypes that make it possible to say:

> "Holy shit, this is what Sonic Garden should be."

Not:

> "These are technically impressive procedural environments."

Not:

> "These look realistic."

Not:

> "These contain lots of effects."

I want **stylized, colorful, abstract, highly reactive digital art that would look at home in a sophisticated music video, live audiovisual performance, experimental game, or generative-art installation.**

Keep the geometry relatively simple.

Make the composition excellent.

Make the color excellent.

Make the motion excellent.

Make the modulation deep.

Make the VFX beautiful.

Make the visual identity unmistakable.

**Stop trying to recreate the real world. Start designing things that could only exist inside Sonic Garden.**
