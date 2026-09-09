# Scene states, world macros and the authoring layers

Status: implemented (ADR-031). Nothing here is a second parameter system: states are presets,
macros are routes, the layers are a filter over the one parameter set.

## 1. World macros

A world macro is a knob (`macros/<name>`, published as the signal `macro.<name>`) plus a list of
targets. Each target becomes an ordinary modulation route with a remap, so the value reaches the
parameter through the normal chain and is visible in the Routes tab, in projects, over OSC and in
offline renders.

```json
"worldMacros": [
  { "name": "energy", "label": "WORLD ENERGY", "default": 0.0,
    "targets": [
      { "path": "procedural/columns/distribution/radius", "min": 20, "max": 34 },
      { "path": "scene/brightness", "min": 0.4, "max": 1.2, "curve": "power", "curveAmount": 2.0 },
      { "path": "particles/dust/spawnRate", "min": 0, "max": 8000, "op": "replace" }
    ] }
]
```

- `min`/`max` map knob 0 → min and knob 1 → max (after `curve`, one of `linear`, `power`, `log`,
  `exp`, `scurve`).
- `op` is the route operation: `add` (default) offsets the base value, `replace` overrides it,
  and `multiply`, `min`, `max` behave as in any route.
- Editing a macro regenerates exactly its own routes; other routes are untouched. The Macros tab
  of the World window adds targets and knobs live.
- Because the knob is a parameter, it can itself be automated by the timeline, driven by audio or
  bound to a MIDI fader.

## 2. Scene states

A state names a preset and how to get there.

```json
"states": {
  "initial": "Dormant",
  "states": [
    { "name": "Dormant", "preset": "dormant" },
    { "name": "Awakening", "preset": "awakening",
      "transition": { "seconds": 6, "easing": "smooth", "quantize": "bar" },
      "triggers": [ { "kind": "onset", "threshold": 0.8 },
                    { "kind": "macro", "signal": "energy", "threshold": 0.6 } ] },
    { "name": "Overload", "preset": "overload",
      "transition": { "seconds": 1.5, "easing": "easeIn" },
      "triggers": [ { "kind": "beat", "every": 64, "from": "Ascension" } ] }
  ]
}
```

- **Transition**: `seconds` (0 = instant), `easing` (`linear`, `smooth`, `easeIn`, `easeOut`,
  `easeInOut`, `bezier` with `"bezier": [c0, c1]`), `quantize` (`none`, `beat`, `bar` — the
  transition waits for the next beat or bar before it starts).
- **Triggers**: `manual` (only the UI, `goToState` or OSC), `beat`/`bar` with `every`, `onset`
  with `threshold` against `audio.onsetStrength`, `signal` with any bus signal name and a rising
  (or `"falling": true`) crossing of `threshold`, `macro` with a knob name. `from` restricts a
  trigger to one current state; `target` sends it somewhere other than its own state.
- A transition is a preset morph, the same operation timeline cues use, so it interpolates every
  parameter both presets contain and leaves the rest alone. Structural values (counts, kinds) snap
  when they cross an integer boundary.
- The machine publishes `state.progress` (0..1 during a transition) and `state.index`, so the
  world can react to its own transitions.
- OSC: `/avgen/state/go <name> [instant]` or `/avgen/state/<name>`.

## 3. Authoring layers

The World window's layer selector filters the Parameters window:

| Layer | Shows |
|---|---|
| Beginner | `macros/*`, `scene/*` (atmosphere, brightness, fog), `env/*`, `post/*`, `camera/*`, `root/*` |
| Intermediate | the above plus `procedural/*`, `field/*`, `spline/*`, `sdf/*`, `material/*`, `particles/*`, `nodes/*` |
| Advanced | every registered parameter |

The filter never changes what exists; a hidden parameter is still automated, saved and reachable
over OSC. Presets, projects and renders are identical whatever the layer.

## 4. World overview and the inspector

The overview lists the flattened scene by family (architecture, fields, splines, SDF, particles,
materials, atmosphere, camera and lighting) with instance and node counts. Selecting an object
fills the inspector with:

- its counts, bounds, deformers, point operations and effectors;
- per-attribute statistics of its point cloud (min, max, mean);
- **why is this moving?** — for every parameter of the object, the routes (with the source signal
  and its last output), timeline tracks, cues, states and world macros that write to it, plus the
  base and final values.

## 5. Debug visualisation

The Debug tab toggles the inspection modes: instance points, bounds, normals, field frames and
sampled field vectors, splines, density and instance-id colouring, LOD and culling colouring, and
a horizontal SDF slice. Point size, the field sampling grid and depth testing are adjustable.
