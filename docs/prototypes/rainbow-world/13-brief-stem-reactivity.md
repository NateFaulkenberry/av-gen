> Owner's brief, saved verbatim on 2026-10-20. Note: the stems are at `assets/audio/stems/` (gitignored), not
> `audio/stems/` as the brief says: `feline-footwear (Drums).wav`, `feline-footwear (Bass).wav`,
> `feline-footwear (Synth).wav`. In the same message the owner also asked: "investigate and fix: Rainbow World's
> slow frames. Typical frames are 22–24 ms, inside the 29.3 ms budget for 30 fps. The slowest 5% run 30–35 ms at
> the edge crossings and rainbow holds, with unexplained 50–70 ms spikes in long runs." And: "I havent seen enough
> of the world yet to evaluate lavender dusk and rainbow reactivity".

# AV Gen — Rainbow World: Stem-Driven Audio Reactivity, World FX, and Performance Showcase

## Mission

Take the existing Rainbow World scene from its current visually appealing state to a **highly musical, intensely reactive, visually spectacular audiovisual performance**.

This is an implementation task, an audio-analysis task, a VFX research task, and a performance-engineering task.

The scene already has a compelling foundation: a colorful landscape that twists, folds, and loops through itself while the camera travels rapidly around it. Preserve that identity. Do not restart the scene, replace its art direction, or turn this into an open-ended environment redesign.

The goal is to make the existing world feel like a living instrument.

As the camera travels through the folded landscape, the ground should pulse and ripple, terrain features should respond to bass, drum transients should produce sharp and satisfying visual impacts, rainbows should emit traveling waves of color, clouds and vegetation should participate in the music, and light, particles, atmospheric effects, and world-scale phenomena should respond to the actual musical arrangement.

The strongest effects should be coordinated with one another. We want a world that seems to understand the track, not a collection of independent objects with their scales connected to an amplitude meter.

The result should be an exceptionally entertaining, polished demonstration of AV Gen's new stem-based audio-analysis capability.

### Core requirements

1. Deeply analyze all three provided stems before designing the final effect mapping.
2. Research audio-reactive graphics, musical onset detection, signal smoothing, shader techniques, procedural animation, and real-time VFX.
3. Study AV Gen's actual audio-analysis, stem, signal-bus, parameter, modulation, scene, rendering, and offline-rendering implementations before changing code.
4. Build a distinctive, layered visual-response system that gives drums, bass, and synth different musical identities.
5. Make the terrain, environment, entities, atmosphere, and lighting all participate in the performance.
6. Use the actual Feline Footwear stems to develop and validate a tightly synchronized example.
7. Preserve live-performance feasibility while supporting richer offline rendering.
8. Iterate using real renders, actual playback, instrumentation, and profiling—not code inspection alone.
9. Keep the scope focused on polishing and animating the existing scene. Make additional art changes only when they clearly improve the result or support a new effect.

Do not interpret “tons of audio reactivity” as “make everything flash constantly.” The goal is a rich, varied, musically legible performance with both restraint and spectacular moments.

---

# Phase 1 — Repository Reconnaissance and Baseline

Before modifying anything, inspect the repository and identify the precise scene, its current state, and the relevant subsystems.

## 1.1 Locate and study the scene

Find the scene corresponding to the existing Rainbow World build. Inspect:

- Scene source, scene registration, configuration, and asset dependencies.
- The geometry and implementation of the twisting, folded, race-track-like world.
- Camera path, camera orientation, roll, speed, and any scripted transitions.
- Existing terrain, trees, rocks, vegetation, clouds, rainbows, materials, lighting, particles, and world effects.
- Any existing scene-specific VFX, entity animation, procedural shader, or time-based animation systems.
- Scene quality presets, live/offline categories, and scene-specific performance settings.
- Any existing audio or MIDI response.
- Relevant example scenes and their approaches to audio reactivity.
- How the scene is currently launched, rendered, previewed, and tested.

Understand the actual geometry and movement rather than assuming the scene is a conventional landscape with a conventional camera.

The world itself is the main attraction. Its folds, curves, and continuous spatial traversal create opportunities for effects to propagate through the environment as the camera approaches, crosses, and leaves them.

## 1.2 Audit the audio and stem implementation

Trace the existing implementation from source audio to rendered scene parameters:

`Audio input / stem playback → analysis → signal bus → timeline/modulation → parameters → generators/entities/world FX → renderer`

Use the actual architecture found in the repository. This diagram is a conceptual guide, not a demand to introduce another pipeline.

Determine:

- How audio sources and multiple input channels are represented.
- How a scene declares and selects audio inputs or stems.
- How stems are loaded, decoded, buffered, synchronized, and played.
- Whether stems are analyzed independently or through a shared analysis path.
- Which waveform, RMS, envelope, spectral-band, onset, beat, or other features already exist.
- Whether features are calculated per block, per frame, or at another cadence.
- How analysis timestamps relate to playback position.
- How signals reach scene parameters and shader uniforms.
- How modulation, smoothing, interpolation, clamping, and parameter ranges work.
- How offline playback, seeking, looping, and rendering affect signal timing.
- What the current live-input latency and processing costs look like.
- Which existing APIs should be reused and which genuinely need extension.

Do not implement a parallel stem-analysis system if the existing architecture already provides the required functionality.

Do not add a new abstraction merely to make the new code appear more sophisticated. Prefer the smallest robust extension that fits the current design.

## 1.3 Establish a visual and performance baseline

Capture representative frames and short clips from the existing scene.

Record:

- Current rendering resolution and frame rate.
- GPU and CPU frame times.
- GPU timings for expensive passes, where available.
- The performance of the scene with audio analysis enabled.
- The performance of the scene without any new effects.
- Existing material, lighting, particle, and post-processing costs.
- Behavior at the intended live/projector output resolution.
- Any visible popping, clipping, aliasing, or artifacts during rapid camera motion.

Keep a baseline capture so that later comparisons are meaningful.

The existing scene's quality is the baseline to preserve. Do not trade away its landscape, rainbow structures, or camera traversal to fund gratuitous effects.

---

# Phase 2 — Deep Analysis of Feline Footwear

This is a mandatory research phase, not a quick file-inspection step.

The repository contains three stereo WAV stems in:

`audio/stems/`

Expected roles:

- Drums
- Bass
- Synth

First locate the exact files, inspect their metadata, and verify that the files are usable. Do not assume filenames, duration, sample rate, encoding, or channel layouts without checking.

## 2.1 Analyze the actual audio

Listen to the complete stems individually and together. If available, use waveform visualization, spectrograms, short repeated listening windows, and scripted analysis.

Write a reproducible Python analysis script if it makes the process more efficient. Use appropriate libraries already available in the environment, or install dependencies only if consistent with repository practices.

For each stem, inspect:

- Duration and sample rate.
- Channel count and whether left/right differences matter.
- Peak amplitude, RMS level, and crest factor.
- Loudness or energy distribution over time.
- Short-time energy and amplitude envelopes.
- Frequency distribution and spectral evolution.
- Transient density and temporal spacing.
- Quiet passages, sustained passages, buildups, accents, fills, and major changes in activity.
- Repeated patterns and changes in those patterns.
- Any timing or alignment discrepancies between stems.

Generate useful visualizations: waveform overview, energy envelopes, spectrograms, and a combined timeline that makes relationships between the stems easy to inspect.

Do not mistake a spectrogram or onset detector for listening. Use the measurements to guide careful listening and interpretation.

## 2.2 Determine what the instruments actually do

Do not stop at generic statements such as “drums are rhythmic” or “bass is low frequency.”

Listen to what is actually present.

For the drum stem, identify where appropriate:

- Kick-like low-frequency transients.
- Snare or clap-like backbeats.
- Hi-hats, cymbals, shakers, and high-frequency ticks.
- Rolls, fills, rapid subdivisions, accents, and unusually strong hits.
- Quiet rhythmic sections and denser passages.

For the bass stem, identify:

- Sustained bass notes versus sharp plucks.
- Repeated rhythmic pulses.
- Longer notes and decays.
- Changes in pitch, register, density, and intensity.
- Notes or phrases that create especially strong musical accents.

For the synth stem, identify:

- Sustained pads and evolving textures.
- Arpeggios, sequences, repeated plucks, or other rhythmic figures.
- Melodic accents.
- Rising and falling energy.
- Sweeps, transitions, stabs, and longer phrases.
- Spectral brightness changes and high-frequency detail.
- Any distinctive events that would be visually recognizable.

These are possible categories, not assumptions about the content. Record what is actually audible.

If the synth stem contains multiple perceptually different elements, determine whether the existing analysis architecture can distinguish them sufficiently. Do not assume that a stereo stem equals a single simple musical role.

## 2.3 Build a musical event map

Produce a timestamped event map covering the entire track.

At minimum, identify:

- Major sections and transitions.
- Distinctive drum patterns and fills.
- Strong bass accents and changes in bass activity.
- Distinctive synth events.
- Moments where multiple stems reinforce one another.
- Quiet moments where subtle motion should remain visible.
- The strongest moments that warrant a major visual payoff.

Use seconds and, where practical, musical beat positions. Establish tempo and beat-grid confidence rather than blindly imposing a constant BPM if the track does not support it.

If the arrangement is ambiguous, document the uncertainty.

The event map must be detailed enough that another developer can select a timestamp and understand what is happening musically and what visual response is intended.

