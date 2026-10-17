# AV Gen R&D: Coupled Oscillators, Flow Fields, and Reaction-Diffusion

(Owner brief, received 2026-10-17, saved verbatim. Coordinator notes: the owner has said to ignore the 120 Hz / 8.33 ms reference in these briefs — report against the live target of 1080p at 30 fps; live scenes carry no audio file — test with assets/audio/feline-footwear.wav fed on the command line.)

## Mission

Conduct a serious experimental R&D investigation into three new generative behavior systems for AV Gen:

1. **Coupled oscillators and phase fields**
2. **Flow fields and fluid advection**
3. **Reaction-diffusion and emergent pattern formation**

The objective is to discover new visual behaviors that can drive compelling, musically responsive 3D audiovisual scenes. We are not merely adding three conventional audio-reactive effects. We are investigating three fundamentally different ways of generating visual structure and motion.

Each system should be prototyped, rendered, evaluated, tuned, and compared against the existing resonator concept.

The result should be a set of convincing visual experiments, performance measurements, documented findings, and a recommendation about which systems should be developed further.

**Prioritize visual discovery over premature abstraction.** Build enough infrastructure to conduct good experiments, but do not begin by designing a universal simulation framework or refactoring existing scene architecture.

The work is successful if we discover even one genuinely compelling new visual behavior that could become a defining AV Gen scene or reusable capability.

---

# 1. First: Understand the Existing Engine

Before implementation, inspect the current AV Gen repository and establish how these experiments should fit into the actual project.

Read the relevant source code and documentation for:

- The existing resonator implementation and recent Cymatic World / Resonant Silk work
- Scene creation, registration, configuration, and lifecycle
- Audio analysis, signal bus, modulation, and parameter systems
- MIDI input and musical event handling
- Geometry generation, vertex displacement, instancing, and particle systems
- Compute shaders and GPU resource management
- The WebGPU Dawn/Metal rendering pipeline
- Material, lighting, post-processing, and color management
- Existing scene test harnesses, offline rendering capabilities, and performance instrumentation
- Current scene-specific performance problems and GPU budgets

Use the existing implementation as the source of truth. Do not assume names, interfaces, or capabilities based on this specification if the repository already provides equivalent functionality.

Identify the cleanest way to create isolated experiments that can be launched and visually inspected using existing AV Gen workflows.

## Constraints

- Preserve the existing resonator implementation and its current visual quality.
- Do not replace, regress, or casually refactor existing scenes.
- Do not introduce a new renderer or a parallel rendering architecture.
- Do not create a second audio-analysis pipeline.
- Reuse existing audio features, signal routing, modulation, MIDI handling, scene lifecycle, and GPU infrastructure wherever appropriate.
- Avoid backward-compatibility shims for abandoned experimental code unless they are necessary to preserve real functionality.
- Avoid building a generalized simulation framework before the experiments demonstrate what should be generalized.
- Keep the work modular enough that an unsuccessful experiment can be removed without damaging unrelated systems.
- Respect the existing WebGPU/Dawn/Metal backend and supported platform constraints.

Start by writing a concise repository-specific implementation plan. Then proceed with the experiments rather than stopping for approval.

---

# 2. Experiment A: Coupled Oscillators and Phase Fields

## Concept

Build a system in which many oscillating elements interact, synchronize, drift apart, and form collective patterns.

This should be a different visual behavior from conventional cymatic resonance.

The existing resonator concept explores how a system responds to excitation and develops resonant spatial structure. This experiment instead focuses on **relationships between oscillators**: phase alignment, synchronization, interference, traveling phase waves, and transitions between organized and disorganized behavior.

A relatively simple oscillator network should be capable of generating rich visual behavior through its internal dynamics.

## 2.1 Mathematical foundation

Begin with a network of oscillators. Each oscillator has at least:

