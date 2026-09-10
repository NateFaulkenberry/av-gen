# ADR-075: The camera director, connected

Status: accepted
Date: 2026-09-10

## Context

ADR-062 built a shot vocabulary. ADR-071 extended it, added a fold from musical moments into
sections, and added a director that turns sections into a `Sequence` which bakes down to ordinary
timeline keys. All of it was implemented and tested.

None of it was reachable. Before this change, `src/app/cinematic.hpp` was included by exactly one
other file in the repository: its own test. A camera director that nothing calls is a camera that
never moves, and a test suite that is green about it is a test suite measuring the wrong thing.

## Decision

Three small steps rather than one function, because each is useful alone: a brief can be inspected
before it is directed, a sequence can be looked at before it is installed, and only the last step
touches the engine.

### Directing is a bake, not a per-frame decision

A musical structure is a fold over a *whole track* — where the builds and drops are — so it cannot
be known from the frame you are on. Baking to timeline keys is also what keeps a directed camera
identical between a 120 Hz window and a 30 fps offline render, which for a deterministic engine is
the only acceptable answer. Nothing here runs per frame and nothing runs on the audio thread.

### The brief comes from the heroes, in their own order

`world::HeroPoint`s arrive ranked, and ADR-072 makes that ranking strictly descending. So the film's
subject is simply the first and the rest are its supporting cast in order. That is the whole reason
importance had to be strict: a director cannot resolve a tie, and would otherwise choose by array
position, which is not a decision anybody made.

A hero's *horizontal* radius alone is the wrong thing to hand a camera. A twenty-metre tree half a
metre wide would be framed as if it were half a metre across, putting the camera inside the canopy —
so the subject's radius is the larger of its half-width and its half-height.

### Installing replaces the camera's tracks and nothing else

Two tracks writing `camera/position` is not a blend. It is whichever the timeline applies last,
which presents as the director being ignored. So installing removes every track targeting a
parameter the director owns.

Everything else survives. A project's automation of anything that is not the camera is somebody's
work, and directing the camera is not a reason to discard it.

The set of owned parameters is stated once, and a test walks the installed tracks checking each is
on that list. That test earned its place immediately: the first version of the list named four
targets and the bake emits six, so a second direction would have left the previous one's focal
length and aperture in place beside the new ones. A list that appears twice is a list that will
disagree with itself; this one appears once and is checked against reality.

Directed tracks are parsed through the timeline's own JSON reader rather than constructed by hand,
so a directed track and an authored one are the same kind of object and cannot drift apart in what
they support.

## Consequences

- `camera_director.cpp` links against `Engine`, and `engine.cpp` is already in both test targets, so
  the join is tested without a GPU — including that installing puts keys on a real engine's
  timeline and that the camera actually travels rather than holding one pose.
- A world with no heroes is a failure with a cause rather than an empty sequence. Returning
  something valid-looking that shoots the origin would hide the problem until somebody watched a
  render.

## Not done here

Nothing yet *calls* this with a real track's structure, because building a structure needs musical
moments and nothing runs the detector at runtime. That is ADR-073's job and is in flight; this ADR
covers the join from heroes and a structure to a moving camera, and the two meet at
`signals::MusicalStructure`.
