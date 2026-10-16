# AV Gen — Cymatic World: Resonant Shape Language & Geometry Overhaul

(Owner brief, received 2026-10-16, saved verbatim. Extends and governs the motion overhaul (05) and the motion stability pass (07).)

## Mission

Transform Cymatic World from a field of generic, moving organic blobs into a visually compelling, continuously evolving resonant environment inspired by the geometry and behavior of cymatics, Chladni figures, standing waves, modal interference, and vibrating surfaces.

The current scene has made progress toward dynamic geometry, but the surface still reads as a sea of moving blobs. It has motion and displacement without sufficiently strong geometric organization. We need to address the underlying shape-generation model, not merely adjust the material, add more displacement, increase saturation, or smooth the animation.

The goal is to preserve the immersive, full-frame 3D world and existing plasma-inspired art direction while giving the geometry a distinct and recognizable cymatic visual language.

**Core artistic principle: The music should reorganize a coherent resonant world, not simply animate a collection of lumps.**

The finished scene should feature distinct lobes, petals, rings, ridges, channels, branching boundaries, nested forms, interference patterns, and other visually interesting structures that emerge from the interaction of resonant spatial modes.

Those structures should evolve continuously with the music. They must be visible in the actual geometry, not merely implied by color, emission, or post-processing.

This is an implementation and artistic development task. Inspect the existing implementation, develop and compare meaningful prototypes, implement the best approach, validate the result, and deliver a working scene. Do not stop after producing an analysis or proposal.

---

## 1. Preserve the identity of Cymatic World

Before changing anything, inspect the current scene, its history if useful, and the existing Cymatic Plate implementation.

Establish which parts of the current scene should remain intact.

### Preserve

- The current immersive, ground-filling camera composition.
- The established cyan, fuchsia, violet, and deep-navy plasma-inspired palette.
- The existing relationship between geometry, lighting, emission, and audio response wherever it remains useful.
- The current scene registration, project conventions, rendering integration, and live audio/MIDI pathways.
- Any existing performance optimizations and working features that do not interfere with the new geometry model.
- The sense that the viewer is immersed in a continuous world rather than watching a small object or conventional flat visualization.

The existing color treatment is a useful foundation. Do not throw it away to compensate for weak geometry.

### Do not reintroduce

- Floating crystals, shards, organisms, arbitrary props, or decorative objects that distract from the resonant surface.
- Walls, architecture, or a conventional landscape that changes the premise of the scene.
- A distant horizon or cinematic void that wastes the frame.
- A flat, literal recreation of a physical Chladni plate.
- A generic procedural terrain made interesting only through lighting.
- A collection of unrelated visual presets that abruptly replace one another.

This is a resonant environment, not a plate model, a landscape generator, or a collection of abstract assets.

---

## 2. Begin with an implementation audit

Trace the existing geometry pipeline from audio input to the final rendered surface.

Document the answers internally and summarize them in the final report.

Determine:

1. How is the current surface topology generated?
2. Is displacement driven by radial functions, procedural noise, independent oscillators, a simulation grid, or some combination?
3. Which signals control displacement, and are they raw FFT magnitudes, smoothed bands, transient detectors, envelopes, or normalized feature values?
4. How are spatial frequencies, temporal frequencies, and mode phases represented?
5. Are modes persistent across frames, or are parameters, phases, seeds, or pattern identities being regenerated?
6. What controls the relative contribution of broad-, medium-, and fine-scale geometry?
7. How are surface normals computed?
8. What is the current mesh resolution and effective screen-space resolution?
9. Does the renderer already support compute-based fields, storage textures, procedural vertex displacement, or other reusable infrastructure?
10. How does Cymatic Plate create its visually compelling patterns, and which of its implementation techniques are reusable?
11. What causes the current surface to read as blobs rather than distinct resonant structures?
12. What are the current CPU and GPU costs at the target live resolution?

Inspect actual code and capture actual output. Do not assume the current problems can be diagnosed from a scene description alone.

In particular, determine whether the existing geometry model has a structural limitation. If the underlying representation is fundamentally a generic displacement field, do not spend the task polishing that representation indefinitely.

Use Cymatic Plate as an artistic and technical reference, but do not assume its exact implementation is appropriate for a continuous 3D world.

---

## 3. Establish a cymatic shape vocabulary

Develop a deliberate vocabulary of shapes that the geometry system can generate and combine.

