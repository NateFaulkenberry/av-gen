# Aurora Borealis pass 3: the owner's Sabertooth brief (verbatim, 2026-10-24)

Coordinator notes (checked 2026-10-24):
- `~/Desktop/sabertooth/` holds exactly the ten named files, casing as named (`BASS.wav`, `Cloud Diving.wav`, `Crash.wav`, `HH.wav`, `Kick.wav`, `Moog.wav`, `Snare.wav`, `Toms.wav`, `tone block.wav`, `Sabertooth.wav`). All 48 kHz 24-bit; Moog and tone block are mono, the rest stereo.
- Lengths: the mix, Crash and HH are 70.18 s; the other seven are 72.29 s. Owner: "they should all contain tempo info and have same start, I bounced some of them seperately". Every file's bext TimeReference is 178213012 (same start) and its LIST chunk says Tempo 83.0 (Ableton bounces); the 2.1 s is tail on the seven batch-bounced stems. To be confirmed by the stem-set check (ADR-1632).
- Never commit or copy these files into the repo; reference them where they are or through the gitignored assets convention.
- State at the start: scene/aurora-borealis 1f903bff (pass 2 gates A-F built, G partial, live acceptance not run; see PROGRESS.md). The scene is now `Sonic VFX - Aurora Borealis`.

---

# AV Gen — Aurora Borealis: Sabertooth Stem-Synchronized Visual Performance

## Mission

Upgrade the existing Aurora Borealis scene in `av-gen` into a tightly synchronized, instrument-specific audiovisual performance driven by a new track: Sabertooth.

The audio files are located on the development machine's Desktop:

- Main track: `~/Desktop/sabertooth/Sabertooth.wav`
- Stem directory: `~/Desktop/sabertooth/`

The folder contains the original track and individual instrument stems. Inspect the actual directory contents and audio files before making implementation decisions.

The objective is to make the environment feel as though the music is physically performing the scene.

Different musical parts should control distinct visual elements:

- Cloud Diving controls the aurora's large-scale color, intensity, and movement.
- Crash triggers fast, brilliant shooting stars that illuminate the environment.
- Moog makes the mountains on the LEFT side of the river glint, shimmer, and emit light.
- Kick sends a bright, narrow beam of light rapidly down the river and creates water ripples.
- Snare triggers a similar river effect in a different color, with its own visual character.
- Toms and tone block make the mountains on the RIGHT side of the river respond with glints, flashes, and luminous accents.
- Bass and HH are initially reserved for experimentation after the primary mappings work.

The scene must retain its existing identity: a wide mountain valley, dark faceted terrain, a winding reflective river, and a luminous purple/pink/blue aurora.

This is a focused scene-development task. Do not rewrite the renderer, replace the scene, or create a parallel audio-analysis architecture.

**Performance constraint: sustained 30 FPS at 1920 × 1080 in live mode on the established M2 Max / Apple Silicon Metal-Dawn baseline.**

## 1. Mandatory preflight and research

Before implementing changes, inspect the existing repository and scene.

Use two parallel subagents with distinct responsibilities.

### Subagent A: Audio assets and synchronization

Investigate:

- The complete contents of `~/Desktop/sabertooth/`.
- The format, sample rate, channel count, duration, and synchronization of every stem.
- Whether the stems align exactly with `Sabertooth.wav`.
- Whether the engine can load these files through its existing stem-analysis workflow.
- The current analysis and event-detection capabilities for each instrument.
- The engine's signal update cadence, event timing, smoothing, and any known latency.
- The best way to use stem analysis while preserving the intended playback mix.

Produce an inventory of the available audio assets and a proposed mapping from the actual stem filenames to the visual behaviors specified below.

Do not assume the filenames, extensions, or timing characteristics without inspecting the files.

### Subagent B: Scene implementation and performance

Investigate:

- The current Aurora Borealis scene and its registration.
- The existing aurora, mountain geometry, river, water material, and reflection implementation.
- The current implementation of the river light pulse, including why it currently draws a line perpendicular to the river.
- Existing particle, ribbon, emissive, lighting, bloom, reflection, and water-distortion capabilities.
- The existing stem-aware modulation and parameter-routing architecture.
- Available profiling and live-performance tools.
- Useful improvements retained from Living Prism, particularly signal-timing improvements.

