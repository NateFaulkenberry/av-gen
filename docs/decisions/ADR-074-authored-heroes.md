# ADR-074: Heroes a scene can declare

Status: accepted
Date: 2026-09-10

## Context

ADR-072 gave the engine a `world::HeroPoint`: one object, in one place, that the composition is
arranged around, carrying what a camera director needs in order to discover it. It validates, it
round-trips through JSON, and exactly one thing in the repository produces one — the world composer.

So heroes existed everywhere except the one world that obviously has one. Glowmere is hand-authored,
and its elder is the clearest hero in the project: a twenty-metre fungus at the end of a valley that
a ninety-second camera move exists solely to travel towards. Nothing could say so. The scene could
name a *focal point* at the elder's position, which is an instruction to the camera about where to
aim, and that is all it could say — not how big the thing there is, not how far away it wants to be
seen from, not what colour it owns, and not that it is alive and answers the music. A reaction
profile had no route to it at all.

ADR-072 itself calls `HeroPoint::assembly` "the seam for the real thing" and notes that the
composer's single-asset heroes are "honestly weaker" than a designed object. The elder is the
designed object. This is the seam being used.

## Decision

### `"heroes"` is a top-level block in the scene file

A sibling of `"composition"`, not a member of it. The distinction is the same one ADR-072 draws
between a focal point and a hero: `composition.focalPoints` are instructions to the camera about
where the frame should point, and a hero is a description of what the thing standing there actually
*is*. They happen to coincide in Glowmere. They are not the same kind of statement, and folding one
into the other would make "point here" and "this is important" indistinguishable in the file.

It is also not a property of a node, which was the obvious alternative and does not survive contact
with the elder: `elder-crown`, `elder-stem` and `elder-filaments` are three procedural nodes plus a
practical light in the rig, and no single one of them is the hero. `assembly` names the group; the
scene's node names carry the prefix. Nothing enforces that relationship yet beyond a test that reads
the shipped file, which is honest about where the seam currently stops.

### Refusal, never a silent drop

`Composition::setHeroes` validates the whole set and takes it or takes none of it, and the scene load
fails with the hero's own name in the message. `HeroPoint::validate` already produces good messages —
an activation radius inside the stand-off, a reaction profile nobody wrote — and the loader wraps
them with the file and the index rather than replacing them.

Skipping a bad entry with a warning is what the rest of the loader does for *assets*, where a missing
file still leaves a world you can look at. It is the wrong behaviour here. A hero that quietly failed
to load produces a director that frames nothing, which looks exactly like a scene that never declared
a hero — a failure found by staring at a frame rather than by reading an error, which is the failure
mode ADR-072 rejected for reaction sources for the same reason.

`setHeroes` also refuses duplicate names. Everything downstream refers to a hero by name — a focus
request from the World Builder panel, reactions installed against it, a director's choice of subject
— and two heroes called "elder" would surface as the wrong object being framed rather than as an
error.

### Declaring a hero cannot change a frame

`setHeroes` deliberately does not set `dirty_`, and heroes never reach `Scene`. A hero describes
geometry that the scene's own nodes have already placed; it is not a second way to put something in
the world. A rebuild triggered from here would be a rebuild that can only risk changing a frame for
no benefit.

This was verified rather than asserted, and as an A/B through one pinned binary rather than as a
before-and-after in time: `examples/world/glowmere-stylized.json` rendered headless at 640×360 over
8.000 s–8.050 s, with the `"heroes"` block present and with it removed, gives the same
`sequence hash 6538c438c48e0b80`. The control matters because the project file around this scene is
under concurrent change; the absolute hash moves when the project's routes do (it was
`c6eee2d2412e6f3c` before fourteen routes were added to `glowmere-stylized.json`), and only the
paired comparison says anything about this block.

### Written only when there are heroes

`toJson` omits the block entirely for a composition with no heroes, so every scene file in the
repository that never mentioned one writes back exactly what it had. An empty `"heroes": []` in every
saved scene would be a diff in every file to record the absence of something.

### It is serialised because the last two of these were not

