# Talking Heads: the owner's scene specification (verbatim, 2026-10-24)

The owner's covering note: "I don’t want to start active development on this scene yet but do any deep research and prep work for a new live scene now, get things set up and ready to pass off to an agent later"

Coordinator notes:
- Prep only for now: research and set-up, ready to hand to an implementation agent later. No production scene code yet.
- The spec mentions "the supplied De-Loused in the Comatorium cover" as a mood reference; no image was attached in the conversation. Ask the owner for it before art work starts.
- Category: Sonic Abstract (per the spec). Live target 1080p30; owner rules apply (no compat shims, consistent renders, no helper agents, 2-agent cap).
- ADR range reserved for the implementation: 1670-1689.

---

AV Gen Scene Specification: TALKING HEADS

1. Objective

Create a new Sonic Abstract live-performance scene provisionally named Talking Heads.

The scene features a surrealist mannequin bust floating in a dark atmospheric ether. The bust emits colored light rays from its mouth and eye regions and undergoes audio-reactive visual transformations, including vibration, spectral duplication, chromatic corruption, and localized video-signal distortion.

The artistic objective is an unsettling, slightly abstract audiovisual entity that appears to communicate through music.

This is a real-time performance scene, not an offline cinematic rendering project.

Hard constraints

* Target 1920×1080 at 30 FPS on the established AV Gen live-rendering baseline.
* Prioritize predictable frame time over expensive effects.
* Reuse existing scene, audio-analysis, modulation, material, particle, and rendering systems wherever practical.
* Do not build a new rendering architecture for this scene.
* Do not introduce unnecessary dependencies.
* Do not create multiple heads in the initial implementation.
* Do not build a complete facial animation rig.
* Do not use a large environment, elaborate scenery, a cathedral, or a complicated architectural backdrop.
* Do not use generic AI-generated realistic scenery as a substitute for a deliberately authored 3D asset.
* No compatibility shims or unrelated architectural changes.
* Keep the scene independently testable and easy to tune.

The initial deliverable must be a complete, visually compelling single-head scene with genuine audio reactivity, not merely a static model surrounded by audio meters.

2. Art direction

Core visual identity

A pale, smooth, uncanny mannequin bust floats in a dark, abstract space.

The bust should feel like an art object that has acquired an unwanted consciousness. It is neither a normal human portrait nor a robot. It has a recognizable head silhouette but simplified, subtly unnatural features.

Visual references:

* Surrealist sculpture and gallery busts.
* Smooth porcelain mannequins.
* Uncanny, expressionless faces.
* Experimental analog-video and electronic-music visuals.
* Colored light emission from the mouth and eyes.
* Restrained, deliberate use of glitch effects.

Use the supplied De-Loused in the Comatorium cover as an initial mood reference only. Do not reproduce the cover or copy its composition.

Silhouette and proportions

Aim for a sculptural, slightly elongated head with a simplified jaw, high cheekbones, a restrained nose, and an elongated neck terminating in a small shoulder or bust base.

The proportions may be subtly exaggerated. Avoid grotesque caricature, conventional beauty-render proportions, and overt horror-monster features.

The object must remain legible as a head in silhouette, including when viewed at a slight three-quarter angle.

The mouth and eye regions must be visually identifiable enough to support the emitter effects.

Surface treatment

Use a smooth, pale porcelain or matte ceramic surface with restrained specular response.

The material should have:

* A warm ivory or cool bone-white base.
* Soft highlights that reveal the sculptural shape.
* Slightly darker eye sockets and mouth interior.
* Optional subtle surface imperfections if they improve the result.
* No obvious photogrammetry artifacts.
* No excessive skin pores, wrinkles, eyelashes, or other realistic facial details.
* No unnecessary texture maps.

The face should contrast against the darker background and saturated emitted light.

Do not rely on an expensive subsurface-scattering implementation. A carefully tuned standard material is sufficient.

Initial palette

Establish a consistent starting palette:

* Environment: near-black indigo and deep violet.
* Secondary atmosphere: restrained blue-purple.
* Primary emission: magenta and violet.
* Secondary emission: electric blue and pink.
* Occasional accent: acidic red or pale white.

Avoid random rainbow cycling as the default behavior. Colors should change in response to sound and intentional musical transitions.

Composition

The bust is the primary subject and should occupy approximately 30–50% of the frame height in the initial composition.

Use a three-quarter or nearly frontal view, with sufficient space around the head for the emitted rays.

The camera should have a slow, restrained drift. Avoid constant spinning, aggressive camera shake, or motion that makes the object difficult to inspect.

The environment should be deliberately sparse:

* A dark gradient or equivalent atmospheric background.
* A small amount of depth-establishing haze, if inexpensive.
* Sparse floating particles, if useful.
* No floor, horizon, buildings, landscape, or visible stage.