Determine which parts of the requested behavior can be implemented using existing features and which require narrowly scoped changes.

### Plan and implementation

After the investigations, synthesize an actionable plan covering audio synchronization, stem mappings, effect implementation, performance, and validation.

Then proceed with the work. Do not stop after producing the plan.

Use the actual rendered scene and real audio as the source of truth. Research should support implementation rather than replace it.

## 2. Audio asset integration

Use Sabertooth as the development and validation track for this iteration of Aurora Borealis.

The previous Feline Footwear stems were used to develop the scene, but Sabertooth is now the active track for this task.

Do not delete the Feline Footwear assets or unnecessarily remove the existing scene's ability to work with its established audio workflow. Make the new track available through the existing supported configuration or selection mechanism.

### Audio playback and stem analysis

- Preserve the intended main-track playback mix.
- Use the individual stems as synchronized analysis sources.
- Verify alignment between the original track and each stem.
- Use the existing engine architecture for stem loading, analysis, and signal routing.
- Avoid creating redundant playback instances or a new scene-specific audio engine.
- Avoid resampling or transforming audio unnecessarily.
- Report any missing, misaligned, or incompatible stem files.

If the engine currently expects stems in a particular asset layout, use the existing conventions where possible and make only the necessary configuration changes.

### Required asset inventory

Identify the actual files corresponding to:

- `Sabertooth.wav` — full mix
- `BASS.wav`
- `Cloud Diving.wav`
- `Crash.wav`
- `HH.wav`
- `Moog.wav`
- `Kick.wav`
- `Snare.wav`
- `Toms.wav`
- `tone block.wav`

These names are expected from the supplied folder description. Verify exact casing, spaces, extensions, and file availability.

If a named stem is absent, report the discrepancy rather than silently substituting another stem.

## 3. Core principle: each instrument has a visual role

The scene should have a recognizable visual identity for each major stem.

Avoid routing every stem into the same global brightness or scale parameter. Avoid making every instrument trigger a flash across the entire scene.

The intended composition is spatially divided into three main performance areas:

1. **Sky:** Cloud Diving controls the aurora; Crash triggers shooting stars.
2. **Left mountain range:** Moog controls selective facet and edge glints.
3. **River and right mountain range:** Kick and Snare control river effects; Toms and tone block control the right-side mountains.

Bass and HH should remain unassigned to major new effects until the primary mappings have been implemented and evaluated.

Each behavior should remain visually understandable when the other stems are active.

## 4. Cloud Diving: aurora as the primary sustained instrument

### Purpose

The Cloud Diving stem contains the large synth swells. It should be the primary control for the aurora's broad visual evolution.

The existing aurora already pulses and changes shape in response to audio. Preserve that work and improve its musical relationship to the new stem.

### Desired behavior

Use the Cloud Diving stem to control a coordinated set of aurora parameters:

- Dominant aurora color and secondary color balance.
- Overall luminance and perceived energy.
- Width, spread, or vertical extent of the curtains.
- The amplitude and scale of broad flowing folds.
- The intensity of internal luminous detail.
- The persistence of bright aurora regions.

The aurora should grow and recede with the actual synth swells.

During quieter passages, retain an attractive baseline aurora. During stronger swells, the curtains should become brighter, broader, and more visually expressive.

The transition must be smooth enough to follow sustained synth energy without visible jitter.

### Color direction

Retain the existing violet, indigo, magenta, pink, and electric-blue identity.

Use controlled palette transitions. Avoid random per-frame color changes or a simplistic mapping in which every amplitude fluctuation directly changes the entire aurora to a different hue.

The stem should influence both the intensity and the color composition, but not necessarily at the same rate.

For example, intensity can follow the swell envelope more quickly, while the dominant hue transitions more gradually.

### Shape and movement

The aurora should retain broad, identifiable ribbon forms. Changes in shape should feel like the curtains are expanding, folding, and flowing rather than a flat noise texture rapidly distorting.