Create a compact reference document and useful plots under an appropriate scene-development or analysis-artifact directory. Avoid committing enormous temporary datasets or generated audio unnecessarily.

## 2.4 Understand the stems as a coordinated set

Verify whether all three files share the same duration, sample rate, start time, and intended alignment.

Compare the combined stems with the original track if an original mix is available. Do not invent an original mix if none exists.

Check for:

- Leading silence.
- Different file durations.
- Sample-accurate or near-sample-accurate alignment.
- Export padding.
- Different gain levels.
- Phase relationships, where relevant.
- Unexpected offsets that would cause visual effects to trigger late.

Do not assume equal stem loudness. A bass stem's raw amplitude should not be compared directly to the drum stem's amplitude without calibration.

If the three stems are intended to be played alongside one another, ensure they are synchronized on a common transport and are not being started or advanced independently.

## 2.5 Create a stem-analysis report

Before committing to a major visual implementation, write a report containing:

1. File metadata and validation results.
2. Waveform and spectrogram summaries.
3. A description of each stem's actual musical character.
4. A timestamped map of notable events.
5. Recommended analysis features for each stem.
6. A proposed instrument-to-effect mapping.
7. Timing, gain, alignment, and uncertainty findings.
8. Any limitations of the existing analysis or playback system.

**The implementation must be based on this report.** Do not use generic example mappings as a substitute for understanding Feline Footwear.

---

# Phase 3 — Independent Research

Conduct substantive research before selecting the final effect techniques.

The references below are starting points. They are not the complete research assignment.

Investigate professional real-time audiovisual work, VJ systems, shader-based animation, musical onset detection, audio-feature extraction, procedural VFX, and efficient GPU rendering.

Read technical material and inspect actual code where useful. Prefer original technical documentation, working source repositories, and detailed implementation breakdowns over superficial listicles.

## 3.1 Starting references

### Audio-reactive visuals and shader workflows

- Derivative, Audio-Reactive Visuals with GLSL:  
  https://derivative.ca/community-post/tutorial/touchdesigner-audio-reactive-visuals-glsl/73339

- Derivative, Audio-Reactive Visuals in TouchDesigner:  
  https://derivative.ca/community-post/tutorial/audio-reactive-visuals-touchdesigner/65638

- Creative Coding, Audio-Reactive Visuals:  
  https://joemighty.github.io/CreativeCoding/touchdesigner/beginner/06-audio-reactive-visuals/

Study how audio features are converted into control signals, how those signals reach shaders and visual operators, and how separate kinds of response can coexist.

These tutorials demonstrate concepts in TouchDesigner. Do not introduce TouchDesigner as a runtime dependency or copy its architecture into AV Gen unnecessarily.

### Onset detection and musical timing

- Web-Onset, a spectral-flux onset-detection experiment:  
  https://github.com/Keavon/Web-Onset

Investigate the underlying principles rather than adopting this repository blindly. Pay particular attention to spectral flux, local adaptive thresholds, transient detection, false positives, and the difference between detecting an onset and determining that it is a beat.

Also investigate established DSP references and implementations for envelope following, RMS, spectral flux, beat tracking, and time-domain transient detection.

### GPU-driven animation and procedural detail

- NVIDIA GPU Gems, Effective Water Simulation from Physical Models:  
  https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-1-effective-water-simulation-physical-models

- NVIDIA GPU Gems 2, Implementing Improved Perlin Noise:  
  https://developer.nvidia.com/gpugems/gpugems2/part-iii-high-quality-rendering/chapter-26-implementing-improved-perlin-noise

- NVIDIA GPU Gems, Rendering Water Caustics:  
  https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-2-rendering-water-caustics

- Example GPU water shader implementation:  
  https://github.com/DonBeleren/gpu-water-rendering

Investigate how continuous procedural functions, layered wave scales, dynamic normals, domain warping, and shader-driven detail can produce rich motion without excessive geometry or CPU work.

These techniques are references for principles and possible implementation patterns. Adapt them to the scene and renderer, and verify compatibility with the project's graphics API and shader conventions.

## 3.2 Expand the research independently

Find additional references that directly address:

- Audio-reactive GLSL and GPU particle systems.
- Spectral flux, onset detection, envelope following, and transient classification.
- Beat-synchronous and phrase-synchronous visual composition.
- Real-time volumetric lighting, god rays, light shafts, and atmospheric scattering.
- Procedural terrain displacement and animated material detail.
- Expanding waves, traveling pulses, ripple interference, and wave propagation.
- GPU particle simulation and efficient instancing.
- Procedural foliage and instanced vegetation animation.
- Emissive materials, bloom, light response, and temporal stability.
- Frame-rate-independent animation and deterministic time-based simulation.
- Real-time/offline quality scaling and GPU profiling.

