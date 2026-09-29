# GV3: targeted art pass (the owner's brief, 2026-09-28)

**Queued; not started.** It starts only after the QA pass's GV cleanup (W3) and performance investigation (W1) are
complete and merged. One agent runs it, on Opus 5.5 at max effort (`~/.claude/agents/gv3-art-pass.md`).

**Water references:** the owner's two screenshots are in `~/Desktop/av-gen-review/20-gv3-art-pass/reference/`. They
are kept out of the repository.
- `water-1-hill-wall-stepped-edge.png`: the hill water ends in a stepped, vertical wall at the valley edge.
- `water-2-hill-slab-jagged-edges.png`: the hill water is a slab with staircase edges.

---

# GV3 — Targeted Art Pass

GV3 is now close to production-ready.

The current project is in a good place: the Director + Creative Critic workflow has produced a genuinely strong music-video direction, and the overall visual language is working. **This pass is a targeted refinement pass, not a redesign.** Preserve the existing composition, art direction, camera work, pacing, lighting, effects, and creative decisions that are already working unless one of the items below requires modification.

The goal is to address the following specific issues and then stop for review.

## Important workflow instructions

* Work directly from the **current GV3 project**. Do not rebuild the scene or start from an earlier GV3/GV2 state.
* Preserve successful existing work.
* Make the revisions below systematically.
* Avoid unnecessary experimentation or broad art-direction changes.
* **Minimize the number of renders required for this pass.**
* Use scene inspection, diagnostics, existing render data, screenshots, and other available analysis wherever possible before rendering.
* Batch related changes together.
* Do **not** run the Creative Critic after every individual change.
* Only once **all revisions in this prompt are complete**, perform **one final Creative Critic evaluation** of the revised GV3.
* The final critic check should evaluate the whole result rather than generating a sequence of intermediate critic renders.
* If the critic identifies a small obvious correction that is necessary to satisfy one of the requirements below, make that correction and perform the minimum additional verification necessary. Do not enter an open-ended iteration loop.
* The attached screenshots are visual references for the water problems described below. Use them as evidence of the specific problems to correct, not as instructions to copy their appearance.

---

# 1. Fix the river / terrain / water integration

One of the best creative decisions in the current GV3 was adding the hill on one side of the valley and extending the river so that it now flows down the hill and into the valley.

**Keep this creative decision. It is working.**

The water simply needs another technical/art pass so that it properly integrates with the new terrain.

### Problems to address

The attached screenshots demonstrate two issues:

### A. Jagged water/terrain boundaries

There are areas where the water meets the land with visibly jagged, square/stepped edges.

The water should meet the surrounding terrain naturally.

Inspect the water/terrain intersection across the entire river, not just the locations visible in the screenshots.

Look for:

* square or stepped water boundaries
* visible gaps between terrain and water
* terrain protruding unnaturally into the water
* water visibly cutting through terrain
* unnatural shoreline transitions
* obvious discontinuities caused by the new hill geometry

The result should read as **one continuous naturally formed river**, rather than a water surface placed over several disconnected terrain regions.

### B. The new hill water does not properly connect to the existing river

The additional river/water section running down the hill currently appears visually or geometrically disconnected from the rest of the river.

Fix the underlying scene representation so that the **entire body of water is actually connected**.

Do not merely hide the discontinuity from the camera.

Trace the water from its highest point on the hill all the way down into the valley and verify:

**hill → descending river → valley → existing river**

is one continuous body of water.

The transition should look physically and visually coherent.

### Water pass constraints

Do not redesign the river.

The current concept is good:

* river descending from the hill
* flowing into the valley
* existing valley river retained

The task is to make the new terrain/water relationship production-quality.

---

# 2. Refine the 4-beat hero pulse transitions

The current hero pulse effect on the mushrooms and aliens is **very close to finished**.

The actual pulse animation looks good.

Keep the current basic behavior:

* hero objects pulse on the beat
* the effect is re-triggered every 4 beats
* pulses expand outward from the hero
* the current visual character of the effect is retained

### The problem

When the next 4-beat pulse is triggered, the previous pulse/wave appears to be abruptly removed/reset.

That creates a visible discontinuity.

I do **not** want the existing pulse to simply disappear when the next pulse begins.

