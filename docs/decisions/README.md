# Architecture Decision Records

Each record follows: Status, Context/Problem, Alternatives considered, Decision, Rationale,
Consequences, Rejected alternatives (with the decisive reason), and Revisit triggers. Records are
immutable once Accepted; a change is a new record that supersedes the old one.

| ADR | Title | Status |
|---|---|---|
| [001](ADR-001-rendering-backend.md) | Rendering backend: WebGPU via Dawn behind a thin `gpu` module | Accepted |
| [002](ADR-002-windowing.md) | Windowing and input: SDL3 | Accepted |
| [003](ADR-003-audio-engine.md) | Audio decoding and playback: miniaudio | Accepted |
| [004](ADR-004-audio-analysis.md) | Audio analysis: in-house STFT pipeline over a pluggable FFT (KissFFT first) | Accepted |
| [005](ADR-005-asset-format.md) | Asset format: glTF 2.0 canonical; procedural-only in 0.1 | Accepted |
| [006](ADR-006-shader-system.md) | Shader system: WGSL files at runtime now; ISF-style user shaders later | Accepted |
| [007](ADR-007-ui-framework.md) | UI framework: Dear ImGui (docking) + ImPlot | Accepted |
| [008](ADR-008-dependency-management.md) | Dependency management: CPM.cmake with pinned versions | Accepted |
| [009](ADR-009-testing-framework.md) | Testing: Catch2 v3 + CTest; GPU-free by default | Accepted |
| [010](ADR-010-language-and-core-libraries.md) | C++23 allow-list; spdlog/fmt, GLM, nlohmann/json | Accepted |
| [011](ADR-011-parameters-and-modulation.md) | Parameter and modulation model | Accepted |
| [012](ADR-012-time-model.md) | Time model: injected RenderTime, sample-indexed analysis | Accepted |
| [013](ADR-013-image-based-lighting.md) | Image-based lighting: runtime split-sum preprocessing on the GPU | Accepted |
| [014](ADR-014-user-shader-contract.md) | User shader contract: ISF-style header + WGSL body, background/post layers, hot reload | Accepted |
| [015](ADR-015-gpu-particles.md) | GPU particles: compute pool with dead/alive lists, indirect draw, curl noise | Accepted |
| [016](ADR-016-post-processing.md) | Post-processing chain (bloom, grading, lens, DoF, motion blur, tone operators) over a transient pool | Accepted |
| [017](ADR-017-scene-composition.md) | Scene composition: flattened node compositions, nested scene files, asset registry | Accepted |
| [018](ADR-018-timeline.md) | Timeline: keyframe automation writes finals before routes; cues recall presets | Accepted |
| [019](ADR-019-project-system.md) | Project system: relative asset references, explicit migration, bundles, recent files | Accepted |
| [020](ADR-020-offline-rendering.md) | Offline rendering: render jobs, PNG sequences, native/ffmpeg video, queue, determinism | Accepted |
| [021](ADR-021-live-control.md) | Live control: OSC and MIDI as control signals and direct parameter control, live audio input | Accepted |
| [022](ADR-022-outputs-and-sharing.md) | Outputs and sharing: final texture, per-output mapping windows, Syphon (IOSurface) and runtime NDI | Accepted |
| [023](ADR-023-procedural-geometry.md) | Procedural geometry: sources, distributions, seeded variation, GPU-instanced draw, deformer stack as data | Accepted |
| [024](ADR-024-spatial-data-and-attributes.md) | Spatial data: typed point attributes as the procedural currency | Accepted |
| [025](ADR-025-fields-and-effectors.md) | Fields as spatial control signals, effectors, GPU-identical evaluation | Accepted |
| [026](ADR-026-splines.md) | Splines as first-class spatial data | Accepted |
| [027](ADR-027-sdf-architecture.md) | SDF: data tree, GPU sphere tracing, CPU surface nets | Accepted |
| [028](ADR-028-procedural-graph-and-invalidation.md) | Procedural graph as an authoring layer; structural-hash invalidation | Accepted |
| [029](ADR-029-gpu-procedural-execution.md) | GPU point processing, culling, LOD, hierarchical generation | Accepted |
| [030](ADR-030-procedural-materials.md) | Procedural materials as an interpreted op program; colour utilities | Accepted |
| [031](ADR-031-states-macros-authoring.md) | Scene states, world macros, layered authoring, inspection, debug drawing | Accepted |
| [032](ADR-032-simulation-and-volumes.md) | Simulated fields and volumetric atmosphere | Accepted |
| [033](ADR-033-lighting-architecture.md) | Clustered forward lighting, area lights, colour temperature, light rigs | Accepted |
| [034](ADR-034-shadows-and-occlusion.md) | Cascaded shadows, contact shadows, ground-truth ambient occlusion | Accepted |
| [035](ADR-035-render-passes.md) | Auxiliary render targets and a declared frame graph | Accepted |
| [036](ADR-036-layered-materials.md) | Layered materials, geometric inputs, triplanar mapping, decals | Accepted |
| [037](ADR-037-cinematic-camera.md) | Physical camera, exposure and camera behaviours | Accepted |
| [038](ADR-038-composition.md) | Procedural composition, visual hierarchy and negative space | Accepted |
| [039](ADR-039-image-formation.md) | Image formation, selective post and tone mapping | Accepted |
| [040](ADR-040-particles-and-motion.md) | Particle trails, velocity-aware rendering and motion blur | Accepted |
| [041](ADR-041-world-director.md) | World Director, look presets and musical phrasing | Accepted |
| [042](ADR-042-bevels.md) | Bevels as a first-class operation on generated primitives | Accepted |
| [043](ADR-043-organic-curves.md) | Tubes: one swept curve stands in for every organ | Accepted |
| [044](ADR-044-imported-meshes-as-sources.md) | Imported meshes as instanced sources | Accepted |
| [045](ADR-045-vertex-clustering-decimation.md) | Vertex-clustering decimation for imported meshes | Accepted |
| [046](ADR-046-the-world-map-and-terrain.md) | The world map and terrain: geography as data, chunked ground, water | Accepted |
| [047](ADR-047-biomes.md) | Biomes: soft-edged rules over the map, blended as weights along one ordered axis | Accepted |
| [048](ADR-048-ecology.md) | Ecology: what grows where, decided by the biome weights the ground is coloured with | Accepted |
| [049](ADR-049-hdri-sky.md) | The HDRI sky: the map as the background, sky and lighting intensity split, the key light aimed from the image | Accepted |
| [050](ADR-050-material-interpreter-cost.md) | What the material interpreter was actually spending: the register file and the field evaluator in the loop | Accepted |
| [051](ADR-051-measuring-a-frame.md) | Measuring a frame, and one shared indirect buffer | Accepted |
| [052](ADR-052-highlight-chroma.md) | Holding hue in compressed highlights: chroma retention after the tone curve | Accepted |
| [053](ADR-053-ecology-light-field.md) | The light a glowing ecology casts: aggregate first, then light | Accepted |
| [054](ADR-054-colour-that-clusters.md) | Colour that clusters in space, and glow that is rare | Accepted |
| [055](ADR-055-the-wind-field.md) | A wind field, and vegetation that answers it for nothing | Accepted |
| [056](ADR-056-simulated-plants.md) | Plants that are actually simulated, for the few hundred worth simulating | Accepted |
| [057](ADR-057-the-living-chromatic-field.md) | The living chromatic field: hue that drifts in world space and time | Accepted |
| [058](ADR-058-the-air-and-the-ambient.md) | The air and the ambient | Accepted |
| [059](ADR-059-edges-and-a-block-nobody-read.md) | Edges, and a block nobody read | Accepted |
| [060](ADR-060-semantic-assets-and-world-recipes.md) | Semantic assets and world recipes | Accepted |
| [061](ADR-061-the-world-composer.md) | The world composer | Accepted |
| [062](ADR-062-the-cinematic-director.md) | The cinematic director | Accepted |
| [063](ADR-063-musical-events.md) | Musical events | Accepted |
| [064](ADR-064-the-job-system.md) | The job system | Accepted |
| [065](ADR-065-optional-ai.md) | AI is optional, replaceable, and never in the frame loop | Accepted |
| [066](ADR-066-generate-world.md) | Generate World | Accepted |
| [067](ADR-067-world-art-direction.md) | A generated world's art direction, and the negative space that was never applied | Accepted |
| [068](ADR-068-viewport-interaction.md) | Mouse control and picking in the viewport | Accepted |
| [069](ADR-069-placement.md) | Placing assets by hand | Accepted |
| [070](ADR-070-art-direction-profiles.md) | Art-direction profiles | Accepted |
| [071](ADR-071-cinematic-camera.md) | A camera vocabulary, and a camera cut to the music | Accepted |
| [072](ADR-072-heroes.md) | Heroes | Accepted |
| [073](ADR-073-musical-signals.md) | Musical signals | Accepted |
| [074](ADR-074-authored-heroes.md) | Heroes a scene can declare | Accepted |
| [075](ADR-075-camera-director-connected.md) | The camera director, connected | Accepted |
| [076](ADR-076-editor-shell.md) | The editor shell | Accepted |
| [077](ADR-077-frame-profiler.md) | The frame profiler | Accepted |
| [078](ADR-078-mesh-lod.md) | Mesh LODs from meshoptimizer, and what the existing decimator is still for | Accepted |
| [079](ADR-079-tilt-shift.md) | Tilt-shift | Accepted |
| [080](ADR-080-camera-clearance.md) | Camera clearance | Accepted |
| [081](ADR-081-shadow-stability.md) | Shadow stability | Accepted |
| [082](ADR-082-lod-stability.md) | LOD stability | Accepted |
| [083](ADR-083-composition-and-layers.md) | Composition and layers | Accepted |
| [084](ADR-084-the-editors-frame.md) | The editor's frame: where the swapchain wait stands, and what a drag may cost | Accepted |
| [085](ADR-085-lod-chain-wired.md) | The LOD ladder is built by meshoptimizer, and it has to descend | Accepted |
| [086](ADR-086-skeletal-animation.md) | Skeletal animation: skins, clips, blending and GPU skinning | Accepted |
| [087](ADR-087-shadow-mask.md) | A half-resolution screen-space shadow mask | Accepted |
| [088](ADR-088-entities.md) | Entities and behaviour: driving nodes from signals | Accepted |
| [089](ADR-089-the-cinematic-sequence.md) | The cinematic sequence: choreography through time | Accepted |
| [090](ADR-090-terrain-generation-and-queries.md) | Terrain with geography in it, and one place to ask where the ground is | Accepted |
| [091](ADR-091-simulation-authority.md) | Two-tier simulation authority: what bakes, what lives, and what a scrub may do | Accepted |