- Phase
- Natural frequency
- Amplitude or activity
- Coupling to neighboring oscillators
- A spatial location or relationship to a visual structure

A useful starting point is a Kuramoto-style phase-coupling model:

dθᵢ/dt = ωᵢ + (K/N) Σⱼ Aᵢⱼ sin(θⱼ − θᵢ)

Where:

- θᵢ is oscillator phase.
- ωᵢ is its natural frequency.
- K controls coupling strength.
- Aᵢⱼ describes which oscillators interact and how strongly.

Do not assume this equation is the final solution. Explore spatially local coupling, nonuniform coupling, phase delays, and alternative oscillator models where useful.

A fully connected network is not necessarily desirable. Spatially structured networks may produce much more interesting visual results and scale better.

Explore at least these network configurations:

1. **Local lattice:** Each oscillator couples to nearby neighbors.
2. **Waveguide:** Oscillators primarily interact along a path or surface.
3. **Layered network:** Multiple oscillator populations couple at different strengths.
4. **Spatially weighted network:** Coupling falls off with distance.
5. **Hybrid network:** Local coupling with a small number of longer-range connections.

Measure simulation stability and cost rather than assuming every configuration is practical.

## 2.2 Visual experiments

Create several distinct visual mappings of the same underlying behavior.

### A. Phase-wave sculpture

Represent phase as a continuous field across a surface or 3D arrangement.

Use phase to drive:

- Vertex displacement
- Local surface orientation
- Wavefront propagation
- Geometric ridges and troughs
- Surface curvature
- Emissive accents

The objective is to create an evolving sculpture whose geometry is shaped by interacting oscillators.

Avoid simply mapping phase to a sine wave on a flat grid and calling the result complete. Explore multiple spatial layouts, scales, and deformation functions.

### B. Synchronization sculpture

Create a field of geometric elements that visually express the degree of synchronization.

When oscillators synchronize, elements might align into coherent spatial structures. When synchronization breaks down, the structure might split, drift, twist, or develop competing patterns.

Explore transitions between:

- Disordered motion
- Partial synchronization
- Strong global synchronization
- Local synchronization islands
- Traveling synchronization waves

The changes should be visually legible and aesthetically interesting rather than appearing as random jitter.

### C. Interference geometry

Allow multiple oscillator populations to produce overlapping phase patterns.

Explore constructive and destructive interactions through geometry, shading, and spatial density.

The visual result might resemble folded membranes, interwoven ribbons, layered wavefronts, or intricate interference sculptures.

Avoid generic particle spaghetti. The geometry should have a strong composition and a recognizable visual identity.

### D. Resonant Silk extension

Test whether phase fields can provide a new behavioral engine for the existing Resonant Silk aesthetic.

Possible approaches:

- Multiple ribbons following different phase contours
- Ribbons that synchronize and separate in groups
- Traveling phase waves that propagate along ribbons
- Coupled ribbon networks that reorganize their motion
- Phase-dependent color boundaries and luminous pulses

This is an experiment, not a requirement to modify the existing scene. Prefer a separate prototype until the new behavior demonstrates a clear improvement.

## 2.3 Audio and MIDI interaction

Use the existing AV Gen signal infrastructure.

Audio should affect the dynamics of the system, not simply control an object's scale.

Explore mappings such as:

- Low-frequency energy influences the natural frequencies or coupling of large-scale oscillator groups.
- Mid-frequency energy influences local phase interactions.
- High-frequency energy introduces fine-scale phase disturbances.
- Onsets inject spatially localized phase perturbations.
- Sustained harmonic content encourages particular oscillator groups to stabilize or synchronize.
- MIDI notes excite specific regions, initiate traveling phase waves, or temporarily alter coupling.
- MIDI velocity controls excitation strength.
- Note duration controls the persistence of a disturbance where appropriate.

These are hypotheses to test, not fixed assignments. Compare alternatives and retain mappings that produce the strongest musical correspondence.

