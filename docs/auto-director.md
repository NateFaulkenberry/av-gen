# The Auto-director

(Renamed from "the camera director" and "Direct to Music" in the Glowmere Valley 2 work,
`docs/glowmere-valley-2/12-phase-8.md`. The internal identifiers -- `app::DirectorState`,
`camera_director.cpp` -- deliberately keep their names: three unrelated things in this codebase are
called a director, and a rename that reached into `app::WorldDirector`, `seq::Director` and
`entity::Authority::Director` would be a worse outcome than the old name.)

What "Enable Auto-director" does, what it can be relied on for, and where it stops. Written against
the code as of 2026-09-16; the decisions behind it are ADR-062, ADR-071, ADR-072, ADR-075, ADR-080,
ADR-104 to ADR-107, ADR-200 to ADR-203, ADR-245 and ADR-249.

## Three modes, and what actually differs between them

The Auto-director cuts in one of three ways. They are **not three styles of the same thing**: they
differ in what they read.

| Mode | Reads | Decides | Output |
|---|---|---|---|
| **Continuous shot** | a fold of the audio into musical sections | one uninterrupted move whose *intent* changes at section boundaries | timeline keys on one camera |
| **Edited sequence** | the same fold | a cut list: each shot composed independently | timeline keys on one camera |
| **Song** | an **authored song plan** — sections, each with a shot intent | which camera, what it frames, how it moves, and when to cut | timeline keys **and a camera shot track** |

Continuous and Edited fold the music themselves and decide everything from the section *kind*
(`signals::MusicalSection`). Song Mode does not fold anything: the fold has already happened, a
person has edited the result, and what arrives is a plan. That is why Song Mode can direct a project
whose audio is not even loaded, and why it is the only mode that uses more than one camera.

## Continuous shot and Edited sequence

Three inputs, one output:

```
  an analyzed track  ──▶  musical structure  ──┐
                                               ├──▶  Sequence  ──▶  timeline keys
  the scene's heroes ──▶  DirectionBrief    ──┘
```

- **`structureOfTrack`** folds the *whole* analysis into sections — Intro, Build, Phrase, Drop,
  Verse, Breakdown, FinalBuild, FinalDrop, PreChorus, Chorus, Break, Bridge, Instrumental,
  FinalChorus, Outro — with a detector of its own, so the same track gives the same structure
  whenever you press the button.
- **`briefFromHeroes`** takes the heroes in rank order.
- **`directHeroes`** turns each section into a shot.
- **`installSequence`** bakes the shots to keyframes and puts them on the timeline.

**Directing is a bake, not a per-frame decision.** A structure is a fold over a whole track, so it
cannot be known from the frame you are on — and keys are what make a directed camera identical
between a 120 Hz window and a 30 fps offline render.

**Cut to the music.** Each section becomes one shot, and the mapping is fixed and legible:

| Section | Shot | Why |
|---|---|---|
| Intro, Outro | Establish | nothing has happened; show where we are |
| Build | Discovery | a build is the camera setting off towards something |
| Phrase, Break | Drift | an ordinary passage is the world going past |
| Drop, Chorus | HeroReveal | the payoff a build was for |
| Verse, Bridge | Transition | the section changed; leave what we were on and find another |
| Breakdown | Approach | quiet and close: the shot a loud section could not hold |
| PreChorus | Approach | the run-up to a chorus is shorter than a build: close the gap |
| Instrumental | Orbit | the voice is out; the silhouette is what there is to look at |
| FinalBuild | Ascent | the last run-up goes up, so the last drop has somewhere to fall from |
| FinalDrop, FinalChorus | Reveal | the widest opening-out in the film, kept for the end |

**Land the reveal on the beat.** A drop or a chorus always opens its own shot, exactly on the beat.
If the shot in front of it would have been a flash rather than a shot, its time is given back to the
one before instead of keeping a one-second cut that reads as a glitch.

