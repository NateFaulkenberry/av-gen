# AV Gen — Cymatic World: Resonant Color, Lighting & Optical Effects Pass

(Owner brief, received 2026-10-16, saved verbatim. Coordinator notes: the owner said to ignore the 120 Hz / 8.33 ms reference in §14 — the performance target is live 1080p at 30 fps; Feline Footwear is the test track, fed on the command line; the scene carries no audio file.)

## Mission

The Cymatic World geometry now has meaningful resonant motion. Develop the next visual layer: a sophisticated, evolving system of resonant color, geometry-aware illumination, traveling light patterns, and carefully controlled post-processing.

The scene should feel like a continuous resonant medium in which geometry, color, light, and audio are all expressions of the same underlying system.

This is not a generic shader beautification task. Implement a coherent visual system that makes the scene more distinctive and visually interesting throughout a musical performance.

**Core principle: Light and color should reveal, trace, and transform the resonant structures—not merely pulse independently to the beat.**

Preserve the existing geometry, its improved resonant behavior, the immersive camera framing, and the established cyan/fuchsia/violet/deep-navy direction. Treat these as the foundation for the work.

Inspect the actual current implementation before making changes. Do not assume that any particular proposed effect is missing, or that it should be added if an existing engine feature already provides a better solution.

---

## 1. Establish a baseline and protect the geometry

Before implementation:

- Capture the current scene during representative audio.
- Record current material, lighting, exposure, tone mapping, bloom, and post-processing settings.
- Inspect the current geometry field and identify which data can be reused by the shading system.
- Inspect existing shader infrastructure, render passes, screen-space buffers, and available post-processing effects.
- Determine how audio-derived signals reach the material and renderer.
- Measure current CPU and GPU frame times at the normal live configuration.
- Identify the actual limitations of the current appearance.

The existing geometry is now a successful part of the scene. Do not regress it to accommodate the new visual effects.

Preserve the current resonant field, mode continuity, audio responsiveness, and temporal stability. The color and lighting system must consume the geometry's established signals without destabilizing them.

Do not redo the geometry overhaul unless a specific, demonstrated interface problem prevents the visual system from working.

---

## 2. Build a coherent resonant shading model

Avoid implementing a pile of unrelated shader effects.

Develop a unified shading model with a small number of meaningful inputs.

Where available, reuse:

- The signed resonant field.
- Surface displacement and derivatives.
- Nodal boundaries or nodal proximity.
- Large-, medium-, and fine-scale mode contributions.
- Mode energy or excitation envelopes.
- Transient excitation.
- Audio-band envelopes.
- Surface normals and view direction.
- A persistent time or phase signal.

Not every input needs to be exposed to every shader stage. Select the smallest set that produces the desired behavior.

The shading system should distinguish at least four concepts:

1. **Base color field:** establishes the surface's underlying color organization.
2. **Resonant color variation:** evolves as spatial modes and excitation change.
3. **Illumination response:** reveals the geometry and emphasizes selected structures.
4. **Emission and optical finishing:** creates controlled luminosity without destroying contrast.

These layers should interact, but remain sufficiently independent to be tuned and debugged separately.

Do not directly map each audio band to a separate color channel. The result should feel like a coherent visual instrument rather than a conventional audio spectrum visualizer.

---

## 3. Resonant color fields

Develop a spatially organized color field that complements the geometry.

### Base palette

Retain the established direction:

- Deep navy and blue-black for visual depth.
- Cyan and electric blue for cool highlights.
- Violet for transitions and intermediate regions.
- Fuchsia and magenta for warm contrast.
- Limited near-white cyan highlights for the strongest luminous structures.

Use the existing scene palette as the baseline. Do not replace it with arbitrary rainbow cycling.

### Spatial organization

Color should follow meaningful structures in the resonant field.

Explore combinations of:

- Field value.
- Nodal proximity.
- Local slope or curvature.
- Spatial mode contributions.
- Resonant energy.
- Slowly evolving phase.
- A separate, coherent color-interference field.

The goal is to create broad, legible regions of color separated by interesting boundaries.

Color regions should flow across lobes, surround hollows, trace ridges, and form secondary structures that complement the underlying shape.

Avoid noisy per-pixel coloration or high-frequency patterns that compete with the geometry.

### Color evolution

The color field should evolve continuously as the resonant configuration changes.

A pattern might begin with broad cyan regions around deep violet hollows, develop fuchsia bands as another mode becomes active, and form more intricate blue-violet interference regions as the spatial structure changes.

