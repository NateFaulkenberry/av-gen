# AV GEN — CINEMATIC SEQUENCER / MUSIC VIDEO AUTHORING EXPANSION

## Mission

You are working directly inside the existing AV Gen repository.

AV Gen has grown from a realtime audiovisual visualizer into a serious native C++23 audiovisual engine capable of:

- realtime 3D rendering
- offline rendering
- procedural scenes
- imported 3D assets
- camera control
- lighting
- particles/ecology
- shaders
- audio analysis
- beat/onset/band analysis
- parameter modulation
- timelines
- keyframes
- scene authoring
- serialization
- GPU rendering
- editor tooling

The project is now powerful enough that we want to test a substantially more ambitious workflow:

> **Can AV Gen actually be used to author a simple music video?**

The specific proof-of-concept is an **isometric stylized 3D city / life simulation music video**.

The music video concept:

- a stylized isometric 3D city
- a recurring character
- the character moves through different environments
- different scenes correspond to different sections of the song
- the camera follows, observes, reveals, and reframes the character
- environments contain simple animation
- the soundtrack controls some aspects of the world
- text/lyrics can appear above the rendering
- the timeline determines when scenes, shots, animations, lyrics, and camera movements occur
- the final result can be rendered offline as a complete video

This is NOT a request to turn AV Gen into Maya, Blender, Unreal, Unity, After Effects, or Premiere.

The objective is much more focused:

> **Build the smallest coherent cinematic sequencing toolkit that lets AV Gen author a real, timed, shot-based audiovisual piece.**

Then actually use those tools to build a basic proof-of-concept music video project.

---

# 1. FIRST: UNDERSTAND WHAT ALREADY EXISTS

Before writing significant code, thoroughly inspect the repository.

Do not assume anything in this document already exists.

Determine the actual state of:

- scene system
- scene loading/unloading
- scene serialization
- asset system
- animation support
- skeletal animation support
- glTF animation support
- camera system
- camera controllers
- transform system
- timeline
- keyframes
- interpolation
- parameters
- modulation
- audio analysis
- project format
- render targets
- HDR pipeline
- composition/layer system
- offline renderer
- realtime renderer
- editor
- viewport
- timeline UI
- performance instrumentation

Map the relevant architecture.

Identify which requested capabilities already exist and should simply be exposed to the timeline.

Identify which capabilities are missing.

Do not rebuild functionality that already works.

---

# 2. THE CENTRAL ARCHITECTURAL IDEA

The project should evolve toward this conceptual model:

```text
                           AV GEN PROJECT
                                │
             ┌──────────────────┴──────────────────┐
             │                                     │
           AUDIO                              SEQUENCE
             │                                     │
      ┌──────┴──────┐                   ┌──────────┴───────────┐
      │             │                   │                      │
   Analysis      Modulation           Shots                Overlays
      │             │                   │                      │
      └──────┬──────┘                   │             ┌────────┴───────┐
             │                          │             │                │
             │                       Scene          Text             Graphics
             │                       Camera
             │                       Character
             │                       Animation
             │
             └───────────────┬───────────────┘
                             │
                       FINAL COMPOSITION
                             │
                    Realtime / Offline
```

The timeline/sequence becomes the **orchestration layer**.

The renderer remains responsible for rendering.

The scene system remains responsible for scenes.

The animation system remains responsible for animation.

The composition system remains responsible for 2D overlays.

The audio system remains responsible for audio analysis.

The sequence system coordinates them.

---

# 3. IMPORTANT ARCHITECTURAL PRINCIPLE

Do NOT create separate systems for:

```text
TextAnimationSystem
CameraAnimationSystem
CharacterAnimationSystem
SceneAnimationSystem
LyricAnimationSystem
```

if the existing architecture can support a common abstraction.

Prefer:

```text
Timeline
    ↓
Track
    ↓
Keyframes / Clips
    ↓
Target Property
    ↓
Evaluated Value
```

This should eventually allow:

```text
Camera.position.x
Character.position.z
Text.opacity
Light.intensity
Object.rotation.y
Scene.parameter
```

to all be animated through the same fundamental machinery.

The user should not care whether the thing being animated is a camera, character, light, text layer, or scene parameter.

---

# 4. INTRODUCE THE CONCEPT OF A SEQUENCE

If the architecture does not already have an equivalent concept, introduce a lightweight Sequence/Cinematic Sequence abstraction.

A sequence represents the complete timed audiovisual performance.

Conceptually:

```text
Sequence
│
├── duration
├── tracks
├── shots
├── layers
└── markers
```

The sequence is synchronized to the project's audio timeline.

The sequence must be deterministic.

Given:

```text
time = 37.250 seconds
```

the same scene, camera, character, animation, lighting, and overlay state must be evaluated regardless of whether playback arrived at that time normally or an offline renderer jumped directly there.

---

# 5. SHOTS

Introduce a lightweight concept of a **Shot**.

A shot represents a section of the music video timeline during which a particular visual setup is active.

Conceptually:

```text
Shot
├── name
├── start
├── duration
├── scene
├── camera
└── optional state
```

Example:

```text
00:00 — 00:18
"Morning City"

00:18 — 00:36
"Neighborhood Walk"

00:36 — 00:52
"Downtown"

00:52 — 01:15
"Night City"
```

A shot should not necessarily duplicate a scene.

Multiple shots can reference the same scene.

A scene can therefore be reused with:

- different camera positions
- different camera animation
- different lighting
- different character positions
- different time ranges
- different visual treatments

This is important for efficiency.

---

# 6. SCENE SWITCHING

The sequence must be able to determine which scene is active at a given time.

At the simplest level:

```text
Shot A
    Scene = CityMorning
    0:00 → 0:20

Shot B
    Scene = Downtown
    0:20 → 0:40
```

At shot boundaries:

- deactivate previous scene
- activate next scene
- load resources as appropriate
- preserve deterministic state
- avoid visible corruption
- avoid unnecessary reloads where practical

Do not implement elaborate streaming infrastructure unless the existing engine makes it straightforward.

But architect the feature so that future scene preloading is possible.

---

# 7. SCENE TRANSITIONS

Initially support simple hard cuts.

This is enough:

```text
CITY A
████████████████

CITY B
                ████████████████
```

Then, if straightforward, support:

```text
Crossfade
Fade to black
Fade from black
```

Do not build a giant transition library.

The important thing is that shot boundaries become explicit timeline events.

---

# 8. CAMERA AS A FIRST-CLASS ANIMATABLE OBJECT

This is extremely important.

The camera should be controllable through the sequence/timeline.

At minimum expose:

```text
position X
position Y
position Z

rotation X
rotation Y
rotation Z

fov
```

or whatever camera representation the existing engine actually uses.

The user must be able to keyframe the camera.

Example:

```text
00:00
camera position = A
camera rotation = A

00:08
camera position = B
camera rotation = B

00:16
camera position = C
camera rotation = C
```

The result should be smooth cinematic movement.

---

# 9. CAMERA EASING

Camera animation should not feel like a primitive linear interpolation demo.

Support at least:

```text
Linear
Ease In
Ease Out
Ease In/Out
```

If the existing timeline supports better interpolation, reuse it.

Eventually the engine can gain:

- Bézier curves
- spline paths
- camera rails
- look-at targets
- shake
- dolly controls
- orbit controls

But DO NOT require those for this implementation.

---

# 10. CAMERA LOOK-AT / TARGET SUPPORT

Investigate whether the current camera architecture can support a simple target/look-at concept.

This would be extremely useful for the music video.

Conceptually:

```text
Camera
    ↓
Look At
    ↓
Character
```

Then the camera can move through the city while automatically keeping the character framed.

If the architecture supports it cleanly, implement:

```text
Look At Target
Look At Weight
```

or an equivalent mechanism.

This is preferable to requiring the user to hand-author every rotation value.

---

# 11. CAMERA PRESETS

