# Project format

Decision: ADR-010 (serialisation) and ADR-011 (what is serialised). Implemented in
`src/params/serialization.cpp`; used by tests today and by the project system in 0.9.

Version 2 (milestone 0.3) adds modulation sources, presets and per-route polarity. Version 1
documents (no `sources`/`presets`, routes without `polarity`) load unchanged.

```json
{
  "format": "avgen-project",
  "version": 2,
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
  reader's is rejected; older versions will be migrated in order.
- Paths are the identity for parameters everywhere: UI, presets, OSC addresses (`/orb/scale`).


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
    { "name": "backdrop", "kind": "scene", "asset": "scenes/backdrop.json", "position": [0, 0, -6] }
  ]
}
```

Node kinds: `gltf` (a glTF 2.0 file; instances of the same file share meshes and textures),
`orb` (the built-in orb mesh and material), `grid` (the reference floor), `particles` (a GPU
particle system, every `ParticleSystem` field optional with the defaults from `docs/rendering.md`),
`scene` (another scene file). `camera.distance` 0 means "fit to the scene bounds".

Parameters a composition registers (all saveable in a project and modulatable):

| Path | Meaning |
|---|---|
| `nodes/<name>/position`, `rotation` (degrees), `scale` | offset from the node's rest transform |
| `nodes/<name>/visible`, `emissiveBoost`, `roughnessScale` | per-instance overrides |
| `nodes/<name>/nodes/<child>/…` | the same for nodes of a nested scene |
| `particles/<name>/…` | the particle node's system (see `docs/rendering.md`) |
| `camera/distance`, `height`, `orbitSpeed`, `fov`; `env/intensity`, `env/rotation`; `scene/brightness`, `scene/gridIntensity`; `root/scale`, `root/rotationSpeed`, `root/impulse` | as in the orb and glTF scenes |
