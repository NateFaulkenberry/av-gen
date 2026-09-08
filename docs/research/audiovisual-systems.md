# Audiovisual Systems: Architecture & Workflow Survey

**Purpose.** Research input for a native C++ real-time audiovisual engine whose pipeline is
`audio -> analysis -> control signals -> modulation -> scene parameters -> GPU rendering`
(an "instrument for making visuals", not "audio -> waveform -> animation").

**Scope.** Architecture and workflows only (not UI) of: Notch, TouchDesigner, Unreal Engine,
Unity, Resolume Arena/Wire, Processing/p5 (Sound/Minim), Hydra, Max/MSP Jitter, vvvv gamma,
Magic Music Visuals, Synesthesia (SSF), Milkdrop/projectM, Cables.gl, Godot; plus
game-engine architecture references (EnTT, flecs, UPROPERTY, Godot properties, O3DE reflection),
Blender drivers/F-curve modifiers, Ableton macro mapping, Bitwig/Vital/SynthLab modulation matrices,
the OSC spec, and the One Euro filter.

**Date of research:** 2026-09-08. All URLs were accessed on that date.

**Citation convention.** Every section ends with a **Sources** block. Each entry carries the
mandatory fields: *Source* (name), *URL*, *Accessed* (always 2026-09-08), *Learned*, *Relevance*,
*Confidence/limitations*. Inline claims reference entries as `[TD-1]`, `[NOTCH-3]`, etc.
Where an official page could not be fetched (HTTP 403/404) and the claim rests on a search-engine
snippet or on prior knowledge, the confidence field says so explicitly.

---

## Table of contents

1. Cross-system comparison matrix
2. TouchDesigner
3. Notch
4. Unreal Engine
5. Unity
6. Resolume Arena / Wire
7. Processing / p5 (Sound, Minim)
8. Hydra
9. Max/MSP Jitter and vvvv gamma
10. Magic Music Visuals
11. Synesthesia (SSF)
12. Milkdrop / projectM
13. Cables.gl
14. Godot
15. Game-engine architecture references (ECS, reflection/property systems)
16. Blender drivers, F-curves and F-curve modifiers
17. DAW-style modulation: Ableton macros, Bitwig modulators, synth modulation matrices
18. Live-control protocols: OSC (and MIDI/DMX conventions)
19. Smoothing conventions across systems (attack/release, lag, One Euro)
20. Design lessons for our engine
21. Proposed conceptual data model (parameters, signals, modulation, scenes, presets, timeline)
22. Open questions / things not verified
23. Sources (consolidated)

---

## 1. Cross-system comparison matrix

| System | Scene model | Parameter model | Audio -> parameter path | Smoothing primitive | Timeline vs graph | Offline export | Project format | Live control |
|---|---|---|---|---|---|---|---|---|
| TouchDesigner | Operator network (TOP/CHOP/SOP/MAT/DAT/COMP) | Every op has typed pars; CHOP channels export to pars by name | Audio Device In -> Audio Spectrum / audioAnalysis -> Lag/Filter/Math -> Export | Lag (up/down), Filter (Gaussian, One Euro), Trigger ADSR | Both: root/component timelines + Animation COMP; graph is primary | Movie File Out TOP, non-realtime flag | `.toe` (binary, `toeexpand` to ASCII), `.tox` components | OSC/MIDI/DMX CHOPs |
| Notch | Node hierarchy under Root, layers, compositions | Node properties, keyframable, "Modifiers" input per property | Sound / Sound FFT modifiers with attack/decay/smoothing/scale/offset | Attack/Decay/Smoothing on modifier; Smooth Value Modifier | Both: timeline with keyframes + curve editor; nodegraph is primary | Export Video (H.264/HAP/NotchLC/seq), motion blur, AA passes, preroll | `.dfx` project; `.dfxdll` Block (engine + project) | Exposed params via Blocks; MIDI/OSC modifiers |
| Unreal | Actors/components; Niagara stacks | UPROPERTY reflection + meta (ClampMin/Max); Niagara namespaces | Audio Synesthesia (NRT + real-time), submix spectral delegates -> Blueprint -> Niagara User params / MPC | Blueprint-side (no built-in envelope on param) | Sequencer timeline; Niagara/material graphs | Movie Render Queue (temporal/spatial samples, EXR) | `.uasset`/`.umap` binary | OSC plugin, DMX plugin (not verified here) |
| Unity | GameObjects/components; VFX Graph systems | Serialized fields; VFX Blackboard exposed props; Shader Graph props | AudioSource spectrum (not verified here) -> C# -> `VisualEffect.SetFloat` | User C# | Timeline (asset/instance, Playables) | Unity Recorder (editor only) | YAML/binary assets | third-party |
| Resolume | Composition > layers > clips, effects | Every parameter animatable; modes Timeline/BPM Sync/Audio | FFT (External/Composition/Clip/Layer/Group), band Low/Mid/High + in/out, Gain, Fall | "Fall" (release) on audio parameter | Clip timelines + BPM; Wire graph | Record output (not verified) | `.avc` XML (undocumented) | OSC with fixed address paths, MIDI |
| Synesthesia | Single-pass/multipass GLSL scene + JS | `scene.json` CONTROLS (type/min/max/default) -> uniforms | Built-in analysis -> `syn_*` uniforms | Built into `syn_*` (smoothed levels, hits, presence) | Neither; per-frame `update(dt)` | Record video (not verified) | `.synScene` dir: json + glsl + js | MIDI/OSC mapping to controls (not verified) |
| Milkdrop | Preset = equations + warp/comp shaders | INI-like `.milk` keys; `q1..q32` bridge | `bass/mid/treb`, `_att` damped versions | `_att` time-damping | Per-frame/per-vertex equations | none (renderer-level) | `.milk` text | none |
| Cables.gl | Op graph with trigger flow + value ports | Ports typed (trigger/number/string/bool/array/object/texture) | AudioAnalyzer -> FFT array -> FFTAreaAverage | user ops | Graph, MainLoop trigger | export standalone HTML | JSON patch | MIDI/OSC ops |
| Godot | Scene tree of nodes | `@export` hints, shader uniform hints | AudioEffectSpectrumAnalyzer on a bus -> script -> `set_shader_parameter` | user script | AnimationPlayer (not covered) | Movie Maker mode (not verified) | `.tscn` text | — |

---

## 2. TouchDesigner

### 2.1 Scene architecture
TouchDesigner is a procedural operator network with six operator families: TOPs (textures),
CHOPs (channels), SOPs (geometry), MATs (materials), DATs (tables/text) and COMPs (components,
containers, 3D objects) [TD-1]. The **CHOP** is the control-signal abstraction: a CHOP holds
named channels, each a sequence of one or more float samples; every CHOP has a sample rate and
a start/end interval shared by all its channels; data updates ("cooks") each frame, typically 60 fps
[TD-1]. Samples are 32-bit float, computed at 64-bit [TD-1].

### 2.2 Parameter system and binding channels to parameters
Two mechanisms connect CHOP channels to operator parameters [TD-1][TD-2]:
1. **Export flag** — a channel is dragged onto a parameter; a "Channel Name is Path:Parameter"
   export method mass-binds channels named like `geo1:tx` to `/geo1`'s `tx` parameter [TD-2].
2. **Expression reference** — parameters contain Python expressions such as
   `op('filter1')['chan2']` or `op('null1')[0] * 2 - 1` [TD-1][TD-2].
Since 2017 the two perform equivalently; export excels at mass-assignment by naming convention,
expressions at inline math [TD-2]. *Design implication:* names double as routing addresses.

### 2.3 Time slicing (frame-drop safety)
A **Time Slice** is "the time from the last cook frame to the current cook frame"; time-sliced
CHOPs only compute the samples generated since the last frame, so when frames drop, samples are
processed in batches of 1, 2 or more rather than lost — preventing audio pops and keeping
animation accurate [TD-3]. Audio I/O CHOPs are always time-sliced; Filter and Noise optionally;
Constant never [TD-3]. *Design implication:* control-signal processing should be sample-accurate
across a variable-length frame delta, not evaluated once per frame with a fixed dt.

### 2.4 Audio analysis exposed
- **Audio Spectrum CHOP**: FFT sizes 64..16384 (power of two); `frequencylog` toggles log vs linear
  bins; `highfreqboost` amplifies highs; outputs `_m` (magnitude) and `_p` (phase) channels; the
  doc recommends 2048 as a balance of CPU and resolution that tolerates dropped frames [TD-4].
- **Palette `audioAnalysis`** component: outputs Low/Mid/High levels, Kick, Snare, Rhythm, Spectral
  Centroid, and slow/fast moving partial density (Smp/Fmp); per-band Threshold/Smooth/Gain/Add
  parameters; analyses can be individually disabled to save compute [TD-5]. (The dedicated
  "Audio Analysis CHOP" page returned 404; the palette component is the documented path.)
- **Envelope CHOP**: amplitude envelope via exponential decay or local-maximum window; suggested
  use is normalizing loudness by dividing audio by its wide-window envelope [TD-11].
- **Beat CHOP**: tap tempo / BPM -> ramp, pulse, sine, count, count+ramp, bar/beat/sixteenths,
  BPM channels; periods 1/4..32 beats or custom; "Multiples" produce staggered channels; play modes:
  timeline-locked, global beat source, or continuous local [TD-9].

### 2.5 Control-signal processing chain (the CHOP toolbox we should copy)
- **Lag CHOP**: separate lag for rising and falling values (lag ~ time to reach 90% of a change),
  overshoot up/down, clamp slope (velocity) and clamp acceleration [TD-6].
- **Filter CHOP**: Gaussian, Left-Half Gaussian (causal), Box, Left-Half Box, Edge Detect, Sharpen,
  De-spike, Ramp Preserve, **One Euro**; width in samples/frames/seconds; Effect 0..1; passes [TD-7].
- **Math CHOP**: combine ops (add/sub/mul/div/avg/min/max/length), pre/post unary ops, **From Range
  -> To Range** linear remap, integer rounding, channel match by name/index [TD-8].
- **Trigger CHOP**: threshold crossing -> Delay/Attack/Peak/Decay/Sustain/Release envelope, per-segment
  shapes (linear/ease-in/out), re-trigger delay, min trigger length, multi-trigger add/restart [TD-10].

### 2.6 Shader system
**GLSL TOP** supports vertex/pixel and compute modes; uniforms: vectors, colors (with color-space
conversion), CHOP-fed arrays as uniform arrays or texture buffers, matrices, atomic counters,
specialization constants; up to 3 sampler2D inputs with extend modes; multiple render targets
("# of Color Buffers"); compile errors go to an Info DAT; compilation can be threaded with a
placeholder (checkerboard/black/previous) shown until ready [TD-12]. *Design implication:* feeding
whole channel arrays into shaders as texture buffers is the scalable path for spectrum data.

### 2.7 Timeline and keyframes
There is a root timeline (`Timepath = /`) and every component may have its own; rate is
`project.cookRate`; start/end plus a working range; loop/once [TD-13]. The **Animation COMP** wraps a
Keyframe CHOP plus Table DATs; keys store time, value, slope and interpolation; playback modes:
locked to timeline, custom index input (random access), sequential with speed, or whole range as a
lookup curve; output is a CHOP that can be exported to parameters [TD-14]. *Design implication:*
keyframe animation is just another channel source; timeline evaluation is `index -> value`.

### 2.8 Offline rendering / export
**Movie File Out TOP** encodes H.264/H.265/AV1 (license-gated), Hap, NotchLC, ProRes, VP8/9, GIF,
Animation, Motion JPEG, Cineform; image sequences as TIFF/OpenEXR (EXR can pack channels from
multiple TOPs); frame-drop-free recording is achieved by turning off the global **Realtime** flag
(non-realtime cooking); audio needs a time-sliced CHOP input and video frames are repeated to keep
A/V sync if the cook rate lags [TD-15].

### 2.9 Serialization, presets, live control
`.toe` contains networks, operators, parameters, pane layouts; it is natively binary-ish but
`toeexpand`/`toecollapse` convert to/from ASCII; `.tox` saves a component for reuse [TD-16].
MIDI channel naming encodes device semantics in the channel name (`ch1n45` = note 45 on MIDI
channel 1, `c7` = controller 7); OSC In supports pattern include/exclude of channel names; DMX Out
maps channels to addresses with 0-255 values [TD-17].

### Sources (TouchDesigner)
- **[TD-1]** *Source:* Derivative docs, "CHOP". *URL:* https://docs.derivative.ca/CHOP. *Accessed:* 2026-09-08.
  *Learned:* channels/samples/sample-rate model, cooking, export vs expression referencing, 32/64-bit floats.
  *Relevance:* the canonical "control signal as typed channel" model. *Confidence:* high (official doc, fetched).
- **[TD-2]** *Source:* Derivative docs, "Export". *URL:* https://docs.derivative.ca/Export. *Accessed:* 2026-09-08.
  *Learned:* `path:parname` mass-export naming, export table, parity with expressions. *Relevance:* naming as routing. *Confidence:* high.
- **[TD-3]** *Source:* Derivative docs, "Time Slicing". *URL:* https://docs.derivative.ca/Time_Slicing. *Accessed:* 2026-09-08.
  *Learned:* time-slice definition, batch processing on frame drops, which CHOPs slice. *Relevance:* frame-rate-independent signal processing. *Confidence:* high.
- **[TD-4]** *Source:* Derivative docs, "Audio Spectrum CHOP". *URL:* https://docs.derivative.ca/Audio_Spectrum_CHOP. *Accessed:* 2026-09-08.
  *Learned:* fftsize range, log frequency, high-freq boost, `_m/_p` channels, 2048 recommendation. *Relevance:* FFT defaults. *Confidence:* high.
