> Owner brief, verbatim, 2026-10-31. Coordinator notes for the agent are in `01-coordinator-notes.md`;
> where they differ on working rules (agent count, stops, scene registration), the notes apply, and
> on art direction the owner's words here win.

CHRONOS — Temporal Imaging

AV Gen Proof of Concept: Scene & Rendering Specification

Status: Proposed — Ready for repository reconnaissance and implementation planning
Project: AV Gen
Scene name: CHRONOS
Implementation target: C++23 / WebGPU / Dawn / Metal
Initial performance target: 1920×1080 at 30 FPS
Primary objective: Demonstrate a compelling real-time 3D environment whose visual appearance is transformed through temporal sampling, slit-scan imaging, and controlled temporal feedback.

⸻

1. Project Overview

CHRONOS is an experimental audiovisual scene exploring time as a visual and spatial dimension.

Rather than treating time as a sequence of independent rendered frames, the scene captures its own visual history and uses that history as material for subsequent frames.

The result should be an environment that can stretch, fracture, displace, and recursively reconstruct itself according to its movement and musical input.

The scene combines:

* A procedurally generated 3D geological environment.
* A GPU-resident temporal frame-history system.
* Multiple slit-scan sampling techniques.
* Temporal displacement and image fragmentation.
* Controlled feedback accumulation.
* Audio-reactive modulation.
* An intentional cinematic camera journey.

This is a proof of concept for both a new AV Gen scene and a potentially reusable renderer capability.

Important architectural distinction: The temporal imaging system should be designed as a reusable renderer feature, while CHRONOS owns the particular artistic configuration, environment, camera, and musical mappings.

We should not build a CHRONOS-specific collection of hardcoded post-processing shaders.

2. Artistic Direction

2.1 Environment: The Temporal Canyon

The environment should be a visually rich, surreal geological landscape.

The initial environment consists of:

* A broad, winding canyon.
* Layered, eroded sandstone formations.
* Distinctive foreground rock formations.
* A river running through the canyon.
* Distant mountain ranges.
* Atmospheric haze and volumetric-looking lighting.
* A dramatic sky with evolving illumination.
* Occasional suspended geological formations or unusual natural structures.

The environment should feel handcrafted and cinematic rather than procedurally random.

Use a restrained but vivid palette:

* Deep indigo and violet shadows.
* Warm orange, amber, and red geological surfaces.
* Subtle magenta and gold atmospheric illumination.
* Selective cool highlights in the water.
* Strong contrast between illuminated formations and shadowed canyon walls.

Avoid generic sci-fi architecture, repetitive geometric structures, random floating shapes, and generic AI-generated environments.

The landscape must look compelling before any temporal effects are enabled.

2.2 Landscape Generation

Investigate the procedural terrain generation and geometry conversion approaches used in the existing Strange Attractor scene.

Reuse successful components where practical.

In particular:

1. Preserve successful procedural canyon generation techniques.
2. Prefer engine-native triangle geometry when it provides more reliable terrain surfaces.
3. Verify triangle winding, normals, topology, seams, and UV/procedural coordinates.
4. Ensure the river remains properly aligned with the terrain.
5. Avoid visible cracks and edge distortion.
6. Use deterministic seeds.
7. Establish explicit vertex, triangle, and instance budgets.
8. Use instancing and shared geometry where practical.
9. Keep the terrain generation independent of the temporal imaging system.

Do not blindly copy the Strange Attractor environment. Reuse the underlying techniques and lessons while creating a distinct landscape.

2.3 Camera

The camera should follow a slow, deliberate cinematic path.

Initial journey:

1. Establish the canyon from an elevated perspective.
2. Descend toward the river.
3. Travel forward along the canyon.
4. Pass close to interesting rock formations.
5. Gradually reveal the temporal anomalies.
6. Enter the most visually intense temporal transformation.
7. Allow the environment to settle back into its original form.

The camera must provide meaningful movement and parallax.

Do not use a stationary camera simply to demonstrate a screen-space shader.

Camera motion is especially important because it provides a way to distinguish temporal image history from conventional motion trails.

3. Temporal Imaging System

3.1 Architecture

Implement a GPU-resident temporal imaging subsystem.

Conceptual pipeline:

```
Scene Rendering
|
v
Scene Color Capture
|
v
Temporal Frame History
|
+–– Frame History Ring
+–– Optional Depth History
+–– Frame Metadata
|
v
Temporal Sampling
|
+–– Vertical Slit Scan
+–– Horizontal Slit Scan
+–– Directional Sampling
+–– Temporal Displacement
+–– Temporal Fracture
|
v
Temporal Compositing
|
+–– Feedback Accumulation
+–– Controlled Decay
+–– Color Processing
|
v
HDR Scene Output
|
v
Existing Tone Mapping / Final Presentation
```

