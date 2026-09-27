# ADR-928: Set-piece templates, and a beat that begins on the clock

**Status:** Accepted
**Date:** 2026-09-26
**Follows:** ADR-209/210 (staging: actors, scenarios, beats), ADR-385 (the `stillRoles` gate: a lift
only under a craft that has stopped), ADR-671 and ADR-700 (a seek replays the director, from
checkpoints), ADR-089 (a seek lands on the frame a play does)
**Found by:** the GV3 revision's Director audit (`reports/director.md` §2 "More UFO events": class d,
"the Plan cannot author scenarios"; recommendation 5), and the owner's brief §9 ("increase the number
of UFO/abduction events ... do not simply duplicate the same abduction shot")
**Implemented by:** `stage::SetPieceKind`, `SetPieceSlot`, `SetPieceSpec`, `SetPieceTimeline`,
`instanceSetPiece`, `setPieceTimeline`, `validateSetPieceSpec`, `setPieceSlotLabel`
(`src/stage/setpiece.*`); `BeatDesc::startAt`, `StepDesc::component`, `ScenarioParam::label`, a
beat-named `startOn`/`stopOn`, `Staging::wrote`, the bus-id cache, step completion (`reached`)
(`src/stage/staging.*`, `stage_json.cpp`); `staging/` on the beginner layer (`ui::detail::kBeginnerPrefixes`,
`src/ui/ui_logic.hpp`)
**Tests:** `tests/unit/test_setpiece_templates.cpp` (`[stage][setpiece]`: the three templates' beats
and cues, every registered parameter read by a step, the clock on the placed moment only, the
refusals, the JSON round trip); `tests/unit/test_staging.cpp` (the clock, the component, a scenario
started on another's beat, the bus-id cache, and "a step whose duration is a whole number of frames
ends on that frame however the instant was computed"); `tests/unit/test_setpiece_film.cpp` (`[setpiece][film]`:
the proof on a played film); `tests/unit/test_setpiece_reach.cpp` (the Parameters panel's own
grouping); `tests/rendering/test_setpiece_gpu.cpp` (the beam colour on pixels)

## Context

`stage::StagingDesc` could already say everything a UFO event needs: a craft and its beam as one
actor, region queries with a clearance, claims, parallel cues, and the `stillRoles` gate. What it
could not do was be *planned*. Every Glowmere Valley 3 set piece was written beat by beat in Python
(`tools/gv3/cast.py`): one abduction, about forty hand-tuned numbers, and a dissolve re-timed by
measuring where it landed with `avgen_cast_trace` and adjusting `abductSeconds` until it hit the drop.
The owner asked for many UFO events through the film, varied in place, scale, timing, animal count and
relation to the music. Writing ten of those by hand is how one abduction gets copied ten times.

Two engine facts stood in the way of anything more than copying:

- **A moment placed by adding durations drifts.** A beat hand-off costs a frame, and a step's end is
  quantised up to the frame grid, so GV3's beats landed a frame or two late per beat and a dissolve
  "on the drop" had to be found by measurement.
- **A step could write only component 0** of a parameter. A beam's colour is `colorStart`, an RGBA
  vector, so a set piece could only ever make it redder.

## Decision

**A set piece is a template: a parameterised `ScenarioDesc` with named slots, instanced with
overrides.** Three ship.

| Template | What happens | Beats (moments a viewer names in **bold**) |
|---|---|---|
| `abduction` | 1-3 animals lifted in parallel, one role and one cue each, under a craft the `stillRoles` gate has seen stop | rest, transit, **approach**, hover (gate, animals taken here), **beam**, **lift**, **depart** |
| `survey` | the beam lights over a field and sweeps across it; nothing is lifted | rest, transit, **approach**, hover (gate), **beam**, **sweep**, **depart** |
| `flyby` | a crossing on a path | rest, transit, **cross**, **depart** |