Avoid mapping every frequency band to every available parameter. That creates uncontrolled behavior and makes it difficult to understand what the music is doing to the system.

## 2.4 Musical transitions

Investigate synchronization transitions as a major source of visual expression.

For example:

- A sparse musical passage produces loosely coupled motion.
- Repeated rhythmic material gradually establishes synchronized regions.
- A strong transient disrupts an established pattern.
- A sustained musical passage allows the field to reorganize.
- A section transition changes the natural frequency distribution or coupling topology.

Explore parameter changes that cause bifurcations or other recognizable changes in system behavior.

Use smooth transitions where possible, but do not eliminate meaningful discontinuities when an actual phase transition is the desired visual event.

## 2.5 Required controls

Keep the user-facing interface simple.

Begin with:

- Coupling
- Complexity or density
- Motion intensity
- Audio influence
- Persistence
- Visual mode

Expose additional controls only when they demonstrably improve the experiment.

Provide useful defaults and avoid exposing low-level mathematical parameters unnecessarily.

---

# 3. Experiment B: Flow Fields and Fluid Advection

## Concept

Build a coherent spatial motion system in which geometry, particles, trails, and other visual elements can respond to a shared field of forces and directions.

The central idea is that motion should emerge from a designed field rather than from many unrelated animation curves.

This system should explore a different class of visual behavior from resonance: directional flow, vortices, circulation, branching currents, turbulence, and large-scale coordinated movement.

Do not confuse this experiment with implementing a complete physically accurate fluid solver. The initial objective is to discover visually compelling behavior with a manageable real-time GPU cost.

## 3.1 Field representation

A flow field assigns a velocity vector to each point in a domain.

Start with a practical GPU-compatible representation that fits AV Gen's existing architecture.

Investigate:

- Analytic vector fields evaluated directly in shaders
- Procedural curl-noise fields
- Grid-based velocity fields
- Particle-driven flow
- Combinations of analytic fields and simulated fields

Do not immediately commit to a grid-based fluid solver. Analytic and procedural fields may provide most of the desired artistic behavior at substantially lower cost.

The implementation should support 3D environments, while allowing 2D or surface-constrained fields where they make visual sense.

## 3.2 Field behaviors

Build a set of genuinely distinct flow behaviors.

### A. Vortex fields

Create rotating flow around one or more centers or axes.

Explore:

- Single coherent vortices
- Multiple interacting vortices
- Orbiting vortex centers
- Vortices with changing circulation
- Vortices embedded in a larger directional current

Look for sculptural motion rather than generic spinning particles.

### B. Curl-noise flow

Use a divergence-controlled or approximately divergence-free procedural field to generate smooth, swirling motion without excessive convergence or divergence.

Explore multiple spatial scales:

- Large-scale movement that establishes composition
- Medium-scale circulation that creates interesting trajectories
- Fine-scale motion that adds detail

The large-scale structure should remain legible even when fine-scale motion is active.

### C. Directed currents

Create fields that move visual elements along coherent directional paths.

Possible forms include:

- Parallel flowing bands
- Curved currents
- Converging and diverging streams
- Branching channels
- Helical flows
- Interwoven currents

These should be designed as meaningful spatial structures, not randomly sampled directions.

### D. Attractor-driven flow

Introduce attractors and repulsors that bend trajectories around important locations.

Allow the field to form coherent channels between regions, circulate around focal points, or move elements through a series of spatial zones.

### E. Traveling disturbances

Introduce impulses that propagate through the field.

A musical transient might produce an expanding disturbance, a traveling rotational wave, or a change in the direction of a current.

Explore both short-lived disturbances and persistent changes to the field.

## 3.3 Visual mappings

The flow field should be useful beyond a conventional particle effect.

Build prototypes for at least three of these representations:

1. **Particles:** Persistent trajectories that reveal the flow.
2. **Ribbons:** Curves or ribbons generated from integrated paths through the field.
3. **Surface deformation:** A mesh whose vertices move according to the field or a related displacement field.
4. **Volumetric density:** A volume or density field shaped by flow where existing rendering support makes this practical.
5. **Instanced geometry:** Small sculptural elements following the field while maintaining controlled orientation and spacing.
6. **Flow lines:** Carefully selected streamlines that reveal the field's structure.

Use the same underlying field to drive multiple representations where practical.

For ribbons, investigate parallel-transport frames or another stable orientation method so that ribbons do not twist unpredictably along their trajectories.

For particles, avoid resetting every particle on every beat. Persistent trajectories and meaningful temporal evolution are central to the experiment.

For surface deformation, prevent excessive distortion, self-intersection, and high-frequency noise that destroys the underlying form.

## 3.4 Audio and MIDI interaction

Explore audio mappings that change the field's structure:

- Bass energy modifies large-scale circulation or the strength of major currents.
- Mid-frequency content influences medium-scale vortices or directional channels.
- High-frequency content affects fine-scale turbulence.
- Onsets inject localized impulses.
- Repeated rhythmic events reinforce a directional flow or circulation pattern.
- MIDI notes create attractors, repulsors, vortices, or temporary flow channels.
- MIDI velocity controls impulse strength.
- Note release influences how quickly temporary structures dissipate.

Test alternatives. Do not use the same frequency-to-parameter mapping by default across every prototype.

A strong result should sound as though the music is shaping a coherent physical or quasi-physical environment, not operating a bank of independent effect knobs.

## 3.5 Musical and spatial composition

Investigate whether the flow field can improve scene composition.

For example:

- Major currents lead the eye toward a focal object.
- A transient disrupts a stable current and creates a new trajectory.
- Different musical layers control distinct spatial regions.
- A buildup increases circulation and spatial complexity.
- A breakdown allows the field to settle into a small number of elegant streams.

Keep important geometry and focal points visible. Motion should support composition rather than obscure everything with trails.

## 3.6 Required controls

Start with:

- Flow type
- Flow strength
- Spatial scale
- Turbulence
- Persistence
- Audio influence

Keep the number of exposed controls low. More detailed field parameters can remain internal or be added only after experimentation identifies a genuine need.

---

# 4. Experiment C: Reaction-Diffusion and Emergent Pattern Formation

## Concept

Investigate a system that generates complex spatial patterns through local interaction rather than explicit geometry animation.

Reaction-diffusion systems can produce spots, stripes, labyrinthine patterns, branching structures, and evolving boundaries.

The experiment should determine whether these patterns can become an important visual behavior in AV Gen, influencing surfaces, geometry, lighting, and spatial organization.

The goal is not merely to place a colorful reaction-diffusion texture on a plane.

We need to explore how the pattern can become part of a 3D scene's structure.

## 4.1 Mathematical foundation

Begin with a Gray-Scott-style reaction-diffusion model involving two fields, commonly called U and V.

A representative system is:

∂U/∂t = Dᵤ∇²U − UV² + F(1 − U)

∂V/∂t = Dᵥ∇²V + UV² − (F + k)V

Where:

- U and V are interacting fields.
- Dᵤ and Dᵥ control diffusion.
- F controls feed.
- k controls removal or kill rate.

Treat these equations as a starting point rather than a mandatory implementation.

Investigate numerical stability, boundary conditions, time-step selection, and parameter ranges that produce different visual regimes.

Avoid allowing arbitrary audio modulation of every equation parameter to destabilize the simulation. Musical control should be constrained and deliberate.

## 4.2 Simulation domain

Begin with a GPU-resident 2D simulation because it is a practical way to test the behavior and generate detailed patterns.

Investigate:

- Periodic boundaries
- Fixed boundaries
- Controlled seed locations
- Localized disturbances
- Multiple seed regions
- Different initial conditions
- Pattern growth and recovery after disturbances