This is a conceptual architecture, not a mandate to insert new stages without understanding the existing renderer.

The implementation must integrate with the established AV Gen render graph, resource lifetime rules, and synchronization model.

3.2 Frame History

Create a configurable circular texture buffer.

Requirements:

* GPU-resident frame history.
* Configurable history duration.
* Configurable sampling resolution.
* No per-frame texture allocation.
* Predictable memory consumption.
* Correct handling of ring-buffer wraparound.
* Explicit resource synchronization.
* Support for different temporal sampling offsets.
* Ability to clear or reset history.
* Graceful handling of scene initialization and invalid history.

Start with scene color history.

Investigate depth history as a second stage rather than making it an unconditional dependency.

The history should initially capture scene-linear HDR color before final tone mapping and UI composition, subject to the existing renderer's architecture.

Do not accidentally feed editor overlays, debug UI, or unrelated final presentation elements back into the temporal system.

3.3 Effect A: Slit Scan

This is the foundational effect and the first implementation priority.

The shader should sample a narrow region of previous frames and combine those samples spatially.

Supported sampling configurations:

Vertical slit scan

Sample narrow vertical strips from successive frames and arrange them horizontally.

Horizontal slit scan

Sample horizontal strips from successive frames and arrange them vertically.

Directional slit scan

Allow the sampling direction and orientation to be controlled by parameters.

Moving slit

Allow the sampling position to move through the image over time.

Expose controls for:

* Slit width.
* Sampling direction.
* History duration.
* Sampling interval.
* Scan position.
* Scan velocity.
* Temporal blending.
* Sampling resolution.

The effect must be useful at both subtle and extreme settings.

It should produce recognizable temporal structures rather than simply smearing the image.

3.4 Effect B: Temporal Displacement

Sample different temporal offsets for different color channels or image regions.

Examples:

* Red samples the recent past.
* Green samples an intermediate point in history.
* Blue samples a substantially older frame.

Expose:

* RGB temporal offsets.
* Displacement magnitude.
* Spatial displacement direction.
* Channel blending.
* Audio modulation inputs.

The result should create colored temporal separation, not ordinary chromatic aberration.

3.5 Effect C: Temporal Fracture

Divide the image into independently sampled temporal regions.

Each region can display a different point in the frame history.

Support:

* Horizontal bands.
* Vertical bands.
* Irregular spatial regions.
* Configurable region count.
* Temporal discontinuities.
* Controlled tearing and displacement.
* Transition between coherent and fractured sampling.

The visual behavior should evolve organically.

Avoid static, evenly spaced glitch stripes as the only implementation.

3.6 Effect D: Temporal Feedback

Introduce a separate ping-pong feedback accumulation system.

Previous temporal output may be reintroduced into subsequent frames.

Controls:

* Feedback intensity.
* Decay rate.
* Temporal persistence.
* Color attenuation.
* Displacement.
* Feedback blend mode.
* Maximum accumulation.
* Reset behavior.

Prevent uncontrolled accumulation, NaNs, excessive HDR values, and runaway visual brightness.

The effect should be capable of producing recursive visual structures while retaining enough recognizable environmental information to communicate the underlying landscape.

Feedback should be independently switchable.

4. Audio Integration

Use AV Gen's existing signal path and modulation infrastructure.

Do not introduce a separate audio analysis implementation solely for CHRONOS.

Audio should control the underlying behavior of the temporal imaging system, rather than merely changing the overall effect intensity.

4.1 Initial Mapping

| Signal | Parameter | Desired behavior |
|---|---|---|
| Kick / transient energy | Temporal fracture | Sudden temporal discontinuities |
| Bass | History depth | Increasing or decreasing temporal persistence |
| Synth | Temporal displacement | Gradual shifts between moments |
| Hi-hat / high-frequency energy | Sampling granularity | Fine-grained temporal fragmentation |
| Crash / major transient | Feedback intensity | Large-scale temporal rupture |
| Overall amplitude | Effect blend | Controls the overall degree of transformation |

These are initial artistic mappings, not mandatory classifier dependencies.

Use actual stems or explicitly available audio buses where possible.

Do not make the scene dependent on unreliable kick/snare/hi-hat inference from a stereo mix.

If individual stems are unavailable, use general audio analysis, existing buses, or MIDI modulation.

4.2 Musical Behavior

The system should support both transient and sustained musical behavior.

Examples:

Sustained synth note

Gradually increases temporal persistence, allowing the landscape to accumulate a longer visual history.

Bass progression

Changes the sampling rate and temporal depth, creating an evolving sense of temporal acceleration or deceleration.

Percussive transient