The following are target forms, not a checklist requiring each shape to appear independently as a preset.

### Primary structures

**Radial lobes**

- Two-, three-, four-, five-, six-, and higher-order arrangements where appropriate.
- Distinct convex and concave regions separated by meaningful boundaries.
- Symmetry that can evolve or break, rather than a permanently fixed radial flower.
- Variation in lobe width, curvature, amplitude, and orientation.

**Concentric and nested rings**

- Interlocking circular or elliptical ridges and basins.
- Nested rings that intersect with angular modes.
- Partial rings and broken rings created through interference.
- Ring structures that can expand, contract, merge, or become distorted.

**Star and flower patterns**

- Rounded petals, pointed lobes, star-like structures, and intermediate forms.
- Variation between smooth, flowing boundaries and sharper geometric creases.
- Multi-scale patterns in which a large form contains smaller related structures.

**Standing-wave structures**

- Stable bands of positive and negative displacement.
- Clear nodal boundaries.
- Alternating ridges and basins that maintain a recognizable spatial relationship.
- Distinct regions that remain organized under sustained excitation.

**Interference structures**

- Branching curves, saddle regions, crossing ridges, and asymmetric lobes.
- Overlapping modes that create new structures instead of simply adding unrelated bumps.
- Patterns that become more intricate through interaction between modes.

**Traveling and propagating structures**

- Ripples, waves, or phase progression moving across the established pattern.
- Transient disturbances that travel outward or along a ridge network.
- Traveling motion that complements stable standing-wave structures rather than dissolving the entire surface into noise.

**Asymmetric and broken-symmetry structures**

- Off-center lobes, rotated modes, unequal regions, and local distortions.
- Controlled symmetry breaking through mode combinations, spatial weighting, or boundary variation.
- Avoidance of an endless succession of perfect centered mandalas.

The vocabulary should emerge from a coherent mathematical representation. Avoid implementing a separate hard-coded geometry generator for every named form unless experimentation demonstrates that a small number of specialized components are genuinely useful.

---

## 4. Build the geometry around a coherent resonant field

The central technical requirement is a geometry representation that naturally produces organized patterns.

Evaluate at least the following approaches before committing to the implementation.

### Approach A: Analytic spatial modes

Represent the field as a weighted superposition of spatial modes.

Candidate components include:

- Angular harmonics.
- Radial basis functions.
- Standing-wave components.
- Elliptical or anisotropic modes.
- Rotated and offset coordinate frames.
- Higher-order components for fine structure.
- Traveling-wave components with continuous phase progression.

A conceptual field might take the form:

`F(x, y, t) = Σ Aᵢ(t) · Mᵢ(x, y, φᵢ(t))`

Here, each spatial mode `Mᵢ` describes a coherent shape, `Aᵢ(t)` controls its contribution, and `φᵢ(t)` provides continuous temporal phase where applicable.

This is a conceptual model, not a requirement to use a particular formula or coordinate system.

The implementation must ensure that modes interact to create meaningful structures. Simply summing many unrelated sine waves is not sufficient if the resulting surface still looks like random bumps.

Explore combinations of radial and angular components, such as radial oscillations modulated by angular harmonics, as well as other representations supported by the current renderer.

Analytic modes are attractive because they are deterministic, inexpensive, continuous, and easy to inspect or tune.

### Approach B: Simulated wave or resonant field

Evaluate whether a GPU-based field simulation would produce better interactions.

Potential methods include:

- A damped wave equation.
- A stable discrete wave field.
- Modal excitation on a grid.
- A hybrid system in which analytic modes provide the large-scale structure and simulation provides secondary propagation.

A simulation may support compelling transient propagation and local interactions, but it introduces numerical stability, resolution, integration, and performance concerns.

Do not implement a full physical solver merely because the scene is called Cymatic World.

If using simulation, explicitly address:

- Integration stability.
- Time-step independence.
- Damping.
- Boundary behavior.
- Numerical dispersion.
- Grid resolution and aliasing.
- Deterministic or reproducible behavior where practical.
- GPU synchronization and data-transfer costs.

A visually convincing approximation is preferable to an unnecessarily expensive or fragile physical model.

### Approach C: Hybrid resonant representation

Evaluate a hybrid if it offers a clear advantage.

For example:

- Analytic modes define the large-scale pattern.
- A smaller number of additional modes create medium-scale detail.
- A damped field or propagated transient adds local motion.
- A restrained fine-scale component supplies surface vibration.

