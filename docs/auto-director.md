# The Auto-director

(Renamed from "the camera director" and "Direct to Music" in the Glowmere Valley 2 work,
`docs/glowmere-valley-2/12-phase-8.md`. The internal identifiers -- `app::DirectorState`,
`camera_director.cpp` -- deliberately keep their names: three unrelated things in this codebase are
called a director, and a rename that reached into `app::WorldDirector`, `seq::Director` and
`entity::Authority::Director` would be a worse outcome than the old name.)

What "Enable Auto-director" does, what it can be relied on for, and where it stops. Written against the
code as of 2026-09-12; the decisions behind it are ADR-062, ADR-071, ADR-072, ADR-075, ADR-080 and
ADR-104 to ADR-107.

## The shape of it

Three inputs, one output:

```
  an analyzed track  ──▶  musical structure  ──┐
                                               ├──▶  Sequence  ──▶  timeline keys
  the scene's heroes ──▶  DirectionBrief    ──┘
```

- **`structureOfTrack`** folds the *whole* analysis into sections — Intro, Build, Phrase, Drop,
  Verse, Breakdown, FinalBuild, FinalDrop, Outro — with a detector of its own, so the same track
  gives the same structure whenever you press the button.
- **`briefFromHeroes`** takes the heroes in rank order: the first is the subject, the rest are the
  supporting cast.
- **`directHeroes`** turns each section into a shot.
- **`installSequence`** bakes the shots to keyframes and puts them on the timeline.

**Directing is a bake, not a per-frame decision.** A structure is a fold over a whole track, so it
cannot be known from the frame you are on — and keys are what make a directed camera identical
between a 120 Hz window and a 30 fps offline render.

## What it can do

**Cut to the music.** Each section becomes one shot, and the mapping is fixed and legible:

| Section | Shot | Why |
|---|---|---|
| Intro, Outro | Establish | nothing has happened; show where we are |
| Build | Discovery | a build is the camera setting off towards something |
| Phrase | Drift | an ordinary passage is the world going past |
| Drop | HeroReveal | the drop lands on the reveal it was built for |
| Verse | Transition | the section changed; leave what we were on and find another |
| Breakdown | Approach | quiet and close: the shot a loud section could not hold |
| FinalBuild | Ascent | the last run-up goes up, so the last drop has somewhere to fall from |
| FinalDrop | Reveal | the widest opening-out in the film, kept for the end |

**Land the reveal on the beat.** A drop always opens its own shot, exactly on the drop. If the shot
in front of it would have been a flash rather than a shot, its time is given back to the one before
instead of keeping a one-second cut that reads as a glitch.

**Cast the film.** The hero owns the builds and the drops — the arc that lands the reveal. Everything
else goes to the rest of the cast in a deterministic order seeded from the brief rather than drawn
from a PRNG stream, so adding one section does not re-cast everything after it. A structure with no
build and no drop puts the hero back in the rotation, because a film that never shows its subject is
not restraint either.

**Hold a shot, not a section.** A passage longer than `maxShotSeconds` (12 s) becomes several shots
inside the same section, each cast separately — so a thirty-second verse is the camera travelling
between three objects rather than one object for a third of the piece. Builds, drops and breakdowns
are never split: a build is one continuous move, a drop lands on its beat, and a breakdown is the
close quiet hold a loud section could not carry.

**Frame by size, not by number.** Shot distances are in subject radii, so the same shot reads
correctly on a two-metre artefact and a forty-metre tree. A hero's own `preferredCameraDistance`
overrides that when it has an opinion.

**Vary the approach.** Consecutive shots are offset by the golden angle, so the film is not nine
views down the same axis.

**Stay out of the scenery.** Baked camera paths are lifted clear of the terrain, the canopy and the
heroes themselves before installation (ADR-080). Glowmere's first directed pass went through the
trees.

**Drive seven things and replace only those.** `camera/mode`, `camera/position`, `camera/target`,
`camera/lens/focalLength`, `camera/lens/aperture`, `camera/lens/focusDistance` (and
`camera/focus/emphasis` when a build registers it). The mode is part of it because a composition
ignores `camera/position` and `camera/target` unless it is in free mode, and defaults to orbit. Any
existing track on those is replaced; every
other track in the project is left alone.

**One hero, one object.** A hero is the scene object of the same name (ADR-107) -- so every hero has
a row in World ▸ Objects, and every star is one click away from the thing it describes. Three objects
that should be one hero are a group, and the group is what you star.

