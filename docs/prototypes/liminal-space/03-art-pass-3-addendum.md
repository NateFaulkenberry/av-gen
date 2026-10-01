# All You Got: art pass 3 addendum (the owner, 2026-10-01)

*A coordinator's header; the owner's words follow it, verbatim. This GOVERNS pass 3 and builds on
`02-art-pass-2.md`.*
- **Base:** branch `proto/liminal-space`, at main `f723b268` (pass 2 merged).
- **Staffing:**
  - `space-engineer` (medium): the spatial validator (§1-15), engine fixes (mannequin integrity and head, entity
    jumping, screen static, the spatial colour sweep, removing camera breathing), and corruption extensions.
  - `space-art` (max): character blocking, the intro, the key shots, critic passes, renders.
- **Bar numbering** as in pass 2: the owner's bar N is SONG-ANALYSIS bar N+1.
- The owner's weekly limit is nearly used up: commit after every step, and keep `PROGRESS-*.md` resumable cold.
- **ADRs:** 1051-1059, then ask.
- **Review media:** `~/Desktop/av-gen-review/24-liminal-space/pass3/`.

---

# ALL YOU GOT — ART PASS ADDENDUM

## Spatial Validation, Character Blocking, Digital Effects & Scene Corrections

This is an addendum to the previous All You Got music-video art-direction specification.

The previous pass produced substantially more promising material and there is now enough good material that the goal should be **refinement and structural improvement rather than wholesale replacement**.

There are several specific issues to address, plus one potentially reusable AV Gen capability that should be investigated:

> **A deterministic geometry / spatial composition validator capable of identifying obviously incorrect relationships between entities in rooms.**

---

# 1. NEW SYSTEM: ROOM / SPATIAL COMPOSITION VALIDATOR

Investigate and, if technically appropriate, implement a reusable validator for interior room scenes.

The goal is not to replace the creative critic.

The two systems should have different responsibilities:

### Geometry Validator

Answers:

> "Is this spatial arrangement actually valid?"

### Creative Critic

Answers:

> "Does this spatial arrangement look intentional, interesting and artistically effective?"

The validator should catch obvious structural mistakes before they reach the critic/render review stage.

---

# 2. VALIDATOR PRINCIPLES

The validator should operate on the **semantic scene representation**, not merely inspect the final pixels.

Where possible, entities should expose or derive:

* bounding box
* oriented bounding box
* world transform
* dimensions
* semantic category
* affordances
* room association
* surface association
* collision geometry
* intended orientation
* attachment relationship

The validator should produce machine-readable violations with:

* severity
* entity IDs
* spatial relationship
* measured distance/intersection
* human-readable explanation
* suggested correction where possible

Example:

```text
ERROR
Chair_03 intersects DiningTable_01

penetration: 0.38m

Expected relationship:
chair should occupy a seating position adjacent to table,
with sufficient clearance for a seated occupant.
```

Or:

```text
WARNING
LyricText_07 intersects Window_02

text plane overlaps window opening by 31%

Suggested correction:
move lyric to adjacent wall segment.
```

---

# 3. ROOM SEMANTICS

Investigate introducing semantic categories such as:

### Architectural

* wall
* floor
* ceiling
* door
* window
* stairs
* doorway
* room boundary

### Furniture

* couch
* chair
* table
* desk
* bed
* cabinet
* shelf
* toilet
* sink
* bathtub
* mirror

### Decorative

* plant
* lamp
* painting
* television
* wall decoration
* hanging object

### Character

* mannequin
* person
* seated character
* standing character
* lying character

### Typography

* wall text
* floor text
* stair text
* floating text

This does not necessarily need to become a huge hardcoded taxonomy.

Investigate the smallest extensible semantic/affordance system that can support these validations.

---

# 4. BASIC GEOMETRIC VALIDATION

At minimum, detect:

## Entity intersections

Flag objects that physically intersect when they should not.

Examples:

* chair through table
* couch through wall
* bed through wall
* cabinet through floor
* lamp through ceiling
* character through furniture
* furniture occupying another object's volume

Allow intentional exceptions.

Some objects obviously need to intersect:

* objects attached to walls
* lamps attached to ceilings
* windows embedded in walls
* doors embedded in doorways
* decorations mounted on surfaces

Therefore the validator needs the concept of **expected attachment / embedding relationships** rather than treating every intersection as an error.

---

# 5. FLOOR CONTACT

Detect objects that are supposed to rest on the floor but are:

* floating
* partially buried
* significantly tilted
* intersecting the floor

Examples:

```text
Chair_04:
floor clearance = 0.37m
WARNING: chair appears to float
```

and:

```text
Table_01:
floor penetration = 0.14m
ERROR
```

Small tolerances should exist for artistic/environmental variation.

---

# 6. FURNITURE AFFORDANCES

This is where the validator becomes significantly more useful than a generic collision detector.

A chair should not merely be "near a table."

It should occupy a **plausible seating relationship**.

For example:

### Dining table

Potential valid relationships:

```text
table
  |
chair
```

or:

```text
chair   table   chair
```

etc.

Check:

* reasonable distance from table
* chair orientation toward table
* adequate clearance
* no major intersection
* chair isn't placed on top of table
* chair isn't facing completely away from table without an intentional reason

Likewise:

### Desk

A chair should generally:

* be near the desk
* face the desk
* have sufficient clearance
* have a plausible seated position

### Couch

Objects should not randomly occupy the couch's seating volume.

### Bed

A character lying in bed should occupy the bed's usable surface rather than float above it or intersect its frame.

---

# 7. ROOM-SPECIFIC AFFORDANCES

Investigate allowing scene entities to declare simple affordances.

For example:

```text
Chair:
  supports: sitting
  preferred_surface: floor
  preferred_orientation: toward(table|desk)

Table:
  supports: dining|working
  preferred_surface: floor

Bed:
  supports: lying
  preferred_surface: floor

Mirror:
  mounts_to: wall
  supports: reflection

TV:
  mounts_to: wall|stand
  faces: room

Toilet:
  mounts_to: floor
  supports: sitting
```

This should be data-driven where practical.

Do not build an enormous AI reasoning system for this.

A lightweight semantic constraint layer is sufficient.

---

# 8. WALL / WINDOW / DOOR VALIDATION

Rooms should have coherent architectural relationships.

Detect:

* windows floating in walls
* windows intersecting unrelated geometry
* doors floating away from doorways
* doors embedded incorrectly
* furniture blocking doors
* furniture blocking windows when it should not
* windows extending beyond wall boundaries
* text intersecting windows
* text intersecting doors
* objects occupying doorway clearance

The goal is to prevent obviously impossible room compositions.

---

# 9. LYRIC PLACEMENT VALIDATION

This is particularly important.

Lyrics should be treated as **spatial objects**, not merely text overlays.

When placing lyric text onto a wall:

1. Identify candidate wall surfaces.
2. Determine usable wall area.
3. Detect architectural interruptions.
4. Detect windows.
5. Detect doors.
6. Detect furniture.
7. Detect other mounted objects.
8. Determine a clear text rectangle.
9. Place the lyric within that usable region.
10. Validate the final placement.

The validator should explicitly detect:

### Bad:

```text
LYRIC
████
WINDOW
████
```

where text overlaps the window.

### Good:

```text
████████████
 HOW LITTLE
 DO I KNOW?
████████████
```

on a sufficiently clear wall section.

---

# 10. LYRIC CLEARANCE

Give lyric text a configurable safety margin around obstacles.

For example:

```text
text bounds
+
minimum wall clearance
```

The exact margin should be configurable rather than hardcoded.

The system should understand that the text itself needs breathing room.

Do not allow text to technically "fit" while being visually jammed against:

* windows
* doors
* corners
* furniture
* other text
* architectural elements

