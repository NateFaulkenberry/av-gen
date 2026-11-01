> Owner brief, verbatim, 2026-11-01. This REPLACES the temporal-canyon art direction of `00-brief.md`
> for the CHRONOS scene. Working rules and stops: `11-devourer-coordinator-notes.md`; where they differ,
> the owner's words here win on art direction.

# CHRONOS — New Artistic Direction & Visual POC
## The Devourer of Souls

**Project:** AV Gen  
**Scene:** CHRONOS  
**Task:** Complete artistic direction pivot and visual proof of concept  
**Priority:** Visual quality and feasibility over feature expansion  
**Primary reference:** Goya's *Saturn Devouring His Son*  
**Technical foundation:** Existing working slit-scan implementation and current AV Gen renderer

---

# 1. OBJECTIVE

We are pivoting the CHRONOS scene away from its original temporal canyon concept.

The new direction is a surreal, expressionist cosmic-horror scene centered on an enormous, horrifying humanoid entity representing Chronos, the personification of time.

Chronos consumes human souls through an endless procession of horizontally suspended human figures.

As the figures approach his mouth, their bodies are progressively transformed using the existing slit-scan and temporal imaging technology. Their visual histories are stretched, fragmented, displaced, and eventually consumed by the creature.

The central artistic metaphor:

**Time is an insatiable entity that consumes every living thing.**

The visual experience should communicate this immediately, without requiring explanation.

We are not recreating Goya's painting. We are taking inspiration from its disturbing expression, anatomy, psychological horror, and treatment of the human figure, then translating those qualities into an original real-time 3D audiovisual scene.

## Primary goals

1. Create a genuinely disturbing and memorable Chronos creature.
2. Make his enormous face the dominant visual element.
3. Establish a recognizable procession of horizontal human figures.
4. Make the slit-scan transformation of the figures visually compelling.
5. Develop a distinctive devouring effect centered around Chronos's mouth.
6. Establish an atmospheric, chaotic background that supports the composition.
7. Demonstrate that the concept is achievable within AV Gen's existing rendering and performance constraints.

This is an art-direction and visual-feasibility exercise.

Do not expand the environment, introduce unnecessary renderer features, or undertake broad architectural changes.

---

# 2. ART DIRECTION

## 2.1 Overall Visual Identity

The scene should feel like a moving expressionist horror painting brought into an experimental audiovisual 3D environment.

Visual influences:

- Francisco Goya's *Saturn Devouring His Son*.
- Classical expressionist horror painting.
- Dark surrealism.
- Organic cosmic horror.
- Experimental slit-scan video art.
- Analog video degradation and digital temporal fragmentation.

The result should feel painterly, grotesque, ancient, and otherworldly.

Avoid generic fantasy monster aesthetics.

Specifically avoid:

- Horned demons.
- Conventional fantasy armor.
- Generic video-game bosses.
- Mechanical robots.
- Generic alien creatures.
- Overly clean science-fiction materials.
- Excessive decorative geometry.
- Generic AI-generated photorealistic environments.

The creature should feel like an ancient entity that has existed for an incomprehensible amount of time.

The imagery should be disturbing through expression, anatomy, scale, and movement rather than graphic violence.

## 2.2 Color Palette

Establish a distinctive palette based on the emotional qualities of Goya's painting.

Primary colors:

- Near-black and extremely deep brown.
- Dark crimson.
- Dried blood red.
- Muted burgundy.
- Desaturated flesh tones.
- Bone-like pale highlights.
- Warm amber and occasional dirty gold.

Secondary accents:

- Muted violet.
- Occasional deep magenta.
- Very restrained cold highlights.

Avoid excessive cyan, teal, bright blue, or conventional neon cyberpunk palettes.

The overall image should be dark, but not so dark that the creature, figures, and temporal effects become difficult to distinguish.

Use strong chiaroscuro lighting.

The face should have selectively illuminated anatomical features, with significant areas disappearing into shadow.

---

# 3. CHRONOS — THE CREATURE