**Follow the heroes afterwards.** Star or unstar an object while the camera is directed and the shot
re-cuts on the next frame with the same seed (ADR-105) — wherever the playhead is, because that is
somebody asking to see the result. Move a hero's object, or change what a hero is worth, and the
shot re-cuts a quarter-second after you let go (ADR-106) — but only while the transport is parked.
During playback a hero moving is the *world* moving (Glowmere's wanderer walks), and re-cutting the
whole film every time it settles is what once made the director look stuck on one hero.

**Be tuned per hero.** Expanding an object's row in World ▸ Objects gives its importance (which hero
is the subject), its aim offset (where *on* the object the camera looks) and its stand-off (how far
away a shot stops, which is what to raise when a shot ends up inside something). The controls are
inert until the object is starred. Each drag is one undo step, and the viewport mark shows the aim
point while the object is selected.

**Hand the camera back — or just take it.** Camera ▸ Hand Camera Back to the Viewport removes the
director's tracks and ends its claim, leaving the scene's own camera and every other piece of
automation intact. So does moving the camera by hand: an orbit, pan or look drag, the wheel, or
framing an object. Reaching for the camera is asking for it back, and the status line says it
happened. Automation you authored yourself is never deleted this way — a gesture that meets it says
which menu item to use instead.

**Be deterministic.** Same track, same heroes, same seed, same film — which is what makes an offline
render of a directed camera worth anything.

## What it cannot do

**No track, no film.** It needs a *precomputed* analysis of a whole file. A live audio input has no
future to fold, and a track the fold finds no structure in is refused by name rather than producing
an empty sequence.

**No heroes, no film.** Directing needs something to point at. Star an object in World ▸ Objects, or
generate a world.

**Ties cannot be resolved by default.** The subject — whoever gets the builds and the drops — is the first hero
by importance, and heroes designated from the editor all take the default 0.5, so among them the tie
breaks on the order you starred them in — until you set them. Expand an object's row in World ▸
Objects for its importance, its aim offset and its stand-off.

**Shots are not editable.** The director produces the whole sequence or none of it. There is no way
to keep eight shots and re-cut the ninth, to change one shot's kind, or to nudge a cut — the output
is keyframes, so the only editing surface afterwards is the timeline itself.

**Size does not follow the object.** A hero translated by moving its object keeps its declared
`radius` and `height`. Scale an object and its framing is still the old size until you unstar and
re-star it.

**A hero can outlive its object.** A hero is one object, matched by name. Rename or delete the object
and the hero stays where it was declared, still shot by the director; the World panel's Heroes list
is where you find and remove it.

**The path is cleared, not planned.** Clearance lifts keys out of the ground and the canopy. It is
not collision avoidance: it does not know about buildings, water, moving entities, or anything that
arrives after the shot is cut.

**The cut cannot see the world's own events — except one, on request (ADR-217).** The whole sequence
is folded from the music before a frame is drawn, so a shot can land in the middle of a staging
scenario (ADR-210) and the next one can walk out of it. `holdScenario` names one scenario; while it
holds `holdRole` bound and the playhead is in a shot cut for that scenario's actor, the camera keeps
that shot's framing on the actor and rides along with it, rejoining the cut over `holdRelease`
seconds afterwards. Off unless a scenario is named, and it does not change the cut itself: the keys,
the timings and every other shot are the same film. On the command line:
`--director holdScenario=abduction`; in the panel, *Stay with a scenario ▸ mid-event*.

**No re-cut during an offline render.** `refreshDirection` runs in the interactive loop only; a
render's heroes cannot change while it runs.

**One camera, no cameras.** There is no set of named cameras to cut between and no concept of a
second unit — the director owns *the* camera or it owns nothing.

## Using it

1. Load audio. Offline analysis runs at load, and the menu item stays disabled until it exists.
2. Star at least one object in World ▸ Objects — the panel's Heroes list shows what you have and in
   what order.
3. Camera ▸ Direct to Music.
4. Adjust by starring, unstarring and moving objects; the shot re-cuts itself.
5. Camera ▸ Hand Camera Back to the Viewport when you want the camera again.

## Known sharp edges

- **The subject is whichever hero ranks first**, and re-cutting is all-or-nothing, so an unstar can
  change the whole film rather than just removing one shot. That is intended, and it surprises
  people.
- **A re-cut folds the whole track** — a click's worth of work, not a frame's. It is why the move
  debounce exists, and why continuously moving a hero would never settle into a re-cut.
- **`camera/mode` is driven, not set.** The base value stays whatever the scene was authored with,
  which is what lets handing the camera back restore an orbit camera. A project saved while directed
  carries the mode track with it and reopens directed.
