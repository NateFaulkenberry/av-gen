# All You Got: art pass 4, geometry validation, cleanup and the city (the owner, 2026-10-02)

*A coordinator's header; the owner's words follow it, verbatim. This GOVERNS pass 4 and builds on passes 2-3.*
- **Base:** branch `proto/liminal-space` at `0cf4b31b`, pass 3 final (take 3).
  - Pass 3 is NOT merged to main yet.
  - Pass 3's spatial validator (ADR-1051), its time spans and lyric shape tests, `--trace-jumps` (ADR-1053), the rim
    (1052), static (1054) and world wave (1055) are all on this branch.
- **Staffing:**
  - `space-engineer` (Opus medium): Part 1. Extend the EXISTING validator (ADR-1051) with structural relationships,
    containment, text bounds and self-intersection, camera validation, and twitch detection. Investigate ~2:15.
  - `space-art` (Opus max): Parts 2-16. The cleanup, the sequence revisions, the basement, the city, and render QA.
- **Bar numbering** as before: the owner's bar N is SONG-ANALYSIS bar N+1. The times given here are the owner's
  film times.
- **GPU:** two Sonic agents are also active (`../av-gen-sonic`).
  - All GPU work goes through `tools/gpu-lock.sh`, one job per lock hold.
  - No unfiltered full-suite runs until the final hand-back.
- **ADRs:** 1056-1059, then ask the coordinator.
- **Review media:** `~/Desktop/av-gen-review/24-liminal-space/pass4/`.
- Commit at every milestone, and keep `PROGRESS-*.md` resumable cold. Usage limits can end a session without warning.

---

# ALL YOU GOT — MUSIC VIDEO

## Geometry Validation, Structural Cleanup & Art Direction Pass

We are continuing development of the **“All You Got” music video**.

This is an **art pass + technical validation pass**, not a request to rebuild the project from scratch.

The existing sequence, timing, environments, animation concepts, and visual language have substantial potential. Preserve what is already working while correcting structural inaccuracies, improving the weaker sections, and expanding the city-based storytelling.

The primary goals of this pass are:

1. Build better tooling/validation for detecting geometric and scene-layout inaccuracies.
2. Perform a comprehensive structural/art cleanup pass.
3. Address the specific timing and staging issues below.
4. Expand the city sections into a stronger visual narrative.
5. Preserve the lonely, confused, wandering emotional tone of the song.
6. Avoid adding complexity merely for visual spectacle. Every environment and effect should support the emotional progression of the music.

---

# PART 1 — FIRST: INVESTIGATE AND IMPROVE VALIDATION TOOLING

Before making extensive artistic changes, investigate whether AV Gen currently has sufficient tooling to automatically detect the kinds of problems appearing in this scene.

We are repeatedly seeing:

* Window sills that do not align with windows.
* Wall details intersecting doors.
* Wall details extending through walls.
* Text rendering partially outside walls.
* Text clipping through itself or other geometry.
* Objects intersecting furniture.
* Structural pieces appearing slightly misaligned.
* Camera shots that appear to intersect or clip through walls.
* Distorted geometry appearing at certain camera positions.
* Objects that visually "twitch" after being placed.
* Architectural components that don't appear structurally connected.
* Objects that occupy physically implausible positions.

Do NOT simply manually fix these instances one at a time without investigating whether we can improve the underlying validation workflow.

## Geometry / Scene Validator Investigation

Determine what information AV Gen already has available at the scene/entity/component level that could support automated validation.

Investigate implementing or extending a validation system capable of checking:

### Structural relationships

Examples:

* Window must be contained by / aligned with its wall opening.
* Window sill should be aligned with the window's lower edge.
* Door should be contained by a wall opening.
* Architectural trim should not cross through doors/windows.
* Roof sections should actually connect to the building they belong to.
* Floors should connect logically to walls.
* Stairs should connect floors rather than terminate in empty space.
* Railings should follow stairs/balconies.
* Furniture should remain inside the room/environment it belongs to.

### Intersection detection

Detect suspicious intersections such as:

* Furniture ↔ furniture
* Furniture ↔ walls
* Doors ↔ walls
* Windows ↔ walls
* Decorative details ↔ doors
* Decorative details ↔ windows
* Text ↔ surrounding geometry
* Stairs ↔ walls
* Character/mannequin ↔ architectural geometry

Not every intersection is necessarily an error, so distinguish between:

* expected/intentional intersections
* suspicious intersections
* clearly invalid intersections

Do not create a validator that simply reports every mesh overlap as an error.

### Containment / placement checks

For room-scale objects, determine whether objects are plausibly contained within their intended room.

Examples:

* Chair should generally be inside the dining room rather than half inside a wall.
* Table should sit on the floor.
* Chairs around a table should have plausible spacing.
* Beds should sit against/within the intended bedroom layout.
* Appliances should remain inside kitchen boundaries.
* Gym equipment should occupy the workout room.
* Laundry machines should remain within the laundry room.

### Text validation

Text deserves special treatment.

Check:

* Is text still inside the intended wall plane?
* Is it sufficiently far from the wall boundaries?
* Is it intersecting another object?
* Is it intersecting itself?
* Is the text plane facing an appropriate direction?
* Is the text clipping through architectural geometry?
* Is there sufficient clearance around the text?
* Is text being placed over doors/windows when that would make it unreadable?

The validator should ideally provide actionable information such as:

> Text entity X exceeds wall bounds by 0.21m on the right edge.

rather than simply:

> Collision detected.

### Camera validation

Investigate whether we can validate camera shots against scene geometry.

Detect:

* Camera position inside solid geometry.
* Near-plane clipping.
* Camera passing through walls.
* Camera trajectories intersecting architecture.
* Excessively close geometry that produces visual distortion.
* Camera shots that enter an environment before the environment has visually opened enough to justify the movement.

For the existing scene, specifically investigate the suspicious geometry/distortion around **~2:15**.

Determine whether this is:

* camera clipping
* geometry intersection
* bad wall placement
* depth/renderer artifact
* animation issue
* another cause

Fix the underlying cause rather than simply hiding the symptom.

---

# Validator Workflow

If practical, create a reusable scene validation tool/report that can be run against the music-video scene before an art pass.

Ideally it should produce something similar to:

## Scene Validation Report

### Critical

* Camera 03 intersects wall at frame 412.
* Text entity X extends beyond wall bounds.
* Window sill Y offset from window = 0.18m.

### Warnings

* Chair intersects table by 0.04m.
* Decorative trim intersects door opening.
* Mannequin is within 0.02m of wall.

### Informational

* Object intentionally overlaps parent architectural mesh.
* Decorative effect intersects floor.

The exact implementation is up to the agent.

The goal is **not** to build a giant generalized physics engine.

The goal is to give AV Gen enough scene-awareness to catch the kinds of obvious visual mistakes that currently require manual review.

If the existing architecture already provides suitable bounding boxes, transforms, hierarchy information, room membership, semantic entity types, or collision data, reuse those systems rather than creating a parallel architecture.

If a robust validator can be implemented without destabilizing the renderer, do it.

If some checks require additional infrastructure, document those gaps and implement the highest-value checks first.

---

# PART 2 — COMPREHENSIVE STRUCTURAL ART PASS

Once validation has been improved, run a full visual cleanup of the existing music video.

Do not only fix the examples explicitly listed below.

Review the entire sequence for:

* architectural alignment
* object placement
* intersections
* camera clipping
* text placement
* inconsistent scale
* floating objects
* objects penetrating other objects
* implausible room layouts
* unnatural mannequin placement
* visual discontinuities between shots
* objects that visibly twitch after being placed
* environment pieces that appear to move after construction
* geometry that breaks during camera movement

The finished environment should feel **structurally believable even though it is stylized**.

---

# PART 3 — INTRO

## ~0:15–0:22

The current version is not showing enough growth/intensity during this section.

The intro needs to feel like the world is increasingly assembling itself.

Increase the amount and/or speed of visible environmental development.

However:

### IMPORTANT — PLACEMENT SHOULD BECOME STABLE

Once something has been structurally placed, it should **stay there**.

For example:

* roofs should snap/build into place
* once a roof is established, it should not twitch afterward
* walls should settle into their final position
* architectural components should not subtly oscillate

The desired effect is:

> The city is being constructed rhythmically.

Not:

> The city is continuously wobbling.

Think of the construction as being driven by the song's rhythmic pulse:

* quarter-note events
* eighth-note events
* larger structural events on stronger musical moments

But once an element has completed its construction event, it becomes stable.

The visual rhythm should therefore be:

**build → lock → build → lock → build → lock**

rather than:

**build → wobble → wobble → wobble → build**

This should apply wherever appropriate throughout the sequence.

---

# PART 4 — KITCHEN / BIG CLAP

## ~1:17

There is currently a significant musical hit/clap around **~1:17** that is not being adequately represented visually.

Use this as an opportunity to improve the shot rather than simply adding a flash/effect.

### Proposed staging

Pan into the kitchen.

Show the mannequin standing at the oven/stove looking down into a steaming pot.

The implication should be:

> He is making dinner.

This should be a quiet, mundane human moment.

The steam can provide subtle environmental motion.

Use the clap to create a visually satisfying camera/event beat while maintaining the loneliness of the scene.

Avoid making it comedic or overly theatrical.

---

# PART 5 — “IT'S STEPS IN A PROCESS”

## ~1:25

The existing:

> “let it go” → camera moves into the hallway → travels upstairs

concept works.

However, the camera begins entering the hall and moving upstairs too early.

The previous version had better timing.

Restore the **general timing relationship of the previous version** while preserving the current successful feeling of:

* ascending the stairs
* gaining momentum
* reaching the upper level
* eventually falling off the edge

The fall/off-edge moment currently has the right emotional/physical feeling.

Do not lose that.

The goal is simply to delay/reposition the beginning of the stair transition so the preceding musical/visual phrase has more room.

---

# PART 6 — BASEMENT / “LET IT GO”

The basement currently needs more life and stronger environmental storytelling.

Build the basement around several distinct spaces.

## 1. Workout Room

Create a basic home gym.

Potential elements:

* bench
* dumbbells
* simple rack
* exercise equipment
* mat
* mirror or other minimal gym details

Keep it believable and relatively sparse.

## 2. Laundry Room

The existing laundry room is working.

Preserve it while improving any obvious structural inconsistencies found during validation.

## 3. Downstairs Lounge

Add a downstairs lounge area.

Important feature:

### Colorful bar

The bar should provide a visually distinctive color/light element while still feeling like part of the same house.

Do not turn this into a nightclub.

It should feel like a person's private downstairs entertainment area.

## 4. Mannequin Placement

Use the mannequin to imply different mundane activities.

Possible staging:

* folding laundry
* sitting alone in the lounge
* sitting at the bar
* sitting on/near a workbench
* standing passively in the gym

The mannequin should often feel **present but emotionally inactive**.

That contrast is useful to the song.

### Remove or reduce the tool-bench emphasis

The current tool bench area feels visually boring.

Consider cutting it entirely or replacing it with the lounge/workout context.

The basement should feel like a real lived-in environment rather than a collection of generic rooms.

---

# PART 7 — ~2:15 GEOMETRY PROBLEM

There appears to be a significant visual artifact around **~2:15**.

Investigate this specifically.

It looks like the camera may be clipping through a wall or otherwise generating distorted geometry.

Use the new validation tooling if available.

Inspect:

* camera trajectory
* near-plane
* wall geometry
* room boundaries
* animated transforms
* object hierarchy
* rendering artifacts

Identify the actual cause.

Fix the cause rather than masking it with an effect.

---

# PART 8 — “LET IT GROW”

## TRANSITION INTO THE CITY

Change the conceptual setting for the **“let it grow”** section.

Instead of continuing in an abstract/interior environment, transition into the **city**.

This should feel like a major expansion of scale.

The earlier sections have shown:

**individual → house → rooms → basement → personal routine**

Now:

**house → neighborhood → city**

Use the growth of the city as the visual escalation.

The city does not need to become gigantic instantly.

It can grow outward:

* buildings
* roads
* intersections
* streetlights
* traffic
* sidewalks
* surrounding structures
* distant skyline