These transitions should arise from continuous field changes, not hard-coded scene-wide color swaps.

Allow the color field to have its own phase and spatial scale, but anchor it to the geometry so it feels physically or artistically connected to the surface.

The color should sometimes reinforce the geometry and sometimes provide a secondary interference pattern that makes the image more surprising.

### Artistic restraint

Do not maximize saturation everywhere.

Reserve the brightest colors for the structures that matter. Use dark regions and relatively quiet color transitions to preserve depth and give luminous features room to stand out.

---

## 4. Resonant illumination along ridges and nodal structures

Develop geometry-aware illumination that makes the resonant pattern visible through light, not just color.

### Ridge and nodal response

Where suitable field information exists, derive a stable measure of proximity to nodal structures or other meaningful boundaries.

Use this to modulate selected lighting or emission contributions.

Possible behaviors include:

- Soft luminous bands along major boundaries.
- Sharper cyan highlights on strongly excited ridges.
- Violet or fuchsia glow in selected hollows.
- Brightness variations that follow mode excitation.
- Fine luminous details that appear only at suitable excitation levels.

Do not simply draw bright outlines around every contour. A uniformly outlined surface will look like a technical wireframe rather than a living resonant environment.

### Multi-scale lighting hierarchy

Different structures should receive different treatment.

- Primary structures establish the dominant lighting pattern.
- Secondary structures receive more restrained highlights.
- Fine detail receives occasional glints rather than continuous intense emission.

Use field amplitude, scale, and excitation to prioritize the important features.

### Actual geometry and lighting

Use the available normals, derivatives, and existing lighting infrastructure to make the surface appear sculpted.

Evaluate grazing light, directional illumination, and existing engine-supported lighting approaches before implementing expensive new lighting systems.

The objective is a convincing sense of curvature and depth, with highlights that move coherently as the geometry evolves.

Do not add hundreds of point lights or create a new dynamic-lighting architecture unless an actual prototype demonstrates a substantial improvement.

### Light behavior over time

Lighting should respond to changes in the resonant field with appropriate persistence.

A transient may briefly brighten a ridge network or a subset of lobes. The illumination should decay smoothly, not flicker independently of the geometry.

The strongest response should occur in meaningful structures, not uniformly across the entire image.

---

## 5. Traveling illumination and color waves

Explore a system of traveling light and color disturbances that can move through the resonant field.

This is an opportunity to make the scene feel more alive without introducing additional objects or particle clutter.

### Possible behaviors

- Broad rings of illumination propagate away from an excited region.
- A band of cyan or fuchsia travels along a ridge network.
- A wave of brightness moves through a set of connected lobes.
- Interference between two traveling waves creates temporary bright and dark regions.
- A transient excites a ripple that fades as it spreads.
- Sustained audio creates recurring bands or oscillations in selected regions.

These are possible visual behaviors, not separate mandatory effects.

### Design requirements

- Waves must have a clear spatial organization.
- Their motion must be continuous.
- Their speed and spatial scale must remain visually legible.
- Their intensity must decay or settle according to a deliberate model.
- They must not cover the entire surface in equally bright moving rings.
- They must not become a collection of unrelated circular ripples.
- Their motion must complement the primary resonant structures.

Prefer a field-based implementation over spawning large numbers of independent particles or lights.

If the existing resonant field can supply the propagation behavior directly, reuse it. Otherwise, introduce a lightweight, persistent traveling-wave component.

Avoid tying every transient to a global, perfectly centered expanding ring. Explore localized excitation and propagation along the geometry where practical.

### Audio relationship

- Bass transients may produce broad, slower-moving illumination.
- Midrange excitation may create more intricate bands.
- High-frequency activity may create smaller, more restrained optical responses.
- Sustained notes may maintain a stable traveling or standing color pattern.

These are starting hypotheses. Tune them against real audio and retain only the behaviors that look compelling.

---

## 6. Controlled iridescence and optical interference

Explore an iridescent response that adds visual richness to the existing plasma surface.

The target is sophisticated, restrained interference coloration—not a generic rainbow material.

### Candidate influences

- Surface orientation relative to the view.
- Surface curvature.
- Resonant field value.
- A slow phase field.
- Mode contributions.
- Existing base color.

Potential results include subtle transitions from cyan to blue to violet, with occasional fuchsia highlights at selected angles or excitation levels.

### Requirements

