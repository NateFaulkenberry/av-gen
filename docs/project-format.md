# Project format

Decision: ADR-010 (serialisation) and ADR-011 (what is serialised). Implemented in
`src/params/serialization.cpp`; used by tests today and by the project system in 0.9.

Version 2 (milestone 0.3) adds modulation sources, presets and per-route polarity. Version 3
(milestone 0.8) adds `"shaders"` (written by the engine since 0.4) and `"timeline"`. Version 4
(milestone 0.9) adds `"assets"` (audio / scene / environment references) and `"app"`
(`{ "name", "version" }` of the writer); both are filled in by the engine.

Version 4 also carries three blocks that are *optional and unversioned by the envelope*, because
their absence is meaningful and needs no migration: `"composition"` (ADR-083, the 2D layer stack,
with a `version` of its own), `"timeline"`, and `"sequence"` (ADR-089, the cinematic sequence). A
project written before any of them existed simply has no such key, and loads with none.

Older documents are upgraded in memory by `params::migrateProject`, one version at a time,
before loading (the file on disk is only rewritten on save, at the current version); every step
is logged at info level. 1 -> 2: routes without `polarity` get `"unipolar"`, empty `sources` /
`presets` are added. 2 -> 3: an empty `shaders` array is added (`timeline` stays absent, meaning
none). 3 -> 4: `"assets": {}` and `"app": { "name": "avgen", "version": "unknown" }` are added.
Keys that already exist are kept, and unknown keys survive. A `version` newer than the reader's
is rejected.

```json
{
  "format": "avgen-project",
  "version": 4,
  "parameters": {
    "orb/scale": 1.0,
    "orb/baseColor": [0.75, 0.2, 0.9],
    "scene/brightness": 1.0,
    "sources/wobble/rate": 0.5,
    "macros/energy": 0.7
  },
  "routes": [
    {
      "source": "audio.bass",
      "target": "orb/scale",
      "component": -1,
      "amount": 1.2,
      "op": "add",
      "polarity": "unipolar",
      "enabled": true,
      "chain": {
        "gain": 1.0, "offset": 0.0,
        "curve": "power", "curveAmount": 0.8,
        "clampEnabled": false, "clampMin": 0.0, "clampMax": 1.0,
        "threshold": "none", "thresholdLevel": 0.5,
        "attackMs": 15.0, "decayMs": 180.0,
        "envelope": "none", "envelopeHoldMs": 0.0, "envelopeFallPerSecond": 4.0,
        "remapEnabled": false, "remapInMin": 0.0, "remapInMax": 1.0, "remapOutMin": 0.0, "remapOutMax": 1.0
      }
    },
    { "source": "lfo.wobble.bipolar", "target": "orb/rotationSpeed", "polarity": "bipolar", "amount": 0.5 }
  ],
  "sources": [
    { "kind": "lfo",      "name": "wobble", "settings": { "shape": "sine" } },
    { "kind": "envelope", "name": "hit",    "settings": { "trigger": "audio.onset" } },
    { "kind": "noise",    "name": "drift",  "settings": { "seed": 1 } },
    { "kind": "random",   "name": "pick",   "settings": { "trigger": "audio.onset", "seed": 1 } },
    { "kind": "timeline", "name": "intro",  "settings": {
        "keys": [ { "time": 0.0, "value": 0.0, "interp": "smooth" }, { "time": 8.0, "value": 1.0, "interp": "linear" } ],
        "loopLength": 0.0 } },
    { "kind": "macro",    "name": "macros", "settings": { "knobs": [ { "name": "energy", "default": 0.7 } ] } }
  ],
  "presets": [
    { "name": "calm", "values": { "orb/scale": [1.0], "orb/baseColor": [0.2, 0.3, 0.9], "macros/energy": [0.2] } }
  ]
}
```

Rules:
- `parameters` holds base values only (finals are derived every frame); numbers for float/int,
  booleans for bool, arrays for vectors and colours. Only parameters flagged `serialized`. Source
  settings that are parameters (`sources/<name>/rate`, `macros/<knob>`, ...) live here, not in
  `sources`.
- Enums are lower-case strings: curve `linear|power|log|exp|scurve`; threshold
  `none|gate|binary|subtract`; envelope `none|peakhold|linearfall`; op
  `add|multiply|replace|min|max`; polarity `unipolar|bipolar` (missing = `unipolar`; `bipolar`
  maps the source 0..1 to -1..1 before the chain); LFO shape
  `sine|triangle|saw|square|samplehold`; keyframe interp `step|linear|smooth` (declared on the
  left key of each segment).
