> Owner's brief, saved verbatim on 2026-10-18.

AV Gen — RD Pins + Phase Hall: Music Reactivity, Lighting & Art Direction Pass

Mission

Perform a substantial visual art pass on the existing RD Pins and Phase Hall generative scenes.

Both scenes currently have a recognizable visual foundation, but neither communicates musical energy strongly enough. They appear to progress through different colors and subtly changing shapes, rather than responding convincingly to the music.

We need to elevate them into two distinct, polished, projection-worthy audiovisual experiences.

This is an art-direction and implementation task, not a request to replace the existing scenes with unrelated concepts. Preserve their strongest existing characteristics while substantially improving musical expressiveness, lighting, depth, motion, and visual identity.

Primary objectives

1. Make audio reactivity immediately perceptible when listening to music.
2. Introduce richer, layered motion with relationships between bass, percussion, mids, and high frequencies.
3. Implement compelling lighting, luminous materials, bloom, and controlled HDR highlights.
4. Transform Phase Hall from an architectural room into an immersive spherical environment viewed from the inside.
5. Make RD Pins feel like a living, illuminated topographic interference field rather than a flat animated pattern.
6. Ensure both scenes remain visually interesting during quiet passages, sustained notes, dense mixes, and rhythmic sections.
7. Preserve strong visual composition and intentional color palettes throughout the entire animation.
8. Maintain the existing real-time rendering budget, especially with audio and MIDI enabled.

Do not solve the problem by simply increasing animation speed, applying more random noise, cycling colors faster, or making every parameter react to the same audio envelope.

⸻

Part 1 — Audit the Existing Implementations

Before changing code, inspect both scenes in their current implementation.

Identify:

* How geometry and surface displacement are generated.
* How the scenes receive audio-analysis data.
* Which audio features are available and reliable.
* How parameters are modulated and how MIDI input reaches the scenes.
* How materials, emission, lighting, and post-processing currently work.
* Which features are responsible for the existing visual identity.
* Which operations are expensive on the GPU or CPU.
* Whether either scene has controls that appear exposed but do not meaningfully affect the rendered result.

Establish a reproducible baseline for both scenes.

Capture representative frames and, where possible, short video sequences at:

* Quiet or sparse audio.
* Sustained low-frequency energy.
* Strong rhythmic percussion.
* Dense broadband audio.
* A transition or major musical accent.

Record frame rate, GPU time, CPU time, and resolution under the same conditions that will be used for final validation.

Do not begin by rewriting the scene architecture. Make the smallest structural changes that can produce the intended visual result.

⸻

Part 2 — Shared Audio-Reactivity Design

2.1 Give different frequency ranges different visual responsibilities

Both scenes should use the available audio-analysis and signal-modulation infrastructure, but they should not use an identical mapping.

Where the signal bus provides suitable features, build the following visual roles:

Sub/bass energy

* Drives large-scale displacement and broad waves.
* Produces substantial but controlled luminous pulses.
* Creates traveling surface disturbances and large-scale expansion/contraction.
* Can trigger deeper changes in the overall field's shape.

Kick and low-mid transients

* Produce localized impacts, expanding rings, traveling waves, or momentary surface deformations.
* Give the scene a strong rhythmic sense of physical energy.
* Should be distinct from the smoother response to sustained bass energy.

Midrange energy

* Controls medium-scale ripples, phase interactions, surface undulations, and secondary motion.
* Can affect the spacing, curvature, or orientation of existing structures.

High-frequency energy

* Produces fine surface ripples, narrow luminous accents, spark-like glints, and high-frequency interference.
* Should add detail without turning the entire image into noise.

Transient/onset strength

* Drives short, clearly visible accent events.
* May create traveling disturbances, brief flashes, or expanding waves.
* Should respond to actual musical attacks rather than continuously retriggering from a sustained note.

Overall energy

* Influences the scene's broad intensity, density, and motion envelope.
* Must not override the more specific frequency-band mappings.

Use the actual signal features available in AV Gen. Do not assume dedicated kick, snare, or onset detectors exist without checking.

If a feature is unavailable, derive a suitable approximation from existing analysis data rather than introducing an entirely separate audio-analysis subsystem.

2.2 Build a proper response envelope

Avoid directly multiplying raw audio samples or rapidly fluctuating analysis values into geometry and lighting.

