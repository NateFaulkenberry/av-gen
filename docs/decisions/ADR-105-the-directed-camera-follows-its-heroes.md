# ADR-105: A directed camera follows its heroes until somebody else takes it

Status: accepted
Date: 2026-09-12

## Context

ADR-075 made directing a **bake**: `installSequence` writes the shot onto the timeline as ordinary
keyframes, and from that moment the camera is those keys. That is the right shape — it is what makes
a directed camera scrubbable, renderable offline and identical on every run — but it left the shot
frozen against the hero set it was cut from.

ADR-104 then made heroes one click away. The two together produced a bad loop: star an object,
nothing happens; hand the camera back to the viewport; direct it again; look. Two menu items to see
the effect of one click, and the natural reading of the silence in between is that the star is
broken.

## Decision

### The bake stays; noticing that it is stale becomes automatic

`refreshDirection(engine, state)` runs once a frame. It compares `Composition::heroRevision()`
against the revision the shot was cut from and, when they differ, cuts the shot again from the heroes
as they are now, with the same seed. Until something changes it is an integer comparison.

The director is deliberately **not** made live. A camera that re-derives itself per frame is not
scrubbable, is not the same twice, and cannot be rendered offline from a file — which is the whole
of what ADR-075 bought.

### A revision counter, not a comparison of the lists

`setHeroes` bumps a counter. What reads it is asking "is the shot I cut still the shot these heroes
describe" — a question about *when*, not about which fields differ — and comparing two vectors of
heroes every frame to answer "no" is work nobody needs done.

### The claim ends when the camera stops being the director's

`DirectorState` is set when the shot is cut and cleared when the camera is handed back. It is also
cleared by `refreshDirection` itself whenever `camera/position` is no longer automated, so a project
load, an undo, or a track deleted by hand ends the claim without any of those places having to know
that a director exists. Self-healing rather than notified: there is exactly one place that can be
wrong, and it checks itself.

The claim is per-session and is not saved. A directed shot round-trips as what it is — timeline
keys — and a project reopened is a project whose camera is authored, not one still being directed.

### The last hero going hands the camera back

An empty hero set is not an error to report and not a shot to keep. Leaving the last trajectory
running would fly the camera along a path towards something the user has just said is not there, so
the director's tracks are removed and the viewport has the camera again.

## Rejected alternatives

- **Re-cut on a timer, or on every frame.** The fold is over the whole track; doing it when nothing
  has changed is pure waste, and doing it repeatedly under a scrub would fight the transport.
- **A dirty flag set by the editor's hero commands.** It would have to be set by every writer of the
  hero list — the editor, the world builder, a project load, the AI control plane — and the one that
  forgets is a stale shot nobody can explain. The counter is on the data.
- **Re-cutting when a hero's *object* moves.** A hero carries its own position, so moving the node it
  was measured from does not change the hero, and this does not pretend otherwise. Making the
  declaration track the object is a real question and a separate one.
- **Asking first ("heroes changed — re-cut?").** A confirmation for an action that is instant,
  visible and undone by starring the object back is a dialog in the way of the work.

## Consequences

- Starring while directed re-cuts on the next frame; the status line says so, and the temporal
  history is reset because the camera has jumped.
- Every re-cut is a full fold of the track. That is a click's worth of work, not a frame's, but it
  is the cost that decides how often this can be allowed to run — hence the counter.
- The offline render loop does not call it: a render's heroes cannot change while it runs.

## Revisit triggers

- A hero list that changes continuously — a generator tuned live, say — at which point the fold
  needs caching against the track rather than being redone per change.
