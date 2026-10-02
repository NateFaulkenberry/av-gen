# Sonic Garden: full art pass restart (the owner, 2026-10-02)

*A coordinator's header; the owner's words follow it, verbatim. This GOVERNS the Sonic Garden scene art from now on. It supersedes the art half of `02-brief-vfx-expansion.md`, but none of its engineering.*
- **Base:** branch `proto/sonic-garden` at the head after the previous art agent's stop commit. Not merged to main yet.
- **Engineering is done and stays:**
  - ADR-1060..1070: the live drum detector, the response model, signal triggers, the scene switcher, publish, post as instruments, temporal effects, `voronoiEdge`, the live sky;
  - the kit and tools in `tools/sonic_vfx/`, and the evaluator `tools/sonic_vfx_critic.py`.
  
  See `VFX-ARCHITECTURE.md` and `PROGRESS-vfx-eng.md`.
- **The previous art pass is REJECTED.** Its 16 scenes (`examples/sonic-garden/` set-list, `tools/sonic_vfx/scenes/`) are disposable first drafts. Keep them in git history and recompose. `PROGRESS-vfx-art.md` and `SCENE-CATALOG.md` are reference for verified engine facts only.
- **Staffing:** a fresh `sonic-art` agent (Opus max). Engineering needs go to the coordinator; a `sonic-engineer` can be started for them.
- **Review media:** `~/Desktop/av-gen-review/27-sonic-art-restart/`.
  - For every scene: a SILENT still (all modulation off), then the reactive clip, plus a before/after against `25-sonic-vfx/`.
  - Put the bad ones in honestly.
- **GPU:** shared with the Liminal art agent. Everything goes through `tools/gpu-lock.sh`, one job per hold, and previews stay small.
- **Commit at every milestone,** keep `PROGRESS-art-restart.md` resumable cold, and push the branch after milestones. Never commit `assets/`.

---

# Sonic Garden — Full Art Pass Restart / Visual Quality Rebuild

## Context

The latest Sonic Garden art pass expanded the project from 4 audio-reactive scenes to 16.

This pass is being rejected.

The attached/rendered examples demonstrate a systemic visual-quality problem: the scenes technically contain interesting objects, lighting, particles, audio reactivity, post-processing and procedural elements, but they do **not** consistently look like compelling environments.

They frequently read as:

> empty 3D space + one interesting object + some glow/particles/effects

rather than:

> a deliberately composed, immersive environment that happens to be audio-reactive.

Do not defend the current results and do not attempt to cosmetically polish them into acceptability.

**Treat the existing 16-scene art pass as a failed first draft and restart the visual design of the scenes.**

The underlying AV Gen rendering/modulation technology is not the problem being solved here. The problem is **art direction, environment composition, visual hierarchy, spatial storytelling, and aesthetic coherence.**

---

# Primary Goal

Rebuild the Sonic Garden 16-scene art pass so that the scenes feel like **distinct, authored worlds**, not procedural visualizers.

The quality target is:

**A compelling cinematic still frame before audio reactivity is enabled.**

Audio/MIDI reactivity should subsequently make those environments come alive.

It must not be necessary for audio reactivity to make an otherwise weak composition interesting.

If a scene looks bad with all modulation disabled, the scene is not finished.

---

# VERY IMPORTANT: Do Not Optimize for Feature Count

Do not interpret this task as:

* add more entities
* add more particles
* add more effects
* add more emissive materials
* add more geometry
* add more post-processing
* add more procedural noise

Those are not measures of visual quality.

A scene with 20 carefully composed elements is preferable to a scene with 500 unrelated generated elements.

The goal is **intentional visual composition**, not density.

---

# First Step: Analyze the Failed Pass

Before modifying scenes, inspect the current 16 scenes and identify why the existing compositions fail.

For each scene, explicitly evaluate:

* What is the visual concept?
* What is the focal point?
* What is the camera trying to communicate?
* What establishes foreground depth?
* What establishes midground structure?
* What establishes background depth?
* What communicates scale?
* What secondary elements support the hero?
* What creates environmental identity?
* What creates visual rhythm?
* What makes this feel like a place rather than an effect?
* What is currently generic/procedural-looking?
* What can be discarded entirely?

Do not simply preserve existing scene layouts because they already exist.

**Existing scene geometry is disposable.**

Preserve underlying systems and useful assets where appropriate, but redesign the actual visual composition from the ground up.

---

# The New Scene Design Model

Every Sonic Garden scene should be designed as an environment with a hierarchy:

## 1. Environmental premise

Define a specific place.

Examples:

* flooded subterranean garden
* impossible crystalline canyon
* abandoned observatory overtaken by bioluminescent growth
* enormous fungal forest
* submerged ruins
* alien tidal basin
* floating garden above a cloud layer
* ancient stone sanctuary
* fog-filled marsh
* surreal desert ecosystem

Do NOT make the premise:

> "a dark environment with glowing objects."

The viewer should be able to describe what kind of place they are looking at.

---

## 2. Emotional / aesthetic intent