An offline render saves the project and reloads it from a file before drawing a single frame. ADR-067
lost the corridor clearances that way — they existed in memory and never in a frame — and ADR-070
lost a generated world's light rig to the same reload, and named the pattern: an in-memory value with
no serialisation, in a pipeline that round-trips through a file. A hero would have been the third.
The test that matters is therefore not the `toJson`/`fromJson` pair but the one that goes out through
`saveFile` and back in through `loadFile`.

## The elder

```json
{"name": "elder", "assembly": "elder", "position": [-1.0, -9.0, -46.0],
 "radius": 8.2, "height": 16.5, "importance": 0.95, "focalWeight": 0.9,
 "preferredCameraDistance": 50.0, "preferredCameraElevation": 6.0, "activationRadius": 160.0,
 "colorAccent": [1.0, 0.47, 0.15], "reactionProfile": "organism"}
```

Every number is measured off the scene's own nodes rather than guessed, because a hero whose size is
approximately right is a stand-off that is approximately right, and framing is the one thing this
type exists to inform.

- **position** is the stem's base, `(-1, -9, -46)`, not the crown. It matches the composer's own
  convention — `HeroPoint::position` is where the thing meets the ground — and it is the only one of
  the elder's three anchors that does not move if the cap is re-modelled.
- **height 16.5** is the stem's curve (14.5 m from its base at y −9) plus the crown's half-thickness
  (2.0), putting the top of the cap at y 7.5. ADR-072 and the audit both describe the elder as
  "about twenty metres"; the geometry says 16.5, and the measurement is what is recorded here.
- **radius 8.2** is the crown's actual half-extent: a unit sphere under a `sourceTransform` scale of
  `[8.2, 2.0, 7.4]`. The world-builder audit describes the cap as "about 8 m across", which cannot be
  the diameter — that is 16.4 m — and the scene's existing focal point already uses a radius of 8.
- **preferredCameraDistance 50** is three times the height, the stand-off the composer frames a hero
  from. On this scene's 40° lens that puts a 16.5 m subject across a little under half the frame
  height, which is a hero in a valley rather than a portrait of a mushroom.
- **activationRadius 160** is comfortably outside the stand-off, as `validate()` insists, and is
  chosen against the authored camera move rather than as a multiple: the camera's furthest pose,
  `(-30, 10, -118)`, is about 78 m from the elder, so 160 m keeps it active for all ninety seconds.
  A radius sized only off the stand-off would have switched the elder off during the shot built to
  arrive at it.
- **importance 0.95** is the composer's value for hero 0 and is deliberately not 1.0, per ADR-072:
  the ceiling is left free so a later hero in some other world can outrank this one without a tie.
- **colorAccent** is the filaments' emissive colour verbatim, `[1.0, 0.47, 0.15]`. The accent is what
  makes a hero findable from anywhere in frame, so it has to be the colour the object is actually
  emitting rather than one chosen beside it.
- **reactionProfile `organism`**, out of `monument`, `organism`, `craft` and `still`. The elder is a
  living thing that opens and glows: `organism` is a slow scale pulse on low-frequency energy, an
  inner glow on the beat, and a colour drift with the spectral centroid, with no rotation — "a thing
  that grew there does not spin". `monument` is the wrong shape of stillness (its behaviour is runes
  lighting and a practical burst, and the elder is not architecture), `craft` hovers because it is
  machinery, and `still` is what the other heroes in a world get so that this one reads.

Of those reactions, `ColorShift` currently has nowhere to land — `heroBehaviourTarget` reports it
unsupported, needing a material program with a hue input. That is ADR-072's existing, deliberate
state and is not changed here; the profile is still the right one to name.

## Consequences

- Nothing consumes `Composition::heroes()` yet. This change makes the declaration expressible,
  storable and durable across a file; wiring it to the camera director and to reaction installation
  happens in `src/app`, which this change does not touch.
- The tie between `assembly: "elder"` and the three `elder-*` nodes is a naming convention that the
  loader does not check. Checking it would mean the composition validating hero assemblies against
  its own node list, which is a reasonable next step and a larger decision than this one — an
  assembly may legitimately name something that a later installer creates.
- `examples/world/glowmere-stylized.scene.json` gains a `"heroes"` block and nothing else. Its other
  1 193 lines, and the frame they produce, are unchanged.