The result should feel like an entity suspended in an unknown space, not a mannequin photographed in a studio.

3. Asset acquisition and creation

The coding agent must investigate existing assets before committing to custom modeling.

Phase A: inspect existing assets

Evaluate these candidates:

1. Sketchfab — Mannequin Head by scbenoit:
    https://sketchfab.com/3d-models/mannequin-head-d465a1f697b54136b65288ce9c23b16f
2. Innerscene — Modern Abstract Bust:
    https://www.innerscene.com/tools/library/3d-parts/modern-abstract-bust-91e3ae04
3. Sketchfab — Mannequin Head #3, Untextured:
    https://sketchfab.com/3d-models/mannequin-head-3-untextured-77910a3722124bf398a3c3030bc0eac4

These are research leads, not approved assets. Verify the current listing, actual geometry, downloadable files, license, attribution requirements, and permitted use before selection.

The third candidate is noncommercially licensed according to its listing and must not be used in a commercial production deliverable without appropriate additional permission.

Inspect the actual model, not just its thumbnail.

Evaluate:

* Silhouette from frontal and three-quarter views.
* Whether the model has the desired abstract or mannequin-like character.
* Surface continuity and mesh defects.
* Whether the eye and mouth regions read clearly.
* Polygon count and draw-call implications.
* Whether the geometry can be modified without destroying the silhouette.
* Import compatibility with the existing asset pipeline.
* License suitability for distribution with AV Gen.

Do not download an asset solely because it is free.

Phase B: create or modify the asset in Blender

If no existing candidate is suitable, create a custom sculptural bust using Blender.

Use the available Blender MCP workflow if it is operational; otherwise use Blender’s Python API and headless Blender where practical.

Prefer procedural modeling and modifiers for reproducibility. Manual sculpting is acceptable when it produces a materially better result and can be completed efficiently.

Create a smooth, simplified head with:

* A clean, recognizable silhouette.
* An elongated neck and simplified bust termination.
* Shallow eye sockets or clearly defined eye regions.
* A simplified mouth opening or dark recessed mouth region.
* Smooth surface transitions.
* No elaborate facial rig.
* No unnecessary internal geometry.

Use a small number of well-defined material regions. Separate the mouth interior from the outer surface if necessary to control emitted rays and lighting.

Do not attempt to construct a convincing animated human face. This milestone is a sculptural object, and its motion should come primarily from the scene’s visual effects.

Asset deliverables

Save the working Blender source and the runtime-ready asset in the repository’s established locations.

The source should be reproducible and editable.

Export through the existing supported asset pipeline. Prefer an already supported format and do not introduce a new loader just for this asset.

Document:

* Original asset URL, if applicable.
* License and attribution.
* Any modifications.
* Export format.
* Triangle count.
* Material assignments.
* Required scene setup.

Validate that the model imports, appears correctly in the live renderer, and uses the intended coordinate system, scale, and orientation.

4. Mouth and eye emission

The emitted light is the scene’s defining visual feature.

Build this as a reusable effect rather than embedding all behavior directly into the scene definition.

Mouth emission

Create a fan of thin colored rays or ribbons originating from the mouth region.

The effect should support:

* Emission intensity.
* Ray count.
* Beam length.
* Beam width.
* Spread angle.
* Directional bias.
* Color and color blending.
* Motion or wavering.
* Transient response.
* Sustained emission.
* Fade-in and fade-out.
* A short residual trail, if supported efficiently.

The beams should look like intentional light phenomena rather than thin debugging lines.

Use a bright core with a restrained soft halo. Layered geometry or shader-based width variation is preferable to excessive transparent particle overlap.

The fan should remain readable against the dark environment. Avoid making every beam equally bright.

Eye emission

Create narrower rays originating from the eye regions.

Eye emission should have a different visual character from mouth emission:

* More focused and directional.
* Narrower beams.
* Shorter or more tightly controlled emission.
* Independent left and right intensity.
* Optional slight divergence between the two eyes.
* Occasional short bursts tied to transients.

The eyes must not continuously emit maximum-strength rays. Their quiet state should remain visually interesting.

Emitter attachment

The mouth and eye emitters must remain attached to the head during its movement and deformation.

Use stable local-space emitter transforms or the closest equivalent supported by the scene architecture.

Avoid hardcoded world-space coordinates that break when the head scale, rotation, or asset changes.

Performance strategy

Start with a small number of beam primitives and a simple material.

Prefer instanced geometry, procedural beam generation, or an existing AV Gen effect implementation where appropriate.

Do not introduce volumetric light scattering as a prerequisite. Screen-space glow, simple haze, and good beam composition should establish the effect first.

5. Entity-level visual effects

Implement a small, coherent selection of effects that visibly transform the bust.