If practical, expose simple cinematic camera presets.

Examples:

```text
Isometric
Follow
Wide
Close
Top Down
Tracking
```

These do not need to be complex systems.

They can simply establish useful initial camera configurations.

The goal is to reduce the amount of manual setup required to create a shot.

---

# 12. CHARACTER SUPPORT

The proof-of-concept requires a recurring character.

Inspect the existing asset pipeline.

Determine what animation formats are already supported.

If glTF/GLB skeletal animation is available, use it.

Do not build a skeletal animation system from scratch unless absolutely necessary.

The character needs at minimum:

```text
position
rotation
scale
visibility
animation selection
```

Ideally:

```text
animation playback speed
animation time
loop
```

---

# 13. CHARACTER ANIMATION CLIPS

The timeline should be capable of triggering an animation clip.

Example:

```text
00:00 → Idle

00:04 → Walk

00:16 → Idle

00:19 → Walk

00:32 → Run
```

The implementation should ideally distinguish between:

```text
Animation selection
```

and:

```text
Character transform
```

so that the same walking animation can be reused while the character moves anywhere.

---

# 14. CHARACTER MOVEMENT

Do NOT build a full navigation/pathfinding system.

For the initial music-video experiment, simple transform animation is enough.

Example:

```text
00:10
Character.position = street A

00:20
Character.position = street B
```

The walking animation plays while the transform moves.

If desired, interpolate the transform smoothly.

This is enough to create:

> a character walking through a city.

Later we can build:

- path splines
- navmesh
- collision
- autonomous agents
- crowds

Those are not required now.

---

# 15. OPTIONAL PATH / SPLINE SUPPORT

If the existing engine already has spline/path functionality, expose it.

Otherwise, do not let this block the project.

The ideal future model is:

```text
Character
    ↓
Path
    ↓
Timeline
```

rather than manually keyframing hundreds of transform points.

But two-point or multi-keyframe transform animation is perfectly acceptable for v1.

---

# 16. GENERIC TRANSFORM ANIMATION

This should not be character-specific.

The same system should eventually animate:

```text
Character
Vehicle
Prop
Light
Camera
Particle emitter
Floating object
```

with:

```text
position
rotation
scale
```

using the common timeline architecture.

If generic transform animation already exists, expose it rather than creating a new mechanism.

---

# 17. SCENE PARAMETERS SHOULD BE ANIMATABLE

This is one of AV Gen's biggest advantages over conventional editors.

The sequence should eventually be able to control arbitrary exposed scene parameters.

Examples:

```text
time of day
fog density
fog color
light intensity
sky brightness
particle amount
world scale
material parameter
environment color
```

The existing parameter/modulation system should be reused.

This means a shot can have both:

```text
camera animation
```

and:

```text
audio-reactive scene animation
```

simultaneously.

---

# 18. AUDIO MUST REMAIN A FIRST-CLASS DRIVER

Do NOT turn AV Gen into a conventional silent animation system.

The music is still central.

The final architecture should permit:

```text
Song
 ↓
Analysis
 ↓
Beat / Energy / Bands / Onsets
 ↓
Modulation
 ↓
Scene + Camera + Character + Composition
```

Examples:

```text
Bass → city light intensity

Beat → street light pulse

Energy → crowd activity

High frequencies → neon brightness

Beat → text scale

Section transition → scene change
```

Do not necessarily implement all of these now.

The important requirement is that the sequence system must coexist cleanly with the existing audio-reactive system.

---

# 19. MUSIC SECTIONS / MARKERS

Add a lightweight timeline marker concept if one does not already exist.

Markers should allow the user to label musical sections:

```text
0:00 INTRO
0:18 VERSE
0:42 CHORUS
1:02 VERSE
1:25 CHORUS
1:50 BRIDGE
2:10 FINAL CHORUS
```

Markers are not necessarily executable events.

They primarily provide authoring context.

Eventually they can drive:

- scene changes
- presets
- visual themes
- automation

---