### Desired behavior

Allow previous pulse waves to transition naturally into the next pulse.

For example:

1. Pulse begins.
2. Wave expands outward.
3. Wave continues expanding.
4. Wave gradually fades as it travels.
5. Four beats later, another pulse begins.
6. The previous wave can still exist while the new wave begins.
7. Multiple waves may therefore coexist briefly, producing layered outward-moving pulses.
8. Older waves eventually fade completely.

This would allow the effect to read more like a continuous rhythmic energy field rather than an animation that resets every four beats.

### Important

**Do not redesign the pulse effect.**

The current animation is good.

This is specifically a **transition/lifecycle problem**:

> Preserve the current pulse appearance and timing, but make successive pulse instances independent and allow previous instances to finish/fade naturally.

Avoid abrupt removal of existing waves.

A small amount of overlapping pulse activity is desirable if it makes the effect feel more organic.

---

# 3. Remove small amounts of character/animal foot sliding

There is a subtle movement issue with some aliens and animals.

It is most noticeable when a character is idle and begins moving.

There can be a small amount of visible sliding before the first actual footstep/contact occurs.

Inspect the transition:

**idle → movement**

for the aliens and animals.

The desired behavior is that movement begins in a way that is visually grounded to the first footstep.

Look for:

* root translation beginning before the first meaningful foot contact
* feet sliding across the ground
* animation/root-motion timing mismatch
* small positional interpolation that causes the character to drift before stepping
* similar sliding during transitions back from idle

This is a **subtle polish issue**, not a request to replace the locomotion system.

Make the smallest appropriate correction that improves grounding while preserving the existing animation quality.

Do not spend excessive time trying to eliminate microscopic motion that is not visible in the final cinematic renders.

Prioritize what is actually visible on camera.

---

# 4. Add a spacetime warp effect around the UFO

Add a new creative effect around the UFO.

The concept is a **spacetime distortion/warp field** associated with the UFO.

There are two desired states:

### UFO moving

When the UFO is moving, the distortion should potentially:

* trail behind it
* distort the space immediately around/behind the craft
* create a sense that the UFO is bending or dragging the surrounding environment
* feel related to its movement rather than looking like a generic particle trail

### UFO stationary

When the UFO is stationary, experiment with having a subtler warp/distortion field around it.

This could be much less pronounced than the moving version.

The exact implementation is intentionally left open.

**Use creative judgment.**

The goal is to make the UFO feel like it is manipulating spacetime rather than simply flying through the scene.

### Critical constraint: abduction sequence

There is one important concern:

**Do not degrade the existing abduction lift animation.**

The abduction/lift sequence currently works and should remain visually strong.

The spacetime warp must not:

* obscure the abducted character
* interfere with the lift beam
* visually confuse the beam and warp effects
* reduce readability of the abduction
* introduce distracting geometry/artifacts
* overwhelm the focal point of the shot

If necessary, the warp effect should be reduced, suppressed, or modified during the abduction sequence.

Think of the warp as an enhancement to the UFO, **not a competing hero effect** during the abduction.

This is the one area where some creative experimentation is encouraged. Implement a promising version and let the final review determine whether it should remain.

---

# 5. Add two additional hero mushrooms

Use the existing **procedural mushroom generator** to create **two additional hero mushrooms**.

Requirements:

* Both must be generated using the existing procedural mushroom system.
* They must be visually unique from the existing hero mushrooms.
* Avoid simply duplicating an existing mushroom with trivial parameter changes.
* They should fit the established Glowmere visual language.
* Both must explicitly be designated/configured as **hero mushrooms** so they participate in the existing hero systems.
* They should receive the appropriate existing hero behavior/effects automatically.
* Integrate them naturally into the current environment rather than placing them arbitrarily.

### Shot integration

After adding the mushrooms:

* inspect the existing shots
* identify shots where the new hero mushrooms can contribute visually
* update appropriate shots/compositions to include them
* do not force them into every shot
* maintain composition and visual hierarchy
* avoid overcrowding existing hero moments

The purpose is to expand the hero ecosystem of the scene while retaining the current cinematic composition.

---

# Final validation

Before the final critic pass, verify the following:

### Water