- **Slots.** Every number a template reads is a slot with a default, a legal range, a unit and the
  words a viewer would use ("hover height above the ground", "beam brightness"). The defaults are
  GV3's own abduction where it had one, and GV2's canopy clearance (below), which GV3's did not ask. A slot is one of two kinds, and the difference is the silent
  no-op family the engineering rules name:
  - **knob** (`SlotUse::Parameter`): becomes a scenario parameter, `staging/setpiece/<key>/<slot>`,
    because a step reads it every frame -- a duration, a height, a beam level, the moment a beat is
    clocked to. Only the ones some step actually reads are registered, so an abduction whose animals
    are named does not register a `gatherRadius` that does nothing.
  - **structure** (`SlotUse::Structure`): folded into the beats when the template is instanced --
    the animal count, the approach and departure bearings and distances, the stacking in the column,
    the cruise speed the timeline checks. Nothing reads these at run time, so they are never
    registered; they change by revising the plan.
- **Variation** comes from the slots and the spec: place (a point, or a region searched for the
  nearest tagged animal with clear air), approach bearing and distance, hover height and duration,
  beam colour, brightness, width and density, animal count, lift height, speed, spin and sway.
- **One craft, many set pieces.** Each set piece is its own scenario, `setpiece/<key>`; all of them
  autostart and wait. Each hides its craft at t = 0 (idempotent) and takes it with a hidden move to
  where it will appear, so a craft plays any number of them in sequence and a set piece's content is
  independent of the ones before it (adding an earlier one does not re-fingerprint the later ones).
- **Parallel lifts.** An abduction's animals are gathered at arrival (the ones under the craft, not
  the ones that were there when it set off), held through the hover and the beam, and lifted at once,
  each to its own place in the column: spread round the axis by `stackRadius` and stepped in height
  by `stackStagger`, so two bodies never rise through each other. No animal under the craft: it
  leaves without beaming (`otherwise: depart`).

**The staging clock: `BeatDesc::startAt`.** A beat with a clock is entered on the first frame at or
after that timeline second, whatever the beats before it cost, and holds between beats until then.
It is a pure function of time, so a play and a seek's replay enter it on the same frame. A set piece
is placed by ONE moment (the beam for an abduction or a survey, the crossing for a flyby, or any
moment the plan names): that beat carries `<moment>At`; everything before it is scheduled backwards
with `kAnchorSlackSeconds` (0.25 s) absorbed before the clocked beat, and the transit carries
`transitAt`; everything after follows the authored durations. `SetPieceTimeline` gives the nominal
times on the frame model the beats run on; `avgen_cast_trace` measures where they land.