Creates localized fractures or sudden changes in sampling offsets.

Large crash

Triggers an intense temporal rupture, followed by controlled recovery.

Quiet section

Allows the environment to return toward its original appearance.

Use bounded modulation and smoothing to prevent unstable parameter oscillation.

The scene should remain musically expressive without becoming visually chaotic at all times.

5. Scene Progression

Implement the following initial progression.

Act I — The Present

* Establish the conventional canyon.
* No significant temporal distortion.
* Slow cinematic camera movement.
* Introduce the river and surrounding geological structures.

Act II — Temporal Awakening

* Introduce subtle vertical and horizontal slit scanning.
* The river begins retaining visual traces of previous positions.
* Moving rock formations exhibit temporal echoes.
* Gradually increase history depth.

Act III — Temporal Displacement

* Introduce RGB temporal separation.
* Increase spatial and temporal displacement.
* Different parts of the canyon begin showing different moments.
* Introduce controlled temporal fracture.

Act IV — Temporal Collapse

* Enable more aggressive slit scanning.
* Combine temporal fracture and displacement.
* Introduce feedback accumulation.
* Allow major musical events to dramatically transform the image.

Act V — Reconstruction

* Reduce feedback.
* Gradually restore coherent sampling.
* Return to the conventional canyon.
* Allow the camera to continue moving through the environment.

The progression should be controlled through the scene's existing timeline, modulation, and parameter systems wherever practical.

It should also be possible to audition individual effects independently.

6. Renderer and Performance Requirements

Initial target:

* 1920×1080.
* 30 FPS sustained live operation.
* Apple Silicon / Metal / Dawn.
* Existing HDR rendering pipeline.

Performance requirements:

1. Avoid CPU readback of temporal frame history.
2. Keep temporal history GPU-resident.
3. Avoid unnecessary full-resolution intermediate buffers.
4. Reuse textures and render resources.
5. Minimize synchronization barriers.
6. Avoid redundant full-screen passes.
7. Support reduced-resolution temporal processing.
8. Ensure predictable memory usage.
9. Profile each effect independently and in combination.

The implementation should expose useful profiling information:

* Temporal history capture cost.
* Slit-scan processing cost.
* Displacement processing cost.
* Fracture processing cost.
* Feedback processing cost.
* Total temporal subsystem GPU cost.
* Temporal buffer memory consumption.

Do not compromise image quality prematurely to meet performance targets. Establish a visually compelling baseline, then optimize.

Offline rendering at higher quality is a potential future extension, not a requirement for this POC.

7. Configuration and Debugging

Expose the temporal imaging system through the existing AV Gen scene parameter and modulation interfaces.

Provide a way to inspect:

* Current history buffer index.
* History duration.
* Sampling offsets.
* Current effect intensities.
* Feedback accumulation.
* Temporal subsystem GPU timings.
* Buffer resolution.

Provide independent enable/disable controls for each effect.

A clean, neutral debug mode should allow comparison between:

1. Original scene.
2. Slit scan only.
3. Temporal displacement only.
4. Temporal fracture only.
5. Temporal feedback only.
6. Combined temporal processing.

The debug functionality should not become part of the final artistic presentation.

8. Testing and Validation

Implement automated tests where meaningful.

Cover:

* History buffer indexing and wraparound.
* Initialization and reset.
* Parameter bounds.
* Invalid history handling.
* Resource lifetime and synchronization.
* Feedback decay behavior.
* Resolution changes.
* Deterministic scene generation.

Visual validation should include:

Test A — Stationary camera

Move an object through the scene and verify that slit scanning produces recognizable temporal structure.

Test B — Moving camera

Move through the canyon and verify that temporal sampling creates coherent spatial distortions.

Test C — Stationary geometry

Verify that stationary elements do not exhibit unintended temporal smearing.

Test D — Audio transients

Verify that transient-driven fractures are responsive and recover smoothly.

Test E — Sustained audio

Verify that persistent musical energy produces gradual, controlled temporal changes.

Test F — Feedback stability

Verify that prolonged feedback does not cause uncontrolled accumulation or visual corruption.

Test G — Performance

Profile the full effect stack at 1080p/30 FPS.

9. Implementation Phases

Phase 0 — Repository Reconnaissance

Before changing code:

* Inspect the current renderer architecture.
* Identify the HDR scene color capture point.
* Identify render graph and texture resource management.
* Inspect existing post-processing implementations.
* Inspect existing audio parameter and modulation interfaces.
* Inspect the Strange Attractor scene and its terrain-generation approach.
* Identify any existing temporal history, feedback, or frame-buffer infrastructure.

Produce a short architectural assessment identifying reuse opportunities and integration risks.

Do not assume that the architecture described in this document maps directly to existing class names or render stages.