**This is the most important asset and the primary artistic priority.**

Chronos should occupy the dominant portion of the composition.

His face should be enormous, recognizable, and deeply unsettling.

We should primarily see his head and face, with only enough neck and upper torso to establish that he is an enormous humanoid entity.

We do not need a complete full-body creature.

## 3.1 Facial Design

Use Goya's Saturn as an emotional and expressive reference, not a modeling template.

Explore the following characteristics:

- Extremely gaunt, aged humanoid face.
- Deeply recessed eyes.
- Wide, almost frenzied eyes with an unsettling expression.
- Pronounced brow and cheekbones.
- Hollow cheeks.
- Wrinkled, weathered skin.
- Uneven, elongated facial proportions.
- Long, tangled, unkempt hair.
- An enormous mouth with an unnaturally extended jaw.
- Irregular, aged teeth.
- Deep anatomical shadows around the eyes, nose, and mouth.

The expression should communicate a combination of:

- Insatiable hunger.
- Madness.
- Desperation.
- Ancient suffering.
- Complete indifference to the lives being consumed.

Avoid making him look like an angry generic monster.

The expression should be closer to an ancient being that has become psychologically consumed by an eternal appetite.

## 3.2 Mouth and Jaw

The mouth is the most important functional feature of the creature.

It needs to be large enough to establish a convincing visual destination for the incoming procession.

Requirements:

- Clearly recognizable upper and lower jaw.
- Distinct lip geometry.
- Irregular teeth.
- Visible interior darkness.
- Sufficient opening to accommodate the visual consumption effect.
- Ability to animate the jaw and mouth aperture.

The mouth should not look like a conventional circular portal attached to a face.

It needs to remain recognizably anatomical while the interior transitions into something impossible.

The eventual visual effect should make it appear as though the creature is swallowing the temporal existence of the incoming figures.

## 3.3 Creature Modeling Approach

Prioritize a convincing face over unnecessary anatomical completeness.

Investigate efficient approaches for achieving the required quality:

- Sculpted or artist-created base mesh.
- Blender-created creature asset.
- Appropriate use of normal maps and roughness variation.
- Skinned facial geometry where practical.
- Simple jaw rig or dedicated mouth deformation.
- Efficient eye and facial animation.

Use Blender headless or the existing Blender MCP workflow where appropriate.

Free assets are acceptable as starting points, but do not settle for an obviously generic monster model.

If the existing asset pipeline supports it, construct a deliberately exaggerated expressionist face rather than relying on a realistic human head with a few superficial modifications.

The face needs to hold up at close camera distances.

## 3.4 Animation

Keep animation restrained initially.

The creature should not constantly thrash around.

Explore:

- Subtle eye movement.
- Occasional eye tracking toward incoming souls.
- Slow jaw opening and closing.
- Slight facial tension.
- Subtle head movement.
- Small changes in expression during consumption.

The creature should feel alive even when relatively stationary.

Major jaw movement can later be connected to the drum stem.

Do not implement separate kick, snare, or hi-hat classifiers. We currently only have a drum stem available.

For this POC, use the existing drum signal and general modulation infrastructure where useful.

---

# 4. THE SOUL ASSEMBLY LINE

The second major visual element is an endless procession of human figures suspended horizontally.

They are moving toward Chronos's mouth as though carried by an invisible conveyor system.

There should be no need to construct an elaborate mechanical conveyor.

The figures themselves and their movement should communicate the assembly-line metaphor.

## 4.1 Human Figure Design

Use recognizable human anatomy.

The figures should resemble adult human beings rather than mannequins, robots, or abstract geometric characters.

Visual characteristics:

- Pale, desaturated skin.
- Neutral or minimal clothing.
- Clearly identifiable heads, torsos, arms, and legs.
- Natural human proportions.
- Slight variation in anatomy and poses.
- Subtle differences between individual figures.

The figures should be suspended horizontally, generally lying flat with their bodies aligned toward the creature.

Some variation in arm and leg positioning is welcome.

Avoid perfectly identical copies appearing in a repetitive line.

