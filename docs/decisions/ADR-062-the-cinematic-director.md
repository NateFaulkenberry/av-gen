# ADR-062: The cinematic director

Status: Accepted

## Context

The camera has a timeline, splines that can be rails, a lens with real focal lengths and apertures,
and cues. What it has never had is the vocabulary above them: that a camera move is a *kind* of
move with a subject, and that a film is a list of them. Every shipped scene animates
`camera/position` with hand-written keys, which is why every camera in this project so far has been
some variety of approach-and-retreat.

## Decision

**A sequence bakes into timeline keys.** It does not drive the camera and does not run per frame:
it is evaluated once into `camera/position`, `camera/target` and the lens tracks, and from then on
the timeline the engine already has does the work. Scrubbing, deterministic playback and offline
rendering all keep working because nothing new is in the frame loop.

**Distances are in subject radii.** A shot says "start at six radii, end at one", so the same shot
works against a two-metre subject and a sixty-metre one. That is why `FocalTarget` carries a radius
and why a subject with no size is rejected rather than defaulted.

**Nine kinds, and orbit is one of them.** Establish, Approach, Reveal, Entry, Passage, Descent,
Ascent, Orbit, Track. Each supplies defaults for distance, sweep and elevation, so authoring a shot
is naming its kind and its subject. Orbit is in the list because a changing silhouette is sometimes
the point — but it is one of nine, not the fallback.

**Every kind aims at its subject except Passage**, which aims where it is going: looking back at
what you are flying through reads as a mistake.

**Keys carry already-eased values and interpolate linearly.** Handing eased values to a smooth
interpolator eases them twice and flattens every move's ends.

**The lens gets one key per shot.** A focal length that slides through a shot is a zoom, and a zoom
should be asked for rather than arrived at.

**A shot with no explicit start follows the previous one**, which is how a cut list is written by
hand and the only reason a nine-shot sequence is readable. Overlapping shots are refused: two
cameras at once is not something a single-camera engine can honour, and quietly picking one is
worse than saying so.

## Consequences

Pure geometry and JSON; no device needed, so it is unit-tested rather than GPU-tested.

What it does not yet do: `CompositionProfile.framing` and `headroom` are carried and serialised but
not applied — putting a subject at a third of the frame requires offsetting the aim by a
field-of-view-dependent amount, which wants the camera's aspect and is the obvious next increment.
`focusOnSubject` likewise is recorded and not yet wired to the focus distance.
