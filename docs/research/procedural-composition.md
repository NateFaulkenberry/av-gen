# Procedural composition, hierarchy and art direction

Status: research (2026-09-09). Decisions: ADR-038, ADR-041.

## 1. The core problem

Procedural generation distributes things uniformly unless told otherwise. Uniform density,
uniform brightness, uniform scale and uniform motion are the four signatures of generated imagery.
The engine already has the machinery to place anything anywhere; what it lacks is any notion of
what matters in a frame.

## 2. Composition as data

The concepts worth making first-class, drawn from how technical artists work in Houdini and PCG:

- **Focal point / region**: a world-space position with a radius and a weight. Generators reduce
  density and clutter near it, lighting raises contrast on it, the camera frames it, and post can
  sharpen it.
- **Depth layers**: foreground, midground, background, far. Each layer gets its own density,
  contrast, saturation and detail budget. Atmospheric perspective then does the separation.
- **Negative space**: exclusion regions and a clearance radius around focal points. Emptiness is a
  positive instruction, not an absence of instructions.
- **Visual weight**: a per-object scalar that lighting, emission, size and motion respond to, so a
  hero reads as a hero without hand-tuning every parameter.

All of these are spatial fields in the sense the engine already has: a density field, an exclusion
field, a weight field. That is the cheapest possible integration, because point operators and
effectors already consume fields.

## 3. Scale and motion hierarchy

Rules of thumb that consistently separate authored from generated work:

- At least three visible scales in frame, roughly an order of magnitude apart: dust, human-scale
  detail, monumental structure.
- Motion frequencies spread the same way: particles at seconds, mechanisms at bars, architecture
  at phrases, atmosphere and camera at the whole piece. Everything moving at the beat is the
  visualiser failure mode the brief calls out.

## 4. Art direction as a control layer

Unreal's post-process volumes, Notch's global controls and colour-grading LUT stacks all converge
on the same idea: a small set of artistic words mapped onto many technical parameters. The
engine already has exactly the right mechanism for that in world macros, which expand into
ordinary routes. A World Director is therefore not a new system: it is a curated set of macros
with names like drama, warmth, mystery and depth, plus a look preset that captures a whole
configuration.

## 5. Musical structure

The brief asks for phrase-level structure, not beat-level reaction. The engine has beats, bars and
a timeline; what it lacks is a phrase signal. A phrase counter derived from bars (with an
adjustable length, typically 4, 8 or 16 bars) plus a section index gives the state machine
something to key transitions to, so escalation happens over musical time rather than every kick.

## 6. The 80/15/5 guideline

The brief's ratio is a good default: most motion autonomous, some audio-influenced, a little
event-driven. In practice this means routes with slow envelopes on macros, not fast routes on
individual object parameters, and reserving onsets for genuine punctuation.