* [ ] River remains connected from hill to valley.
* [ ] No obvious square/jagged water/terrain boundaries remain.
* [ ] No visible gaps or discontinuities exist along the new water path.
* [ ] The hill-to-valley transition reads as one continuous river.

### Hero pulses

* [ ] Existing pulse appearance remains intact.
* [ ] 4-beat retriggering remains intact.
* [ ] Previous pulse waves no longer abruptly disappear.
* [ ] Older waves can expand/fade naturally while newer pulses begin.
* [ ] Multiple overlapping waves are possible where appropriate.

### Character movement

* [ ] Idle → movement transitions have reduced visible pre-footstep sliding.
* [ ] Aliens and animals remain grounded.
* [ ] No unnecessary locomotion redesign was introduced.

### UFO

* [ ] UFO has a creative spacetime warp/distortion effect.
* [ ] Moving UFO can produce a trailing/associated warp.
* [ ] Stationary UFO can have a subtler warp presence.
* [ ] Abduction lift remains clearly readable.
* [ ] Warp does not degrade the abduction sequence.

### Hero mushrooms

* [ ] Two new mushrooms were generated procedurally.
* [ ] They are visually distinct.
* [ ] Both are designated heroes.
* [ ] They are integrated into appropriate shots.
* [ ] They participate correctly in the existing hero systems.

---

# Final Creative Critic

**Only after all of the above revisions are complete**, run one Creative Critic evaluation on the revised GV3.

The critic should evaluate:

1. Overall visual quality
2. Water/terrain integration
3. Hero pulse continuity
4. Character grounding
5. UFO warp integration
6. Abduction-sequence readability
7. New hero mushroom integration
8. Overall cinematic cohesion

The critic should be used as a **final quality gate**, not as an iterative driver for this pass.

Minimize rendering. Prefer inspection and targeted verification wherever possible, and render only what is necessary to confidently validate the completed pass.

If the final result is strong and the requested changes are satisfied, **stop**. Do not invent additional art changes simply because the critic can identify things that could theoretically be improved.

This is a refinement pass on an already successful GV3 — preserve the good work and make these specific areas production-ready.

---

## Addendum (owner, 2026-09-28): the aurora answers the bass

The QA pass's state audit found that the aurora's audio sensitivity is 0 in what renders (`fx/aurora/audioSensitivity`,
`tools/gv3/look.py`). The owner's decision: **the aurora should react to the audio, with a similar low-energy bass
pulse.**