Create a small collection of distinct base poses or figure variations.

For the POC, prioritize a relatively small number of visually convincing figures over hundreds of low-quality figures.

Use instancing or shared geometry where practical.

## 4.2 Movement

The figures travel through space toward Chronos.

Their movement should feel continuous, inevitable, and mechanical without looking like a literal industrial conveyor.

Explore a procession that curves or bends slightly through the image toward the mouth.

The important thing is that the viewer can immediately understand the direction of movement.

Establish a consistent progression:

1. Figure appears in the distance.
2. Figure approaches Chronos.
3. Figure enters the temporal transformation zone.
4. Figure begins stretching and fragmenting.
5. Figure is pulled toward the mouth.
6. Figure disappears into the mouth.

The procession should continue while individual figures are consumed.

Do not let the entire scene depend on one animated figure.

---

# 5. THE DEVOURING EFFECT

This is the central technical and artistic demonstration.

The existing slit-scan implementation is already working.

Reuse it.

Do not rebuild the temporal imaging subsystem without identifying a specific limitation that prevents the desired result.

The objective is to apply temporal imaging selectively to the souls and the mouth.

## 5.1 Soul Transformation

As a figure approaches Chronos, it should progressively lose its conventional 3D appearance and become a temporal image.

Develop a transition with the following stages:

**Stage A — Intact**

The figure is fully recognizable and three-dimensional.

**Stage B — Temporal Echo**

The figure begins leaving multiple historical positions behind.

Subtle horizontal bands and displaced image fragments become visible.

**Stage C — Temporal Stretching**

The body begins elongating along the direction of travel.

Previous positions are accumulated and spatially recombined using slit-scan sampling.

The figure appears to exist across multiple moments simultaneously.

**Stage D — Fragmentation**

The figure becomes increasingly fragmented.

Different body regions display different points in time.

Limbs, torso, and head may appear displaced relative to one another.

**Stage E — Consumption**

The remaining temporal image is pulled toward the mouth.

The figure compresses into an increasingly narrow ribbon or stream of visual information before disappearing.

The transition must be gradual and spatially controlled.

Avoid simply switching the slit-scan shader on when a figure reaches a particular distance.

## 5.2 Localized Temporal Processing

Investigate the most practical way to apply the existing temporal imaging effect to individual figures or groups of figures.

Possible approaches:

- Per-object temporal masks.
- Isolated object rendering.
- Depth-aware temporal compositing.
- Spatial masks defining a consumption zone.
- Per-figure temporal parameters based on position.
- Shared temporal history with localized effect masks.

Do not assume we need independent full-resolution history buffers for every soul.

Prefer the least expensive approach that produces the desired visual result.

The entire background should not become distorted simply because one soul enters the mouth.

The transformation needs to remain visually localized.

## 5.3 Mouth Consumption

The mouth should become a second temporal imaging focal point.

Explore the following treatment:

- Deep interior darkness.
- Distorted horizontal temporal bands.
- Recursive sampling of previous imagery.
- Incoming figures appearing to compress into the mouth.
- Distorted image fragments receding into the throat.
- Controlled temporal feedback.
- Localized red and amber illumination.

The mouth should appear to consume the image rather than simply act as a hole through which the figures pass.

Experiment with combining the existing slit-scan effect with temporal feedback.

The interior could display progressively older versions of incoming figures, suggesting that Chronos consumes their entire history.

Avoid making the mouth look like an ordinary portal or glowing energy sphere.

It should remain part of the creature's anatomy.

---

# 6. BACKGROUND — AMBIENT CHAOS

We are deliberately reducing environmental complexity.

Do not build:

- Vast architectural environments.
- Detailed underground chambers.
- Elaborate geological landscapes.
- Large procedural terrain systems.
- Extensive scenery.
- Complex environmental narratives.
- Large numbers of unrelated props.

These would consume resources without materially improving the primary visual experience.

Instead, the background should be an atmospheric field of controlled visual chaos.

It exists to establish depth, movement, texture, and atmosphere around the creature.