Phase 1 — Minimal Temporal Rendering Prototype

Implement:

* GPU frame-history ring buffer.
* History capture.
* Basic vertical slit scan.
* Basic horizontal slit scan.
* Configurable history duration.
* Enable/disable controls.

Use a simple moving test object if necessary before completing the landscape.

Exit criteria: Demonstrate that previous rendered frames can be sampled and spatially recombined in real time.

Phase 2 — CHRONOS Environment

Implement the canyon landscape and camera path.

Reuse successful Strange Attractor terrain techniques where practical.

Establish compelling conventional rendering before introducing aggressive effects.

Exit criteria: A visually complete environment with a cinematic camera path.

Phase 3 — Temporal Imaging Effects

Implement:

* Directional slit scanning.
* Temporal displacement.
* Temporal fracture.
* Feedback accumulation.

Expose individual controls.

Exit criteria: Each effect works independently and can be combined without resource conflicts.

Phase 4 — Audio Integration

Connect the existing audio and modulation infrastructure.

Implement initial musical mappings.

Validate behavior with actual audio material and available stems.

Exit criteria: Sustained and transient musical events produce meaningfully different temporal behavior.

Phase 5 — Artistic Integration and Optimization

Implement the scene progression.

Tune the temporal behavior, color, camera, and environmental composition.

Profile at 1080p/30 FPS and address the largest bottlenecks.

Exit criteria: A complete, repeatable audiovisual scene suitable for demonstration and further development.

10. Explicitly Out of Scope for the Initial POC

Do not let the project expand prematurely into:

* Full temporal geometry reconstruction.
* Complex physically simulated historical geometry.
* World-space persistence of every previous object position.
* Full motion-vector reprojection.
* Advanced volumetric temporal reconstruction.
* Multiple independent environments.
* Elaborate portal systems.
* A complete offline rendering pipeline.
* A generalized node-based temporal compositing editor.

These are potential future extensions.

The initial POC should prove the value of the temporal imaging concept using screen-space history and a compelling 3D environment.

11. Future Research: Temporal Geometry

Keep the architecture open to a future extension where temporal history becomes actual 3D geometry.

Potential experiments:

* Previous object positions represented as translucent geometric remnants.
* River trajectories preserved as spatial ribbons.
* Terrain formations retaining displaced historical positions.
* Geometry reconstructed from previous depth and camera transforms.
* World-space temporal trails that persist independently of camera movement.

This is not required for the initial implementation.

However, avoid architectural choices that unnecessarily prevent future depth-aware or world-space temporal processing.

12. Agent Workflow and Deliverables

Use a disciplined repository workflow.

1. Inspect the repository and existing project documentation.
2. Research the renderer and Strange Attractor implementations.
3. Produce an implementation plan before writing code.
4. Identify the minimum set of changes needed for Phase 1.
5. Use no more than two parallel subagents, with clearly separated responsibilities.
6. Keep implementation isolated in an appropriate branch or worktree.
7. Preserve existing scene behavior and renderer stability.
8. Add tests and profiling instrumentation.
9. Run relevant tests and CI.
10. Review the final diff for architectural issues and unnecessary complexity.
11. Update relevant documentation and architectural decisions.
12. Merge the completed work cleanly into the main development branch.

Use Blender headless/MCP and free assets where appropriate for environment construction. Prioritize deliberate VFX-artist-quality scenery over generic procedural assets.

Required Deliverables

* Repository reconnaissance report.
* Renderer integration plan.
* Temporal imaging architecture.
* GPU frame-history implementation.
* Working slit-scan prototype.
* CHRONOS scene implementation.
* Initial audio-reactive mappings.
* Automated tests.
* Performance report.
* Documentation and implementation notes.
* Captured visual demonstrations of the individual effects and complete scene.

⸻

Definition of Done

The CHRONOS POC is successful when:

1. A recognizable, visually compelling 3D canyon environment exists.
2. The camera moves through the environment.
3. Previous rendered frames can be sampled and recombined spatially.
4. Vertical and horizontal slit scanning work in real time.
5. Temporal displacement and fracture produce distinct visual effects.
6. Feedback creates recursive temporal imagery without instability.
7. Audio can control the temporal behavior through existing AV Gen infrastructure.
8. The scene has a coherent beginning, transformation, climax, and resolution.
9. The complete effect stack can operate at the initial 1080p/30 FPS target, or has clearly documented and measured bottlenecks.
10. The temporal imaging system is reusable outside CHRONOS.

The central artistic objective: Make the audience feel as though they are moving through a world in which time has become a physical material. The effect should feel intentional, beautiful, disorienting, and deeply connected to the music—not like a conventional glitch filter applied to a landscape.