- **[TD-5]** *Source:* Derivative docs, "Palette:audioAnalysis". *URL:* https://docs.derivative.ca/Palette:audioAnalysis. *Accessed:* 2026-09-08.
  *Learned:* output feature set and per-band threshold/smooth/gain/add. *Relevance:* minimum viable feature vector. *Confidence:* high (official; `Audio_Analysis_CHOP` URL was 404).
- **[TD-6]** *Source:* Derivative docs, "Lag CHOP". *URL:* https://docs.derivative.ca/Lag_CHOP. *Accessed:* 2026-09-08.
  *Learned:* asymmetric lag, overshoot, slope/accel clamps. *Relevance:* attack/release smoothing primitive. *Confidence:* high.
- **[TD-7]** *Source:* Derivative docs, "Filter CHOP". *URL:* https://docs.derivative.ca/Filter_CHOP. *Accessed:* 2026-09-08.
  *Learned:* filter kernel list incl. One Euro and causal half-kernels. *Relevance:* processor node catalogue. *Confidence:* high.
- **[TD-8]** *Source:* Derivative docs, "Math CHOP". *URL:* https://docs.derivative.ca/Math_CHOP. *Accessed:* 2026-09-08.
  *Learned:* range remap stage, pre/post ops. *Relevance:* mapping stage design. *Confidence:* high.
- **[TD-9]** *Source:* Derivative docs, "Beat CHOP". *URL:* https://docs.derivative.ca/Beat_CHOP. *Accessed:* 2026-09-08.
  *Learned:* beat-locked ramp/pulse/sine/count outputs and subdivisions. *Relevance:* beat/bar phase signals. *Confidence:* high (Ableton Link not mentioned on this page).
- **[TD-10]** *Source:* Derivative docs, "Trigger CHOP". *URL:* https://docs.derivative.ca/Trigger_CHOP. *Accessed:* 2026-09-08.
  *Learned:* 6-stage envelope, shapes, retrigger rules. *Relevance:* impulse -> envelope processor. *Confidence:* high.
- **[TD-11]** *Source:* Derivative docs, "Envelope CHOP". *URL:* https://docs.derivative.ca/Envelope_CHOP. *Accessed:* 2026-09-08.
  *Learned:* envelope methods; normalization trick. *Relevance:* level following / AGC. *Confidence:* high.
- **[TD-12]** *Source:* Derivative docs, "GLSL TOP". *URL:* https://docs.derivative.ca/GLSL_TOP. *Accessed:* 2026-09-08.
  *Learned:* uniform kinds incl. CHOP arrays as texture buffers, MRT, threaded compile. *Relevance:* shader parameter binding. *Confidence:* high.
- **[TD-13]** *Source:* Derivative docs, "Timeline". *URL:* https://docs.derivative.ca/Timeline. *Accessed:* 2026-09-08.
  *Learned:* root/component timelines, cookRate, ranges. *Relevance:* hierarchical time. *Confidence:* high.
- **[TD-14]** *Source:* Derivative docs, "Animation COMP". *URL:* https://docs.derivative.ca/Animation_COMP. *Accessed:* 2026-09-08.
  *Learned:* keyframes as tables + Keyframe CHOP, index-driven playback. *Relevance:* timeline as channel source. *Confidence:* high.
- **[TD-15]** *Source:* Derivative docs, "Movie File Out TOP". *URL:* https://docs.derivative.ca/Movie_File_Out_TOP. *Accessed:* 2026-09-08.
  *Learned:* codecs, EXR sequences, non-realtime flag, A/V sync rule. *Relevance:* offline render design. *Confidence:* high.
- **[TD-16]** *Source:* Derivative docs, ".toe". *URL:* https://docs.derivative.ca/.toe. *Accessed:* 2026-09-08.
  *Learned:* contents, toeexpand/toecollapse, .tox. *Relevance:* project serialization. *Confidence:* medium-high.