**A step's duration is reached allowing for the noise of an instant, and nothing more.** A `wait`,
`moveTo`, `lookAt`, `play` or `actions` step ends when `elapsed + 1e-10 >= duration`. A frame's
instant is computed three ways -- `i * dt` by a trace, `k / fps` by a render's clock, `target - m *
dt` counted back by a seek's replay (`EntityWorld::seek`) -- and they differ in the last bits, a few
1e-13 s even twenty minutes in. So a duration that is exactly a whole number of frames sat on a frame
boundary and the noise chose the side. Found by this ADR's proof: the lab's second abduction, whose
fade delay (3.5 s, the template's default, exact as a float) is 210 frames, entered `depart` at
66.083 s in a play and at 66.067 s after a seek to 115 s. The tolerance is a thousand times the noise
and below the rounding of any duration stored as a float that is not exact (0.8f is 0.8 + 1.2e-8,
2.4f is 2.4 + 9.5e-8), so every such step ends on exactly the frame it always ended on. The first
version allowed a microsecond, which swallowed that rounding and moved every such step a frame
earlier: the ADR-623 trace digest of Glowmere Valley 2 caught it (its 0.8 s aim), and at 1e-10 the
digest is again the pinned `0x6e863d80f8c6146f`. (`set` steps already ended on `u >= 1.0f`.)

**Two staging primitives**, general rather than set-piece specific:

- `StepDesc::component`: a `set`, `show` or `hide` step writes the component it names. A coloured
  beam is six one-component `set` cues (`colorStart` and `colorEnd`, red, green, blue) run in
  parallel during the transit while the craft is unseen -- parallel because a cue advances one step a
  frame, and six frames of a half-tinted beam is a flicker -- and the scene's own colours are written
  back at the departure (read from the parameter bases), so the next set piece finds the beam the
  scene authored.
- **A scenario can start on another's beat**: `startOn: "setpiece/east/beam"` is the frame after that
  scenario entered that beat, read from the director's own record of its last frame (a member, so a
  checkpoint carries it), not from the bus.

**Where an artist finds it** (the owner's rule):

- **Parameters panel -> staging -> setpiece/<key>**: every knob of the set piece named `<key>`,
  labelled in viewer words with its unit (`ScenarioParam::label`, JSON `"label"`, registered as the
  parameter's label): "hover height above the ground (m)", "beam brightness (x)", "beam at (s)",
  "beam colour: red". `staging/` is now on the beginner list, so these show on the layer the editor
  opens on; before this every staging knob -- GV2's and GV3's authored abduction's included -- showed
  only on Advanced.
- **Director panel -> UFO set pieces**: the set piece itself -- its template, place, time and
  variation (ADR-929).

## Consequences

- **The bus-id defect, found and fixed.** The director cached a scenario's `startOn`/`stopOn` signal
  ids against whichever bus it first saw. A seek's replay hands it the replay bus (ADR-870), whose ids
  agree with the live one's only over the frame signals they share, so an id cached against one and
  read against the other read a different signal or past the end. A different bus now drops the
  cache (`test_staging.cpp`, "a signal id cached against one bus is not read against another").
- **The beginner layer shows every staging knob.** `staging/` joins `temporal/` and `music/` as a
  prefix plainly visible in the picture (ADR-410's reasoning; ADR-375's defect, fourth instance). The
  change is a UI one only: GV2's and GV3's `staging/abduction/*` sliders now appear in the Parameters
  panel on Beginner and Intermediate. No scene's look or behaviour changes.
- **Step completion moves only where it was already undetermined:** a duration that is exactly a
  whole number of frames as a float (3.5, 5.0, 7.0, 2.0 s ...) now ends on the frame it names on every
  path, where before a play and a seek could end it a frame apart. Measured in a throwaway build
  with the old rule: Glowmere Valley 2's multicam film (whose abduction has three such steps -- the
  7 s approach, the 5 s beam drain and the 2 s glow hold) traced every frame for its first 60 s, three
  abduction cycles, differs in **0** of 3,601 frames x 19 bodies between the old rule and the new; the
  ADR-623 digest of Glowmere Valley 2 is the pinned value under both. The two control arms fail under
  the old rule, as they must: the new staging test enters the next beat on frame 216 by `i * dt` and
  215 by `k / fps` and by the replay's count-back, and the film proof's `field/depart` is a frame
  apart between the seek and the play.
- **The abduction's `targetClearance` default is 6.5 m, Glowmere Valley 2's own**, not the 3 m the
  template first shipped with. The canopy a query asks is the tallest scatter layer that can grow at a
  point (`ClearanceField::canopyHeight`), not what stands there, and measured on a scratch copy of
  Glowmere Valley 3 on a 15 m grid (861 points) it is 3.2 m over the whole valley floor (191 points:
  the meadow layer), 5.4 m in scrub (5), 7.1 m (111) and 8.0 m (520) in the woods. At 3 m every point
  of the valley refused an abduction -- at plan time and, through the same query, at run time; at 6.5 m
  meadow and scrub lift and the woods refuse.
- **Authored scenarios keep their bytes.** `startAt` defaults to 0 (no clock), `component` to 0 and
  `label` to empty; none is written when at its default, so every tracked scenario serialises as
  before.
- **Measured on the lab film** (`test_setpiece_film.cpp`, three abductions, one craft, 60 fps): each
  beam beat is entered on the frame at its second (12.000, 60.000, 110.000 s); all six herd animals
  are bound by the set piece over their herd and retired; the two strays never move; the craft holds
  its station through beam and lift to within the hover sway (at most 0.41 m from it, `craftWobble`
  0.3 m),
  and with `craftWobble` 0 it does not move at all, frame to frame. Figures in ADR-929 and ADR-930.
- **Not done.** A region place is an abduction's only (a survey or flyby works over a point). The
  templates have no alien reaction of their own: characters already hear `setpiece/<key>/beam` as a
  world event (ADR-930) and react as they react to any event.
