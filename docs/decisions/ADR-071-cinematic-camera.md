# ADR-071: A camera vocabulary, and a camera cut to the music

Status: accepted
Date: 2026-09-10

Supersedes nothing. Extends ADR-062 (the cinematic director) and ADR-063 (musical events).

## Context

ADR-062 gave the camera nine named moves and a way to bake them into timeline keys. Two things were
still missing.

The first is vocabulary. Nine kinds sounds like a lot until you try to write a shot list: there was
no way to say "come at it from the side so it emerges from behind the trees" (an approach comes
straight down the barrel and only makes the obstruction bigger), no way to say "pass it at speed
while holding it in frame" (a passage looks where it is going), no way to say "slide sideways
through the ferns" (an orbit pans to hold its subject, which cancels the parallax that was the
point), and no way at all to leave one subject and find another. Every one of those came out as
some flavour of approach, retreat or orbit — which is the same complaint ADR-062 was written to
answer, one level up.

The second is that a sequence still had to be written by hand, as times. The engine has known the
difference between a beat and a drop since ADR-063 and none of that reached the camera. The
temptation with an audio-reactive camera is well documented and always the same: shake on every
onset, cut on every phrase, a move on every beat. That is the "particle screensaver" failure with a
lens on it, and it is what most of the decisions below exist to prevent.

The reference this work is measured against is the Glowmere camera in the audit, section 1.4: free
mode, two timeline tracks, eight keys each over ninety seconds, forward through a valley and rising
twelve metres. It never orbits, never zooms, and holds its final pose. Section 8 lists that
restraint among the things not to change.

## Decision

**Five kinds added to the nine, not a second system.** Discovery, HeroReveal, Flyby, Drift and
Transition join Establish, Approach, Reveal, Entry, Passage, Descent, Ascent, Orbit and Track. The
spec's other words map onto what already existed — "Follow" is Track, "Establishing" is Establish —
and `shotKindFromName` accepts those spellings as one-way aliases so a shot list written in a
director's words parses, while `shotKindName` keeps returning the canonical name so a sequence does
not change spelling each time it round-trips.

**Where the camera points is a separate decision from where it is.** `LookMode` replaces the
hard-coded rule "everything aims at its subject except Passage". Subject, Ahead, Fixed, Parallel and
Handoff. `Parallel` is the one that could not be expressed before: the aim direction is frozen at
whatever it was on the first frame, so the camera translates and the world slides past. Panning to
hold something during a lateral move cancels exactly the parallax the move was for.

**Path shape is a separate decision from timing.** `easeIn`/`easeOut` shape *when* the camera
covers its ground; `MovementCurve` and a bow fraction shape *where* it goes. A straight line between
two points reads flat however well it is eased. The bow is a sine profile rather than a parabola
because its slope at the ends is finite: a parabola meets the chord at an angle and the camera
visibly kinks into its first frame. For Flyby the bow is not decoration — interpolating a distance
from positive to negative puts the midpoint exactly on the subject, and the bow is what keeps the
camera outside the thing it is flying past.

**Unset means "whatever this kind does".** `look`, `curve` and `curveBow` are optionals resolved
against the kind's defaults rather than fields with concrete defaults, and only what a shot actually
asked for is serialised. Writing a resolved default back out would freeze it: the shot would stop
following its kind the first time it round-tripped, which is exactly the bug that makes a
"defaults" system rot.

**A shot may be authored as two absolute points and a height range.** Distances in radii are what
make a shot reusable, and they are the wrong unit for a shot about a *place*: a valley traverse is
"two metres up, rising to fourteen", and a valley has no radius to divide that by. Any of
`startPosition`, `endPosition` and `heightRange` replaces the derived value; unset, nothing changes.
`heightRange` is a pair or nothing, because a half-given range would have to invent its other end
from the polar elevation it was brought in to replace.

**A shot may be paced instead of timed.** `speed` in metres per second, with `Sequence::retime()`
deriving the duration from the sampled path length and repacking the cut list. Repacking is
all-or-nothing: a sequence in which some shots are timed and some are paced cannot have both be
authoritative.

**Spotlighting is on the shot, not on the subject.** `Spotlight{active, emphasis}` says "this object
is the point of this shot" and nothing more — framing, exposure and rim light belong to whatever
reads it. It lives on the shot because the same object is a hero in one shot and scenery in the
next, which is the whole idea. It is published two ways: as spans (`spotlightSpans()`), because a
lighting change wants to know when it starts and ends rather than what it is halfway through, and as
a `camera/focus/emphasis` track with two keys per shot, so emphasis holds flat and changes at the
cut instead of ramping the hero's importance across the shot before it.

**A hero shot in which the hero cannot be seen is refused.** `subjectCoverageAt` turns radius,
distance and focal length into the fraction of frame height the subject spans, and `validate()`
rejects a spotlit shot that never crosses six per cent of it. Deliberately generous: it catches "the
hero is a speck", not "the hero could be bigger". Without it that mistake only appears in a render.

**Focus distance is now wired.** ADR-062 recorded `focusOnSubject` and left it unconnected; the
sequence now emits `camera/lens/focusDistance`, per sample. Per sample and not per shot, unlike
focal length: a rack focus that follows the subject through a move is not a zoom, it is the lens
doing the one thing it must to keep the subject sharp.

