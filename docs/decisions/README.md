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
| [092](ADR-092-the-world-editor.md) | The world editor: a ghost under the cursor, a record of every edit | Accepted |
| [093](ADR-093-world-navigation.md) | World navigation: a hybrid graph, per-instance obstacles, and a character with somewhere to go | Accepted |
| [094](ADR-094-ai-control-plane.md) | The AI control plane: a tool API the engine owns, and one client of it | Accepted |
| [095](ADR-095-the-help-system.md) | The Help system, staged around "Help should never lie" | Accepted |
| [096](ADR-096-actions-and-intent.md) | Actions, schedules and interactions: the intent layer | Accepted |
| [097](ADR-097-spatial-reactivity.md) | A field scales the reactions an entity already has | Accepted |
| [098](ADR-098-cinematic-events.md) | Cinematic events: three tiers, and where the bake stops | Accepted |
| [099](ADR-099-water.md) | Water occupies a channel | Accepted |
| [100](ADR-100-city-layout.md) | A city is a plan, and the plan is what places the buildings | Accepted |
| [101](ADR-101-edit-system.md) | Editing is an application concern, and the history belongs to the application | Accepted |
| [102](ADR-102-the-transport.md) | The transport owns the timeline position; audio follows it | Accepted |
| [103](ADR-103-audio-arrangement.md) | Several audio files are one arrangement, mixed down to one buffer | Accepted |
| [104](ADR-104-designating-heroes.md) | A hero is declared by starring the object that is one | Accepted |
| [105](ADR-105-the-directed-camera-follows-its-heroes.md) | A directed camera follows its heroes until somebody else takes it | Accepted |
| [106](ADR-106-a-hero-follows-its-object.md) | A hero follows the object it describes | Accepted |
| [107](ADR-107-a-hero-is-one-object.md) | A hero is one object | Accepted |
| [108](ADR-108-one-spatial-instance.md) | One spatial instance, drawn once per material | Accepted |
| [109](ADR-109-a-scene-has-an-identity.md) | A scene has an identity, not just an address | Accepted |
| [110](ADR-110-lod0-through-meshoptimizer.md) | LOD0 through meshoptimizer, like every rung below it | Accepted |
| [111](ADR-111-shadow-terms-use-the-geometric-normal.md) | Shadow terms use the geometric normal, not the shading normal | Accepted |
| [112](ADR-112-the-shadowed-range-is-sized-to-the-texel.md) | The shadowed range is sized to the texel, not to the world | Accepted |
| [113](ADR-113-a-measurement-carries-its-conditions.md) | A measurement carries its conditions, and a comparison lives in one process | Accepted |
| [114](ADR-114-cluster-occupancy-is-measured-uncapped.md) | Cluster occupancy is measured before the cap, not after it | Accepted |
| [115](ADR-115-overdraw-is-counted-in-its-own-pass.md) | Overdraw is counted in its own pass, or not at all | Accepted |
| [116](ADR-116-sigpipe-is-ignored-process-wide.md) | SIGPIPE is ignored process-wide, not blocked per thread | Accepted |
| [117](ADR-117-an-ab-arm-may-be-a-quality-setting.md) | An A/B arm may be a quality setting, not only a missing pass | Accepted |
| [118](ADR-118-the-contact-march-is-memory-bound.md) | The contact march is the frame's largest fragment item, and it is memory-bound | Accepted |
| [119](ADR-119-attachment-bandwidth-is-not-the-constraint.md) | Attachment load/store bandwidth is not this frame's constraint | Accepted |
| [120](ADR-120-what-the-shortened-shadow-range-bought.md) | What the shortened shadow range bought, settled | Accepted |
| [121](ADR-121-framebuffer-fetch-is-not-available.md) | Framebuffer fetch is not available here, and the reason is the driver | Accepted |
| [122](ADR-122-importance-is-pixels-per-triangle.md) | A drawable's importance is measured in pixels per triangle | Accepted |
| [123](ADR-123-representation-is-two-decisions.md) | Representation is two decisions, not one ladder | Accepted |
| [124](ADR-124-the-band-is-a-band.md) | The target is a band of a few hundred pixels per triangle, not the fewest triangles | Accepted |
| [125](ADR-125-hysteresis-is-opt-in-and-never-offline.md) | Hysteresis is opt-in, and offline never gets it | Accepted |
| [126](ADR-126-glowmeres-quad-overdraw-share.md) | Glowmere's quad-overdraw share, and the instrument that had to be built to find it | Accepted |
| [128](ADR-128-the-object-cap-is-an-allocation-policy.md) | The object cap is an allocation policy, not a constant | Accepted |
| [129](ADR-129-object-data-stays-a-uniform-buffer.md) | Object data stays a uniform buffer until multi-draw indirect exists | Accepted |
| [130](ADR-130-the-object-ceiling-is-a-budget-below-the-naming-limit.md) | What remains of the object ceiling is a byte budget, kept below the naming limit | Accepted |
| [131](ADR-131-the-cheap-half-of-the-curve-is-not-measurable.md) | The cheap half of the fragment curve is not measurable, and the target stays where it is | Accepted |
| [132](ADR-132-spread-is-the-transition-mitigation.md) | Spread is the transition mitigation, and temporal AA is not a prerequisite | Accepted |
| [144](ADR-144-a-curve-carries-its-own-noise-floor.md) | A curve carries its own noise floor, or it is not a curve | Accepted |
| [145](ADR-145-a-scene-declares-its-own-density.md) | A scene declares its own density, and the declaration is what is checked | Accepted |
| [146](ADR-146-offline-is-only-half-hysteresis-free.md) | Offline is only half hysteresis-free, and the half that is not is measured | Accepted |
| [147](ADR-147-the-batch-render-path-never-selects-the-offline-tier.md) | The batch render path never selects the offline tier | Accepted |