The musical growth should correspond to the visual growth of the environment.

---

# PART 9 — “IS THAT ALL YOU” BRIDGE

Now that we have a substantial growing CITY rather than an abstract environment, use the city to tell a more human story.

The central idea:

## Everyone is moving through the city, but our protagonist is alone.

Create several shots.

### Shot concept 1 — Traffic light

Lead mannequin:

* sitting alone in traffic
* stopped at a red light
* completely static

Surrounding city:

* other cars/traffic if practical
* other mannequins
* movement elsewhere

The world continues around him.

### Shot concept 2 — Bar

Lead mannequin:

* sitting alone at a bar
* static
* isolated within the environment

Other mannequins:

* moving
* talking/gesturing
* performing simple activities

The protagonist is surrounded by people but remains still.

### Shot concept 3 — Park bench

Lead mannequin:

* sitting alone on a park bench
* static

Other mannequins:

* walking
* talking
* passing by
* basic ambient movement

Again:

**the city moves; he doesn't.**

This contrast is more important than making the city visually complicated.

---

# ARTISTIC EXPLORATION OF THE CITY BRIDGE

Do not treat the three examples above as a rigid shot list.

Explore variations that communicate the same central idea:

> The world is full of motion and people, but the protagonist appears emotionally disconnected from it.

Potential environments:

* crosswalk
* subway entrance
* coffee shop
* bus stop
* diner
* convenience store
* office lobby
* rooftop
* street corner
* nighttime intersection

Use restraint.

We don't need hundreds of unique interactions.

A handful of convincing ambient mannequin behaviors will make the city feel alive.

### Important contrast

Our lead mannequin should generally be:

**static / isolated / observational**

while the environment is:

**moving / active / populated**

This contrast should become one of the visual motifs of the music video.

---

# PART 10 — “IS THAT ALL YOU GOT” DANCE BRIDGE

## BRIDGE 3

Transform this section into a **breathing city sequence**.

Rather than focusing primarily on a single architectural construction effect, let the camera become the energetic element.

### Camera concept

The camera should soar through/around the city.

Potential movement:

* rising above streets
* sweeping between buildings
* diving toward intersections
* orbiting structures
* passing through urban corridors
* pulling dramatically upward
* revealing the scale of the city

The city should feel alive.

Introduce subtle environmental motion where appropriate:

* traffic
* lights
* pedestrians
* animated signage
* atmospheric movement
* subtle lighting changes
* rhythmic environmental responses

The goal is not chaos.

The goal is:

> The city has become a living organism.

The camera is now moving freely through it.

This should contrast with the static protagonist imagery from the previous bridge.

---

# PART 11 — FINAL CHORUS

When we eventually reach the top of the stairs, the lead mannequin should now be present at the top.

Previously the top-of-stairs moment could feel somewhat empty.

Instead:

### The mannequin should look triumphant.

Not cartoonishly victorious.

More like:

* upright
* purposeful
* finally reaching somewhere
* stronger silhouette
* stronger framing
* more open environment
* visually elevated above what came before

This is a payoff to the earlier stair sequence.

The character has spent the video moving through increasingly large environments.

At this point, give the audience a clear visual punctuation point.

---

# PART 12 — ~4:08 DIGITAL TRANSITION → DAWN

The digital transition around **4:08** is working well.

Keep it.

However, the dawn scene currently arrives several seconds too late.

The transition should lead **directly into the dawn scene**.

Desired structure:

**digital transition → immediate dawn reveal → ending**

Do not insert several seconds of unrelated visual material between the transition and dawn.

The dawn should feel like the natural consequence/payoff of the transition.

Review the exact timing against the music and move the dawn entrance earlier so it lands directly with the intended ending.

---

# PART 13 — OVERALL VISUAL/NARRATIVE ARC

As you revise the sequence, think about the entire music video as a progression:

### Beginning

**Construction / routine**

The world is being assembled.

### Middle

**House / rooms / repetition**

The protagonist exists inside a constructed routine.

### Expansion

**City**

The environment becomes much larger.

### Isolation

**The city is populated, but the protagonist remains alone.**