Keep the system understandable and controllable. Do not build a complex simulation framework before proving that its added behavior materially improves the scene.

### Selection criteria

Choose the simplest approach that produces the best result according to:

1. Shape quality and distinctiveness.
2. Coherence under sustained excitation.
3. Ability to form new patterns through interference.
4. Smooth transitions and persistent mode identity.
5. Response to audio and MIDI.
6. Geometric fidelity at the actual camera distance.
7. GPU cost and live performance.
8. Ease of debugging and artistic tuning.

Prototype early. Compare real renders, not just mathematical diagrams or descriptions.

---

## 5. Make nodal structures a first-class part of the geometry

This is a major artistic requirement.

In cymatic imagery, the relationships between nodal regions are often more compelling than the height of individual peaks. The scene must use these relationships to organize the surface.

Investigate the field's zero crossings, low-amplitude regions, sign transitions, gradients, or other stable features to identify where meaningful boundaries occur.

Use those features to create actual geometric structures, potentially including:

- Raised ridges.
- Recessed channels.
- Sharper creases.
- Smoothly defined boundaries between lobes.
- Alternating convex and concave regions.
- Secondary ridge systems nested inside larger forms.

Do not simply apply an arbitrary threshold to noisy data and create jagged edges. Boundaries must remain spatially coherent and temporally stable.

### Boundary design

The geometric treatment of a boundary should depend on its role and scale.

A large-scale nodal boundary might create a broad, sculpted ridge. A medium-scale boundary might form a sharper crease. A fine-scale boundary should remain subtle enough not to destroy the overall silhouette.

The mapping from field to displacement must be tunable and should avoid sharp discontinuities unless deliberately introduced as a controlled artistic effect.

The result should have a strong visual hierarchy:

- Large forms define the overall composition.
- Medium forms subdivide and elaborate those structures.
- Fine forms contribute restrained detail.

**Acceptance criterion:** The surface should remain visually organized when rendered in a neutral, non-emissive material. If the nodal pattern only becomes visible through cyan and fuchsia coloring, the geometry is not doing enough work.

---

## 6. Establish a clear spatial hierarchy

The current blob-like appearance may partly result from too many spatial scales contributing similar amounts of displacement.

Implement explicit control over at least three spatial scales.

### Large-scale structure

Responsible for:

- Major lobes.
- Broad basins and peaks.
- Primary radial or angular arrangements.
- Large-scale symmetry and asymmetry.
- Overall composition and silhouette.

These forms should dominate the scene's perception.

### Medium-scale structure

Responsible for:

- Secondary petals.
- Intersecting ridges.
- Nested rings.
- Branching structures.
- Local elaboration of the primary forms.

These details should enrich the large forms rather than compete with them.

### Fine-scale structure

Responsible for:

- Restrained vibration.
- Small ripples.
- High-frequency excitation.
- Fine surface undulation.

This layer should never turn the entire surface into a jittering mass of bumps.

Expose the scale contributions separately so they can be inspected and tuned.

Do not assume that adding more octaves, more noise, more vertices, or higher-frequency modes will improve the result.

---

## 7. Create an actual pattern evolution system

The world needs to evolve between distinct spatial configurations without looking like a sequence of unrelated presets.

Implement persistent mode identities and continuous changes in mode contribution.

### Pattern evolution

A musical phrase should be able to transform one coherent configuration into another.

For example:

1. A broad four-lobed structure dominates.
2. An additional angular mode gradually becomes influential.
3. The original lobes deform and split as interference develops.
4. Nested ridges emerge.
5. The pattern settles into a denser configuration.
6. As energy subsides, the structure returns toward a simpler resonant state.

This is an illustrative sequence, not a required fixed animation.

The important behavior is that recognizable structures persist through their transformation.

### Continuity requirements

- Do not regenerate mode phases every frame.
- Do not randomly select new modes every frame.
- Do not reset the entire field on a kick or snare.
- Do not crossfade between unrelated surfaces as the primary evolution mechanism.
- Avoid abrupt changes in mode weights.
- Avoid uncontrolled phase jumps.
- Preserve continuity when modes become more or less influential.
- Use seeded variation only where it has a clear artistic purpose.

Mode activation, deactivation, phase, and amplitude should all be handled deliberately.

A mode that fades in should not cause the entire surface to pop. A mode that fades out should not leave a sudden hole or discontinuity.