Use appropriate ping-pong resources or equivalent GPU-safe update strategies.

Do not read simulation data back to the CPU every frame just to render the result.

Once the 2D behavior is convincing, investigate applying it to 3D geometry.

Potential domains include:

- A deformed surface
- A sphere or other closed mesh
- A curved or folded surface
- Multiple separate surfaces sharing a pattern field
- A spatial volume, if the visual benefit justifies the added cost

Do not assume that a full 3D reaction-diffusion simulation is necessary. A 2D simulation mapped onto well-designed 3D geometry may produce a better visual result for considerably less cost.

## 4.3 Pattern regimes

Explore distinct pattern families rather than relying on a single parameter preset.

Investigate:

- Spots and cellular structures
- Stripes and branching bands
- Labyrinthine structures
- Slowly evolving boundaries
- Competing pattern regions
- Traveling or propagating disturbances
- Pattern collapse and regrowth
- Transitions between sparse and dense structures

Document which parameter ranges produce useful regimes and which produce unstable, uniform, or visually uninteresting results.

Do not merely expose feed and kill as user-facing sliders. Translate useful combinations into a small number of meaningful artistic modes.

## 4.4 Turn patterns into actual visual structure

Build several renderings of the same simulation field.

### A. Patterned materials

Use the field to drive color boundaries, roughness, emissive regions, or other appropriate material properties.

This is the baseline, not the final goal.

### B. Surface relief

Use the field to drive displacement, ridges, raised spots, recessed channels, or layered surface structures.

Explore nonlinear remapping, thresholded regions, and smooth transitions.

The resulting surface should have recognizable structure rather than looking like an ordinary noisy displacement map.

### C. Pattern boundaries as geometry

Investigate extracting contours or isosurfaces from the pattern field.

Possible representations include:

- Raised contour lines
- Branching geometric networks
- Ribbons following pattern boundaries
- Thin emissive structures embedded in the surface
- Instanced elements distributed according to field density

Choose representations that work with AV Gen's existing geometry pipeline and can be evaluated at realistic performance costs.

### D. Spatial pattern propagation

Explore disturbances that spread across a surface or between separate regions.

A musical event could initiate a new pattern region, displace an existing boundary, or temporarily alter the local pattern regime.

The system should evolve continuously enough that the audience can perceive development across time.

## 4.5 Audio and MIDI interaction

Explore controlled musical influence:

- Bass energy modifies the strength or scale of surface deformation.
- Sustained harmonic energy biases pattern formation toward stable or slowly evolving regimes.
- High-frequency energy influences pattern detail within a safe range.
- Onsets inject localized disturbances into the field.
- Rhythmic repetition reinforces or refreshes selected seed regions.
- MIDI notes initiate pattern growth at specific locations.
- MIDI velocity controls disturbance size or intensity.
- Note duration influences the persistence of a locally altered region.

Test whether musical features should modify simulation parameters, initial conditions, local sources, or some combination.

Prefer spatially localized events over globally changing every simulation parameter on every transient.

Avoid allowing beat detection to continually reset the simulation. The pattern should have memory and develop its own visual history.

## 4.6 Color and lighting

The pattern system should support more than the familiar neon organic texture aesthetic.

Explore multiple art-directed palettes and lighting approaches, including:

- High-contrast red and indigo
- Deep violet with restrained gold highlights
- Warm ivory and dark mineral tones
- Black surfaces with selective emissive boundaries
- Low-saturation surfaces with carefully controlled luminous regions

Treat these as starting points, not fixed requirements.

Keep pattern generation separate from palette selection. The same underlying field should be capable of producing dramatically different appearances through material and lighting choices.

Explore whether pattern boundaries can drive subtle lighting accents, emissive propagation, or local shading changes.

Avoid indiscriminate bloom, oversaturated neon, and uniform emissive coverage.

---

# 5. Cross-Experiment Requirements