# 20. BEAT / MUSICAL MARKERS

If the audio system already knows beat positions, expose them visually on the timeline.

For example:

```text
| | | | | | | | | | | | | | |
^   ^   ^   ^   ^   ^   ^

BEATS
```

This would make manual synchronization dramatically easier.

If beat timestamps are already available, do not duplicate them.

Expose existing data.

---

# 21. SNAP-TO-BEAT

If practical, implement optional snapping:

```text
Snap:
[ ] Off
[ ] Frames
[✓] Beats
[ ] Markers
```

This could be incredibly useful for music-video authoring.

But keep it lightweight.

---

# 22. COMPOSITION / TEXT SYSTEM

Continue the previously specified composition system.

The sequence should support:

```text
3D render
Text
Image
Shape
```

as compositing layers.

Text should support:

```text
content
font
size
color
opacity
alignment
position
scale
rotation
anchor
```

and keyframes.

---

# 23. TEXT TIMING

Text layers need explicit timing.

Example:

```text
Text Layer
    start = 00:12.400
    end   = 00:15.800
```

Outside that range it is invisible.

This is enough to create lyrics.

---

# 24. LYRIC WORKFLOW

For the proof of concept, create several timed text layers.

Example:

```text
00:12.4
"I'M WALKING"

00:15.8
"THROUGH THE CITY"

00:19.2
"TONIGHT"
```

Each can have:

- different position
- different size
- different color
- different animation

The text should be rendered over the 3D world.

---

# 25. SIMPLE TEXT ANIMATION PRESETS

If there is enough time after the core system is stable, implement a tiny set of reusable text animation presets:

```text
Fade In
Fade Out
Fade In/Out
Scale Pop
Slide Up
Slide Down
```

These should be built on top of the generic keyframe/property system.

Do NOT create a hardcoded animation engine.

A preset should simply create or manipulate keyframes.

---

# 26. LAYER ORDER

The final composition should conceptually be:

```text
Background / 3D Scene
        ↓
Graphics
        ↓
Text
        ↓
Foreground overlays
```

Layer ordering must be explicit.

---

# 27. STATIC BORDER / FRAME

The system should allow a static border.

It can be:

- a Shape Layer
- an Image Layer

depending on what is easiest.

Example:

```text
┌─────────────────────────────────────────┐
│                                         │
│                                         │
│             3D CITY                     │
│                                         │
│                                         │
└─────────────────────────────────────────┘
```

This demonstrates that AV Gen can produce a deliberately composed visual frame rather than merely rendering a scene.

---

# 28. EDITOR STRUCTURE

The editor should evolve toward something conceptually like:

```text
┌──────────────────────────────────────────────────────┐
│                     VIEWPORT                         │
│                                                      │
│                 3D MUSIC VIDEO                      │
│                                                      │
├──────────────────────────────────────────────────────┤
│ LAYERS / SHOTS                                       │
│                                                      │
│ Scene: Morning City                                  │
│ Camera                                                │
│ Character                                             │
│ Lyrics                                                │
│ Border                                                │
├──────────────────────────────────────────────────────┤
│ TIMELINE                                              │
│                                                      │
│ 0      10      20      30      40      50           │
│ │       │       │       │       │       │            │
│ ├─────────────── SHOT ──────────┤                    │
│       ◆────────◆ Camera                               │
│    █████████████ Character                            │
│             █████ Lyrics                              │
│ █████████████████████ Border                         │
└──────────────────────────────────────────────────────┘
```

Do not blindly reproduce this layout.

Use the existing UI architecture and design language.

The point is conceptual:

> The user should be able to understand the entire audiovisual composition from the editor.

---

# 29. SHOT TRACK

If practical, create a visible shot track.

Example:

```text
SHOT

[ Morning City ][ Neighborhood ][ Downtown ][ Night ]
```

Selecting a shot should expose its scene/camera properties.

---

# 30. TIMELINE SCRUBBING

Scrubbing must update:

- active scene
- camera
- character
- animation
- text
- graphics
- audio-reactive state where deterministic