## 6.1 Background Treatment

Explore a combination of:

- Deep black and burgundy atmospheric gradients.
- Subtle volumetric-looking haze.
- Slowly drifting particles.
- Abstract shadow movement.
- Fragmented temporal imagery.
- Distorted horizontal bands.
- Occasional red and amber light pulses.
- Subtle noise and analog-video texture.
- Indistinct organic forms emerging and disappearing into darkness.

The background should have depth without requiring a large environment.

It should feel as though Chronos exists inside an unstable temporal space.

## 6.2 Visual Hierarchy

Maintain a clear visual hierarchy at all times:

1. Chronos's face.
2. The open mouth.
3. The soul procession.
4. The localized temporal transformations.
5. The atmospheric background.

The background must never compete with the creature.

Keep its brightness, detail, and contrast controlled.

The scene should remain visually legible even during intense musical passages.

---

# 7. COMPOSITION AND CAMERA

The camera should primarily frame Chronos's enormous face and the approaching procession.

Do not prioritize elaborate environmental flythroughs.

The POC should establish a strong composition that allows the viewer to understand the entire visual concept.

Preferred initial arrangement:

- Chronos's face occupies a substantial portion of the frame.
- His mouth is clearly visible and serves as the destination.
- The soul procession enters from one or more sides of the composition.
- Figures travel toward the mouth in a recognizable direction.
- Background chaos creates depth without obscuring the subject.

Explore both a frontal three-quarter view and a more direct frontal view.

Choose whichever makes the creature most expressive and the consumption mechanism easiest to understand.

Camera movement should be relatively restrained.

Small movements, subtle perspective changes, and occasional closer approaches are sufficient for the initial POC.

Do not spend time implementing a complicated multi-act camera journey.

First make a single composition look extraordinary.

---

# 8. AUDIO — INITIAL DRUM STEM ONLY

The current audio material provides a drum stem.

Do not build a dependency on separate kick, snare, hi-hat, bass, or synth stems.

Use the existing drum stem and available audio analysis/modulation infrastructure.

Possible mappings:

| Drum signal | Visual behavior |
|---|---|
| Overall drum energy | General intensity of the scene |
| Transient energy | Jaw movement or sudden temporal fragmentation |
| Sustained energy | Soul procession speed |
| Strong drum events | Increased consumption intensity |
| Quiet sections | Reduced distortion and subtle creature movement |

These are initial artistic possibilities, not mandatory mappings.

The first priority is visual quality.

The scene must work convincingly even with audio modulation disabled.

Do not spend time building sophisticated audio-reactive behavior until the underlying creature, figures, and devouring effect look good.

Future work can introduce more detailed mappings when additional stems become available.

---

# 9. PERFORMANCE AND IMPLEMENTATION CONSTRAINTS

This is a deliberately constrained visual POC.

Prioritize:

- The creature.
- A small number of high-quality human figures.
- The temporal transformation.
- The mouth effect.
- Background atmosphere.

Avoid spending resources on extensive environmental geometry.

Reuse existing AV Gen systems wherever possible.

Use shared meshes, instancing, efficient animation, and existing renderer infrastructure.

Maintain the initial live target of 1080p/30 FPS.

Profile the scene as it develops.

Do not introduce a significant renderer refactor to achieve the artistic direction.

If a particular effect proves too expensive, produce a documented lower-cost alternative rather than abandoning the artistic concept.

---

# 10. IMPLEMENTATION WORKFLOW

Before implementation:

1. Inspect the current CHRONOS scene and existing working slit-scan implementation.
2. Locate and inspect the user's reference images in `~/Desktop/chronos`.
3. Review all reference images and identify their useful artistic qualities.
4. Separate creature-design references from temporal-effect references and general atmosphere references.
5. Inspect existing character rendering, skinned mesh, instancing, and temporal compositing capabilities.
6. Identify the most efficient route to producing the desired creature and soul figures.
7. Produce a short implementation plan with any significant technical risks.