---

# 11. TEXT ORIENTATION

The validator should also detect obvious orientation errors.

Wall text should generally:

* lie approximately on the wall plane
* face into the room
* have readable orientation
* avoid being mirrored
* avoid being upside down unless intentionally specified

Stair text should conform to the stair geometry.

Floating text should have an explicit world-space orientation.

---

# 12. ROOM ACCESSIBILITY / NAVIGATION

Check whether major pathways remain plausible.

For example:

* door → room
* room → hallway
* hallway → stairs
* stairs → next floor

Detect situations where:

* furniture completely blocks a doorway
* objects create impossible navigation bottlenecks
* the camera path passes through walls
* the character's intended location is inaccessible
* a room appears to have no sensible entrance

This is particularly important now that the video is intentionally structured around moving through connected rooms.

---

# 13. CHARACTER SPATIAL VALIDATION

The mannequin/character should be validated separately from ordinary furniture.

The character should have:

* valid floor contact when standing
* valid seating contact when sitting
* valid bed contact when lying
* appropriate orientation
* no obvious intersections
* no floating
* no clipping through furniture

But the validator should also understand **intentional poses**.

For example:

```text
Pose:
THINKER

Anchor:
chair

Relationship:
character seated in chair
character facing generally forward/downward
```

or:

```text
Pose:
COUCH_LYING

Anchor:
couch

Relationship:
character lying along couch seating surface
```

---

# 14. VALIDATOR OUTPUT

The validator should produce something useful to both an agent and a human.

Example:

```text
ALL YOU GOT — ROOM VALIDATION

Errors: 2
Warnings: 5

ERROR
Chair_03 intersects DiningTable_01
penetration: 0.31m

ERROR
Lyric_12 intersects Window_02
overlap: 28%

WARNING
DeskChair_01 orientation is 63° away from Desk_01

WARNING
TV_01 has no valid support relationship

WARNING
Bedroom door clearance is below recommended minimum

WARNING
Character_04 foot penetration: 0.07m

PASS
18 architectural relationships
42 entity placements
9 lyric placements
```

This should be available to the agent as structured data.

---

# 15. VALIDATOR VS CREATIVE CRITIC

Do not ask the creative critic to perform deterministic geometry checks that the scene representation can answer exactly.

Preferred workflow:

### Step 1

Generate / modify scene.

### Step 2

Run spatial validator.

### Step 3

Automatically fix deterministic errors where safe.

### Step 4

Render.

### Step 5

Run creative critic.

### Step 6

Use critic feedback for artistic refinement.

### Step 7

Re-run validator.

This creates a useful separation:

> **Does it make physical/semantic sense?**

followed by:

> **Does it look good?**

---

# 16. VERSE CAMERA / CHARACTER DIRECTION

This is a major new artistic direction for the verses.

Rather than rapidly traveling through disconnected rooms, the camera should often **sweep around the room we are currently inhabiting**.

The character should always be present in the room.

But:

> **Every time the character comes back into view, he should be in a different contemplative pose.**

We should never see him performing an animated action.

Instead, he should appear as a series of frozen contemplative tableaux.

The camera moves.

The character is still.

Then the camera returns to him from another angle or after another sweep.

His pose has changed.

This should create the feeling that time is passing even though we never actually see him move.

---

# 17. CHARACTER AS A CONTEMPLATIVE MOTIF

The character should communicate:

* loneliness
* contemplation
* confusion
* introspection
* isolation
* resignation

Potential poses:

### Living room

* sitting in chair
* leaning forward
* head in hands
* sitting motionless on couch
* lying on couch staring at ceiling

### Kitchen

* sitting alone at table
* elbows on table
* head resting in hands
* staring down at table
* staring blankly into room

### Bathroom

* standing in front of mirror
* staring at reflection
* hands resting on sink
* head lowered

### Toilet

A brief, darkly humorous variation is acceptable:

* sitting on toilet
* staring downward
* completely miserable / contemplative

Keep this subtle and deadpan rather than comedic.

### Bedroom

* lying in bed
* staring at ceiling
* sitting on edge of bed
* head in hands

### Desk

* sitting at desk
* staring at monitor
* head in hands
* sitting motionless

---

# 18. NO CHARACTER ANIMATION DURING THESE TABLEAUX

The character should generally be treated as a **static visual composition**.

Do not animate him:

* walking
* gesturing
* talking
* breathing noticeably
* performing elaborate actions

The camera provides the movement.

The character provides the emotional anchor.

This is closer to a series of moving photographs than a character performance.

---

# 19. CHARACTER POSITION CHANGES

Each return to the character should reveal a different pose/location within the same environment.

Example:

### Pass 1

Camera enters living room.

Character:

> sitting in chair, Thinker pose.

Camera sweeps away.

### Pass 2

Camera returns.

Character:

> lying on couch staring at ceiling.

Camera moves around room.

### Pass 3

Character:

> sitting forward with head in hands.

### Pass 4

Character:

> standing near window staring outside.

The character becomes a recurring visual motif.

---

# 20. THE WORLD STILL BREATHES

Remove the **camera breathing** effect from the project.

I do not think the camera breathing effect is helping the visual language.

### Remove:

* rhythmic camera bobbing
* FOV breathing
* camera pulse movement
* camera position tweening tied directly to quarter notes

The camera should feel intentional and cinematic.

### KEEP:

The world breathing.

Use the quarter-note relationship through:

* geometry
* lighting
* color
* entity effects
* object animation
* environmental response

The world should still feel musically alive.

---

# 21. INCREASE DATA-MOSHING / DIGITAL CORRUPTION

Increase the use of data-moshing and digital corruption effects throughout the world.

The intent is:

> **this is a simulated digital universe that isn't functioning perfectly.**

Explore:

* geometry displacement
* temporal corruption
* vertex tearing
* color-channel corruption
* frame-like artifacts
* object stretching
* positional glitches
* intermittent digital breakup
* brief geometry corruption
* texture/static corruption

However:

### Do not turn the entire video into a glitch effect.

The corruption should appear as a recurring property of the world.

It should be especially useful around:

* transitions
* musical accents
* BIG CLAP events
* lyric appearances
* scene changes
* emotional peaks

---

# 22. TELEVISION / COMPUTER SCREENS

All screens inside the world should display **static**.

This includes:

* television
* computer monitors
* CRT-like displays
* digital displays
* other screen-like objects

Do not leave screens blank.

Do not render arbitrary generic content unless specifically directed.

The default screen behavior should be:

> analog/digital static

This becomes another recurring visual motif suggesting that the world is a broken simulation.

---

# 23. INTRO — REVISE THE HOUSE-BUILDING SEQUENCE

The house-building intro is interesting and should become the **entire intro**.

Remove the earlier procedural abstract geometry sequence.

The current combination of:

> procedural geometry → house construction

feels disconnected.

Instead:

> **start directly with the house/world construction sequence.**

---

# 24. INTRO — SCALE IT UP

The house-building concept should become more ambitious and turbulent.

Do not just build one quiet house.

Explore:

* larger house
* multiple structures
* neighborhood
* city street
* multiple houses
* roads
* trees
* street objects
* architectural growth
* rapidly appearing geometry

The feeling should be:

> **a digital world being violently constructed in real time.**

It should feel:

* turbulent
* fast
* chaotic
* energetic
* mutating
* rapidly assembling

Like a simulated universe attempting to create itself.

---

# 25. INTRO — AUDIO TIMING

The entrance into the house is currently early.

This needs to be corrected precisely.

The camera should enter the interior of the house **exactly on the downbeat**.

There should be no awkward:

> enter early → pause → light splash → geometry appears

Instead:

### Before downbeat

Build tension.

### Downbeat

The transition into the house interior occurs.