Context for the implementer. `look.py` records why the response was zeroed. The aurora's built-in audio response
and its spectrum shape read the analyser every frame, unsmoothed, and made the sky jump up to 44% between two frames
on a kick. Do not simply set those back to their old values. Give the aurora a slow, low-energy pulse on the bass
instead, in the style of the project's other smoothed bass routes (for example `audio.bass ->
nodes/valley/water/ripple` with an attack and a decay, in `look.py`). It should be a gentle swell and fall of the
curtain's brightness and/or lift that you can read over the bass, never a frame-to-frame flicker. Also keep the
existing slow `lead.aurora` response. Measure the frame-to-frame change of the sky over the arrival wide and the
drop, as `look.py` did, and state the numbers. Resolve the aurora's duplicate block and parameter values (the audit's
table) so that the Effects panel and what renders agree.


---

## Addendum (owner, 2026-09-28): Astronaut Musicians + UFO Abduction

*Coordinator's note, not the owner's words:*
- **The validated source of truth** is branch `proto/astronaut-musicians` (`7f1dc21d`, worktree `../av-gen-astro`).
  It has the build script `tools/make_astronaut_musicians.py` (with the rigid-helmet fix and `NECK_STIFFNESS = 0.5`),
  the standalone example `examples/musicians/`, `assets/musicians/ATTRIBUTION.md`, and
  `docs/prototypes/astronaut-musicians/PROGRESS.md`. It must be merged into main before this pass starts, or into
  this pass's branch.
- **The assets** (`~/Desktop/musician_assets/`, and everything generated from them) have unknown or unconfirmed
  licences, so they are never committed. Regenerate them with the Blender command in PROGRESS.md.
- **The prototype's report** is `~/Desktop/av-gen-review/21-astronaut-musicians/REPORT.md`.
- **Owner decisions, 2026-09-28** (all accepted as they are):
  - the flat white suit and dark visor, with no texture;
  - the keyboard at about twice real width;
  - the drummer's corrections (the right leg turned out 40°, the left wrist raised 8 cm);
  - `NECK_STIFFNESS = 0.5`.
- **Still open:** the missing kick and hi-hat pedals are a prototype gap. Add them only if they are cheap and they
  read in shot. The licences, as the owner confirmed them on 2026-09-28:
  - the astronaut, the keyboard and the stand: CC0;
  - the drum kit: a personal royalty-free licence, not for redistribution;
  - the Mixamo clips: not redistributable as files.
  So all the generated GLBs stay uncommitted, and renders are fine.
- **GV3's cast scale** (1.94x) must be applied to the musicians and their props together, as one transform per
  group.

The owner's words follow, verbatim.

# GV3 Art Pass Addendum — Astronaut Musicians + UFO Abduction

This is an **addendum to the existing Glowmere Valley 3 art-pass specification**.

The GV3 art pass has not yet begun.

The standalone astronaut-musician prototype has now been completed successfully. The two astronauts have been rigged and the supplied Mixamo animations are working convincingly with the instruments.

We are now ready to bring that validated work into GV3.

---

# 1. Import the Astronaut Musicians

Import the validated astronaut musician assets from the standalone prototype into GV3.

There should be two performers:

### Musician 1 — Keyboard

* Low-poly astronaut
* Keyboard
* Folding keyboard stand
* Mixamo piano-playing animation

### Musician 2 — Drummer

* Low-poly astronaut
* Low-poly drum kit
* Mixamo drum-playing animation

**Reuse the exact rigging/animation solution that was validated in the prototype.**

Do not redo the character rigging unless there is a genuine GV3 integration issue.

Do not create a new character system as part of this task.

---

# 2. Placement — Central River Area

Place the two musicians somewhere **roughly in the middle of Glowmere Valley near the river**.

The location should feel intentional and visually interesting, rather than simply being the nearest empty patch of terrain.

Desired composition:

```text
                 River
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

          Keyboard Astronaut
                  ↘
                    ↘
                      ↘

                       ↙
                     ↙
                   ↙
          Drummer Astronaut
                 
             [some space]

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
```

The exact arrangement is flexible.

The important characteristics are:

* Both musicians should be reasonably close to the river.
* They should have **clear separation from one another**.
* They should be **facing each other**, as though they are performing together.
* There should be enough space between them that each character reads clearly.
* Their instruments should remain visually distinct.
* The composition should work from the existing GV3 cameras where possible.

They should feel like a deliberate musical performance happening inside Glowmere rather than two unrelated NPCs dropped into the environment.

---

# 3. Vegetation / Environment Integration

Be especially careful with the existing Glowmere vegetation.

Do not simply place the musicians and accept whatever intersections occur.

Inspect the final placement for:

* mushrooms growing through the drum kit
* plants growing through the keyboard
* vegetation intersecting astronaut bodies
* branches/leaves intersecting instruments
* terrain clipping through chairs/instruments
* props floating above or sinking into the ground
* awkward vegetation directly underneath the performers

Prefer **small local adjustments to placement and/or vegetation** rather than substantially clearing the environment.

The musicians should feel embedded in the existing Glowmere environment.

Do not sterilize the surrounding area just to make placement easy.

---

# 4. Make Both Musicians Heroes

Both astronauts should be treated as **hero entities** in GV3.

Use the existing GV3 hero/entity infrastructure.

Do not invent a parallel system.

Make sure both musicians can participate in the existing hero-effect architecture.

---

# 5. New Hero Effect — Rainbow Bioluminescent Pulse

Create a new hero treatment for the astronauts.

The initial concept:

> **Outward-radiating rainbow bioluminescent energy that pulses to the music.**

The effect should make the astronauts feel like unusual Glowmere entities rather than ordinary astronauts imported into the scene.

### Desired visual behavior

The effect should:

* originate around each astronaut
* radiate outward
* have a bioluminescent quality
* contain a shifting/rainbow spectral color range
* pulse rhythmically with the music
* respond clearly enough to be visible in the render
* remain aesthetically compatible with the existing Glowmere palette
* avoid looking like a generic RGB game effect

Think:

**bioluminescent aura / energy field**

rather than:

**rainbow outline / neon cartoon glow.**

The effect should feel organic and atmospheric.

---

# 6. Audio Reactivity

Tie the hero effect into the existing GV3 audio-reactive system.

The pulse should respond to the actual music rather than simply running on an arbitrary timer.

Prefer existing audio analysis/signal-bus infrastructure.

The effect should have some relationship to the beat:

* subtle baseline activity
* stronger pulse on beats/transients
* potentially stronger intensity during larger musical moments

Do not make it so aggressive that the astronauts become giant flashing objects that dominate the entire scene.

The goal is **musical life**, not visual noise.

---

# 7. Make the Two Astronauts Feel Like a Pair

Although they are separate hero entities, they should visually read as part of the same performance.

Consider subtle synchronization:

* shared pulse timing
* synchronized aura expansion
* slight variation in color phase/intensity between the two
* complementary rather than identical effect behavior

Do not make them perfectly identical clones in behavior if a small amount of variation would improve the shot.

---

# 8. Optional Stage Lighting

Investigate whether the musicians benefit from a subtle stage-like lighting treatment.

One possible approach:

* a soft localized light over the performers
* potentially a subtle spotlight/cone
* enough illumination to separate them from the environment
* preserve the astronauts' own bioluminescent effects

### This is OPTIONAL.

Do not force additional lighting into the scene if it compromises the existing Glowmere look.

The existing scene relies heavily on glowing materials, modulation, atmospheric lighting and bioluminescent effects.

Too much conventional illumination could flatten the scene and reduce the visual impact of those effects.

Therefore:

> **Only add a stage/spotlight treatment if it genuinely improves the composition.**

If implemented, keep it restrained.

Prefer a subtle theatrical pool of light over a bright obvious spotlight.

Evaluate both versions if practical.

---

# 9. UFO Abduction — Replace Existing Animal

GV3 already contains a UFO-abduction shot during the riser.

Currently the UFO abducts an animal.

Replace that subject with the **drummer astronaut**.

The intended sequence is:

1. Drummer is playing normally behind the drum kit.
2. UFO enters / appears according to the existing shot.
3. UFO begins abducting the drummer.
4. Drummer rises from behind the drum kit.
5. Drummer is lifted upward toward the UFO.
6. Drummer exits the camera framing / is abducted.
7. Once the shot no longer sees the drummer, restore him to his normal position behind the drum kit.
8. Resume the normal drum-playing animation.

The scene does **not** need to make literal narrative sense.

This is a music-video gag.

The drummer can simply disappear from the performance, get abducted, and then magically be back behind the drum kit afterward.

Do not spend time implementing narrative continuity.

---

# 10. Drummer Abduction Motion

The existing UFO abduction should provide the basic upward motion.

However, add additional character motion if feasible.

The ideal visual result is that the drummer's body becomes increasingly uncontrolled as he is pulled upward.

Potential behavior:

* legs dangling
* torso rotating slightly
* arms flailing
* drumsticks moving erratically
* hands leaving their normal playing positions
* slight rotation of the character while ascending

The motion should communicate:

> "Holy shit, I'm being abducted."

rather than:

> "Character translation Y += 0.1."

---

# 11. Bonus: Procedural Flailing

If the existing character animation system makes this practical, layer a simple procedural secondary motion on top of the abduction animation.

For example:

```text
normal drum animation
        ↓
