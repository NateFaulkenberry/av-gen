# Project format

Decision: ADR-010 (serialisation) and ADR-011 (what is serialised). Implemented in
`src/params/serialization.cpp`; used by tests today and by the project system in 0.9.

Version 2 (milestone 0.3) adds modulation sources, presets and per-route polarity. Version 3
(milestone 0.8) adds `"shaders"` (written by the engine since 0.4) and `"timeline"`. Version 4
(milestone 0.9) adds `"assets"` (audio / scene / environment references) and `"app"`
(`{ "name", "version" }` of the writer); both are filled in by the engine.

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
project. `output` is `sequence` (PNG files named by `pattern`) or `video`. Codecs: `prores4444`,
`prores422`, `h264`, `hevc` on the native macOS backend, or any encoder name for a user-supplied
ffmpeg (`backend: "ffmpeg"`). CLI overrides: `--render <out>`, `--size`, `--fps`, `--range a:b`,
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
