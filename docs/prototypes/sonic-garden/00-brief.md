# Sonic Visual Interpretation System: the owner's brief (2026-09-30)

*A coordinator's header; the owner's words follow it, verbatim.*

- **Queued (the owner's instruction):** this work starts only AFTER the Procedural Space POC
  (`proto/procedural-space`) is complete.
- **Branch and worktree:** `proto/sonic-garden` in `../av-gen-sonic`, from main `a557d63b`. When the work starts,
  bring it up to date with main first. Main stays the owner's.
- **Staffing (the owner's instruction):**
  - The development work (the research, architecture, analyzers, Sonic Character, the Visual Interpreter plumbing,
    signal-bus integration, tests and performance) is done by a development agent at Opus 5.5 medium effort
    (agent type `sonic-engineer`).
  - The art work (the Visual Interpreter's artistic mappings, the visual families, the Sonic Garden scene's look,
    the three design tests of §34-36 judged visually, and creative evaluation) is done by an art agent at Opus 5.5
    max effort (agent type `sonic-art`).
  - The coordinator hands work between them.
- **The GPU:** all GPU work goes through `tools/gpu-lock.sh`, and the owner's own GPU use comes first.
- **ADRs:** the block **1020-1039** is assigned to `proto/sonic-garden`.
- **Notes:** progress is kept in `docs/prototypes/sonic-garden/PROGRESS.md`, current enough that a fresh agent can
  resume. Review media goes in `~/Desktop/av-gen-review/23-sonic-garden/`.

---

AV GEN — SONIC VISUAL INTERPRETATION SYSTEM

Research, Architecture, POC, Development & Art Direction Specification

⸻

1. PURPOSE

AV Gen needs to eventually support fully realized live sound visualization.

The goal is not to build another conventional audio visualizer based primarily on waveform amplitude, FFT bars, or generic particle reactions.

The goal is to create a generalized audiovisual system capable of interpreting:

* what is being played
* how it is being played
* what the sound actually sounds like
* the musical structure
* the sonic character of the instrument
* changes in timbre and articulation

and translating those properties into meaningful visual behavior.

The core concept is:

Sound → Sonic Character → Visual Interpretation

combined with:

MIDI → Musical Context → Visual Interpretation

The result should allow AV Gen to understand that two instruments playing the exact same MIDI sequence can produce radically different visual identities because their sounds are different.

For example:

A soft analog pad might produce:

* smooth geometry
* slow breathing motion
* diffuse light
* warm or muted colors
* organic curves
* long trails
* translucent materials

while the same MIDI played through an aggressive distorted wavetable synth might produce:

* jagged geometry
* high-frequency detail
* rapid deformation
* saturated illumination
* fragmentation
* sharp transitions
* energetic spatial movement

This should become a general AV Gen capability, not a narrow “Synth Visualizer” feature.

⸻

2. IMPORTANT IMPLEMENTATION PHILOSOPHY

Do NOT turn this into a giant architecture rewrite.

AV Gen already has an established pipeline:

Audio
→ Analysis
→ Signal Bus
→ Timeline
→ Modulator
→ Parameters
→ Generators
→ Scene
→ Renderer

The new system should fit into this architecture rather than replacing it.

The likely architecture is:

Audio
→ Audio Analysis
→ Sonic Character

MIDI
→ Musical Analysis
→ Musical Context

Sonic Character
+
Musical Context
→ Visual Interpreter
→ existing Signal Bus
→ existing Modulators
→ existing Parameters
→ existing Generators
→ Scene
→ Renderer

The Visual Interpreter is therefore primarily a semantic translation layer, not a replacement for the existing modulation system.

⸻

3. BACKWARDS COMPATIBILITY PHILOSOPHY

Do NOT build a large backwards-compatibility framework, migration system, compatibility shim layer, or alternate execution path simply for the sake of this feature.

Existing AV Gen projects should continue to work naturally if this subsystem is not used.

The desired behavior is simply:

* subsystem unused → nothing happens
* subsystem absent from a project → existing behavior continues
* subsystem enabled → new functionality becomes available
* subsystem produces signals → existing AV Gen signal/modulation architecture consumes them

Do not redesign existing project schemas unnecessarily.

Do not introduce compatibility abstractions unless actual implementation work demonstrates that one is necessary.

If an existing project such as Glowmere Valley 3 visually breaks as a result of the implementation, we can inspect it, fix the regression if it is small and justified, or revert/scrap the approach if necessary.

Do not spend excessive development time attempting to prove that every existing project is perfectly preserved before the feature has demonstrated value.

⸻

4. DEVELOPMENT PHILOSOPHY

This is an exploratory feature.

The first goal is to determine:

Does this actually make AV Gen more visually expressive?

Do not spend weeks measuring theoretical correctness before producing something visible.

The implementation should be research-informed, technically sound, and architecturally clean, but the first milestone should be a working visual experiment.

Prefer:

Research
→ small implementation
→ visible result
→ evaluate
→ iterate

over:

Research
→ exhaustive benchmarking
→ exhaustive abstraction
→ exhaustive testing
→ extensive compatibility framework
→ finally see a visual result

The developer/user can visually inspect the resulting scene.

Automated tests should cover important deterministic behavior and core algorithms, but do not create an enormous test suite simply to prove the concept before it has demonstrated artistic value.

⸻

5. RESEARCH REQUIREMENT

Before implementing the subsystem, perform a focused research pass.

Do not assume this specification is necessarily the optimal implementation.

The agent should independently research:

* real-time audio feature extraction
* perceptual timbre descriptors
* musical information retrieval
* MIDI analysis
* MIDI 2.0
* real-time audio visualization
* cross-modal sound/color relationships
* timbre visualization
* audiovisual mappings
* existing music visualization systems
* synthesis/timbre analysis
* methods for smoothing perceptual features
* methods for mapping high-dimensional sound descriptors to visual parameters

The agent should explicitly challenge the architecture proposed here.

If a better approach is discovered, document it before changing direction.

The research should focus on concepts that are useful for AV Gen rather than becoming an academic literature survey.

⸻

6. RESEARCH RESOURCES

Use these as starting points and then continue researching independently.

Essentia

Essentia is a major open-source audio analysis framework with a broad collection of descriptors covering time-domain, spectral, tonal, rhythm, and higher-level analysis.

Useful concepts include:

* loudness
* RMS
* spectral centroid
* spectral bandwidth
* spectral contrast
* spectral rolloff
* spectral flux
* spectral flatness
* harmonicity
* inharmonicity
* dissonance
* pitch
* chroma/HPCP
* key
* chords
* onset detection
* beat tracking
* rhythm
* segmentation

Research:

Essentia documentation:
https://essentia.upf.edu/documentation/

Essentia algorithm reference:
https://essentia.upf.edu/algorithms_reference.html

Essentia streaming extractor:
https://essentia.upf.edu/streaming_extractor_music.html

Librosa

Librosa provides a useful reference implementation and documentation for many common audio descriptors.

Research:

https://librosa.org/doc/latest/

Particularly investigate:

* spectral centroid
* spectral bandwidth
* spectral contrast
* spectral rolloff
* spectral flatness
* RMS
* zero-crossing rate
* chroma
* MFCCs
* spectral flux / onset-related features

Timbre Toolbox

Timbre Toolbox is useful research for perceptual characterization of musical timbre.

Repository:

https://github.com/AudioCommons/timbretoolbox

Research the underlying timbre descriptor concepts and determine which ones are useful for a real-time AV Gen implementation.

Web Audio AnalyserNode

Although AV Gen is not a Web Audio application, Web Audio provides a useful reference for real-time FFT/time-domain visualization architecture.

https://developer.mozilla.org/en-US/docs/Web/API/AnalyserNode

Also research the Web Audio visualization guide:

https://developer.mozilla.org/en-US/docs/Web/API/Web_Audio_API/Visualizations_with_Web_Audio_API

MIDI 2.0

Research MIDI 2.0 as a future-facing model for richer musical information.

MIDI Association:

https://midi.org/midi-2-0

MIDI 2.0 specifications:

https://midi.org/specifications

The system should remain compatible with MIDI 1.0 concepts while not designing itself in a way that prevents richer MIDI 2.0 information later.

Relevant future capabilities include:

* higher-resolution velocity
* per-note expression
* note attributes
* per-note control
* pitch expression
* richer articulation information

CLAP

CLAP is useful as an architectural reference for modern plugin event models.

https://github.com/free-audio/clap

In particular, investigate its event concepts:

* note events
* note IDs
* note expression
* parameter modulation
* per-note modulation
* transport events

Do NOT turn AV Gen into a CLAP host as part of this work.

Use CLAP only as a source of architectural ideas where useful.

Cross-modal sound/color research

Research how humans associate sound characteristics with visual properties.

One useful starting point is research into sound-color synesthesia and cross-modal correspondences.

Search/research:

https://pubmed.ncbi.nlm.nih.gov/

Relevant concepts include relationships between:

* pitch
* brightness
* loudness
* lightness
* timbral characteristics
* visual intensity

Do not assume there is one objectively correct mapping from sound to color.

The purpose of this research is to understand perceptual tendencies and then deliberately create an artistic mapping system.

⸻

7. CORE CONCEPT

The system should distinguish three fundamentally different kinds of information.

A. Audio Analysis

“What does the sound physically contain?”

Examples:

* RMS
* peak
* spectral centroid
* spectral bandwidth
* spectral rolloff
* spectral flatness
* spectral contrast
* spectral flux
* low/mid/high energy
* harmonicity
* inharmonicity
* pitch
* transient strength
* attack characteristics
* stereo width

These are measurements.

⸻

B. Sonic Character

“What does the sound feel like?”

This is a normalized perceptual abstraction layer.

Examples:

* warmth
* brightness
* sharpness
* softness
* roughness
* smoothness
* harmonicity
* inharmonicity
* density
* complexity
* stability
* movement
* organicness
* mechanicalness
* spatiality
* energy

These are not direct physical measurements.

They are derived interpretations.

For example:

spectral centroid
+
high-frequency energy
+
rolloff

might contribute to:

brightness

while:

spectral flux
+
transient strength
+
high-frequency energy

might contribute to:

sharpness

⸻

C. Musical Context

“What is the music doing?”

Examples:

* active notes
* note density
* polyphony
* pitch center
* pitch range
* pitch movement
* velocity
* rhythmic density
* rhythmic regularity
* chord structure
* arpeggio direction
* repetition
* phrase activity
* note duration
* articulation

This information should be independent from timbre.

The same MIDI performance should therefore be capable of producing different visual results depending on the sound source.

⸻

8. INITIAL ARCHITECTURE

Proposed subsystem:

                     ┌──────────────────┐
Audio ──────────────→ │  Audio Analyzer  │
                     └────────┬─────────┘
                              │
                              ▼
                     ┌──────────────────┐
                     │ Sonic Character  │
                     └────────┬─────────┘
                              │
                              │
                              ▼
                        ┌──────────────┐
                        │    Visual    │
                        │ Interpreter  │
                        └──────┬───────┘
                               │
                               ▼
MIDI ──────────────→ ┌──────────────────┐
                     │ Musical Analyzer │
                     └────────┬─────────┘
                              │
                              ▼
                       Musical Context
Sonic Character
       +
Musical Context
       │
       ▼
Visual Interpreter
       │
       ▼
Existing AV Gen Signal Bus
       │
       ▼
Existing Modulators
       │
       ▼
Existing Parameters
       │
       ▼
Existing Generators
       │
       ▼
Scene
       │
       ▼
Renderer

Do not prematurely turn each box into a large class hierarchy.

Start with the smallest clean implementation that preserves these conceptual boundaries.

⸻

9. AUDIO ANALYZER

Implement a real-time audio analysis layer capable of producing a compact feature snapshot.

Initial raw feature categories:

Energy

* RMS
* peak
* transient strength
* approximate dynamic range

Spectral

* spectral centroid
* spectral bandwidth
* spectral rolloff
* spectral flatness
* spectral contrast
* spectral flux
* low-frequency energy
* mid-frequency energy
* high-frequency energy

Tonal

* fundamental frequency estimate
* pitch confidence
* harmonicity
* inharmonicity

Temporal

* onset strength
* attack strength
* decay behavior
* sustain approximation
* release behavior
* onset rate

Spatial

* stereo width
* left/right energy
* channel correlation

Do not implement every possible descriptor immediately.

The agent should research which descriptors are actually useful for the first POC and prioritize accordingly.

⸻

10. SONIC CHARACTER MODEL

Create a compact normalized perceptual model.

Initial candidate dimensions:

energy
brightness
warmth
roughness
sharpness
smoothness
harmonicity
inharmonicity
density
complexity
stability
movement
organic
mechanical
spatial

Prefer normalized values approximately in:

0.0 → 1.0

or a similarly convenient representation.

The important property is consistency rather than the exact numeric range.

These should be derived from raw analysis features.

For example:

Brightness

Potential inputs:

* spectral centroid
* spectral rolloff
* high-frequency energy

Warmth

Potential inputs:

* low-frequency energy
* spectral centroid inverse
* spectral balance

Sharpness

Potential inputs:

* spectral flux
* transient strength
* high-frequency content
* attack characteristics

Roughness

Potential inputs:

* spectral complexity
* inharmonicity
* noisy energy
* rapid amplitude modulation

Harmonicity

Potential inputs:

* harmonic energy
* spectral contrast
* pitch confidence
* harmonic/noise ratio

Smoothness

Potential inputs:

* low spectral flux
* low transient activity
* stable spectral distribution

These formulas are starting points, not scientific truths.

They should be tunable.

⸻

11. TEMPORAL BEHAVIOR

Do not allow every descriptor to change instantly.

Different visual properties need different response times.

Separate signals into categories such as:

Fast

Respond quickly:

* transient
* onset
* peak
* impact
* note trigger

These can drive:

* flashes
* impacts
* particle bursts
* geometry strikes
* camera impulses

Medium

Respond over hundreds of milliseconds:

* energy
* brightness
* roughness
* spectral movement

These can drive:

* color
* deformation
* lighting
* material properties
* motion

Slow

Respond over seconds:

* warmth
* harmonicity
* density
* overall sonic identity

These can drive:

* environment
* world geometry
* dominant palette
* material family
* atmospheric behavior

Use smoothing, inertia, hysteresis, or equivalent mechanisms where appropriate.

Avoid visual flickering caused by noisy analysis values.

⸻

12. MUSICAL ANALYZER

Create a parallel musical analysis layer.

Initial features:

activeNotes
noteDensity
polyphony
pitchCenter
pitchRange
pitchMotion
velocity
rhythmicDensity
rhythmicRegularity
noteDuration
chordStructure
arpeggioDirection
repetition
phraseActivity

Where practical, derive additional information:

* ascending vs descending movement
* repeated notes
* chord changes
* sustained notes
* rapid note sequences
* rhythmic bursts
* sparse sections
* dense sections

The system should be capable of distinguishing:

slow sustained chord

from:

rapid arpeggio

even when their average audio energy is similar.

⸻

13. VISUAL INTERPRETER

The Visual Interpreter is the artistic heart of the system.

Its job is not merely:

RMS → scale

Instead it should combine:

Sonic Character
+
Musical Context
+
optional mappings
→
visual signals

Potential visual dimensions include:

* color
* hue
* saturation
* value/luminance
* geometry scale
* geometry deformation
* curvature
* sharpness
* particle density
* particle velocity
* emission
* roughness
* metallicity
* transparency
* spatial spread
* rotation
* growth
* pulsing
* turbulence
* trail length
* camera movement
* lighting intensity
* fog
* atmosphere

The interpreter should produce ordinary AV Gen signals wherever possible rather than inventing an entirely separate rendering mechanism.

⸻

14. ARTISTIC MAPPING LANGUAGE

Use mappings that communicate a visual metaphor.

Do not simply map every feature directly to a numeric property.

Examples:

Warm

Potential visual interpretation:

* amber
* copper
* deep magenta
* earthy tones
* soft emission
* organic forms

Bright

Potential interpretation:

* luminous surfaces
* higher visual value
* finer detail
* crystalline structures
* high-frequency geometry

Dark

Potential interpretation:

* deep saturated colors
* low illumination
* large negative spaces
* concentrated highlights

Soft

Potential interpretation:

* rounded curves
* diffusion
* translucency
* slow deformation
* flowing motion

Sharp

Potential interpretation:

* angular geometry
* hard edges
* spikes
* rapid directional movement

Rough

Potential interpretation:

* fragmentation
* noisy deformation
* irregular geometry
* granular detail

Smooth

Potential interpretation:

* flowing surfaces
* continuous motion
* rounded geometry

Harmonic

Potential interpretation:

* repetition
* symmetry
* ordered structures
* coherent patterns

Inharmonic

Potential interpretation:

* irregular crystalline structures
* asymmetry
* fractured patterns
* unexpected intervals

Dense

Potential interpretation:

* many overlapping forms
* layered geometry
* crowded spatial composition

Sparse

Potential interpretation:

* isolated objects
* large negative spaces
* minimal composition

Aggressive

Potential interpretation:

* rapid expansion
* fragmentation
* strong deformation
* sudden motion
* intense illumination

Do NOT automatically map “aggressive” to red.

Do not reduce the artistic system to common audio-visualizer clichés.

⸻

15. ART DIRECTION

The system should produce visuals that feel like an interpretation of the sound, not a visualization of an FFT.

A successful result should make a viewer think:

“That visual world belongs to this sound.”

rather than:

“Those particles are reacting to the bass.”

The visual language should therefore be:

* cinematic
* dimensional
* spatial
* material-aware
* atmospheric
* compositional
* expressive
* capable of subtlety

Avoid:

* generic spectrum bars
* endless particle explosions
* equalizer-style graphics
* random noise
* every frequency becoming a different colored object
* constant camera shaking
* everything pulsing on every beat

AV Gen should leverage its existing strengths:

* 3D environments
* terrain
* plants
* mushrooms
* crystals
* creatures
* materials
* lighting
* atmospheric effects
* spatial effects
* cameras
* sequencer
* entity effects
* procedural geometry

The sound should influence these systems.

⸻

16. VISUAL ART FAMILIES

Develop several broad visual families.

A. ORGANIC / LIVING

Suitable for:

* warm
* soft
* harmonic
* legato
* low-to-medium brightness
* sustained sounds

Visual vocabulary:

* tendrils
* vines
* petals
* mushrooms
* breathing forms
* fluid surfaces
* soft bioluminescence
* drifting spores
* slow growth
* organic deformation

The visual world should feel alive.

Glowmere Valley is an obvious reference for this family, but do not make every sound become Glowmere.

⸻

B. CRYSTALLINE / SYNTHETIC

Suitable for:

* bright
* harmonic
* precise
* arpeggiated
* high-frequency
* highly structured sounds

Visual vocabulary:

* crystals
* geometric lattices
* prisms
* glass-like materials
* repeating structures
* sharp reflections
* crystalline growth
* ordered patterns

A precise FM bell or crystalline synth should feel fundamentally different from a warm analog pad.

⸻

C. CHAOTIC / AGGRESSIVE

Suitable for:

* distorted
* rough
* inharmonic
* transient-heavy
* high-energy sounds

Visual vocabulary:

* fractured geometry
* jagged edges
* energetic deformation
* sparks
* shards
* turbulence
* violent expansion
* spatial distortion
* fragmented structures
* abrupt lighting changes

Avoid simply making everything red, noisy, and shaky.

Aggression can be expressed through:

* geometry
* timing
* spatial behavior
* material response
* density
* deformation
* camera
* light

⸻

17. THE CRITICAL POC

Build a dedicated experimental scene.

Do NOT begin by attempting to retrofit the entire system into Glowmere Valley 3.

Create a small environment specifically designed to demonstrate the concept.

Call it something like:

Sonic Garden

The POC should use the same musical material played through multiple contrasting sounds.

For example:

1. Soft analog pad
2. FM bell
3. Distorted wavetable bass
4. Noisy/percussive sound

Use the same MIDI sequence for all of them.

The critical test is:

Does the visual world change meaningfully when the sound changes while the musical performance remains the same?

If yes, the architecture is working.

⸻

18. POC VISUAL DESIGN

The POC should not require an elaborate world.

Build a visually clean environment containing several reusable elements.

For example:

* central hero form
* several organic structures
* several crystalline structures
* atmospheric particles
* ground plane
* distant background
* simple lighting
* camera

The interpreter can then activate or modulate these elements.

Example:

Pad

* organic forms become dominant
* slow breathing
* warm colors
* soft light
* long trails
* low geometric sharpness

FM Bell

* crystalline objects activate
* bright highlights
* precise pulses
* geometric repetition
* short ringing trails

Distorted Bass

* larger geometry
* fractured surfaces
* strong deformation
* dark/highly saturated materials
* rapid movement
* spatial distortion

Percussion

* impacts
* short-lived particles
* geometric strikes
* localized flashes
* sharp motion

⸻

19. MIDI + AUDIO MUST WORK TOGETHER

This is a critical design requirement.

The system should not treat MIDI and audio as competing sources.

They answer different questions.

MIDI says:

What musical event occurred?

Audio says:

What did that event actually sound like?

For example:

MIDI:
C3
velocity 0.8
duration 1.5 sec

Audio:

dark
warm
harmonic
soft
long release
low brightness

Visual interpretation:

large
slow
warm
organic
long-lasting
softly glowing form

Another instrument receives exactly the same MIDI:

C3
velocity 0.8
duration 1.5 sec

but audio is:

bright
inharmonic
sharp
metallic
short release

The visual result should be substantially different.

This is one of the most important demonstrations of the system.

⸻

20. MUSICAL ROLE MAPPING

Eventually, the system should understand that different musical roles can control different visual layers.

Potential mappings:

Kick
→ impact / physicality
Bass
→ scale / weight / terrain / large forms
Pad
→ environment / atmosphere
Lead
→ hero / focal object
Arpeggio
→ geometry / patterns
Percussion
→ particles / fragmentation
Vocal
→ organic / expressive elements

Do not hardcode these as universal rules.

Treat them as possible semantic mappings that a project or Director can use.

⸻

21. DIRECTOR INTEGRATION

Do not make the Sonic Visual Interpreter responsible for high-level artistic composition.

There should eventually be a hierarchy:

Analyzer

“What is happening in the sound?”

Sonic Character

“What does the sound feel like?”

Musical Context

“What is the music doing?”

Visual Interpreter

“How can these characteristics become visual behavior?”

Director

“What should the composition do with those behaviors?”

Creative Critic

“Does the resulting visual composition actually work?”

This separation will become increasingly valuable as AV Gen’s Director Agent and Creative Critic mature.

The Director might eventually reason:

“This section contains a dark, spacious pad with sparse high-register notes. Use a large environment with slow camera movement and minimal geometry.”

The Visual Interpreter then handles the continuous sound-driven behavior inside that composition.

⸻

22. REAL-TIME ARCHITECTURE

Audio analysis must not block rendering.

Preferred architecture:

Audio Thread
    ↓
Analysis
    ↓
Compact Sonic State
    ↓
Atomic / lock-free snapshot
    ↓
Render / simulation side reads snapshot

Do not perform expensive DSP work on the render thread.

The exact implementation should follow AV Gen’s existing threading architecture after investigation.

Analysis does not need to run at render-frame frequency.

A reasonable starting point is approximately:

20–60 analysis updates per second

with the actual rate determined experimentally.

Audio-rate events such as transients/onsets can be represented independently where necessary.

⸻

23. FEATURE UPDATE RATES

Consider separating:

Fast event signals

Examples:

* onset
* transient
* note-on
* note-off

Potential visual uses:

* impact
* burst
* flash
* attack
* particle emission

Continuous signals

Examples:

* brightness
* warmth
* roughness
* harmonicity
* energy

Potential uses:

* color
* material
* geometry
* atmosphere
* deformation

Slowly evolving identity

Examples:

* overall timbral character
* density
* harmonicity
* organic/mechanical character

Potential uses:

* visual family
* world palette
* environment
* dominant geometry

⸻

24. DIAGNOSTIC UI

Do not build a giant editor panel initially.

A compact diagnostic view is enough.

Something approximately like:

SONIC CHARACTER
Energy       ███████░░░  0.72
Brightness   █████░░░░░  0.51
Warmth       ████████░░  0.81
Roughness    ██░░░░░░░░  0.18
Sharpness    ███░░░░░░░  0.27
Smoothness   ████████░░  0.83
Harmonicity  █████████░  0.91
Density      █████░░░░░  0.49
Movement     ████░░░░░░  0.42
MUSICAL CONTEXT
Active Notes      4
Polyphony         4
Pitch Center      C3
Pitch Range       19 semitones
Note Density      0.61
Rhythmic Density  0.44

This is primarily for debugging and understanding the system.

Do not spend significant UI development time here initially.

⸻

25. SIGNAL BUS INTEGRATION

The preferred integration point is the existing AV Gen signal architecture.

The new system should expose signals that can be consumed like other modulation sources.

Potential signals:

sonic.energy
sonic.brightness
sonic.warmth
sonic.roughness
sonic.sharpness
sonic.smoothness
sonic.harmonicity
sonic.inharmonicity
sonic.density
sonic.complexity
sonic.stability
sonic.movement
sonic.spatial
music.noteDensity
music.polyphony
music.pitchCenter
music.pitchRange
music.pitchMotion
music.velocity
music.rhythmicDensity
music.rhythmicRegularity
music.arpeggioDirection

Potential event signals:

music.noteOn
music.noteOff
audio.transient
audio.onset

These names are suggestions.

Follow existing AV Gen naming conventions after inspecting the current codebase.

⸻

26. DO NOT OVER-HARDCODE THE MAPPINGS

Avoid architecture such as:

if brightness > 0.8:
    create crystals
if roughness > 0.7:
    create particles
if warmth > 0.6:
    use orange

That may be useful for a prototype, but it should not become the permanent architecture.

Instead, the system should eventually allow a project to map semantic dimensions to visual parameters.

Conceptually:

Sonic Character
      ↓
Mapping
      ↓
Visual Signal
      ↓
Existing AV Gen parameter

This keeps the analyzer independent from the artistic choices.

⸻

27. ARTISTIC DEFAULTS VS SCIENTIFIC TRUTH

The system must explicitly treat mappings as artistic interpretation.

There is no universally correct:

sound → color

or:

timbre → geometry

relationship.

Some perceptual correspondences may be supported by research, but AV Gen is ultimately an artistic system.

Therefore:

* research should inform defaults
* defaults should be tunable
* projects should be able to override mappings
* the system should not pretend that subjective mappings are objective measurements

⸻

28. FUTURE POSSIBILITIES

Do not implement these initially unless the POC naturally leads there.

Potential future features include:

Timbre fingerprints

Create a persistent visual identity for an instrument or sound source.

For example:

Synth A
→ crystalline blue-green visual identity
Synth B
→ warm organic amber identity
Synth C
→ fractured high-energy identity

This could remain consistent across an entire music video.

⸻

Instrument tracking

If audio/MIDI routing allows it, identify individual instruments or tracks.

Then:

Kick
Bass
Pad
Lead
Percussion
Vocal

can occupy different visual domains.

⸻

Learned mappings

Eventually investigate whether machine learning could help infer relationships between:

audio descriptors
+
MIDI
→
visual parameters

This is explicitly future work.

Do not introduce ML merely because it sounds interesting.

⸻

Sound-source classification

Potentially identify:

* bass
* pad
* lead
* percussion
* pluck
* strings
* vocal
* noise
* texture

Again, future work.

⸻

29. POC SUCCESS CRITERIA

The POC is successful if all of the following are demonstrated:

1. Same MIDI, different sound

The same musical sequence produces visibly different visual behavior when the instrument/timbre changes.

2. Musical events matter

Notes, rhythm, velocity, density, and pitch movement visibly affect behavior.

3. Timbre matters

Changing the sonic character changes the visual identity even when the musical content stays constant.

4. Continuous behavior matters

The visuals respond to evolving sonic character rather than only firing events.

5. The result feels intentional

The output should look like a visual interpretation rather than a generic reactive visualizer.

6. Existing AV Gen architecture remains useful

The new system should feed into existing signal/modulation/parameter mechanisms wherever practical.

7. Performance is reasonable

The system must not materially damage the interactive render loop.

Do not optimize prematurely.

Measure enough to identify obvious architectural problems, but prioritize getting a visually compelling result.

⸻

30. DEVELOPMENT PHASES

PHASE 0 — RESEARCH

Inspect:

* current AV Gen audio architecture
* current signal bus
* MIDI implementation
* modulation architecture
* threading model
* renderer update flow
* existing procedural generators
* existing effect system
* existing diagnostic UI patterns

Then research:

* Essentia
* Librosa
* Timbre Toolbox
* MIDI 2.0
* CLAP event architecture
* real-time visualization systems
* perceptual timbre research
* sound/color cross-modal research

Deliver:

* short research report
* recommended architecture
* list of proposed descriptors
* list of rejected/overkill descriptors
* any architectural concerns

Do not spend excessive time here.

⸻

PHASE 1 — AUDIO ANALYZER

Implement the smallest useful real-time analyzer.

Start with:

RMS
peak
spectral centroid
spectral bandwidth
spectral rolloff
spectral flatness
spectral flux
low/mid/high energy
pitch
harmonicity
inharmonicity
transient/onset strength
stereo width

Add additional descriptors only where they clearly improve the POC.

⸻

PHASE 2 — MUSICAL ANALYZER

Implement:

active notes
note density
polyphony
pitch center
pitch range
pitch motion
velocity
note duration
rhythmic density

Add:

chord structure
arpeggio direction
repetition
phrase activity

if practical.

⸻

PHASE 3 — SONIC CHARACTER

Create the perceptual abstraction layer.

Implement initial values:

energy
brightness
warmth
roughness
sharpness
smoothness
harmonicity
inharmonicity
density
complexity
stability
movement

Add:

organic
mechanical
spatial

if they prove useful.

Apply smoothing and temporal behavior.

⸻

PHASE 4 — VISUAL INTERPRETER

Build the mapping layer.

Do not tie it directly to specific Glowmere objects.

The interpreter should output generalized visual signals.

⸻

PHASE 5 — SONIC GARDEN POC

Create a dedicated small scene.

Use one MIDI sequence and multiple contrasting sounds.

Demonstrate:

* pad
* FM bell
* distorted bass
* percussion/noise

Create visually distinct responses.

This is the most important phase.

⸻

PHASE 6 — EXISTING AV GEN INTEGRATION

Once the POC demonstrates value:

Connect the system to the existing AV Gen signal/modulation/parameter pipeline.

Do not force the entire application to depend on the new subsystem.

It should remain optional.

⸻

PHASE 7 — LIVE INPUT

Once the architecture is proven:

Support live:

* microphone/audio input
* live MIDI
* live synth input
* potentially plugin/DAW routing

The end goal is a genuine live audiovisual instrument.

⸻

31. TESTING REQUIREMENTS

Keep testing proportional to the experimental nature of the work.

Write tests for:

* descriptor normalization
* smoothing
* feature conversion
* deterministic mapping functions
* MIDI event interpretation
* signal generation

Do not create hundreds of tests for every possible combination of descriptors.

Do not spend days building exhaustive performance benchmarks.

Do enough testing to prevent obvious implementation errors.

Then visually evaluate the POC.

⸻

32. PERFORMANCE REQUIREMENTS

Avoid:

* allocations in the audio callback
* blocking the audio thread
* blocking the render thread
* expensive DSP per rendered frame
* unnecessary synchronization
* copying large buffers between threads

Prefer:

* preallocated buffers
* compact feature state
* atomic snapshots
* ring buffers where appropriate
* moderate FFT sizes
* decoupled analysis rate
* smoothed feature values

Follow AV Gen’s existing performance architecture rather than inventing an unrelated threading system.

⸻

33. WHAT NOT TO BUILD YET

Do NOT initially build:

* machine-learning visual mapping
* giant visual mapping editor
* automatic genre recognition
* automatic instrument recognition
* complete MIDI 2.0 authoring UI
* CLAP hosting
* massive timbre database
* neural audio embeddings
* procedural world generation driven entirely by audio
* a new renderer
* a new modulation architecture
* a new project format
* a giant compatibility framework
* exhaustive benchmarking infrastructure

Those may become relevant later.

First prove that:

sound character + musical context → compelling visual behavior

⸻

34. IMPORTANT DESIGN TEST

The agent should explicitly perform this conceptual test during the POC:

Take one MIDI sequence.

Render it through:

A. warm analog pad
B. FM bell
C. distorted wavetable bass
D. noisy percussion

Then ask:

Can a viewer tell that the visuals belong to different sounds even though the musical sequence is identical?

If not, the visual interpretation layer needs more work.

This is more important than whether the architecture has a perfectly elegant API.

⸻

35. SECOND DESIGN TEST

Take one sound.

Play:

slow sustained notes

then:

rapid arpeggio

then:

dense chord sequence

The visual system should respond differently because the musical context changed even though the timbre remained the same.

This verifies that MIDI/musical context is not redundant with audio analysis.

⸻

36. THIRD DESIGN TEST

Take one sound and gradually modify its synthesis parameters:

For example:

clean
→ brighter
→ more resonant
→ more distorted
→ noisier

The visual character should evolve continuously rather than switching abruptly between unrelated visual presets.

This verifies that Sonic Character is actually functioning as a continuous semantic space.

⸻

37. ARTISTIC NORTH STAR

The ultimate goal is not:

“Make things move when music plays.”

AV Gen can already do that.

The goal is:

Make the visual world feel like an embodiment of the sound.

A viewer should eventually be able to watch a music video generated by AV Gen and intuitively feel that:

* the bass has weight
* the pad has atmosphere
* the lead has identity
* the percussion has physical impact
* the distortion has aggression
* the melody has movement
* the harmonic structure has visual order
* the timbre has color
* the space of the music has become a literal visual space

That is the larger vision.

⸻

38. FINAL IMPLEMENTATION GUIDANCE

Before coding:

1. Inspect the existing AV Gen architecture.
2. Identify the cleanest integration points.
3. Research the external systems listed above.
4. Challenge this specification where appropriate.
5. Produce a concise implementation plan.
6. Identify any architectural risks.
7. Proceed with the smallest useful implementation.

During implementation:

* favor existing AV Gen infrastructure
* avoid unnecessary abstractions
* avoid compatibility shims
* avoid renderer rewrites
* avoid premature optimization
* avoid excessive testing
* keep the subsystem optional
* keep audio analysis off the render thread
* keep musical semantics separate from timbral analysis
* keep perceptual interpretation separate from raw DSP
* keep visual interpretation separate from rendering

Most importantly:

Get to a visible Sonic Garden result quickly.

Once the POC is producing interesting visuals, iterate artistically.

The user will visually evaluate the result.

If the concept proves compelling, expand it into a first-class AV Gen capability.

If it does not, simplify, revise, or remove it rather than allowing the architecture to grow around an unproven idea.