The initial release should include the following three effects.

Effect A: spectral vibration

The bust develops a rapid, unstable vibration in response to low-frequency energy and transient intensity.

Visual behavior:

* Slight oscillation or displacement.
* One or two offset spectral copies.
* Optional directional separation between copies.
* Increased separation with stronger audio.
* Fast recovery to the stable form.

The baseline model must remain readable.

Avoid large-scale vertex explosions or arbitrary twisting. The effect should resemble an unstable optical or physical signal.

Prefer a few additional draws or another inexpensive implementation over a costly simulation.

Effect B: chromatic possession

Color spreads across the bust in response to musical energy.

Visual behavior:

* Controlled color washes across the face.
* Smooth transitions between palette states.
* Stronger emission or color displacement on transients.
* Optional localized color concentration around the mouth and eyes.
* Gradual recovery to the base porcelain appearance.

The effect must be visually attached to the bust.

Use existing material parameter modulation or shader capabilities if available. Do not introduce a bespoke material framework for one effect.

Effect C: localized signal tearing

The bust briefly appears to break into misaligned video-signal bands.

Visual behavior:

* Horizontal displacement or band offsets.
* Occasional chromatic separation.
* Brief, transient-driven bursts.
* A controllable distortion amount.
* Rapid recovery.

Keep the effect localized to the bust where possible.

Do not apply full-screen data moshing as a prerequisite for this milestone. True temporal data moshing can be explored later if the renderer supports it efficiently.

If the current material or rendering pipeline cannot implement localized distortion cleanly, document the limitation and implement the best lightweight alternative rather than modifying the renderer architecture.

Effect composition

Effects must be independently adjustable and combinable.

A bass transient might cause vibration and a brief color pulse simultaneously. A snare could trigger signal tearing while the mouth emits a short beam burst.

Establish a quiet baseline and avoid saturating every effect continuously.

6. Audio-reactive behavior

Use the existing AV Gen audio-analysis, signal-routing, and modulation systems.

Do not build a parallel audio-analysis pipeline.

First inspect the current audio source and modulation APIs, including how they handle per-channel signals, transient detection, parameter smoothing, and same-frame signal propagation.

The scene must respond to the assigned audio source, not merely to a generic master amplitude.

Initial signal set

Use existing signals where available, or the closest equivalent supported by the engine:

* Low-frequency energy.
* Mid-frequency energy.
* High-frequency energy.
* Broadband amplitude.
* Transient intensity.
* Sustained energy.
* Optional onset or hit events.

Avoid implementing duplicate detectors if existing signals can provide the required behavior.

Use the existing per-channel or stem-based source assignment when available.

Suggested mappings

Audio signal	Visual response
Low-frequency energy	Spectral vibration and head displacement
Bass transients	Brief mouth-beam expansion and vibration spikes
Mid-frequency energy	Mouth emission intensity and chromatic washes
High-frequency energy	Eye-ray flicker and fine signal tearing
Strong transients	Short, controlled bursts of distortion
Sustained energy	Persistent but restrained beam emission
Silence or low energy	Beams fade while the bust returns to its quiet state

These are initial defaults, not mandatory one-to-one mappings.

The scene must support smoothing, attack/release behavior, and parameter ranges that avoid noisy flicker or constant maximum output.

Musical responsiveness

Use fast transient response for attacks and slower decay for sustained visual trails.

Do not introduce additional frame-delayed macro or envelope hops if the existing signal architecture can deliver the needed values in the current frame.

Inspect the signal path for latency and validate the resulting visual response against an actual audio file.

The scene must remain compelling with different source material and must not be tuned exclusively to one track.

MIDI and manual controls

Expose the most useful parameters through the established AV Gen control system.

At minimum, provide access to:

* Overall effect intensity.
* Mouth-ray intensity.
* Eye-ray intensity.
* Vibration amount.
* Signal-tearing amount.
* Chromatic effect amount.
* Global visual gain or equivalent master control.

Use existing parameter and MIDI mapping conventions. Do not create a new control system.

7. Procedural life and uncanny behavior

The head should feel faintly conscious even when the audio is quiet.

Implement only lightweight behaviors that do not require a facial rig:

* Slow, subtle head rotation.
* Small irregular orientation changes.
* Slight positional drift.
* Optional eye-region highlights that shift over time.
* Brief movement accents triggered by sound.

The initial bust does not need articulated eyeballs or a sophisticated gaze rig.

If the selected asset has clearly modeled eyes and the implementation is inexpensive, a later extension may animate independent eye direction.

Do not simulate eye movement by deforming arbitrary facial vertices.

Movement should be restrained, irregular, and intentional. The bust must not constantly spin, bounce, or twitch.

8. Scene lifecycle and visual states

Design the scene to move between three useful states.

State 1: dormant