The visual event should feel instantaneous and musically locked.

Use the actual audio timing rather than manually estimating it from the render.

---

# 26. NO PROCEDURAL INTRO GEOMETRY

Remove the initial abstract procedural geometry section.

The house/world construction sequence should carry the entire intro.

The sequence should evolve naturally from:

> construction → completion → entry → interior world

rather than:

> abstract geometry → unrelated house scene

---

# 27. ROOM NAVIGATION / AVOID REPETITION

Do not constantly double back into previously used rooms.

The house/world should feel spatially coherent.

Use:

* stairs
* different floors
* basement
* bedrooms
* kitchen
* living room
* bathroom
* office
* hallways
* other small rooms

to create progression.

Revisiting a room is acceptable when there is an artistic reason, particularly when the character's changing pose is the point.

But avoid:

> room A → room B → room A → room B → room A

simply because the procedural system selected those rooms again.

The viewer should feel like the camera is exploring a larger coherent environment.

---

# 28. ELIMINATE RANDOM ENTITY JUMPING

There are currently cases where entities randomly jump upward/downward.

This does not look intentional.

### Do not allow this.

Investigate the source of these movements.

Determine whether they are caused by:

* procedural animation
* audio modulation
* transform interpolation
* bad anchor points
* physics
* effect stacking
* animation loops
* entity spawning
* timeline interpolation

Then either:

1. fix the underlying behavior, or
2. explicitly constrain the entity so that this behavior cannot occur.

A visual effect should never accidentally look like an object is randomly bouncing because the procedural system malfunctioned.

---

# 29. ~1:28 — STAIRS / "STEPS IN A PROCESS"

The hallway scene around approximately **1:28** is one of the strongest discoveries in the current version.

Keep this concept.

The stairs contain text:

> "it's just steps in a process"

and:

> "let it go"

This is visually compelling.

However, extend the shot.

### Current concept

Camera sees stairs.

### Desired concept

Camera should **actually climb the stairs**.

As we ascend:

* reveal the text
* move upward through the geometry
* build anticipation
* continue toward the top

Then, at the top:

### The camera appears to go off a cliff.

The stairway/environment should suddenly terminate into:

> blackness

The camera continues forward/downward/outward into the black void.

This should transition directly into the **"let it go" animation**.

The metaphor should feel deliberate:

> climbing the steps → reaching the end → stepping off → letting go

This is a strong candidate for a major authored shot.

---

# 30. ~3:28 — COLOR TRANSITION

The previous rainbow wipe implementation interpreted the suggestion too literally.

Do **not** create a literal rainbow wipe across the screen.

The intended concept is:

> **a scene-level sweep of color and light**

Think of the effect more like the **camera beam effect from GV3**.

The color should travel through the environment.

For example:

* a wave of colored light moves through the room
* surfaces change color as it passes
* geometry catches the color
* objects become illuminated
* the environment transitions from one palette to another
* the camera remains inside the world

The viewer should feel like:

> **a wave of colored energy just passed through the entire environment.**

It should be spatial, volumetric, environmental and integrated into the geometry.

Not:

> "a rainbow rectangle wipes from left to right."

---

# 31. MANNEQUIN HEAD RENDERING

The mannequin currently has a rendering problem.

Its body receives illumination, but the head becomes a dark black blob.

Investigate whether this is caused by:

* material
* normals
* missing texture
* missing geometry
* lighting
* emissive settings
* shader behavior
* mesh import
* face orientation

The head should remain visually readable.

---

# 32. MANNEQUIN HEAD — GLOWING LINE TREATMENT

Explore a subtle glowing line treatment around the mannequin's head.

The goal is not necessarily a literal outline shader.

Possible solutions:

* emissive contour
* rim light
* line rendering
* silhouette glow
* fresnel effect
* thin emissive geometry
* post-process edge treatment

The important visual result:

> the head should be readable against dark environments.