- Keep the palette bounded and intentional.
- Preserve the contrast between dark regions and luminous structures.
- Avoid rapid rainbow cycling.
- Avoid strong view-dependent color shifts that make the entire surface change dramatically with tiny camera movements.
- Avoid a uniformly oily appearance if it obscures the resonant topology.
- Do not introduce excessive procedural noise to simulate surface complexity.

Treat iridescence as a supporting layer. It should make the surface feel richer on close inspection without replacing the main color design.

Prototype it independently so its contribution can be evaluated objectively.

---

## 7. Audio-reactive lighting and emission

Build on the existing audio-response system rather than introducing redundant analysis.

Use the existing signal bus, analysis features, envelopes, modulation, and parameter conventions.

### Bass and low-frequency excitation

Potential responses:

- Increased illumination of broad ridges and primary structures.
- A controlled increase in the depth or intensity of selected color regions.
- Brief enhancement of the largest luminous structures after a kick.
- Broad, slow-moving illumination waves.

Avoid a uniform whole-screen brightness flash on every kick.

### Midrange excitation

Potential responses:

- Increased contrast in medium-scale structures.
- Activation of secondary ridge networks.
- Shifts in the relative contribution of cyan, violet, and fuchsia.
- More intricate interference in selected regions.

### High-frequency excitation

Potential responses:

- Small highlights and glints.
- Subtle fine-scale luminosity.
- Restrained shimmer in selected regions.

Avoid a constant high-frequency sparkle layer.

### Transients

Transient response should use a controlled attack and decay envelope.

A transient can brighten selected structures, excite an illumination wave, or increase the visibility of existing detail.

It must not reset the material, randomly change the palette, or overwhelm the image with an instantaneous white flash.

### Sustained excitation

Allow the visual field to develop a persistent state under sustained musical energy.

The system should retain a sense of history through appropriately damped envelopes or persistent phase, not merely mirror the instantaneous FFT magnitude.

### Independence and coherence

Color, emission, and lighting do not need identical response curves.

For example, geometry might respond to low-frequency energy with a broad, sustained deformation, while emission has a quicker attack and a shorter decay.

Tune these relationships intentionally so the scene has depth and temporal complexity without feeling disconnected.

---

## 8. Post-processing: refine, do not obscure

Inspect the current rendering pipeline and reuse existing post-processing features wherever possible.

Do not implement redundant effects if an appropriate engine pass already exists.

### Bloom

Refine bloom around the new luminous geometry.

- Use a controlled threshold or equivalent mechanism where supported.
- Preserve localized highlights.
- Keep the bloom radius restrained.
- Avoid broad foggy halos around the entire scene.
- Preserve dark areas and the distinction between ridges and hollows.
- Verify that bright cyan and fuchsia structures remain distinct.

Bloom should make the surface feel emissive without hiding the actual geometry.

### Tone mapping and exposure

Inspect the existing HDR pipeline and AgX tone mapping.

Ensure that:

- Highlights retain useful color.
- Strong emission does not turn every bright feature white.
- Dark regions retain detail.
- The plasma palette remains differentiated.
- Exposure changes do not create a flat, washed-out image.

Do not replace the established tone mapper without evidence that it is a limiting factor.

### Optional effects to evaluate

Prototype only effects that suit this particular scene.

**Subtle diffusion or glare:** A restrained optical glow around the brightest structures, if not already adequately handled by bloom.

**Chromatic aberration:** Consider only an extremely subtle version if it materially improves the result. It should not create conspicuous RGB fringes or reduce geometry clarity.

**Vignette:** Avoid by default. The scene already relies on an immersive, full-frame surface. Do not darken the edges simply to imitate a cinematic grade.

**Motion blur:** Keep disabled unless a concrete, validated approach improves the scene. Temporal instability or excessive blur should not be used to hide poor motion.

**Screen-space distortion:** Consider only if the existing renderer offers an inexpensive, controllable approach and the effect reinforces resonant interference. Do not warp the entire image into a generic heat-haze effect.

**Temporal effects:** Avoid accumulating trails or history-based effects unless their persistence, ghosting, and response to fast changes are explicitly tested.

The task does not require all of these effects. It requires evidence-based selection of the small number that genuinely improve the image.

### Post-processing budget

Measure the GPU cost of each added pass.

Prefer a small number of carefully tuned effects over a large stack of inexpensive-looking filters that collectively soften, flatten, or obscure the scene.

Retain the existing live rendering budget and report the incremental cost.

---

## 9. Make the visual identity evolve over a performance

The scene should not look identical throughout an entire track, but it should not behave like a random color generator either.