* The bust floats almost motionlessly.
* The surface is pale and softly illuminated.
* The background remains dark and atmospheric.
* Only faint environmental movement is visible.
* Beams are absent or nearly invisible.

State 2: communicating

* The mouth and eyes emit colored rays.
* Spectral vibration responds to low frequencies.
* Chromatic washes follow musical energy.
* Signal tearing occurs in short bursts.
* The camera remains sufficiently stable to preserve the composition.

State 3: overload

* Strong transients intensify the emitted beams.
* Multiple entity effects may overlap.
* Color becomes more saturated.
* Spectral separation increases.
* Signal tearing becomes more pronounced.

Overload must be temporary and controllable. It should not permanently obscure the bust or turn the entire frame into undifferentiated noise.

Returning to dormancy should feel like the entity falling silent, not like the scene abruptly disabling its effects.

9. Rendering and performance constraints

The target is 1080p at 30 FPS on the existing live-rendering baseline.

Profile the complete scene, including all post-processing and effect passes.

Prioritize:

* One primary bust mesh.
* Minimal draw calls.
* Small numbers of beam primitives.
* Simple material shading.
* Limited transparency overdraw.
* No unnecessary real-time shadow maps.
* No expensive volumetric simulation.
* No high-cost full-screen effects that can be localized.
* No per-frame CPU mesh reconstruction unless demonstrated to be inexpensive.

Reuse the engine’s existing lighting, glow, material, and effect implementations where possible.

Profile the scene before and after adding each major effect.

Record CPU time, GPU time, resolution, and frame rate. Distinguish CPU and GPU bottlenecks.

If the scene exceeds its frame budget, identify the dominant cost and simplify that component. Do not automatically reduce the bust’s visual quality to compensate for an unnecessarily expensive effect.

The initial goal is a stable, polished single-head scene. Additional heads are explicitly outside the first milestone.

10. Artistic validation

Do not judge the result solely by whether the scene renders correctly.

Produce captured previews or short clips that demonstrate:

1. Dormant state.
2. Mouth emission.
3. Eye emission.
4. Spectral vibration.
5. Chromatic possession.
6. Localized signal tearing.
7. Combined response to a musical passage.
8. Recovery during a quiet passage.

Inspect the bust at its actual intended on-screen size.

Verify that:

* The silhouette reads immediately.
* The surface looks sculptural rather than like generic CG skin.
* The beams have visual depth and direction.
* Effects are visibly attached to the entity.
* The audio mappings produce distinct, understandable responses.
* The quiet state remains atmospheric.
* High-energy passages remain visually legible.
* The result feels like a coherent audiovisual artwork rather than a demo of unrelated shaders.

Use the existing cinematic or visual-critique workflow where appropriate. Fix obvious compositional, material, and effect problems before adding features.

11. Implementation workflow

Before making production changes:

1. Inspect the repository and existing scene authoring conventions.
2. Identify reusable scenes, materials, beam effects, post-processing, audio signals, and parameter mappings.
3. Research the candidate assets and verify their licenses.
4. Inspect available Blender automation and export workflows.
5. Produce a concise implementation plan with dependencies and risks.
6. Identify the smallest complete vertical slice.

Use two parallel subagents where supported:

* Asset and art-direction subagent: inspect candidate models, licensing, silhouette, Blender options, and propose the most achievable asset route.
* Engine and implementation subagent: inspect existing scene systems, signal routing, effect capabilities, rendering costs, and reusable implementations.

Have the subagents return concrete findings before integrating their work. Avoid conflicting edits to the same files.

Then implement the smallest complete scene, build it, run the relevant tests, and capture visual evidence.

Do not create elaborate planning documents or unrelated infrastructure for their own sake.

Required deliverables

* A working Talking Heads scene registered through the existing scene system.
* A validated, editable bust asset and its source where appropriate.
* Functional mouth and eye emission.
* Three composable entity FX.
* Audio-responsive parameter mappings.
* Exposed controls using existing conventions.
* Successful build and relevant tests.
* Visual captures demonstrating the required states.
* A performance report at 1080p.
* Documentation of the asset source, license, implementation choices, limitations, and remaining work.

Finish by summarizing what was implemented, what was reused, the measured performance, any outstanding visual shortcomings, and the recommended next step.

12. Explicitly deferred work

Do not implement these in the initial milestone:

* Three or four independently controlled heads.
* Full facial animation or lip synchronization.
* Complex articulated eyes.
* True data-moshing simulations.
* Expensive volumetric lighting.
* Elaborate environments or set dressing.
* A general-purpose entity FX editor.
* A new audio-analysis framework.
* A new rendering pipeline.
* Large-scale architectural changes.

The intended outcome is one unmistakable, uncanny sculptural entity that appears to speak through music. Make that work beautifully before expanding the scene.