Use existing shader parameters and the current aurora implementation wherever possible.

Do not add a new volumetric renderer or expensive ray-marching system.

### Acceptance criteria

- Major Cloud Diving swells create obvious, well-timed aurora changes.
- The aurora relaxes naturally between swells.
- Color transitions remain coherent and aesthetically controlled.
- The effect does not jitter excessively or remain saturated continuously.
- Other stems do not unintentionally take over the aurora's overall color and intensity.

## 5. Crash: fast, brilliant shooting stars

### Purpose

Replace the previous slow-comet concept with a short, fast shooting-star event driven by the Crash stem.

A crash cymbal should produce a visually unmistakable streak of light crossing the sky.

This is a transient effect, not a slowly drifting decorative comet.

### Motion

Each eligible crash event should trigger a shooting star that:

- Appears at a controlled position in the sky.
- Travels rapidly along a clear diagonal or curved trajectory.
- Has a bright head and a tapered luminous trail.
- Completes its main movement quickly, on the timescale of the cymbal event.
- Fades naturally after the main streak.
- Does not remain as a persistent object.

The exact travel duration should be tuned against the actual crash transients and rendered playback.

Start with a short, high-impact streak. Avoid slow travel that makes the event look like a generic comet.

The trail can persist slightly longer than the head so the event has visual weight without lasting excessively.

### Brightness and environmental illumination

The shooting star should be exceptionally bright compared with the baseline aurora.

Its impact must come from more than an emissive line:

- The head should have a tight, bright core.
- The trail should have a controlled glow.
- The aurora may briefly brighten or react near the trajectory.
- Selected mountain facets should receive a short-lived light accent.
- The river should receive a brief reflected flash or traveling highlight.

Use the existing lighting and post-processing architecture where practical.

A genuine moving light is not mandatory. If dynamic lighting is too expensive, approximate the illumination with shader-driven color washes, material parameters, or another bounded technique.

The effect should visibly illuminate the environment without turning the entire image white or causing an uncontrolled bloom explosion.

### Event detection

Use the Crash stem to identify crash events, using the existing analysis pipeline where possible.

Do not trigger a shooting star on every high-frequency transient if the stem contains sustained cymbal energy or multiple sub-events.

Tune the detector against actual playback so each meaningful crash produces a clear event without repeated retriggering from its decay.

Use bounded event state and sensible retrigger behavior.

Do not impose a long arbitrary cooldown that causes genuine crash hits to be missed.

### Event density

Allow a small, fixed maximum number of simultaneous shooting-star effects. The default should favor one clear event at a time.

If a second genuine crash arrives while a previous trail is fading, either allow a limited overlap or prioritize the new event according to a simple, predictable rule.

Do not create an unbounded particle system.

### Acceptance criteria

- Shooting stars travel rapidly and visibly across the sky.
- Their duration matches the character of the crash event.
- Their brightness is striking but controlled.
- Their environmental illumination is visible.
- They remain distinct from the aurora's slower movement.
- Repeated crashes do not create continuous sky clutter.
- Event timing is verified against the actual Crash stem.

## 6. Moog: luminous glints on the LEFT mountains

### Purpose

The Moog stem should animate the mountains on the LEFT side of the river.

The effect should make selected mountain facets appear to catch, emit, and briefly throw colored light in coordination with the Moog part.

This should not make the entire left mountain range flash uniformly.

### Visual design

Use the existing faceted geometry as the basis for the effect.

Possible behaviors include:

- Short, bright glints at selected facet intersections.
- Thin luminous edge accents.
- Briefly illuminated planes.
- Small, controlled sparkles on prominent ridgelines.
- Light that appears to travel across a sequence of nearby facets.

Use a curated subset of the available mountain geometry. Prioritize visible foreground and middle-distance formations.

The left mountain range should retain its dark, faceted base appearance between events.

### Musical behavior

Analyze the Moog stem and determine which signal or event-detection method best captures its actual musical articulation.

Use a combination of:

- Transient detection for discrete notes or attacks, if appropriate.
- A short envelope for the brightness of each glint.
- Existing spectral or amplitude analysis if it provides useful distinctions.

