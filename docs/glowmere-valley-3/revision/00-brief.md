# Glowmere Valley 3 — Quality-First Revision Pass

(The owner's revision specification, saved verbatim, 2026-09-26. The §1 water screenshot arrived in
a follow-up message. It is kept outside the repository with the other renders, at
`~/Desktop/av-gen-review/18-glowmere-valley-3/revision/water-reference-from-owner.png`. It shows
narrow, stepped zig-zag bands in which the ripples are compressed into dense parallel stripes: current
seams, which the engine's water already produces by accident late in a film.)

## Mission

Perform a second, substantially more rigorous production-quality pass on `glowmere-valley-3`.

**Do not begin modifying the Glowmere Valley 3 scene yet.**

The first pass produced a visually decent result, but it exposed a major problem: the scene currently looks like a nicely composed 3D environment with some animated characters rather than a genuinely **audio-reactive audiovisual world**.

The goal of this pass is not merely to make individual shots prettier.

The goal is to make Glowmere Valley 3 feel **alive, musically responsive, cinematic, intentional, and continuously evolving**, while preserving the strong visual identity established by `glowmere-valley-2-multicam`.

There are three phases to this task:

1. **Quality/evaluator infrastructure**
2. **Architecture and capability assessment/development**
3. **Glowmere Valley 3 revision and iterative evaluation**

Do not skip ahead.

---

# PHASE 0 — HARD GATE

Before making any Glowmere Valley 3 scene edits:

1. Confirm the new quality evaluator/analyzer tool is installed and actually usable by the agent.
2. Confirm the agent can:

   * render frames or short sequences
   * submit them to the evaluator
   * receive structured analysis
   * inspect specific frames/shots
   * compare renders between iterations
   * inspect temporal behavior rather than only isolated frames
   * inspect audio-reactivity behavior
   * obtain machine-readable results without repeatedly polling the evaluator
3. Confirm the evaluator can operate at multiple analysis speeds/depths:

   * fast iteration analysis
   * normal quality analysis
   * slow/deep analysis for final review
4. Confirm the evaluator can retain/compare historical iteration results.

**If the evaluator is not yet ready, STOP.**

Do not begin the Glowmere Valley 3 revision work until the evaluator is operational.

The evaluator is now part of the creative iteration loop, not an optional diagnostic utility.

---

# PHASE 1 — SYSTEM CAPABILITY AUDIT

Before changing the scene, inspect the current state of:

* Director Agent
* directive generation
* shot planning
* shot duration selection
* camera behavior
* camera smoothing
* character animation
* alien locomotion
* character behavioral logic
* entity effects system
* hero effects
* modulation system
* audio analysis
* signal routing
* parameter modulation
* world effects
* post processing
* offline rendering
* draw-distance/render-quality configuration

## Director Agent assessment

Evaluate whether the current Director Agent is actually capable of producing the type of music video being requested.

Specifically determine whether it can reason about:

* musical structure
* intensity
* transitions
* buildup/riser sections
* drops
* quieter passages
* repetition
* visual variation
* shot density
* shot duration
* visual hierarchy
* environmental activity
* audio-reactive opportunities
* recurring visual motifs
* hero moments
* secondary environmental activity
* escalation

Do not assume the Director Agent is adequate simply because it successfully generated a coherent sequence.

Ask:

> "Could this Director Agent reliably create another high-quality audio-reactive Glowmere video from scratch, or did this scene merely get lucky because the underlying environment already looks good?"

Identify architectural deficiencies.

If the Director Agent needs additional capabilities, build them **before continuing to the scene revision**.

---

# CHARACTER ANIMATION ASSESSMENT

Evaluate the current character animation system specifically against what is visible in Glowmere Valley 3.

The aliens currently frequently:

* stand around doing almost nothing
* perform movements that have little apparent purpose
* walk to a point and then simply turn around and walk back
* lack convincing behavioral variation
* appear like animated objects rather than creatures inhabiting the environment

The animation system is still under development, so do not attempt to solve every character-animation problem in this pass.

However, determine what improvements are already possible with the existing architecture.

At minimum, investigate whether the system can provide:

* idle variation
* short purposeful behaviors
* wandering
* looking/attention changes
* pauses
* direction changes
* curved turns
* avoidance
* interaction with nearby objects
* movement toward points of interest
* temporary reactions to world events
* simple group behavior
* animation variation
* non-repeating movement patterns

### Hard requirement for now

Do not allow aliens to remain stationary for long periods unless the shot deliberately calls for that behavior.

Do not allow the default behavioral pattern to become:

`walk → stop → turn 180° → walk back`

That reads as procedural test behavior rather than intentional character movement.

If the existing character architecture cannot avoid these patterns cleanly, improve the architecture first.

Do not build a huge new AI system unnecessarily.

The goal is to eliminate obviously pointless/repetitive movement and make the current system visually credible.

---

# SYSTEM DEVELOPMENT GATE

After auditing Director Agent and character animation:

### If the current systems are sufficient:

Proceed to Glowmere Valley 3.

### If they are not sufficient:

Implement the minimum architectural improvements required to achieve the desired result.

These changes may be made on the main branch if they are general AV Gen improvements.

Before making them:

* inspect current branch/state
* understand existing architecture
* avoid duplicating systems
* preserve existing functionality
* add regression tests
* document significant architectural changes

If required, **merge the necessary system changes into `main` first**, then continue the Glowmere Valley 3 work from the updated architecture.

Do not artificially constrain the work to scene JSON if the underlying system is the actual problem.

---

# EMISSIVE BOOST

Fix the recurring `emissiveBoost` issue.

Do not preserve backwards compatibility for old scene behavior solely to avoid changing the look of other scenes.

At this stage we are actively developing AV Gen and can accept intentional visual changes to existing scenes.

Fix the underlying issue cleanly rather than repeatedly asking for permission to modify it.

Regression-test the parameter/system afterward.

---

# PHASE 2 — RESEARCH / CAPABILITY GAPS

Before the main revision pass, determine whether additional research is warranted.

In particular investigate whether the current audio modulation architecture is being used effectively for a cinematic audiovisual scene.

Research should focus on practical implementation patterns for:

* audio-driven environmental lighting
* frequency-band modulation
* transient detection
* energy/envelope smoothing
* beat/subdivision modulation
* low-frequency environmental movement
* high-frequency sparkle/detail modulation
* musical-section-aware modulation
* correlated but non-identical modulation
* avoiding visually noisy "everything pulses to the beat" behavior
* creating hierarchical audio-reactive behavior

The purpose is not to research audio visualization academically.

The purpose is:

> Determine how to make a 3D cinematic world feel like it is responding to music without turning into a generic audio visualizer.

If research identifies a meaningful architectural deficiency in AV Gen, address it before the final scene pass.

---

# PHASE 3 — GLOWMERE VALLEY 3 REVISION

Once the evaluator and necessary architecture are ready, begin the actual scene revision.

Use the existing `glowmere-valley-3` project.

Do not destroy the good work from the first pass.

Iterate from the existing scene.

---

# 1. WATER — VISIBLE SURFACE TEARING

The water currently needs stronger visual interest.

The attached screenshot is the visual reference for the desired phenomenon.

Add visible "tearing" lines / directional surface distortions across the water.

These should:

* actually read clearly in selected shots
* remain integrated with the Glowmere visual style
* not become generic noisy normal-map movement
* vary subtly over time
* respond appropriately to camera distance
* be visible enough to survive cinematic rendering
* preferably have some relationship to wind/audio/environmental movement

Do not make the entire water surface uniformly busy.

Use the evaluator to determine whether the effect is actually visible in the intended shots.

---

# 2. CAMERA SMOOTHING

The cameras are still occasionally too wobbly, particularly shots following the aliens.

Fix this.

Following shots should feel intentional and cinematic.

Investigate:

* positional smoothing
* rotational smoothing
* target smoothing
* look-at damping
* velocity prediction
* acceleration/deceleration
* spline/path behavior
* camera noise amplitude
* camera noise frequency

Do not simply eliminate all camera movement.

The desired result is:

**controlled cinematic movement rather than procedural wobble.**

Use temporal evaluator analysis to identify excessive camera-frequency changes and frame-to-frame instability.

---

# 3. AUDIO REACTIVITY — HIGHEST PRIORITY

This is the most important revision.

The current scene has almost no meaningful audio reactivity.

That is unacceptable for AV Gen.

The environment currently looks nice but largely behaves like a static 3D scene playing alongside music.

We need the world itself to respond to the music.

The scene should have substantially more modulation.

---

## Hero effects

The hero objects currently have essentially no meaningful effects applied.

This is especially disappointing because Glowmere Valley 2 used hero pulses extensively and established a strong precedent for how the environment can respond to music.

Use hero effects throughout the scene.

Do not blindly copy Glowmere Valley 2.

Instead, identify appropriate hero objects and determine what each should respond to.

Examples:

* pulse
* emissive pulse
* color shift
* brightness modulation
* intensity modulation
* scale modulation where appropriate
* subtle rhythmic movement
* frequency-dependent response

Hero effects should not all use identical timing or amplitude.

---

# 4. MUSHROOMS — MISSED AUDIO-REACTIVE OPPORTUNITY

The small colored mushrooms throughout the valley are an especially strong opportunity.

They already have:

* varied colors
* emissive/light-like appearance
* strong visual readability
* dense environmental distribution

Use them.

Explore effects such as:

* chromatic shifting
* emissive pulses
* radiant glow
* brightness modulation
* frequency-band response
* staggered/grouped responses
* local waves of illumination
* subtle synchronized behavior
* section-based color evolution

The objective is not to make every mushroom flash.

The objective is to make the valley feel like **the living bioluminescent environment is listening to the music**.

This is exactly the sort of opportunity the Director should be finding automatically.

---

# 5. ENVIRONMENTAL AUDIO-REACTIVE LAYERS

Look beyond hero objects.

Identify multiple levels of audio-reactivity:

### Micro

Individual mushrooms, plants, small lights, particles, etc.

### Meso

Clusters of vegetation, environmental groups, water regions, floating objects, local effects.

### Macro

World lighting, fog, sky, large effects, atmospheric intensity, environmental color.

The modulation should be hierarchical.

Avoid making everything respond simultaneously with identical timing.

The world should feel correlated with the music rather than mechanically synchronized to it.

---

# 6. WIND

The valley currently lacks the subtle environmental motion that makes it feel alive.

Add subtle wind.

Wind should affect appropriate environmental elements:

* grass
* plants
* leaves
* vegetation
* lightweight environmental elements
* potentially atmospheric effects

The movement should be subtle and continuous.

It should not look like everything is being hit by a storm unless the music/directive explicitly calls for it.

Ideally establish:

* wind direction
* wind speed
* gust variation
* subtle temporal variation

Where appropriate, allow musical intensity to influence wind energy without making it an obvious audio visualizer.

---

# 7. DO NOT USE THE CURRENT POST-RISER STYLE SHIFT

The stylized visual shift after the riser is interesting as an experiment, but the current implementation is not necessarily the correct direction.

Glowmere Valley 2 established a coherent visual language across the entire scene.

Do not abruptly abandon that visual identity after the riser.

Instead, find another way to communicate the musical transition.

Possible approaches include changes in:

* lighting
* emissive intensity
* atmospheric density
* world color
* environmental activity
* camera language
* effects intensity
* movement
* contrast
* post-processing intensity
* fog
* sky illumination

The world should feel like it has entered a new musical state while still clearly remaining **Glowmere Valley**.

Do not simply apply a new visual style to the entire world.

---

# 8. SHOT DURATION / PACING

Many shots currently run too long.

The problem improves somewhat after the riser, but the later sequence can still become more intense.

Use the evaluator to identify:

* excessively static shots
* shots with little visual change
* repeated compositions
* shots whose primary subject stops doing anything
* shots that have already communicated their idea and continue too long

The Director should reason about shot duration from musical and visual information.

Do not enforce one global shot duration.

Use shorter shots where the music and visual density demand them.

Allow longer shots when they create contrast or establish scale.

---

# 9. STOP MAKING THE ALIENS THE ENTIRE VIDEO

The current sequence gives too much attention to the aliens.

They are an important element, but they should be part of the world rather than the entire subject.

The UFO abduction is a major event.

There is currently only one major abduction.

That is not enough.

Increase the number of UFO/abduction events throughout the piece.

However:

**Do not simply duplicate the same abduction shot repeatedly.**

Create variation in:

* location
* framing
* scale
* distance
* timing
* number of animals involved
* relationship to the music
* camera movement
* surrounding environmental activity

The riser can build toward a major centerpiece abduction.

But the world should establish earlier that UFO activity is occurring throughout the valley.

Think:

**UFO activity is part of the world.**

not:

**The video is about one UFO scene.**

---

# 10. ALIEN BEHAVIOR

Until the character animation system is more sophisticated, optimize for avoiding obviously meaningless behavior.

Do not allow long stretches where aliens simply stand in place.

Give them purposeful variation.

Possible behaviors:

* wandering
* investigating
* looking around
* moving between points of interest
* observing farm animals
* reacting to UFO activity
* moving toward/away from events
* small group interactions
* varied pauses
* varied movement speeds
* curved direction changes

Avoid:

* robotic 180-degree turns
* walk-to-point → instant reversal
* repeated identical paths
* long dead periods
* obviously deterministic loops

Use the evaluator to inspect temporal behavior rather than judging only single frames.

---

# 11. FARM ANIMALS — GROUNDING AND NAVIGATION

Some farm animals are still incorrectly positioned on slopes.

Examples include horses facing directly into hills.

For this scene, a simple robust solution is acceptable:

**Prefer placing animals in the flatter valley areas and constrain their navigation to terrain that is appropriate for them.**

Do not spend excessive effort making animals traverse extreme terrain if the scene does not require it.

Animals should:

* remain grounded
* orient correctly to the terrain
* avoid visibly intersecting terrain
* avoid walking into steep slopes
* move naturally within their allowed area

---

# 12. ANIMAL TURNING

Animals currently appear to rotate around an axis when changing direction.

That reads as a technical animation artifact.

Improve turning so that animals:

* use curved trajectories
* gradually reorient
* move through turns
* do not pivot unnaturally in place

The exact implementation can be architectural or scene-level depending on what the existing navigation system supports.

Prefer fixing the underlying movement behavior if it is reusable.

---

# 13. 4K / OFFLINE QUALITY

The final evaluation/render pass must use:

**4K output.**

Use the maximum appropriate offline-quality settings.

Verify that the final render is actually configured for maximum offline quality rather than simply assuming the defaults are sufficient.

Specifically audit:

* resolution
* draw distance
* shadow quality
* volumetric quality
* texture quality
* filtering
* post processing
* reflections
* lighting quality
* effect quality
* anti-aliasing where applicable
* temporal quality where applicable
* any offline-specific quality controls

The current result does not appear to be using effectively unlimited draw distance.

Investigate the actual configuration and fix it.

Do not merely increase a random distance parameter.

Determine how AV Gen's offline renderer is intended to achieve maximum scene visibility/quality.

---

# 14. CINEMATIC POST PROCESSING

The current output still has a clean, digital CG appearance.

Use post processing more effectively to produce a more cinematic image.

Do not simply add heavy blur, bloom, grain, or arbitrary LUT-like effects.

Investigate the existing post stack and improve:

* tonal cohesion
* highlight handling
* atmospheric depth
* bloom/emissive integration
* contrast
* color relationships
* exposure
* depth separation
* subtle filmic character

Maintain the Glowmere palette.

The goal is:

**cinematic Glowmere**

not:

**generic cinematic filter applied to a 3D render.**

Use the evaluator to compare before/after output.

---

# 15. DIRECTOR SHOULD USE THE QUALITY EVALUATOR

The Director Agent should actively use the evaluator during iteration.

Do not make the evaluator a final inspection tool only.

The desired loop is:

1. Generate/revise directives
2. Render representative shots
3. Evaluate
4. Identify concrete weaknesses
5. Revise directives/scene/effects
6. Render again
7. Compare against previous iteration
8. Repeat

The evaluator should specifically answer:

* Is the world visually active?
* Is audio reactivity actually visible?
* Are effects present?
* Are hero objects responding?
* Is environmental modulation present?
* Are shots too long?
* Are cameras unstable?
* Are characters idle?
* Are characters moving meaningfully?
* Are animals grounded?
* Are turns natural?
* Are UFO events varied?
* Is the world stylistically cohesive?
* Does the post-processing improve the image?
* Are important effects actually visible at final output resolution?

---

# 16. AUDIO-REACTIVITY COVERAGE REQUIREMENT

Do not consider the scene "audio reactive" merely because a few parameters technically have modulators attached.

The evaluator should distinguish:

### Configuration

A parameter is technically connected to audio.

### Behavioral response

The rendered object visibly changes over time in response to audio.

### Meaningful response

The change contributes positively to the visual composition and does not merely produce noise.

This distinction is critical.

We want meaningful audio-reactive behavior.

---

# 17. QUALITY EVALUATOR ITERATION REQUIREMENT

For every major revision category, capture before/after evidence where practical.

At minimum evaluate:

* water
* camera stability
* hero effects
* mushroom modulation
* environmental motion
* alien behavior
* animal placement
* UFO activity
* shot pacing
* visual style
* post processing
* final render quality

Do not blindly trust evaluator scores.

Use the evaluator's findings as evidence for iteration.

If its analysis disagrees with visual inspection, investigate why.

---

# 18. FINAL DIRECTOR SELF-CRITIQUE

Before declaring the project complete, have the Director perform a final structured critique.

Ask:

### Audio reactivity

Does the world visibly respond to the music?

### Environmental life

Does the environment feel alive even when no character is present?

### Character behavior

Do characters appear to inhabit the world rather than merely move through it?

### Visual variety

Does the video continually introduce visual information?

### Pacing

Are shots staying on screen because they need to, or because the Director failed to move on?

### Musical structure

Does the visual intensity track the structure of the song?

### Hero moments

Are major musical events rewarded with major visual events?

### Cohesion

Does the scene remain Glowmere Valley throughout?

### Cinematic quality

Does the final render still look like clean real-time CG, or does the image have enough depth, atmosphere, lighting integration, and post treatment to feel cinematic?

Be brutally honest.

If the result is not yet good enough, continue iterating.

---

# DELIVERABLES

Produce:

1. Updated `glowmere-valley-3`
2. Any required shared AV Gen architecture changes
3. Any Director Agent improvements
4. Any character-animation improvements
5. Updated evaluator integration/configuration
6. Tests for new reusable behavior
7. Research notes if additional research materially affected implementation
8. A concise revision report

The report should contain:

* what was wrong
* what was changed
* what architecture was changed
* why it was changed
* evaluator findings before/after
* remaining known weaknesses
* representative final renders
* final render configuration
* whether Director Agent capabilities are now sufficient
* whether character animation capabilities are now sufficient
* whether additional work should happen before the next music-video experiment

---

# IMPORTANT OPERATING PRINCIPLES

Do not optimize for completing the task quickly.

Optimize for producing a materially better music video.

Do not confuse:

* "the scene loads"
* "the scene renders"
* "the scene is technically audio-reactive"

with:

**"this is a compelling audiovisual world."**

The last one is the actual objective.

And do not begin the Glowmere Valley 3 revision until the quality evaluator gate has passed.