**Cast the film.** One cast, ranked, and importance is the whole of it (ADR-202): every subject
accrues its own importance each shot, the shot goes to whoever has accrued the most, and that
subject gives back the total. The turns are *spread* rather than clumped. `dwellShots` (ADR-203)
sets how many shots in a row one subject keeps once its turn comes.

**Hold a shot, not a section.** A passage longer than `maxShotSeconds` becomes several shots inside
the same section, each cast separately. Builds, drops, choruses, pre-choruses, breaks and breakdowns
are never split.

**Frame by size, not by number.** Shot distances are in subject radii, so the same shot reads
correctly on a two-metre artefact and a forty-metre tree. A hero's own `preferredCameraDistance` and
`preferredCameraElevationDegrees` bias that when it has an opinion.

**Vary the approach.** Consecutive shots are offset by the golden angle around the subject's own
preferred bearing, so the film is not nine views down the same axis.

**Stay out of the scenery.** Baked camera paths are lifted clear of the terrain, the canopy and the
heroes themselves before installation (ADR-080).

**Hold a pace.** `maxCameraSpeed` and `maxViewRate` (ADR-200) cap how fast the camera travels and how
fast the *view swings*, which measurement showed are two separate problems. What gives way is the
distance, never the timing — a cut here lands on the music.

## Song Mode

```
  a song plan (sections + shot intents) ──┐
  the scene's heroes                    ──┼──▶ SongDirection ──┬──▶ timeline keys (framing)
  the cameras marked available          ──┘                    └──▶ camera shot track (which camera)
```

**A shot intent is six numbers and a name, and the director never reads the name.**

```
hero       0 the environment   .. 1 the subject
distance   0 intimate          .. 1 the widest this world offers
movement   0 locked off        .. 1 constantly travelling
variation  0 one setup held    .. 1 keep finding new ones
cutRate    0 the longest hold  .. 1 the shortest
cameras    how many viewpoints the section wants used
```

The name — "Hero Performance", "Ocean Ambience", "Atmospheric Establishing" — is carried for log
lines and panel rows and is compared against nothing. That is not a convention: `app::SongPlan`
(`src/app/song_plan.hpp`) is the only way a song reaches the director, and it carries no section
type, no musical function and no vocabulary of any kind. `tests/unit/test_song_director.cpp`
scrambles every label and every intent name in a plan and requires the resulting film to be
identical, which is a guarantee a `switch` on a label cannot survive.

**What Song Mode decides** — and what the plan therefore does not have to say:

- **Which camera.** Every camera ticked *available to the Auto-director* is scored against the
  intent on two axes: how wide it is (its focal length, or its field of view when it states none)
  and how much it is *about a subject* (a camera with a `followNode` or `aimNode` is watching
  something; the Auto-director's own camera frames whatever the shot is of; a placed viewpoint is a
  shot of a world). The intent's `cameras` asks for a number of them; the world may not have that
  many, and then the director says so rather than inventing one.
- **How many cuts, and where.** `cutRate` places the shot length in the band between
  `minShotSeconds` and `maxShotSeconds`, and the section's own measured energy and density move it
  within that band when the autonomy allows.
- **What each shot is of.** The same weighted cast rotation the other two modes use, leaned towards
  or away from the film's subject by `hero`.
- **The move, and the framing.** `hero`, `distance` and `movement` together choose the shot kind;
  `distance` places the camera in the band between 2.6 and 14 subject radii; `movement` sets how far
  it travels and how far round it sweeps.

**Song Mode is not shot playback.** Two sections carrying a byte-identical intent come out as
different films, because every decision is also a function of the section's `occurrence` — which
time round this material is. The opening camera is a *guarantee*: a second pass over the same intent
never opens on the same camera it did the first time, when the section has more than one to open on.
The rest — the subject, the framing, the bearing — varies by a hash of the same coordinates.

None of that costs determinism. Everything is decided once, at bake time, from
`(seed, intent shape, occurrence, shot index)`. Nothing is drawn from a stream and nothing is decided
per frame, so a seeked second is still a function of the second (ADR-091). See ADR-249.