- **[TD-17]** *Source:* Derivative docs, "MIDI In/Out CHOP", "OSC In CHOP", "DMX Out CHOP" (via search snippets; URLs https://docs.derivative.ca/MIDI_Out_CHOP, https://docs.derivative.ca/OSC_In_CHOP, https://docs.derivative.ca/DMX_Out_CHOP). *Accessed:* 2026-09-08.
  *Learned:* channel-name encoding of MIDI events, OSC name patterns, DMX ranges. *Relevance:* live-control naming. *Confidence:* medium (snippets, pages not fully fetched).

---

## 3. Notch

### 3.1 Scene architecture
Notch is a hierarchical node graph: a **Root node** anchors each composition; child nodes hang
below it and execution/render order follows the parent -> child hierarchy; **Render Layer** nodes
allow selective rendering and **Composition Precomp** nodes nest node structures [NOTCH-1][NOTCH-2].
Node categories include 3D objects, particles (emitters/affectors/renderers), fields (volumetric
emitters/affectors), procedurals, cloning (array/grid/radial) and modifiers [NOTCH-1].

### 3.2 Parameter system: properties with a "Modifiers" input
Every node property is keyframable in the Timeline (with a curve editor exposed inline) and can
receive a **Modifier** through the property's Modifiers input; modifiers are value processors that
replace static values with computed ones [NOTCH-2][NOTCH-3]. Modifier outputs combine with the
target via an explicit **operation**: Add, Subtract, Multiply or Replace (documented on Math and
Beat Pulse modifiers) [NOTCH-5][NOTCH-6]. Modifiers also carry a **Blend amount** [NOTCH-5].

### 3.3 The modifier catalogue (Notch's modulation vocabulary)
The 1.0/2026.1 reference lists, among others: Accumulator, Bake Modifier, BPM Modifier, Combiner,
Condition Modifier, Continuous Modifier, Curve Remap, Delay Value, Expression, Extractor, Gradient
Remap, Limiter, Math Modifier, MIDI Modifier, MIDI Note Modifier, OSC Modifier, Quantise Modifier,
Range Remap, Record Modifier, Smooth Value Modifier, Sound FFT Modifier, Sound FFT Region Modifier,
Sound Modifier, Triggerable Value Modifier, Value, Video Sampler Modifier, plus interactive
(Clock Time, Keyboard, Mouse Picker) and motion modifiers [NOTCH-4]. *Design implication:* this is
effectively a taxonomy of processor nodes: sources (Value, Math, BPM, Sound), transforms (Range
Remap, Curve Remap, Quantise, Limiter, Smooth, Delay, Accumulator), logic (Condition, Combiner,
Expression) and I/O (MIDI, OSC, Record/Bake).

### 3.4 Audio modifiers and their smoothing conventions
- **Sound Modifier**: uses frequency content and level of incoming audio to produce a float usable
  in any numerical property; tuned with **Frequency Range Min/Max, Attack, Decay, Smoothing, Scale,
  Offset** [NOTCH-3][NOTCH-7].
- **Sound FFT Modifier / Sound FFT Region Modifier**: band-targeted FFT analysis with
  attack/decay/smoothing/scale/offset [NOTCH-8].
- **Math Modifier**: sine, cosine, triangle, sawtooth, square, inverted square, Perlin, interpolated
  and random noise; "speed 1 = one cycle per second (1 Hz)"; scale, offset, absolute value; **Time
  Mode**: locked to timecode (deterministic) vs running/looping [NOTCH-5].
- **Beat Pulse Modifier**: BPM, Num Beats, Num Beats Offset, Attack, Decay, Pulse Sharpness, Scale,
  Time Offset, Time Mode (timecode-locked vs running) [NOTCH-6].
*Design implication:* Notch chooses **Attack/Decay + Smoothing** as the three-knob smoothing model
and makes **timecode-locked vs free-running** an explicit per-modulator choice — the key to
deterministic offline renders.

### 3.5 Exposed parameters, Blocks, Playback
A parameter is exposed via `[RFX] Expose To Block/Standalone` in the property editor; exposure
creates a pathway for external apps to override that property in the exported Block or
standalone app [NOTCH-9]. A **Notch Block** (`.dfxdll`) is a self-contained file containing the
Notch engine as a DLL plus the project and assets; media servers (disguise, Hippotizer, Resolume,
Ai, Screenberry, 7thSense) control exposed properties and select layers; Blocks respond to timecode
[NOTCH-10][NOTCH-11]. The `.dfx` is the editable Notch Builder project [NOTCH-11].

### 3.6 Offline export
Export Video supports H.264, HAP, HAPq, NotchLC, MOV/MP4 and image sequences; **motion blur** by
overlapping multiple sub-frames with an adjustable amount; **antialiasing passes** by re-rendering
with sub-pixel offsets; refinement passes for raytracing; tiled rendering to reduce VRAM;
**preroll** to warm up history-dependent effects; alpha toggle; audio export with frame offset;
render length defaults to composition length [NOTCH-12][NOTCH-13].

### Sources (Notch)
- **[NOTCH-1]** *Source:* Notch Manual 1.0, "Nodes" overview. *URL:* https://manual.notch.one/1.0/en/topic/nodes. *Accessed:* 2026-09-08.
  *Learned:* node categories, Root, layers, modifiers, exposed properties. *Relevance:* scene model. *Confidence:* medium-high (fetched; summary-level page).
- **[NOTCH-2]** *Source:* Notch Manual 1.0, "Nodegraph". *URL:* https://manual.notch.one/1.0/en/docs/reference/user-interface/nodegraph/. *Accessed:* 2026-09-08.
  *Learned:* hierarchy/render order, Render Layer and Precomp, modifiers on properties, keyframable properties. *Relevance:* graph semantics. *Confidence:* medium (fetch returned structure-level detail).
- **[NOTCH-3]** *Source:* Notch Manual 1.0, "Sound Modifier". *URL:* https://manual.notch.one/1.0/en/docs/reference/nodes/modifiers/sound-modifier/. *Accessed:* 2026-09-08.
  *Learned:* freq range, attack, decay, smoothing, scale, offset; Modifiers input. *Relevance:* audio->param conventions. *Confidence:* high.
- **[NOTCH-4]** *Source:* Notch Manual 1.0, "Modifiers" index. *URL:* https://manual.notch.one/1.0/en/docs/reference/nodes/modifiers/. *Accessed:* 2026-09-08.
  *Learned:* the full modifier list. *Relevance:* processor-node taxonomy. *Confidence:* high.
- **[NOTCH-5]** *Source:* Notch Manual 0.9.23, "Math Modifier". *URL:* https://manual.notch.one/0.9.23/en/docs/nodes/modifiers/math-modifier/. *Accessed:* 2026-09-08.
  *Learned:* waveforms, 1 Hz convention, blend, add/sub/mul/replace, time modes. *Relevance:* LFO source + combine op. *Confidence:* high (older manual version).
- **[NOTCH-6]** *Source:* Notch Manual 0.9.22, "Beat Pulse Modifier". *URL:* http://manual.notch.one/0.9.22/en/topic/nodes-modifiers-beat-pulse-modifier. *Accessed:* 2026-09-08.
  *Learned:* BPM envelope pulse parameters, time modes, ops. *Relevance:* beat-phase envelope. *Confidence:* high (older version).
- **[NOTCH-7]** *Source:* Notch Manual 2026.1, "Working With Audio". *URL:* https://manual.notch.one/2026.1/en/docs/learning/working-with-audio/. *Accessed:* 2026-09-08.
  *Learned:* Sound Loader vs Sound Capture, modifier chain. *Relevance:* file vs live inputs. *Confidence:* medium (page partly truncated in fetch).
- **[NOTCH-8]** *Source:* Notch Manual 1.0, "Sound FFT Modifier". *URL:* https://manual.notch.one/1.0/en/docs/reference/nodes/modifiers/sound-fft-modifier/. *Accessed:* 2026-09-08.
  *Learned:* FFT band modifier attributes. *Relevance:* band mapping. *Confidence:* medium (fetch summarized generically).
- **[NOTCH-9]** *Source:* Notch Manual 2026.1, "Exposed Parameters". *URL:* https://manual.notch.one/2026.1/en/docs/workflows/working-with-media-servers/exposed-parameters/. *Accessed:* 2026-09-08.
  *Learned:* expose mechanism, Expose To Block node, Exposable nodes. *Relevance:* external control surface. *Confidence:* medium-high.
- **[NOTCH-10]** *Source:* Notch Manual 2026.1, "Notch Blocks". *URL:* https://manual.notch.one/2026.1/en/docs/workflows/working-with-media-servers/blocks/. *Accessed:* 2026-09-08.
  *Learned:* Block = engine + project, media servers, layer selection, timecode. *Relevance:* playback runtime packaging. *Confidence:* medium-high.
- **[NOTCH-11]** *Source:* Notch "Notch Blocks" feature page + Smode doc (search snippets). *URLs:* https://www.notch.one/features/notch-blocks ; https://www.smode.io/doc/ref/compo/integrations/notch-block-.dfxdll-file.htm. *Accessed:* 2026-09-08.
  *Learned:* `.dfx` = editable project, `.dfxdll` = engine DLL + project + assets. *Relevance:* format split. *Confidence:* medium (snippets).
- **[NOTCH-12]** *Source:* Notch Manual 0.9.23, "Export Video". *URL:* https://manual.notch.one/0.9.23/en/docs/user-interface/exporting-video/. *Accessed:* 2026-09-08.
  *Learned:* codecs, motion blur, AA passes, tiles, preroll. *Relevance:* offline render features. *Confidence:* high (older version).
- **[NOTCH-13]** *Source:* Notch Manual 2026.1, "Rendering Basics"/"Timeline" (search snippets). *URLs:* https://manual.notch.one/2026.1/en/docs/learning/rendering/rendering-basics/ ; https://manual.notch.one/2026.1/en/docs/reference/user-interface/timeline/. *Accessed:* 2026-09-08.
  *Learned:* keyframe curve editor inline in timeline; render length = composition length. *Relevance:* timeline. *Confidence:* medium.

---

## 4. Unreal Engine

### 4.1 Niagara (particles) — stack + namespaces
Niagara Systems contain Emitters; Emitters contain Modules (HLSL-authored graphs) that "stack
together"; execution proceeds through System, Emitter, Particle groups, each with Spawn and Update
stages plus Events and Simulation Stages, then Render [UE-1]. Data is namespaced: **User** (set only
from outside — Blueprint/C++), **Engine**, **System**, **Emitter**, **Particles**; modules can write
only their own group's namespace and read the ones above them [UE-1][UE-2]. *Design implication:*
scoped, read-only-from-below parameter namespaces are a clean way to define what external control
can touch (User.*) vs internal simulation state.

### 4.2 Reflection / property system (UPROPERTY)
`UPROPERTY([specifiers], meta(...))` drives serialization, editor UI, GC and networking; visibility
specifiers (EditAnywhere/EditDefaultsOnly/VisibleAnywhere), Blueprint access, `Category="A|B"`,
`AdvancedDisplay`; meta keys **ClampMin/ClampMax** (hard clamp), **UIMin/UIMax** (slider range),
**EditCondition**, **DisplayName** [UE-3]. Material Parameter Collections are global shader
parameter sets settable from Blueprint/Sequencer (page fetch returned only a TOC; details unverified)
[UE-4].

### 4.3 Sequencer (timeline)
Level Sequence assets hold tracks (property, transform, event, material parameter, Niagara),
keyframes inside sections, subsequences/shots for non-linear editing, and **bindings**:
possessables (existing actors) vs spawnables (spawned for the sequence's lifetime) [UE-5].

### 4.4 Movie Render Queue (offline)
Spatial samples re-render the same instant with camera jitter (no time advance); temporal samples
slice the shutter interval into sub-frames using engine motion blur; **Render Warm Up** and
**Engine Warm Up** counts (recommended 120 each) let temporal history and simulations settle;
outputs PNG/EXR(16-bit)/JPG/BMP + WAV; per-job console variables apply only during the render;
High Resolution tiling exceeds GPU limits [UE-6]. *Design implication:* offline mode = deterministic
stepping with explicit warm-up and sub-frame accumulation.

### 4.5 Audio analysis
**Audio Synesthesia** exposes analyzers: LoudnessNRT (perceptual loudness; AnalysisPeriod,
Min/MaxFrequency), ConstantQNRT (bands spaced like piano keys; StartingFrequency, NumBands,
NumBandsPerOctave), OnsetNRT (onset timestamps + strength; GranularityInSeconds, Sensitivity);
NRT = non-real-time, computed in editor and stored as `.uasset`, read at runtime from Blueprint
[UE-7]. Submixes support real-time envelope following and spectral analysis via
`AddSpectralAnalysisDelegate` / `StopSpectralAnalysis`, with a settings struct for band count and
min frequency [UE-8]. (Real-time Synesthesia analyzer variants exist in newer versions per prior
knowledge, but were not verified in the fetched pages.)

### Sources (Unreal)
- **[UE-1]** *Source:* Epic docs, "Key Concepts in Niagara Effects". *URL:* https://dev.epicgames.com/documentation/en-us/unreal-engine/key-concepts-in-niagara-effects-for-unreal-engine. *Accessed:* 2026-09-08.
  *Learned:* stack groups/stages, namespace read/write table. *Relevance:* scoped parameter namespaces. *Confidence:* high.
- **[UE-2]** *Source:* ibbles, "Niagara user exposed parameters" notes (search snippet). *URL:* https://github.com/ibbles/LearningUnrealEngine/blob/master/Niagara%20user%20exposed%20parameters.md. *Accessed:* 2026-09-08.
  *Learned:* User params are set only from outside the simulation. *Relevance:* external-control boundary. *Confidence:* medium (community notes).
- **[UE-3]** *Source:* Epic docs, "UProperties". *URL:* https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-uproperties. *Accessed:* 2026-09-08.
  *Learned:* specifier/meta vocabulary incl. ClampMin/Max, UIMin/UIMax, EditCondition. *Relevance:* parameter metadata design. *Confidence:* high.
- **[UE-4]** *Source:* Epic docs, "Using Material Parameter Collections". *URL:* https://dev.epicgames.com/documentation/en-us/unreal-engine/using-material-parameter-collections-in-unreal-engine. *Accessed:* 2026-09-08.
  *Learned:* only page existence/TOC; details not retrievable. *Relevance:* global shader params. *Confidence:* low (fetch returned TOC only; per prior knowledge a collection holds up to 1024 scalars + 1024 vectors — unverified).
- **[UE-5]** *Source:* Epic docs, "Cinematics and Movie Making". *URL:* https://dev.epicgames.com/documentation/en-us/unreal-engine/cinematics-and-movie-making-in-unreal-engine. *Accessed:* 2026-09-08.
  *Learned:* Sequencer asset/track/binding model. *Relevance:* timeline design. *Confidence:* medium-high.
- **[UE-6]** *Source:* Epic docs, "Rendering High Quality Frames with Movie Render Queue". *URL:* https://dev.epicgames.com/documentation/unreal-engine/rendering-high-quality-frames-with-movie-render-queue-in-unreal-engine?lang=en-US. *Accessed:* 2026-09-08.
  *Learned:* spatial vs temporal samples, warm-up, formats, CVars, tiles. *Relevance:* offline renderer. *Confidence:* high.
- **[UE-7]** *Source:* Epic docs, "Audio Synesthesia". *URL:* https://dev.epicgames.com/documentation/en-us/unreal-engine/audio-synesthesia-in-unreal-engine. *Accessed:* 2026-09-08.
  *Learned:* Loudness/ConstantQ/Onset NRT analyzers and settings. *Relevance:* analysis feature set; pre-baked analysis. *Confidence:* high for NRT; real-time variants unverified.
- **[UE-8]** *Source:* Epic docs, "Add Spectral Analysis Delegate" / "Overview of submixes" (search snippets). *URLs:* https://dev.epicgames.com/documentation/en-us/unreal-engine/BlueprintAPI/Audio/Spectrum/AddSpectralAnalysisDelegate ; https://dev.epicgames.com/documentation/unreal-engine/overview-of-submixes-in-unreal-engine. *Accessed:* 2026-09-08.
  *Learned:* real-time FFT/envelope on submixes via delegates. *Relevance:* real-time bus analysis. *Confidence:* medium.

---

## 5. Unity

### 5.1 VFX Graph
Two orthogonal flows: vertical **processing logic** (Systems -> Contexts: Spawn, Initialize, Update,
Output) built from stackable **Blocks** ("every Block is in charge of one operation"), and horizontal
**property logic** built from **Operators** [UN-1]. Properties (bool/int/float/vectors/textures/
AnimationCurve/Gradient plus compound types like Sphere) live on the **Blackboard**; marking one
**Exposed** surfaces it on the VisualEffect component, where per-instance overrides use a checkbox
that reverts to the graph default when cleared [UN-2]. From C#, `VisualEffect.SetFloat(name|id, v)`
sets an exposed property and flips its overridden state; IDs from `Shader.PropertyToID` or the
`ExposedProperty` helper avoid string lookups [UN-3].

### 5.2 Timeline and Playables
A **Timeline asset** stores tracks/clips/recorded animation without scene references; a **Timeline
instance** (PlayableDirector) binds the asset's tracks to scene GameObjects; the same asset can be
instanced many times and edits propagate to all instances [UN-4]. Timeline is built on the
**Playables API** (PlayableGraph of playable nodes with outputs, blending weights, custom
PlayableBehaviours) [UN-5]. *Design implication:* separate "sequence data" from "bindings to a
scene" so timelines are reusable across scenes.

### 5.3 Shader Graph properties
Types: Float (Default/Slider/Integer modes), Vector2/3/4, Color (Default/HDR), Boolean, Texture2D/
2DArray/3D, Cubemap, Virtual Texture, Matrix2/3/4; settings: Name, Reference (auto `_` prefix),
Precision, **Scope** (Global = script-only, Per Material, Hybrid Per Instance), Show In Inspector
[UN-6].

### 5.4 Recorder and Cinemachine
Unity Recorder records Movie, Image Sequence, Animation Clip and Audio (WAV), can accumulate
sub-frames for motion blur/path-tracing convergence, writes AOVs to multi-part EXR, can be triggered
from Timeline, and works only in the Editor's Play mode [UN-7]. Cinemachine keeps a priority-sorted
queue of virtual cameras; the Brain picks the highest priority (most recently activated on ties)
and blends between cameras over a time/curve, with custom per-pair blends [UN-8].

### Sources (Unity)
- **[UN-1]** *Source:* Unity VFX Graph 17.0 manual, "Graph Logic and Philosophy". *URL:* https://docs.unity3d.com/Packages/com.unity.visualeffectgraph@17.0/manual/GraphLogicAndPhilosophy.html. *Accessed:* 2026-09-08.
  *Learned:* Systems/Contexts/Blocks/Operators. *Relevance:* particle pipeline structure. *Confidence:* high.
- **[UN-2]** *Source:* Unity VFX Graph 17.2 manual, "Properties" and "Blackboard". *URLs:* https://docs.unity3d.com/Packages/com.unity.visualeffectgraph@17.2/manual/Properties.html ; https://docs.unity3d.com/Packages/com.unity.visualeffectgraph@17.2/manual/Blackboard.html. *Accessed:* 2026-09-08.
  *Learned:* property types, Exposed, per-instance override checkbox. *Relevance:* graph parameters. *Confidence:* high (Range/Tooltip attributes not in fetched text).
- **[UN-3]** *Source:* Unity Scripting API, `VFX.VisualEffect.SetFloat` (search snippet). *URL:* https://docs.unity3d.com/ScriptReference/VFX.VisualEffect.SetFloat.html. *Accessed:* 2026-09-08.
  *Learned:* SetFloat semantics, PropertyToID caching. *Relevance:* external binding API. *Confidence:* medium-high.
- **[UN-4]** *Source:* Unity Timeline 1.8 manual, "Timeline assets and instances". *URL:* https://docs.unity3d.com/Packages/com.unity.timeline@1.8/manual/tl-overview.html. *Accessed:* 2026-09-08.
  *Learned:* asset vs instance vs bindings. *Relevance:* reusable timelines. *Confidence:* high.
- **[UN-5]** *Source:* Unity Manual, "Playables API". *URL:* https://docs.unity3d.com/Manual/Playables.html. *Accessed:* 2026-09-08.
  *Learned:* PlayableGraph model, Director relation. *Relevance:* evaluation graph. *Confidence:* medium.
- **[UN-6]** *Source:* Unity Shader Graph 17.0 manual, "Property Types". *URL:* https://docs.unity3d.com/Packages/com.unity.shadergraph@17.0/manual/Property-Types.html. *Accessed:* 2026-09-08.
  *Learned:* types and Scope options. *Relevance:* shader parameter metadata. *Confidence:* high.
- **[UN-7]** *Source:* Unity Recorder 5.1 manual. *URL:* https://docs.unity3d.com/Packages/com.unity.recorder@5.1/manual/index.html. *Accessed:* 2026-09-08.
  *Learned:* recorder types, accumulation, AOV EXR, editor-only. *Relevance:* export. *Confidence:* high.
- **[UN-8]** *Source:* Unity Cinemachine docs (search snippets). *URLs:* https://docs.unity3d.com/Packages/com.unity.cinemachine@3.1/manual/concept-camera-control-transitions.html ; https://docs.unity3d.com/Packages/com.unity.cinemachine@2.3/manual/CinemachineBlending.html. *Accessed:* 2026-09-08.
  *Learned:* priority queue + blends. *Relevance:* camera as a prioritized, blendable state source. *Confidence:* medium.

---

## 6. Resolume Arena / Wire

### 6.1 Scene & parameter animation
Composition > layers > clips with effects; "you can animate all the parameters" [RES-1]. Parameter
animation modes: **Timeline** (play/speed/duration/in-out/loop/ping-pong/random), **BPM Sync** (same
but in beats), **Audio Analysis** with FFT source **External / Composition / Clip / Layer / Group**;
frequency selection Low/Middle/High plus finer in/out points; **Gain** and **Fall** sliders shape
intensity and recovery speed; **Envelopes** shape any animation type; **Parameter Presets** save
animations; **Start settings** decide when animation begins (composition load, clip trigger, column
trigger) [RES-1]. *Design implication:* "Fall" is a release-time control and "Gain" a pre-scale —
the minimal two-knob audio mapping.

### 6.2 Wire (node graph) and FFT
Wire's **Spectrum In** node yields 1024 floats; at 48 kHz each bin is ~23 Hz (24000/1024); a patch
with Spectrum In shows a source dropdown (local / composition / external) when used as an effect,
source or mixer in Arena [RES-2]. Wire supports **ISF** shaders (float with MIN/MAX, bool, color,
event inputs) but no vertex shaders as of 7.22 [RES-3].

### 6.3 Live control and serialization
Every item is addressable by a fixed OSC path, e.g. `/composition/layers/1/video/opacity`,
`/composition/layers/2/clips/8/transport/position`; values are normalized 0..1 with per-parameter
meaning (scale 0..1 = 0..1000%, rotation 0..1 = -180..+180); float/int/color/string tags;
absolute vs "selected layer" relative addresses; OSC output feedback with wildcards; ZeroConf
discovery [RES-4]. Compositions are `.avc` XML with `<Composition>`, `<versionInfo>`,
`<CompositionInfo>`, deck/layer/clip elements; Resolume states the format is undocumented and may
change [RES-5].

### 6.4 ISF (Interactive Shader Format) — the shader-parameter contract Resolume shares
ISF is "a [slightly modified] GLSL fragment shader with a JSON blob at the beginning"; the JSON
declares `ISFVSN`, `DESCRIPTION`, `CATEGORIES`, an **INPUTS** array (each with `NAME`, `TYPE` in
float/long/bool/event/point2D/color/image/audio/audioFFT, optional `DEFAULT`, `MIN`, `MAX`, `LABEL`,
`VALUES`/`LABELS` for long popups), **PASSES** (`TARGET`, `PERSISTENT`, `FLOAT`, `WIDTH`/`HEIGHT`
expressions in `$WIDTH`/`$HEIGHT`), and **IMPORTED** images; standard uniforms `TIME`, `TIMEDELTA`,
`RENDERSIZE`, `FRAMEINDEX`, `PASSINDEX`, `DATE`, `isf_FragNormCoord`; `audio` inputs arrive as an
image holding the waveform centered at 0.5 and `audioFFT` as an image of FFT results; filters
require an `inputImage`, transitions `startImage`/`endImage`/`progress` [ISF-1].

- **[ISF-1]** *Source:* ISF Specification (mrRay/ISF_Spec). *URL:* https://github.com/mrRay/ISF_Spec. *Accessed:* 2026-09-08.
  *Learned:* JSON header schema for inputs/passes/imports, standard uniforms, audio-as-image convention. *Relevance:* a proven, portable JSON contract for shader parameters and multipass/persistent buffers. *Confidence:* high (primary spec).

### Sources (Resolume)
- **[RES-1]** *Source:* Resolume support, "Parameter Animation". *URL:* https://resolume.com/support/en/parameter-animation. *Accessed:* 2026-09-08.
  *Learned:* animation modes, FFT sources, Gain/Fall, envelopes, presets, start settings. *Relevance:* per-parameter animation modes and audio mapping knobs. *Confidence:* high.
- **[RES-2]** *Source:* Resolume support, "Wire FFT". *URL:* https://resolume.com/support/en/wire-fft. *Accessed:* 2026-09-08.
  *Learned:* 1024-bin spectrum, bin width, source selection. *Relevance:* spectrum data contract. *Confidence:* high.
- **[RES-3]** *Source:* Resolume support, "ISF". *URL:* https://resolume.com/support/en/isf. *Accessed:* 2026-09-08.
  *Learned:* supported ISF input types, no vertex shaders. *Relevance:* shader format interop. *Confidence:* high.
- **[RES-4]** *Source:* Resolume support, "OSC". *URL:* https://resolume.com/support/en/osc. *Accessed:* 2026-09-08.
  *Learned:* fixed hierarchical address scheme, normalized values, feedback. *Relevance:* parameter-path addressing. *Confidence:* high.
- **[RES-5]** *Source:* Resolume forum "preset file xml format spec" + Resolume-Composition-Converter README (search snippets). *URLs:* https://resolume.com/forum/viewtopic.php?t=12760 ; https://github.com/tijnisfijn/Resolume-Composition-Converter. *Accessed:* 2026-09-08.
  *Learned:* `.avc` is XML; undocumented. *Relevance:* serialization precedent. *Confidence:* medium.

---

## 7. Processing / p5 (Sound library, Minim)

- The **Sound library FFT** takes a power-of-two band count (default 512); `analyze()` fills
  `spectrum[]`; for a full-scale sine the bin reads 1.0 (0 dB) but real signals "usually don't exceed
  0.05"; `frequency = binIndex * sampleRate / (2*numBands)` [PR-1]. *Design implication:* raw FFT
  magnitudes need normalization/AGC before they are useful as 0..1 control signals.
- **BeatDetector** flags energy spikes ("may or may not coincide exactly with a musical beat"),
  does not compute BPM, and has `sensitivity(ms)` = minimum time between detections [PR-2].
- **Minim BeatDetect** has SOUND_ENERGY (whole buffer, `isOnset`) and FREQ_ENERGY (FFT bands via
  `logAverages`, `isKick/isSnare/isHat/isRange(low,high,threshold)`); a beat is "a brutal variation
  in sound energy" relative to ~1 s of energy history; `setSensitivity(ms)` [PR-3].
- Sketch model: `setup()` once, `draw()` per frame (standard Processing reference; not fetched — 403).

### Sources (Processing)
- **[PR-1]** *Source:* processing-sound Javadoc, `FFT`. *URL:* https://processing.github.io/processing-sound/processing/sound/FFT.html. *Accessed:* 2026-09-08.
  *Learned:* bands, scaling, bin frequency formula. *Relevance:* normalization needs. *Confidence:* high.
- **[PR-2]** *Source:* processing-sound Javadoc, `BeatDetector`. *URL:* https://processing.github.io/processing-sound/processing/sound/BeatDetector.html. *Accessed:* 2026-09-08.
  *Learned:* energy-spike onset, sensitivity in ms. *Relevance:* onset primitive. *Confidence:* high.
- **[PR-3]** *Source:* Minim docs, `BeatDetect`. *URL:* https://code.compartmental.net/minim/beatdetect_class_beatdetect.html. *Accessed:* 2026-09-08.
  *Learned:* two modes, band-specific detectors, energy-history algorithm. *Relevance:* onset per band. *Confidence:* high.

---

## 8. Hydra

Hydra is a functional, chainable shader DSL: sources (`osc`, `noise`, `shape`, `gradient`,
`voronoi`, `src`) piped through transforms/color/blend/modulate functions to outputs `o0..o3`
[HY-1]. Audio (via Meyda) exposes `a.fft[]` (low index = low frequency), `a.setBins(n)`,
`a.setSmooth(0..1)`, `a.setScale()` (maps to 0..1), `a.setCutoff()` (noise floor), `a.show()`;
audio must be passed as an arrow function so it is re-evaluated each frame:
`osc(10, 0, () => a.fft[0]*4).out()` [HY-2]. Parameters also accept arrays as sequences with
`.fast()`, `.smooth()`, `.ease()` modifiers [HY-1]. *Design implication:* "a parameter value is a
function of time evaluated per frame" is the same idea as a modulation route; Hydra's
bins/smooth/scale/cutoff quartet is the smallest sensible audio pre-processing API.

### Sources (Hydra)
- **[HY-1]** *Source:* Hydra API reference. *URL:* https://hydra.ojack.xyz/api/. *Accessed:* 2026-09-08.
  *Learned:* function families, outputs, arrays/functions as params. *Relevance:* functional composition of shader stages. *Confidence:* medium-high.
- **[HY-2]** *Source:* Hydra docs, "Audio". *URL:* https://hydra.ojack.xyz/docs/docs/learning/interactivity/audio/. *Accessed:* 2026-09-08.
  *Learned:* `a.*` API and per-frame function pattern. *Relevance:* minimal audio API. *Confidence:* high.

---

## 9. Max/MSP Jitter and vvvv gamma

### 9.1 Jitter
A Jitter matrix is a grid of cells each holding a list of numbers; **dimensions** = resolution,
**planes** = channels (ARGB for video; 3/5/8/12/13-plane float32 for geometry); types char (0-255),
long, float32, float64; matrices travel through patch cords as named references (`jit_matrix
u12345678`); objects are configured via `@attributes` (`@adapt`, `@interp`, `@planemap`) [MAX-1].
*Design implication:* "data as typed N-plane matrices with named references" is the same design as
GPU textures/buffers; passing handles rather than copies is the key.

### 9.2 vvvv gamma (VL)
A **Spread** is an immutable collection ("a spread coming from one data source can be fed to many
data sinks and all sinks access the same data"); mutation happens on a `SpreadBuilder` (ToBuilder
-> edits -> ToSpread), builders should stay local [VVVV-1]. VL removed beta's implicit spreading
(auto-execution per slice) in favor of explicit loops; it distinguishes stateful **Process** nodes
from stateless **Operation** nodes; it has real numeric types (Float32/64, Integer32/64, Boolean)
and true Vector2/3/4 types instead of "spreads of length 3"; connections are allowed only from lower
to higher precision [VVVV-2]. *Design implication:* immutable, shareable signal buffers per frame
plus explicit stateful vs stateless processors is a good model for a C++ signal graph.

### Sources (Max, vvvv)
- **[MAX-1]** *Source:* Cycling '74 User Guide, "Jitter Matrix". *URL:* https://docs.cycling74.com/userguide/jitter/matrix/. *Accessed:* 2026-09-08.
  *Learned:* dims/planes/types, named matrix messages, attributes. *Relevance:* data-handle passing. *Confidence:* high.
- **[VVVV-1]** *Source:* The Gray Book, "Spreads and Other Collections". *URL:* https://thegraybook.vvvv.org/introduction/lo_9_2_Spreads.html. *Accessed:* 2026-09-08.
  *Learned:* immutability, SpreadBuilder. *Relevance:* buffer ownership. *Confidence:* high.
- **[VVVV-2]** *Source:* The Gray Book, "The Language" (beta vs gamma). *URL:* https://thegraybook.vvvv.org/reference/getting-started/beta/language.html. *Accessed:* 2026-09-08.
  *Learned:* Process vs Operation nodes, typed numerics, no implicit spreading. *Relevance:* node typing. *Confidence:* high.

---

## 10. Magic Music Visuals

Official pages (User's Guide, Features, forum) returned HTTP 403 in this session; the following
comes from search-engine snippets of those pages and should be treated as low-to-medium confidence.
Magic organizes projects into unlimited **scenes**, selected via a **Playlist** (buttons, keys or
timed auto-advance); visuals are built from **modules** whose parameters can be linked to audio
features — **Volume**, **Tone**, **Pitch**, arbitrary **frequency** ranges — and to MIDI, with
**thresholds** and **smoothing**; "Modifiers can let you get very complex movement" [MAG-1]. The
guide is a single ~100-page HTML document [MAG-1].

### Sources (Magic)
- **[MAG-1]** *Source:* Magic Music Visuals "User's Guide" and "Features" pages (search snippets; direct fetch 403). *URLs:* https://magicmusicvisuals.com/downloads/Magic_UsersGuide.html ; https://magicmusicvisuals.com/features ; https://magicmusicvisuals.com/documentation. *Accessed:* 2026-09-08.
  *Learned:* scenes/playlist, audio feature types, thresholds/smoothing, modifiers. *Relevance:* "feature -> parameter with threshold/smoothing" pattern; scene playlist. *Confidence:* low-medium (unfetched).

---

## 11. Synesthesia (SSF — Synesthesia Shader Format)

### 11.1 Scene format
A `.synScene` directory holds `main.glsl` (fragment shader), `scene.json` (metadata + CONTROLS +
IMAGES + PASSES), `thumbnail.png`, optional `script.js` and `images/` [SYN-1]. Control types:
slider, knob, toggle, bang, xy, color, dropdown; each has `NAME`, `TYPE`, `MIN`, `MAX`, `DEFAULT`,
`UI_GROUP`, `DESCRIPTION`; controls become uniforms automatically [SYN-1].

### 11.2 Standard uniforms
`RENDERSIZE` (vec2), `TIME`, `FRAMECOUNT`, `_xy`, `_uv`, `_uvc` (aspect-corrected), `PI`, mouse
(`_mouse`, `_click`, `_muv`, `_muvc`), `PASSINDEX`, `syn_FinalPass` (previous frame for feedback),
`syn_Media`, `syn_MediaType` (0 none/1 image/2 video/3 webcam) [SYN-2].

### 11.3 Audio uniforms (the documented audio-variable conventions)
All levels/hits/presence are normalized 0..1; the spectrum is split into Bass, Mid, MidHigh, High
[SYN-3]:

| Family | Uniforms | Semantics |
|---|---|---|
| Textures | `syn_Spectrum` (sampler1D: r raw FFT, g "juiced" smoothed FFT, b smooth FFT, a waveform), `syn_LevelTrail` (r whole, g bass, b mid, a high over time) | spectrum/waveform + level history |
| Levels | `syn_Level`, `syn_BassLevel`, `syn_MidLevel`, `syn_MidHighLevel`, `syn_HighLevel` | smoothed band loudness, 0..1 |
| Hits | `syn_Hits`, `syn_BassHits`, `syn_MidHits`, `syn_MidHighHits`, `syn_HighHits` | transient spikes, 0..1 |
| Presence | `syn_Presence`, `syn_BassPresence`, `syn_MidPresence`, `syn_MidHighPresence`, `syn_HighPresence` | "is this band active" 0..1 |
| Time clocks | `syn_Time`, `syn_BassTime`, `syn_MidTime`, `syn_MidHighTime`, `syn_HighTime`, `syn_CurvedTime` | clocks that advance when the band is loud (unbounded) |
| Beat | `syn_OnBeat` (1 then fast decay), `syn_ToggleOnBeat` (0/1 flip, logistic smoothing), `syn_RandomOnBeat` (new random on beat), `syn_BeatTime` (+1 per beat) | beat events |
| BPM | `syn_BPM` (50..220), `syn_BPMConfidence` (0..1), `syn_BPMTwitcher` (beat clock with exponential jumps), `syn_BPMSin`, `syn_BPMSin2`, `syn_BPMSin4`, `syn_BPMTri`, `syn_BPMTri2`, `syn_BPMTri4` (0..1 oscillators at BPM, /2, /4) | tempo-locked oscillators |
| Large features | `syn_FadeInOut` (rises at music start, falls at end), `syn_Intensity` (accumulates toward 1 with song intensity) | macro-structure |

Notes from the docs: level uniforms carry "some added smoothing to reduce jitter"; hits "spike in
value during isolated transients"; BPM-clock uniforms are "clocks that move forward when the volume
of a specific frequency band is high" [SYN-3][SYN-5].

### 11.4 Scripting
`script.js` runs `setup()` once and `update(dt)` per frame; all uniforms (standard, audio, controls)
are globals; `setUniform(name, value)` publishes new uniforms to the shader; `setControl`,
`setControlNormalized`, `randomizeControl/Group`, `defaultControl/Group` drive controls; event
handlers `onChange`, `onOffToOn`, `whileOn` are registered in `setup()` [SYN-4].

### Sources (Synesthesia)
- **[SYN-1]** *Source:* Synesthesia Docs, "Synesthesia Shader Format (SSF)". *URL:* https://app.synesthesia.live/docs/ssf/ssf.html. *Accessed:* 2026-09-08.
  *Learned:* file layout, scene.json controls schema. *Relevance:* JSON control -> uniform contract. *Confidence:* high.
- **[SYN-2]** *Source:* Synesthesia Docs, "SSF Standard Uniforms". *URL:* https://app.synesthesia.live/docs/ssf/standard_uniforms.html. *Accessed:* 2026-09-08.
  *Learned:* standard uniform set incl. feedback pass. *Relevance:* built-in uniform contract. *Confidence:* high.
- **[SYN-3]** *Source:* Synesthesia Docs, "SSF Audio Uniforms". *URL:* https://app.synesthesia.live/docs/ssf/audio_uniforms.html. *Accessed:* 2026-09-08.
  *Learned:* full audio uniform table with ranges. *Relevance:* THE reference vocabulary for audio control signals. *Confidence:* high.
- **[SYN-4]** *Source:* Synesthesia Docs, "JavaScript Scripting". *URL:* https://app.synesthesia.live/docs/ssf/script.html. *Accessed:* 2026-09-08.
  *Learned:* setup/update(dt), setUniform, control API, events. *Relevance:* per-frame scripting layer over the parameter system. *Confidence:* high.
- **[SYN-5]** *Source:* Synesthesia Docs index (search snippet). *URL:* https://synesthesia.live/docs/. *Accessed:* 2026-09-08.
  *Learned:* prose descriptions of levels/hits/BPM clocks. *Relevance:* semantics. *Confidence:* medium.

---

## 12. Milkdrop / projectM

Presets are INI-like `.milk` text files with sections: `per_frame_init` (once, sets `q1..q32`
defaults), `per_frame` (every frame), `per_vertex`/per-pixel (at mesh grid points), `custom_wave
[1-4]` and `custom_shape[1-4]` (init/per-frame/per-point code), `[warp]` and `[composite]` HLSL
shaders [MD-1]. Audio variables: `bass`, `mid`, `treb`, `vol` (1 = normal; ~0.7 quiet, ~1.3 loud)
and time-damped `bass_att`, `mid_att`, `treb_att`, `vol_att` ("attenuated" = damped in time, do
not change rapidly; `bass_att > 1` means the bass is spiking) [MD-1][MD-2]. Time: `time` (wraps at
10,000 s), `frame`, `fps`, `progress` (preset age 0..1). Three isolated variable pools (preset,
custom wave, custom shape); `q1..q32` bridge pools and flow from per-frame into per-vertex and the
shaders, resetting each frame to init values; `t1..t8` bridge wave/shape init to per-point code
[MD-1]. Per-vertex variables: `x,y` (0..1), `rad`, `ang`, `zoom`, `rot`, `warp`, `dx,dy`, `sx,sy`,
`cx,cy`. Shader inputs: `sampler_main` (+ `fw/fc/pw/pc` filtering variants), `rand_frame`,
`rand_preset` (4 floats each), `aspect`, `texsize`, pre-computed `roam_cos/sin` and
`slow_roam_cos/sin` oscillators, blur textures `GetBlur1..3`, noise textures (`noise_lq/mq/hq`,
`noisevol_*`). Presets crossfade via a short blend transition [MD-1].
*Design implication:* Milkdrop is the archetype of "a small, fixed vocabulary of normalized audio
variables with raw + smoothed pairs, plus a handful of scratch registers that flow from per-frame
scalar code into per-pixel GPU code."

### Sources (Milkdrop)
- **[MD-1]** *Source:* Geiss, "MilkDrop Preset Authoring Guide". *URL:* https://www.geisswerks.com/milkdrop/milkdrop_preset_authoring.html. *Accessed:* 2026-09-08.
  *Learned:* full preset structure, variables, pools, shader inputs. *Relevance:* audio-variable vocabulary; scalar->pixel data flow. *Confidence:* high (primary source).
- **[MD-2]** *Source:* Winamp Developer Wiki, "MilkDrop Preset Authoring" (search snippet). *URL:* http://wiki.winamp.com/wiki/MilkDrop_Preset_Authoring. *Accessed:* 2026-09-08.
  *Learned:* `_att` ranges/semantics wording. *Relevance:* smoothing convention. *Confidence:* medium.

---

## 13. Cables.gl

Patches are graphs of **ops** connected by typed ports: Trigger (yellow; `op.inTrigger`/
`op.outTrigger`, `onTriggered`), Value/number (green; `op.inFloat`, `op.inValueSlider(name,min,max)`),
String, Boolean, Array (light purple), Object (dark purple), plus textures [CAB-1][CAB-2]. There
are two flows: **trigger flow** (execution order, driven from a **MainLoop** op — any op needing
continuous updates takes a trigger input connected to MainLoop; convention: trigger-in first,
trigger-out too) and **value flow** (data propagation, `onChange`) [CAB-2][CAB-3]. Audio:
**AudioAnalyzer** outputs amplitude and an FFT array; **FFTAreaAverage** averages a rectangular
region (frequency range x amplitude range) of the spectrum; **AnalyzerTexture** turns the FFT array
into a grayscale texture; effects placed before the analyzer can shape the analysis without being
heard [CAB-4]. Patches export as standalone HTML [CAB-1].
*Design implication:* separating "when to execute" (trigger) from "what value" (data) makes frame
ordering explicit; the FFTAreaAverage "rectangle on the spectrum" is a nice band-extractor UI/model.

### Sources (Cables)
- **[CAB-1]** *Source:* cables.gl docs index. *URL:* https://cables.gl/docs. *Accessed:* 2026-09-08.
  *Learned:* concepts and doc map. *Relevance:* overview. *Confidence:* medium.
- **[CAB-2]** *Source:* cables.gl docs, "Ports". *URL:* https://cables.gl/docs/5_writing_ops/dev_creating_ports/dev_creating_ports. *Accessed:* 2026-09-08.
  *Learned:* port types and creation API. *Relevance:* port typing. *Confidence:* high.
- **[CAB-3]** *Source:* cables.gl docs, "Guidelines"/"Developing Ops" (search snippets). *URLs:* https://cables.gl/docs/5_writing_ops/guidelines/guidelines ; https://cables.gl/docs/5_writing_ops/dev_ops/dev_ops. *Accessed:* 2026-09-08.
  *Learned:* MainLoop trigger convention. *Relevance:* execution model. *Confidence:* medium.
- **[CAB-4]** *Source:* cables.gl docs, "Real-Time Audio Analyzation & Audio Visualization". *URL:* https://cables.gl/docs/8_audio/2_realtime_visualization/realtime_visualization. *Accessed:* 2026-09-08.
  *Learned:* AudioAnalyzer, FFTAreaAverage, AnalyzerTexture. *Relevance:* analysis ops. *Confidence:* high.

---

## 14. Godot

- **AudioEffectSpectrumAnalyzer** sits on an audio bus without altering audio; `buffer_length`
  0.1..4 s; `fft_size` 256..4096 (larger = smoother, more latency); instance obtained via
  `AudioServer.get_bus_effect_instance()`; `get_magnitude_for_frequency_range(from_hz, to_hz, mode)`
  returns a `Vector2` (x = left, y = right) in linear energy; modes `MAGNITUDE_AVERAGE` and
  `MAGNITUDE_MAX` (default) [GD-1][GD-2]. The demo converts linear energy to dB and normalizes to 0..1
  for rendering [GD-3].
- **Shader uniforms** carry editor hints: `hint_range(min,max[,step])`, `source_color`,
  `hint_enum(...)`, texture defaults; **instance uniforms** (`instance uniform`, max 16, no
  textures/arrays) allow per-node values without material copies; **global uniforms** are defined in
  Project Settings and set with `RenderingServer.global_shader_parameter_set`; materials use
  `set_shader_parameter` [GD-4]. Visual shaders are node graphs with Scalar/Vector/Boolean/
  Transform/Sampler ports and scalar-to-vector broadcast [GD-5].
- **`@export`** annotations: `@export_range(min,max,step,"or_greater","or_less","exp","suffix:m",
  "radians_as_degrees")`, `@export_enum`, `@export_group/subgroup/category`, `@export_storage`
  (serialize without inspector); exported values are saved with the scene/resource; low-level
  `_get_property_list()` with `PROPERTY_HINT_RANGE` [GD-6].
*Design implication:* Godot shows the parameter metadata set that matters in practice: range +
step + soft/hard bounds ("or_greater") + exponential slider + unit suffix + grouping.

### Sources (Godot)
- **[GD-1]** *Source:* Godot class reference, `AudioEffectSpectrumAnalyzer`. *URL:* https://docs.godotengine.org/en/stable/classes/class_audioeffectspectrumanalyzer.html. *Accessed:* 2026-09-08.
  *Learned:* buffer_length, fft_size. *Relevance:* analyzer config. *Confidence:* high.
- **[GD-2]** *Source:* Godot class reference, `AudioEffectSpectrumAnalyzerInstance`. *URL:* https://docs.godotengine.org/en/stable/classes/class_audioeffectspectrumanalyzerinstance.html. *Accessed:* 2026-09-08.
  *Learned:* band magnitude API and modes. *Relevance:* band extraction API shape. *Confidence:* high.
- **[GD-3]** *Source:* Godot Asset Library, "Audio Spectrum Visualizer Demo" (search snippet). *URL:* https://godotengine.org/asset-library/asset/2762. *Accessed:* 2026-09-08.
  *Learned:* dB normalization pattern. *Relevance:* mapping convention. *Confidence:* medium.
- **[GD-4]** *Source:* Godot docs, "Shading language". *URL:* https://docs.godotengine.org/en/stable/tutorials/shaders/shader_reference/shading_language.html. *Accessed:* 2026-09-08.
  *Learned:* uniform hints, instance/global uniforms, set APIs. *Relevance:* shader parameter metadata. *Confidence:* high.
- **[GD-5]** *Source:* Godot docs, "Visual shaders". *URL:* https://docs.godotengine.org/en/stable/tutorials/shaders/visual_shaders.html. *Accessed:* 2026-09-08.
  *Learned:* port types/broadcast. *Relevance:* node typing. *Confidence:* high.
- **[GD-6]** *Source:* Godot docs, "GDScript exported properties". *URL:* https://docs.godotengine.org/en/stable/tutorials/scripting/gdscript/gdscript_exports.html. *Accessed:* 2026-09-08.
  *Learned:* export hint vocabulary. *Relevance:* parameter metadata. *Confidence:* high.

---

## 15. Game-engine architecture references

### 15.1 EnTT (ECS)
Type-less, sparse-set ECS with no compile-time registration (`entt::registry`); entities are
opaque ids = index + version (safe recycling); components live in per-type paged sparse-set pools
("all or nothing": a `T*` to every instance of T is always available); **views** are zero-cost
queries iterating the smallest pool, **groups** own/rearrange pools for cache-optimal iteration;
storage emits `on_construct`/`on_update`/`on_destroy` signals; a per-registry **context** stores
singleton-like data; `snapshot`/`snapshot_loader`/`continuous_loader` serialize whole registries or
component subsets with entity remapping [ECS-1].

### 15.2 flecs
World/entity/component/tag; **pairs** (relationship, target) encode relations; `ChildOf` builds
hierarchies, `IsA` prefabs; queries match component composition; systems = query + callback ordered
in **pipeline phases** (OnLoad, OnUpdate, OnStore...); observers fire on OnAdd/OnSet; modules
package components+systems; reflection via entity types [ECS-2].
*Design implication:* an ECS registry is a good backbone for the scene (nodes = entities; parameter
blocks, render components, modulation routes = components), with `on_update` signals giving us
dirty tracking for parameter changes for free.

### 15.3 Reflection / property systems
- **UPROPERTY** (see [UE-3]): one declaration feeds serialization, editor UI, GC, networking; clamp
  vs UI range distinction.
- **O3DE**: three contexts — `SerializeContext` (persistence + versioning), `EditContext` (Min/Max/
  Step/UIHandler attributes for Inspector), `BehaviorContext` (scripting exposure); one property
  reflected once into all three [REF-1].
- **Godot** (see [GD-6]).
*Design implication:* parameter metadata must be authored once and consumed by serialization,
UI, scripting and modulation routing; keep "hard clamp" and "UI soft range" separate.

### Sources (ECS / reflection)
- **[ECS-1]** *Source:* EnTT docs, `entity.md`. *URL:* https://raw.githubusercontent.com/skypjack/entt/master/docs/md/entity.md. *Accessed:* 2026-09-08.
  *Learned:* registry model, views/groups, signals, context, snapshots. *Relevance:* scene backbone. *Confidence:* high.
- **[ECS-2]** *Source:* flecs docs, "Quickstart". *URL:* https://www.flecs.dev/flecs/md_docs_2Quickstart.html. *Accessed:* 2026-09-08.
  *Learned:* pairs/relationships, prefabs, pipeline phases, observers. *Relevance:* hierarchy + prefab modelling. *Confidence:* high.
- **[REF-1]** *Source:* O3DE docs, "Reflection". *URL:* https://docs.o3de.org/docs/user-guide/programming/components/reflection/. *Accessed:* 2026-09-08.
  *Learned:* Serialize/Edit/Behavior contexts. *Relevance:* single-source parameter metadata. *Confidence:* high.

---

## 16. Blender drivers, F-curves and F-curve modifiers

- **Drivers**: driver types Averaged Value, Sum Values, Minimum, Maximum, Scripted Expression (Python;
  a "Simple Expressions" subset evaluates fast without the interpreter); **driver variables**: Single
  Property (RNA path), Transform Channel, Rotational Difference, Distance, Context Property; `Use
  Self`; variables exist so the dependency graph can track dependencies (do not reference data
  directly inside the expression) [BL-1][BL-2]. The driver's output is then passed through the
  driver F-curve (value -> value mapping) — stated in Blender's manual per prior knowledge; the
  fetched panel page did not include that paragraph.
- **F-curve modifiers** (stack evaluated top to bottom): Generator (polynomial, additive), Built-in
  Function (sine/cos/tan/sqrt/ln/normalized sine with amplitude, phase multiplier/offset, value
  offset), Envelope (reference + min/max control points), Cycles (repeat / repeat with offset /
  mirrored; count), Noise (blend replace/add/sub/mul; scale, strength, offset, phase, depth,
  lacunarity, roughness), Limits (clamp X/Y), Stepped Interpolation (step size, offset), Smooth
  (Gaussian; sigma, width); every modifier has **Influence** and **Restrict Frame Range** with
  blend in/out; Cycles and Smooth must be first [BL-3].
- **Bake Sound to F-Curves**: lowest/highest frequency, attack time, release time, threshold,
  accumulate, additive, square (from a third-party wiki; official page not fetchable) [BL-4].
*Design implication:* Blender's "curve + modifier stack with influence and range" is exactly a
processor chain on a channel; Blender's driver = (variables -> combine -> curve remap) is a
modulation route with an explicit remap curve.

### Sources (Blender)
- **[BL-1]** *Source:* Blender Manual (main branch source), "Drivers Panel". *URL:* https://projects.blender.org/blender/blender-manual/raw/branch/main/manual/animation/drivers/drivers_panel.rst. *Accessed:* 2026-09-08.
  *Learned:* driver types, variable types, Use Self, simple expressions. *Relevance:* driver = modulation route. *Confidence:* high (docs.blender.org itself returned 403; the source repo was fetched).
- **[BL-2]** *Source:* Blender Python API, `bpy.types.Driver` (search snippet). *URL:* https://docs.blender.org/api/current/bpy.types.Driver.html. *Accessed:* 2026-09-08.
  *Learned:* driver type enum AVERAGE/SUM/SCRIPTED/MIN/MAX; dependency tracking rationale. *Relevance:* same. *Confidence:* medium.
- **[BL-3]** *Source:* Blender Manual (main branch source), "F-Curve Modifiers". *URL:* https://projects.blender.org/blender/blender-manual/raw/branch/main/manual/editors/graph_editor/fcurves/modifiers.rst. *Accessed:* 2026-09-08.
  *Learned:* full modifier list and stacking rules. *Relevance:* processor chain precedent. *Confidence:* high.
- **[BL-4]** *Source:* site-builder.wiki "Audio Visualizer [Blender]" (Japanese, search snippet). *URL:* https://site-builder.wiki/posts/39611. *Accessed:* 2026-09-08.
  *Learned:* Bake Sound to F-Curves parameters. *Relevance:* offline audio->curve baking with attack/release. *Confidence:* low-medium (third party; official page not fetched).

---

## 17. DAW-style modulation

### 17.1 Ableton Live Racks: macros
Up to 16 Macro Controls per Rack; **Map Mode** overlays mappable parameters; the **Mapping
Browser** gives per-mapping **Min/Max** sliders — inverted mappings by setting Min > Max; one macro
may drive many parameters (then it shows a generic 0..127 scale unless all targets share units and
range); **Macro Variations** store/recall snapshots of macro values; **Rand** randomizes all mapped
macros with per-macro exclusion [ABL-1].

### 17.2 Bitwig modulators
"Each modulator is a special-purpose module that can be added to any device or plug-in; its output
is then assigned to control various parameters"; categories: Audio-driven, Envelope, Interface,
LFO, Modifier (reshape modulation signals), Note-driven, Sequence, Voice Stacking [BW-1]. Modulators
switch between unipolar (0..1) and bipolar (-1..+1) and can modulate other modulators; amounts are
per target and routings can be copied between modulators [BW-2].

### 17.3 Synth modulation matrices (Vital, SynthLab)
Vital: drag-and-drop routing; amount adjustable like a knob; a **Matrix** tab lists every routing as
a row with source/destination popups, on/off, polarity (bipolar), stereo, and a response curve
(linear/log) plus **Mod Remap** secondary modulation [VIT-1]. SynthLab (Pirkle) formalizes it as
source array x destination array with three intensities (source, destination, channel; each
-1..+1, negative inverts), a single `runModMatrix()` that sums source*intensity into destinations,
hard-wired plus runtime routings, and per-destination transforms (bipolar/unipolar) and defaults
[SL-1]. General definition: sources, destinations, and slots with a scaling coefficient; a
destination may be modulated by multiple sources each with its own amount [MM-1].

### Sources (DAW modulation)
- **[ABL-1]** *Source:* Ableton Reference Manual 12, "Instrument, Drum and Effect Racks". *URL:* https://www.ableton.com/en/manual/instrument-drum-and-effect-racks/. *Accessed:* 2026-09-08.
  *Learned:* macro count, Map Mode, Min/Max + inversion, variations, randomize. *Relevance:* macro = one-to-many mapping with per-target ranges; variations = presets of macros. *Confidence:* high.
- **[BW-1]** *Source:* Bitwig User Guide, "Modulators". *URL:* https://www.bitwig.com/userguide/latest/modulator/. *Accessed:* 2026-09-08.
  *Learned:* modulator categories and definition. *Relevance:* modulator taxonomy. *Confidence:* high.
- **[BW-2]** *Source:* polarity.me "Bitwig Modulation System" guide (search snippet). *URL:* https://polarity.me/posts/bitwig-guides/2022-04-12-the-modulation-system-bitwig-modulator-guide/. *Accessed:* 2026-09-08.
  *Learned:* unipolar/bipolar toggle, modulating modulators, copying routings. *Relevance:* polarity and meta-modulation. *Confidence:* medium (third-party).
- **[VIT-1]** *Source:* David Vogel, Vital User Guide "Modulation". *URL:* https://davidmvogel.com/docs/Vital/UserGuide/Modulation. *Accessed:* 2026-09-08.
  *Learned:* matrix rows, polarity, curve, remap. *Relevance:* matrix UI/data model. *Confidence:* medium-high.
- **[SL-1]** *Source:* Will Pirkle, SynthLab SDK "Modulation Matrix". *URL:* https://www.willpirkle.com/synthlab/docs/html/mod_matrix.html. *Accessed:* 2026-09-08.
  *Learned:* C++ mod matrix structure and run loop. *Relevance:* direct implementation reference. *Confidence:* high.
- **[MM-1]** *Source:* Attack Magazine "What is a modulation matrix?" and ResearchGate "A modulation matrix for complex parameter sets" (search snippets). *URLs:* https://www.attackmagazine.com/technique/synth-secrets/what-is-a-modulation-matrix-how-does-it-work/ ; https://www.researchgate.net/publication/228732325_A_modulation_matrix_for_complex_parameter_sets. *Accessed:* 2026-09-08.
  *Learned:* general definition. *Relevance:* terminology. *Confidence:* medium.

---

## 18. Live-control protocols: OSC (and MIDI/DMX conventions)

OSC 1.0: addresses are a hierarchical tree `/container/.../method` (URL-like); patterns support
`?`, `*`, `[abc]`/ranges/negation, `{foo,bar}`; type tags begin with `,` and include `i f s b`
plus extensions `h t d S c r m T F N`; **bundles** carry NTP-format 64-bit time tags (LSB-only =
"immediately") and are applied atomically; servers pattern-match incoming addresses against their
address space [OSC-1]. Resolume's fixed OSC address scheme with normalized 0..1 values [RES-4] and
TouchDesigner's channel-name-as-address conventions [TD-17] both show that **the parameter path is
the control address**.

### Sources (protocols)
- **[OSC-1]** *Source:* OpenSoundControl 1.0 Specification. *URL:* https://opensoundcontrol.stanford.edu/spec-1_0.html. *Accessed:* 2026-09-08.
  *Learned:* address/pattern/type-tag/bundle semantics. *Relevance:* parameter addressing + timestamped control. *Confidence:* high.

---

## 19. Smoothing conventions across systems (summary)

| System | Smoothing model | Notes |
|---|---|---|
| Notch Sound/FFT modifiers | Attack + Decay + Smoothing, then Scale/Offset [NOTCH-3] | three knobs; op add/mul/replace |
| Resolume audio parameter | Gain + Fall [RES-1] | Fall = release; attack implicit |
| TouchDesigner Lag CHOP | separate lag up / lag down (time to 90%), overshoot, slope/accel clamps [TD-6] | asymmetric = attack/release |
| TouchDesigner Filter CHOP | Gaussian / half-Gaussian / One Euro etc. [TD-7] | kernel smoothing, causal variants |
| TouchDesigner Trigger CHOP | D/A/P/D/S/R envelope from threshold crossing [TD-10] | impulse -> envelope |
| Synesthesia | levels pre-smoothed; hits fast-decay; logistic smoothing on toggles [SYN-3] | smoothing baked into vocabulary |
| Milkdrop | raw `bass` + damped `bass_att` [MD-1] | raw/smoothed pair |
| Hydra | `a.setSmooth(0..1)` EMA-style + cutoff + scale [HY-2] | single knob |
| UE Audio Synesthesia | AnalysisPeriod; onset granularity + sensitivity [UE-7] | analyzer-side |
| Godot | fft_size trades smoothness vs latency [GD-1] | analyzer-side |
| One Euro filter | speed-adaptive low-pass with `mincutoff` (jitter at low speed) and `beta` (lag at high speed) [OE-1] | best for noisy interactive inputs |
| Blender F-curve Smooth/Noise/Envelope | modifier stack with Influence [BL-3] | offline curves |

- **[OE-1]** *Source:* Casiez, Roussel, Vogel, "1€ Filter" (CHI 2012) project page. *URL:* https://gery.casiez.net/1euro/. *Accessed:* 2026-09-08.
  *Learned:* mincutoff/beta semantics and tuning procedure. *Relevance:* smoothing for controller/sensor inputs. *Confidence:* high.

---

## 20. Design lessons for our engine

Each lesson names the systems it is derived from.

1. **Control signals are first-class typed channels with a processing chain (TouchDesigner CHOP
   model).** Represent every time-varying control value as a named channel (float samples, sample
   rate, history), processed by small composable nodes (Lag, Filter, Math/Range, Trigger, Envelope,
   Beat) and bound to parameters by name/path [TD-1][TD-2][TD-6..TD-11]. Notch's modifier list
   [NOTCH-4] and Blender's F-curve modifiers [BL-3] confirm the same taxonomy: sources, transforms,
   logic, I/O.

2. **Process signals in time slices, not once per frame.** Adopt TouchDesigner's time-slice
   semantics: every frame processes all samples since the last cook, batching on frame drops so
   envelopes/onsets are never skipped and offline renders at any fps produce identical results
   [TD-3]. Notch's "locked to timecode vs running" mode on LFO/beat modifiers [NOTCH-5][NOTCH-6] and
   MRQ's warm-up frames [UE-6] are the same concern from the rendering side.

3. **Generalized modulation matrix: source -> processor chain -> amount (bipolar) -> combine op ->
   destination.** Notch (Add/Subtract/Multiply/Replace + Blend) [NOTCH-5][NOTCH-6], SynthLab's
   source/destination intensity arrays summed in one `runModMatrix()` [SL-1], Vital's rows with
   polarity + curve + remap [VIT-1], Bitwig's unipolar/bipolar and modulators-of-modulators [BW-2],
   Blender drivers (variables -> avg/sum/min/max/expression -> F-curve remap) [BL-1]. Multiple routes
   per destination must sum; routes must be data, not code, so they serialize with the scene.

4. **Parameter = typed value + rich metadata + unique path.** From UPROPERTY (ClampMin/Max vs
   UIMin/Max, Category, EditCondition) [UE-3], Godot exports (range/step/or_greater/exp/suffix/group)
   [GD-6], Godot shader hints (`hint_range`, `source_color`) [GD-4], SSF controls (TYPE/MIN/MAX/
   DEFAULT/UI_GROUP) [SYN-1], ISF INPUTS (TYPE/MIN/MAX/DEFAULT/LABEL) [ISF-1], Shader Graph scope
   (global / per-material / per-instance) [UN-6], O3DE's single reflection feeding
   serialize/edit/behavior [REF-1]. The path (`scene/orb/scale`) is simultaneously the OSC address
   [OSC-1][RES-4], the export-binding key [TD-2] and the preset key.

5. **Smoothing is attack/release (asymmetric), not a symmetric lerp; offer One Euro for noisy
   inputs.** Notch (Attack/Decay/Smoothing) [NOTCH-3], Resolume (Gain/Fall) [RES-1], TD Lag
   (lag up/down) [TD-6], Milkdrop raw/`_att` pairs [MD-1], Synesthesia's pre-smoothed levels and
   fast-decay hits [SYN-3], One Euro for controllers [OE-1][TD-7]. Publish both raw and smoothed
   variants of core analysis features, as Milkdrop and Synesthesia do.

6. **Publish a fixed, normalized audio-feature vocabulary (Synesthesia/Milkdrop pattern), with
   beat/bar phase signals.** Bands (bass/mid/midhigh/high + whole), per-band Level (smoothed, 0..1),
   Hits/onsets, Presence, band-driven clocks, OnBeat pulse, BeatTime counter, BPM + confidence,
   tempo-locked sin/tri at 1x/½x/¼x, FadeInOut and Intensity macro features [SYN-3]; kick/snare/
   rhythm/centroid/partial density from TD's audioAnalysis [TD-5]; ConstantQ bands + onset
   strength from UE [UE-7]; TD Beat CHOP's ramp/pulse/count/bar/beat/sixteenth channels [TD-9].
   Normalize with AGC/envelope-division (TD Envelope CHOP trick [TD-11]; Processing spectrum values
   rarely exceed 0.05 raw [PR-1]; Godot demo converts to dB then 0..1 [GD-3]).

7. **Presets are parameter snapshots keyed by path; macros are one-to-many mappings with per-target
   ranges and inversion.** Ableton macros/variations/randomize [ABL-1], Resolume parameter presets
   [RES-1], Synesthesia `setControl`/`randomizeGroup`/`defaultGroup` [SYN-4], Milkdrop preset blend
   transitions [MD-1]. Preset morphing = interpolating snapshots per parameter curve type.

8. **Scenes are reusable graphs; separate sequence data from scene bindings.** Unity Timeline asset
   vs instance + bindings [UN-4], Sequencer possessables vs spawnables [UE-5], Notch Blocks packaging
   a project + engine with exposed params [NOTCH-10], TD `.tox` components [TD-16], flecs prefabs/
   `IsA` [ECS-2]. Exposed parameters should be an explicit, curated surface (Niagara `User.*` [UE-1],
   Notch Expose To Block [NOTCH-9], VFX Graph Exposed [UN-2]).

9. **Deterministic timeline evaluation with keyframes as just another channel source.** TD Animation
   COMP (index -> value, random access) [TD-14], Notch keyframes + curve editor [NOTCH-13], Blender
   F-curves + modifier stack with influence/range [BL-3], Milkdrop `progress`/`time` [MD-1]. Offline
   export = fixed-step evaluation with pre-analyzed audio (UE Synesthesia NRT assets [UE-7], Blender
   Bake Sound to F-Curves [BL-4]) fed through the *same* channel API as live analysis.

10. **Shader parameters bind by name from the same parameter system; arrays go in as texture
    buffers.** GLSL TOP uniform arrays/texture buffers [TD-12], Synesthesia controls -> uniforms and
    `syn_Spectrum` as a 1D texture [SYN-1][SYN-3], ISF INPUTS/PASSES/PERSISTENT + `audioFFT` as an
    image [ISF-1], Godot instance/global uniforms [GD-4], Unity Shader Graph scope [UN-6]. Support
    multipass with persistent targets (ISF `PERSISTENT`, SSF `syn_FinalPass`) for feedback effects,
    and threaded shader recompilation with a placeholder (TD) for live-coding.

11. **Explicit execution order: separate "trigger/tick" flow from "value" flow, and stateful from
    stateless nodes.** Cables' MainLoop trigger ports [CAB-2][CAB-3], VL's Process vs Operation nodes
    and immutable spreads [VVVV-1][VVVV-2], Niagara stack groups with namespace read/write rules
    [UE-1], Jitter's named-handle matrix passing [MAX-1].

12. **Offline export features to plan for:** non-realtime cook flag and A/V sync by frame repetition
    [TD-15]; temporal/spatial sample accumulation, warm-up frames, per-job settings, tiled high-res
    [UE-6]; motion blur via sub-frame overlap, AA passes, preroll, alpha, audio offset [NOTCH-12];
    EXR multi-channel/AOV sequences [TD-15][UN-7].

13. **Serialization: text (JSON) project with binary asset caches.** `.tscn` text (Godot), `.avc`
    XML (Resolume, undocumented — a cautionary tale about not documenting) [RES-5], `.milk` INI
    [MD-1], SSF JSON [SYN-1], `.toe` binary with an expand-to-ASCII tool [TD-16]. Version the schema
    (O3DE SerializeContext versioning [REF-1]).

14. **Live control addresses = parameter paths; support bundles with time tags.** OSC address
    space + bundles [OSC-1]; Resolume's normalized 0..1 convention with per-parameter unit mapping
    [RES-4]; TD's channel-name conventions for MIDI/DMX [TD-17]; Notch MIDI/OSC modifiers [NOTCH-4].

---

## 21. Proposed conceptual data model

Informed by sections 2-19. Names are illustrative; types are conceptual (not final C++).

```text
// ---------- Parameters (the destination side) ----------
ParamType     = Float | Int | Bool | Vec2 | Vec3 | Vec4 | Color | Enum | Trigger | Texture | Curve
Curve         = Linear | Log | Exp | Custom(remap points)          // Godot "exp", Vital lin/log
Scope         = Global | Scene | Node | Instance                      // Shader Graph scope, Godot instance uniforms

ParamDesc {                                 // authored once; feeds UI, serialization, routing (O3DE, UPROPERTY)
  path        : "scene/orb/scale"           // unique; doubles as OSC address and preset key
  type        : ParamType
  default     : Value
  hard_range  : [min, max] | none           // UE ClampMin/Max
  soft_range  : [min, max] | none           // UE UIMin/UIMax; Godot "or_greater"
  step, unit, curve                          // Godot @export_range step/suffix/exp
  group, label, tooltip                      // SSF UI_GROUP, Notch/Godot groups
  flags       : Exposed | Animatable | Modulatable | Serialized | Hidden   // VFX Graph "Exposed", Godot @export_storage
  smoothing   : optional default SmoothSpec  // per-param default attack/release (Notch, Resolume "Fall")
  edit_condition: optional expr              // UE EditCondition
}

ParamValue  { base : Value; keyed : Value?; modulated : Value; final : Value }
// final = clamp( combine( keyed ?? base , Σ modulation routes ) , hard_range )

// ---------- Control signals (the source side) ----------
Channel {                                   // TouchDesigner CHOP channel
  name        : "audio/bands/bass/level"    // namespaced path; namespaces: audio/, beat/, lfo/, midi/, osc/, time/, user/
  rate        : samples/sec (control rate, e.g. 1000 Hz) | frame
  samples     : ring buffer (time-sliced: all samples since last cook)   // TD Time Slicing
  polarity    : Unipolar(0..1) | Bipolar(-1..1) | Unbounded              // Bitwig/SynthLab
  meta        : { units, smoothed_from: channel?, latency }
}

AnalysisBus (fixed vocabulary, published every slice; Synesthesia/Milkdrop pattern):
  audio/level            audio/level_raw
  audio/bands/{bass,mid,midhigh,high}/{level,level_raw,hits,presence,time}
  audio/onset/{kick,snare,any}      audio/spectrum (N-bin texture buffer, raw/smooth)
  audio/features/{centroid,flux,rms}
  beat/{phase,bar_phase,pulse,count,bpm,confidence,on_beat}
  beat/osc/{sin,tri,saw}/{1,2,4}    beat/ramp/{1_4,1_2,1,2,4,8}          // TD Beat CHOP
  song/{fade_in_out,intensity}
  time/{t,frame,dt,progress}        rand/{frame.xyzw,preset.xyzw}         // Milkdrop rand_frame/rand_preset

// ---------- Processors (chain nodes on channels) ----------
Processor = Gain | Offset | RangeMap(from,to,curve) | Clamp | Quantize(step)
          | AttackRelease(attack_s, release_s)                          // Notch Attack/Decay; TD Lag up/down
          | Lag(up,down,overshoot,slope_clamp,accel_clamp)               // TD Lag CHOP
          | OneEuro(mincutoff,beta) | Gaussian(width) | HalfGaussian(width)
          | EnvelopeFollower(window,mode)                                // TD Envelope CHOP; AGC
          | Trigger(threshold, D,A,P,D,S,R, shapes, retrigger)           // TD Trigger CHOP
          | Delay(time) | Accumulate | Hold(sample&hold) | Toggle | Random(on_trigger)
          | Math(expr over channels)                                     // Blender scripted expr; TD Math
          | Curve(remap points) | Logistic                               // Synesthesia ToggleOnBeat smoothing
          | Combine(avg|sum|min|max)                                     // Blender driver types
Processors are either Stateless(Operation) or Stateful(Process); stateful ones own their state and
are re-entrant per time slice (vvvv gamma distinction).

// ---------- Modulation routes (the matrix) ----------
ModRoute {
  id
  source      : channel path | LFO spec | keyframe track | macro id       // any Channel
  chain       : [Processor...]                                            // per-route shaping
  amount      : float (-1..1, negative inverts)                           // SynthLab intensity
  polarity    : Unipolar | Bipolar
  op          : Add | Multiply | Replace | Min | Max                      // Notch operation
  target      : ParamDesc.path (+ component .x/.y/.z)
  enabled, blend (0..1)                                                   // Notch blend; Blender influence
  time_mode   : LockedToTimeline | FreeRunning                            // Notch time mode (determinism)
}
Evaluation per slice: for each target, acc = keyed ?? base;
  for routes ordered by (op priority: Replace < Multiply < Add):  acc = op(acc, amount * chain(source))
  final = clamp(acc, hard_range); write ParamValue.final; emit on_update (EnTT signal) for dirty tracking.

// ---------- Macros / presets ----------
Macro { id, name, value 0..1, mappings: [{ target path, min, max (min>max = inverted), curve }] }   // Ableton
Preset { name, values: { path -> base Value }, macros: { id -> value }, routes?: [ModRoute] }        // snapshot
Preset morph: for each path, interpolate by the param's curve type; Replace-op routes cross-fade.

// ---------- Scene / graph ----------
Scene = ECS registry (EnTT) with:
  entities  = nodes (Root, Layer, Generator, Effect, Particles, Camera, ShaderPass...)
  components = ParamBlock (ParamDesc refs + ParamValue), Hierarchy (ChildOf), RenderComponent,
               ModRoutes, Keyframes, ExposedSurface (curated param list; Niagara User.* / Notch Expose)
  prefabs   = reusable subgraphs (.tox / Block analogue) instantiated with their own ParamValues
SceneAsset (JSON, versioned schema) vs SceneInstance (bindings to devices, audio inputs, outputs)  // Unity asset/instance

// ---------- Shaders ----------
ShaderModule { source, inputs: [ParamDesc...] (parsed from a JSON header: ISF/SSF-compatible),
               passes: [{ target, persistent, float, size expr }], standard uniforms
               (TIME, TIMEDELTA, RENDERSIZE, FRAMEINDEX, PASSINDEX, prev pass), analysis textures }
Binding: ParamDesc.path -> uniform by name; channel arrays -> texture buffers; hot-reload with placeholder.

// ---------- Timeline ----------
Timeline { rate, range, tracks: [ { target path, keys: [{t, value, slope, interp}] , modifiers: [Processor] } ] }
Evaluate(t) is pure: keyed values are a function of t only; beat/LFO sources in LockedToTimeline mode derive
phase from t; audio in offline mode comes from a pre-analysis cache indexed by t. Same Channel API as live.

// ---------- Live control ----------
Address = ParamDesc.path (OSC "/scene/orb/scale"), values normalized 0..1 by soft_range (Resolume style)
Inputs (MIDI/OSC/DMX/keyboard) publish Channels under midi/, osc/, dmx/ and can be routed like any source.
OSC bundles with time tags are scheduled onto the control clock.
```

Key invariants:
- One metadata source (ParamDesc) drives UI, serialization, presets, OSC, and routing validity.
- Everything time-varying is a Channel; everything that reads a Channel is a Processor or a Route.
- Evaluation of a frame is a function of (timeline t, analysis-bus slice, live-input slice, state)
  and is reproducible offline by replaying the same slices.

---

## 22. Open questions / not verified in this pass

- Unreal Material Parameter Collection limits and Sequencer MPC tracks (page fetch returned only a
  TOC) [UE-4].
- Whether UE's real-time (non-NRT) Synesthesia analyzers and the exact Blueprint node names
  ("Start Analyzing Output", "Get Magnitude for Frequencies") exist as remembered — not confirmed.
- Notch `.dfx` on-disk encoding (binary assumed; not documented in fetched pages).
- Magic Music Visuals internals (all official pages 403) [MAG-1].
- Blender's driver F-curve remap paragraph and the official "Bake Sound to F-Curves" page (403 on
  docs.blender.org; manual source repo fetched for other pages) [BL-1][BL-4].
- Synesthesia MIDI/OSC mapping and video recording; Resolume record/export details; Godot Movie
  Maker mode; Unity audio spectrum API (`AudioSource.GetSpectrumData`) — all from prior knowledge
  only, not fetched.
- The dedicated TouchDesigner "Audio Analysis CHOP" page 404'd; the palette component is the
  documented equivalent [TD-5].

---

## 23. Sources (consolidated)

All accessed 2026-09-08. Confidence: H = official doc fetched; M = official page partially
fetched or reputable secondary/search snippet; L = third-party snippet or direct fetch blocked.

| Ref | Source | URL | Conf. |
|---|---|---|---|
| TD-1 | Derivative docs — CHOP | https://docs.derivative.ca/CHOP | H |
| TD-2 | Derivative docs — Export | https://docs.derivative.ca/Export | H |
| TD-3 | Derivative docs — Time Slicing | https://docs.derivative.ca/Time_Slicing | H |
| TD-4 | Derivative docs — Audio Spectrum CHOP | https://docs.derivative.ca/Audio_Spectrum_CHOP | H |
| TD-5 | Derivative docs — Palette:audioAnalysis | https://docs.derivative.ca/Palette:audioAnalysis | H |
| TD-6 | Derivative docs — Lag CHOP | https://docs.derivative.ca/Lag_CHOP | H |
| TD-7 | Derivative docs — Filter CHOP | https://docs.derivative.ca/Filter_CHOP | H |
| TD-8 | Derivative docs — Math CHOP | https://docs.derivative.ca/Math_CHOP | H |
| TD-9 | Derivative docs — Beat CHOP | https://docs.derivative.ca/Beat_CHOP | H |
| TD-10 | Derivative docs — Trigger CHOP | https://docs.derivative.ca/Trigger_CHOP | H |
| TD-11 | Derivative docs — Envelope CHOP | https://docs.derivative.ca/Envelope_CHOP | H |
| TD-12 | Derivative docs — GLSL TOP | https://docs.derivative.ca/GLSL_TOP | H |
| TD-13 | Derivative docs — Timeline | https://docs.derivative.ca/Timeline | H |
| TD-14 | Derivative docs — Animation COMP | https://docs.derivative.ca/Animation_COMP | H |
| TD-15 | Derivative docs — Movie File Out TOP | https://docs.derivative.ca/Movie_File_Out_TOP | H |
| TD-16 | Derivative docs — .toe | https://docs.derivative.ca/.toe | H |
| TD-17 | Derivative docs — MIDI Out / OSC In / DMX Out CHOP | https://docs.derivative.ca/MIDI_Out_CHOP ; https://docs.derivative.ca/OSC_In_CHOP ; https://docs.derivative.ca/DMX_Out_CHOP | M |
| NOTCH-1 | Notch Manual 1.0 — Nodes | https://manual.notch.one/1.0/en/topic/nodes | M |
| NOTCH-2 | Notch Manual 1.0 — Nodegraph | https://manual.notch.one/1.0/en/docs/reference/user-interface/nodegraph/ | M |
| NOTCH-3 | Notch Manual 1.0 — Sound Modifier | https://manual.notch.one/1.0/en/docs/reference/nodes/modifiers/sound-modifier/ | H |
| NOTCH-4 | Notch Manual 1.0 — Modifiers index | https://manual.notch.one/1.0/en/docs/reference/nodes/modifiers/ | H |
| NOTCH-5 | Notch Manual 0.9.23 — Math Modifier | https://manual.notch.one/0.9.23/en/docs/nodes/modifiers/math-modifier/ | H |
| NOTCH-6 | Notch Manual 0.9.22 — Beat Pulse Modifier | http://manual.notch.one/0.9.22/en/topic/nodes-modifiers-beat-pulse-modifier | H |
| NOTCH-7 | Notch Manual 2026.1 — Working With Audio | https://manual.notch.one/2026.1/en/docs/learning/working-with-audio/ | M |
| NOTCH-8 | Notch Manual 1.0 — Sound FFT Modifier | https://manual.notch.one/1.0/en/docs/reference/nodes/modifiers/sound-fft-modifier/ | M |
| NOTCH-9 | Notch Manual 2026.1 — Exposed Parameters | https://manual.notch.one/2026.1/en/docs/workflows/working-with-media-servers/exposed-parameters/ | M |
| NOTCH-10 | Notch Manual 2026.1 — Notch Blocks | https://manual.notch.one/2026.1/en/docs/workflows/working-with-media-servers/blocks/ | M |
| NOTCH-11 | Notch Blocks feature page; Smode Notch Block doc | https://www.notch.one/features/notch-blocks ; https://www.smode.io/doc/ref/compo/integrations/notch-block-.dfxdll-file.htm | M |
| NOTCH-12 | Notch Manual 0.9.23 — Export Video | https://manual.notch.one/0.9.23/en/docs/user-interface/exporting-video/ | H |
| NOTCH-13 | Notch Manual 2026.1 — Rendering Basics / Timeline | https://manual.notch.one/2026.1/en/docs/learning/rendering/rendering-basics/ ; https://manual.notch.one/2026.1/en/docs/reference/user-interface/timeline/ | M |
| UE-1 | Epic — Key Concepts in Niagara | https://dev.epicgames.com/documentation/en-us/unreal-engine/key-concepts-in-niagara-effects-for-unreal-engine | H |
| UE-2 | ibbles — Niagara user exposed parameters | https://github.com/ibbles/LearningUnrealEngine/blob/master/Niagara%20user%20exposed%20parameters.md | M |
| UE-3 | Epic — UProperties | https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-uproperties | H |
| UE-4 | Epic — Material Parameter Collections | https://dev.epicgames.com/documentation/en-us/unreal-engine/using-material-parameter-collections-in-unreal-engine | L |
| UE-5 | Epic — Cinematics and Movie Making (Sequencer) | https://dev.epicgames.com/documentation/en-us/unreal-engine/cinematics-and-movie-making-in-unreal-engine | M |
| UE-6 | Epic — Rendering High Quality Frames with MRQ | https://dev.epicgames.com/documentation/unreal-engine/rendering-high-quality-frames-with-movie-render-queue-in-unreal-engine?lang=en-US | H |
| UE-7 | Epic — Audio Synesthesia | https://dev.epicgames.com/documentation/en-us/unreal-engine/audio-synesthesia-in-unreal-engine | H |
| UE-8 | Epic — Add Spectral Analysis Delegate; Overview of submixes | https://dev.epicgames.com/documentation/en-us/unreal-engine/BlueprintAPI/Audio/Spectrum/AddSpectralAnalysisDelegate ; https://dev.epicgames.com/documentation/unreal-engine/overview-of-submixes-in-unreal-engine | M |
| UN-1 | Unity VFX Graph — Graph Logic and Philosophy | https://docs.unity3d.com/Packages/com.unity.visualeffectgraph@17.0/manual/GraphLogicAndPhilosophy.html | H |
| UN-2 | Unity VFX Graph — Properties; Blackboard | https://docs.unity3d.com/Packages/com.unity.visualeffectgraph@17.2/manual/Properties.html ; https://docs.unity3d.com/Packages/com.unity.visualeffectgraph@17.2/manual/Blackboard.html | H |
| UN-3 | Unity Scripting API — VisualEffect.SetFloat | https://docs.unity3d.com/ScriptReference/VFX.VisualEffect.SetFloat.html | M |
| UN-4 | Unity Timeline — assets and instances | https://docs.unity3d.com/Packages/com.unity.timeline@1.8/manual/tl-overview.html | H |
| UN-5 | Unity Manual — Playables API | https://docs.unity3d.com/Manual/Playables.html | M |
| UN-6 | Unity Shader Graph — Property Types | https://docs.unity3d.com/Packages/com.unity.shadergraph@17.0/manual/Property-Types.html | H |
| UN-7 | Unity Recorder 5.1 manual | https://docs.unity3d.com/Packages/com.unity.recorder@5.1/manual/index.html | H |
| UN-8 | Unity Cinemachine — camera control/transitions; blending | https://docs.unity3d.com/Packages/com.unity.cinemachine@3.1/manual/concept-camera-control-transitions.html ; https://docs.unity3d.com/Packages/com.unity.cinemachine@2.3/manual/CinemachineBlending.html | M |
| RES-1 | Resolume — Parameter Animation | https://resolume.com/support/en/parameter-animation | H |
| RES-2 | Resolume — Wire FFT | https://resolume.com/support/en/wire-fft | H |
| RES-3 | Resolume — ISF | https://resolume.com/support/en/isf | H |
| RES-4 | Resolume — OSC | https://resolume.com/support/en/osc | H |
| RES-5 | Resolume forum (avc format); Resolume-Composition-Converter | https://resolume.com/forum/viewtopic.php?t=12760 ; https://github.com/tijnisfijn/Resolume-Composition-Converter | M |
| ISF-1 | ISF Specification (mrRay/ISF_Spec) | https://github.com/mrRay/ISF_Spec | H |
| PR-1 | processing-sound Javadoc — FFT | https://processing.github.io/processing-sound/processing/sound/FFT.html | H |
| PR-2 | processing-sound Javadoc — BeatDetector | https://processing.github.io/processing-sound/processing/sound/BeatDetector.html | H |
| PR-3 | Minim — BeatDetect | https://code.compartmental.net/minim/beatdetect_class_beatdetect.html | H |
| HY-1 | Hydra — API reference | https://hydra.ojack.xyz/api/ | M |
| HY-2 | Hydra docs — Audio | https://hydra.ojack.xyz/docs/docs/learning/interactivity/audio/ | H |
| MAX-1 | Cycling '74 — Jitter Matrix user guide | https://docs.cycling74.com/userguide/jitter/matrix/ | H |
| VVVV-1 | The Gray Book — Spreads and Other Collections | https://thegraybook.vvvv.org/introduction/lo_9_2_Spreads.html | H |
| VVVV-2 | The Gray Book — The Language (beta vs gamma) | https://thegraybook.vvvv.org/reference/getting-started/beta/language.html | H |
| MAG-1 | Magic Music Visuals — User's Guide / Features (403; snippets) | https://magicmusicvisuals.com/downloads/Magic_UsersGuide.html ; https://magicmusicvisuals.com/features | L |
| SYN-1 | Synesthesia Docs — SSF | https://app.synesthesia.live/docs/ssf/ssf.html | H |
| SYN-2 | Synesthesia Docs — Standard Uniforms | https://app.synesthesia.live/docs/ssf/standard_uniforms.html | H |
| SYN-3 | Synesthesia Docs — Audio Uniforms | https://app.synesthesia.live/docs/ssf/audio_uniforms.html | H |
| SYN-4 | Synesthesia Docs — JavaScript Scripting | https://app.synesthesia.live/docs/ssf/script.html | H |
| SYN-5 | Synesthesia Docs index | https://synesthesia.live/docs/ | M |
| MD-1 | Geiss — MilkDrop Preset Authoring Guide | https://www.geisswerks.com/milkdrop/milkdrop_preset_authoring.html | H |
| MD-2 | Winamp Dev Wiki — MilkDrop Preset Authoring | http://wiki.winamp.com/wiki/MilkDrop_Preset_Authoring | M |
| CAB-1 | cables.gl docs index | https://cables.gl/docs | M |
| CAB-2 | cables.gl — Ports | https://cables.gl/docs/5_writing_ops/dev_creating_ports/dev_creating_ports | H |
| CAB-3 | cables.gl — Guidelines / Developing Ops | https://cables.gl/docs/5_writing_ops/guidelines/guidelines ; https://cables.gl/docs/5_writing_ops/dev_ops/dev_ops | M |
| CAB-4 | cables.gl — Real-Time Audio Analyzation | https://cables.gl/docs/8_audio/2_realtime_visualization/realtime_visualization | H |
| GD-1 | Godot — AudioEffectSpectrumAnalyzer | https://docs.godotengine.org/en/stable/classes/class_audioeffectspectrumanalyzer.html | H |
| GD-2 | Godot — AudioEffectSpectrumAnalyzerInstance | https://docs.godotengine.org/en/stable/classes/class_audioeffectspectrumanalyzerinstance.html | H |
| GD-3 | Godot Asset Library — Audio Spectrum Visualizer Demo | https://godotengine.org/asset-library/asset/2762 | M |
| GD-4 | Godot — Shading language | https://docs.godotengine.org/en/stable/tutorials/shaders/shader_reference/shading_language.html | H |
| GD-5 | Godot — Visual shaders | https://docs.godotengine.org/en/stable/tutorials/shaders/visual_shaders.html | H |
| GD-6 | Godot — GDScript exported properties | https://docs.godotengine.org/en/stable/tutorials/scripting/gdscript/gdscript_exports.html | H |
| ECS-1 | EnTT — entity.md | https://raw.githubusercontent.com/skypjack/entt/master/docs/md/entity.md | H |
| ECS-2 | flecs — Quickstart | https://www.flecs.dev/flecs/md_docs_2Quickstart.html | H |
| REF-1 | O3DE — Reflection | https://docs.o3de.org/docs/user-guide/programming/components/reflection/ | H |
| BL-1 | Blender Manual source — Drivers Panel | https://projects.blender.org/blender/blender-manual/raw/branch/main/manual/animation/drivers/drivers_panel.rst | H |
| BL-2 | Blender Python API — bpy.types.Driver | https://docs.blender.org/api/current/bpy.types.Driver.html | M |
| BL-3 | Blender Manual source — F-Curve Modifiers | https://projects.blender.org/blender/blender-manual/raw/branch/main/manual/editors/graph_editor/fcurves/modifiers.rst | H |
| BL-4 | site-builder.wiki — Audio Visualizer [Blender] | https://site-builder.wiki/posts/39611 | L |
| ABL-1 | Ableton Reference Manual 12 — Racks | https://www.ableton.com/en/manual/instrument-drum-and-effect-racks/ | H |
| BW-1 | Bitwig User Guide — Modulators | https://www.bitwig.com/userguide/latest/modulator/ | H |
| BW-2 | polarity.me — Bitwig Modulation System | https://polarity.me/posts/bitwig-guides/2022-04-12-the-modulation-system-bitwig-modulator-guide/ | M |
| VIT-1 | Vital User Guide — Modulation (D. Vogel) | https://davidmvogel.com/docs/Vital/UserGuide/Modulation | M |
| SL-1 | SynthLab SDK — Modulation Matrix | https://www.willpirkle.com/synthlab/docs/html/mod_matrix.html | H |
| MM-1 | Attack Magazine — What is a modulation matrix; ResearchGate paper | https://www.attackmagazine.com/technique/synth-secrets/what-is-a-modulation-matrix-how-does-it-work/ ; https://www.researchgate.net/publication/228732325_A_modulation_matrix_for_complex_parameter_sets | M |
| OSC-1 | OpenSoundControl 1.0 Specification | https://opensoundcontrol.stanford.edu/spec-1_0.html | H |
| OE-1 | 1€ Filter project page (Casiez et al.) | https://gery.casiez.net/1euro/ | H |