immediately.

This is critical to the authoring experience.

A music-video tool that cannot scrub reliably is not useful.

---

# 31. PLAYBACK

Playback should:

1. start from current playhead
2. evaluate sequence time
3. determine active shots
4. evaluate keyframes
5. evaluate scene state
6. evaluate character animation
7. evaluate composition
8. render final frame
9. synchronize audio

Realtime playback should not depend on accumulated frame count for deterministic animation.

Use the authoritative timeline time.

---

# 32. OFFLINE VIDEO

The entire sequence must render through the existing offline renderer.

The offline renderer should understand:

```text sequence time
shot selection
camera
character animation
composition
text
```

A 60-second sequence rendered offline should produce the same visual state as the realtime preview at corresponding timestamps.

This is a fundamental requirement.

---

# 33. FRAME-ACCURATE DETERMINISM

Avoid:

```text wall clock
random unseeded state
frame-count-dependent animation
playback-history-dependent scene state
```

Prefer:

```text absolute timeline time
deterministic seeds
explicit animation time
explicit scene evaluation
```

This matters enormously for offline rendering.

---

# 34. SCENE STATE RESET

Think carefully about scene state when jumping around the timeline.

For example:

```text user scrubs from 10s → 45s → 3s → 30s
```

The scene should evaluate correctly.

Do not assume the user always plays forward from zero.

If some simulation systems inherently depend on accumulated state, identify them and determine the appropriate strategy.

For the first cinematic system, prefer deterministic/evaluable animation over simulations that cannot be randomly accessed.

---

# 35. PERFORMANCE

This is extremely important.

The existing renderer is already performance-constrained in complex scenes.

Do not build a sequencing system that causes the engine to become dramatically slower.

Particularly avoid:

- rebuilding entire scenes every frame
- reloading assets unnecessarily
- recreating text resources every frame
- rebuilding animation data every frame
- CPU/GPU synchronization stalls
- unnecessary render passes
- repeated serialization
- excessive allocation

The sequence evaluator should ideally be lightweight.

---

# 36. SCENE LOADING / PRELOADING

For v1, it is acceptable for scenes to load relatively simply.

However, architect scene activation so that future preloading is possible.

Eventually:

```text
Current Shot
       ↓
Current Scene

Next Shot
       ↓
Preload Next Scene
```

This will be important for seamless music videos with many environments.

Do not build the complete streaming system unless necessary now.

---

# 37. SERIALIZATION

The project format should persist:

```text
sequence
shots
shot timing
scene references
camera state
camera animation
character instances
character animation
composition layers
text
layer timing
keyframes
markers
```

Use existing project serialization.

Add versioning appropriately.

Old projects must continue to load.

---

# 38. DATA MODEL EXAMPLE

The exact schema should follow the repository's conventions, but conceptually we want something resembling:

```text
Project
│
├── Audio
│
├── Scenes
│
├── Assets
│
└── Sequence
    │
    ├── Shots
    │   ├── Shot
    │   │   ├── start
    │   │   ├── duration
    │   │   ├── scene
    │   │   └── camera
    │   │
    │   └── ...
    │
    ├── Animation Tracks
    │
    ├── Composition Layers
    │
    └── Markers
```

Do not copy this literally if the existing architecture suggests a better model.

---

# 39. PROOF-OF-CONCEPT MUSIC VIDEO

After implementing the required tools, DO NOT STOP.

Actually construct a basic music-video project.

This is part of the task.

The project should demonstrate the entire pipeline.

---

# 40. POC VISUAL CONCEPT

Create an initial stylized isometric city.

The visual target is broadly:

> a charming, highly stylized miniature/isometric 3D city that feels like a modern simulation game or animated diorama rather than realistic AAA graphics.

Possible aesthetic directions:

- SimCity
- Cities: Skylines
- miniature diorama
- stylized Nintendo-like environments
- clean colorful low-poly art
- slightly cinematic lighting
- nighttime neon variant

Do not chase photorealism.