UFO abduction begins
        ↓
reduce / override normal playing animation
        ↓
add arm + leg + torso secondary motion
        ↓
character rises toward UFO
        ↓
character leaves camera
```

The flailing does NOT need to be physically accurate.

It just needs to read clearly in the shot.

Possible implementation approaches include:

* animation blending
* additive animation
* procedural bone offsets
* simple keyed transforms
* temporary animation override

Use the simplest approach that produces a convincing result.

Do not build a generalized procedural ragdoll system for this shot.

---

# 12. Restoration After Abduction

Once the drummer is no longer visible:

* restore the drummer to the original performance transform
* restore the normal drum-playing animation
* ensure the drum kit remains in its original position
* ensure no permanent transform state from the abduction leaks into subsequent shots

The drummer should be visually identical to his pre-abduction state when he returns.

This should be deterministic and shot-specific.

---

# 13. Existing UFO Shot Preservation

Do not unnecessarily redesign the UFO itself.

Preserve the existing UFO aesthetic, animation and timing where possible.

The primary change is:

**animal subject → drummer astronaut**

with additional character-specific abduction motion.

If the existing UFO animation already provides a strong beam/pull effect, reuse it.

---

# 14. Camera / Composition Review

After implementing the musicians, review the existing GV3 cameras that see this area.

The musicians should read clearly when visible.

If necessary, make **small composition adjustments** to existing shots so that:

* both musicians aren't obscured
* instruments remain readable
* the rainbow bioluminescence is visible
* the river/environment still contributes to the shot
* the performers don't accidentally become background clutter

Do not redesign the entire sequence.

Do not add unnecessary camera shots.

---

# 15. Performance Considerations

GV3 is already approaching a performance bottleneck.

This is especially important.

Keep the musicians extremely lightweight.

Reuse:

* existing character meshes
* existing animation infrastructure
* existing materials where possible
* existing effect systems where possible

Avoid:

* expensive transparent particle volumes
* large numbers of dynamic lights
* high-resolution shadow maps
* unnecessary post-processing
* duplicated high-poly assets
* expensive per-frame CPU processing

The rainbow aura should be implemented using the **cheapest existing effect mechanism that can produce the desired visual quality**.

If the optional stage lighting introduces a significant GPU cost, omit it.

---

# 16. Art Direction

The musicians should ultimately feel like they belong in Glowmere.

Do not make them look like realistic NASA astronauts suddenly dropped into a fantasy scene.

The low-poly astronaut aesthetic is intentional.

Use the surrounding Glowmere environment, emissive materials, hero effects and lighting to integrate them into the world.

The contrast between:

**simple low-poly astronauts**

and

**rich bioluminescent environment**

is desirable.

Lean into that contrast.

---

# 17. Validation Pass

Before considering the implementation complete, render and inspect:

### Normal performance

* keyboard astronaut playing
* drummer playing
* both facing each other
* instruments clearly visible
* no vegetation/instrument clipping
* hero effects visible
* audio-reactive pulse working

### UFO sequence

* drummer playing normally
* UFO arrival
* drummer lifting from kit
* convincing flailing
* drummer leaving frame
* restoration after shot
* normal drum animation resumes

### Lighting

Evaluate the scene both:

1. without additional stage lighting
2. with the optional subtle stage lighting

Use whichever produces the stronger Glowmere result while preserving the existing bioluminescent aesthetic.


---

# 18. Scope Boundary

This task is an **art-pass implementation**, not the beginning of a generalized character system.

Do not use this work to introduce:

* Character Synthesis
* generalized animation authoring
* generalized procedural animation
* generalized ragdoll physics
* character-generation UI
* new character schemas
* generalized musician entities

However, document any reusable infrastructure discovered during implementation.

If the implementation exposes a genuinely useful missing capability in AV Gen's character/animation architecture, record it as a potential future Character Synthesis requirement rather than expanding the current scope.

---

# Definition of Done

The addendum is complete when:

* [ ] Two astronauts are integrated into GV3.
* [ ] Keyboard astronaut plays the keyboard correctly.
* [ ] Drummer plays the drum kit correctly.
* [ ] Both are positioned near the river in the central valley.
* [ ] They face one another with deliberate spacing.
* [ ] Vegetation does not visibly clip through the instruments.
* [ ] Both are registered as hero entities.
* [ ] Both have the new rainbow bioluminescent hero effect.
* [ ] The effect pulses with the music.
* [ ] Optional stage lighting has been evaluated and only retained if beneficial.
* [ ] Existing UFO animal-abduction shot has been converted to abduct the drummer.
* [ ] Drummer rises from behind the drum kit.
* [ ] Drummer performs convincing secondary/flailing motion if feasible.
* [ ] Drummer exits the shot.
* [ ] Drummer is restored to his original performance position afterward.
* [ ] Normal drum animation resumes.
* [ ] Existing GV3 shots continue to function.
* [ ] Performance impact has been evaluated.
* [ ] Final renders have been visually inspected.
* [ ] No unrelated GV3 systems have been modified unnecessarily.

This should be implemented as part of the upcoming GV3 art pass, with the existing standalone musician prototype treated as the validated source of truth for the character assets and animation setup.

---

## Revision round 1 (owner, 2026-09-29)

*The owner reviewed `GV3-art-pass-final.mp4`: "that render is looking great". Then, in their words, verbatim:*

> check all new hero mushroom stems properly connect to their caps (looks like one or two had gaps)
> go ahead and foot lock 4 legged rigs
> add a faint rim, let's see if we can improve UFO warp visibility
> try a rainbow effect instead of a gold effect on the lift - still looks prettty good either way
> should we give musicians their own class so critic doesnt treat like animals? go with best rec - just dont mess up the UFO abduction animation

*Added the same day, verbatim:*

> have it do a few passes through the critic this time - there were like one or two shots that felt a little bland but otherwise it was pretty tight

*Coordinator's notes, not the owner's words:*
1. **Stems and caps.** Check EVERY placed instance of opal and sail, not just the ones in shot, for a gap between
   stem and cap. Measure it with geometry (the distance from the stem top to the cap underside, per instance, across
   the variation seeds), not by eye. Find the cause (variation, tilt, scale, the deformer stack) and fix it at the
   cause. Also check the existing ten hero mushrooms with the same measurement, and report what is found there, but
   fix them only if they have the same defect.
2. **Foot locking for four-legged rigs.** Measure the slide at walk starts, and at steady walk, for the farm animals
   before and after, the same way the aliens were measured. Don't regress the two-legged characters or GV2. Remember
   the GV2 behaviour fingerprint.
3. **UFO warp rim.** A FAINT rim so the warp reads over black sky (the s08 flyby). It must not become a visible
   outline or halo over the lit valley. Show A/B stills over dark sky and over the lit valley.
4. **Rainbow lift light.** Replace the drummer's gold lift light with a rainbow one, consistent with the musicians'
   rainbow hero treatment. The owner likes both: show the A/B and pick the better; say which and why.
5. **Musician class.** The coordinator's recommendation, which the owner delegated to the agent to decide: give the
   musicians their own class (for example `performer`). Don't make abduction piggyback on "animal": make it eligible
   by an explicit property or tag, so the Critic classes them correctly. HARD CONSTRAINT: the abduction must be
   unchanged. Prove it with the same timings (lift at 172.8 s, back on the stool within 1 mm at 183.75 s, seen
   playing at 198.8 s) and frame-identical, or measurably equivalent, pose samples before and after the refactor. If
   the cleanest route is riskier than it's worth, say so and keep the tag with a documented reason.
6. **The Critic, a few passes (this overrides "the Critic once" for this round).** After the five items are done and
   verified, run the Critic over the whole film and act on it, in up to about three passes; stop earlier once a pass
   stops producing worthwhile improvements. Focus on the one or two bland shots, found from the Critic's per-shot
   scores and measurements (brightness, contrast, subject salience, motion), improved with targeted changes within
   existing systems (framing, timing, lighting, hero or effect emphasis, what the camera looks at). Candidates: s04
   repeating s14/s30, s05 repeating s01, subjects outweighed in s15/s31/s42. "Otherwise it was pretty tight": don't
   rework shots that work, don't redesign the sequence, don't add shots. After each pass confirm technical quality
   and lighting haven't dropped and clipping hasn't risen above the r0 final's 2.32%. The Critic's input renders can
   be at whatever resolution it needs; only the final r1 is 1080p60 with the song.
- Same rules: ADRs 986-989 remain; the fewest renders; GPU work under the gpu-lock; no licensed files committed;
  don't push, don't merge. Both suites exit 0 at the end.
- **Deliverable:** `~/Desktop/av-gen-review/20-gv3-art-pass/GV3-art-pass-r1.mp4` (1080p60, supersample 2, the
  song), the previous final kept as it is; A/B stills in `r1/`; a "Revision round 1" section in the review folder's
  REPORT.md, naming the shots treated as bland with before/after stills; for each Critic pass, the scores against
  the previous pass and what changed in response.