- Missing chain keys take defaults; unknown enum strings are errors.
- `sources` is an ordered array of `{ kind, name, settings }`. `kind` is one of
  `lfo|envelope|noise|random|timeline|macro`; `name` is unique per kind. `settings` holds only the
  non-parameter settings of that kind (see the example); a missing `settings` uses defaults.
  Unknown kinds are skipped with a warning (forward compatibility); malformed settings are errors.
  Loading replaces the whole rack; the caller attaches it again and re-binds the modulator.
- `presets` is an array of `{ name, values }` where `values` maps parameter paths to arrays of
  components (a bare number or boolean is accepted as one component). Presets store base values
  only; paths unknown to the current parameter set are kept and ignored on apply. Loading
  replaces the bank.
- Load order: `sources` (rack replaced, and attached when it already was, so their parameters
  exist), `parameters`, `routes` (replaced), `presets` (bank replaced). A document without a
  `sources` or `presets` section (e.g. version 1) leaves an empty rack / bank when the caller
  passes them; a caller that passes no rack / bank keeps its own untouched.
- Loading validates everything first (including `sources` and `presets`, whether or not the
  caller loads them) and changes nothing on failure. Unknown parameter paths are skipped with a
  warning (forward compatibility); a type mismatch is an error. `version` greater than the
  reader's is rejected; older versions are migrated in order (see above).
- Paths are the identity for parameters everywhere: UI, presets, OSC addresses (`/orb/scale`).


## Timeline (milestone 0.8, ADR-018)

```json
"timeline": {
  "enabled": true,
  "tracks": [
    { "target": "orb/scale", "component": -1, "timeBase": "seconds", "mode": "replace",
      "loopLength": 0.0, "enabled": true,
      "keys": [ { "time": 0.0, "value": [1.0], "interp": "easeInOut" },
                { "time": 4.0, "value": [2.0], "interp": "bezier", "tangentOut": [0.0], "tangentIn": [-1.0] } ] },
    { "target": "orb/emissive", "timeBase": "beats", "loopLength": 4.0,
      "keys": [ { "time": 0.0, "value": [3.0], "interp": "easeOut" }, { "time": 1.0, "value": [0.5], "interp": "step" } ] }
  ],
  "cues": [ { "time": 16.0, "name": "drop", "preset": "big", "morphSeconds": 0.5, "timeBase": "seconds" } ]
}
```

Tracks write the target's final value (after the base, before modulation routes) with
`replace`, `add` or `multiply`; `component` -1 keys every component (one `value` entry per
component), otherwise a single component. `timeBase` `beats` reads the beat clock and pairs
with `loopLength` for repeating patterns. Interpolations: `step`, `linear`, `smooth`, `easeIn`,
`easeOut`, `easeInOut`, `bezier` (tangents in value units per time unit). Cues recall a preset
from the project's bank at their time, morphing over `morphSeconds` (beats for beat-based cues);
an empty `preset` is a marker.

## Scene composition files (milestone 0.7, ADR-017)

A scene file is a separate document (`"format": "avgen-scene"`, version 1) describing a
`scene::Composition`: a list of nodes flattened into one renderable scene. Asset paths are
relative to the scene file's folder (the engine relativises them on save). A project file
continues to hold parameters, routes, sources, presets and shader layers; a scene file holds
*what* is in the scene, so a project can drive any scene and a scene can be reused in another
scene (`"kind": "scene"`, nested up to four levels; a file that includes itself is refused).

```json
{
  "format": "avgen-scene",
  "version": 1,
  "name": "stage",
  "camera": { "distance": 0.0, "height": 0.35, "orbitSpeed": 0.15, "fov": 45.0 },
  "environment": { "map": "hdr/studio.hdr", "intensity": 1.0 },
  "nodes": [
    { "name": "helmet", "kind": "gltf", "asset": "models/DamagedHelmet.glb",
      "position": [0, 0, 0], "rotation": [0, 90, 0], "scale": [1, 1, 1],
      "visible": true, "emissiveBoost": 1.0, "roughnessScale": 1.0 },
    { "name": "orb", "kind": "orb", "position": [2.5, 0.5, 0], "scale": [0.4, 0.4, 0.4] },
    { "name": "floor", "kind": "grid" },
    { "name": "dust", "kind": "particles",
      "particles": { "maxParticles": 20000, "spawnRate": 400, "shape": "sphere", "blend": "additive" } },
    { "name": "backdrop", "kind": "scene", "asset": "scenes/backdrop.json", "position": [0, 0, -6] },
    { "name": "lamp", "kind": "orb", "parent": "helmet", "position": [0, 1.2, 0], "scale": [0.2, 0.2, 0.2] }
  ]
}
```

Node kinds: `gltf` (a glTF 2.0 file; instances of the same file share meshes and textures),
`orb` (the built-in orb mesh and material), `grid` (the reference floor), `particles` (a GPU
particle system, every `ParticleSystem` field optional with the defaults from `docs/rendering.md`),
`scene` (another scene file). `camera.distance` 0 means "fit to the scene bounds".

