# ADR-209: The director is a decision layer, not a second behaviour system

**Status:** Accepted
**Date:** 2026-09-15

A brief asking for "a lightweight cinematic/world director": the user describes what should happen
and the engine works out the movement and the timing. Its first test case is a UFO that finds farm
animals in Glowmere Valley 2, flies to them, lifts them out of the grass with a tractor beam and
goes looking for the next one — with no authored keyframes anywhere.

The brief also says, twice, *do not build a parallel version of something that already exists.*

## The finding that shaped everything: four fifths of it was already built

`docs/director-poc-plan.md` claimed this before the work started and the claim was checked rather
than trusted. ADR-096's `src/entity/action.hpp` opens by stating the brief's own pipeline —

```
Character -> Action -> Target -> Animation -> Completion -> Next Action
```

— and implements it with eight primitives, targets that may be a point or an entity or a verb a
prop published, start conditions, branch labels, completion events, a pause-aware `Schedule`, and a
three-tier queue whose **top tier is already called `Authority::Director`**. `EntityWorld::direct`
already pushes a list onto it. `spatial::PointGrid` already answers radius queries.
`world::TerrainQuery` already answers walkable / wet / steep / under-canopy / inside-a-solid.
`EntityDesc::tags` already exists. Node parenting already exists, and `visitor-beam` has said
`"parent": "visitor"` since it was authored.

So the brief's Transform / Animation / Visibility / Timing vocabulary was not missing. What was
missing was **anybody standing in the director tier**. `ActionQueue` is a thing that can be *told*
what to do; nothing in the repository *decided*.

Four gaps, and they are the whole of the new code:

1. **A decision layer** — scene queries, target claiming, and the loop.
2. **A name for an actor group** — the transform relationship was already parenting; the *group*
   had no name, so nothing could say "move the saucer" and mean the beam too.
3. **`Parallel` across different entities** — a saucer hovering while a cow rises is two entities,
   and a per-entity stack of tiers structurally cannot express it.
4. **Somewhere for a flying body's position to come from** — `ActionKind::Move` routes over
   navigation and snaps to the ground it is crossing, which is exactly right for a walker and a lie
   for a craft.

## Decision

`src/stage/` — namespace `avgen::stage`, class **`Staging`**. (`WorldDirector` is taken: ADR-041
uses it for art-direction knobs — Drama, Warmth, Mystery — which are a different thing with a
confusable name, and the docs must not conflate the two.)

```
StagingDesc
  actors[]      -- a body entity plus its named parts (beam, lights)
  scenarios[]
    params[]    -- every number the scenario reads, as ordinary params::Parameters
    beats[]
      find[]    -- scene queries; each binds a role, or the beat takes `otherwise`
      cues[]    -- run in PARALLEL, one per role
        steps[] -- run in SEQUENCE
      then / otherwise / release
```

Three containers, and they are the brief's timing vocabulary exactly: **Sequence** is a cue's step
list, **Parallel** is a beat's cue list, **Repeat** is the scenario's cycle. It is three containers
rather than a behaviour tree because the brief asked for the small version and because nothing in
the existing architecture suggested the large one.

### A step names a role, never an entity

`actor`, or whatever a `find` bound. `actor.beam` is the actor's part called `beam`. This is the
whole of why the UFO abduction contains no UFO-specific C++: change the actor and the tag the query
filters on and the same five beats abduct something else, or follow it, or land next to it. The
acceptance test for the architecture was stated up front as "the UFO sequence contains no
UFO-specific C++ beyond its parameters", and it is met by the scenario living in
`examples/world/glowmere-valley-2.scene.json` under `"staging"` — 24 parameters, 5 beats, 0 lines of
C++ that know a saucer exists.

### Eleven step kinds, and what each of the brief's names maps onto

| The brief | Here |
|---|---|
| MoveTo, MoveBy | `moveTo` (`relative` for the second) |
| RotateTo, LookAt | `lookAt` |
| Follow, Hover | `follow` (with a `height`) |
| Attach | `attach` / `detach` |
| PlayAnimation, StopAnimation, SetAnimationState | `play` — an *activity* name, never a clip |
| Show, Hide | `show` / `hide` |
| Fade | `set`, over a duration |
| Wait | `wait` |
| Sequence, Parallel, Repeat | a cue, a beat, a cycle |
| FindEntity/Nearest/Random/ByTag/WithinRadius/InRegion | one `QueryDesc` with different fields set |

Plus three the brief did not name and the loop needs: `actions` (hand ADR-096 `ActionDesc`s to the
entity's own queue — the bridge that makes every walker primitive reachable without re-implementing
one), `release` (drop a claim), and `retire` (hide it, release it, and never offer it to a query
again).

