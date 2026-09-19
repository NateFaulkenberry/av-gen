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
| [133](ADR-133-material-tiers-are-three-rungs-selected-by-a-uniform.md) | A material tier is three rungs selected by a per-draw uniform | Accepted |
| [134](ADR-134-one-quality-policy-object.md) | A mode is one policy object, not four independently-set structs | Accepted |
| [135](ADR-135-object-uniforms-has-no-free-lane-for-a-tier.md) | `ObjectUniforms` has no free lane, so the material tier is frame-global for now | Accepted |
| [136](ADR-136-what-the-shading-tiers-are-worth-and-what-that-says-about-the-residual.md) | What the shading tiers are worth, and what that says about the 8.4 ms residual | Accepted |
| [137](ADR-137-render-scale-is-a-tier-parameter-and-is-not-applied-yet.md) | Render scale is a tier parameter, and the tonemap binding is what stops it being applied | Accepted |
| [138](ADR-138-what-tier-assignment-can-realize-on-glowmere.md) | What tier assignment can realize on Glowmere is bounded by terrain coverage | Accepted |
| [139](ADR-139-the-volume-has-two-scalability-axes.md) | The volume has two scalability axes, they are tier parameters, and they do not divide evenly | Accepted |
| [140](ADR-140-the-march-and-the-composite-are-two-costs.md) | The march and the composite are two costs, and one of them is below the instrument | Accepted |
| [141](ADR-141-half-the-march-is-not-pixel-work.md) | Half the march is not pixel work, and Constellation's noise floor is not one number | Accepted |
| [142](ADR-142-a-quality-arm-can-be-looked-at.md) | A quality arm can be looked at, not only timed | Accepted |
| [143](ADR-143-temporal-reprojection-of-the-volume-is-rejected.md) | Temporal reprojection of the volume is rejected, because a policy parameter already reaches its ceiling | Accepted |
| [144](ADR-144-a-curve-carries-its-own-noise-floor.md) | A curve carries its own noise floor, or it is not a curve | Accepted |
| [145](ADR-145-a-scene-declares-its-own-density.md) | A scene declares its own density, and the declaration is what is checked | Accepted |
| [146](ADR-146-offline-is-only-half-hysteresis-free.md) | Offline is only half hysteresis-free, and the half that is not is measured | Accepted |
| [147](ADR-147-the-batch-render-path-never-selects-the-offline-tier.md) | The batch render path never selects the offline tier | Accepted |
| [148](ADR-148-the-noise-floor-is-four-numbers.md) | The noise floor is four numbers, and the one that binds is usually the pairs disagreeing | Accepted |
| [150](ADR-150-the-ceiling-is-measured-on-the-frame.md) | The ceiling of a representation system is measured on the frame, not on the curve | Accepted |
| [151](ADR-151-impostors-and-hlod-proxies-are-not-justified-on-this-content.md) | Impostors and HLOD proxies are not justified on this content, and the reason is measured | Accepted |
| [152](ADR-152-the-cull-ladder-omits-the-source-transform.md) | The GPU cull ladder sizes a procedural by its raw mesh, not by the object | Accepted |
| [153](ADR-153-the-impostor-rung-is-unreachable-and-would-cost-more.md) | The impostor rung exists, imported meshes cannot reach it, and reaching it would cost more | Accepted |
| [154](ADR-154-hlod-has-nothing-to-merge-here-and-the-invalidation-rule-is-recorded-anyway.md) | HLOD has nothing left to merge on instanced content, and the invalidation rule is recorded anyway | Accepted |
| [155](ADR-155-the-tier-follows-the-rung.md) | The material tier follows the LOD rung, and the ceiling is not reachable | Accepted |
| [157](ADR-157-a-depth-derived-quantity-needs-the-axis-and-the-filter.md) | A number derived from the depth buffer needs the axis correction and the filter | Accepted |
| [158](ADR-158-a-directed-shot-aims-at-its-hero-and-nothing-else-follows.md) | A directed shot's aim follows its hero, and nothing else follows | Accepted |
| [159](ADR-159-no-tap-may-step-further-than-the-texel-it-reads.md) | No tap may step further than the texel of the texture it reads | Accepted |
| [160](ADR-160-the-distance-ladder-is-authored-and-there-are-two-of-them.md) | The distance ladder is authored, all four rungs of it — and there are two ladders | Accepted |
| [161](ADR-161-root-motion-is-not-in-this-content-and-foot-slip-is-what-was-missing.md) | Root motion is not in this content, and foot slip is the thing that was missing | Accepted |
| [162](ADR-162-a-walker-off-the-navigable-set-cannot-be-steered-back-onto-it.md) | A walker that steps off the navigable set cannot be steered back onto it | Accepted |
| [170](ADR-170-the-gpu-lock-does-not-establish-exclusivity.md) | The GPU lock serialises agents, not the device, and a timing taken beside an open window is not evidence | Accepted |
| [171](ADR-171-glowmere-valley-is-two-scenes-and-the-named-one-is-the-wrong-one.md) | "Glowmere Valley" names two scenes, and the successor is built on the one that is not called that | Accepted |
| [172](ADR-172-an-aesthetic-score-component-is-a-band-not-a-maximum.md) | An aesthetic score component is a band, never a maximum | Accepted |
| [173](ADR-173-the-candidate-sampler-is-sobol-and-the-experiment-that-would-overturn-it.md) | The mushroom candidate sampler is a Sobol sequence, and the experiment that would overturn it is named | Accepted |
| [174](ADR-174-vegetation-is-banded-on-height-above-water.md) | Vegetation is banded on height above the water table, and the cap thins instead of truncating | Accepted |
| [175](ADR-175-a-searched-organism-is-a-parameter-vector-the-scene-owns.md) | A searched organism is a parameter vector the scene owns, not geometry the generator kept | Accepted |
| [176](ADR-176-a-tree-needs-an-internal-economy.md) | Space colonization decides where a tree grows; apical control decides how much | Accepted |
| [177](ADR-177-the-tree-is-skinned-not-bent.md) | The tree is skinned to a branch skeleton, because a per-tier wind uniform cannot hold its joints together | Accepted |
| [178](ADR-178-hierarchy-is-substance-not-order.md) | A branch's tier is its substance, not its depth in the graph | Accepted |
| [179](ADR-179-a-program-that-asserts-emission-owns-it.md) | A material program that asserts emission owns the whole emission contract | Accepted |
| [180](ADR-180-a-search-finds-only-what-its-parameterisation-expresses.md) | A search can only find what its parameterisation can express | Accepted |
| [181](ADR-181-counterbalance-the-arms-and-detect-the-drift.md) | Counterbalance the arms, and detect the drift the counterbalancing hides | Accepted |
| [182](ADR-182-a-diagnostic-arm-that-cannot-fail.md) | A diagnostic arm that cannot fail is worse than no arm | Accepted |
| [183](ADR-183-the-waterline-work-is-reverted-pending-a-different-approach.md) | The waterline work is reverted, pending a different approach | Accepted |
| [184](ADR-184-the-riverbank-work-is-carried-forward.md) | The riverbank work is carried forward; only the shimmer chase was reverted | Accepted |
| [185](ADR-185-a-continuous-shot-carries-its-aim-across-the-cut.md) | A continuous shot carries its aim across the cut, not just its eye | Accepted |
| [186](ADR-186-an-offline-render-lifts-the-limits-playback-needs.md) | An offline render lifts the distance limits playback needs | Accepted |
| [187](ADR-187-fxaa-gets-its-own-arm.md) | FXAA gets its own isolation arm | Accepted |
| [188](ADR-188-parenting-gets-an-appearance.md) | Parenting gets an appearance, and an offset you can type | Accepted |
| [189](ADR-189-fxaa-fades-in-rather-than-switching-on.md) | FXAA fades in rather than switching on | Accepted |
| [190](ADR-190-a-transition-comes-from-where-the-last-one-arrived.md) | A transition comes from where the last one arrived | Accepted |
| [191](ADR-191-the-lod-ladder-is-a-prefilter-not-a-saving.md) | The LOD ladder is a prefilter, not a saving | Accepted |
| [192](ADR-192-six-aliens-from-one-modular-source.md) | Six aliens from one modular source | Accepted |
| [193](ADR-193-the-entity-layer-hears-when-the-world-changes.md) | The entity layer hears when the world changes | Accepted |
| [194](ADR-194-a-body-off-the-ground.md) | A body off the ground | Accepted |
| [195](ADR-195-water-is-a-depth-a-body-is-in.md) | Water is a depth a body is in, not a line it stops at | Accepted |
| [196](ADR-196-a-solid-says-what-getting-past-it-takes.md) | A solid says what getting past it takes; the body says what it can do about that | Accepted |
| [197](ADR-197-the-navigation-layer-gets-an-appearance.md) | The navigation layer gets an appearance, and the overlay gets a camera pointed at it | Accepted |
| [198](ADR-198-secondary-motion-and-four-inhabitants.md) | Secondary motion, and four inhabitants instead of one placeholder | Accepted |
| [199](ADR-199-two-bugs-from-one-assumption.md) | Two bugs from one assumption -- that a thing is centred on its origin | Accepted |
| [200](ADR-200-the-camera-was-fast-in-a-way-the-cap-did-not-measure.md) | The camera was fast in a way the first cap did not measure | Accepted |
| [201](ADR-201-there-is-no-primary-hero.md) | There is no primary hero, only importance | Accepted |
| [202](ADR-202-importance-is-the-whole-of-casting.md) | Importance is the whole of casting | Accepted |
| [203](ADR-203-a-cap-that-broke-the-film.md) | A cap that broke the film, and the control that was missing | Accepted |
| [204](ADR-204-a-clip-starts-where-its-keys-start.md) | A clip starts where its keys start, a facing is not an odometer, and a stride speed is a claim about a clip | Accepted |
| [205](ADR-205-nine-farm-animals-from-one-turntable.md) | Nine farm animals from one turntable scene | Accepted |
| [206](ADR-206-structure-from-repetition-not-from-energy.md) | Structure from repetition, not from energy | Accepted |
| [207](ADR-207-world-effects.md) | World effects propagate through one frame-global block, not through per-object state | Accepted |
| [208](ADR-208-what-world-effects-cost.md) | What world effects cost, and the cross-binary number that is not evidence | Accepted |
| [209](ADR-209-the-kind-the-box-fix-missed.md) | The kind the box fix missed | Accepted |
| [210](ADR-210-the-director-is-a-decision-layer-not-a-behaviour-system.md) | The director is a decision layer, not a second behaviour system | Accepted |
| [211](ADR-211-a-route-you-can-click-and-an-answer-that-is-complete.md) | A route you can click, and an answer that is complete | Accepted |
| [212](ADR-212-an-offline-render-may-spend-pixels.md) | An offline render may spend pixels a realtime one cannot | Accepted |
| [213](ADR-213-an-animal-with-one-clip.md) | An animal with one clip | Accepted |
| [215](ADR-215-a-section-someone-decided-outranks-a-section-someone-detected.md) | A section somebody decided outranks a section somebody detected | Accepted |
| [216](ADR-216-the-sequencer-is-where-the-song-lives.md) | The Sequencer is where the song lives | Accepted |
| [217](ADR-217-the-cut-cannot-know-what-the-world-is-doing.md) | The cut cannot know what the world is doing, so let it be told one thing (the camera holds on a scenario's actor) | Accepted |
| [218](ADR-218-a-body-in-a-beam-has-two-positions-and-a-size.md) | A body in a beam has two positions and a size, and the scenario knew about neither | Accepted |
| [225](ADR-225-a-setting-nobody-keeps.md) | A setting the application does not keep is not a setting (the Auto-director saves with the project) | Accepted |
| [226](ADR-226-the-second-scene-with-the-same-stride-defect.md) | The second scene with the same stride defect, and the bands nobody scaled | Accepted |
| [227](ADR-227-the-blocker-search-gets-its-own-budget.md) | The blocker search gets its own budget, and the lane it needed | Accepted |
| [230](ADR-230-atmospheric-effects.md) | An atmospheric effect is a view ray, not a surface term (comets and aurora) | Accepted |
| [231](ADR-231-the-interface-says-what-it-can-do-and-what-it-is-doing.md) | The interface says what it can do, and what it is doing (cursors, context menus, the processing indicator, a threaded scatter) | Accepted |
| [232](ADR-232-a-parameter-path-carries-the-structure-it-was-saved-against.md) | A parameter path carries the structure it was saved against (the material op table that black-holed an elder) | Accepted |
| [233](ADR-233-two-things-rebuilt-that-nobody-changed.md) | Two things were rebuilt every frame that nobody had changed (the idle editor's 22 regenerations, and the sky rebuilt inside the drag) | Accepted |
| [240](ADR-240-a-placement-is-not-an-offset.md) | A placement is not an offset, a euler triple is not an orientation, a push is not a walk, and a stride speed is still a claim about a clip | Accepted |
| [241](ADR-241-the-director-says-what-it-writes.md) | The director says what it writes (the Inspector's `Kind::Staging`, declared and observed) | Accepted |
| [242](ADR-242-a-target-nobody-reads-is-not-a-feature.md) | A target nobody reads is not a feature (AOV export, and the normal pass that was not a normal) | Accepted |
| [243](ADR-243-the-detector-and-the-eye-disagree.md) | The detector and the eye disagree, and the eye decides what ships (§4 reviewed: the artifact is spatial, FXAA stays, Glowmere supersamples) | Accepted |
| [244](ADR-244-section-7-found-nothing-to-fix.md) | §7 found nothing to fix, and that moved the bottleneck (cinematic lighting reviewed; authoring ergonomics promoted) | Accepted |
| [245](ADR-245-a-camera-is-not-the-camera.md) | A camera is not *the* camera (multiple cameras, authored shots, event-driven cuts, and the one published active camera) | Accepted |
| [246](ADR-246-the-preview-is-not-a-picture-of-the-render.md) | The preview is not a picture of the render, it is the render (output preview, the output frame in the canvas, safe areas) | Accepted |
| [247](ADR-247-the-director-is-never-told-what-a-chorus-is.md) | The director is never told what a chorus is (the song shot language: extensible section types, semantic shot intents, per-field override provenance) | Accepted |
| [249](ADR-249-a-section-says-what-it-wants-not-which-camera.md) | A section says what it wants, not which camera (Song Mode: semantic shot intents, multi-camera execution, and variation that is not randomness) | Accepted |
| [250](ADR-250-the-instrument-is-not-the-engine.md) | The instrument is not the engine, and no single number is allowed to be the verdict (Quality Lab architecture, the metric set and its rejections, the AOV/supersampling split) | Accepted |
| [251](ADR-251-the-supersampled-exr-is-a-corner-of-the-frame.md) | The supersampled EXR is a corner of the frame, and it looks like a render (the HDR readback takes its extent from the output size) | Accepted |
| [252](ADR-252-the-banding-instruments-are-blind-in-opposite-places.md) | The banding instruments are blind in opposite places, and VMAF ranks a defect above its own reference (libvmaf measured and put on the ladder; the `neg` model made the default) | Accepted |
| [253](ADR-253-the-residual-knows-where-the-pixel-came-from.md) | The residual knows where the pixel came from, and that is the whole difference (the motion-compensated residual, the disocclusion mask, and ADR-243 narrowed to its axis) | Accepted |
| [254](ADR-254-a-benchmark-that-cannot-show-the-artifact.md) | A benchmark scene that cannot show the artifact is not a benchmark (the spline camera that looked away, and the FXAA that was never on) | Accepted |
| [255](ADR-255-the-shadow-aov-and-the-tier-that-has-no-shadow-texture.md) | The shadow AOV, and the tier that has no shadow texture to export (the mask pass does not run at `offline`, which is the one tier the Quality Lab measures) | Accepted |
| [256](ADR-256-a-material-id-is-not-a-class.md) | A material id is not a class, and a list of integers would go stale in silence (the mapping has to ship with the frames) | Accepted |
| [257](ADR-257-the-eye-ranked-them-the-way-the-spatial-measure-did.md) | The eye ranked them the way the spatial measure did, and the control in the frame was not a control (Quality Lab Phase 6: the human gate, the temporal detector's coin flip, the orb's interior) | Accepted |
| [258](ADR-258-the-shadow-aov-is-a-second-opinion.md) | The shadow AOV is a second opinion, measured at 0.43% of the frame, and it ships saying so (ADR-255 implemented; the fidelity experiment, and the arm that inverted the plane) | Accepted |
| [259](ADR-259-the-identifiers-material-half-is-not-a-material.md) | The identifier's "material" half is not a material, and the manifest had to be keyed on the half that is (ADR-256 implemented; surface classes, materials.json, vegetationResidual) | Accepted |
| [260](ADR-260-the-character-runtime-has-three-positions-and-only-one-of-them-is-drawn.md) | The character runtime has three positions, and only one of them is drawn (the stride bob that sank a grounded body, and the slope lean that has no fix inside the Euler triple) | Accepted |
| [261](ADR-261-the-lab-suite-is-mostly-already-here.md) | Most of the lab suite was already here under other names, so Phase 1 built the four things that were not (the Engineering Lab Suite: registry, cases, overlay profiles, the visibility vocabulary — and the two debug switches wired to nothing) | Accepted |
| [262](ADR-262-the-beam-is-a-column-not-an-axis-and-the-animal-is-not-its-origin.md) | The beam is a column, not an axis; the animal is not its origin; and the project was overriding the scene (the Tractor Beam Lab, `Anchor::Drawn`, `StepDesc::place`, and why three reported fixes never reached a render) | Accepted |
| [263](ADR-263-one-sphere-was-answering-two-questions.md) | One sphere was answering two questions, and a rung change drew nothing (the LOD / Geometry Lab: the ladder's own radius, the provable level range, the impostor predicate, and the rung overlay wired) | Accepted |
| [264](ADR-264-a-scene-file-is-not-the-state-that-runs.md) | A scene file is not the state that runs, and the entity layer was anchored before the project spoke (a project's 5,489 saved parameters, the beam that ran at 3.07 m, and the 28.661 m that was the diagonal) | Accepted |
| [265](ADR-265-the-shadow-pass-is-a-second-cull-and-only-half-the-engine-knows.md) | The shadow pass is a second cull, and only half the engine knows it (the Shadow Lab: the caster rule made a function, the impostor that faced the camera while the sun was looking, and the ecology's missing second cull) | Accepted |
| [266](ADR-266-nine-of-the-twelve-layers-were-already-here.md) | Nine of the twelve layers were already here, and the three that were not are not the three anyone expected (character intelligence Phase 0: navigation, the action queue and the staging parallel, already built) | Accepted |
| [267](ADR-267-the-scrub-already-matches-the-play.md) | The scrub already matches the play, and what breaks it is the frame rate and where the camera is (play vs seek 0.000022 m; behaviour LOD reads the camera and moves a body 50 m) | Accepted |
| [268](ADR-268-a-navmesh-has-no-triangle-soup-to-be-built-from.md) | A navmesh has no triangle soup here to be built from, and one walkable surface per point is a fact about this world (Recast rejected, and what would change the answer) | Accepted |
| [269](ADR-269-nobody-decides.md) | Nobody decides, and a behaviour tree would be a second interpreter over the queue we already have (utility scoring over `ActionQueue`, not a BT) | Accepted |
| [270](ADR-270-every-character-can-see-everything.md) | Every character can see everything, and the only sight this engine owns costs 1.7 ms a look (perception as the missing layer; the scan measures 0.098 us) | Accepted |
| [271](ADR-271-a-ui-edit-lands-in-the-project-and-must-be-readable-there.md) | A UI edit lands in the project, and the number it leaves there has to be readable (`particles/<name>/extent` becomes absolute metres; the beam guard compares runtime against the parameter stack) | Accepted |
| [272](ADR-272-the-reach-was-computed-for-a-light-the-shader-never-sees.md) | The reach was computed for a light the shader never sees, and the froxel look-up had no reference (the Lighting Lab: an area light cut off while still lighting, `emitterArea` promoted, `ClusterGrid::clusterOf`, the per-light cap report, and the two overlays that draw a light) | Accepted |
| [273](ADR-273-ninety-seconds-is-not-an-amount-of-work.md) | Ninety seconds is not an amount of work (`SeekBudget` prices a scrub in body-steps; the four seek defects; the replay's last step lands on the target and the residual goes to zero; a flatten's texture version becomes conditional) | Accepted |
| [274](ADR-274-a-joint-transform-is-entity-local-and-nothing-had-to-choose.md) | A joint transform is entity-local, and until something implemented it nothing had to choose (`ISkeletonQuery` implemented, `setSkeleton` called, `socketTransform` reports its fallback; the node scale it never carried) | Accepted |
| [275](ADR-275-a-lab-case-that-cannot-be-run-says-what-it-is-waiting-for.md) | A lab case that cannot be run says what it is waiting for (the Character Intelligence Lab: `blockedBy`, the floating hero that read as "obstacles do not contain a character", and the explorer's 1.36 m scrub) | Accepted |
| [276](ADR-276-a-star-is-project-data-and-never-reached-a-render.md) | A star is project data, and until now it never reached a render (session 1 hero, reloaded 0; the third instance of ADR-207's family, and the directed cut it left aimed at nothing) | Accepted |
| [277](ADR-277-the-only-way-to-look-at-a-post-stage-was-to-write-a-gpu-test.md) | The only way to look at a post stage was to write a GPU test, and two readbacks were quietly wrong (the HDR / Exposure / Bloom Lab: `--post-stages`, the metering copy a reset did not discard, and ADR-251's mistake at the call site it did not reach) | Accepted |
| [278](ADR-278-a-scene-file-could-not-author-a-light.md) | A scene file could not author a light, and the key it wrote instead was ignored in silence (top-level `"lights"`, a `"node"` that rides instead of a `NodeKind::Light`, the precedence against the four existing sources, and `core/json_keys.hpp` -- which found fifteen scenes writing `volumeNoiseAmount` at a parser that reads `volumeNoise`) | Accepted |
| [279](ADR-279-the-blooms-reach-is-a-pixel-count.md) | The bloom's reach is a pixel count, so supersampling halves it -- measured, and not changed (r99 flat at 120 px across a fourfold range; 0.506 at renderScale 2 with a control that does not move) | Accepted |
| [285](ADR-285-an-impostor-is-the-size-of-the-thing-it-stands-in-for.md) | An impostor is the size of the thing it stands in for (`makeLodMesh` sizes the billboard through `sourceTransform.scale`, the one step of the chain a camera-facing quad cannot travel; byte-identical on Glowmere, which reaches no impostor rung) | Accepted |
| [286](ADR-286-a-level-that-lost-part-of-the-object-says-so.md) | A LOD level that lost part of the object says so (ADR-085's guard applied to bounds; the reported error floored by a measured lower bound; `CommonTree_1`'s bottom rung goes 183 tris and 59% of its height to 130 tris and 86%) | Accepted |
| [287](ADR-287-the-ecology-gets-its-own-caster-list.md) | The ecology gets its own caster list, and it is not the camera's (one classify, two compactions, two slot ranges; Glowmere's 485/485 becomes 485 visible and 288 casting -- 208 gained, 405 shed) | Accepted |
| [290](ADR-290-a-percept-is-what-a-character-noticed-and-the-budget-is-per-character.md) | A percept is what a character noticed, the cadence is for the replay, and the occlusion budget is per character (`IPerception` built: two grid scans, a pure-function sense tick with a per-body phase, salience without visibility; ADR-270's 1.7 ms sightline confirmed at 1800.6 us and shown to be a property of the world function; the cadence is worth 15x on a scrub and `SeekBudget` cannot see it) | Accepted |
| [295](ADR-295-the-world-was-sampled-twice-and-the-grid-must-prove-it-may-answer.md) | The world was sampled twice, and a grid that answers for terrain has to prove it may (`TerrainQuery::at` took two `WorldMap::sample`s; `steer` 59.754 -> 34.853 us; `NavGrid::vouches` tests its own rule against the world at build and Glowmere refuses) | Accepted |
| [296](ADR-296-the-engine-knew-which-half-of-the-world-it-was-on.md) | The engine knew which half of the world it was on, and which characters could not leave (`NavGridStats::stranded` -- 9,646 of 19,584 cells on Glowmere; `NavDebug::confinedFor` for the body that never asks for a path) | Accepted |
| [297](ADR-297-a-door-that-closes-is-a-rectangle-of-the-graph.md) | A door that closes is a rectangle of the graph, and the labels are the whole of it (`NavGrid::rebuildRect`: 2.05 ms over 24 m against a 287 ms build; regions relabelled whole because a wall divides a region that reaches the other side of the world) | Accepted |
| [300](ADR-300-a-head-is-a-group-of-names-and-the-rig-has-no-word-for-it.md) | A head is a group of names, and the rig has no word for it (the animation layer stack: an aim layer and an additive one over the gait, a joint mask resolved against the rig it runs on, and `head.x` with zero children on a 90-joint alien; `lookTarget` and `reaction` reach a pose for the first time) | Accepted |
| [310](ADR-310-a-sentence-the-editor-draws-is-one-somebody-can-read.md) | A sentence the editor draws is one somebody can read (one `PushTextWrapPos` in 19,000 lines of `src/ui` and 150 unwrapped tooltips; the Control panel's load warnings stopped at `'atmos/`, every modulation route's label was outside the window, the Assets name column was fifteen points wide; the wrap guard is declared where a panel is begun, and a negative wrap position means *do not wrap*) | Accepted |
| [320](ADR-320-the-preview-is-the-file-or-it-is-nothing.md) | The preview is the file's own pixels, or it is nothing (the Render panel shows the frames the encoder is writing, tapped between the hash and the queue; 0.19 ms a frame, and the sequence hash could not have caught the hazard the file comparison did) | Accepted |
| [330](ADR-330-a-removal-is-a-negative-fact-and-nothing-was-left-to-carry-it.md) | A removal is a negative fact, and nothing was left to carry it (deleting an object never reached a render: 80 -> 79 -> 80 on the owner's own film; the project records the node-set **difference** against the scene it saves by reference, spliced into the document before the parse so there is one way to make a node) | Accepted |
| [331](ADR-331-a-cap-on-an-authored-value-is-a-refusal.md) | A cap on an authored value is a refusal, and this one refused in silence (`material/emissive`'s ceiling of 50 clamped an authored 256; the hard range now holds what the file says and the soft range still draws the slider, and every range in the table reports the authored values it overrules -- zero of them across the repository) | Accepted |
| [332](ADR-332-fifteen-scenes-asked-for-noise-and-five-projects-wrote-back-the-zero.md) | Fifteen scenes asked for volumetric noise, and five projects wrote back the zero they got (ADR-278's open trigger closed: the key corrected, the ADR-264 residue that would have made it inert removed, and the picture measured -- SSIM >= 0.9990, CIEDE2000 p95 <= 0.76, nothing worse) | Accepted |
| [333](ADR-333-a-character-kind-is-a-list-of-considerers-and-the-goal-model-had-to-move-without-moving.md) | A character kind is a list of considerers, and the goal model had to move without moving a millimetre (`IConsiderer` built: a selector whose dwell is counted in decision ticks, four stock considerers, the first consumer of a percept, and a guard with no `Guard` class; `Explore`'s goal model extracted against a 3,600-sample byte-identical position trace, and its weighted roll deliberately left where it is) | Accepted |
| [334](ADR-334-a-scale-is-a-ratio-and-glowmere-had-lost-both-ends-of-it.md) | A scale is a ratio, and Glowmere had lost both ends of it (the cast back to 1.0 with every metre-per-second beside it; the tree layers to 8/8/6.5 m so the signature organism stands above the tree line; the hero fungi untouched, and `kSignatureMax` deleted rather than widened because three renders said the elder was never what was wrong) | Accepted |
| [335](ADR-335-the-cast-goes-back-up-to-the-trees-and-half-the-ladder-stops-being-a-canopy.md) | The cast goes back up to the trees, and half the ladder stops being a canopy (the owner's sentence had two readings 1.85x apart and they chose the tree line: 21 bodies x1.94 in four scenes with every metre-per-second beside them; forty-two project speeds that had been putting ADR-334's back since it merged; arm 2 replaced by a band -- five canopies and five grounded -- whose halves fail on 3.6x and on 1.0x) | Accepted |
| [336](ADR-336-a-considerer-that-prices-the-way-and-not-the-place.md) | A considerer that prices the way and not the place, and the river the lab already had (the fifth stock considerer: two `requestPath` calls at two `NavPathCost::wadePenalty`, both scored with the character's own price on a wet metre; the detour wins at 12.0 and loses at 0.4 on the identical pair of routes, and the option's actions are a `Move` per waypoint because an option that named only the far bank forded anyway) | Accepted |
| [337](ADR-337-root-motion-is-a-transfer-of-authority-and-the-root-is-not-the-ancestor.md) | Root motion is a transfer of authority, and on this content the root is not the ancestor (per-clip opt-in writing `MotionAuthority::Simulation`; `Landing`'s -0.567 m reaches `state().position()` and the node parameter as -2.0457 m while the drawn toe falls the same 2.8120 m either way; compensation at `root.x` would leave `spine_01.x` falling 0.567 m, so it lands on the skeleton root; 165 clips compared, 159 bit-identical, 6 changed and all of them `Landing`) | Accepted |
| [338](ADR-338-the-look-was-in-the-blend-file-and-the-glb-came-out-brown.md) | The look was in the .blend file and the GLB came out brown (the Tree of Life hero export has no textures, no UVs and one emissive material out of seven, and a `material` block on a `gltf` node is parsed and dropped -- so the Glowmere palette is restored by a tool that rewrites the GLB's JSON chunk and proves the binary chunk's SHA-256 did not move; the island's Unity mask map repacked to glTF ORM with `_Smoothness` applied; the cottage shipped as its own export and rendered against the bare landmass, where the house is 57 units tall against the tree's 78 and hides the trunk) | Accepted |
| [339](ADR-339-the-glowmere-look-was-four-masks-and-the-export-kept-the-one-that-mattered.md) | The Glowmere look was four masks, and the export kept the one that mattered (`Tree_Glowmere.blend` and `Tree_of_Life_Hero.blend` are byte-identical in content, so there is no separate Glowmere source; the leaf emission is `ObjectInfo.Random > 0.94` per *instance* and the hero GLB's leaves are contiguous 18-vertex blocks, so `instance = vertex // 18` recovers 51,633 and 122,767 leaves and 5.9%/6.1% come out lit; five layer GLBs turn one scalar `emissiveBoost` into §8's five channels with no engine change; the cream-white in the reference is the blend's warm area key, not a material; large procedural points are opaque squares, so the haze is a background shader whose first two versions rendered a plausible flat gradient from a hash with `fract` on the wrong side of the dot product) | Accepted |
| [340](ADR-340-the-showcase-is-a-rebuild-and-the-river-was-a-wall.md) | The showcase is a rebuild, the river was a wall, and the hills were banded off on purpose (valley 3 generated by a script that reads neither of its outputs, 4 project parameters against valley-2-multicam's 5,455; the Glowmere run is 3.6 m deep against a 0.85 m wade and leaves the map at both ends, so a ford and a backwater are authored and the nav grid goes from 19 regions with 49% stranded to 3 with 0%; the hills are refused on height-above-water 95.1% and on slope 0.4%, not on biome, so eight tree species replace three and 2,593 trees stand where 545 did in the same world, 1,344 of them on the hills where 150 were with the 11.20 m tree line untouched; `decide` gains a stall breaker and `ground` gains a body, both off by default, after four of five characters stopped moving by t = 90 and two closed to 0.238 m) | **Retired** (ADR-344; the river, the riparian gate, the `investigate` lock-on and the two engine fixes survive it) |
| [342](ADR-342-the-engine-advertised-exr-environments-and-could-not-read-one.md) | The engine advertised EXR environments and could not read one (`asset_catalog` has called `.exr` an "environment" all along while `loadImage` routed everything through stb_image, so `--env map.exr` failed *initialisation* with "unknown image type"; routing it to the `readExr` that already existed then exposed tinyexr's refusal of BlenderKit's DWAA, so the runtime copies are derived 2048x1024 half+ZIP and the tool proves they are still scene-linear by exact channel means either side of the encode; neither HDRI is ever drawn as the visible sky, which settles the double-moon, double-sun, HDRI-horizon and seam hazards with one decision and is measured -- the island underside moves 8.8x and 3.1x while the sky patch does not move at all) | Accepted |
| [343](ADR-343-one-dayphase-and-the-sky-you-cannot-have.md) | One dayPhase, and the sky you cannot have (`fract(seconds / cycleSeconds + offset)` with no accumulator, so scrub equals play and 5,000 cycles later is the same phase to 1e-4; the ocean is a flooded terrain because a finite plane cannot reach the horizon -- 218,000 units would be needed -- and the horizon of a flat sea is at the camera's eye height, so the framing is solved rather than nudged; ADR-049's two skyIntensities bite, and driving the IBL one left midnight rendering at 102/110/125 sRGB; and the engine ties the IBL source to the skybox, so HDRI lighting with a procedural visible sky is not configurable and the sky-colour curves are inert in this scene) | Accepted |