`parent` names another node of the same file: the node's world transform is the parent's world
transform times its own local one (authored rest values plus the `nodes/<name>/position`,
`rotation`, `scale` parameter offsets), evaluated up the chain, so moving, rotating or scaling
a parent (by hand or by modulation) carries its children and grandchildren. Nodes may appear in
any order; a parent that does not exist is a warning and the node behaves as a root; a cycle
is an error (also for `Composition::addNode` / `setParent`). Removing a node re-parents its
children to its parent. Parameter paths stay `nodes/<name>/…` regardless of parenting.

Parameters a composition registers (all saveable in a project and modulatable):

| Path | Meaning |
|---|---|
| `nodes/<name>/position`, `rotation` (degrees), `scale` | offset from the node's rest transform |
| `nodes/<name>/visible`, `emissiveBoost`, `roughnessScale` | per-instance overrides |
| `nodes/<name>/nodes/<child>/…` | the same for nodes of a nested scene |
| `particles/<name>/…` | the particle node's system (see `docs/rendering.md`) |
| `camera/distance`, `height`, `orbitSpeed`, `fov`; `env/intensity`, `env/rotation`; `scene/brightness`, `scene/gridIntensity`; `root/scale`, `root/rotationSpeed`, `root/impulse` | as in the orb and glTF scenes |

### `"entities"` — what moves on its own, and how it answers the music (ADR-088)

A sibling of `"nodes"` and `"heroes"`. An entity does not add geometry: it *drives* a node the
scene has already placed, which is what lets one entity type drive an imported craft, a procedural
rock or a skinned character.

```json
"entities": [
  {
    "name": "visitor",
    "node": "visitor",
    "seed": 20260911,
    "fullDetailDistance": 420.0, "coarseInterval": 0.1, "cullDistance": 900.0,
    "clips": { "idle": "Idle", "walk": "Walk", "run": "Run" },
    "behaviors": [
      { "kind": "hover", "amplitude": 0.85, "rate": 0.055, "tilt": 1.7 },
      { "kind": "spin", "signal": "audio.beat", "baseRate": 1.1, "impulse": 13.0, "damping": 0.5 }
    ],
    "reactions": [
      { "signal": "audio.bass", "target": "parts/Light/emissiveGain", "depth": 2.6,
        "chain": { "attackMs": 35, "decayMs": 260, "curve": "power", "curveAmount": 1.6 } },
      { "signal": "audio.onset", "target": "position", "component": 1, "depth": -0.28,
        "chain": { "envelope": "peakhold", "envelopeHoldMs": 8, "envelopeFallPerSecond": 6 } }
    ],
    "sockets": [ { "name": "RightHand", "joint": "mixamorig:RightHand", "position": [0, 0, 0] } ],
    "attachments": [ { "node": "lantern", "socket": "RightHand" } ]
  }
]
```

**Behaviours** are autonomous, stateful motion. Every knob one owns is registered at
`entity/<entity>/<behaviour>/<knob>`, so it is keyframeable, presettable and a legal modulation
target like anything else — the music can drive a wander's speed. They run in declaration order and
each sees what the ones before it wrote, which is the contract by which `interest` points `lookAt`
at something without either knowing about the other.

| Kind | What it does | Knobs |
|---|---|---|
| `hover` | aperiodic vertical float with a matching tilt | `amplitude`, `rate`, `tilt`, `phase` |
| `drift` | lateral wander inside a radius | `radius`, `rate` |
| `bank` | leans into the direction of travel | `degrees`, `responseMs` |
| `spin` | a yaw *rate* that events push and damping pulls back | `signal`, `baseRate`, `impulse`, `damping`, `maxRate` |
| `orbit` | slow travel around a named point | `around`, `radius`, `rate`, `phase` |
| `wander` | navigable destination, walk, pause, repeat — local milling about | `speed`, `runSpeed`, `turnRate`, `arrive`, `minRange`, `maxRange`, `pauseMin`, `pauseMax`, `homeRadius` |
| `explore` | the full loop: idle, choose somewhere worth going, plan a route, walk it, arrive facing it, observe (ADR-093) | `speed`, `runSpeed`, `turnRate`, `arrive`, `idleMin`, `idleMax`, `observeChance`, `observeMin`, `observeMax`, `minRange`, `maxRange`, `homeRadius`, `runChance`, `slopeAlign`, `bodyRadius`, `headroom`, `footprint` |
| `ground` | follows the terrain and leans into it, for a character whose motion comes from elsewhere | `slopeAlign`, `smoothingMs`, `maxFloat`, `maxTilt` |
| `lookAt` | turns the body towards a subject when not travelling | `target`, `turnRate`, `weight` |
| `interest` | probabilistic stop-and-look, plus a decaying reaction to a strong audio event | `signal`, `subjects`, `observeChance`, `minDwell`, `maxDwell`, `alertThreshold`, `reactionDecay`, `reactionCooldown` |