Six query *functions* would have been six copies of the same walk, so there is one struct. The
difference between `FindNearest` and `FindRandom` is a tie-break; the difference between
`FindByTag` and `FindWithinRadius` is which field is set.

### A number is a parameter or a literal, everywhere

`{ "param": "hoverHeight" }` anywhere a number appears. Every knob the brief lists — search radius,
preferred height, travel speed, approach duration, hover duration, beam activation time, abduction
duration, lift height, rotation, time between targets, maximum abductions, seed — is a
`params::Parameter<float>` under `staging/<scenario>/<name>`, and is therefore keyframeable,
presettable and modulatable without this layer knowing any of those systems exist. "Do not hardcode
these values throughout the implementation" is a property of the format rather than of the author's
diligence.

A `Value` naming a parameter the scenario never declared is a **load error**, not a zero. A
parameter that silently reads zero is a hover duration of nothing and a search radius of nothing,
which looks exactly like a director that is broken for a reason nobody can find.

### `entity::DirectorMotion`: the one addition to the entity layer

Twenty lines on `Entity` and three places in `EntityWorld::update`. A director says where a body
**is**, absolutely; it becomes the entity's `travel` — the same field navigation writes — so
`EntityState::position()`, the crowd field, the fields pass and every query that reads an entity's
place all agree. It raises `driven`, so `wander` and `explore` yield *by keeping their state*,
exactly as they do under an action: an animal put down after an abduction resumes the walk it was
on. Its `rotation` is added **after** the behaviours, which is what lets the visitor keep hovering,
drifting, banking and spinning to the beat while it is being flown somewhere, and its `speed` is
handed to the gait, which is what keeps a cow's legs going all the way up the beam.

Absolute rather than relative because "the saucer is here now" is the claim a director makes; an
offset would make the answer depend on an anchor, and an anchor is an authoring decision that must
be free to change. It also means **clearing a director's hold does not snap anything back**: travel
persists, and the behaviours pick up from where the shot left the body.

### `moveTo` takes both a speed and a duration, and they mean different things

`speed` is how fast it travels; `duration` is the *least* time the move may take. A shot that says
"30 m/s" and "no less than seven seconds" gets a craft that crosses two hundred and fifty metres at
speed and crosses twenty metres without looking like it teleported. The brief asked for both
"Travel Speed" and "Approach Duration / smoothing" and this is why.

The tween is a `smoothstep` over a captured start and a goal re-read every frame — eased at both
ends, and tracking a target that moves — with a `clearance` floor read off the navigation layer's
own ground height, which is the brief's "avoid terrain collisions" asked of the world rather than
hoped for.

### Cost

No per-frame scan over the scene. The candidate index is built from the entities whose tags any
query in the description actually mentions — in the shipped scene, 18 animals out of 24 entities —
and rebuilt on `searchInterval` seconds against a `spatial::PointGrid`. With no scenario running,
`update` returns after one branch. Measured over the shipped POC: ~440 index rebuilds in 220 s of
playback, not 13,200.

## Parts 1 and 2: the farm

The animals are **`gltf` nodes with an `animation` state, one entity each** — not a scatter layer.
ADR-205 measured why: `world::scatter` merges primitives per material and the merge drops skin
influences, so a farm animal in a scatter layer draws in its bind pose, identically, for ever.

`tools/place_farm_animals.cpp` is a build target rather than a script for the reason
`make_tree_of_life.cpp` gives: it loads the real scene and asks the engine's own questions of it —
`TerrainQuery::at`, `isOccupied` against the 1,238 per-instance obstacles the scene builds at load,
`canopyHeightAt` — and a Python placer would hold a second copy of the terrain noise and the two
would drift apart silently. Three populations, scarcest ground first: inside a declared glade, under
the canopy, and open ground anywhere. Both halves of the brief, and "hidden" never means "inside a
tree" because the canopy band and the obstacle test are two separate questions asked separately.

It emits a *fragment*; `tools/splice_farm_animals.py` puts it in. A JSON round trip through
nlohmann reorders every key in the file and reformats every float — a 9,000-line diff for an
18-node change — which is the same reason `refresh_scene_fingerprint.py` is a regex.

Part 2 needed **no new behaviour at all**. `wander` already has a home radius, already asks the
navigation layer for a destination, already steers round what is in the way, already drops the
destination and waits a beat when every way out is blocked, and already yields to a director while
keeping its state. Each animal gets its own seed, its own territory (4–15 m by species) and its own
pause range, which is what makes "do not have every animal move simultaneously" structural rather
than hoped for.

## Consequences