Build variation from persistent field state and musical activity.

### Evolution dimensions

The system may vary:

- Spatial color phase.
- Relative dominance of cyan, violet, and fuchsia.
- Which ridges receive the strongest illumination.
- The spatial scale of the color pattern.
- The relationship between color boundaries and geometric nodal lines.
- The strength and reach of traveling light waves.
- The visibility of fine luminous structures.
- The relative balance between diffuse color and sharp highlights.

Changes should be continuous and controlled.

### Musical development

During quieter sections, allow broad color fields, subtle illumination, and more negative space.

During dense sections, additional structures may become visible and interference may become more intricate.

During transients, temporarily emphasize a subset of meaningful structures.

During sustained passages, allow the lighting and color field to settle into recognizable patterns.

During transitions, gradually reorganize the color relationships rather than replacing the entire palette.

These behaviors must emerge from a coherent mapping. Do not implement a fixed sequence of scene-wide color presets.

### Avoid sameness and chaos

The system needs enough variation to remain visually interesting over time, but enough persistence for the viewer to understand its patterns.

Use bounded, deterministic or reproducible modulation where possible. Avoid uncontrolled randomness and arbitrary per-frame parameter changes.

A strong moment should come from the interaction of several visual systems, not from turning every effect to maximum intensity.

---

## 10. Technical architecture and integration

Follow the existing AV Gen architecture and rendering conventions.

Before implementing new resources or passes:

- Inspect existing material parameters and shader bindings.
- Inspect the current HDR and post-processing pipeline.
- Identify available render targets and intermediate buffers.
- Determine which field data can be shared between geometry and shading.
- Check whether nodal or resonant information is already available.
- Inspect existing parameter and signal modulation systems.
- Reuse established shader and resource management patterns.

Avoid duplicating field evaluation unnecessarily between vertex, fragment, and compute stages.

If field evaluation must occur in multiple stages, consider whether shared data, a compact field texture, analytic evaluation, or existing render infrastructure offers the best balance.

Do not add a high-resolution simulation texture simply because it is convenient. Measure the required spatial resolution and choose an appropriate representation.

Keep new controls limited to meaningful artistic parameters. Prefer internal tuning and established debug mechanisms over introducing a large new public settings surface.

Maintain clear separation between:

- Base color.
- Resonant color variation.
- Ridge or nodal illumination.
- Transient illumination.
- Emission.
- Post-processing.

This separation should support debugging and tuning without producing a needlessly elaborate material framework.

---

## 11. Diagnostic views and tuning

Create or reuse lightweight debug views where they materially help development.

Useful views include:

- Base color without emission.
- Resonant color field by itself.
- Nodal or ridge illumination contribution.
- Traveling-wave contribution.
- Emission before bloom.
- Bloom contribution.
- Final tone-mapped image.
- Individual spatial-scale contributions.
- Audio-derived envelopes controlling the visual layers.

Use these to isolate whether a visual improvement comes from meaningful field organization or merely increased brightness and saturation.

Capture the scene under representative audio while adjusting each layer.

Keep the geometry unchanged during initial shading experiments so comparisons remain meaningful.

Remove temporary debug paths when finished or keep them behind the project's existing debug conventions.

---

## 12. Required implementation sequence

### Stage A — Baseline and diagnosis

Inspect the current scene and renderer. Capture representative before-images and measure current performance.

### Stage B — Resonant color field

Implement and tune the base color field independently of new lighting and post-processing.

Demonstrate that the color field forms organized, evolving patterns across the existing geometry.

### Stage C — Geometry-aware illumination

Add selected ridge and nodal highlights. Ensure they reveal the actual topology and maintain a clear visual hierarchy.

### Stage D — Traveling illumination

Prototype one or two propagation behaviors. Keep only those that produce compelling results in motion and respond naturally to the resonant system.

### Stage E — Emission and optical finish

Tune emission and existing bloom. Evaluate optional post-processing individually and reject effects that reduce clarity or create a generic look.

### Stage F — Audio integration

Tune attack, sustain, and decay relationships across color, illumination, and emission using the existing signal system.

### Stage G — Performance and regression validation

Measure the complete scene at the target live configuration. Verify that the geometry and its established motion remain unchanged in quality.

### Stage H — Final art pass

Capture multiple musical conditions, compare against the baseline, and make final adjustments to color balance, lighting hierarchy, and effect intensity.

Do not proceed to the next stage merely because the implementation compiles. Each stage must demonstrate a visible improvement.