`explore` also reads settings that are taste rather than automation, so they are authored on the
behaviour and are not registered parameters: `landmarkAffinity`, `characterAffinity`, `glowAffinity`,
`waterAffinity` and `vistaAffinity` say what a character is drawn to; `strollChance` is how often it
goes for a walk instead of somewhere; `waypointRadius`, `repathSeconds`, `stuckSeconds` and
`noveltyRadius` tune the route following. Its destinations come from the world's interest registry —
heroes, entities, luminous patches of ecology, shoreline and high ground — assembled by the
composition; `bodyRadius`, `headroom` and `footprint` are the character's own size, and 0 on any of
them takes the world's default.

**Reactions** are `property <- signal`, declared in data. Each compiles to an ordinary modulation
route with an ordinary `chain` (every field of `docs/control.md`'s route chain applies). What the
entity adds is addressing: a `target` is resolved against, in order,

1. the entity's own behaviour knobs — `hover/amplitude` → `entity/<entity>/hover/amplitude`
2. the driven node's transform — `position`, `rotation`, `scale`, `visible`, `emissiveBoost`
3. the driven node's geometry — `material/emissive`, `parts/<i>/tint`, and for a `particles` node
   `spawnRate`, `emissive`, `size`, `speed`, …

with `parts/<material name>/…` rewritten to the part index the asset's material landed in, so a
scene file writes the name an artist can see in the model rather than an index ordered by surface
area. `@` prefixes an absolute parameter path when none of that fits. **A target that resolves to
nothing is reported by name with the candidates that were tried and the material parts that were
available**, both in the log and in `Engine::projectWarnings`.

**Budget.** Past `fullDetailDistance` metres from the camera an entity updates every
`coarseInterval` seconds with the accumulated delta instead of every frame; past `cullDistance` it
is not updated at all and its node stays where the scene put it. 0 disables either stage.

**Characters.** `clips` maps an activity (`idle`, `walk`, `run`, `turn`, `observe`, `react`) onto
the animation state name the asset carries (ADR-086), so a behaviour never names a clip. An entity
with no `clips` drives no rig. `sockets` name a place on the entity — a skeleton joint when there
is one, its own origin until then — and `attachments` make another node follow one.

**Tags** (`"tags": ["dancer"]`) say what an entity *is*, for a field or a trigger volume to filter
on. The profile an entity was built from counts as a tag it did not have to be given.

Determinism: every behaviour draws from a per-entity PCG32 seeded from `seed` (or from the world
seed and the entity's name), and nothing reads a clock other than the timeline second. The same
scene, seed, audio and timeline produce the same frame.

### `"worldEffects"` — waves propagating through the world (ADR-207)

A top-level array beside `"heroes"`, and a sibling of it for the same reason: an effect is a thing in
the world, not hidden renderer state. Absent from a scene that declares none, so every file written
before this existed round-trips unchanged.

```json
"worldEffects": [
  {
    "name": "Camera Travel Beam",
    "enabled": true,
    "style": "Bioluminescent",
    "source": { "kind": "camera" },
    "target": { "kind": "focusHero" },
    "activation": "cameraTravel",
    "propagation": {
      "kind": "directional", "direction": "blended",
      "forwardWeight": 1.0, "velocityWeight": 0.6, "targetWeight": 1.2,
      "speed": 110.0, "range": 300.0, "frontWidth": 10.0, "trailLength": 30.0,
      "falloff": 2.2, "startOffset": 10.0,
      "verticalExtent": 40.0, "verticalGrowth": 0.15,
      "ringCount": 0.0, "beamRadius": 0.0
    },
    "appearance": {
      "color": [0.07, 0.78, 1.0], "intensity": 1.0,
      "edgeColor": [0.72, 1.0, 0.92], "edgeIntensity": 3.2, "width": 1.0,
      "rainbow": false, "rainbowSpeed": 0.35, "rainbowScale": 0.012,
      "rainbowSaturation": 0.85, "rainbowBrightness": 1.0
    },
    "sparkle": { "enabled": true, "density": 1.1, "size": 0.22, "intensity": 2.4,
                 "speed": 0.6, "fadeDistance": 85.0, "seed": 1 },
    "response": { "ground": 0.45, "foliage": 1.1, "surface": 0.7, "emissive": 1.2 },
    "timing": { "delay": 0.15, "lifetime": 0.0, "fadeIn": 0.5, "fadeOut": 1.1,
                "windowStart": 0.0, "windowSeconds": 6.0, "repeatSeconds": 2.6 }
  }
]
```

`name` may not contain a `/`: it is half of a parameter path (`worldfx/<name>/intensity`), and two
effects of one name are refused outright rather than one of them being dropped.

**`source` and `target`** (`kind`, plus `name`, `position` and `groundOffset` where they apply):

| `kind` | resolves to |
|---|---|
| `world` | the authored `position` |
| `node` | the world transform of the scene node of that `name`; falls back to `position` when the scene has no such node, so a scene swap does not send an effect to the origin |
| `hero` | the node the hero of that `name` **is** (ADR-107), and its `colorAccent`; falls back to `HeroPoint::position` |
| `camera` | the active camera's eye |
| `focusHero` | whichever hero the directed camera's cut is holding right now — no name, no wiring |

`groundOffset` drops the resolved origin by that many metres on Y, for a source whose transform sits
somewhere up its stem.

**`propagation.kind`** is `directional` (distance along an axis) or `radial` (a radius in the ground
plane). **`propagation.direction`** decides where a directional wave points: `explicit`,
`sourceForward`, `cameraForward`, `cameraVelocity`, `sourceToTarget`, `cameraToTarget` or `blended`
(a weighted sum of camera forward, camera velocity and camera-to-target). A direction that needs a
target and has none is refused at load.

**`activation`** is `always`, `window` (an authored `timing.windowStart` / `windowSeconds`),
`cameraTravel` (while the director's cut says the camera is moving between subjects) or `heroFocus`
(while it is holding this effect's source). The last two need a directed camera; with none they
simply never fire. Note that `always` opens the *window* for the whole timeline — the front still
makes one pass, so a permanently-visible effect is one with a `timing.repeatSeconds`.

**Every number under `appearance`, `sparkle`, `propagation`, `response` and `timing` is also a
parameter** at `worldfx/<name>/<property>`, so it can be keyed, preset, cued and modulated. A pulse
that answers the beat is an ordinary `beat.pulse -> worldfx/<name>/intensity` route in the project's
`routes` block; there is no audio hook inside the effect. What the *scene* stores is the authored
value, which is the default those parameters register with.

### `"entityProfiles"` — a profile library (ADR-097)

A scene-level path to one file holding many named profiles, so a crowd shares a bundle instead of
carrying a copy of it each:

```json
"entityProfiles": "profiles/night-shift.json",
"entities": [
  { "name": "dancer-01", "node": "dancer-01", "profile": "dancer" },
  { "name": "dancer-02", "node": "dancer-02", "profile": "dancer" }
]
```

```json
{ "format": "avgen-entity-profile-library", "version": 1,
  "profiles": {
    "dancer": { "tags": ["crowd"],
                "behaviors": [ { "kind": "wander", "speed": 1.2 } ],
                "reactions": [ { "signal": "audio.bass", "target": "parts/Lamp/emissiveGain", "depth": 2.5 } ],
                "clips": { "idle": "Idle", "walk": "Walk", "react": "Dance" } },
    "barfly":  { "tags": ["crowd"], "reactions": [ … ] } } }
```

An entity's `"profile"` is looked up in the library first and treated as a path if the library does
not have it, so a single-profile file (`"profile": "craft-lights.profile.json"`, resolved against
the scene's folder) still works exactly as before. A profile's behaviours, reactions, clips, sockets
and tags come first and the entity's own are appended, so a local reaction lands *on top of* a
shared one on the same property rather than instead of it; saving names the profile and writes back
only what the entity added. Profiles do not chain: one level of indirection is a library, two is a
maze. A name the library does not have is an error that lists every name it does.

### `"fields"` — trigger volumes and music influence fields (ADR-097)

A sibling of `"entities"`. A field is a place in the world that **scales the depth of the reactions
the entities inside it already have** — it never adds a reaction of its own, which is what keeps
there being one reactivity system rather than two.

```json
"fields": [
  { "name": "stage", "shape": "sphere", "center": [0, 0, -12], "radius": 14,
    "falloff": "smooth", "inner": 0.25, "strength": 1.4, "floorGain": 0.05,
    "tags": ["dancer"], "source": "singer",
    "arc": { "holdSeconds": 6, "holdJitter": 1.5, "delaySeconds": 0.4, "delayJitter": 0.8,
             "intensityJitter": 0.35, "releaseSeconds": 0.6, "refractorySeconds": 20,
             "activity": "react" } },
  { "name": "doorway", "shape": "box", "center": [4, 0, 0], "halfExtents": [1.2, 2.2, 0.4],
    "rotation": [0, 45, 0], "falloff": "constant", "scaleReactions": false },
  { "name": "column", "shape": "capsule", "center": [0, 2, 0], "radius": 2, "height": 8 }
]
```

| Key | Meaning |
|---|---|
| `shape` | `sphere` (`radius`), `box` (`halfExtents`, `rotation`), `capsule` (`radius`, `height`, `rotation`) |
| `center` | where the volume is — an **offset from `source`** when one is named |
| `falloff` | `smooth` (default), `linear`, `inverseSquare`, or `constant` for a hard-edged trigger |
| `inner` | fraction of the way out that is still full strength: a plateau, not a scale |
| `strength` | the gain at full influence. Above 1 boosts the reactions it governs |
| `floorGain` | the gain *outside* the volume. 0 (default) means a governed entity outside is silent |
| `tags` | which entities it governs. Empty governs everything. Matches an entity's `tags` **or** its profile name |
| `source` | an entity, node or hero the volume follows. Empty: it stays at `center` |
| `scaleReactions` | `false` makes it a pure trigger: edges and signals, no modulation |
| `requireScrubExact` | `true` makes a live source a reported problem rather than a surprise |
| `arc` | the reaction arc, below. A block that is present is enabled unless it says otherwise |

**Influence** is 1 at the centre, 0 at the surface, smooth between, and **a point exactly on the
surface is outside**. Two overlapping fields combine by *max*, not sum. An entity no field's filter
matches keeps a gain of exactly 1 and is never queried, so a scene with no fields — or with fields
that do not name it — is exactly the scene it was.

Set `"spatial": false` on a reaction to exempt it. The case that needs it is a `Multiply` route
whose chain rests at 1: scaling its depth to zero *mutes* the property rather than making the
reaction quiet.

**The arc** (`delaySeconds`, `holdSeconds`, `releaseSeconds`, their `*Jitter` siblings,
`intensity`, `intensityJitter`, `refractorySeconds`, `activity`, `holdStill`) is what an entity does
when it crosses in: it holds `activity` for the duration and then stops holding it, so what comes
back is whatever its behaviours are doing then — `Walking → Dance → Walking`, never
`Walking → Dance → Idle`. The jitters are drawn from the entity's own seed, the field's name and how
many times it has entered, so a crowd does not move as one and a replay redraws identically.

**Knobs.** `strength`, `scale` (one multiplier for a sphere's radius, a box's half-extents and a
capsule's length), `inner`, `floorGain` and `center` are registered as ordinary parameters at
`entity/fields/<name>/…`, so they are keyframeable on the timeline and presettable like any other.
They are **not** modulatable: the field pass runs before the routes, deliberately, so a route
pointed at one is written and wiped without ever being read — a scene that does it is told so by
name at load, and the answer is a timeline key.

**Signals.** Every field publishes `field.<name>.occupancy` (0..1, entities inside over entities
governed), `field.<name>.enter` and `field.<name>.exit` (events). A light, a material or a particle
system reacts to a volume by routing from those, with no new mechanism — an entity with `reactions`
and no `behaviors` driving any node, or an `@` absolute path for something that is not a node at
all (`"@lightrig/key/fill/intensity"`).

**Determinism (ADR-091).** A field with no source, or one following a node or a behaviour-less
entity, is a pure function of time and is scrub-safe and offline-exact. A field following an entity
with behaviours is not. Which one a scene has is logged by name at every load — `field 'stage'
(sphere reach 14.0, falloff smooth, strength 1.40) follows 'singer' -> live: this field's influence
is NOT scrub-exact (ADR-091)` — and `requireScrubExact` turns it into a warning that names the
field and the source.


## Assets and app blocks (milestone 0.9, ADR-019)

```json
"app": { "name": "avgen", "version": "0.1.0" },
"assets": {
  "audio": { "path": "media/track.wav", "size": 52920044, "sha256": "9f86d0…" },
  "environment": { "path": "../hdr/studio.hdr", "size": 6291500, "sha256": "e3b0c4…" },
  "scene": { "kind": "composition", "path": { "path": "scenes/stage.json", "size": 812, "sha256": "…" } }
},
"shaders": [ { "path": "shaders/glow.wgsl", "stage": "post", "size": 1201, "sha256": "…" } ]
```

`assets.scene.kind` is `orb`, `gltf` (with `path`) or `composition` (with `path`, or `inline`
holding a whole scene document when the composition was never saved to a file). Every file
reference (`audio`, `environment`, `scene.path`, `shaders[].path`) is an object
`{ "path", "size", "sha256" }`: the path relative to the project file (`..` allowed; absolute
only across roots), the byte size and the SHA-256 of the content (hashed on save, streaming;
`src/core/hash.hpp`). The older bare-string form is still read. Node assets inside scene files
stay plain paths relative to the scene file.

On load the assets are restored first. A referenced file that is missing is searched for
under the project folder (recursively, six levels deep): candidates are files with the same
name, those with the stored size first; with a stored hash a candidate must match it (a
same-name file with other content is rejected), without one the size (or, for legacy string
references, the name alone) decides. A hit is used and reported as a warning
`relinked <audio|environment|scene|shader>: <old> -> <new>` in `Engine::projectWarnings()`
(the new location is written on the next save); otherwise the usual missing-asset warning
stands and the rest of the document still applies.

`avgen --export-bundle <dir>` (or File > Export Bundle) copies every referenced file into
`<dir>/assets/` (scene files are rewritten so their node assets point into the bundle, glTF
sidecar `.bin`/image files next to a `.gltf` come along) and writes `<dir>/project.json`.


## Render block (milestone 1.0, ADR-020)

```json
"render": { "width": 1920, "height": 1080, "fps": 60.0, "start": 0.0, "end": -1.0,
            "output": "video", "path": "renders/show.mov", "pattern": "frame_{:06d}.png",
            "codec": "prores422", "backend": "auto", "quality": 80, "muxAudio": true, "encoderThreads": 0 }
```

`end` < 0 means the audio duration (else the timeline's, else 10 s). `path` is relative to the
project. `output` is `sequence` (tone-mapped 8-bit PNG files named by `pattern`), `exr`
(scene-linear half-float OpenEXR files, before tone mapping; `pattern` defaults to
`frame_{:06d}.exr` when absent) or `video`. Codecs: `prores4444`, `prores422`, `h264`, `hevc` on
the native macOS backend, or any encoder name for a user-supplied ffmpeg (`backend: "ffmpeg"`).
CLI overrides: `--render <out>`, `--format png|exr|video`, `--size`, `--fps`, `--range a:b`,
`--codec`, `--quality`.

## Outputs block (milestone 1.2)

```json
"outputs": [
  { "name": "left", "display": 1, "fullscreen": true, "enabled": true,
    "mapping": { "crop": [0.0, 0.0, 0.55, 1.0], "blend": [0.0, 0.1, 0.0, 0.0], "blendGamma": 2.2 } },
  { "name": "right", "display": 2, "fullscreen": true,
    "mapping": { "crop": [0.45, 0.0, 0.55, 1.0], "blend": [0.1, 0.0, 0.0, 0.0],
                 "corners": [[0.02, 0.0], [1.0, 0.01], [0.98, 1.0], [0.0, 0.99]] } },
  { "name": "preview", "width": 960, "height": 540, "borderless": false, "alwaysOnTop": true,
    "mapping": { "brightness": 0.8, "gamma": 1.1, "flipX": false, "flipY": false } }
]
```

An array of output windows (`app::OutputManager::toJson/fromJson`); absent or empty means the
main window only. Every field but `name` is optional: `display` (index into
`platform::Window::displays()`, -1 = the default display, default -1), `fullscreen` (borderless
desktop fullscreen on that display, default false), `width` / `height` (points, used when not
fullscreen, default 1920x1080), `borderless` (default true), `alwaysOnTop` (default false),
`enabled` (default true; an output closed by the user is saved disabled), `mapping`. Names must
be unique. `mapping` is an `OutputMapping` (see `docs/rendering.md`, Outputs): `crop`
`[x, y, w, h]` in 0..1 of the source (default full), `corners` four `[x, y]` pairs in target
space for the source's TL, TR, BR, BL corners (default the unit square), `blend`
`[left, right, top, bottom]` widths in 0..1 (default 0), `blendGamma` (2.2), `brightness` (1),
`gamma` (1), `flipX` / `flipY` (false). Loading validates the whole array before replacing the
set: a bad crop, a concave or degenerate quad, widths outside 0..1, non-positive gammas, a
duplicate or empty name are errors.

## Render queue files

```json
{ "format": "avgen-render-queue", "version": 1,
  "jobs": [ { "project": "shows/a.json" },
            { "project": "shows/b.json", "render": { "path": "renders/b_4k.mov", "width": 3840, "height": 2160 } } ] }
```

Run with `avgen --queue jobs.json`. Each job's settings are the project's `render` block with
the job's `render` fields merged over it; paths in the queue file are relative to it.

## `"sequence"` — the cinematic sequence (ADR-089)

Optional, and read **last**, after every parameter a bake could name exists. The full user guide is
[docs/sequencer.md](sequencer.md); this is the shape on disk.

```json
"sequence": {
  "name": "night-shift",
  "duration": 105.0,
  "scenes": [ { "id": "plaza", "node": "plaza", "file": "plaza.scene.json" } ],
  "shots": [
    { "name": "The Walk", "start": 20.0, "duration": 20.0, "scene": "plaza",
      "in":  { "kind": "cut", "seconds": 0.0 },
      "out": { "kind": "fadeOut", "seconds": 1.4 },
      "camera": { "kind": "keys", "samples": 24, "lookAtActor": "hero",
                  "lookAtHeight": 1.15, "lookAtWeight": 1.0,
                  "keys": [ { "time": 0.0, "position": [-77, 15, 1.1],
                              "target": [-50, 1.2, -3.4], "interp": "linear",
                              "focalLength": 42.0 } ] },
      "tracks": [] } ],
  "actors": [
    { "id": "hero", "node": "walker", "visible": true,
      "keys":  [ { "time": 0.0, "position": [-62, 0, -3.4], "interp": "smooth" } ],
      "clips": [ { "time": 12.0, "clip": "Walk", "speed": 1.0 } ] } ],
  "overlays": [
    { "id": "lyric007", "kind": "text", "content": "EVERY WINDOW", "style": "lyric",
      "start": 49.0, "end": 52.15, "order": 10, "anchor": [0.5, 0.135],
      "pivot": [0.5, 0.5], "size": 1.0, "rotation": 0.0,
      "color": [0.97, 0.95, 0.92, 1.0], "preset": "fadeInOut", "presetSeconds": 0.32,
      "extra": {} } ],
  "markers": [ { "time": 40.0, "name": "CHORUS", "kind": "section" } ],
  "tracks": [ ]
}
```

`shots[].camera.kind` is `inherit`, `move` or `keys`; a `move` carries an `app::Shot` in the
ADR-062 vocabulary. Camera and shot-local track times are **relative to the shot's start**, so
moving a shot moves its automation with it; actor, overlay, marker and sequence-level track times
are absolute. `transitions` are `cut`, `fadeIn` and `fadeOut`. `overlays[].extra` is carried
verbatim and handed to the layer system untouched, which is where anything the four built-in styles
(`lyric`, `title`, `caption`, `credit`) do not cover goes.

**What is deliberately not written.** The tracks the bake produced, the layers it realised and those
layers' parameter values are all derived from the sequence and are re-created on load. Writing them
as well would mean a load that reads them *and* re-bakes them, and two tracks writing
`camera/position` is not a blend — it is whichever one the timeline applies second. So `"timeline"`
holds what the author keyed by hand, `"composition"` holds the layers the author made by hand, and
`"sequence"` holds the piece.

## `"composition"` — the 2D layer stack (ADR-083)

Optional, versioned separately from the envelope, and read *before* `"parameters"` so that the
`layers/<id>/...` paths exist when their saved values arrive.

```json
"composition": {
  "version": 1,
  "reference": { "width": 1920, "height": 1080 },
  "layers": [
    {
      "id": 3, "kind": "text", "name": "line 1", "enabled": true,
      "blend": "normal", "start": 10.0, "end": 19.0,
      "position": [0.5, 0.185], "scale": [1.0, 1.0], "rotation": 0.0,
      "anchor": [0.5, 0.5], "opacity": 1.0, "color": [0.95, 0.99, 1.0, 1.0],
      "text": "the valley keeps its own light",
      "font": { "family": "Avenir Next", "weight": -0.4, "fallback": "Helvetica Neue" },
      "size": 0.052, "align": "center", "tracking": 0.045, "lineSpacing": 1.15,
      "glow": 0.028, "glowColor": [0.35, 0.8, 1.0, 0.45],
      "shadowOpacity": 0.35, "shadowOffset": [0.008, -0.01]
    },
    {
      "id": 1, "kind": "shape", "name": "frame", "enabled": true,
      "blend": "normal", "start": 0.0, "end": 0.0,
      "position": [0.5, 0.5], "scale": [1.0, 1.0], "rotation": 0.0,
      "anchor": [0.5, 0.5], "opacity": 1.0, "color": [1.0, 1.0, 1.0, 0.0],
      "shape": "rectangle", "size": [1.66, 0.9], "cornerRadius": 0.006,
      "strokeWidth": 0.0022, "strokeColor": [0.72, 0.93, 1.0, 0.5]
    }
  ]
}
```

`layers` is in compositing order: index 0 is drawn first, nearest the 3D render. `id` is unique
within the composition and is what the parameter paths and the timeline tracks use, so renaming a
layer never orphans a track. `end` at or before `start` means "until the end". `reference` is the
frame the composition was authored against; nothing about layout depends on it, because positions
are normalised and sizes are relative to the frame height (ADR-083).

Every animatable layer property is also an ordinary parameter and therefore also appears in
`"parameters"`. The two are kept in agreement by pulling the parameter bases back into the
authored fields before every save. A `version` newer than the reader's is rejected and nothing is
mutated; an unknown `kind` is an error for the same reason.

Fonts are referenced, never bundled: a project carries a family and a PostScript name, and the
machine that opens it supplies the face. An absent face is reported loudly (log at error level,
and in red in the inspector) rather than silently substituted.