- One new directory, `src/stage/`, and one line added to the core source glob.
- `src/entity/entity.{hpp,cpp}`: `DirectorMotion`, and three places in the update loop.
- `src/scene/composition.{hpp,cpp}`: a `staging` block, parsed after `entities` so validation has
  the actor list, and a tick in `updateBehaviour` **before** the entity pass — so an override
  issued this frame executes this frame. The director reads the action events the *previous* update
  produced, which is a deliberate one-frame latency and is what makes the whole thing a pure
  function of the frame sequence.
- `visitor-beam` now starts `"visible": false`. It was authored always-on, which is the right look
  for a craft that never does anything and the wrong one for a craft that abducts things.
- Nothing in `src/rendering/` or `shaders/`. The tractor beam is the particle system that was
  already there; the director writes `particles/visitor-beam/spawnRate` and `emissive`, which are
  the same parameters the scene's own audio reactions write.

## What this cost, in defects found

Four, and the interesting ones came from fixing the earlier ones.

### Two cues that take their height from each other climb away

The shipped abduction had the saucer hovering `hoverHeight` above the cow while the cow rose to
`liftHeight` under the saucer. Each frame both read the other's *new* height. Measured: **488 metres
of "lift" in four and a half seconds**, and the scenario starved after one abduction because the
saucer was in the stratosphere and nothing was in range.

It had been invisible, and the thing that made it visible was fixing something else. `ground` was
pinning the cow to the terrain every frame, which broke the loop and also meant the cow never rose
at all — the lift measured 1.2 m instead of 19.6 and the sequence "worked". So the grounding fix is
what *exposed* this, which is the usual shape of these: a defect masked by a second defect, and the
count of abductions went *down* (six to one) when the code got more correct. "Improved" is not
"correct" (ADR-199), and neither is "it looked like it was working".

The horizontal half of that coupling is fine and is the point — a craft should follow a target that
walks. Only the vertical half is circular, and only when neither end is anchored to something that
does not move. `aboveGround` is that anchor: a height measured from the terrain rather than from the
other body. `setDesc` now **refuses** a beat where two roles take their height from each other and
neither is anchored, and says which two and what to do about it.

### The wander metric was measuring the level-of-detail band, and then it could not fail

The first reading was 63.6% of commanded frames still and a **75-second** stall — worse than the
`sage` precedent this was supposed to beat. The 75 seconds was one chick, culled by
`EntityDesc::cullDistance` because the camera was 260 m away, holding its last `speed` and its last
position for the rest of the run. "It did not move this frame" is not a fact about wandering when
the entity was not updated at all. The measurement now runs with ADR-186's offline setting —
`entityDistanceCull = false`, every entity every frame.

And then the reading went to **0.0%**, which is worse news than it looks: once `wander` stops
reporting a speed it is not travelling at, the speed it reports *is* the distance it covered, so
"commanded to move and did not" is zero by construction and the probe can no longer fail (ADR-182).
It is kept as a regression guard and labelled as one. The measurement that carries the weight is
two others, chosen because a stuck animal makes them move:

* the **longest unbroken motionless stretch**, against the animals' own authored `pauseMax`.
  Measured 26.20 s against an authored 26 s — one pause, not two. The `sage` wedge was 62.3 s.
* the **least distance any single animal covered**. An aggregate percentage hides one boxed-in
  animal behind seventeen that are fine; a per-animal minimum does not.

The aggregate "percentage still" is 61.8%, and asserting the `sage` 31.7% against it would have
been measuring the *settings*: these animals pause for 3 to 26 authored seconds between
destinations, so most of the run is correctly a pause. A number borrowed from a different behaviour
with different knobs is not a threshold.

### And two from probes that could not fail (ADR-182)

`parameterPath` was reading `StepDesc::role` instead of the role the cue resolves when a step names
none, so **every `show`, `hide` and `set` in the shipped scenario was failing silently**. The test
that should have caught it passed, because its fixture registered the node's `visible` parameter as
a `ParamDesc<bool>` left at its defaults — hard range `[false, false]` — so the parameter was
already 0 and "the director hid it" and "the director did nothing" produced the same reading. The
fixture now spells out the range and the test asserts the parameter is *visible first*, which is
what makes "hidden afterwards" a change rather than a coincidence.

## Not done

**Part 6, the sequencer track.** Another agent owns `src/seq/sequence.*` and
`src/ui/sequence_panel.cpp`. The API a track needs is complete and is deliberately ignorant of what
a beat is: `Staging::start(name, now)`, `stop(name, now)`, `trigger(name, beat, now)` to jump to a
labelled beat, `running(name)`, `cycles(name)`, `beat(name)`, `binding(name, role)`, and
`setParameter(scenario, knob, value)` for an automation lane. `Composition::director()` reaches it.