Look for concrete source code, papers, documentation, shader examples, and implementation breakdowns.

For each useful reference, record the specific technique, why it applies, its likely cost, and whether the source code or method can be reused legally.

Do not spend the entire task collecting links. Research should inform working effects.

## 3.3 Deliverable from research

Maintain a short, useful technical research dossier containing:

- Source URL and title.
- Technique and key takeaway.
- Relevance to Rainbow World.
- Proposed adaptation.
- Expected performance implications.
- License and reuse notes when applicable.

Distinguish proven facts from design proposals and unverified assumptions.

---

# Phase 4 — Design the Scene's Musical Language

Before building effects, create an explicit mapping from the stems to the scene.

Think of the environment as a layered musical instrument with several scales of response:

1. Micro-events: individual hits, ticks, plucks, and short accents.
2. Rhythmic patterns: recurring kicks, backbeats, subdivisions, and sequences.
3. Sustained motion: bass notes, pads, and evolving textures.
4. Phrases: buildups, transitions, and changes in musical energy.
5. Combined events: moments where several stems reinforce one another.

The world should respond at all five levels where the audio supports them.

## 4.1 Drum identity: impact, rhythm, and propagation

Drums should provide the clearest and most immediate transient response.

Possible mappings include:

- Kick-like events trigger broad pulses that travel along or across the terrain.
- Strong kick accents create brief terrain compression, luminous rings, or a traveling displacement wave.
- Snare-like events trigger sharp radial bursts, branching flashes, short-lived geometry accents, or crisp color impacts.
- Hi-hat-like activity creates fine-grained sparks, tiny glints, quick light traces, or delicate high-frequency environmental motion.
- Fills trigger denser sequences of smaller events that accelerate or cascade through the world.
- Strong combined accents can create a coordinated response across multiple nearby systems.

Use the actual stem analysis to determine which of these makes sense and where they should occur.

Do not make every detected transient produce a giant explosion. Differentiate hit classes, strength, and musical context.

## 4.2 Bass identity: the physical world

Bass should make the environment feel physically alive.

Explore effects such as:

- Terrain displacement or low-amplitude broad waves.
- Traveling undulations along the folded track.
- Large-scale deformation that briefly compresses or expands a section of the landscape.
- Subtle changes in the shape or thickness of luminous world features.
- Broad pulses in ground material patterns.
- Ripple fronts traveling away from the camera or from a musical event's world-space origin.
- Low-frequency illumination that spreads through the terrain.
- Vegetation or trees responding with restrained, weighty movement.
- Slow, coherent changes in the world's visual energy.

The strongest bass response should feel substantial rather than merely brighter.

Avoid scaling the entire world on every bass hit. Favor spatially localized, smoothly propagated, layered responses that preserve the scene's geometry and camera readability.

If the track's bass is more sustained than transient, make its response continuous and expressive instead of forcing artificial percussive events.

## 4.3 Synth identity: atmosphere, color, and spectacle

The synth stem should drive effects that complement its actual texture.

Potential mappings include:

- Rainbow arches that transmit traveling bands of color or light.
- Spectral energy that changes rainbow brightness, internal motion, or emissive detail.
- Moving light shafts and volumetric-looking beams.
- Cloud illumination and subtle atmospheric color changes.
- Shimmering outlines or light traces that move through the folded world.
- Floating particles that drift, cluster, or streak according to musical energy.
- Fine procedural patterns that evolve with sustained notes.
- Sweeping visual gestures that follow melodic movement or spectral change.
- Large phrase-level events such as a coordinated environmental glow or a wave of light traveling through the landscape.

Do not equate synth energy with arbitrary hue rotation. Build a consistent palette and use brightness, spatial movement, pattern, and color selectively.

If the synth stem contains distinct transient and sustained components, use different analysis features to drive them.

## 4.4 Combined-stem interactions

The most exciting moments should occur when the stems interact.

Examples to investigate:

- A kick initiates a terrain wave; the bass sustains its movement; the synth illuminates its path.
- A snare accent triggers a brief flash that travels through rainbow geometry.
- A dense drum fill produces a rapid sequence of localized light events, while the environment retains its broader bass motion.
- A sustained synth swell gradually illuminates the world, with drum transients punctuating the crescendo.
- A major combined accent produces a carefully coordinated world-scale event, followed by a visible decay.

These are creative hypotheses. Use the event map to select actual moments where each interaction will work.

The result should feel like one coherent audiovisual system rather than three unrelated visualizers.

---

# Phase 5 — Build a Robust Analysis-to-Visual Control System

Inspect and extend the existing analysis system only as necessary.

The goal is to transform raw audio measurements into useful, stable, musically meaningful control signals.

## 5.1 Separate raw features from artistic controls

Where the current architecture supports it, distinguish:

- Raw audio measurements.
- Normalized and smoothed analysis features.
- Event detection and classification.
- Scene-specific artistic mappings.
- Final parameters consumed by entities, materials, shaders, and world FX.

Do not make scene entities independently compute FFTs or scan audio buffers.

Do not scatter hard-coded audio thresholds throughout shader code.

Prefer shared analysis features and scene-level mappings that can be inspected, tuned, and tested.

## 5.2 Analyze the right features

Select features based on their intended effect, not because every possible feature is available.

Candidates include:

- Short-time RMS and smoothed amplitude envelopes.
- Frequency-band energy.
- Spectral centroid or another measure of spectral brightness.
- Spectral flux and onset strength.
- Transient envelopes.
- Beat or pulse estimates when reliable.
- Sustained energy and slow energy trends.
- Relative energy between stems.
- Temporal patterns and section-level activity.

The exact implementation should reflect existing AV Gen capabilities and measured usefulness.

Do not create a large DSP subsystem solely to expose metrics that the scene does not use.

## 5.3 Calibrate and normalize

The three stems may differ significantly in level and crest factor.

Raw amplitudes should not directly determine artistic intensity.

Investigate per-stem gain calibration, robust normalization, envelope tracking, adaptive thresholds, and sensible clamps. Make the response useful across quiet and loud passages without amplifying background noise into constant activity.

Avoid normalization schemes that cause quiet sections to appear as intense as the loudest sections. Preserve meaningful musical dynamics.

For transient detection, investigate local adaptive thresholds and minimum event spacing. Avoid firing repeatedly on the decay of one event.

## 5.4 Smoothing and response envelopes

Different effects need different temporal behavior.

Use appropriately chosen attack and release behavior, hysteresis, interpolation, and decay envelopes.

Examples:

- Drum impacts: fast attack, short decay.
- Bass energy: smooth and weighty response with less flicker.
- Sustained synth: continuous movement and gradual evolution.
- Particle bursts: discrete events with controlled lifetimes.
- Phrase-level lighting: slower rise and fall.

Avoid a single smoothing constant for every feature.

All smoothing must behave sensibly when the render frame rate changes. Use elapsed time or a suitable time-based formulation rather than assuming a fixed frame rate.

## 5.5 Keep onset events and continuous controls distinct

A transient event and a continuously varying energy signal are different kinds of control.

Use discrete events to spawn impacts, pulses, bursts, or traveling waves.

Use continuous signals to modulate displacement amplitude, lighting, material intensity, motion speed, or particle density.

Do not continuously spawn particles at the same rate merely because an amplitude envelope is high unless that is explicitly the intended effect.

Where practical, preserve the timestamp and strength of each event so that effects can respond consistently.

## 5.6 Deterministic offline behavior

Offline rendering must be able to reproduce the same audiovisual sequence.

Determine how the existing renderer advances time, evaluates scene parameters, seeks, and handles simulation state.

Effects should be driven by the intended audio/render timeline, not by wall-clock time or assumptions about how many frames have elapsed.

For deterministic behavior:

- Derive animation from scene time where appropriate.
- Make event timing consistent with the audio timeline.
- Avoid uncontrolled randomness.
- Seed any required randomness reproducibly.
- Ensure seeking or rendering frames out of order has defined behavior, or document limitations where the current architecture cannot support it.
- Avoid frame-rate-dependent accumulations that cause offline and live renders to diverge unnecessarily.

If a particle or simulation system requires sequential state, determine whether it can be reconstructed or reset deterministically for offline rendering.

Do not compromise live responsiveness just to make offline rendering work. Find an architecture that serves both use cases.

---

# Phase 6 — Effect Development

Develop effects in stages, testing each in the actual AV Gen renderer.

Do not implement every idea at once. Build a small number of strong foundational effects, validate their musical timing, then compose them into richer combinations.

## 6.1 World-level effects

The world itself should visibly participate in the music.

Investigate:

### Traveling terrain waves

Create broad, smooth waves that travel across the existing terrain or along the folded track.

Explore a combination of low-frequency geometric displacement and higher-frequency shading detail. Ensure that waves follow the actual world geometry and do not assume a flat ground plane if the track loops, folds, or twists.

Where possible, define waves in a coordinate system that remains coherent through the world rather than in screen space.

### Layered ripple systems

Use several spatial and temporal scales:

- Broad, slow undulations.
- Medium-sized traveling ripples.
- Fine, short-lived surface detail.
- Sharper localized impacts.

Avoid simply stacking arbitrary sine waves. Tune direction, scale, phase, attenuation, and interaction so that the result feels designed.

### Traveling illumination

Explore luminous waves that propagate through terrain materials, world features, or a dedicated effect layer.

A strong drum event might initiate a bright traveling front; bass energy might sustain a broader glow behind it; synth energy might contribute color or fine structure.