We want a coherent visual style.

---

# 41. CITY SCENE

Build or assemble a small city environment using the project's existing asset capabilities.

Include as many of the following as practical:

```text
roads
sidewalks
buildings
trees
streetlights
cars
signs
parks
benches
small props
```

The city does NOT need to be enormous.

A small highly composed environment is preferable to a huge empty procedural world.

Art direction matters more than scale.

---

# 42. RECURRING CHARACTER

Place a stylized human character in the city.

The character should be visually readable from the isometric camera.

The character is the protagonist.

The entire video should feel like we are following this person.

---

# 43. FIRST SHOT

Create something like:

```text
SHOT 01 — CITY INTRO

0:00 → 0:12

Wide isometric view.

Character is somewhere in the city.

Camera slowly pushes toward the neighborhood.

Streetlights / environmental elements subtly animate.

Music begins.
```

---

# 44. SECOND SHOT

```text
SHOT 02 — THE WALK

0:12 → 0:28

Camera moves closer.

Character walks down a street.

Camera follows.

Buildings pass through frame.

Some environmental animation occurs.

First lyric/text appears.
```

---

# 45. THIRD SHOT

```text
SHOT 03 — REVEAL

0:28 → 0:45

Camera pulls upward and outward.

Reveal the larger city.

Music reaches a more energetic section.

City lights / particles / environmental elements respond to music.
```

---

# 46. FOURTH SHOT

```text
SHOT 04 — NIGHT

0:45 → 1:05

Transition to nighttime environment or nighttime lighting.

Character continues moving.

Neon/signage/streetlights become visually important.

Camera moves laterally or circles the character.

Lyrics continue.
```

---

# 47. FIFTH SHOT

If practical:

```text
SHOT 05 — DESTINATION

1:05 → END

Character reaches a recognizable location.

Camera settles.

Text/title appears.

Final musical hit triggers a visual change.

End on a composed frame.
```

This does not need to be a polished music video.

It needs to be a **credible proof that AV Gen can author one.**

---

# 48. MAKE THE CITY FEEL ALIVE

Avoid the common failure mode:

> static buildings + one walking character + camera movement.

Even a small city should have secondary animation.

Examples:

```text
cars moving
streetlights changing
windows turning on
trees moving
particles
small environmental objects
animated signs
pedestrians if available
```

Use existing procedural/audio-reactive capabilities wherever possible.

We want:

> a living world that happens to be choreographed around a song.

---

# 49. USE AUDIO REACTIVITY SELECTIVELY

Do NOT make every object violently pulse.

That would destroy the cinematic quality.

Instead use subtle musical relationships:

```text
beat → light pulse
bass → neon intensity
energy → particle activity
section change → lighting change
major hit → camera accent
```

The 3D scene should remain visually composed.

---

# 50. CAMERA LANGUAGE

The music video should demonstrate multiple types of shots.

Use:

```text
wide establishing
medium tracking
close character shot
pullback/reveal
lateral tracking
overhead/isometric
```

Even if the actual implementation is simple.

The goal is to prove the sequence system can produce cinematic variety.

---

# 51. LYRIC/TEXT TREATMENT

Use the new composition system to add a few lyric phrases.

Do not cover the entire screen with text.

Keep typography intentionally composed.

For example:

```text
small subtitle
bottom center

or

large phrase
center screen

or

small title
upper corner
```

Animate at least some text.

Demonstrate:

- fade
- movement
- scale
- timing

---

# 52. STATIC FRAME

Add a subtle border or frame.

This is deliberately simple but demonstrates the compositing system.

---

# 53. MUSICAL STRUCTURE

Choose a suitable test track already available to the project, or use an existing test asset if appropriate.

Do not spend the majority of the implementation effort searching for music.

The important thing is that the sequence has:

```text
intro
section change
more energetic section
another section
ending
```

so that the visual sequencing capabilities can be demonstrated.

---

# 54. ART DIRECTION OVER TECH DEMO

This is extremely important.