Use the existing modulation system where practical, with separate attack and release behavior.

Desired characteristics:

* Fast response to transient attacks.
* Smooth recovery after impacts.
* Slower response to sustained energy.
* A small amount of hysteresis to prevent flicker.
* Independent response times for geometry, illumination, and fine detail.

Low-frequency energy should be able to sustain a broad visual deformation. A high-frequency transient should be capable of producing a sharp, brief glimmer.

Do not allow slow smoothing to erase percussion. Do not allow fast modulation to make sustained notes flicker.

Use normalized and bounded signals, with appropriate gain staging and sensible defaults across different input levels.

2.3 Make reactivity obvious without making it crude

The visual response should be discernible at a glance.

A strong kick should produce an identifiable event. A bass-heavy section should visibly reshape the scene. A bright transient should generate a crisp light response. A sustained musical passage should continue to influence the visual field without requiring repeated attacks.

However:

* No constant screen-wide flashing.
* No uncontrolled geometry explosions.
* No indiscriminate random movement.
* No single envelope driving all parameters.
* No excessive sensitivity to quiet background noise.
* No movement so large that the scene loses its original character.

Use layered responses at different spatial scales and time constants.

2.4 MIDI interaction

Inspect the existing MIDI modulation path and use it rather than inventing a parallel system.

Where the scene's existing parameter architecture allows it, expose meaningful MIDI-controllable parameters for:

* Overall response intensity.
* Large-scale movement or displacement.
* Transient impact strength.
* Luminous intensity.
* Color-palette selection or palette position.
* Fine-detail activity.

Do not add a large bank of arbitrary controls. Prioritize a compact set of parameters that materially changes the performance.

Verify that MIDI controls remain useful while audio reactivity is active, with clear and intentional interactions between the two.

⸻

Part 3 — RD Pins: Living Topographic Interference

3.1 Preserve the existing identity

RD Pins currently features dense, flowing, maze-like contour lines with an illuminated appearance.

This is its strongest feature. Keep the recognizable flowing topographic pattern, rounded contours, nested paths, and graphic quality.

Do not replace it with generic particles, floating spheres, random polygonal geometry, or a conventional terrain scene.

The goal is to make the existing pattern feel like a dynamically excited physical field.

The contour lines should remain coherent and attractive even when the music becomes energetic.

3.2 Add genuine layered surface motion

The current effect needs more than gradual shape morphing.

Introduce several distinct, spatially coherent forms of motion.

A. Broad traveling waves

Create broad waves that travel across the field.

They should:

* Move in identifiable directions.
* Overlap and interfere with one another.
* Distort the existing contour pattern.
* Respond to bass energy and musical accents.
* Produce changes in local spacing and curvature.

Where feasible, use analytically defined wave fields or other inexpensive procedural functions instead of adding large numbers of independently simulated objects.

B. Secondary ripples

Layer smaller ripples over the broad waves.

These should travel at different speeds and wavelengths, creating nested patterns of compression and expansion.

Use midrange energy to modulate their strength and high-frequency energy to increase fine-detail activity.

The resulting pattern should remain structured. Avoid a sea of uncorrelated noise.

C. Localized impact waves

Introduce localized disturbances triggered by strong musical transients.

Each disturbance should have:

* A defined origin.
* A rapidly expanding or traveling wavefront.
* A controlled decay.
* A visible influence on nearby contour lines.
* A luminous accent that follows or briefly precedes the disturbance.

Use a small bounded collection of active events, or an equivalent GPU-friendly representation. Do not spawn unbounded objects or allocate resources every frame.

Impact positions may be deterministic or drawn from a controlled spatial distribution. They should feel intentional rather than randomly scattered.

D. Interference

Allow waves to reinforce and cancel each other.

Create moments where:

* Contours bunch together.
* Narrow luminous channels emerge.
* Nested shapes expand.
* Curved paths merge and separate.
* Local regions become temporarily brighter or more densely patterned.

The pattern should evolve continuously, but not uniformly. Some areas should be active while others remain relatively calm.

E. Subtle depth and parallax

Retain the graphic readability of the contour field while introducing stronger depth cues.

Investigate:

* Small but meaningful surface-height variations.
* Grazing-angle highlights.
* Controlled perspective changes.
* A slight sense of the field curving away from the viewer.
* Subtle parallax, where supported by the existing camera and scene design.