Do not assume every Moog note is a short transient. Inspect the stem and select a suitable detector.

A note should produce a recognizable flash or glint. Sustained energy may keep selected facets gently illuminated for the duration of a note.

### Spatial design

Use a small, deterministic selection of candidate edges or facets.

Avoid randomly choosing different geometry on every frame. Any variation should be controlled and reproducible.

The effect should create the impression that the Moog is playing the left side of the valley.

### Light behavior

Use the existing material and emissive system.

Where affordable, allow the glint to contribute a restrained color accent to neighboring facets or the river.

Do not add a dynamic light for every edge or every detected note.

### Acceptance criteria

- Moog events visibly affect the LEFT mountain range.
- The right mountains do not respond identically to Moog.
- Glints are distinct, brief, and musically coordinated.
- The effect does not resemble a uniform neon outline.
- Geometry and lighting costs remain bounded.

## 7. Kick: a fast light beam traveling down the river

### Purpose

Replace the current perpendicular moving-line effect.

The desired visual is a narrow, brilliant beam or ribbon of light that travels rapidly DOWN THE CENTER OF THE RIVER, following its actual winding path.

Think of the clarity and directional motion of a musical lane in a rhythm game, but integrated into the reflective mountain environment.

It must not look like a flat line moving sideways across the river.

### Geometry and path

Inspect the existing river representation and current implementation.

Use the river's actual centerline or another coherent path representation to determine the light's trajectory.

The effect must follow the river's bends and perspective.

The beam should remain aligned with the river as it recedes into the distance.

Avoid a straight world-space trajectory that merely intersects the river.

If the existing river geometry does not expose a useful path, introduce a small, reusable path representation shared by the river and the traveling-light effect. Do not create an unrelated path that can drift out of alignment with the water.

### Visual form

The beam should be narrow, bright, and visually legible.

Evaluate two complementary appearances:

1. **Underwater beam:** a concentrated band of colored light visible beneath or through the reflective surface.
2. **Emergent light:** short, upward-projecting shafts or rays that rise from the river surface as the beam passes.

The primary effect should be one continuous, fast-moving colored light pulse. The upward rays are accents, not a permanent field of spikes.

Use one dominant, hot, saturated color for Kick. Select a color that stands apart from the Snare treatment while remaining harmonious with the aurora palette.

Do not use a thick opaque line that covers the water or destroys its reflections.

The beam should look embedded in the water and integrated into the landscape.

### Motion

On a valid Kick event:

- Launch a bright pulse at the configured origin or upstream end of the visible river.
- Move it rapidly along the river path.
- Give it a clearly defined leading edge.
- Allow a short trailing glow or fading wake.
- Remove the event cleanly after it reaches the end of its trajectory.

The event should be fast enough to read as a kick accent, not a slowly moving ambient wave.

Tune the travel duration using actual Sabertooth playback. Avoid assuming a fixed speed without reviewing the scene's river length and camera perspective.

### Light interaction

As the beam travels:

- The water surface should brighten locally.
- A subtle colored reflection or wake should follow it.
- Nearby terrain may receive a restrained color accent where the river passes close to the shoreline.
- The effect should be visible even when the baseline water is dark.
- The underlying mountain reflections should remain recognizable.

Use a shader-driven emissive band, localized material response, or another inexpensive approximation.

Do not implement full volumetric light transport or complex real-time caustics.

### Acceptance criteria

- The Kick effect follows the winding river's centerline.
- It reads as a narrow, bright beam traveling downstream.
- It has a clear beginning, fast movement, and controlled decay.
- The effect does not resemble the existing perpendicular line.
- The water remains visually readable.
- The beam has a distinct and consistent color identity.
- Repeated Kick events cannot produce unbounded accumulated trails or geometry.

## 8. Snare: a contrasting river beam and water ripple

### Purpose

Snare should use the same fundamental river-light system as Kick, but with a distinct color and visual character.

The two effects should clearly belong to the same family while remaining distinguishable in a busy musical passage.

### Color

Choose a second vivid, saturated color that contrasts clearly with Kick.

Do not select colors in isolation. Test both effects together against the aurora palette and mountain materials.