This could become a particularly strong motif in the dark line-art world.

---

# 33. MANNEQUIN INTEGRITY

The mannequin's head is currently missing entirely in the final scene.

This is a hard correctness issue.

Ensure the mannequin is consistently rendered with:

* head
* body
* correct transform
* correct material
* correct lighting
* correct geometry

The geometry validator should potentially include a basic **entity integrity check** so that an expected character/entity is not silently missing major required components.

Example:

```text
ERROR
Mannequin_01

Expected components:
head
body

Missing:
head geometry
```

---

# 34. USE THE CRITIC AGAIN

After these changes, use the creative critic to review representative shots.

Pay particular attention to:

### Rooms

* Do they feel inhabited?
* Are furniture relationships believable?
* Is there enough detail?
* Does the geometry feel intentional?

### Character

* Does the character feel lonely?
* Are the poses visually different?
* Does the camera movement reveal each tableau effectively?
* Does the mannequin feel like a recurring visual motif?

### Digital world

* Does the data corruption feel integrated?
* Is there enough digital instability?
* Is it overused?

### Audio reactivity

* Does the world visibly breathe?
* Are quarter-note responses perceptible?
* Are BIG CLAPs obvious?
* Are musical events driving meaningful visual changes?

### Transitions

* Does the stair sequence communicate "letting go"?
* Does the ~3:28 color transition feel spatial rather than like a wipe?
* Does the intro land precisely on the downbeat?

---

# 35. IMPORTANT: PRESERVE THE GOOD DISCOVERIES

Do not treat this as permission to regenerate everything.

The current pass has produced several promising elements.

In particular:

* the house-building intro concept
* the hallway/stair scene around 1:28
* the general small-room visual language
* the contemplative character concept
* the primitive digital-world aesthetic
* the emerging use of lyrics inside geometry

Build on those discoveries.

The goal of this pass is:

> **make the existing good ideas feel authored, coherent, spatially valid, and musically intentional.**

---

# 36. IMPLEMENTATION PRIORITY

Prioritize this work approximately in this order:

## Priority 1 — Spatial correctness

Investigate and implement the reusable room/spatial validator.

At minimum:

* intersections
* floor contact
* wall contact
* furniture relationships
* doors
* windows
* room access
* lyric placement
* character placement

## Priority 2 — Character blocking

Implement the contemplative tableau system.

Create/reuse poses such as:

* Thinker
* couch lying
* head in hands
* kitchen table
* bathroom mirror
* toilet
* bed
* desk

Keep the character static while the camera moves.

## Priority 3 — Intro

Replace the existing abstract intro.

Make the world construction substantially more turbulent.

Synchronize house entry precisely to the downbeat.

## Priority 4 — Digital-world effects

Increase data moshing and simulation corruption.

Add static to all screens.

Eliminate accidental entity jumping.

## Priority 5 — Key shot revisions

### ~1:28

Climb stairs → reach top → go over cliff → blackness → Let It Go.

### ~3:28

Replace literal rainbow wipe with spatial/environmental color sweep.

## Priority 6 — Mannequin

Fix head integrity.

Explore glowing head contour/rim treatment.

## Priority 7 — Critic / polish

Run the critic across representative sections and iterate.

---

# 37. FINAL ARTISTIC PRINCIPLE

The video is increasingly becoming a story told through **spaces rather than characters performing a story**.

Lean into that.

The character is not an actor.

He is a recurring visual presence.

The rooms are not just procedural environments.

They are emotional states.

The geometry is not just geometry.

It is the visual language of the simulated world.

The glitches are not just effects.

They communicate that the simulation itself is imperfect.

The lyrics are not subtitles.

They are artifacts embedded in the world.

And the camera should not constantly pulse to the beat.

Instead:

> **The world listens to the music while the camera observes it.**

The result should feel like we are wandering through a strange digital house/world that is simultaneously:

**lonely → contemplative → broken → growing → increasingly alive → ultimately released.**