### Escalation

**The city becomes alive and enormous.**

### Final movement

**The protagonist climbs.**

### Resolution

**He reaches the top.**

### Ending

**Dawn.**

The geometry should therefore support a narrative progression rather than simply becoming increasingly elaborate.

---

# PART 14 — ART DIRECTION PRINCIPLES

Keep the existing emotional direction:

* loneliness
* confusion
* wandering
* repetition
* isolation
* searching
* gradual expansion
* eventual release

Do NOT optimize the sequence for:

* maximum polygon count
* maximum visual complexity
* random procedural detail
* generic cyberpunk spectacle
* effects for their own sake

The city should feel believable enough to emotionally support the story.

A simple empty park bench with a moving crowd can communicate more than an extremely detailed procedural environment.

Likewise, a mannequin sitting alone at a bar can communicate more than another large abstract VFX sequence.

---

# PART 15 — MUSIC REACTIVITY

Preserve the existing musical timing that is already working.

Where the sequence responds to musical events, distinguish between:

### Structural events

These should produce discrete changes:

* building placement
* roof placement
* road construction
* major environment expansion

Once completed, structural elements should remain stable.

### Continuous modulation

These can continue responding:

* lighting
* atmosphere
* steam
* subtle environmental movement
* signage
* city activity
* camera energy
* post effects

Do not use continuous modulation on structural transforms unless it is intentionally part of the effect.

This distinction should help eliminate the "everything is slightly twitching" problem.

---

# PART 16 — FINAL QA PASS

After completing the art pass, do a dedicated QA/render review.

Do not assume that fixing the scene data means the render is correct.

Review the actual rendered result for:

### Geometry

* walls
* windows
* doors
* sills
* roofs
* stairs
* floors
* furniture
* decorative elements

### Camera

* clipping
* intersections
* bad framing
* premature transitions
* awkward camera acceleration
* excessive wobble

### Characters

* mannequin intersections
* floating mannequins
* impossible poses
* mannequin clipping through furniture
* inconsistent positioning

### Text

* wall boundaries
* readability
* self-overlap
* clipping
* orientation

### Animation

* objects twitching after placement
* abrupt transforms
* objects snapping incorrectly
* structural elements moving after construction

### Musical timing

Specifically verify:

* intro growth ~0:15–0:22
* clap ~1:17
* stair transition ~1:25
* basement section
* geometry issue ~2:15
* city transition during “let it grow”
* “is that all you” city isolation section
* Bridge 3 city camera sequence
* final stair payoff
* ~4:08 digital transition → dawn

---

# IMPLEMENTATION STRATEGY

Work in this order:

## Phase 1 — Investigate

Inspect the existing scene and determine:

* what validation infrastructure already exists
* what geometry metadata is available
* what camera/scene bounds are available
* what semantic entity types exist
* what can be validated cheaply
* what requires additional infrastructure

## Phase 2 — Validation Tooling

Implement the highest-value reusable checks.

Do not over-engineer.

Prioritize checks that catch the exact recurring problems in this project.

## Phase 3 — Existing Scene Cleanup

Run validation.

Fix the existing structural problems.

Then manually inspect the rendered sequence for problems the validator cannot detect.

## Phase 4 — Sequence Revision

Implement the timing/staging changes above.

## Phase 5 — City Expansion

Build the city transition and new city-based bridge concepts.

Focus on believable composition and emotional storytelling rather than raw complexity.

## Phase 6 — Final Art Pass

Unify:

* scale
* lighting
* architectural language
* mannequin placement
* camera language
* color
* environmental density
* transitions

## Phase 7 — Render QA

Render representative clips from every major section.

Inspect both:

* scene/geometry correctness
* final visual result

Fix any remaining issues.

---

# IMPORTANT

Do not destroy working material simply because it isn't explicitly mentioned in this specification.

This is an iterative art direction pass.

Preserve successful timing, shots, effects, environments, and musical relationships wherever possible.

When uncertain between adding complexity and improving clarity, prefer clarity.

The goal is to make the existing music video feel **intentional, structurally believable, emotionally coherent, and increasingly alive** rather than simply making it contain more things.
