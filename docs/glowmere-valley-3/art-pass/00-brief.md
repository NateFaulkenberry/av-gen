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