Use stable parameter trajectories, smooth envelopes, and continuous phase where applicable.

---

## 8. Audio response: excitation, not indiscriminate displacement

Audio should control how the resonant system is excited. It should not simply be mapped onto arbitrary height changes.

Trace the existing AV Gen audio-analysis and signal-routing system. Reuse its established analysis, signal bus, timeline, modulation, and parameter conventions where appropriate.

Do not introduce a separate audio-analysis architecture without a clear need.

### Bass and low-frequency energy

Should primarily influence:

- Large-scale displacement.
- Broad resonant modes.
- Deep basins and elevated lobes.
- The overall energy of the resonant structure.

A kick may strongly excite these components, but the geometry should retain a coherent form through the transient and its decay.

### Mid-frequency energy

Should primarily influence:

- Medium-scale angular and radial modes.
- Petal subdivision.
- Secondary ridge networks.
- Interference between established structures.

This band can make the geometry more intricate without requiring the whole surface to move more violently.

### High-frequency energy

Should primarily influence:

- Fine-scale vibration.
- Small ripples.
- Restrained high-order modes.
- Subtle surface activity.

High-frequency energy must not overwhelm the primary pattern or create a constant noisy shimmer.

### Transient events

Kicks, snares, and other detected transients can excite selected modes or localized regions.

Use transient excitation to inject energy into the existing resonant field. Let that energy decay naturally or artistically over time.

A transient should not replace the underlying pattern with a new random displacement.

### Sustained energy

Sustained notes and repeated rhythmic activity should be capable of establishing a recognizable resonant configuration.

Mode contributions may depend on the current audio spectrum, envelopes, signal history, and artistic mapping. The mapping should be designed to produce compelling behavior rather than attempting to make every spectral bin correspond to a physical mode.

### Audio mapping requirements

- Use appropriate smoothing and normalization.
- Separate instantaneous attack response from sustained energy.
- Avoid direct dependence on noisy raw FFT values where it causes instability.
- Prevent sudden spectral changes from causing unrelated geometric changes.
- Avoid a one-to-one mapping of individual FFT bins to individual surface oscillators.
- Use MIDI as an additional means of deliberate artistic control if consistent with the scene's existing interaction model.

The audio response should be musical and legible at normal playback speed.

---

## 9. Motion stability and resonant continuity

Treat temporal coherence as a core part of the geometry design, not a finishing effect.

A cymatic world can vibrate rapidly, but the overall pattern should not feel twitchy or unstable.

### Diagnose the actual cause

Determine whether undesirable motion comes from:

- Raw or insufficiently smoothed audio features.
- Mode weights changing too abruptly.
- Phase resets.
- Too many high-order modes.
- Excessive fine-scale displacement.
- Discontinuous geometry evaluation.
- Unstable simulation integration.
- Incorrect normal generation.
- Temporal aliasing or frame-rate-dependent updates.
- A mismatch between displacement frequency and visible mesh resolution.

Fix the cause rather than indiscriminately smoothing the final output.

### Separate temporal behaviors

The system should distinguish:

- **Attack:** rapid excitation when a transient or new sustained component appears.
- **Sustain:** a coherent, stable pattern under ongoing energy.
- **Release:** gradual decay after excitation decreases.
- **Travel:** continuous phase progression or propagation through the field.
- **Fine vibration:** small, rapid motion that does not destroy large-scale organization.

These behaviors should be controllable independently.

### Damping and memory

A resonant structure should retain some memory of recent excitation.

Implement a suitable damped response for mode energy or field state. Excitation should accumulate and decay according to a deliberate response model, rather than being reconstructed from the instantaneous audio frame alone.

Do not make every mode share an identical attack, decay, and frequency response if that produces mechanical-looking motion.

### Preserve expressive energy

Do not simply slow everything down.

The scene must remain energetic during dense music and sharp transients. Stability comes from coherent structure, persistent modes, controlled excitation, and appropriate damping—not from eliminating movement.

---

## 10. Geometry quality and screen-space fidelity

The surface must look sculpted and intentional at the actual camera distance.

Inspect the current mesh resolution and tessellation strategy before changing it.

### Requirements

- Smooth large-scale silhouettes.
- Adequate vertex density for the finest meaningful geometric features.
- Stable normals and consistent lighting.
- No conspicuous triangulation, faceting, or jagged crater rims.
- No excessive foreground blur caused by insufficient geometric or shading detail.
- No high-frequency displacement beyond what the geometry can represent.
- No unstable shading caused by discontinuous normal estimates.