All three experiments must be evaluated against the same basic standards.

## 5.1 A real visual identity

Each system must demonstrate a distinctive behavior.

The oscillator experiment should emphasize phase relationships and collective synchronization.

The flow experiment should emphasize coherent motion and spatial currents.

The reaction-diffusion experiment should emphasize emergent spatial pattern formation.

If the three experiments end up looking like the same field of glowing particles with different shaders, the work has not achieved its purpose.

Use different geometry, spatial composition, movement, materials, and lighting to express the strengths of each system.

## 5.2 Meaningful musical responsiveness

Create a deliberate mapping between musical input and system behavior.

The audience should be able to perceive relationships between music and visuals without the scene simply flashing or scaling on every beat.

Test the systems with:

- Sustained tones
- Percussive material
- Dense, broadband audio
- Sparse passages
- Gradual builds
- Abrupt transitions
- MIDI note sequences where available

If the existing project provides representative test audio or MIDI material, reuse it.

Document which mappings work well and which create visual noise, instability, or weak musical correspondence.

## 5.3 Autonomous behavior

Each system must remain visually interesting when audio is quiet or relatively static.

Audio should influence a living system, not substitute for one.

Implement suitable autonomous evolution, persistence, and parameter smoothing. Ensure that silence does not cause the system to freeze in an accidental state unless that behavior is intentional.

## 5.4 Art direction

Avoid the following failure modes:

- Generic particle soup
- Randomly spinning objects
- Arbitrary sine-wave deformation
- Uniformly glowing geometry
- Excessive bloom
- Overly busy full-screen motion
- Cheap-looking procedural materials
- Unstructured visual noise
- Motion that obscures the scene's composition
- Generic AI-generated sci-fi aesthetics

Aim for carefully composed generative art and VFX: recognizable forms, deliberate motion, coherent color relationships, controlled detail, and strong silhouettes.

Prefer a few beautiful, legible structures over a huge quantity of mediocre geometry.

Explore both luminous and non-luminous treatments.

## 5.5 GPU performance

AV Gen is a real-time audiovisual instrument as well as an offline rendering engine.

Use the existing performance tools and measure the actual implementation on the available hardware.

The live target on the known M2 Max/Dawn/Metal configuration is 120 Hz, with an 8.33 ms total frame budget. This is a reference target, not permission to consume the entire budget with a new subsystem.

Measure:

- CPU simulation and submission overhead
- GPU simulation cost
- GPU rendering cost
- Memory consumption
- Resource allocation behavior
- Scaling with simulation resolution or element count
- Performance during transitions and peak complexity

Where useful, compare several quality levels.

For reaction-diffusion, test multiple grid resolutions.

For coupled oscillators, test different network sizes and coupling topologies.

For flow fields, test different particle counts, trail lengths, and field evaluation strategies.

Avoid unnecessary CPU-GPU synchronization and per-frame allocation. Keep GPU-resident state where appropriate.

Do not optimize blindly. First establish that the visual result is worth keeping, then identify the actual bottlenecks.

## 5.6 Stability and deterministic testing

Implement suitable tests for the mathematical and lifecycle behavior of each system.

Test:

- Initialization and shutdown
- Reset and reseeding
- Numerical bounds and stability
- Invalid or extreme parameter values
- State persistence
- Repeated scene creation and destruction
- Deterministic behavior where the implementation permits it
- Resource cleanup
- Behavior under silence and abrupt input changes

Use the existing testing conventions rather than introducing a new testing framework.

Visual evaluation remains essential. Passing mathematical tests does not mean the generated art looks good.

---

# 6. Experimental Workflow

Follow this sequence for each system.

## Phase 1: Establish a baseline

Record the current resonator scene's relevant performance and visual characteristics.

Identify reusable existing components and the minimum new code needed for the experiment.

## Phase 2: Build a minimal prototype

Implement the smallest complete version of the underlying system.