Treat the reference images as artistic guidance, not assets that must be reproduced literally.

Do not assume that all reference images are equally important.

Prioritize the original Goya-inspired creature expression and the visual treatment of the incoming human figures.

Use Blender headless/MCP and free resources where appropriate.

If creating or adapting assets, prioritize high-quality creature anatomy and recognizable human figures.

## Implementation order

**Phase 1 — Creature art pass**

Create the Chronos face and establish its appearance, expression, materials, and lighting.

Do not proceed with elaborate scene construction until the creature is visually convincing.

**Phase 2 — Soul procession**

Introduce a small collection of horizontally suspended human figures.

Establish the spatial relationship between the procession and the creature's mouth.

**Phase 3 — Consumption**

Implement the complete transformation of one figure as it approaches and enters the mouth.

Reuse the existing slit-scan system.

**Phase 4 — Multiple figures and background**

Extend the procession and add atmospheric background chaos.

**Phase 5 — Basic drum integration**

Introduce a small number of meaningful mappings using the existing drum stem.

**Phase 6 — Visual polish and profiling**

Tune materials, lighting, composition, temporal effects, and performance.

---

# 11. VISUAL VALIDATION AND ACCEPTANCE CRITERIA

The POC must be evaluated primarily on visual quality.

Capture screenshots and short video demonstrations of the following:

### Test A — Chronos portrait

Show the creature without the soul procession.

Questions:
- Is the face genuinely unsettling?
- Does it feel ancient and inhuman?
- Is the expression distinctive?
- Does the mouth look anatomically convincing?
- Does the creature have a recognizable visual identity?

### Test B — Soul procession

Show the creature with multiple approaching figures.

Questions:
- Are the figures immediately recognizable as humans?
- Is their horizontal orientation clear?
- Is their direction of travel obvious?
- Does the assembly-line metaphor communicate immediately?
- Is the creature still the dominant visual element?

### Test C — Single soul consumption

Show one figure traveling through the complete transformation.

Questions:
- Does the slit-scan effect create a recognizable temporal transformation?
- Does the figure remain identifiable during the early stages?
- Does the transformation become increasingly disturbing?
- Does the mouth convincingly consume the resulting temporal image?
- Is the transition visually coherent?

### Test D — Multiple simultaneous souls

Show multiple figures at different stages of consumption.

Questions:
- Does the scene communicate an endless process?
- Are different stages visually distinguishable?
- Does the composition remain legible?
- Is there excessive repetition?

### Test E — Background atmosphere

Show the scene with the background effects enabled and disabled.

Questions:
- Does the background add depth and atmosphere?
- Does it compete with the creature?
- Does it create visual noise that obscures the main action?

### Test F — Performance

Measure actual GPU and CPU costs at 1080p.

Report any significant bottlenecks.

---

# 12. SCOPE CONTROL

The following are explicitly out of scope for this art pass:

- Extensive architectural environments.
- Detailed terrain generation.
- Large environmental storytelling sequences.
- Complex camera journeys.
- Full-body Chronos modeling unless necessary for the composition.
- Hundreds of individually animated souls.
- New audio stem classification.
- Separate kick/snare/hi-hat detection.
- Major renderer architectural changes.
- Full temporal geometry reconstruction.

Do not expand scope without first demonstrating that the additional work materially improves the visual result.

---

# FINAL ARTISTIC OBJECTIVE

Create an enormous, horrifying, expressionist Chronos face emerging from an unstable dark environment.

A procession of pale human figures travels horizontally toward his enormous open mouth.

As the figures approach, they begin losing their conventional physical form and transform into temporal ribbons through the existing slit-scan system.

The mouth consumes those ribbons through an increasingly distorted temporal aperture.

The background provides controlled atmospheric chaos, but the creature, the souls, and the act of devouring remain the unmistakable center of attention.

**The desired result should look like an experimental horror painting that has come alive, with time itself serving as the mechanism of consumption.**

Make the creature unforgettable. Make the procession immediately understandable. Make the devouring visually extraordinary.

Everything else is secondary.