### Important distinction

Increasing mesh density is not automatically the correct solution.

First determine the highest spatial frequencies the scene actually needs. Then make sure the mesh or procedural representation can represent those frequencies at the camera's projected resolution.

If the renderer supports appropriate procedural displacement or tessellation, evaluate it. Otherwise, improve the mesh density selectively and measure the cost.

Consider level of detail or screen-space filtering where useful. Fine details that cannot be represented cleanly should be attenuated rather than allowed to alias.

### Normals

Verify that normals correspond to the deformed surface.

Compare:

- Existing normal generation.
- Analytic derivatives, if available.
- Finite-difference methods at an appropriate resolution.
- Any existing engine-supported surface-normal path.

Choose the most reliable method that fits the rendering architecture and performance budget.

Do not introduce expensive normal reconstruction if the current renderer already provides an adequate solution.

---

## 11. Art direction: plasma surface, not generic procedural terrain

Once the geometry is convincing, refine the existing material response around the new resonant structure.

Retain the established cyan/fuchsia/violet/deep-navy direction.

### Material hierarchy

Use the resonant field to influence the material in ways that reinforce the geometry.

Potential relationships include:

- Cyan or cyan-white highlights along selected ridges and nodal boundaries.
- Fuchsia or violet coloration within deeper lobes and hollows.
- Restrained emission around strongly excited regions.
- Grazing illumination that reveals surface curvature.
- Subtle traveling highlights associated with propagating disturbances.
- Fine glints that respond to high-frequency excitation without becoming visual noise.

These are art-direction options, not rigid color assignments for every field value.

The material should reinforce the surface's topology and motion.

### Lighting

Use lighting to reveal the geometry:

- Preserve directional grazing light where effective.
- Maintain sufficient contrast between ridges and basins.
- Avoid flattening the surface with excessive fill illumination.
- Avoid crushing the entire scene into black with isolated neon highlights.
- Ensure quiet musical passages remain visually rich.
- Avoid using bloom to conceal weak geometry or poor normals.

### Emission and bloom

Treat emission as a consequence of the field and its excitation, not a substitute for surface detail.

Keep bloom controlled. Stronger excitation may increase emission, but it should not wash out the spatial pattern.

Do not return to a color-only implementation. The geometry must remain compelling when the material is disabled.

---

## 12. Camera and composition

Preserve the immersive full-frame surface composition.

The camera should reveal the relationships between large and medium structures without making the scene look like a tiny object viewed from far away.

Evaluate:

- How much of the frame is occupied by the primary pattern.
- Whether the viewer can recognize individual lobes and ridges.
- Whether foreground geometry remains readable.
- Whether the framing supports large structures without cropping every meaningful form into abstraction.
- Whether the composition benefits from subtle camera movement or should remain stable.

Do not add aggressive camera movement to manufacture interest.

The geometry itself must provide the visual evolution.

A restrained camera adjustment is acceptable if it improves the perception of the new spatial forms, but it must not distract from the resonant behavior or introduce unnecessary motion sickness.

---

## 13. Prototyping and implementation sequence

Work in stages so that the quality of the underlying geometry can be evaluated independently from audio and material effects.

### Stage 1 — Audit and baseline

- Inspect Cymatic World and Cymatic Plate.
- Capture the current scene at normal playback speed.
- Record current performance and rendering settings.
- Identify the primary causes of the blob-like appearance.
- Document reusable infrastructure and likely implementation constraints.

### Stage 2 — Geometry-only prototypes

Create a small number of competing prototypes using a neutral material.

At minimum, compare:

1. A structured analytic modal field.
2. A nodal-boundary-focused representation.
3. A hybrid representation if it offers a meaningful alternative.

Each prototype should demonstrate several distinct patterns, including radial, angular, nested, and asymmetric structures.

Capture the prototypes from the actual scene camera.

Choose based on recognizable shapes, spatial hierarchy, continuity, and visual quality—not on code elegance alone.

Do not proceed with a weak prototype merely because it is the easiest to implement.

### Stage 3 — Shape and interference system

Implement the selected representation.

- Build the primary spatial modes.
- Add secondary modes.
- Establish nodal boundaries.
- Introduce controlled symmetry breaking.
- Verify that interference produces coherent, interesting forms.
- Ensure the pattern remains recognizable under sustained excitation.