Each scene should have a clear emotional identity.

Examples:

* wonder
* isolation
* serenity
* unease
* mystery
* scale
* dreamlike beauty
* alien abundance
* melancholy
* energy
* transcendence

The visual decisions should support that emotional intent.

---

## 3. Hero element

Each scene should have one dominant visual idea.

The hero should have:

* intentional scale
* intentional position
* intentional silhouette
* supporting environment
* appropriate lighting
* meaningful relationship to the camera

Do not simply place a giant object in the middle of an empty environment.

The hero needs to **belong to the world**.

---

## 4. Supporting environment

Build the environment around the hero.

Use secondary and tertiary elements such as:

* terrain
* rocks
* vegetation
* architectural fragments
* structures
* smaller organisms
* pools
* paths
* ruins
* cliffs
* roots
* debris
* distant environmental forms

These elements should establish context, depth and scale.

They should not compete with the hero.

---

# Composition Requirements

Every scene should deliberately establish:

### Foreground

Something close to camera that helps place the viewer inside the environment.

Possible examples:

* vegetation
* rocks
* branches
* architectural fragments
* terrain
* silhouettes
* fog
* shallow water
* environmental framing elements

Do not force foreground objects into every shot mechanically, but most scenes should have some form of foreground depth.

### Midground

The primary environmental area.

This is where the viewer should discover supporting structures, secondary objects, terrain and environmental detail.

### Background

The world should continue beyond the hero.

Use:

* distant terrain
* cliffs
* architecture
* trees
* atmospheric silhouettes
* distant lights
* giant environmental forms
* fog layers
* sky structures

Avoid:

> hero floating against an empty gradient.

---

# Camera Direction

Camera placement must be part of the art direction.

Do not simply position the camera so that all entities are visible.

Instead, compose the camera around:

* focal point
* silhouette
* depth
* scale
* negative space
* foreground framing
* leading lines
* visual balance
* environmental storytelling

The final frame should look intentionally composed even when frozen.

Consider cinematic camera language:

* low-angle scale shots
* environmental wide shots
* partially occluded views
* asymmetric compositions
* foreground framing
* deep perspective
* controlled negative space
* strong silhouettes

Avoid the generic:

> camera centered on the main object.

---

# Scale

Every scene needs convincing scale cues.

The viewer should be able to understand relative size through:

* terrain
* architecture
* vegetation
* characters/creatures
* repeated objects
* distant structures
* environmental layers

If something is enormous, give the viewer something small enough to establish that it is enormous.

If something is intimate, compose accordingly.

---

# Lighting

Lighting must establish hierarchy.

Do not use "everything glows" as the lighting strategy.

Use deliberate relationships between:

* environmental illumination
* key light
* fill
* rim/accent light
* emissive sources
* reflected light
* atmospheric light
* shadow
* contact shadow

Emissive objects should affect the surrounding environment where the renderer supports it.

A glowing object sitting in an otherwise unaffected scene should generally be treated as a warning sign.

---

# Materials

Avoid scenes where every interesting object becomes:

> dark geometry + emissive cyan/magenta material.

Materials should provide visual differentiation.

Consider combinations of:

* wet surfaces
* translucent materials
* rough stone
* crystalline surfaces
* organic surfaces
* metallic remnants
* glass-like elements
* matte terrain
* reflective water
* soft biological forms

Materials should reinforce the identity of the environment.

---

# Color Direction

Do not let the entire project collapse into the same generic neon palette.

Sonic Garden can absolutely use saturated bioluminescence, but each scene needs a controlled palette.

Some scenes may be:

* predominantly cool with a single warm accent
* warm and dusty with cool bioluminescence
* monochromatic with one contrasting color
* pale and ethereal
* deep aquatic
* earthy with isolated neon
* crystalline and high-key

Color should support composition rather than simply maximize saturation.

---

# Environmental Depth

The scene should contain multiple scales of visual information:

### Large scale

Terrain, cliffs, architecture, giant organisms, major structures.

### Medium scale

Trees, rocks, structures, vegetation, secondary organisms.

### Small scale

Plants, particles, surface detail, spores, small lights, debris.

This creates a believable environment without requiring enormous geometry counts.

---

# Audio Reactivity Comes AFTER the Environment

First build the scene.

Then disable all audio/MIDI modulation and render it.

Ask:

> "Would this still be an interesting cinematic environment if the soundtrack were completely silent?"

If not:

**Do not proceed. Redesign the scene.**

Only after the silent composition passes should audio reactivity be applied.

Audio should then influence things such as:

* emissive intensity
* lighting
* vegetation motion
* water
* spores
* atmospheric movement
* environmental deformation
* entity effects
* subtle camera/environment motion
* post-processing
* secondary particle systems

The audio response should feel like the environment is **alive**, not like a visualizer is being rendered on top of it.

---

# Scene Diversity

The 16 scenes should not be 16 variations of:

> dark environment + glowing object.

They need genuinely different environmental compositions.

Across the 16 scenes, deliberately vary:

* environment type
* dominant material
* camera language
* scale
* color palette
* density
* lighting strategy
* hero type
* environmental structure
* emotional tone

Some scenes should be dense.

Some should be sparse.

Some should emphasize architecture.

Some should emphasize organic environments.

Some should emphasize enormous scale.

Some should be intimate.

Some should use water.

Some should use atmospheric depth.

Some should use strong silhouettes.

Some should have almost no particles.

**Do not make every scene look like the same procedural generator with different parameters.**

---

# Reference-Driven Art Direction

Before implementing each scene, establish a visual target.

Where practical, research visual references for:

1. environment type
2. composition
3. lighting
4. materials/color

Do not copy a reference literally.

Use references to establish a visual vocabulary and composition quality that text alone may not communicate.

For each scene, record a short internal art-direction description such as:

> "Wide cinematic view into a flooded cavern. Dark rock walls frame the image from both sides. A shallow reflective pool occupies the midground. A huge translucent organism emerges from the water in the distance. Tiny warm lights establish scale around its base. Cool atmospheric haze separates the foreground rock from the distant cavern wall."

That is much more useful than:

> "Create a beautiful underwater alien scene with glowing organisms."

---

# Art Pass Workflow

Use this workflow for every scene:

### Pass 1 — Concept

Define:

* place
* emotional intent
* hero
* supporting environment
* palette
* visual reference direction

### Pass 2 — Composition Blockout

Create only:

* terrain/environment
* hero
* major supporting forms
* camera
* major lighting

Ignore particles and fancy effects.

Render.

### Pass 3 — Composition Review

Evaluate:

* focal hierarchy
* silhouette
* foreground/midground/background
* scale
* depth
* negative space
* camera composition

Fix anything weak.

### Pass 4 — Environment Detail

Add:

* secondary elements
* tertiary elements
* environmental storytelling
* material variation
* atmospheric depth
* scale cues

### Pass 5 — Lighting / Material

Establish:

* lighting hierarchy
* reflections
* emissive relationships
* shadows
* atmospheric separation
* controlled palette

### Pass 6 — Silent Beauty Test

Disable audio modulation.

Capture a still.

The scene must look good now.

### Pass 7 — Audio Reactive Pass

Only now add:

* beat response
* frequency response
* MIDI response
* entity effects
* environmental movement
* particles
* post-processing modulation

### Pass 8 — Final Review

Evaluate both:

**Silent cinematic frame**

and

**Audio-reactive performance**

The audio version must improve the environment rather than obscure its composition.

---

# Quality Gate

Do not consider a scene complete because it technically satisfies its specification.

A scene passes only if it answers "yes" to most of these questions:

* Does this look like a place?
* Is there a clear visual concept?
* Is there a clear focal point?
* Is the focal point integrated into the environment?
* Does the camera feel intentionally composed?
* Is there meaningful foreground/midground/background depth?
* Can I understand the scale?
* Are there secondary and tertiary forms?
* Is the lighting helping the composition?
* Are materials visually differentiated?
* Is the color palette intentional?
* Does the environment have atmosphere?
* Does the scene have its own identity?
* Does it still look compelling with audio disabled?
* Does audio reactivity enhance rather than rescue the composition?

If the answer is no, **redesign rather than embellish.**

---

# Critical Anti-Patterns

Explicitly avoid these:

### "Object on a plane"

A large hero object sitting on an otherwise empty flat surface.

### "Glowing object in fog"

Fog + emissive object is not automatically an environment.

### "Particle wallpaper"

Particles distributed throughout empty space to make a scene appear detailed.

### "Neon soup"

Everything is emissive, saturated and glowing.

### "Effect as environment"

A shader/post-processing effect should not substitute for actual environmental composition.

### "Centered hero"

Every scene does not need a perfectly centered hero object.

### "Procedural sameness"

Changing the hero while retaining the same terrain, camera, palette and lighting strategy.

### "More stuff = more detail"

Density is not visual sophistication.

---

# What Can Be Reused

Reuse existing AV Gen systems and assets wherever they are useful.

Do NOT rewrite working renderer/modulation infrastructure merely because the art pass failed.

However, scene geometry, entity placement, lighting setups, camera positions, effect configurations and procedural layouts may be completely redesigned.

The objective is not backwards compatibility with the failed compositions.

The objective is a substantially better visual result using the existing technology.

---

# Deliverables

At the end of the pass provide:

1. The rebuilt 16 scenes.
2. A short art-direction description for each scene.
3. The primary visual concept/hero for each.
4. The intended audio-reactive behavior for each.
5. Before/after render comparisons where practical.
6. A short summary of the major systemic changes made to scene composition.
7. Identification of any scenes that still fail the visual-quality bar rather than pretending they are finished.

Do not inflate the result with technical language.

The final question is simple:

> **Do these 16 scenes look like places I would actually want to explore or watch, or do they still look like generated visualizer presets?**

The target is the former.

**Start the art pass over. Do not polish the failed compositions. Recompose them.**