### Structure (ADR-063's second half)

**Events are instants; a film is cut to spans.** `MusicalStructure::fromMoments` folds recognised
moments into named sections — Intro, Build, Phrase, Drop, Verse, Breakdown, FinalBuild, FinalDrop,
Outro — and every rule in the fold is subtractive:

- Beat, Downbeat, BarStart and PhraseStart never open a section. A structure with a boundary every
  five hundred milliseconds is not structure, it is the metre wearing a different name.
- EnergyRise and EnergyDrop never open one either. A trend *describes* a section; it does not bound
  one. Their entire influence is on the section's intensity.
- Phrase starts say where a boundary is *allowed* to land, not where one is. A boundary within the
  snap window moves onto the phrase, because a cut a beat and a half after the phrase turned over
  reads as a mistake to people who could not name what is wrong with it.
- Two boundaries closer than the merge window are one boundary, and priority decides which survives
  rather than time order — the detector emits Break *before* the Drop it resolves into, so a
  first-wins rule would keep the wrong one every single time.
- A section too short to hold a shot is not a section, with one exception: a drop is never absorbed,
  and a build feeding a drop may be brief, because a build exists to end.
- The last build and the last drop are promoted, but only past the point in the piece where "last"
  means something. A single drop ten seconds in is a drop, not a finale.

### Direction

**`directFromStructure` maps sections to moves.** Intro and Outro establish, a build is Discovery
(the camera setting off towards something), a phrase is Drift, a *drop lands on a HeroReveal*, a
verse is a Transition, a breakdown is a close Approach, the final build ascends and the final drop
gets the widest opening-out in the film. The table is exported as `shotKindForSection` because it is
the most arguable thing in the file and a caller who disagrees should be able to read it.

**One hard rule: a drop opens its own shot, exactly on the drop.** Every other grouping rule says
"do not cut that soon" and the drop overrides all of them. Smoothing that cut away from the moment
the music lands is smoothing away the entire reason for reading the structure.

**Everything else is a refusal to cut.** A section that would give a shot shorter than
`minShotSeconds` is absorbed into the shot in front of it rather than becoming a cut. A one-second
shot in front of a drop gives its second back rather than leaving a flash. `cutsPerMinute()` and
`validateCadence()` make the result checkable — cadence is separate from `validate()` because it is
a judgement and geometry is not: a hand-authored sequence may legitimately want a two-second shot
and a generated one may not.

**There is no shake, anywhere, and there is not going to be one.**

**Continuity is the default.** Each shot's start is pinned to the previous shot's end, so a directed
sequence is one unbroken move whose *intent* changes at the section boundaries rather than a cut
list — which is the audit's camera, and section 8 says not to change it. The cost is real: a pinned
start replaces the kind's polar path with a line to the kind's end, so a shot that was an arc becomes
a chord, and the straight ones are given a bow to buy the parallax back.

**A breakdown is the one place a cut belongs.** The music has stopped, so the cut is invisible;
carrying a continuous camera across a break makes the break look like nothing happened, and it also
forces the quiet shot to sprint in from wherever the last loud one finished. Slow and close cannot
be reached at a run.

**Casting is a deterministic walk, not a draw.** The hero owns the payoffs, the run-ups to them and
the wide shots that bracket the film; the supporting cast gets everything in between, because a film
in which every shot is of the same object has no scale — the hero is only big if something else was
small. The seed picks a starting offset into the cast and the walk advances by one. A PRNG stream
would make every later choice depend on how many earlier ones were made, so adding one section would
re-cast the rest of the film.

## Consequences

All of it is pure geometry, arithmetic and JSON. `cinematic.cpp` stays in the GPU-free unit test
target and `musical_events.cpp` stays in `avgen_core`; the structure fold takes a span of moments
rather than reaching for the detector, so the whole path from synthetic signal frames to a shot list
is testable without an audio device, a window or a GPU.

The existing nine kinds are untouched: their defaults, their paths and their aims are what they
were, and every ADR-062 test still passes on its original assertions. The one behavioural change to
existing output is that `toTimelineTracks` now emits six tracks instead of four.

What this does not do:

- `CompositionProfile.framing` and `headroom` are still carried, serialised and not applied. Putting
  a subject on a third needs the camera's aspect, and this work did not need it.
- `Spotlight` is the camera's half only. Nothing yet reads the spans to bias exposure or to light a
  silhouette; that is the next increment and the reason the spans are published as spans.
- The section→shot table has been reasoned about and unit-tested. It has not been watched against
  ninety seconds of music with a render attached, and no claim is made that it is *good* direction —
  only that it is direction that does not cut constantly, does not move on the beat, and lands the
  reveal on the drop.

## Revisit triggers

- A director-generated sequence that reads as restless in a real render, which would mean
  `minShotSeconds` is the wrong instrument and the fix is per-kind minimums.
- Framing being applied, which turns `subjectCoverageAt` from a validation check into an input to
  the aim and probably moves the hero-coverage floor.
- A second camera, or any form of cross-fade between shots, which would break the "shots may not
  overlap" rule ADR-062 rests on.
