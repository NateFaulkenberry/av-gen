# Project format

Decision: ADR-010 (serialisation) and ADR-011 (what is serialised). Implemented in
`src/params/serialization.cpp`; used by tests today and by the project system in 0.9.

```json
{
  "format": "avgen-project",
  "version": 1,
  "parameters": {
    "orb/scale": 1.0,
    "orb/baseColor": [0.75, 0.2, 0.9],
    "scene/brightness": 1.0
  },
  "routes": [
    {
      "source": "audio.bass",
      "target": "orb/scale",
      "component": -1,
      "amount": 1.2,
      "op": "add",
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
    }
  ]
}
```

Rules:
- `parameters` holds base values only (finals are derived every frame); numbers for float/int,
  booleans for bool, arrays for vectors and colours. Only parameters flagged `serialized`.
- Enums are lower-case strings: curve `linear|power|log|exp|scurve`; threshold
  `none|gate|binary|subtract`; envelope `none|peakhold|linearfall`; op
  `add|multiply|replace|min|max`.
- Missing chain keys take defaults; unknown enum strings are errors.
- Loading validates everything first and changes nothing on failure. Unknown parameter paths are
  skipped with a warning (forward compatibility); a type mismatch is an error. `version` greater
  than the reader's is rejected; older versions will be migrated in order.
- Paths are the identity for parameters everywhere: UI, presets, OSC addresses (`/orb/scale`).