Avoid expensive full-screen effects when a material-level or localized shader approach can achieve the same result.

### World-scale accents

Reserve a small number of stronger effects for the most compelling musical moments.

Potential examples include a wave of light racing through the folded landscape, coordinated illumination across the rainbow arches, or a short-lived environmental bloom accompanying a major accent.

World-scale effects should be rare enough to retain their impact.

## 6.2 Entity-level effects

Make the existing entities participate without replacing them.

### Trees and vegetation

Explore:

- Subtle, frequency-dependent sway.
- Short-lived response to strong impacts.
- Traveling illumination through foliage.
- Small clusters of particles or glints on selected events.
- Coordinated motion that avoids every tree moving identically.

Preserve silhouette and readability. Avoid violent shaking or uniformly scaling every plant.

### Rocks and terrain features

Explore:

- Localized glows or emissive seams.
- Brief impact responses.
- Ripples that cross nearby surfaces.
- Light that travels between related world features.

Do not turn every rock into a glowing object at all times.

### Clouds and atmosphere

Explore:

- Gentle illumination changes.
- Slow movement tied to sustained musical energy.
- Brief highlights or light shafts during stronger synth events.
- Subtle particle or atmospheric detail.

Maintain the current sky and cloud aesthetic. The objective is to bring them to life, not replace them.

### Rainbow structures

Treat the rainbows as major hero elements.

Explore:

- Traveling bands of brightness or color.
- Internal flowing patterns.
- Short-lived pulses that propagate along the arches.
- Localized flares where an effect intersects the camera's path.
- Coordinated response with nearby terrain waves.
- Stronger phrase-level illumination.

Preserve the rainbow's broad visual identity. Avoid excessive strobing, uncontrolled color cycling, or making the entire arch uniformly flash on every beat.

## 6.3 Particle and light effects

Develop a small library of reusable effects if the existing system supports them.

Candidate effects:

- Short transient bursts.
- Fine glints and sparkles.
- Directional streaks.
- Traveling luminous particles.
- Small clusters of particles around world features.
- Light shafts and beams.
- Expanding rings or ripple fronts.
- Localized emissive bursts.

Use particles where they add visible motion and scale. Do not add millions of particles simply to increase density.

For every effect, establish:

- What triggers it.
- Which stem or combined feature controls it.
- Its spatial origin.
- Its lifetime and attenuation.
- Its color and brightness behavior.
- Its maximum intensity.
- Its expected rendering cost.
- Whether it is supported in live and offline quality modes.

Prefer GPU-friendly instancing and shader-driven animation where compatible with the existing architecture.

---

# Phase 7 — Musical Composition and Timing

The visual performance must be designed around the actual arrangement.

Do not merely enable every effect for the entire track and call it finished.

## 7.1 Build an effect timeline

Using the stem-analysis report, create a timeline that maps notable musical events to expected visual responses.

For each event, specify:

- Timestamp and, where useful, beat position.
- Relevant stem or stems.
- Detected feature or event.
- Effect family.
- Intensity and duration.
- Spatial scope.
- Intended relationship to other effects.
- Expected visual outcome.

Use this to verify synchronization and to identify places where the scene is either too quiet or visually overloaded.

## 7.2 Support both continuous and event-driven behavior

The scene should not go dead between drum hits.

Maintain subtle continuous motion through suitable low-level controls: terrain undulation, material evolution, vegetation movement, atmospheric illumination, and other inexpensive detail.

Use distinct transient events for accents and discrete effects.

Use longer envelopes or arrangement-level controls for swells and transitions.

## 7.3 Preserve contrast

Build an intentional intensity hierarchy.

- Background motion keeps the world alive.
- Regular events establish the rhythmic pulse.
- Strong accents produce visible local effects.
- Combined musical events produce larger coordinated effects.
- Major musical transitions can trigger rare, spectacular responses.

Do not make every beat a maximum-intensity event.

## 7.4 Camera-aware staging

The camera travels rapidly through the world. Effects must read at that speed.

Consider the camera's current position, orientation, and travel direction when placing or triggering effects.

Prioritize:

- Strong silhouettes.
- Clear spatial origins.
- Traveling effects that can be followed visually.
- Effects that enter the frame at useful moments.
- Near-field detail that survives motion.
- Distant effects that provide scale and depth.
- Avoiding visual clutter at the center of the frame.

Where useful, initiate effects ahead of the camera so the viewer can see them develop or travel into view.

Do not make every effect a screen-space overlay. The world should feel physically animated.

## 7.5 Avoid visual fatigue

This is a colorful, fast-moving scene intended for performance and projection.

Avoid:

- Constant full-screen flashes.
- Excessive white clipping.
- High-frequency flicker.
- Simultaneous maximum brightness across all systems.
- Repetitive identical bursts.
- Effects that obscure the world or make camera movement difficult to read.

Strong effects should feel powerful because they are composed, not because everything is always at maximum intensity.

---

# Phase 8 — Implement, Render, Listen, and Iterate

Use short development cycles.

For each meaningful implementation stage:

1. Build the scene.
2. Play the relevant portion of Feline Footwear with its stems synchronized.
3. Capture representative frames and short video clips.
4. Compare the visuals against the musical event map.
5. Inspect timing, response, composition, and artifacts.
6. Profile CPU and GPU costs.
7. Refine the effect or remove it if it does not improve the result.

Do not rely exclusively on static screenshots. Audio-reactive effects must be evaluated in motion and in relation to the actual music.

## 8.1 First working milestone

Start with three strong, distinct responses:

1. A drum-triggered transient effect with accurate timing.
2. A bass-driven terrain or world-motion effect.
3. A synth-driven rainbow, lighting, or atmospheric effect.

Make each one visibly effective in isolation.

Then combine them and verify that they complement one another rather than fighting for attention.

## 8.2 Second milestone

Add richer interactions:

- Traveling waves.
- Secondary ripples.
- Entity-level motion.
- Particle accents.
- Coordinated light propagation.
- Combined-stem interactions.
- Phrase-level effects.

Prioritize a few well-integrated systems over a large collection of disconnected gimmicks.

## 8.3 Third milestone

Compose the complete Feline Footwear showcase.

Review the entire track, not just the best few seconds.

Look for repetitive patterns, weak sections, excessive effects, missed accents, and opportunities for more convincing musical synchronization.

Make the full journey entertaining. The camera's changing perspective should reveal different effects and combinations as the world unfolds.

---

# Phase 9 — Live and Offline Requirements

Rainbow World is expected to remain useful in both LIVE and offline scene categories.

The same scene and effect concepts should work in both contexts, but rendering quality and analysis capabilities may differ.

## 9.1 Live performance

Live mode must remain practical for an audiovisual instrument.

Consider:

- Real-time input latency.
- Audio-analysis cost and update cadence.
- GPU and CPU budgets.
- Stable behavior when frame rate fluctuates.
- Predictable controls and effect intensity.
- Performance at the intended projector resolution.
- A sensible fallback when stems are unavailable.
- Behavior when the selected live audio source changes.

Do not require pre-analyzed Feline Footwear data for ordinary live operation. The stems are a development and offline showcase resource, not a hard dependency for the scene to function.

Where live analysis lacks reliable knowledge of future events, effects must respond to available real-time signals without assuming that the next beat is already known.

Do not introduce a major new latency cost in pursuit of more elaborate beat prediction.

## 9.2 Offline rendering

Offline mode can support more elaborate and deterministic analysis-driven behavior.

Use the stem feature as the primary demonstration of the new capability:

- Each stem must remain independently addressable.
- Analysis and playback must remain synchronized.
- Effects should evaluate against the correct audio timestamp.
- Offline rendering should reproduce the intended timing reliably.
- More expensive effects may be enabled when appropriate.
- Quality differences should be intentional and documented.

If the existing architecture permits a shared scene configuration with different quality settings, use it rather than maintaining unrelated live and offline versions of the scene.

## 9.3 Quality tiers

Investigate the existing scene-quality mechanisms before introducing new ones.

If needed, define sensible tiers that preserve the same musical behavior while varying rendering cost.

Examples of scalable dimensions:

- Particle density and lifetime.
- Secondary ripple detail.
- Volumetric or light-shaft quality.
- Resolution or update rate of auxiliary effects.
- Number of simultaneous effect instances.
- Additional offline-only refinement.

Avoid changing the core timing or musical identity between tiers.

---

# Phase 10 — Performance and Rendering Quality

The existing scene has previously been considered for projector/live use with a 30 FPS target. Treat that as the minimum acceptance target for the intended live configuration, not permission to waste the entire GPU budget.

Establish the actual hardware, resolution, quality preset, and baseline before setting final numerical budgets.

## 10.1 Profile every expensive system

Measure the cost of new effects independently and in combination.

Investigate:

- Shader instruction cost.
- Additional render passes.
- Overdraw and transparent particles.
- Full-screen post-processing.
- GPU synchronization.
- CPU-side per-frame work.
- Particle simulation and draw submission.
- Geometry updates and deformation.
- Texture allocation and bandwidth.
- Any additional audio-analysis work.

Do not guess at the bottleneck.

## 10.2 Preserve quality intelligently

Prefer techniques such as:

- Shader-based deformation instead of expensive CPU geometry updates.
- Instancing for repeated entities.
- Shared analysis features instead of redundant FFTs.
- GPU-driven animation where it fits the renderer.
- Localized effects instead of unnecessary full-screen passes.
- Appropriate LOD and effect culling.
- Stable, low-cost material animation.
- Reusing existing lighting, bloom, and render infrastructure.

