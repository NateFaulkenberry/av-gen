# AV Gen — Resonator Scene Visual Quality Pass
## Brightness, Saturated Color, Geometric Smoothness & Composition

(Owner brief, received 2026-10-17, saved verbatim. Coordinator notes: the owner has said to ignore the 120 Hz / 8.33 ms reference — the live target is 1080p at 30 fps; live scenes carry no audio file — test with assets/audio/feline-footwear.wav fed on the command line. The "supplied contact sheets" are ~/Desktop/av-gen-review/62-resonator-feline/*-contact-sheet.png; "Wave Form Dimension" is the scene registered as "Waveform Dimension".)

### Mission

Improve the visual quality of the three new resonator scenes:

- Cymatic World
- Wave Form Dimension
- Resonant Silk

The current scenes have promising geometry and visual identities, but three issues remain:

1. The materials and lighting are too dark. Large portions of the surfaces disappear into black, despite attractive saturated colors and specular highlights.
2. Cymatic World and Wave Form Dimension have genuinely rough or jagged geometry that must be corrected at the source, not hidden with anti-aliasing.
3. Resonant Silk has too much empty space around its main forms. The flowing material needs to occupy more of the frame and feel like an immersive environment rather than a small subject suspended in darkness.

This is a targeted visual-quality task. Preserve the existing resonant behavior, recognizable scene identities, and established color variations.

Inspect the current implementation and make the changes. Do not stop at a diagnosis or a proposal.

## 1. Shared baseline and constraints

Before changing anything:

- Inspect all three scene implementations.
- Review the supplied contact sheets and locate the relevant code and assets.
- Capture baseline renders at the current 1080p configuration.
- Identify the material, lighting, tone-mapping, and post-processing settings used by each scene.
- Identify the geometry-generation and displacement paths in Cymatic World and Wave Form Dimension.
- Inspect the camera, subject bounds, and spatial arrangement in Resonant Silk.
- Measure baseline CPU and GPU frame times.
- Determine which issues can be corrected through shared rendering infrastructure and which require scene-specific changes.

Preserve:

- The current resonant geometry and audio response.
- The existing scene identities and palette variations.
- The deep blue, violet, cyan, green, bronze, silver, and magenta directions already present across the scenes.
- The immersive, abstract, sculptural aesthetic.
- The existing AV Gen renderer and project architecture.

Do not replace the scenes with generic neon visualizers, add unrelated objects, or redesign the geometry unnecessarily.

Do not assume all three scenes use the same shader or geometry representation. Diagnose them independently.

---

## 2. Shared objective: brighter surfaces with vivid color

The materials are too dark relative to their attractive specular highlights. Large areas of the actual surfaces collapse into near-black, leaving only narrow reflective edges visible.

Increase surface readability without washing out saturation, flattening the geometry, or turning the entire scene into an emissive neon object.

### 2.1 Diagnose the source of darkness

Determine whether the darkness comes primarily from:

- Base-color values that are too dark.
- Insufficient diffuse or broad illumination.
- Excessively strong specular contrast.
- Inadequate indirect or ambient contribution.
- Tone-mapping compression.
- Exposure settings.
- Excessively dark shadowing or ambient occlusion.
- Material roughness and specular response.
- Emission being used as a substitute for visible surface color.

Do not blindly increase global exposure. Identify the dominant causes first.

### 2.2 Improve base-material visibility

Ensure the underlying material remains visibly colored between highlights.

Tune the existing shader's diffuse/base-color response so that the body of a fold or lobe carries recognizable color even when it is not directly facing the primary light.

Maintain the intended material character:

- Cymatic World should retain its glossy, sculpted resonant-cell appearance.
- Wave Form Dimension should retain its distinct waveform-inspired spatial forms.
- Resonant Silk should retain flowing, silky specular reflections.

Do not turn glossy materials into matte plastic or rubber merely to make them brighter.

### 2.3 Improve fill illumination

Evaluate a broad, restrained fill contribution that reveals surfaces facing away from the key light.

Prefer existing engine-supported lighting, ambient illumination, environment lighting, or an equivalent efficient approach.

Preserve directional lighting and shading contrast so the forms remain three-dimensional.

Avoid adding large numbers of dynamic lights. Do not introduce a new lighting architecture if the existing renderer already offers a suitable solution.

### 2.4 Preserve saturated colors

The desired result is not a desaturated grey lift.

Retain deep blue, violet, cyan, fuchsia, green, bronze, and silver as appropriate to the existing scene palettes.

Avoid:

- Raising every shadow until the image becomes flat.
- Increasing exposure until cyan and violet highlights turn white.
- Using white emission as the primary way to make the surfaces visible.
- Increasing bloom until colored details become indistinct.
- Applying the same base-color adjustment indiscriminately to all scenes.

Tune the balance between base color, fill illumination, specular response, emission, and tone mapping.

### 2.5 Preserve depth

Shadows should remain deep enough to define the geometry, but their internal color should remain visible where possible.

Avoid crushing large portions of the surface into pure black.

Use the minimum amount of ambient occlusion or shadow darkening needed to establish depth. If the current implementation over-darkens narrow creases and intersections, correct the cause rather than eliminating all contact shading.

### 2.6 Tone mapping and post-processing

Inspect the existing HDR pipeline and AgX tone mapping.

Tune the relationship between surface brightness, specular highlights, emission, and the final display transform.

Retain highlight detail and color separation. Do not replace the established tone mapper without a demonstrated reason.

Keep bloom restrained. Bloom should enhance the brightest resonant structures, not compensate for dark base materials.

### 2.7 Brightness acceptance criteria

After the pass:

- The colored bodies of surfaces are visibly readable.
- The main structures remain clearly distinguishable from their surroundings.
- Saturated colors remain saturated.
- Specular highlights retain definition.
- Deep shadows still establish depth.
- The scene does not look washed out, flat, or uniformly emissive.
- The improvement is visible at normal playback speed and in still captures.

Tune the shared rendering behavior first where appropriate, then apply scene-specific material adjustments.

---

## 3. Cymatic World: correct jagged geometric structures

### 3.1 Objective

Preserve the existing cymatic shape language and resonant behavior while improving the smoothness and fidelity of the rendered surface.

The problem is not ordinary screen-space edge aliasing. The actual surface contains rough, angular, pinched, or serrated features around some ridges, narrow connections, and resonant-cell boundaries.

Do not treat this as an anti-aliasing task.

### 3.2 Identify the geometry representation

Inspect how the surface is generated and displaced.

Determine whether it uses:

- A CPU-generated mesh.
- GPU vertex displacement.
- Tessellation or another procedural geometry path.
- An implicit surface or raymarched field.
- A grid or texture-based representation.
- Another engine-specific method.

Identify where the unwanted roughness originates.

### 3.3 Separate geometry problems from shading problems

Use diagnostic renders with:

- A neutral diffuse material.
- Specular highlights disabled.
- Surface normals visualized.
- Wireframe or equivalent topology visualization where supported.
- Displacement magnitude or field visualization where useful.

Determine whether the roughness is caused by actual vertex positions, inadequate mesh density, discontinuities in the field, poor normals, or a combination.

Do not add tessellation blindly if the underlying field contains discontinuities.

### 3.4 Improve geometric resolution

If the surface is mesh-based, determine whether its effective vertex density is sufficient for the spatial frequencies used by the resonant field.

Increase resolution or use an appropriate tessellation strategy if needed.

Prioritize:

- Narrow nodal ridges.
- Ridge intersections.
- High-curvature cell boundaries.
- Pinched connections.
- Areas where adjacent lobes form tight valleys.
- Features visible at the actual camera distance.

Do not simply multiply mesh density everywhere. Measure the performance cost and choose an appropriate resolution for the target live configuration.

If the scene uses an implicit or raymarched surface, investigate field sampling, gradient estimation, intersection precision, and surface reconstruction instead.

### 3.5 Smooth the geometry-generating functions

Inspect the functions that map the resonant field into displacement.

Where abrupt thresholds, sharp nonlinearities, or poorly conditioned intersections produce accidental cusps, replace them with smoother, controllable transitions.

Preserve the intentional definition of nodal structures. The goal is to remove unintended roughness without erasing the resonant pattern.

Keep mode phases, amplitudes, and temporal continuity intact.

Do not globally blur the displacement field or smooth the final rendered image.

### 3.6 Improve normals

Verify that the normals represent the actual deformed surface.

Evaluate the current method and, where justified, compare:

- Analytic derivatives of the displacement field.
- Appropriately sampled finite differences.
- Existing engine-supported normal reconstruction.

Ensure the chosen method remains consistent with the geometry representation.

Avoid normal discontinuities, unstable lighting at ridge intersections, and coarse approximations that create apparent facets on glossy surfaces.

### 3.7 Control unrepresentable fine detail

Identify whether high-frequency spatial modes or audio-driven displacement exceed the effective geometric resolution.

If so, attenuate or filter those components before they create unstable geometry.

Preserve broad and medium-scale structures. Retain fine vibration only where it can be represented reliably.

Use screen-space-aware attenuation or level-of-detail techniques if appropriate to the existing renderer.

Do not reduce all fine detail indiscriminately.

### 3.8 Validation

Inspect the geometry at the target resolution and at representative close-up views.

Compare neutral-material, glossy-material, and final-art renders.

Acceptance requires:

- Smooth, flowing curvature.
- Clean ridge intersections.
- No obvious serrated or pinched boundaries caused by the geometry representation.
- Stable specular highlights.
- No regression in the cymatic shape vocabulary.
- No regression in audio responsiveness.
- Measured performance within the applicable live budget.

---

## 4. Wave Form Dimension: independently diagnose and fix geometry

Apply the same diagnostic principles, but inspect this scene's actual implementation rather than assuming its geometry has the same cause as Cymatic World.

Identify whether the roughness originates in:

- Mesh resolution.
- Displacement sampling.
- Sharp field transitions.
- High-frequency geometric detail.
- Normal reconstruction.
- An implicit-surface sampling problem.
- Another scene-specific limitation.

Implement the appropriate fix.

Do not automatically copy Cymatic World's solution if Wave Form Dimension uses a different geometry representation.

Preserve its distinct spatial forms and intended waveform-inspired character.

The result should have smooth, coherent surfaces and intentional geometric definition, not rounded-off features that lose the scene's identity.

Validate in a neutral material first, then with the final shader and lighting.

Do not use blur, depth of field, bloom, or stronger anti-aliasing to conceal geometric defects.

---

## 5. Resonant Silk: improve composition and fill the frame

### 5.1 Objective

The current composition places the main flowing structure in a relatively narrow horizontal band, surrounded by substantial empty dark space.

The material itself has potential, but the overall image feels underfilled. The viewer is looking at a small portion of an otherwise empty scene rather than being immersed in a continuous resonant environment.

Improve framing, scale, and the arrangement of the existing flowing geometry.

Do not solve the problem by adding arbitrary objects or decorative elements.

### 5.2 Camera and scale first

Inspect the current camera, subject bounds, and framing.

Experiment with:

- Moving the camera closer.
- Increasing the apparent scale of the main geometry.
- Adjusting camera orientation to reveal more surface area.
- Increasing the vertical coverage of the flowing forms.
- Repositioning the main structure to improve frame balance.

Prefer a camera or scale adjustment if the existing geometry already contains enough visual detail.

Do not crop away the most interesting folds or force the subject into an awkward perspective.

### 5.3 Develop the existing folds

If camera adjustments are insufficient, modify the existing geometry to occupy more of the frame.

Potential changes include:

- Broader, more substantial folds.
- Additional continuous folds connected to the primary structure.
- More overlap and depth between the existing flowing regions.
- A more developed central transition between major folds.
- A less abrupt taper into the narrow central connection.
- Greater variation in fold width and curvature.

The geometry should feel like a continuous material under tension, with flowing structures that connect and transform naturally.

Do not turn the scene into a sea of unrelated blobs. Preserve the silk-like behavior and the sense of continuous flow.

### 5.4 Reduce empty space without eliminating negative space

The objective is not to fill every pixel.

Retain enough dark space to preserve contrast and give the material room to breathe.

However, the primary geometry should occupy substantially more of the frame than it currently does.

Use low-intensity reflections, subtle background continuation, or a secondary layer of flowing material only if needed to create convincing depth.

Do not add generic particles, unrelated floating forms, architecture, or random decorative objects.

### 5.5 Make the material itself more visible

Apply the shared brightness treatment to Resonant Silk.

Improve the visibility of the colored material between specular highlights.

Preserve smooth glossy reflections and the silk-like appearance.

Avoid making the material look like hard plastic or polished metal.

### 5.6 Temporal composition

Inspect the scene during actual motion, not just at a single frame.

Ensure the flowing material continues to occupy a compelling portion of the frame as it moves.

Avoid camera motion that repeatedly exposes large empty regions or makes the primary structure appear to shrink into the distance.

If camera movement is already part of the scene, adjust it only as needed to maintain a strong composition.

### 5.7 Composition acceptance criteria

- The flowing material occupies substantially more of the image.
- The central connection feels intentional rather than like a tiny bridge between disconnected objects.
- The main folds have sufficient scale and curvature to read clearly.
- Negative space remains deliberate rather than dominant.
- The scene feels immersive and continuous.
- The material retains its distinct silk-like identity.
- No unnecessary objects have been added to fill the frame.

---

## 6. Shared engineering constraints

Follow existing AV Gen architecture and conventions.

- Reuse existing rendering and material infrastructure.
- Avoid duplicate shader logic where a shared implementation is appropriate.
- Keep scene-specific adjustments separate where the scenes have genuinely different requirements.
- Avoid unnecessary per-frame allocations or expensive geometry regeneration.
- Measure any increase in mesh density, field evaluation, normal reconstruction, or post-processing cost.
- Preserve current scene registration, audio routing, MIDI integration, and quality settings.
- Do not introduce a new global settings system for a handful of tuning parameters.
- Do not rewrite the renderer.
- Do not regress existing scenes or remove useful features.

Treat the recent 120 Hz / approximately 8.33 ms frame-time target as a reference for the live renderer, but report the actual configuration, baseline, and measured result.

If a scene cannot meet its target after the quality improvements, document the bottleneck and evaluate targeted alternatives rather than silently reducing visual quality.

---

## 7. Required iteration sequence

### Phase A — Baseline

Capture the current three scenes and measure their performance.

### Phase B — Brightness

Tune material response and lighting. Capture comparisons with exposure, saturation, and tone mapping controlled so the cause of improvement is clear.

### Phase C — Geometry diagnosis

Use neutral materials, normals, and topology inspection to identify the actual sources of roughness in Cymatic World and Wave Form Dimension.

### Phase D — Geometry correction

Fix the field, resolution, or normals as indicated by the diagnosis. Validate each scene independently.

### Phase E — Resonant Silk composition

Test camera and scale changes first. Modify the geometry only where necessary to achieve a more immersive composition.

### Phase F — Integration

Combine the changes, restore the intended materials, and inspect the scenes under actual audio.

### Phase G — Performance and regression testing

Measure CPU/GPU costs, run relevant builds and tests, and confirm that geometry motion and audio responsiveness remain intact.

Do not treat a successful build as proof of visual success.

---

## 8. Evidence and final report

Provide before-and-after captures at the same resolution and representative timestamps.

For each scene, report:

- What caused the darkness.
- Which material or lighting adjustments were made.
- How vivid color and shadow detail were preserved.
- The root cause of jagged geometry, where applicable.
- How the geometry or normal representation was corrected.
- What changed in Resonant Silk's camera, scale, or geometry.
- CPU/GPU performance before and after.
- Build and test results.
- Any remaining limitations.

Use short clips where necessary to demonstrate temporal stability, smooth highlights, and composition during motion.

### Final artistic direction

Make the resonator scenes brighter and more visually legible without sacrificing saturation or depth.

Make the geometric surfaces smooth because they are well represented, not because they have been blurred.

Make Resonant Silk feel like an immersive flowing environment rather than a small subject floating in empty space.

Preserve what is already working. Correct the actual technical and compositional problems, then validate the complete rendered result.