Do not turn RD Pins into conventional rolling hills. It should remain an abstract, stylized interference field.

3.3 Make the contour lines emit and catch light

The current palette changes are appealing, but the lines need more visual depth and luminous character.

Implement a deliberate material and lighting treatment.

Desired qualities:

* Bright, fine contour-line cores.
* Softer luminous edges.
* Darker or more subdued channels between lines.
* Occasional specular glints traveling along curves.
* Short-lived highlights at wave intersections.
* Localized increases in brightness around strong musical impacts.
* Controlled variation in line intensity rather than uniform emission.

Use existing procedural geometry, line meshes, shader functions, or material properties as appropriate to the implementation.

If the contours are generated in a shader, prefer procedural emission and highlight calculations where they can achieve the same result more cheaply than additional geometry.

Avoid making every line equally bright. The contrast between dark channels and selected luminous paths is essential.

3.4 Introduce traveling light

Add a restrained but noticeable traveling-light effect.

This may take the form of luminous bands or moving highlight envelopes that follow the existing contours and interference structures.

Behavior:

* High-frequency activity increases fine glints and local sparkle.
* Bass transients can trigger broader illumination sweeps.
* The underlying pattern remains visible during all lighting events.
* Highlights vary in intensity and duration.
* Some light movement is continuous; some is tied to musical events.

Avoid obvious repeating conveyor-belt patterns, uniformly spaced marching dashes, or effects that resemble a generic audio visualizer.

3.5 RD Pins lighting and post-processing

Add or improve:

* Bloom around the brightest contour lines.
* HDR emission with a useful range between subdued and intensely luminous.
* Local contrast between bright ridges and dark channels.
* Subtle depth-dependent atmospheric separation, if compatible with the scene.
* Optional restrained lens-like diffusion, if already supported by the renderer.

Bloom must enhance the contours rather than erase their fine structure.

Thin lines should remain sharp at native resolution. Their glow should spread outward without filling every dark gap.

Avoid excessive chromatic aberration, heavy vignette, strong film grain, and generic cinematic overlays.

The scene should look like an intentionally designed generative artwork, not a preset post-processing stack.

3.6 RD Pins palette behavior

Preserve the existing range of palettes as a starting point:

* Warm copper, amber, and deep burgundy.
* Electric blue and acid yellow.
* Violet, magenta, and warm gold.
* Deep green or teal with pale mint highlights.

The palette should be treated as a coordinated set of material, emission, and background colors rather than a simple hue rotation.

Each palette needs:

* A dark foundation.
* A dominant contour color.
* A brighter accent color.
* A controlled highlight color.
* A distinct bloom character.

Allow palette position or selection to be modulated, but avoid uncontrolled continuous rainbow cycling.

Color transitions should feel intentional and harmonically coherent. The scene should remain recognizable while changing palettes.

⸻

Part 4 — Phase Hall: An Immersive Luminous Sphere

4.1 Remove the room entirely

The current Phase Hall effect has compelling luminous vertical panels and a strong material identity, but the architectural room surrounding them is visually unconvincing.

Remove the conventional room composition.

Specifically:

* No rectangular room.
* No conventional floor plane.
* No visible ceiling grid.
* No standard architectural corners.
* No repeated room-wall perspective.
* No generic industrial corridor.

Instead, construct a vast spherical field of illuminated panels viewed from the inside.

The spherical environment should completely surround the viewer, with the effect extending across the horizon and overhead.

This is an abstract generative environment, not a building or a recognizable physical location.

4.2 Spherical layout

The viewer occupies the interior of a large sphere.

Arrange the existing Phase Hall material and panel effect across the interior surface.

Preserve the defining visual characteristics:

* Tall, luminous vertical bands.
* Fine repeated surface detail.
* Strong dark/light contrast.
* Rich saturated colors.
* The sense of light traveling through or across a structured surface.

Adapt the existing geometry or shader to a spherical coordinate system, rather than simply placing the existing rectangular room inside a sphere.

Potential implementation:

* Map the panel field to spherical directions.
* Use a procedural coordinate system for the panel pattern.
* Maintain coherent panel spacing across the sphere.
* Handle the poles and seams without obvious pinching, discontinuities, or singularities.
* Use an appropriate inside-facing surface or procedural background treatment.
* Keep the camera comfortably inside the sphere, far enough from the surface to avoid clipping and extreme distortion.