Do not create a proof-of-concept that looks like:

```text
gray cubes
+
default human
+
camera moving
```

if the existing asset library allows something better.

Use coherent assets.

Use lighting.

Use color.

Use composition.

Use depth.

Use atmospheric perspective.

Use a restrained palette.

Even a technically simple scene should look deliberately authored.

---

# 55. DO NOT SACRIFICE THE EXISTING AV-GEN IDENTITY

This is not intended to turn AV Gen into a conventional linear video editor.

Its differentiator remains:

```text
AUDIO
  ↓
PROCEDURAL WORLD
  ↓
REALTIME GPU
  ↓
MODULATION
  ↓
CINEMATIC SEQUENCING
  ↓
COMPOSITION
```

That combination is the point.

---

# 56. FUTURE EXTENSIONS TO KEEP IN MIND

Do not implement these unless they fall out naturally, but avoid architectural dead ends around:

### Camera
- spline paths
- camera rails
- look-at targets
- depth of field
- motion blur
- handheld/shake
- camera presets

### Character
- animation blending
- animation state machines
- path following
- IK
- crowds
- multiple characters

### Scenes
- scene preloading
- streaming
- nested scenes
- scene variants
- environment state

### Timeline
- markers
- beat snapping
- clip duplication
- ripple editing
- trimming
- copy/paste
- grouped tracks

### Composition
- images
- shapes
- masks
- blend modes
- shader layers
- video layers
- nested compositions

### Text
- lyric import
- LRC
- SRT
- WebVTT
- text presets
- animated typography
- text-on-path

Do not build all of this now.

But don't architect something that makes these impossible.

---

# 57. TESTING REQUIREMENTS

Add tests appropriate to the architecture.

At minimum verify:

### Sequence

- sequence creation
- duration
- shot ordering
- shot activation
- scene selection

### Timeline

- keyframe evaluation
- interpolation
- scrubbing
- out-of-range behavior

### Camera

- transform evaluation
- deterministic evaluation

### Character

- animation selection
- transform evaluation
- deterministic animation time

### Composition

- layer ordering
- visibility
- timing
- text properties

### Serialization

- sequence save/load
- shots
- keyframes
- layers
- text
- references

### Determinism

Evaluate the same timestamp through multiple paths and verify equivalent state where practical.

---

# 58. PERFORMANCE TESTING

Measure:

```text sequence evaluation CPU cost
camera evaluation cost
animation evaluation cost
text composition cost
scene switching cost
```

Do not optimize blindly.

Instrument before making major architectural assumptions.

The sequence layer should ideally be negligible compared to actual scene rendering.

---

# 59. DOCUMENTATION

Update project documentation with:

```text
Sequence architecture
Shot model
Timeline integration
Camera animation
Character animation
Composition integration
Realtime/offline behavior
Serialization
Known limitations
```

Add an ADR if this represents a meaningful architectural decision.

---

# 60. DEVELOPMENT PHILOSOPHY

This is a feature-expansion task, but avoid scope explosion.

At every point ask:

> What is the smallest implementation that allows the user to author a real music-video shot?

Prefer:

```text
simple
generic
deterministic
composable
extensible
```

over:

```text
huge
specialized
clever
fragile
```

---

# 61. CRITICAL UX TEST

When implementation is complete, perform this mental/workflow test:

Can a user:

1. Load a song
2. Create/open a sequence
3. Add a scene
4. Add a character
5. Position the character
6. Choose a character animation
7. Animate the character
8. Animate the camera
9. Create a second shot
10. Switch to another scene
11. Add a lyric
12. Position the lyric
13. Animate the lyric
14. Add a border
15. Scrub through the song
16. Play the complete sequence
17. Render the complete sequence offline

without touching source code?

If not, identify the missing workflow pieces and implement the smallest solution.

---

# 62. IMPORTANT: BUILD THE TOOL, THEN USE THE TOOL

Do not consider this task complete merely because:

```text
classes compile
tests pass
UI exists
```

The actual proof is the music-video project.