---

## 13. Validation scenarios

Validate the scene at normal playback speed under:

1. Silence or near silence.
2. Sustained bass.
3. Kick-heavy rhythms.
4. Strong snare and transient activity.
5. Sustained midrange.
6. High-frequency percussion.
7. Dense full-band music.
8. A quiet-to-loud build.
9. A loud-to-quiet transition.
10. Different tracks with distinct spectral and rhythmic characteristics.

Inspect:

- Whether color patterns remain organized.
- Whether the lighting reveals the geometry.
- Whether bright regions are localized and meaningful.
- Whether traveling waves complement rather than obscure the primary forms.
- Whether the image develops visual variety over time.
- Whether bloom preserves detail and contrast.
- Whether the scene remains compelling during quieter passages.
- Whether fast events cause flicker, clipping, or sudden whole-screen flashes.
- Whether temporal effects produce trails or ghosting.
- Whether the new layers compete with one another.

Use actual audio and reproducible test signals where practical.

Capture both still images and short video clips. A static screenshot cannot establish that the color and lighting system evolves well.

---

## 14. Performance requirements

Measure the complete scene at the project's actual target live resolution and quality settings.

Record:

- CPU frame time.
- GPU frame time.
- Incremental cost of new shader work.
- Incremental cost of additional render passes.
- Memory and bandwidth impact where relevant.
- Performance during dense, high-excitation audio.
- Performance at the existing offline quality level if applicable.

Use the recent AV Gen live baseline of approximately 8.33 ms per frame at 120 Hz as a reference, while reporting the actual target configuration and current baseline.

Prefer existing renderer features and GPU-efficient field evaluation.

Do not sacrifice the resonant geometry's quality to pay for decorative effects.

If a post-processing effect consumes substantial time, compare its visual contribution against a cheaper alternative. Remove effects that do not produce a clear improvement.

Do not rewrite the renderer or introduce a new lighting architecture for this task.

---

## 15. Anti-goals

Do not:

- Merely increase saturation, emission, or bloom.
- Make the entire scene pulse uniformly with the kick.
- Map bass to red, mids to green, and highs to blue.
- Use random rainbow cycling.
- Add a constant layer of sparkling particles.
- Outline every contour with equal intensity.
- Use broad bloom to conceal poor surface shading.
- Add generic cinematic lens effects by default.
- Introduce heavy chromatic aberration, blur, vignette, or image distortion.
- Create unrelated color presets that switch abruptly.
- Duplicate audio analysis that already exists.
- Add expensive dynamic lights without demonstrating their value.
- Regress the geometry, its mode continuity, or its established audio response.
- Claim success based on a single attractive screenshot.

---

## 16. Definition of done

The task is complete when:

- The resonant color field produces organized, visually distinctive patterns.
- Color relationships evolve continuously with the geometry and music.
- Nodal structures and selected ridges receive meaningful illumination.
- At least one additional illumination behavior, such as traveling light waves, has been prototyped and retained only if it improves the result.
- Emission and bloom enhance rather than obscure the surface.
- The image has sufficient dark regions, contrast, and highlight hierarchy.
- The scene develops meaningful visual variety over time without random chaos.
- The existing geometry and motion remain intact.
- The complete scene is validated with actual audio at normal playback speed.
- CPU/GPU costs and build/test results are reported.
- Before-and-after visual evidence is provided.

## 17. Final report

Report:

1. The original visual limitations identified.
2. The color-field approach selected.
3. How color is related to geometry and resonant state.
4. How ridge and nodal illumination is implemented.
5. Which traveling-wave or optical effects were prototyped and retained.
6. How audio affects color, illumination, and emission.
7. Which post-processing settings or passes changed.
8. Before-and-after captures and motion evidence.
9. CPU/GPU cost at the target live configuration.
10. Build/test results and known limitations.

Clearly distinguish implemented and tested behavior from ideas that remain experimental.

## Final artistic direction

Make Cymatic World feel like **resonant matter expressing itself through light**.

The geometry creates the structures. Color reveals relationships between them. Illumination traces their boundaries and emphasizes their energy. Traveling waves introduce motion beyond simple surface displacement. Post-processing gives the brightest structures a controlled optical presence.

The image should be capable of transforming from deep, quiet, iridescent forms into intricate, luminous interference patterns as the music develops—without losing the recognizable geometry that makes the scene compelling.

Prioritize visual organization, distinctive color relationships, and coherent temporal evolution over maximum brightness or effect count.
