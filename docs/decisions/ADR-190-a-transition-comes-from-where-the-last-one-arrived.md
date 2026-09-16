# ADR-190: A transition comes from where the last one arrived

**Status:** Accepted
**Date:** 2026-09-14

## The report

*"In Glowmere Valley 2 the auto director seems to get stuck focusing only on one hero during the
middle of any song... around the 45 second mark, it will continue to spin around a single hero for
about a minute before moving on. I've encountered this exact same behaviour in previous versions of
Glowmere Valley as well, and with other tracks that vary in length — still seems to happen around
the same time every time."*

## What the shot list actually said

Measured rather than reasoned about — a debug line listing every shot, its span and its subject:

```
shot  1 intro-1     0.00s + 3.45s  ember-cap
shot  2 drop-2      3.45s + 3.63s  elder-2-cap
...
shot 20 drop-20    84.89s +33.77s  elder-2-cap
shot 21 verse-21  118.66s + 9.78s  elder-2-cap
...
shot 26 verse-26  171.61s +11.80s  elder-2-cap
```

**Twenty-five of twenty-six shots on one object, in a world declaring eleven heroes.** Worse than the
report, and the report was of the part a viewer notices.

## Two faults, and they compound

### A transition read the wrong end of itself

A `Transition` shot takes the previous subject and puts the one it is travelling *to* in `handoff`,
so its own `subject` names **where it started**. The next transition then did this:

```cpp
if (i > 0 && seq.shots.back().subject.name != shot.subject.name) {
    shot.handoff = shot.subject;
    shot.subject = seq.shots.back().subject;
}
```

`seq.shots.back().subject` is where the last shot *started*, not where it *arrived*. So each
transition left the same object it had left last time, handed off to somebody new, and the next one
started from the object it had supposedly departed. **The chain never advanced.** Six verses in a row
came out on one hero — for ever, on any structure with consecutive transitions.

Fixed by tracking where the camera actually is: a transition ends on its handoff, everything else
ends on its subject.

### The hero's ownership had no ceiling

The hero owns every build and drop by design — that is the arc the film is about, and holding the
hero back until then is the shape of a reveal. The design assumes a structure with a *few* of them.

Glowmere's analyzer folded this track into **nineteen consecutive drops**, and the rule then handed
the entire middle of the film to one object. Whether nineteen drops is a good reading of the music is
the analyzer's business; the director has to stay watchable when it gets one.

So ownership is now a preference rather than a right: after four hero shots in a row the next section
goes to the supporting cast whatever its kind. On a structure that alternates this changes nothing —
the run never reaches four.

## Result

Same project, same track: **17 shots on the hero instead of 25, with nine other heroes now cast**, and
the verses rotating properly through the cast rather than all landing on one object.

## What is deliberately not fixed

One shot in that list is **33.77 seconds long** — a single hold on the hero, which is a good part of
what "spins around a single hero for about a minute" describes. It is long because `mayBeSplit`
exempts a drop from the maximum shot length, on the stated grounds that "a drop has to land on the
beat it was built for".

Capping that exemption fixes it, and contradicts two tests that assert the present contract in as
many words: *"The drop is one shot from 41 s to the end"*, and a cadence test bounding the shot
count. Those tests are not incidental — they were written to stop the director cutting into a move
that exists as one move.

So this is **a decision about what a drop is**, not a defect, and it is left to be made rather than
made here. The change is one constant if it is wanted: split a section whatever its kind once it runs
past twice the maximum shot length, with the first piece still starting exactly on the section
boundary so the drop still lands on its beat.

## The diagnostic that found it

None of this was visible from the summary the director already logged — "26 shot(s), 6 track(s)
installed". The per-shot line is now permanent at debug level, because "why does the film stay on one
object" cannot be answered from a count, and for as long as it could not be answered the behaviour
survived across several versions of the scene and several tracks.