**Autonomy.** Three levels, and the distinction is about what the *section* fixes:

- **Locked** — one shot, one camera, framed at the intent's nominal values. Song Mode behaving like
  an authored shot list, for the moments somebody composed.
- **Guided** — the intent is respected and the execution is the director's.
- **Expressive** — ...and the director also reads the section's own energy and density, so a loud
  section gets more coverage than a quiet one carrying the same intent.

The panel's *Director freedom* is a **ceiling**, not a setting: the effective autonomy of a section
is the lesser of it and the section's own. Lowering it can always be trusted to make the film more
faithful to what was authored.

**It uses the multi-camera system; it does not replace it.** Song Mode *authors*
`scene::CameraShot`s on the composition's existing shot track, exactly as a person does in the
Cameras panel. `scene::resolveActiveCamera` remains the one thing in the engine that says which
camera is live (ADR-245), and an event camera still outranks a directed shot unless that shot's
section was Locked — so an abduction happening during a chorus still takes the frame.

**What it owns, and what it leaves alone.** A directed camera shot carries
`CameraShot::Origin::Directed`. Re-directing replaces every one of those and leaves every shot a
person authored exactly where it was; handing the camera back removes them. The identical rule the
timeline half has always followed, for the identical reason: two cuts on one track is not a blend.

## What all three modes do

**Drive seven things and replace only those.** `camera/mode`, `camera/position`, `camera/target`,
`camera/lens/focalLength`, `camera/lens/aperture`, `camera/lens/focusDistance` (and
`camera/focus/emphasis` when a shot registers it). The mode is part of it because a composition
ignores `camera/position` and `camera/target` unless it is in free mode. Any existing track on those
is replaced; every other track in the project is left alone.

**One hero, one object.** A hero is the scene object of the same name (ADR-107) — so every hero has
a row in World ▸ Objects, and every star is one click away from the thing it describes.

**Follow the heroes afterwards.** Star or unstar an object while the camera is directed and the shot
re-cuts on the next frame with the same seed (ADR-105). Move a hero, or change what it is worth, and
the shot re-cuts a quarter-second after you let go (ADR-106) — but only while the transport is
parked. During playback a hero moving is the *world* moving, and re-cutting the whole film every time
it settles is what once made the director look stuck on one hero.

**Be tuned per hero.** Expanding an object's row in World ▸ Objects gives its importance, its aim
offset and its stand-off. The controls are inert until the object is starred.

**Hand the camera back — or just take it.** Camera ▸ Hand Camera Back to the Viewport removes the
director's tracks, its camera shots and its claim, leaving the scene's own camera and every other
piece of automation intact. So does moving the camera by hand: an orbit, pan or look drag, the wheel,
or framing an object. Automation you authored yourself is never deleted this way.

**Be deterministic.** Same seed, same heroes, same inputs, same film — which is what makes an offline
render of a directed camera worth anything.

## What it cannot do

**Continuous and Edited need a track.** A *precomputed* analysis of a whole file. A live audio input
has no future to fold, and a track the fold finds no structure in is refused by name. **Song Mode
does not** — it needs a plan, which is either the project's own or one derived from the analyzed
structure.

**No heroes, no film.** Directing needs something to point at. Star an object in World ▸ Objects, or
generate a world.

**Song Mode needs a camera.** At least one camera in the scene must be ticked *available to the
Auto-director*, and it is refused by name if none is. (The main camera is one; ticking it is enough,
and a world with only that camera directs correctly and looks like Edited sequence — because that is
what it is.)

**Five shot kinds are out of Song Mode's reach.** `Entry`, `Passage`, `Descent`, `Ascent` and
`Flyby` are moves about a *direction in the world* — into, through, down, up, past — and none of the
six intent axes expresses a direction. They remain available to an authored shot and to the other two
modes. Adding a seventh axis to complete a table would be adding a knob for the wrong reason.