The sphere should feel enormous, with a surface that wraps around the viewer rather than a small ball immediately surrounding the camera.

Do not create a visible wireframe sphere or an outlined spherical cage.

4.3 Prevent the sphere from looking like a room with curved walls

This distinction is essential.

Avoid:

* A clearly visible floor-to-wall boundary.
* Architectural support columns.
* Rectangular openings.
* Window frames.
* Ceiling beams.
* A conventional vanishing point.
* A visible circular outline that makes the environment read as a dome.

The field should feel continuous and immersive.

The viewer should perceive a luminous spherical membrane or a vast interior surface, not a planetarium room or an architectural dome.

The vertical panel structures may curve with the spherical surface, but should retain enough directional consistency to preserve the original visual effect.

Use subtle variations in panel orientation, spacing, and curvature to reinforce the spherical form without making the pattern chaotic.

4.4 Make the surface dynamically responsive

The current effect is visually appealing but too static.

The surface should exhibit a layered combination of motion:

Broad deformation

* Large, slow undulations of the spherical membrane.
* Bass-driven expansion and contraction.
* Traveling bands of displacement.
* Localized deformation around strong transients.

Panel motion

* Subtle lateral shifts or phase offsets between neighboring panels.
* Changes in panel curvature.
* Controlled bending or twisting of the luminous bands.
* Rhythmic changes in the spacing between bands.

Surface interference

* Traveling phase patterns.
* Local compression and expansion of repeated structures.
* Overlapping waves that alter the apparent density of the panels.
* Temporary pockets of brighter or darker illumination.

Fine activity

* Small surface ripples.
* Narrow glimmers traveling along panel edges.
* Brief highlights triggered by high-frequency energy.
* Localized shimmer during dense musical passages.

These layers must operate at different spatial and temporal scales.

Avoid making the entire sphere wobble as a single rigid object. The viewer should feel as though energy is traveling through a responsive surface.

4.5 Create a strong vertical-light identity

The existing vertical bands are one of Phase Hall's best qualities.

Push them further.

Introduce:

* Bright, narrow luminous cores.
* Broader, softer light surrounding the cores.
* Dark spaces between neighboring bands.
* Traveling highlight streaks.
* Pulses that propagate across groups of panels.
* Selective intensity changes that move around the sphere.
* Brief, controlled flashes at musical accents.

The vertical structures should feel like luminous material, not flat colored stripes pasted onto a wall.

Use material emission, local lighting, and specular response where appropriate.

If actual lighting is too expensive, prioritize shader-based illumination, emission, and carefully controlled bloom. Do not add large numbers of dynamic lights merely to simulate the effect.

4.6 Phase Hall must be brighter

Compared with its current appearance, Phase Hall should feel significantly more luminous and energetic.

However, brighter does not mean uniformly washing the entire scene in white.

Create a hierarchy:

1. Intense narrow highlights.
2. Bright saturated panel regions.
3. Softer colored illumination spreading into adjacent areas.
4. Deep, saturated shadows and dark gaps.

Use HDR emission and exposure-aware bloom.

Allow musical peaks to push selected regions into stronger luminous states while preserving color separation.

The darkest areas should remain dark enough to frame the brighter structures.

Avoid clipping entire panels to white. The color of the underlying material must remain visible.

4.7 Spherical lighting and spatial composition

The sphere should have a deliberate visual composition.

Do not distribute equally bright panels uniformly across every visible surface.

Introduce gentle variation through:

* Large-scale intensity gradients.
* Moving zones of illumination.
* Groups of panels with different phase offsets.
* Occasional bright regions that attract the eye.
* Contrasting quieter areas.

The overall image should remain balanced from a fixed performance camera.

The upper field and peripheral regions should remain interesting, rather than concentrating all visual activity in the center.

If the camera supports movement, the environment may reveal additional structure through modest camera motion. Do not depend on camera movement to make the scene work.

The default fixed-camera view must already be compelling.

⸻

Part 5 — Phase Hall Color Direction

Give Phase Hall a richer and more luminous palette system.

Build on the existing cyan, teal, orange, red, violet, and magenta material variations, but treat them as deliberately authored palettes.