Make it possible to run autonomously without audio so that the system's natural behavior can be evaluated independently.

## Phase 3: Explore parameters

Create a range of meaningful configurations rather than settling on the first working implementation.

Use fixed random seeds when possible to make comparisons fair.

Explore both conservative and aggressive settings.

## Phase 4: Create visual representations

Test the system using more than one geometry or rendering approach where appropriate.

Do not confuse a weak visual representation with a weak underlying simulation.

## Phase 5: Integrate audio and MIDI

Introduce musical influence after the autonomous system is visually compelling.

Compare different mappings and preserve the strongest configurations.

## Phase 6: Evaluate and tune

Inspect the scene in the actual AV Gen application.

Capture comparable screenshots and short clips from representative configurations. Use identical camera positions and comparable timing when comparing variants.

Measure performance and correct obvious visual defects.

## Phase 7: Document findings

Record:

- What was implemented
- What produced the strongest results
- Which parameter ranges were useful
- Which visual representations worked
- How audio and MIDI affected behavior
- What the implementation costs
- What limitations remain
- What should be explored next

## Phase 8: Recommend the next step

Decide whether each experiment should be:

- Developed into a standalone scene
- Integrated as a reusable subsystem
- Combined with the resonator concept
- Retained as a specialized tool for certain scenes
- Reworked with a different mathematical or visual approach
- Abandoned because the visual benefit does not justify the cost

Do not force all three experiments into production if the results do not justify it.

---

# 7. Required Deliverables

Deliver the following artifacts using the repository's existing conventions.

### A. Three independently inspectable experiments

Each should be runnable through the existing AV Gen workflow, with useful defaults and a straightforward way to compare configurations.

Use separate prototype scenes or equivalent isolated harnesses as appropriate.

Do not leave important behavior accessible only through undocumented source-code constants.

### B. Visual experiment gallery

Capture representative screenshots and short recordings where the existing infrastructure permits.

For each system, include at least three meaningfully different configurations.

Include both autonomous behavior and audio-reactive behavior.

Organize the outputs so they can be compared easily.

Do not create screenshots of a blank window, loading state, or failed scene and present them as successful results.

### C. Performance report

Record the hardware, resolution, simulation configuration, frame rate, and relevant CPU/GPU timings.

Identify whether each system appears viable for live use, offline rendering, or both.

Distinguish measured values from estimates.

### D. Implementation notes

Document the mathematical model, numerical method, visual mapping, audio/MIDI mapping, controls, limitations, and known issues for each experiment.

Include the reasoning behind significant design decisions.

### E. Recommendation report

Compare the three systems on:

- Visual distinctiveness
- Musical expressiveness
- Autonomous behavior
- Artistic potential
- Reusability across scenes
- GPU and CPU cost
- Implementation complexity
- Suitability for live performance
- Suitability for offline rendering

Rank the experiments based on observed results rather than theoretical appeal.

Recommend the strongest next development step, including whether any experiment should be combined with the existing resonator system.

### F. Repository hygiene

Keep experimental code clearly identifiable.

Add tests and documentation appropriate to the scope.

Ensure the project still builds and relevant tests pass.

Avoid unrelated cleanup, speculative abstractions, and changes to existing scenes that are not necessary for the experiments.

---

# 8. Definition of Success

The work is not complete merely because all three simulations run.

Each experiment must demonstrate a distinct, compelling visual behavior and a plausible relationship to music.

At least one experiment should produce a result that feels like a meaningful new addition to AV Gen's artistic vocabulary rather than a minor variation on an existing effect.

The final report must be candid. If an experiment is mathematically interesting but visually weak, explain why. If a simple implementation outperforms a sophisticated one, retain the simple implementation.

Do not manufacture positive results to justify the work.

**The final objective is discovery:** find out whether these systems can give AV Gen new kinds of visual life, identify the strongest implementations, and establish what deserves further investment.