Do not optimize away the effects that make the scene special without testing alternatives.

## 10.3 Test realistic scenarios

Profile at least:

- Scene with audio reactivity disabled.
- Scene with each stem's effects enabled independently.
- Scene with all effects active.
- A dense musical section with many simultaneous events.
- A quiet section with continuous background motion.
- The intended live/projector resolution.
- The offline quality configuration.

Capture frame-time distributions and worst-case behavior, not just average FPS.

---

# Phase 11 — Visual Acceptance Criteria

The pass is successful when the following statements are true.

## Musical synchronization

- Drum impacts visibly align with actual drum events.
- Bass activity drives substantial, coherent world motion.
- Synth activity creates a distinctive atmospheric and luminous response.
- The three stems have clearly different visual identities.
- Combined musical events create more interesting responses than isolated features alone.
- Sustained notes and quieter sections remain expressive without requiring artificial flashes.
- Timing is consistent during playback and offline rendering.

## Visual quality

- The world feels alive even between major transients.
- Terrain and environmental effects feel connected to the existing folded geometry.
- Rainbows remain strong visual anchors and become expressive participants in the performance.
- Entity effects are varied and spatially coherent.
- The camera's movement reveals the effects rather than hiding them.
- The scene contains both subtle detail and spectacular accents.
- The overall visual language remains consistent with the current art direction.
- No single effect overwhelms the composition for the entire duration.

## Performance

- The intended live configuration sustains the established 30 FPS target at the measured target resolution and hardware.
- CPU and GPU costs are measured and documented.
- Expensive effects degrade gracefully or can be disabled through existing quality mechanisms.
- No avoidable stalls, synchronization problems, or excessive memory growth are introduced.
- Offline rendering preserves intended timing and reproducibility.

## Engineering quality

- Existing architecture is reused where appropriate.
- The scene remains functional without the Feline Footwear stem files.
- Missing or invalid stems fail gracefully.
- New code follows repository conventions.
- Relevant tests are added or updated.
- Existing functionality remains intact.
- Documentation explains the analysis, mapping, controls, and performance tradeoffs.

---

# Phase 12 — Final Deliverables

Produce the following artifacts in appropriate existing repository locations.

1. **Stem analysis report** — metadata, listening observations, plots, musical event map, and recommended mappings.
2. **Research dossier** — annotated technical references and how they informed the implementation.
3. **Effect mapping document** — stem features, thresholds or normalization strategy, triggers, continuous controls, effect parameters, and intended behavior.
4. **Implemented Rainbow World pass** — working, integrated, audio-reactive scene.
5. **Feline Footwear showcase** — a reproducible configuration that uses the three provided stems and demonstrates their visual interplay.
6. **Validation captures** — representative stills and short clips showing important effects and musical events.
7. **Performance report** — baseline versus final CPU/GPU timings, test resolution, hardware, and quality settings.
8. **Tests and usage documentation** — including how to load the stems, reproduce the showcase, and run the scene in live and offline contexts.

Keep generated artifacts organized. Do not commit unnecessary temporary files, enormous analysis dumps, or redundant media.

Do not modify the stems themselves unless a genuine issue is discovered and the change is explicitly documented.

---

# Working Rules

- Research the actual repository and the actual WAV files. Do not invent implementation details.
- Listen to the track. Do not treat numeric analysis as a substitute for musical judgment.
- Use the current scene as the artistic foundation.
- Do not expand the task into a major environment rebuild.
- Do not add arbitrary effects simply to increase the number of effects.
- Do not implement every speculative idea before evaluating a small prototype.
- Reuse AV Gen's existing signal, parameter, scene, and rendering architecture wherever it is appropriate.
- Do not sacrifice live performance for unnecessary complexity.
- Do not claim synchronization, determinism, or performance improvements without testing them.
- When an approach fails visually, revise or remove it rather than defending the implementation.
- Record important design decisions and explain meaningful tradeoffs.
- Keep the implementation clean, testable, and maintainable.

## Initial checkpoint

Begin with repository reconnaissance, stem analysis, independent research, and a concrete effect-mapping proposal.

Then proceed to the first working prototype: one drum-driven effect, one bass-driven effect, and one synth-driven effect, all demonstrated against the actual stems in the existing scene.

After that prototype has been evaluated in motion and profiled, continue toward the complete showcase.

**Final objective:** When someone watches Rainbow World with Feline Footwear, they should feel that the landscape, light, atmosphere, and geometry are all participating in the music. The world should have rhythm, weight, texture, anticipation, and release—and it should achieve that without losing the performance-ready character of AV Gen.