Suggested palettes:

A. Polar Glass

* Deep midnight blue foundation.
* Electric cyan and pale icy-blue luminous cores.
* Small amounts of white-blue highlight.
* Restrained cool bloom.

B. Molten Signal

* Near-black burgundy foundation.
* Saturated crimson and deep red panels.
* Orange and amber highlights.
* Hot, concentrated luminous accents.

C. Ultraviolet

* Dark indigo foundation.
* Deep violet and saturated purple bands.
* Magenta illumination.
* Occasional pale lavender highlights.

D. Acid Cathedral

* Dark blue-black foundation.
* Electric yellow-green accents.
* Muted olive or green intermediate tones.
* Restrained high-intensity highlights.

E. Sunset Interference

* Deep violet foundation.
* Orange and coral bands.
* Warm pink and magenta transitions.
* Pale peach highlights.

F. Glacial Teal

* Very dark blue-green foundation.
* Saturated teal structures.
* Pale mint and icy-white luminous cores.
* Carefully controlled cool bloom.

These are starting directions, not a requirement to add a large preset menu.

Each palette should define the base material, luminous cores, secondary colors, and highlight behavior coherently.

Use smooth transitions between palettes where appropriate. Avoid a continuous rainbow gradient across the entire sphere unless explicitly selected as an artistic mode.

Audio may influence palette position or highlight emphasis, but color changes should not become so rapid that they obscure the panel structure.

⸻

Part 6 — Shared Bloom, HDR, and Post-Processing

6.1 Inspect the existing renderer first

Determine which HDR and post-processing features already exist in AV Gen.

Use the existing pipeline where possible. Do not create a separate renderer or duplicate the post-processing stack for these scenes.

The desired result is controlled luminous diffusion around bright features, with preservation of fine geometry and saturated color.

6.2 Bloom quality

Implement or improve bloom using the most appropriate existing mechanism.

A multiscale or downsampled approach is preferable to a large full-resolution blur if it produces the desired quality at lower cost.

Bloom must:

* React to HDR brightness rather than treating every pixel equally.
* Produce soft halos around bright lines and panels.
* Preserve narrow luminous features.
* Avoid washing out the background.
* Maintain a useful balance between small glints and broader illumination.

Expose a compact set of useful controls if the existing parameter system supports them:

* Bloom intensity.
* Bloom radius or scale.
* Emission intensity.
* Audio-reactive light response.

Use sensible defaults. Do not expose every shader constant to the performer.

If the renderer already has suitable bloom, tune and integrate it rather than implementing a second version.

6.3 Protect image quality

Do not rely on bloom to compensate for weak materials or uninteresting motion.

The base scene must remain compelling with bloom disabled.

Keep contour lines and panel boundaries legible. Avoid excessive glare, large featureless white regions, crushed shadows, or unstable exposure.

Use the existing tone-mapping pipeline and account for the final output transform when tuning emission and bloom.

Avoid adding expensive screen-space effects that provide only marginal visual improvement.

⸻

Part 7 — Performance and Implementation Constraints

Both scenes currently struggle to maintain 30 FPS under some audio/MIDI-enabled projection configurations.

The art pass must improve visual quality without making this worse.

7.1 Preserve the real-time budget

Use the existing AV Gen rendering architecture.

Prefer:

* Analytic procedural waves.
* Shader-based deformation.
* Reusable GPU resources.
* Bounded event buffers.
* Existing audio-analysis features.
* Existing modulation and parameter infrastructure.
* Efficient low-resolution bloom passes.
* Stable allocation-free per-frame updates.

Avoid:

* Per-frame geometry reconstruction where shader displacement is sufficient.
* Unbounded particle or transient-event creation.
* Excessive full-resolution post-processing.
* Large numbers of dynamic lights.
* Redundant CPU calculations for data the GPU already has.
* Additional audio analysis when an equivalent signal is already available.
* Synchronization that forces the CPU to wait for the GPU.

If a technique is visually attractive but too expensive, find a cheaper approximation before removing the effect entirely.

7.2 Establish quality and performance together

Measure both scenes with:

* Audio disabled.
* Audio enabled.
* Audio and MIDI enabled.
* The intended projection resolution.
* The same camera and settings used for visual comparisons.

Identify the actual bottlenecks instead of assuming the new shader or bloom is responsible.