The colors should remain distinguishable in the rendered output, including when bloom is enabled.

### Beam behavior

On a valid Snare event:

- Trigger a narrow pulse along the same river centerline.
- Use its own color and a slightly different temporal profile from Kick.
- Allow the pulse to interact with the water reflection.
- Use a shorter, sharper visual envelope if that matches the actual Snare sound.

Reuse the same path-following implementation. Do not build a second independent river system.

Kick and Snare may occur close together. Support bounded overlap without excessive brightness or visual confusion.

### Water ripples

Kick and Snare should also create water ripples that spread across the river surface.

The ripple is a secondary event layered on top of the traveling beam.

Implement the ripple using the least expensive technique that looks convincing with the current water shader. Possible approaches include a localized shader disturbance, a small animated ripple pattern, or a similar existing water effect.

The ripple should:

- Originate near the projected event position.
- Spread across the surface.
- Distort the reflected aurora and mountain silhouettes locally.
- Fade quickly enough that the river does not become permanently turbulent.
- Preserve the broader mirror-like appearance.

Avoid large, perfectly uniform concentric rings that look like a water-drop simulation pasted over the scene.

The effect should feel like an energetic disturbance passing through the water.

### Timing

Kick and Snare must remain distinct at the event level.

Do not combine them into a single generic drum envelope that triggers the same effect for both.

Use their individual stem signals and tune their attacks and decay against actual playback.

### Acceptance criteria

- Kick and Snare have distinct colors and temporal profiles.
- Both follow the same river path.
- Both can create visible water disturbances.
- Their reflections remain attractive.
- Close-together events remain visually legible.
- Repeated hits do not saturate the entire river with light or ripples.

## 9. Toms and tone block: responsive RIGHT mountains

### Purpose

The Toms and tone block stems should control the RIGHT side of the river valley.

Use the same broad visual language as Moog's left-side response — glints, facet illumination, edge pulses, and brief luminous accents — but give the right side its own identity.

The intention is to create a visual dialogue between the left and right mountain ranges.

### Spatial assignment

- Moog: LEFT mountain range.
- Toms: RIGHT mountain range.
- Tone block: RIGHT mountain range.

These spatial assignments are intentional and should remain clearly distinguishable in the rendered scene.

Do not route these stems into the same global mountain brightness control.

### Toms

Use the Toms stem to trigger more substantial, rhythmic accents on selected right-side facets.

Possible responses:

- Brief flashes on prominent planes.
- A small sequence of glints moving across nearby facets.
- A short-lived luminous edge pulse.
- A restrained, broader glow on strong tom hits.

The effect should match the actual tom articulation. Strong hits may create a more prominent response than weaker events.

Avoid making every tom hit illuminate the entire mountain range.

### Tone block

Use the tone block stem for a different kind of response on the same right-side mountain range.

Potential behaviors:

- Smaller, sharper facet glints.
- Short, precise flashes on selected edges.
- A repeating sequence of illuminated facets when the music contains repeated tone-block patterns.
- Subtle changes in the right-side mountain palette.

Tune the behavior against the actual stem. Do not assume the instrument's articulation or note pattern without inspecting it.

### Shared implementation

Reuse the mountain glint and lighting system developed for Moog.

Provide separate stem inputs, candidate geometry selections, intensity controls, and envelope parameters.

The right side should not simply mirror the left side's animation. The two sides may use similar materials and lighting techniques but must respond to their own instruments.

### Acceptance criteria

- Toms produce distinct visual accents on the RIGHT mountains.
- Tone block produces its own recognizable response on the RIGHT mountains.
- Moog remains assigned to the LEFT mountains.
- The spatial separation is obvious from the performance camera.
- The three stem mappings remain distinguishable when played together.

## 10. Bass and HH: investigate before assigning effects

These stems are deliberately left without a fixed visual assignment.

Do not force a visual effect onto them simply to use every available stem.

After the core mappings are working, inspect their actual musical content and the complete scene during playback.

### Bass

Potential future uses include:

- A very subtle continuous modulation of river flow.
- A low-frequency contribution to overall environmental energy.
- A restrained underlying intensity for selected mountain-edge effects.