At this stage, the geometry should already look substantially more cymatic than the original surface.

### Stage 4 — Pattern evolution

Implement continuous mode weighting, phase progression, and persistent pattern identity.

Demonstrate transitions between distinct spatial configurations without hard resets or unrelated preset swaps.

Capture the geometry in motion at normal speed. Static screenshots alone are insufficient to evaluate temporal coherence.

### Stage 5 — Audio integration

Connect audio features to mode excitation and energy.

Start with a small, deliberate mapping. Add complexity only when it produces a visible and musically meaningful improvement.

Verify the behavior with isolated frequency ranges and transient events before evaluating a dense musical mix.

### Stage 6 — Stability pass

Tune attack, sustain, release, damping, and high-frequency contributions.

Fix the actual source of twitchiness. Verify that the world remains expressive without becoming noisy.

### Stage 7 — Geometry and rendering quality

Correct mesh resolution, normals, screen-space detail, and aliasing.

Do not compensate for poor geometric representation with blur, excessive bloom, or stronger color contrast.

### Stage 8 — Final art pass

Reintroduce and tune the plasma material, lighting, emission, and bloom around the improved geometry.

Preserve the existing visual identity unless evidence from rendered comparisons demonstrates a clear reason to change it.

### Stage 9 — Performance and integration

Measure GPU and CPU cost at the target live configuration.

Optimize the new implementation without removing the qualities that make it visually compelling.

Follow the existing project architecture, testing practices, scene-registration conventions, and coding standards.

### Stage 10 — Final validation

Run the relevant build and tests. Inspect the final scene in motion and provide a clear summary of implementation decisions, performance, limitations, and follow-up opportunities.

---

## 14. Required validation scenarios

Evaluate at least the following conditions:

1. **Silence or near silence:** The surface retains a deliberate, attractive baseline shape without meaningless autonomous chaos.
2. **Sustained bass:** Broad structures become prominent and remain coherent.
3. **Kick-heavy rhythm:** Large-scale excitation is strong but controlled, with an audible decay rather than an instantaneous reset.
4. **Snare and transient activity:** Secondary structures react distinctly without destabilizing the entire field.
5. **Sustained midrange:** Medium-scale modes become legible and develop meaningful interference.
6. **High-frequency material:** Fine structures animate without overwhelming the main forms.
7. **Dense full-band music:** All spatial scales coexist without collapsing into visual noise.
8. **A quiet-to-loud transition:** The pattern grows more intricate and energetic through continuous evolution.
9. **A loud-to-quiet transition:** Excitation dissipates naturally while the underlying structure remains coherent.
10. **Different musical material:** The scene does not depend on one particular track to look good.

Use actual audio input where practical. Include deterministic or reproducible test signals for diagnosis.

For each scenario, inspect both the geometry and the complete rendered scene.

---

## 15. Performance constraints

This is a live audiovisual instrument, so visual quality must be balanced against frame time.

Use the project's existing target live configuration and performance budgets as the source of truth.

The recent AV Gen baseline on Apple Silicon targets a 120 Hz live rendering cadence, corresponding to approximately 8.33 ms per frame. Treat this as a reference target, not an assumption that every scene must already meet it.

Measure:

- CPU frame time.
- GPU frame time.
- Geometry update cost.
- Field evaluation or simulation cost.
- Normal-generation cost.
- Memory allocation and bandwidth where relevant.
- Performance at the actual target resolution.
- Behavior during high excitation and dense musical passages.

Prefer GPU-resident field evaluation where it makes architectural and performance sense.

Avoid unnecessary CPU-to-GPU transfers, per-frame allocations, and expensive full-resolution simulation when a lower-resolution field with suitable reconstruction would suffice.

If a more expensive technique is worthwhile for offline rendering but not live performance, document that distinction and use existing engine conventions for quality tiers where available.

Do not introduce a new settings system or quality menu without a concrete need.

---

## 16. Debugging and inspection tools

Add or reuse lightweight diagnostic visualization where it materially accelerates development.

Useful debug views include:

- The signed resonant field.
- Positive and negative displacement.
- Nodal boundaries.
- Individual spatial modes.
- Large-, medium-, and fine-scale contributions.
- Mode amplitudes and phase.
- Audio excitation envelopes.
- Transient excitation and decay.
- Surface normals.
- Geometry density or screen-space detail.
- Simulation stability indicators, if simulation is used.