If there is a meaningful cost increase, profile it and optimize the offending pass or calculation.

Do not silently reduce resolution, disable important effects, or simplify the artwork to achieve a benchmark.

Where a scalable quality setting already exists, use it appropriately and document the tradeoff.

The minimum acceptance target is a stable 30 FPS in the intended live configuration, with no major visual regression from performance compromises.

If the target hardware cannot sustain this, report the measured limitation and the specific bottleneck rather than claiming success.

⸻

Part 8 — Visual Validation

Use actual rendered output to evaluate the work.

Do not judge success solely from source code, parameter names, screenshots of editor controls, or a shader that compiles.

Produce before-and-after comparisons for both scenes.

For each scene, capture:

* A quiet passage.
* A sustained bass passage.
* A percussion-heavy passage.
* A dense musical passage.
* A strong transient.
* Representative color-palette transitions.

Capture short sequences as well as still frames. The central requirement is improved animation and musical responsiveness, which still images alone cannot demonstrate.

Use the same input recordings or repeatable test signals wherever possible.

Verify that:

* The response is visibly different across the test passages.
* Each frequency band has a discernible visual role.
* Motion is continuous and spatially coherent.
* Strong accents produce identifiable visual events.
* Bloom enhances the imagery without obscuring it.
* The two scenes retain distinct identities.
* The scene remains attractive during quieter sections.
* No obvious seams, poles, clipping, or camera artifacts appear in the Phase Hall sphere.
* The sphere does not read as a conventional room.
* RD Pins retains its topographic contour character.

Do not use a single attractive screenshot as evidence that the entire animation is successful.

⸻

Part 9 — Acceptance Criteria

RD Pins

* [ ] The existing topographic contour identity remains recognizable.
* [ ] Broad traveling waves visibly deform the pattern.
* [ ] Secondary ripples create layered motion.
* [ ] Musical transients produce localized impact waves.
* [ ] Interference creates evolving contour density and curvature.
* [ ] Fine highlights and glimmers travel across the surface.
* [ ] HDR emission and bloom create a clear luminous hierarchy.
* [ ] Palettes are coherent and transitions are controlled.
* [ ] Audio reactivity is obvious without becoming chaotic.
* [ ] The scene remains visually legible during dense music.

Phase Hall

* [ ] The rectangular architectural room is removed.
* [ ] The viewer is immersed inside a vast spherical panel field.
* [ ] The spherical surface wraps around the camera without obvious seams or poles.
* [ ] The original vertical luminous-band identity is preserved.
* [ ] Bass energy drives large-scale deformation.
* [ ] Midrange energy drives intermediate surface motion.
* [ ] High-frequency energy drives fine shimmer and glints.
* [ ] Transients create clear, localized luminous events.
* [ ] The scene is substantially brighter and more luminous than before.
* [ ] Dark gaps, saturated colors, and bright highlights remain distinct.
* [ ] The default fixed-camera composition is compelling.

Shared system

* [ ] Both scenes use the existing audio and MIDI infrastructure.
* [ ] The response is clearly differentiated across musical passages.
* [ ] Bloom and emission are tuned for each scene rather than blindly sharing identical settings.
* [ ] No unnecessary renderer or audio-analysis subsystem has been introduced.
* [ ] No significant allocation or synchronization regressions occur.
* [ ] Performance is measured with audio and MIDI enabled.
* [ ] Both scenes meet the 30 FPS live target on the intended configuration, or any hardware limitation is documented honestly.
* [ ] Before-and-after captures and performance measurements are available.

⸻

Final Art Direction

The two scenes should have distinct visual personalities.

RD Pins: a living, luminous topographic interference field. Elegant, intricate, fluid, and rhythmically alive. Its beauty comes from the interaction of coherent contours, traveling waves, selective glimmers, and rich color.

Phase Hall: an immense spherical membrane of luminous vertical structures. Immersive, bright, spatial, and physically responsive. Its beauty comes from the feeling that music is traveling through a vast illuminated surface surrounding the viewer.

Both must feel deliberately authored, not like generic procedural demos.

Prioritize meaningful motion, beautiful light, controlled contrast, and strong musical expression over feature count.

Implement the art pass in stages, validate each stage in the actual renderer, and retain the strongest results from the existing scenes.
