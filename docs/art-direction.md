# Art direction: the World Director, looks and composition

Status: implemented (ADR-038, ADR-041). This is the layer that decides what a frame is about and
how it should feel. None of it is a second system: a director knob is a world macro, a look is a
preset, and composition data is a set of fields.

## 1. The World Director

Eighteen words a world can be directed with:

| Word | What it should move |
|---|---|
| scale | structure size, spacing, fog depth, camera distance |
| density | instance counts, filters, particle rates |
| drama | lighting contrast, shadow depth, atmosphere, selective emission |
| contrast | grade contrast, key-to-fill ratio, exposure |
| warmth | light colour temperature and the grade's white balance |
| mystery | fog density and falloff, darkness, occlusion |
| energy | motion amplitude, emission, particle output |
| depth | atmospheric perspective, depth of field, parallax |
| atmosphere | volumetric density, scattering, shafts |
| motion | speed of everything that moves |
| chaos | noise amplitude, turbulence, randomised variation |
| stillness | damping, slower fields, a held camera |
| focus | clearance, framing strength, depth of field |
| glow | emissive intensity, bloom, halation |
| organic | curvature, irregular variation, soft motion |
| mechanical | regularity, hard edges, metallic response |
| sacred | symmetry, verticality, shafts, restraint |
| alien | unusual palette, asymmetry, strange emissives |

A world declares which words it implements and where each one reaches:

```json
{ "name": "infinite-temple",
  "knobs": [
    { "knob": "drama", "default": 0.4, "targets": [
        { "path": "scene/keyLight", "min": 0.3, "max": 2.2 },
        { "path": "scene/volumeDensity", "min": 0.01, "max": 0.09 },
        { "path": "post/output/vignette", "min": 0.15, "max": 0.55 } ] } ] }
```

Each knob becomes the macro `macros/<word>` and each target an ordinary route with a remap. The
Direction tab shows the knobs with their descriptions and lets you open a knob to see exactly
which parameters it moves. Directors are saved with the project.

## 2. Looks

A look is a parameter snapshot restricted to visual prefixes: exposure, lens, post, scene,
environment, light rig and macros. It deliberately cannot carry geometry, so the same world can be
rendered under a different visual identity without becoming a different world.

Eight ship under `examples/looks/`: Monumental, Sacred, Alien, Industrial, Bioluminescent, Cosmic,
Dreamlike and Hyperreal. Applying one reports how many values landed and how many the world does
not have, so a look that half-fits says so rather than failing silently. Capture the current state
as a new look from the same tab.

## 3. Composition

Composition data says what the frame is about, and reaches generators as fields:

```json
"composition": {
  "focalPoints": [ { "name": "hero", "position": [0, 5, 0], "radius": 12, "clearance": 8, "weight": 2 } ],
  "layers": [ { "name": "near", "start": 0, "end": 25, "density": 0.4, "detail": 1.5 },
              { "name": "far", "start": 25, "end": 300, "density": 1.5, "saturation": 0.6 } ],
  "exclusions": [ { "name": "well", "shape": "sphere", "radius": 6 } ],
  "cameraTarget": "hero", "targetScreenPosition": [0.333, 0.5], "framingStrength": 0.6 }
```

- **Focal points** publish `composition.clearance.<name>` (0 inside the clearance, 1 outside) and
  `composition.weight.<name>` (peaks on the point). Multiply a density filter by the first to open
  space around the subject; drive an emission or scale effector with the second to make it read.
- **Depth layers** carry density, contrast, saturation and detail per distance band, which is how
  atmospheric perspective becomes an instruction rather than an accident.
- **Exclusions** publish `composition.exclusion.<name>`: 0 inside the volume, 1 outside. Negative
  space as something you ask for.
- **Camera target** names the focal point the camera should frame and where on screen it belongs.

## 4. Musical phrasing

Above the beat: `beat.phrase`, `beat.phraseCount`, `beat.phrasePulse`, `beat.section` and
`beat.sectionCount`, from a phrase length in bars and a section length in phrases, both saved with
the project. State triggers accept `phrase` and `section`, so escalation happens over musical
structure instead of on every kick.

## 5. How much should react to audio

A guideline that holds up in practice: roughly four fifths of the motion autonomous, a sixth
audio-influenced through slow envelopes on macros, and a twentieth event-driven punctuation from
onsets and phrase boundaries. Route audio to director knobs and states, not to individual object
parameters, and reserve onsets for moments that deserve them.