These views should help identify why a pattern is weak, noisy, unstable, or visually unstructured.

Follow existing AV Gen debugging and UI conventions. Do not build a large standalone editor as part of this task.

Where practical, make it possible to isolate one mode or spatial scale at a time. This will make artistic tuning and regression diagnosis substantially easier.

Remove temporary instrumentation that is no longer useful, or retain it behind established debug mechanisms.

---

## 17. Explicit anti-goals

Do not:

- Treat the task as another material-only art pass.
- Simply increase the amplitude of the existing displacement.
- Add more random noise to make the surface look complex.
- Add dozens of independent oscillators without a clear spatial organization.
- Map each FFT bin directly to a separate arbitrary deformation.
- Allow high-frequency motion to dominate the surface.
- Make every feature move independently.
- Regenerate the spatial pattern every frame.
- Use abrupt random shape changes to create variety.
- Solve every visual problem by increasing mesh density.
- Solve every motion problem by smoothing the final geometry.
- Add a physically accurate simulation that offers little visible benefit.
- Introduce unrelated props, architecture, crystals, organisms, or decorative scene elements.
- Discard the current plasma art direction without evidence that it is holding back the geometry.
- Accept a result that only looks compelling in a still frame.
- Claim success based solely on unit tests or a successful build.

---

## 18. Definition of done

The task is complete only when all of the following are demonstrated.

### Shape quality

- The surface contains recognizable, appealing cymatic structures.
- Radial lobes, angular modes, nested rings, ridges, and nodal boundaries can be clearly perceived.
- Large-scale forms dominate, with medium and fine detail in supporting roles.
- The geometry is compelling under a neutral material.
- The result no longer reads primarily as a sea of generic blobs.

### Pattern evolution

- Multiple modes combine into new forms through interference.
- The surface transitions between configurations without arbitrary hard resets.
- Sustained excitation produces coherent structures.
- Asymmetric forms and controlled symmetry breaking prevent visual monotony.
- The scene remains a continuous environment rather than a collection of unrelated presets.

### Motion

- Geometry responds meaningfully to audio.
- Broad and fine-scale movement are appropriately separated.
- Transients excite the existing resonant system.
- Excitation persists and decays according to a deliberate response model.
- Motion remains expressive without becoming twitchy or noisy.

### Rendering

- Geometry and normals are stable and sufficiently detailed at the target camera distance.
- Nodal structures are visible in the actual surface.
- The established plasma palette and lighting complement the geometry.
- Emission and bloom do not conceal weak shapes.
- The framing remains immersive.

### Engineering

- The chosen representation is justified by actual prototype comparisons.
- Performance has been measured at the target live configuration.
- Relevant builds and tests pass.
- No unnecessary architectural duplication or heavyweight simulation has been introduced.
- Debugging and artistic tuning remain manageable.

### Evidence

Provide before-and-after captures, ideally including short clips of the geometry-only prototype and final scene at normal playback speed.

Document performance measurements and the most important implementation decisions.

If a requirement cannot be satisfied, explain the limitation and provide the best validated result rather than marking it complete without evidence.

---

## 19. Final report

At completion, report:

1. The root causes of the original blob-like appearance.
2. The geometry representations prototyped and compared.
3. The selected representation and why it won.
4. How nodal structures and spatial modes generate the new shape vocabulary.
5. How pattern evolution and phase continuity work.
6. How audio features excite the geometry.
7. What was done to improve motion stability.
8. What changed in mesh resolution, normals, or screen-space detail.
9. Before-and-after visual evidence.
10. CPU/GPU performance at the target live configuration.
11. Relevant build and test results.
12. Known limitations and the most valuable next iteration.

Be explicit about what was actually implemented and verified. Do not claim a physical cymatic simulation if the implementation is an artistic modal approximation.

## Final artistic direction

**Build a world of resonant forms, not a field of animated blobs.**

The viewer should be able to perceive a large structure, follow its lobes and boundaries, see smaller structures emerge inside it, and watch the whole pattern transform as the music changes.

The surface should feel as though it possesses an internal resonant logic. Geometry, motion, audio response, and lighting should all express that same logic.

Cymatic Plate is the reference for the appeal of its organized patterns. Cymatic World should carry that appeal into a continuous, richly dimensional environment that can sustain a full musical performance.

Prioritize meaningful shapes, spatial hierarchy, and coherent evolution over raw geometric complexity.
