# Cursed Slammers: the owner's POC 2 brief (verbatim, 2026-10-25)

Replaces the stop-1 stripe treatment (the owner: 'Stop refining that implementation').

---

# AV Gen — Lenticular Rendering POC 2: Real Optical Depth

## Objective

The current monkey-face experiment is not achieving the intended look. It renders a face with dense vertical stripes overlaid on the model, producing a ribbed-screen or scanline appearance.

Stop refining that implementation.

Build a focused technical and visual experiment to determine whether we can reproduce the exaggerated, dimensional appearance of a 1990s lenticular 3D collectible using the existing monkey-face asset.

For now, forget the ape gripping bars, apparition animation, audio reactivity, and finished scene design. We are testing the core visual effect on a single model.

## 1. Understand the reference mechanism

Research real lenticular 3D printing and distinguish three separate mechanisms:

- Lenticular lens structure: parallel optical ridges that direct different image views toward different viewing angles.
- Interlaced image views: multiple perspective images arranged into narrow alternating strips beneath the lens structure.
- Perceived depth: binocular disparity and viewpoint-dependent parallax between facial features and the background.

A striped texture applied to a single rendered image does not reproduce these mechanisms.

Read the existing experiment's implementation and identify exactly what produces the current vertical-line appearance. Determine whether those lines come from a texture, shader, post-processing pass, geometry, or some combination.

Retain the existing monkey asset initially. Do not spend time finding a replacement model until the optical experiment has been evaluated.

## 2. Build an isolated test harness

Create a minimal test scene or development mode that displays the monkey face against a neutral, dark background.

Requirements:

- Use the existing monkey model and textures.
- Frame the face large enough to fill most of the screen.
- Use simple, controlled lighting that clearly reveals the forehead, eye sockets, muzzle, cheeks, and facial silhouette.
- Provide a fixed frontal reference view.
- Provide a controllable horizontal viewing angle or camera offset.
- Allow the optical effect to be enabled and disabled for direct comparison.
- Keep the current production scene untouched unless a shared renderer change is demonstrably necessary.

The test harness must make it easy to distinguish genuine depth from surface decoration.

## 3. Test A — Multi-view lenticular rendering

Implement the simplest credible version of an actual lenticular image.

Render the monkey from multiple slightly different horizontal camera positions. Start with a modest number of views and increase only if necessary.

Generate an interlaced image by selecting narrow vertical strips from those views in a repeating pattern. Match the interlacing layout to a corresponding lenticular lens lookup.

Investigate how to implement the lens lookup in the existing renderer. The effect should be derived from the interlaced views and lens geometry, not from arbitrary stripes painted over the face.

The implementation should account for:

- Lens pitch and orientation.
- View count and ordering.
- Interlaced image coordinates.
- Viewing-angle or view-position selection.
- Correct alignment between the interlaced image and the lens pattern.

If the scene's viewing model does not support physically accurate binocular lenticular optics on an ordinary monitor, implement a clearly documented approximation that produces a useful visual result.

Do not claim to reproduce the physical behavior of a printed lens if the implementation is only simulating its appearance.

### Important distinction

There are two related but different outcomes to evaluate:

1. A real-time 3D image with convincing parallax as the camera moves.
2. A lenticular-inspired image whose visible perspective or image state changes with the viewing angle.

The POC should test both where practical. The first establishes the depth foundation; the second establishes the characteristic lenticular behavior.

## 4. Test B — Exaggerated facial depth

The primary visual target is the deep, almost sculptural appearance of classic 3D lenticular collectibles.

Use the existing 3D model to explore exaggerated perceived depth.

Investigate:

- Stronger foreground/background separation.
- Controlled horizontal parallax.
- Small viewpoint changes that produce clearly visible depth.
- Facial-feature displacement in the rendered views where necessary.
- A dark background that allows the silhouette to emerge cleanly.

The muzzle, brow, eyes, and cheeks should not all appear to lie on one flat plane.

Do not simply scale the entire head toward the camera. The goal is convincing relative depth between facial features.

If the existing model's geometry or textures are inadequate, document the specific deficiency and test whether separate depth layers, a depth map, or a view-dependent image representation would help.

## 5. Test C — Lens appearance

Only after the multi-view effect works, introduce the characteristic lens surface.

Experiment with a fine vertical lenticular structure, controlled specular reflections, and restrained optical highlights.

The surface should not look like a heavy mesh, CRT scanlines, or a security-camera filter.

At the intended viewing distance, the face must remain clear. The ridges should be most noticeable when light catches them at an angle.

Do not add random chromatic aberration, noise, glitching, or heavy interference as substitutes for the underlying optical effect.

## 6. Evaluate the asset honestly

The existing monkey model is the first test subject, not an assumption that the asset is suitable.

Inspect whether it has sufficient:

- Facial geometry and silhouette definition.
- Muzzle and brow depth.
- Texture and normal-map detail.
- Surface variation for directional highlights.
- Background separation.

If the result remains flat, determine whether the problem comes from the rendering method, model geometry, material response, or image preparation.

If the asset is fundamentally unsuitable, recommend the minimum change needed to improve the experiment. Do not silently replace the asset or abandon the optical test.

## 7. Performance and scope

Target the existing AV Gen live-rendering budget of 1080p at 30 FPS.

Start with a small number of views and measure the cost. Do not immediately render dozens of full-resolution views every frame.

Consider lower-resolution view generation, cached views for static geometry, reduced view counts, and other approximations if necessary. Preserve enough quality to evaluate the optical effect.

Keep this isolated from audio analysis and scene sequencing. No audio-reactive controls are needed yet.

## 8. Acceptance criteria

The experiment is successful only if it demonstrates:

1. A recognizable, detailed monkey face without a distracting stripe overlay.
2. Visible separation between facial features and the background.
3. A convincing change in apparent depth or perspective as the viewpoint changes.
4. A lens structure that enhances rather than obscures the image.
5. A direct comparison between the original render, parallax-only render, and full lenticular-inspired render.
6. A measured performance result and a clear explanation of any approximations.

Capture a short side-to-side camera sweep, not just a still screenshot.

If the result does not convincingly demonstrate the effect, do not proceed to the Cursed Slammers scene. Report what failed and what alternative representation should be tested next.

## Deliverables

- An isolated, runnable lenticular POC.
- A side-by-side comparison of the rendering stages.
- A short video showing the camera sweep and changing perspective.
- Performance measurements at the intended resolution.
- A concise technical report identifying what produced the depth, what produced the lens appearance, and what remains missing.

The goal is to establish whether the desired visual language is achievable before investing further in the scene.
