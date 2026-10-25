# Cursed Slammers: The Apparition — the owner's POC brief (verbatim, 2026-10-25)

The owner's covering note: "new POC I want to try, do some deep research on this and have it ready to hand off to an agent when a slot frees up"

Coordinator notes:
- Prep only for now: deep research and an implementation-ready handoff; build when an agent slot frees (owner's 2-agent cap).
- Mix kick/snare/hat detection is retired on main (daf93949): use response.low/onset, stem/role signals; drums-role classifier only on a drum source.
- 'Intended lower-end live hardware': the established baseline is the M2 Max on AC with the LG at 60 Hz, 1080p30.
- Never commit commercial or downloaded third-party assets unless the licence allows; model downloads need owner approval.

---

## Brief

# AV Gen POC: Cursed Slammers — The Apparition

## Mission

Build a proof of concept for a new full-screen AV Gen scene built around the visual language of 1990s lenticular prints, holographic collectibles, optical depth illusions, and uncanny animal imagery.

The scene's first subject is an enormous, King Kong-inspired ape emerging from darkness. It grips vertical metal bars with large hands, peers toward the camera, and appears to possess physical depth beyond the screen. After remaining visible for a while, the creature recedes and dissolves back into the surrounding darkness.

The intended effect is that the audience is looking at an image that is somehow alive: a layered, glossy, optically strange image whose parts seem to move independently in depth as the camera shifts.

**This is a visual proof of concept, not a complete scene collection.** Prove that the core illusion looks compelling before adding other animals, multiple encounters, or elaborate environments.

## 1. Research and planning before implementation

Before writing code:

1. Inspect the current AV Gen repository, scene architecture, live-scene guide, relevant existing scenes, asset pipeline, and available material, animation, shader, and post-processing capabilities.
2. Investigate how existing scenes implement audio-reactive sequencing, camera movement, scene transitions, GPU-friendly effects, and parameter modulation.
3. Use two parallel research subagents:
   - **Optical/art direction:** Research lenticular printing, multi-plane parallax, view-dependent imagery, holographic foils, and practical ways to reproduce their visual characteristics in real-time 3D.
   - **Character/asset production:** Investigate suitable ape head and hand assets, animation options, bar geometry, and feasible approaches to achieving a compelling animal apparition.
4. Find concrete visual references, not just written descriptions. Study the visual behavior of actual lenticular images and the composition of cinematic animal faces behind bars.
5. Produce a short implementation plan identifying the proposed asset source, rendering approach, animation strategy, transition sequence, and expected performance costs.

Do not modify production code until the existing architecture and likely implementation path are understood.

## 2. Core art direction

### Full-screen composition

The entire rendered frame is the image. Do not put the creature inside a circular POG/slammer, card, floating picture frame, or obvious rectangular display.

The ape should dominate the image when fully revealed. Its head, eyes, muzzle, hands, and bars should be large enough to read clearly at 1080p, even when the viewer is not examining the image closely.

The surrounding environment should be minimal and deliberately designed. The creature seems to exist in an almost-black, atmospheric space, but this must not become a generic cinematic void with a tiny subject floating in the middle.

Use subtle layers of haze, reflections, suspended dust, faint optical glints, and very restrained background illumination to establish depth. These elements should support the apparition rather than compete with it.

The overall aesthetic should feel like a cursed, glossy, mass-produced optical image that has acquired a disturbing life of its own. It should be uncanny, tactile, dark, and visually distinctive.

Avoid generic AI-realistic horror imagery, cheap jump scares, excessive fog, random particles, aggressive glitch effects, and conventional horror-film color grading.

### The ape

Create one compelling, King Kong-inspired great ape, not an exact recreation of a particular film character.

Prioritize:
- A powerful, broad head with a recognizable ape silhouette.
- Deep-set eyes with a convincing gaze.
- A pronounced muzzle and believable mouth geometry.
- Large hands with individually readable fingers wrapped around the bars.
- Strong separation between the foreground hands, bars, head, and background.
- A detailed but controlled surface: dark, coarse fur or sculpted fur clumps, with selected areas of skin and wet specular highlights.
- Lighting that reveals the facial structure without making the entire creature uniformly bright.

The ape must look intentional and art-directed. A convincing silhouette, expressive eyes, good lighting, and carefully chosen material response are more important than excessive polygon count or surface detail.

### Asset acquisition

Investigate existing licensed or openly available ape assets before creating everything from scratch. Check actual geometry, texture quality, rigging, topology, licensing, and whether the face and hands can be independently animated.

If a suitable asset cannot be found, use Blender to create or assemble a purpose-built asset. A sculpted or assembled hero head with separate hands may be preferable to a complete character if it gives us greater control over the composition and is easier to render.

Do not accept an asset merely because it is technically usable. Inspect it under the intended lighting and camera angle before committing to it.

## 3. The lenticular illusion

This is the central technical and artistic challenge.

Do not simply apply horizontal or vertical stripes to the ape, add chromatic aberration, or call the result lenticular. Those techniques may be supplementary, but they do not establish the illusion.

Develop the effect through a combination of the following:

### A. Layered depth

Create clear depth separation between:
- The nearest bars.
- The ape's fingers wrapped around those bars.
- The ape's muzzle and brow.
- The eyes and deeper facial features.
- The darker layers behind the head.

The foreground bars and hands should move perceptibly relative to the facial features when the camera moves.

### B. View-dependent motion

Introduce a small, deliberate camera movement: a slow horizontal drift, subtle lateral parallax, and possibly a slight change in viewing angle.

The motion should reveal different spatial relationships between the layers. The face should feel embedded in a structured optical image rather than merely sitting in front of a background.

Investigate whether the effect can be strengthened using controlled view-dependent shading, layered image planes, or a lenticular-inspired material. Choose the simplest approach that produces a convincing result.

### C. Optical surface

Use restrained, finely spaced surface ridges or a directional microstructure to create moving highlights as the viewing angle changes.

The ridges should be most visible in specular reflections, not as an obvious striped overlay covering the entire image.

Explore subtle interference colors, directional reflections, fine scratches, and glossy surface behavior. Keep the palette controlled: mostly dark neutrals with selective silver, cold violet, or muted iridescent highlights.

### D. Living image

Where feasible, use actual animation for the eyes, jaw, head, or hands. A slow blink, tiny jaw movement, subtle change in gaze, or finger tightening can make the creature feel disturbingly alive.

Keep this restrained. The creature should not behave like an ordinary animated gorilla in a cage. It should feel like an image struggling to move beyond the optical surface that contains it.

If true lenticular rendering proves too expensive or difficult, implement a controlled approximation using layered geometry and view-dependent transitions. Document the compromise rather than abandoning the visual concept.

## 4. The apparition sequence

Implement one complete, repeatable appearance cycle.

### Phase 1 — Ether

The screen is nearly dark, but not visually empty. Faint reflections and barely perceptible surface structure suggest that something is concealed in the image.

No obvious placeholder geometry, wireframes, or generic particle cloud.

### Phase 2 — First contact

A faint glint reveals a bar, the curve of a hand, or a small portion of the ape's face.

The creature should emerge selectively rather than appearing as a uniformly fading complete model.

Allow the viewer to discover the subject.

### Phase 3 — Materialization

The ape gradually resolves out of the darkness. Its hands grip the bars, the muzzle becomes visible, and the eyes emerge last or become the final point of focus.

Use controlled lighting, visibility, material, or depth transitions. The reveal should feel like an optical image resolving into depth, not a conventional model fading in.

The transition should have a clear visual hierarchy and remain readable at performance resolution.

### Phase 4 — Presence

Hold on the fully realized ape long enough for the audience to appreciate it.

The creature maintains a subtle, unsettling presence. Its eyes can shift, its jaw can move slightly, or its fingers can tighten around the bars.

Use the slow camera drift to reveal the lenticular-inspired parallax.

A restrained light sweep can travel across the surface and briefly expose the ridges, facial detail, and depth of the image.

This is the hero moment. Prioritize the quality of the face, lighting, composition, and optical illusion.

### Phase 5 — Disturbance

Introduce a small change that suggests the image is unstable. The gaze shifts, a highlight crosses the eyes, the head moves slightly out of alignment with the foreground bars, or the image momentarily reveals another depth layer.

Avoid a generic glitch burst or violent geometric distortion.

### Phase 6 — Return to ether

The creature recedes or breaks apart selectively into darkness. The hands, eyes, muzzle, and head may disappear at different rates, leaving behind a few reflective traces before the image returns to its original near-empty state.

Avoid simply scaling the entire model backward or applying one uniform opacity fade.

### Phase 7 — Reset

Return cleanly to the initial state and allow the sequence to repeat.

The cycle must work repeatedly without visible popping, stuck parameters, abrupt camera resets, or accumulated state.

Use a parameterized timeline or the existing AV Gen sequencing architecture. Do not build a one-off animation system if existing scene mechanisms already support the sequence.

## 5. Audio reactivity

Use the existing AV Gen signal and modulation systems. Inspect current signal naming, timing, and scene conventions before connecting anything.

The POC should be musically responsive without becoming a collection of unrelated effects.

Suggested mappings:

- **Low-frequency energy:** Adds weight to the apparition, deepens the illumination of the face, or causes a very small forward movement.
- **Kick or major transient:** Briefly tightens the creature's grip, reveals a strong facial highlight, or creates a restrained optical pulse.
- **Midrange energy:** Influences the creature's presence, facial illumination, or the rate at which concealed details become visible.
- **High-frequency energy:** Drives small glints, fine ridge reflections, or brief highlights in the eyes.
- **Musical progression:** Controls the overall apparition envelope, with appearances, holds, disturbances, and disappearances arranged into coherent phrases.

These are starting hypotheses, not mandatory mappings. Use signals that are actually available and correctly timed in the repository.

Do not infer kick, snare, or hi-hat identities from the full mix if the current architecture has moved away from mix-based percussion classification. Prefer the supported signal sources and existing instrument-specific routing.

The sequence must remain visually interesting when the audio is quiet and must not flash uncontrollably during dense passages. Audio should influence a well-designed animation, not replace one.

## 6. Performance and implementation constraints

This is a live audiovisual scene.

- Target 1080p at a stable 30 FPS on the project's intended lower-end live-performance hardware.
- Avoid relying on expensive ray tracing, large volumes, high-resolution simulations, or multiple full-screen post-processing passes.
- Prefer simple geometry, efficient materials, baked or authored detail, and carefully controlled lighting.
- Investigate whether the optical effect can be implemented within the existing renderer without adding a new rendering architecture.
- Reuse existing scene, parameter, timeline, animation, and effect systems wherever appropriate.
- Keep the asset budget focused on one hero subject.
- Measure the scene with profiling tools rather than estimating performance from complexity alone.

Do not sacrifice the defining visual effect to add unrelated detail.

## 7. Evaluation criteria

The POC is successful only if all of the following can be demonstrated:

1. **Hero image:** The ape looks compelling and unsettling at 1080p, with a clear silhouette, expressive face, and convincing hands gripping bars.
2. **Full-screen integration:** The image fills the frame without looking like a floating card or a circular collectible.
3. **Perceived depth:** Camera movement reveals meaningful separation between bars, fingers, muzzle, eyes, and background.
4. **Optical character:** Directional reflections and subtle surface structure evoke lenticular printing rather than a generic striped shader.
5. **Apparition:** The ape materializes, remains present, and disappears through a deliberate, polished transition.
6. **Life:** At least one facial or hand movement adds a convincing sense of presence, even if the full character is not animated.
7. **Audio response:** The scene responds to music coherently without destroying the composition.
8. **Performance:** The scene meets its 1080p30 target on the chosen test hardware, with profiling evidence.
9. **Repeatability:** The sequence can loop reliably and reset without artifacts.

## 8. Explicit scope exclusions

Do not implement a menagerie of animals yet.

Do not build a large environment, elaborate cage, elaborate narrative, or complex cinematic camera path.

Do not add circular POG framing, collectible-card layouts, UI elements, or decorative typography.

Do not substitute an ordinary horror scene with a face fading in and out for the intended optical illusion.

Do not spend time building a generalized lenticular authoring framework before the first image works.

## 9. Deliverables

1. The working POC scene, integrated into the existing AV Gen project.
2. The hero ape asset and any required authored materials or animation.
3. A repeatable apparition sequence with working camera and audio-reactive controls.
4. A short technical summary describing the lenticular approximation, the actual sources of perceived depth, and the limitations of the effect.
5. A performance report showing resolution, frame rate, and relevant CPU/GPU timings on the test hardware.
6. A short visual capture demonstrating the full appearance cycle.
7. A concise list of the most important next improvements, ordered by their visual impact.

Keep the implementation focused. The immediate goal is to prove that a full-screen, lenticular-inspired animal apparition can look exceptional in AV Gen. If the ape works, we can use the same visual language to develop a succession of different animals and increasingly strange transformations.

---

## The owner's follow-up note (verbatim)

One artistic decision I'd make early
I'd avoid making the bars look like an ordinary prison cell. Instead, have the hands and bars appear to occupy a different optical depth than the ape's face, with the bars occasionally catching a sharp, thin highlight. The ape should feel like it's pushing through the image itself, not merely standing behind a cage.
And I'd judge the first result with the camera moving slowly from side to side, not with a static screenshot. Lenticular printing is fundamentally about the relationship between viewpoint and image. If the effect only looks good in a still frame, we haven't proved the concept yet.
Once that first apparition works, we can explore animal variants, different emotional expressions, and audio-driven transitions without having to reinvent the scene's core visual technique.
