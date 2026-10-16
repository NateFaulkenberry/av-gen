# AV Gen: Cymatic World — Research, Prototype, and Implementation

(Owner brief, received 2026-10-16, saved verbatim. The owner also shared a ChatGPT link, https://chatgpt.com/s/m_6ad17a308f7c8191b678ada6cc5200d7, titled "Cymatic World Concept Pitch"; it is not readable logged out. If a concept-board image is supplied it will be placed in ~/Desktop/av-gen-review/58-cymatic-world/.)

## Mission

Build a new first-class AV Gen live scene provisionally named Cymatic World.

The goal is to explore a fundamentally different approach to audio-reactive 3D environments: instead of applying conventional audio-driven animation to a scene, create a shared resonant system through which acoustic energy determines how the world behaves.

AV Gen already has a cymatic plate scene that produces compelling, visually synchronized results. Study that implementation and preserve whatever makes it successful. However, this project must go significantly beyond extending the plate into a larger surface.

The central design principle is that the world behaves like a physical system that converts acoustic energy into visible structure and motion.

Audio should excite a coherent resonance field. Terrain, materials, entities, particles, and potentially light should interpret that field in different ways while remaining parts of the same underlying system.

This is an experimental audiovisual instrument, not a conventional game environment, a generic music visualizer, or a showcase of unrelated shader effects.

The result must be visually sophisticated, musically expressive, distinctive, and viable for real-time live performance.

## 1. Begin with repository research

Before implementing anything, investigate the current AV Gen architecture and the existing cymatic plate scene.

Identify:

* The existing cymatic plate scene, its implementation, configuration, assets, and audio-response mechanisms.
* How audio analysis, the signal bus, timeline, modulators, parameters, generators, scene graph, and renderer interact.
* Existing GPU compute, shader, procedural geometry, deformation, particle, field, material, lighting, and post-processing capabilities that could support this experiment.
* Existing approaches to persistent state, temporal smoothing, simulation updates, resource lifetime, GPU synchronization, and live parameter changes.
* How scenes are registered, configured, loaded, previewed, benchmarked, and validated.
* Existing scene-quality tooling, screenshots, render diagnostics, profiling utilities, and automated tests.

Reuse existing infrastructure when appropriate. Avoid duplicating functionality already provided by the engine.

Do not assume that the current plate implementation is the correct architecture for an entire world. It is a reference implementation and a source of successful visual ideas, not necessarily the foundation of every subsystem.

Review relevant architecture decision records and documentation before proposing new infrastructure.

Produce a concise internal design proposal, then proceed with implementation unless a genuine architectural blocker requires a decision.

## 2. Visual direction and reference material

The aesthetic target is experimental, handcrafted, physically suggestive 3D art with extraordinary spatial detail and strong compositional intent.

Aim for an environment that feels like an unfamiliar physical world made from resonant matter. It may contain organic, crystalline, mineral, metallic, fluid, or abstract structures, but these should feel intentionally designed rather than randomly generated.

Use the following visual references as inspiration. These are references for specific qualities, not instructions to reproduce the images literally.

### Reference A — Resonant terrain

Visual target: a dark, sculptural landscape whose actual silhouette and topology reveal intricate standing-wave structures.

* Cymatic terrain and standing-wave landscape references
* Chladni figures and physical resonance patterns

Look for layered ridges, coherent interference patterns, deep hollows, and complex spatial structures. The geometry should be interesting before emissive lines or post-processing are added.

### Reference B — Resonant crystalline and metallic materials

Visual target: sculptural forms with rich internal structure, sharp or intricately layered silhouettes, and materials that reveal their resonant behavior through deformation, reflection, refraction, or illumination.

* Crystalline sculptural forms and refractive structures
* Intricate metallic filaments and geometric sculptures

Avoid the appearance of generic low-detail procedural crystals, primitive shapes, or a random collection of floating objects. The structures should have a deliberate visual identity.

### Reference C — Resonant entities and self-organizing matter

Visual target: complex three-dimensional entities made from particles, filaments, branching structures, or constrained geometric elements.

* Particle-based organic entities and luminous filament structures
* Cymatics particle simulations

Look for intricate, coherent forms that appear to emerge from underlying physical rules. Avoid generic particle spheres, default particle emitters, and undirected swarms.

### Reference D — Optical resonance

Visual target: light and reflections that reveal the geometry of an underlying field.

* Refractive optical caustics and experimental 3D art
* Light interference and luminous sculptural structures

Optical effects must reinforce the world's physical structure, not conceal a weak scene behind distortion.

### Reference E — Overall world and atmosphere

Visual target: a cohesive, otherworldly environment with depth, layered silhouettes, atmospheric perspective, carefully controlled color, and visually interesting structures at multiple scales.

* Surreal alien landscapes with intricate organic geometry
* Bioluminescent sculptural environments

Favor sophisticated deep blues, indigos, violets, and selective teal, cyan, or green accents. Other palettes are acceptable if they serve the scene.

These colors are suggestions, not a requirement to make everything glow. Use darkness, contrast, material variation, and restrained emission to preserve visual hierarchy.

If a concept-board image is supplied alongside this prompt, use it as a high-level moodboard. Treat the individual reference categories above as the more precise guidance for each subsystem.

## 3. Core architecture: a shared resonance field

Investigate and implement a reusable resonance-field abstraction that can eventually serve multiple AV Gen scenes and generators.

Do not build the system as a collection of independently FFT-driven effects.

Conceptually, the system should support:

Audio analysis → excitation → resonant modes → spatial field → subsystem-specific physical responses.

### 3.1 Audio excitation

The field should accept AV Gen's existing audio-derived signals and parameters.

Where available and appropriate, support:

* Frequency-selective excitation.
* Transient and sustained energy.
* Multiple simultaneous excitation sources or frequency regions.
* Temporal persistence and decay.
* Configurable excitation strength and response curves.
* Phase relationships where they contribute meaningfully to the model.

Reuse existing audio-analysis and modulation infrastructure. Do not build a redundant FFT pipeline.

Raw frequency bands should not directly dictate the final shape of every visual effect. They excite the system; the system determines the response.

### 3.2 Resonant modes

Investigate representations for spatial resonance that are suitable for live rendering.

Possible approaches include:

* Analytic standing-wave functions.
* Superpositions of spatial modes.
* Frequency-selective resonators with configurable damping.
* Procedural basis functions appropriate to terrain and object geometry.
* Sampled or texture-backed fields where they offer a meaningful advantage.
* More physically rigorous simulation where research demonstrates that its benefits justify its complexity.

A useful starting point is a modal representation:

D(x,t) = Σᵢ Aᵢ(t) φᵢ(x)

Here, φᵢ(x) describes the spatial shape of a mode and Aᵢ(t) describes its time-dependent response to excitation.

The eventual system may need additional state for phase, velocity, damping, and coupled behavior. This expression is a conceptual starting point, not a requirement to use this exact implementation.

Investigate the relationship between excitation frequency and the resonant response of each mode. Do not assume that mapping an audio FFT bin directly to an arbitrary visual mode creates a physically meaningful resonance.

The field should produce stable, interpretable patterns with enough complexity to be musically expressive.

### 3.3 Temporal behavior

Persistence is essential.

The system should not reset its appearance every frame or merely mirror the current audio amplitude.

Investigate:

* Attack and decay behavior.
* Mode-specific damping.
* Resonance buildup under sustained excitation.
* Overlapping modes and interference.
* Phase continuity.
* Stable behavior when the input becomes quiet.
* Smooth transitions between excitation states.
* Predictable behavior during playback seeking, scene resets, and parameter changes.

The world should have a visual memory of recent excitation, while remaining responsive enough for live performance.

### 3.4 Field queries and subsystem integration

Design a practical interface that allows downstream systems to query the field for the information they actually need.

Potential outputs include:

* Displacement or deformation.
* Local energy or excitation strength.
* Field gradients and preferred directions.
* Phase or modal contributions.
* Local strain or other derived quantities where meaningful.

Do not expose an unnecessarily elaborate general-purpose physics API before a concrete use case requires it.

The interface must be compatible with AV Gen's existing scene, parameter, and rendering architecture.

Prefer an implementation that can run efficiently on the GPU when appropriate, but evaluate the actual bottlenecks rather than assuming GPU execution is always the right answer.

## 4. Three mandatory prototypes

Build three visually distinct manifestations of the same resonance field. These are the first milestone and should be completed before expanding into a large environment.

### Prototype A — Resonant terrain

Create a substantial terrain surface whose actual geometry responds to superimposed resonant modes.

Requirements:

* Real geometric deformation, not only a scrolling shader or emissive line overlay.
* Multiple simultaneous spatial modes.
* Clearly differentiated large-scale and fine-scale deformation.
* Frequency-selective excitation.
* Temporal persistence and damping.
* Meaningful interaction between overlapping modes.
* Attractive surface shading, silhouettes, and lighting.
* Stable geometry without excessive flicker, aliasing, or visible grid artifacts.

The terrain should be interesting when the audio is active and remain visually intentional during quieter passages.

Do not settle for a flat plane with a sine wave applied to its vertices. Explore layered terrain, ridges, hollows, folds, and other sculptural structures that reveal the underlying modes.

Use a manageable terrain resolution and determine whether vertex displacement, procedural mesh generation, GPU deformation, or another existing engine capability provides the best trade-off.

### Prototype B — Resonant sculpture

Create one or more deliberately designed structures that consume the same field but respond differently from the terrain.

Requirements:

* A recognizable, intentionally composed silhouette.
* Material or geometric characteristics that support a distinct resonant identity.
* Frequency-selective response.
* Visible internal or surface structure.
* A response that is demonstrably driven by the shared field.
* Meaningful temporal continuity rather than independent amplitude pulsing.

Explore deformation, structural flexing, internal emission, refraction, reflection, and strain-dependent material behavior as appropriate.

Do not implement all of these simply because they are listed. Select the smallest combination that yields an excellent result.

### Prototype C — Resonant matter

Create a constrained particle, filament, or similar entity that organizes around the same field.

Requirements:

* Stable, coherent three-dimensional structure.
* A recognizable shape or family of shapes.
* A meaningful relationship between resonant modes and particle or filament arrangement.
* Persistence across changing audio input.
* Smooth, controlled transitions.
* Evidence of self-organization or field-constrained behavior rather than simple particle animation.

Investigate whether particles should settle toward nodal regions, follow field gradients, respond to phase relationships, or participate in another appropriate mechanism.

Use structural constraints, damping, or other stabilizing mechanisms as needed. Unconstrained particles that disperse into visual noise are not an acceptable final result.

The entity should feel like resonant matter, not a standard particle emitter with a cymatics texture.

## 5. Optional extension: coupled resonance

Once the three core prototypes work, investigate whether the world can support meaningful coupling between subsystems.

For example:

* Terrain deformation excites a nearby sculpture.
* A sculpture's response influences its surrounding field.
* Resonant entities synchronize or change configuration when exposed to compatible modes.
* Optical effects reveal the same field that drives geometric deformation.

Treat coupling as a research direction rather than an excuse to introduce a costly global physics simulation.

Start with one clear, demonstrable interaction. Use the simplest mechanism that establishes a causal relationship.

Avoid feedback loops that become numerically unstable, generate uncontrolled energy, or require expensive simulation at every point in the world.

## 6. Rendering and aesthetic requirements

The visual quality of the result is a primary deliverable.

The scene should feel like a crafted experimental audiovisual artwork, not a technical demo.

Priorities:

1. Strong composition and readable silhouettes.
2. Visibly three-dimensional resonant geometry.
3. Rich structural detail at multiple spatial scales.
4. Material differentiation.
5. Lighting that reveals rather than obscures the forms.
6. Clear musical responsiveness.
7. Temporal continuity and visual stability.
8. Performance appropriate for live AV use.

Avoid:

* A giant flat surface covered in scrolling wave lines.
* Generic terrain with an audio-reactive displacement shader.
* Unrelated objects all pulsing to the same FFT bands.
* Excessive bloom and emissive outlines used to compensate for weak geometry.
* Random spinning shapes, default primitives, and undirected particle clouds.
* Generic AI-generated landscapes translated literally into 3D.
* Unmotivated architectural structures.
* Excessive full-screen distortion.
* Visual clutter that makes the resonance patterns difficult to read.

Use an intentional camera, meaningful depth, foreground and background separation, and a clear focal hierarchy.

The final scene may be abstract, but it must be artistically deliberate.

Do not make visual fidelity dependent on an expensive offline rendering path. This is a live scene and must run through the existing AV Gen renderer.

## 7. AV Gen integration and performance

Follow the existing engine conventions and use the current rendering backend.

Do not rewrite the renderer or introduce a new graphics API for this project.

Investigate the actual cost of the new field and its consumers using available AV Gen profiling and diagnostic tools.

Measure:

* CPU update cost.
* GPU frame time.
* GPU memory and resource allocations.
* Field evaluation cost.
* Geometry deformation cost.
* Particle or filament update cost.
* Additional rendering passes.
* Behavior at the intended live resolution and at 1080p.

Use existing scene benchmarks and test hardware where available.

Target stable real-time performance, ideally within the engine's existing live-render budget. Do not sacrifice the core visual concept merely to reach an arbitrary frame rate, but document the trade-offs and optimize the expensive components.

Prefer shared field evaluation, cached or persistent resources, bounded simulation work, and efficient GPU data flow where justified.

Avoid redundant field evaluation by each effect, unnecessary per-frame allocations, excessive bind-group churn, and CPU readbacks from the GPU.

If a more expensive approach is valuable artistically, provide an appropriate quality or complexity control rather than making it the only operating mode.

All essential behavior must work in the actual AV Gen runtime, not merely in an isolated shader demo.

## 8. Live performance controls

Expose a compact, useful set of scene parameters through AV Gen's existing parameter system.

Prioritize controls that meaningfully change the behavior:

* Overall excitation or resonance strength.
* Mode complexity or density.
* Resonance persistence or damping.
* Spatial scale.
* Terrain deformation intensity.
* Sculpture response.
* Entity response.
* Optional optical-effect intensity.

Treat this list as a starting point, not a requirement to expose every internal variable.

Use meaningful parameter ranges and defaults. Keep the live control surface simple enough to use while performing.

Where appropriate, make important parameters available for modulation by the existing audio, MIDI, and timeline systems.

The scene should work well out of the box without extensive manual tuning.

## 9. Visual validation and iterative critique

Do not consider the task complete merely because the scene compiles, renders, or responds to audio.

Use a repeated implementation and visual-critique loop.

1. Establish a baseline scene and benchmark.
2. Implement the shared field and the first terrain prototype.
3. Capture representative frames during quiet passages, sustained tones, transients, and dense audio.
4. Inspect the actual rendered output.
5. Identify visual and technical weaknesses.
6. Make targeted improvements.
7. Repeat for the sculpture and resonant-matter prototypes.
8. Test all three together using the same field.
9. Capture a final set of comparable screenshots and performance measurements.

Use the existing AV Gen scene-inspection, render-capture, AOV, depth, profiling, and reference-comparison tooling wherever available.

If the current tooling cannot capture or inspect a needed diagnostic, document the limitation and use an appropriate existing workaround. Do not silently invent new infrastructure when a simpler validation method exists.

Evaluate the following questions explicitly:

* Can the spatial resonance pattern be understood from the geometry alone?
* Does the system respond differently to different frequency content?
* Do overlapping modes produce interesting, coherent structures?
* Does the scene retain a sense of continuity between transients?
* Do terrain, sculpture, and resonant matter look and behave distinctly?
* Can an observer recognize that the different systems share an underlying field?
* Does the result remain visually compelling when the glow and post-processing are reduced?
* Is the scene free of obvious deformation artifacts, temporal instability, and distracting visual noise?

If a prototype looks like a generic audio visualizer, continue iterating. Do not rationalize weak results as acceptable because the underlying architecture is technically correct.

## 10. Tests and documentation

Add tests for the behavior that can be validated independently of subjective visual quality.

Depending on the final design, cover:

* Resonance-mode evaluation.
* Excitation and decay behavior.
* Damping and temporal continuity.
* Finite outputs and numerical stability.
* Deterministic behavior when given the same inputs and simulation conditions.
* Parameter validation and boundary conditions.
* Scene loading and reset behavior.
* Resource lifecycle and relevant integration paths.

Do not create brittle automated tests that claim to measure artistic quality through arbitrary pixel thresholds.

Update relevant documentation and architecture records if the implementation introduces a meaningful reusable subsystem or architectural decision.

Document the field model, its data flow, parameter semantics, performance characteristics, known limitations, and the steps needed to extend it.

## 11. Scope discipline

The first milestone is a strong shared field and three convincing prototypes.

Do not turn this into a generalized world-building framework, a new physics engine, or an extensive catalog of effects.

Avoid unnecessary dependencies, redundant abstractions, compatibility layers, and infrastructure that isn't justified by the current implementation.

Preserve existing behavior in unrelated scenes. Do not modify the existing cymatic plate scene unnecessarily.

If an existing subsystem can support the new behavior with a small, clean extension, prefer that over introducing a parallel implementation.

Make reasonable implementation decisions autonomously and record the important ones in the final report.

## 12. Definition of done

The project is complete when:

* A new Cymatic World scene is registered and usable in AV Gen.
* A shared resonance-field system drives all three prototypes.
* Terrain deformation is genuinely geometric and visibly resonant.
* The sculpture and resonant-matter prototypes exhibit distinct, coherent responses to the same field.
* Temporal persistence, damping, and overlapping modes work convincingly.
* The scene has a polished composition and a distinctive visual identity.
* Live audio drives the system through AV Gen's existing infrastructure.
* Essential controls are exposed and usable.
* Tests, scene validation, and performance measurements are complete.
* Relevant documentation is updated.
* Final screenshots demonstrate the quality and behavior of the implementation.

## Final report

Provide a concise but substantive report covering:

1. The architecture chosen and why.
2. How the resonance field works.
3. How each of the three prototypes consumes it.
4. The most successful visual results and remaining aesthetic shortcomings.
5. Screenshots or a contact sheet showing the actual final render.
6. Performance measurements at the tested resolutions, including CPU and GPU costs where available.
7. Tests run and their results.
8. Documentation and architecture records updated.
9. Known limitations and the most valuable next experiment.

Be candid about any part that remains a proof of concept.

Success is not a large number of audio-reactive effects. Success is a compelling live environment in which music excites a coherent resonant system, and the world expresses that system through several distinct forms of behavior.