**Ties cannot be resolved by default.** The subject is the first hero by importance, and heroes
designated from the editor all take the default 0.5, so among them the tie breaks on the order you
starred them in.

**Continuous and Edited shots are not editable.** The director produces the whole sequence or none of
it: the output is keyframes, so the only editing surface afterwards is the timeline. **Song Mode is
the answer to that** — what is editable there is the plan, one level up, which is where an editing
decision belongs.

**Size does not follow the object.** A hero translated by moving its object keeps its declared
`radius` and `height`. Scale an object and its framing is still the old size until you unstar and
re-star it.

**A hero can outlive its object.** A hero is one object, matched by name. Rename or delete the object
and the hero stays where it was declared.

**The path is cleared, not planned.** Clearance lifts keys out of the ground and the canopy. It is
not collision avoidance: it does not know about buildings, water, moving entities, or anything that
arrives after the shot is cut.

**The cut cannot see the world's own events, except through a camera.** A Continuous or Edited shot
can land in the middle of a staging scenario (ADR-210) and the next one can walk out of it. The
answer since ADR-245 is an *event camera*: a camera whose `eventScenario` names the scenario takes
the frame while it runs, and gives it back by falling through the resolver. (ADR-217's aim hold did
this approximately, for one camera, and has been retired — `holdScenario`, `holdRole` and
`holdRelease` no longer exist in the settings, the project block or `--director`.)

**No re-cut during an offline render.** `refreshDirection` runs in the interactive loop only; a
render's heroes cannot change while it runs.

## Using it

### Continuous shot or Edited sequence

1. Load audio. Offline analysis runs at load, and the menu item stays disabled until it exists.
2. Star at least one object in World ▸ Objects — the panel's Heroes list shows what you have and in
   what order.
3. Camera ▸ Direct to Music.
4. Adjust by starring, unstarring and moving objects; the shot re-cuts itself.
5. Camera ▸ Hand Camera Back to the Viewport when you want the camera again.

### Song

1. Analyze the song in the Sequence panel (or load a project that carries a song plan).
2. Tick *available to the Auto-director* on the cameras you want used, in the Cameras panel. The
   main camera counts; a second and a third are what make Song Mode worth using.
3. Auto-director ▸ Shot mode ▸ **Song**.
4. Set *Director freedom*, and any section's own freedom in the list below it.
5. Enable Auto-director.

Before anybody has authored a shot intent, each section's intent is **derived from its
measurements** — how loud and how busy it was, and nothing else. No label is read, which is why an
ambient track the detector honestly answered `Other` for (ADR-206) still directs. Editing a section's
freedom turns that derived plan into the project's own, saved with it.

On the command line:

```
--director mode=song,autonomy=guided,minShot=1.6,maxShot=5,seed=1
--song-plan <file>        a song plan, for a project that does not carry one
--save-scene <file>       the camera shot track lives in the scene, not the project
```

`examples/world/glowmere-valley-2-song.json` is the worked example; `tools/make_song_demo.py` builds
it from the three-camera demo and prints the two commands that direct and render it.

## Known sharp edges

- **The subject is whichever hero ranks first**, and re-cutting is all-or-nothing, so an unstar can
  change the whole film rather than just removing one shot.
- **A re-cut folds the whole track** in Continuous and Edited — a click's worth of work, not a
  frame's. It is why the move debounce exists. Song Mode does not fold anything and is cheaper again.
- **`camera/mode` is driven, not set.** The base value stays whatever the scene was authored with,
  which is what lets handing the camera back restore an orbit camera.
- **A Song Mode plan is a plan for a piece.** Loading a different song does not re-derive it; the
  plan is the project's, and an empty one is the signal to derive again.
- **A speed cap re-shapes a Song Mode shot the same way it re-shapes any other.** The decision log
  reports the distances the intent asked for; `maxCameraSpeed` may shrink the ground the shot covers
  afterwards (ADR-200), and the log line is what was decided, not what was finally baked.