Use the newly created functionality to build it.

If the process of constructing the POC exposes awkward APIs, missing controls, confusing UI, or architectural problems, fix them.

The POC is itself an integration test of the entire authoring workflow.

---

# 63. SUCCESS CRITERIA

The implementation succeeds if AV Gen can credibly produce something resembling:

```text
SONG
 │
 ├── INTRO
 │     └── Wide city shot
 │
 ├── VERSE
 │     └── Character walks
 │
 ├── CHORUS
 │     └── Camera reveals city
 │
 ├── VERSE
 │     └── Character moves through another area
 │
 ├── CHORUS
 │     └── Night / energetic scene
 │
 └── OUTRO
       └── Character reaches destination
```

with:

- timed scenes
- animated cameras
- animated character
- animated environment
- audio-reactive elements
- lyrics/text
- static graphics
- timeline editing
- deterministic offline rendering

---

# 64. FINAL ARCHITECTURAL GOAL

The long-term AV Gen architecture should be capable of expressing:

```text
                         PROJECT
                            │
                          SONG
                            │
                         SEQUENCE
                            │
              ┌─────────────┼─────────────┐
              │             │             │
            SHOTS        OVERLAYS      MARKERS
              │             │
       ┌──────┼──────┐      ├── Text
       │      │      │      ├── Image
     Scene  Camera Character └── Shape
       │      │      │
       └──────┴──────┘
              │
        AUDIO REACTIVE
              │
          PARAMETERS
              │
          MODULATION
              │
          FINAL FRAME
              │
       ┌──────┴──────┐
       │             │
    REALTIME      OFFLINE
```

This is the direction.

Not a traditional 3D animation package.

Not a traditional video editor.

An **audiovisual composition engine** where the timeline choreographs procedural worlds, cameras, characters, effects, typography, and audio-reactive systems.

---

# 65. AGENT AUTONOMY

You have broad freedom to make engineering decisions.

If the repository reveals a better architecture than the conceptual model described here, use it.

If an existing abstraction can elegantly cover several requested capabilities, extend it.

If a requested feature is unnecessarily complicated because of the current architecture, simplify it.

If a subsystem genuinely needs refactoring before this can be done correctly, refactor it rather than accumulating hacks.

Do not ask for permission for every implementation detail.

Use engineering judgment.

Prioritize:

1. architectural coherence
2. actual authoring usability
3. deterministic rendering
4. performance
5. maintainability
6. extensibility

---

# 66. FINAL DELIVERABLE

At the end of the task, provide a detailed report containing:

### Architecture
- existing systems discovered
- architectural changes
- new abstractions

### Implementation
- files/modules changed
- features implemented
- UI added

### Sequencing
- shots
- scene switching
- timeline
- keyframes
- camera animation

### Characters
- asset support
- animation playback
- transform animation

### Composition
- text
- graphics
- layer ordering
- timing

### Audio
- integration with existing analysis/modulation

### Rendering
- realtime
- offline
- determinism

### Performance
- measurements
- bottlenecks
- optimizations

### Testing
- tests added
- integration testing

### Documentation
- docs/ADRs updated

### POC
- describe the actual music-video project created
- describe each shot
- explain which new features it demonstrates

### Limitations
Be completely honest about what is still primitive.

### Next Steps
Give a prioritized roadmap based on what was learned by actually building the POC.

---

# FINAL DIRECTIVE

Do not think of this as:

> "Add some animation features."

Think of it as:

> **Give AV Gen enough cinematic sequencing capability that we can attempt to make an actual music video with the engine we have built.**

The existing renderer is already capable of producing increasingly sophisticated worlds.

The missing capability is **choreography through time**.

Build that bridge.

Then use it.

Make the first rough music video.

The goal is not perfection.

The goal is to reach the moment where we can press PLAY and genuinely watch:

**a character move through an authored 3D world while the world, camera, typography, lighting, and visual effects unfold in synchronization with a song.**

If we can do that, AV Gen has crossed a very significant line.