Do not let Bass compete with Cloud Diving for control of the aurora's dominant color and large-scale swells.

Do not introduce geometry deformation or a new global pulse unless playback demonstrates that it improves the scene.

### HH

Potential future uses include:

- Extremely restrained micro-glints on selected facets.
- Fine-scale sparkle on a limited subset of mountain edges.
- Subtle water-surface highlights.

Do not create constant noise or excessive flicker.

Hi-hats may contain dense transients. If the effect becomes a continuous stream of flashes, do not enable it.

### Decision process

Only assign either stem a new role if:

1. The stem contains a recognizable musical pattern suitable for a distinct visual response.
2. The response adds something that the existing mappings do not already provide.
3. The behavior is legible and musically satisfying in the full scene.
4. Its cost is acceptable.

Otherwise, leave the stem unassigned and document the decision.

## 11. Audio timing and event accuracy

This is a central requirement, not a finishing detail.

The scene should be tightly synchronized to the actual track.

Investigate and verify:

- Stem alignment with the full mix.
- The relationship between audio playback time and analysis time.
- Whether stem detectors operate at sample, block, or frame granularity.
- Whether the current signal architecture introduces unnecessary frame delays.
- Whether event detection misses strong peaks or fires repeatedly on a single transient.
- Whether smoothing or envelope stages delay the visual attack.
- Whether visual events remain synchronized over the full duration of the track.

Use the existing stem-analysis and signal-routing architecture.

Prefer direct, correctly timed signals and event triggers over unnecessary layers of envelopes and macros.

Do not add smoothing to a transient signal without evaluating its impact on timing.

For transient effects, distinguish the time at which an event is detected from the duration of its visible animation. The animation may have a designed decay, but its onset should remain as close as the engine's architecture permits to the detected musical event.

If the engine's update cadence or analysis windows impose unavoidable latency, measure and document it.

Do not claim sample-accurate behavior unless it has actually been implemented and verified.

## 12. Scene controls and diagnostics

Use existing scene configuration and parameter conventions.

Where appropriate, expose a small number of useful controls for:

- Aurora swell intensity.
- Aurora color response.
- Shooting-star brightness and travel duration.
- Mountain-glint intensity.
- Kick beam color and travel duration.
- Snare beam color and travel duration.
- Water-ripple strength.
- Overall transient sensitivity.
- Optional Bass and HH experiments.

Do not create a sprawling panel of parameters for every individual facet or shader detail.

Provide a way to isolate stem contributions during development using existing diagnostics or a minimal scene-specific debug mechanism.

If the engine already has suitable signal meters, parameter inspectors, or audio-analysis visualization tools, reuse them.

Any temporary debugging UI must be disabled in the final performance presentation.

## 13. Implementation and validation order

Work through the following stages.

### Stage A — Asset and timing validation

- Inventory the Sabertooth files.
- Verify stem synchronization and playback integration.
- Confirm that all named stems can be loaded and analyzed.
- Identify suitable existing signals and transient detectors.
- Establish the baseline performance of the existing Aurora Borealis scene.

Do not begin tuning visual mappings until the audio inputs are verified.

### Stage B — Cloud Diving and Crash

Implement and test the two sky behaviors first.

Cloud Diving should control sustained aurora evolution.

Crash should trigger the fast shooting-star event.

Test both stems independently and then together. Verify that crash events remain visible during large aurora swells.

### Stage C — Mountain instrument mapping

Implement Moog on the left and Toms/tone block on the right.

First validate the mountain glint system using deterministic test events or existing diagnostic controls. Then connect it to the real stem signals.

Tune the effect so the left/right division is clear without excessive flicker.

### Stage D — Kick and Snare river system

Correct the current perpendicular line implementation.

Establish the actual river centerline and build a single reusable path-following effect.

Implement the Kick beam first, then add Snare with its distinct color and temporal behavior.

Add localized water ripples after the beam itself is visually convincing.

Test closely spaced Kick and Snare events.

### Stage E — Full musical integration

Play Sabertooth from the beginning through representative sections.

Review:

- Quiet passages.
- Large Cloud Diving swells.
- Individual crashes.
- Kick and Snare patterns.
- Moog phrases.
- Tom and tone-block passages.
- Sections where several instruments are active simultaneously.

Tune intensity, duration, and overlap based on the rendered result.

Only then experiment with Bass and HH.

### Stage F — Performance and reliability

Profile the complete scene with all accepted effects enabled.

Remove or simplify any effect that causes unacceptable frame-time spikes.

Validate scene initialization, repeated playback, and scene teardown.

Ensure that transient events are bounded and that effects do not accumulate indefinitely.

## 14. Performance requirements

Target sustained 30 FPS at 1920 × 1080 in the established live rendering configuration.

The complete scene must meet the target with the actual Sabertooth stems playing and representative high-cost effects active.

Prefer:

- Shader-driven movement.
- Reusable geometry.
- Small, bounded effect pools.
- Cached resources.
- Existing emissive and bloom systems.
- Limited localized lighting or inexpensive lighting approximations.
- Existing water and reflection infrastructure.

Avoid:

- A dynamic light per mountain edge.
- A particle per hi-hat.
- Unbounded shooting-star or ripple events.
- Per-frame CPU regeneration of terrain or beam geometry.
- Unnecessary render passes.
- A complex underwater simulation.
- New general-purpose engine frameworks for scene-specific effects.

Measure CPU and GPU timing separately where possible.

Record the final resolution, settings, sustained FPS, and observed frame-time spikes. Profile the combined scene rather than extrapolating from isolated effects.

If the scene fails the target, address the measured bottleneck. Do not hide the problem by reporting only average FPS or testing with major effects disabled.

## 15. Final acceptance criteria

### Audio

- [ ] Sabertooth is the active development track.
- [ ] The main mix and individual stems are synchronized.
- [ ] Each required stem is available and routed correctly.
- [ ] Transient detection is verified against the actual audio.
- [ ] Event timing and known latency limitations are documented.

### Visual behavior

- [ ] Cloud Diving drives the aurora's sustained color, shape, and intensity.
- [ ] Crash triggers fast, bright shooting stars with a visible environmental lighting response.
- [ ] Moog controls glints and luminous accents on the LEFT mountains.
- [ ] Kick sends a bright, narrow beam rapidly along the river centerline.
- [ ] Snare sends a distinctively colored beam along the same path.
- [ ] Kick and Snare can produce localized water ripples.
- [ ] Toms control accents on the RIGHT mountains.
- [ ] Tone block controls a separate, recognizable response on the RIGHT mountains.
- [ ] Bass and HH remain optional until their roles are justified.
- [ ] The scene remains coherent when several stems are active together.

### Performance and engineering

- [ ] The final combined configuration is tested at 1080p.
- [ ] Sustained 30 FPS is demonstrated or the remaining verification limitation is explicit.
- [ ] CPU/GPU timings and representative frame-time spikes are recorded.
- [ ] Effects use bounded resources and predictable lifetimes.
- [ ] Existing engine architecture is reused.
- [ ] Relevant tests and checks pass.
- [ ] The final result is reviewed using actual rendered screenshots or clips.

## 16. Final report

At completion, provide:

1. The actual audio asset inventory and synchronization findings.
2. The final stem-to-visual mapping.
3. The shooting-star implementation and its environmental response.
4. The mountain-glint implementation and left/right spatial separation.
5. The Kick/Snare beam and ripple implementation.
6. Any useful findings or optional mappings for Bass and HH.
7. Representative rendered footage or screenshots.
8. Measured CPU/GPU performance at the intended live resolution.
9. Tests, validation results, and any remaining limitations.

Be explicit about any visual behavior that remains incomplete or any performance result that has not been verified.

## Guiding principle

**Make each stem feel like it is playing a different part of the environment.**

The aurora swells with Cloud Diving. Crashes streak across the sky. Moog plays the left mountains. Toms and tone block play the right mountains. Kick and Snare send brilliant, colored energy through the river.

The result should feel tightly synchronized, visually intentional, and musically expressive — while remaining a stable 30 FPS live scene.
