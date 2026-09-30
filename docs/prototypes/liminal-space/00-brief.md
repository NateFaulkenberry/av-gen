# Liminal Euclidean World: the owner's brief (2026-09-30)

*A coordinator's header; the owner's words follow it, verbatim. The lyrics the owner supplied at 18:20 are inserted
where the brief asked for them.*

- **Branch and worktree:** `proto/liminal-space` in `../av-gen-liminal`, from main `31a1e42e`. Main has the
  Procedural Space POC (`examples/space/`, ADR-1000 to 1005), the Sonic system (ADR-1020 to 1024) and live AA.
  Main stays the owner's.
- **The song:** `~/Desktop/All You Got.wav` (48 kHz, 24-bit stereo, 253.8 s). It is the owner's own song. NEVER
  commit it, cache it, or upload it; refer to it by path.
- **Deadline:** a complete, viewable, art-directed music-video render by the morning of 2026-10-01 (§22).
- **Staffing (the owner's instruction: a coding agent and an art agent, as in the first POC):**
  - **The art agent (`space-art`, Opus 5.5 max)** does the art research (§14's visual, audiovisual, colour and
    camera parts), the independent song analysis (§10), the director plan (§13), the art direction and
    implementation of the music-video scene, the test and full renders, the analyzer-driven revisions, and later
    the LIVE visual language.
  - **The engineering agent (`space-engineer`, Opus 5.5 medium)** does the technical research (§14's
    procedural-graphics part), the feasibility review of the existing POC (§1, §17), and the infrastructure the art
    needs:
    - continuous, trajectory-based deformation instead of state switching (§4);
    - procedural camera traversal through the SDF world (§3);
    - the liminal architectural vocabulary in the SDF system (§2);
    - a palette system with smooth interpolation (§7);
    - assets placed inside the SDF world (§8);
    - the quality-analyzer extensions (§15-16);
    - later, the LIVE mode's engineering (§18-19).
  - The coordinator hands work between them.
- **The GPU:** all GPU work goes through `tools/gpu-lock.sh`, including every `avgen_tests` run (docs/testing.md
  #29). Other agents share the GPU, and the owner's use comes first.
- **ADRs:** the block **1040-1059** is assigned to `proto/liminal-space`.
- **Review media:** `~/Desktop/av-gen-review/24-liminal-space/`.
- **Notes:** `docs/prototypes/liminal-space/PROGRESS.md`.

---

# AV Gen — Liminal Euclidean World

## Live MIDI/Audio-Reactive Scene + Music Video Director Pass

### Mission

Build out the existing Euclidean geometry visualization POC into a **live audiovisual environment** and a **fully directed music-video scene**.

The ultimate goal is a reusable AV Gen visual system that can operate in two modes:

1. **LIVE mode**

   * Reacts continuously to live audio and MIDI
   * Maintains autonomous visual motion even without musical events
   * Uses smooth, continuous modulation rather than frame-to-frame state switching
   * Can eventually be driven interactively by a musician/DJ/performance

2. **MUSIC VIDEO mode**

   * Uses a complete song as the source
   * Has deliberate visual direction, camera movement, scene evolution, color progression, and musical phrasing
   * Can be rendered offline and evaluated by AV Gen's creative/quality analyzer
   * Serves as the artistic reference implementation for the LIVE system

### Priority

**Build and art-direct the MUSIC VIDEO version first.**

Do not treat this as merely a technical demonstration of Euclidean geometry.

The music-video version should establish the visual language, motion vocabulary, color language, spatial behavior, and audiovisual relationships that the LIVE implementation will later reproduce procedurally.

---

# 1. Existing POC

Start from the existing Euclidean geometry POC and preserve what is already visually successful.

The existing POC has a stylized procedural look that should be retained and developed rather than replaced wholesale.

However, its current "infinite hall" aesthetic is not the desired final direction.

Do not discard the underlying technology merely because the current scene is not artistically finished.

First understand how the existing geometry generation/deformation system works and determine which pieces can support the new direction.

---

# 2. Core Art Direction

The desired environment is:

## Liminal + Escherian + Dreamlike

Think:

* empty rooms
* simple corridors
* stairways
* landings
* doorways
* platforms
* impossible architectural relationships
* repeating architectural motifs
* spaces that appear familiar but behave incorrectly
* rooms connected in impossible ways
* stairways that lead somewhere unexpected
* corridors that subtly fold or transform
* spaces that appear infinite without looking like a generic sci-fi tunnel

Use **simple architecture**.

Avoid:

* Doric columns
* classical Greek/Roman architecture
* ornate architecture
* gothic cathedral aesthetics
* generic futuristic corridors
* cyberpunk
* excessive sci-fi detailing
* spaceship interiors
* decorative architectural clutter

The environment should feel like a **strange dream of an ordinary building**.

The reference point is the spatial paradox and visual logic of M.C. Escher rather than literal reproduction of any particular artwork.

The goal is not to make "an Escher filter."

The goal is:

> A person is moving through a familiar architectural world whose geometry slowly stops obeying ordinary spatial rules.

---

# 3. The Viewer Must Feel Like They Are Going Somewhere

This is extremely important.

Do NOT make the scene fundamentally:

> static geometry + audio deformation.

The scene should instead communicate:

> **continuous movement through a never-ending impossible architectural world.**

There should be a strong sense of journey.

The camera/world should continuously:

* walk
* glide
* turn
* climb
* descend
* pass through rooms
* enter corridors
* pass through doorways
* transition between architectural spaces
* discover new spaces

The movement should continue even when the music is quiet.

Music should influence the journey rather than being solely responsible for it.

Investigate whether the existing procedural system can support:

* procedural camera paths
* continuous spatial traversal
* looping or recursive spaces
* seamless room transitions
* impossible spatial continuity
* smoothly evolving architecture

A viewer should ideally be able to watch the scene with the audio muted and still feel that a coherent visual journey is occurring.

---

# 4. Continuous Geometry Animation

The current POC appears to have a problem where audio-reactive geometry can become stuck between states and rapidly switch between configurations.

Avoid this architecture.

Do NOT build the system around:

```text
geometry A
→ geometry B
→ geometry C
→ geometry D
```

where audio determines which state is active.

Instead investigate:

```text
continuous autonomous animation
             +
smooth musical modulation
```

The environment should already have a trajectory through visual space.

Audio/MIDI should influence properties of that trajectory.

For example:

* deformation intensity
* deformation speed
* curvature
* scale
* spatial tension
* direction
* rate of transformation
* local distortion
* architectural "breathing"
* movement amplitude

Research and implement appropriate techniques for smooth procedural deformation.

Potential areas to investigate include:

* continuous deformation fields
* domain warping
* signed distance fields
* smooth interpolation
* damped/spring-based parameter response
* low-pass/envelope smoothing
* noise-driven motion
* flow fields
* curl noise
* procedural camera paths
* morphing implicit geometry
* continuous spatial transformations
* interpolation of transforms and orientations

Do not assume all of these are appropriate. Research them and select techniques that fit the existing renderer and procedural architecture.

The core requirement is:

> **The viewer should perceive one continuously evolving world, not a sequence of visual states.**

---

# 5. Audio + MIDI Visual Language

The audiovisual system should not simply map individual audio measurements directly to arbitrary parameters.

Develop a deliberate visual vocabulary.

Research how audiovisual artists commonly translate musical concepts into visual concepts, particularly:

* rhythm
* energy
* tension
* release
* timbre
* pitch
* dynamics
* density
* repetition
* anticipation
* silence
* buildup
* impact

Develop mappings that make artistic sense.

Potential examples:

### Kick

Could produce:

* spatial displacement
* traveling architectural wave
* camera impulse
* localized distortion
* subtle lighting pulse

### Bass

Could influence:

* large-scale architectural deformation
* spatial scale
* depth
* movement amplitude
* environmental pressure

### Aggressive/distorted synth

Could influence:

* high-frequency geometric vibration
* surface instability
* chromatic/color separation
* local distortion
* architectural trembling
* increased spatial tension

### Sustained synth

Could influence:

* continuous environmental deformation
* color evolution
* emissive surfaces
* slow architectural expansion/contraction

### Percussion

Could influence:

* small-scale detail
* localized flashes
* particles
* short spatial events

### Melody

Could influence:

* camera direction
* traversal path
* architectural forms
* movement direction
* evolving visual motifs

### Silence / breakdown

Should allow:

* visual breathing room
* reduced motion
* darkness
* simplified geometry
* camera stillness
* anticipation

These are examples, not mandatory mappings.

Use artistic judgment and testing to determine what works.

---

# 6. Audio Should Influence Behavior, Not Become the Behavior

Avoid:

```text
audio value → geometry position
```

Prefer:

```text
audio
→ musical feature
→ smoothed signal / event
→ visual behavior
→ continuously evolving parameter
```

For example:

```text
distorted synth
→ aggression increases
→ architectural vibration intensity increases
```

rather than:

```text
distorted synth amplitude
→ random vertex displacement
```

The visual system should feel intentional.

---

# 7. Color Direction

Color is a major part of the visual language.

Do not leave color as a secondary technical feature.

Develop a deliberate palette system that can evolve with the music.

Research visual color theory and audiovisual approaches to:

* tension
* warmth
* coolness
* energy
* tranquility
* release
* anticipation
* emotional progression

The scene should have coherent palette states and smooth transitions between them.

Avoid uncontrolled rainbow cycling.

Investigate concepts such as:

```text
base palette
→ musical mood
→ target palette
→ smooth interpolation
```

Color should be able to respond to:

* song section
* energy
* instrumentation
* musical tension
* MIDI events
* major transitions

The architecture, lighting, materials, fog, emissive elements, and post effects should participate in the palette.

---

# 8. Objects Inside the World

Investigate whether existing AV Gen assets can be placed inside the procedural architectural world.

For the initial implementation, use existing built-in/project assets rather than spending significant time creating new assets.

Potential uses:

* mushrooms
* plants
* rocks
* creatures
* floating objects
* architectural props
* existing stylized assets

The important thing is that objects should have a relationship with the space.

Investigate multiple deformation behaviors:

```text
architecture → strong deformation
environment props → moderate deformation
hero objects → relatively stable
particles/effects → highly reactive
```

This is only a starting hypothesis.

A stable object surrounded by a dramatically transforming environment may provide useful visual contrast and scale.

---

# 9. Music Video Mode

Create a complete directed music-video version using:

**`All You Got.wav`**

The WAV includes tempo information.

Import/use the tempo metadata where appropriate, but **do not rely solely on AV Gen's existing song-analysis tools**.

The agent must independently analyze the song using its own available tools.

Perform a genuine musical analysis before making the final director plan.

---

# 10. Independent Music Analysis

Before substantial scene construction:

1. Analyze the waveform/audio independently.
2. Determine tempo and rhythmic characteristics.
3. Identify major sections.
4. Identify transitions.
5. Identify energy changes.
6. Identify instrumentation/timbre changes.
7. Identify major drops/builds/releases.
8. Identify repeated motifs.
9. Identify important vocal phrases.
10. Determine where visual transitions should occur.
11. Identify opportunities for visual escalation and restraint.

Do not simply accept automatically generated AV Gen song-analysis labels.

Use independent analysis to form your own interpretation of the track.

The goal is to understand the song deeply enough to create a director plan.

---

# 11. Lyric Context

The following lyrics are provided primarily as **artistic context**.

Do NOT implement lyric display yet.

Eventually AV Gen's lyric system may be used to incorporate lyrics into the music video, but that is outside the scope of this pass.

Use the lyrics to understand:

* emotional meaning
* narrative progression
* themes
* tension
* reassurance
* uncertainty
* release
* growth
* letting go
* persistence
* introspection
* collective movement

Lyrics:

*The owner's lyrics, supplied 2026-09-30, verbatim:*

```text
How little do I know?
How little do I know, I know? 
How little do I know

Take steps in the process,
breathe and grow

How little do I know?
How little do I know, I know?
Can you tell me it’s fine though?

it’s steps in the process,
feel and grow

Come on
Tell me what you wanna,
Come on
Tell me what you wanna, gonna
maybe cause a little drama

if you feel it, say it,
let it show

can you tell me it’s fine though?
can you tell me we’ll be fine though?

I know
get a little peace of mind though

It’s steps in a process 
let it go

[let it go]

do you wanna have fun?
as the fires keep burning
and the world stops turning

and everyone under the sun
takes steps in the process to
heal and grow

tell me you’re the one
to make the world stop hurting
and my heart keep pumping

while everyone under the sun
take steps in the process to
feel it grow

how little do I know?
how little do I know?
how little do I know? ^
how little do I know?

steps in the process,
breathe and grow

how little do I know?
how little do I know? ^
tell me it’ll be fine though?
how little do I know?

steps in the pocess,
feel it grow

[feel it grow]

is that all you
is that all you
is that all you
is that all you
is that all you got?

x2



is that all?
is that all you got?
is that all?
is that all I show you?
is that all?
is that all you got?
is that all?
is that all?


[let it go]
just steps in a process
for your life
(make you feel right)
```

The repeated ideas include:

* "How little do I know?"
* "steps in the process"
* "breathe and grow"
* "feel and grow"
* "let it go"
* "do you wanna have fun?"
* "as the fires keep burning"
* "the world stops turning"
* "heal and grow"
* "feel it grow"
* "is that all you?"
* "is that all you got?"
* "for your life"
* "make you feel right"

Interpret these thematically rather than literally.

---

# 12. Song Structure — Initial Musical Context

The following user-provided structural description should be treated as a starting hypothesis and then validated against the actual audio:

### Cinematic Intro

Longer cinematic introduction.

The music gradually swells.

Visual direction:

* quiet
* mysterious
* spacious
* restrained
* establish the impossible world
* begin the journey
* introduce the architectural visual language
* slowly reveal that space is behaving incorrectly

Do not immediately overwhelm the viewer with effects.

### First Major Dance Section / Chorus

The song opens into a large dance-oriented section.

The repeated "all you got" material provides a strong opportunity for a visual motif.

Possible direction:

* environment opens dramatically
* camera movement becomes more confident
* spatial scale increases
* stronger color
* stronger architectural deformation
* synchronized large-scale movement

Do not make every beat produce an obvious visual hit.

Think in phrases.

### Verse

The song reduces somewhat but retains a strong four-quarter-note pulse.

Visual direction:

* pull back intensity
* maintain continuous movement
* establish recurring architectural motifs
* allow more detail to become visible
* use the pulse as underlying movement rather than constant impact

### Musical Pause / "Let It Go"

Drums and synths drop out.

Lyrics repeat "let it go."

This should be a major visual contrast.

Potential direction:

* dramatic reduction in motion
* architecture stabilizes
* camera may slow
* space becomes quiet
* color palette shifts
* geometry begins slowly rebuilding
* visual tension is created through anticipation

Do not fill the silence with effects.

Let the absence of music matter.

### Synth Build / Verse 2

The synths rebuild into the second verse.

Musically similar to verse 1 but lyrically denser and somewhat more disheveled.

Use this opportunity for the visual world to become:

* more complex
* less stable
* more layered
* slightly more distorted
* more unpredictable

But retain continuity.

### Bridge / "Feel It Grow"

Subtle tempo increase.

This is an opportunity for visual acceleration.

Possible techniques:

* camera movement accelerates
* architectural transformations become larger
* spaces become more interconnected
* color becomes more energized
* deformation propagates through the environment
* visual density increases

The repeated "feel it grow" concept can inform a literal visual growth/evolution motif.

### Second Bridge / "Is That All You?"

Repeated vocal phrase.

Create a strong visual tension before the next release.

Potential direction:

* increasingly compressed space
* repetitive architectural motif
* escalating distortion
* increasingly intense vibration
* visual questioning/repetition

Then release.

### Return to Four-on-the-Floor

Beat returns.

A quick synth/bass fill leads directly into the next chorus.

Treat the fill as a transitional visual event.

The world should rapidly reconfigure or reveal a new spatial destination rather than simply flash.

### Final Chorus / "Let It Go"

Repeated "let it go."

This should feel like release.

Potentially:

* largest spatial transformation yet
* major camera movement
* dramatic color opening
* architecture unfolding
* transition into a huge impossible space

### Double Chorus / Call and Response

"steps in a process / for your life / make it feel right"

This should be the culmination of the visual language established throughout the video.

The scene should feel like it has progressed somewhere rather than merely becoming more intense.

---

# 13. Director Plan

Before final rendering, create a written director plan containing:

* song timeline
* section boundaries
* musical characteristics
* visual intention
* camera behavior
* architectural behavior
* object behavior
* color palette
* lighting
* effects
* audio-reactive mappings
* major transitions
* recurring visual motifs
* climax/release strategy

The plan should explicitly explain the artistic reasoning behind major choices.

Do not simply produce a parameter table.

Think like a music-video director.

---

# 14. Research Phase — REQUIRED

Before implementation, conduct a deep research pass.

Research at minimum:

### Visual references

* M.C. Escher's impossible architecture
* liminal spaces
* surreal architecture
* impossible rooms
* dreamlike architectural cinematography
* spatial paradoxes
* non-Euclidean visual environments

### Audiovisual art

Research how artists translate:

* rhythm
* timbre
* dynamics
* musical energy
* tension/release
* sound texture

into visual movement.

### Procedural graphics

Research relevant techniques for:

* smooth spatial deformation
* domain warping
* procedural architecture
* SDF-based environments
* continuous camera traversal
* spatial morphing
* flow fields
* procedural animation
* smooth audio-reactive systems

### Color

Research:

* music-video color progression
* emotional color relationships
* palette transitions
* audiovisual color mapping
* cinematic color scripting

### Camera

Research:

* surreal architectural cinematography
* impossible camera movement
* continuous tracking shots
* spatial continuity
* visual transitions between architectural spaces

Do not blindly copy any reference.

The purpose of research is to identify useful artistic and technical principles.

Record useful sources and explain which ideas are being adopted.

---

# 15. Quality Analyzer Integration

Use the existing AV Gen quality/creative analyzer during development.

Do not wait until the end.

First determine what aspects of the analyzer are already useful.

Then evaluate whether it needs additional capabilities specifically for this project.

If necessary, extend the analyzer with project-specific evaluation categories.

Recommended categories:

## Spatial Coherence

* Does the environment read as one continuous world?
* Are transitions spatially understandable?
* Do impossible structures look intentional?

## Liminality

* Does the world feel empty, strange, familiar, and uncanny?
* Does it avoid becoming generic sci-fi?
* Is there sufficient negative space?

## Escherian Spatial Logic

* Are impossible spatial relationships visually legible?
* Are rooms, corridors, stairs, and transitions doing meaningful work?
* Does the geometry create spatial paradox rather than random distortion?

## Camera / Journey

* Does the viewer feel like they are traveling?
* Is camera motion intentional?
* Are transitions smooth?
* Does the camera reveal the environment effectively?

## Musical Synchronization

* Does visual activity correspond meaningfully with musical structure?
* Are major transitions reflected visually?
* Are audio-reactive effects smooth?
* Is the visual response phrased musically rather than merely beat-synced?

## Visual Hierarchy

* Is there a clear focal point?
* Is everything moving at once?
* Are stable elements providing contrast?
* Does visual complexity overwhelm the scene?

## Color

* Is the palette coherent?
* Does it evolve meaningfully?
* Are transitions smooth?
* Does color support musical/emotional progression?

## Artistic Cohesion

* Does the scene feel like one intentional visual work?
* Does the visual language remain consistent?
* Do geometry, lighting, camera, objects, color, and effects feel like parts of the same concept?

## Technical Quality

Continue evaluating normal rendering issues as well:

* aliasing
* artifacts
* geometry failures
* clipping
* temporal instability
* lighting problems
* excessive noise
* broken transitions
* camera collisions
* object intersections

---

# 16. Analyzer Should Evaluate Temporal Behavior

Where technically possible, provide the analyzer with:

* rendered video
* audio
* MIDI
* scene data
* relevant AOVs
* timeline/section information

Do not limit evaluation to individual frames.

The analyzer should be able to identify problems such as:

> "The synth becomes substantially more aggressive here, but the visual system does not respond."

or:

> "The architectural deformation responds to the transient but snaps between states rather than evolving continuously."

or:

> "Color changes are occurring faster than the musical phrase warrants."

or:

> "The visual intensity remains high throughout the breakdown and therefore does not create sufficient contrast for the following section."

If extending the analyzer is practical, prioritize this temporal audiovisual analysis.

---

# 17. Iterative Workflow

Use this workflow:

```text
RESEARCH
   ↓
INDEPENDENT SONG ANALYSIS
   ↓
ART / DIRECTOR PLAN
   ↓
TECHNICAL FEASIBILITY REVIEW
   ↓
IMPLEMENT MUSIC VIDEO
   ↓
SHORT TEST RENDER
   ↓
QUALITY / CREATIVE ANALYSIS
   ↓
ART DIRECTION REVISION
   ↓
FULL MUSIC VIDEO RENDER
   ↓
QUALITY / CREATIVE ANALYSIS
   ↓
FINAL MUSIC VIDEO PASS
   ↓
EXTRACT LIVE VISUAL LANGUAGE
   ↓
IMPLEMENT LIVE MODE
   ↓
LIVE TEST
```

Do not skip the research or director-planning stages.

---

# 18. LIVE Mode Architecture

After the music-video version is sufficiently successful, extract its visual behaviors into reusable live systems.

LIVE mode must support:

* microphone/audio input
* MIDI input
* existing AV Gen signal infrastructure
* continuous autonomous world animation
* smooth audio modulation
* MIDI events
* configurable mappings
* adjustable response smoothing
* no dependence on precomputed song structure

The live version should still feel like the same artistic world.

It should not become a separate generic audio visualizer.

---

# 19. LIVE vs MUSIC VIDEO

Design the system so that the two modes share visual behaviors.

For example:

```text
Visual Behavior
    ├── autonomous motion
    ├── spatial deformation
    ├── color evolution
    ├── architectural transformation
    ├── camera movement
    ├── object behavior
    └── effects

Music Video
    └── timeline/director controls these behaviors

LIVE
    └── audio/MIDI controls these behaviors
```

This is preferable to maintaining two completely separate implementations.

---

# 20. Avoid These Failure Modes

Do NOT allow the project to devolve into:

* generic audio visualizer
* random geometry generator
* beat-synced flashing
* constant camera shaking
* random color cycling
* excessive bloom
* rainbow effects
* generic sci-fi corridors
* static tunnel
* geometry that merely jitters
* every object reacting equally
* every beat causing a visible event
* visual complexity without hierarchy
* technically impressive but emotionally meaningless deformation

The desired result should feel **directed**.

---

# 21. Definition of Success

The music-video render is successful if:

1. It feels like a coherent visual world.
2. It feels like a journey through an impossible architectural space.
3. The liminal/Escherian influence is apparent without becoming a literal Escher imitation.
4. The camera/world continuously moves.
5. Geometry evolves smoothly rather than switching between states.
6. Color evolves with the music.
7. Audio-reactive behavior has distinct visual character.
8. MIDI/audio relationships feel intentional.
9. Objects can inhabit the world without looking pasted on.
10. Quiet musical sections create visual breathing room.
11. Major musical transitions create meaningful visual transitions.
12. The climax feels earned by the preceding visual progression.
13. The quality analyzer finds no major technical or artistic breakdowns.
14. The resulting visual behaviors can be extracted into a compelling LIVE mode.

---

# 22. Overnight Execution

Work autonomously wherever possible.

Do not stop for minor implementation decisions.

Make reasonable engineering/art-direction decisions and document them.

Only stop for genuinely blocking choices that require human input.

The immediate objective is:

> **Produce a complete, viewable music-video render of `All You Got.wav` by the morning, with enough art direction and polish that it can be meaningfully evaluated rather than merely serving as a technical test.**

After the music-video render and analyzer pass, continue toward the LIVE implementation if time and architecture permit.

At completion, provide:

1. What was researched
2. What artistic direction was selected
3. Song structure/analysis
4. Director plan
5. What was implemented
6. What was added/changed in the quality analyzer
7. Music-video render location
8. LIVE mode status
9. Known limitations
10. Recommended next steps

Do not declare the project successful merely because it renders.

The final judgment should come from inspecting the actual rendered result and the analyzer's findings.
