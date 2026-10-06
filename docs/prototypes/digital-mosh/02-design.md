# DIGITAL MOSH: Design

The visual language comes from `01-research.md`. Principle numbers (S1, G4, P8 ...) refer to it. This
document describes what is built. Implementation notes and their measurements are in `03-implementation.md`.

## The concept in one sentence

**A painted dream that has lost its keyframe.** The world is a Dalí/Tanguy plain painted once, perfectly. The
music is the motion that keeps being applied to it. As the music presses on, the world stops being *seen* and
starts being *predicted*. It drifts, its prediction errors spread like an infection, and it finally decodes
into the units the renderer built it from. Then a keyframe arrives and the dream comes back intact, except
for one block that did not refresh.

## The world (brief §3, §14)

*Revised in art pass 1, after the owner asked for real landscape, a camera that travels, more craft and
painting-derived colour. Revised again in art pass 4, after the owner's notes: the land read as flat, the camera sat
too low, the light was flat, and stage changes read as screen tints.*

**Pass 4: the land as sculpture.** The land in pass 1 was a flat pan with ranges far off: the features existed, but
they sat at the edge of the world. Now:

- **Ground:** wind-warped dune fields and crested dunes cover the ground around the stage.
- **The knoll:** the olive stands on a knoll on the bank of a deep dry riverbed. The riverbed spills into a small salt
  pan, and the Tanguy object hovers over the pan.
- **Middle ground:** an eroded escarpment wall stands 200 m behind the pan, and two mesas and a butte at 130-160 m.
  They are close enough that their height and shadows read.
- **Distance:** the far ranges stay 3 km out, in the haze.

A sparse set of deliberate elements stands on a sculpted desert (`land.py`). Each one is chosen for what it lets
the corruption say.

| Element | Reference | Role | What corrupts in it |
|---|---|---|---|
| **The land**: two terrain nodes over one geography. A salt pan for the stage; a dry riverbed meandering into it as a leading line; long low swells and dunes; a Cap de Creus escarpment and two flat-topped mesas at the edge; ranges 3 km out, dissolving into the haze | Dalí's beach and Cap de Creus (S1); Tanguy's infinite plain (S10) | Real foreground, middle ground and distance for a travelling camera, and composed negative space made of land and sky | The ground's colour (ink), its tiling (macroblock cells), the palette of the whole land per stage |
| **The olive**: a trunk of three braided strands, a root flare gripping the ground, and three limbs with three levels of branching, built as five SDF objects | The dead olive of *The Persistence of Memory* (S1, S2), made our own | The protagonist of brief §4's chain | It bends (limbs `bend`), softens (the bark flows), is eaten from the root up (blocks), and its limbs detach and float (node positions) |
| **The Tanguy object**: a smooth mass pierced by a hole, a slender filament rising to a balanced bead, and a needle pointing down that stops a hand's width above its shadow | Tanguy's biomorphs (S11); the hovering of *Dream Caused by the Flight of a Bee* (S8) | Patient zero: the first block goes bad here | It doubles (P2), quantises into blocks, and is the contagion's source |
| **The long shadows**: a raking 12° sun from the side (pass 4; it was behind the stage), so every shadow runs about five times its caster's height across the frame and the dunes model, lit face against shadowed face | de Chirico (D2), Dalí (S3) | Free, enormous, and the first thing to lie | They swing against the sun (Uncanny) |

Negative space is land and sky. The far land ends 4 km out, inside the haze (`fogSky` over 3.2 km), so no frame
shows an edge.

### Colour (`05-palettes.md`)

Every stage's sky, land, light and haze is extracted from one painting:

| Stage | Painting |
|---|---|
| Dream | *Persistence* and the *Bee* |
| Uncanny | de Chirico |
| Infection | de Chirico's sky continues; the land stays the Dream's until the contagion reaches it (pass 4) |
| Corruption | as Infection; Ernst's *Europe After the Rain II* arrives only where the stain is: its rust ahead of its ink |
| Nightmare | *The Elephants* over Tanguy's *Slowly Toward the North* |
| Collapse | Tanguy's *Multiplication of the Arcs* |
| Respite | Magritte's *The Empire of Light* |

The contagion is Ernst's rot at full chroma, so it reads as one painting infecting another. In pass 1 each stage
swapped the whole frame's palette, and the owner read it, rightly, as screen tints. Since pass 4 only the systemic
stages change the palette everywhere: the Nightmare, the Collapse, and Magritte's Respite. Before them, the painting
stays intact except where the contagion has reached it.

### The flight (pass 5)

The owner asked for a soaring camera in place of vantages. Since pass 5 the camera flies one closed path over the
land (`flight.py`, camera mode 2), made of three petals. Each petal leaves the salt pan and returns over it, so the
stone and its contagion come back into view about every half minute, whatever the song's tempo:

- **West:** down onto the riverbed and upstream along it, under the floating rock, then back over the knoll, gliding
  past the tree.
- **East:** low over the dune crests past the east mesa, round through the southern dunes, then home over a crest.
- **North:** climbing over the escarpment's wall to the land and mesas beyond, past the giant at its foot.

The flight never stops:

- **Speed:** the camera's position along the path integrates the stage's pace and the music's energy (about 6 m/s in
  the Dream, 15 in the Nightmare, up to 4 m/s more on the energy).
- **Phrases and bars:** these still drive the camera, as before. They now change how it flies: its altitude over
  the path, how far ahead it looks, its lens, and its lean into turns (ADR-1166).
- **The Nightmare** also cuts the path itself forward and back on the bar, so the camera swoops somewhere else.
- **The Collapse** stalls the flight: the camera holds over the pan while the world comes apart, and Recovery
  resumes it.

### The landmarks (pass 5)

One tree and one stone were beautiful but not yet uncanny, and a flight needs things to pass. Three more were
added, each with a beat of its own:

| Landmark | Reference | Role |
|---|---|---|
| **An impossible flower**: a stem far too tall and thin to stand, and a stone bloom of seven petals, beside the stone | the brief's "impossible flower"; Dalí's stems | The first neighbour the contagion reaches: it carries the stone's infected surface |
| **A floating rock** with a small keep on its crown, over the riverbed | Magritte, *The Castle of the Pyrenees* | The Uncanny's first violation: absent in the Dream, there from the Uncanny on. The flight passes beneath it |
| **The giant**: the Tanguy object again, seven times its size, hovering at the escarpment's foot | Tanguy; P2 (the double) | Something whose scale cannot be judged from anything near it |

### The sky (pass 5)

Magritte's clouds are sculpted, solid-looking heaps far out over the land, lit by the same raking sun. They sit
1.5-2.8 km out, so the haze takes them into the sky's own colour, and they cost no volumetrics.

They drift on a wind, and the Uncanny stops the drift: a cloud that stops is the sky's first relation error. Their
colour follows the stage's light, so the Nightmare's blood light reaches them as well. Dalí's horizon band is the
sky's own gradient, and Tanguy's fog, where ground and sky merge, is the haze.

### The surface (pass 5)

The sand carries wind ripples and its own form:
- The ripples are noise stretched along the crests, faded with the pixel's footprint so they cannot shimmer.
- Convex crests are bleached and hollows darker.
- The near land is meshed at 1 m out to 180 m, so crest lines seen from the air do not step.

### The travelling camera (passes 1 to 4)

The camera moves through the world. It never sits locked off and never shakes. Each stage has two or three
vantages on the land, and the music's phrases (later, its bars) move the camera between them:

| Stage | Move | What the vantages show |
|---|---|---|
| Dream | 16 s glides | Pass 4: it opens behind a dune crest (only sand and sky), rises over it to the whole stage from 30 m, then descends into the riverbed toward the tree on its knoll |
| Uncanny | 12 s | The tree on the sky from the north, the double across the dunes from 24 m, low along the riverbed |
| Infection | 9 s | Toward the first bad block, then a wide reveal of the spread |
| Corruption | 5 s every 4 bars | Low and close, under the drooping limbs |
| Nightmare | 1.2 s jumps every 2 bars | Over the knoll toward the stained pan, a sudden 30 m height, ankle level; the horizon rolls |

Between moves, slow LFOs float the eye and the aim, more deeply as the energy grows. `build.py` checks every eye,
and every straight move between eyes, against the engine's terrain heights. A move is marked `idle` (ADR-1164) so
it never interrupts a stage's own transition.

## Corruption is a substance, not a filter (brief §6)

There is one world property, **contagion**: a scalar grid lying on the land (designed as Gray-Scott, built as an
advected, diffused scalar with a ceiling because Gray-Scott was too slow for a song; `03-implementation.md`). It is
injected at the Stone, so it grows outward as a living front. Everything that is corrupted samples this one field, so corruption is spatially coherent: the
tree is infected where the stain reaches it, from the root up. The domains:

| Domain | How it is built | Read from |
|---|---|---|
| **Geometry** | Voxel shells: cubes on the forms' own surfaces that appear where the contagion has eaten the smooth SDF surface (`displaceField`), then lift, tumble and float through field effectors; limbs that bend like wax and detach; bark that flows | contagion grid, `melt` and `lift` fields, bass |
| **Temporal** | The temporal ring: mosh (blocks replaced by older blocks, G1/G2), echo, slit-scan. Freezes: a cloud stops; the camera's orbit stops while the music continues (G11) | corruption × percussion |
| **Spatial** | The Stone's double appears where it should not be (P2). The Headland duplicates nearer. A dolly-zoom (FOV against distance) breaks perspective (D1). The horizon steps (the plain's far half offset) | states, macros |
| **Material** | The bark takes the stone's surface and the stone takes the bark's (S14, collage). The material colour migrates along the contagion. Specular goes wrong | material programs reading the contagion field |
| **Image space** | Block glitch, tears, posterise, pixelate, sort, split: **only** on transients, **only** above Corruption, and only as the Collapse's strata (G7, §16) | corruption × onsets |

**One law per event (S2).** Each stage adds exactly one kind of violation to what the previous stage left.

## Colour contagion (brief §7)

The palette is the temperature of the dream:

- **Dream:** cream, ochre, Catalan blue, peach horizon, lavender shadow.
- **Uncanny:** the light turns slightly cold: turquoise in the shadows, a yellow-green cast in the sky.
- **Corruption:** the colours of a failing YUV decoder. Magenta is overflowing chroma and chartreuse is zeroed
  chroma (research, Part 4), so the corruption colour is the medium's own failure colour, not a choice of
  "neon".

A corruption event introduces a colour that propagates through every channel the renderer has. As built (pass 4) it
reaches them in this order:

1. its light: the fracture light's reach grows from 12 to 28 to 50 m;
2. its spores: strain-coloured particles drifting out from the stone;
3. the haze that light passes through;
4. the ground: Ernst's rust a few metres ahead of the ink cells;
5. the tree.

Everything it has not reached keeps the painting.

The design, as first written, was this **magenta fracture** (the stone's first bad block):

1. **Fracture.** The stone's blocks emit magenta (its material program, masked by quantisation).
2. **Magenta light.** A point light at the stone, its intensity driven by the corruption, falls on the plain
   and the tree.
3. **Magenta particles.** Motes rise from infected ground (`emitMaskField` = the contagion).
4. **Magenta scattering.** The haze takes the contagion's colour (`volumeColorField`).
5. **Magenta vegetation.** The bark's program mixes toward magenta where the contagion reaches it, in blocks.
6. **Magenta reflections.** The stone's glossy specular picks up the magenta light.
7. **The neighbours inherit it**, because the contagion field is shared.

A second strain, chartreuse, takes over when the music is bright (spectral centroid). The Gray-Scott
parameters then shift toward spots, so the music's timbre changes the *species* of the infection.

There are periods of restraint: Recovery is the dream palette exactly, and in every stage up to Infection the
corrupted area is less than 10% of the frame.

## Audio mapping, in four layers (brief §10, §12)

Nothing is keyed to a song. Every response reads the live bus (`audio.*`, the band onsets, spectrum and onset
fields), and the arc reads only accumulated energy.

| Layer | Signal | Target | Meaning |
|---|---|---|---|
| **Micro** (ms) | treble, hat onsets | glints on the infected cells (the material program's `audio` input); dust-mote sparkle; at Corruption and above, small block flicker | high = fine detail, edges, pixels |
| | spectrum field (element band) | each infected ground cell hears its own band and flickers in it | the stain is *listening* |
| **Rhythmic** (beat) | kick (`onsetLow`, the `onset` field) | a front travelling out from the Stone across the plain, which lifts the fragments and flashes the blocks; mosh bursts scaled by corruption | kick = fracture, shockwave |
| | snare (`onsetMid`) | a contagion injection pulse (the colour propagates on the beat); tears and glitches from Nightmare on | snare = spread |
| | bass level | the tree's sag (flow amount), liquid swells of the plain near the infection | bass = mass, gravity |
| **Musical** (phrase) | `energy` macro (a slow follower of loudness, flux and air) | haze density, camera height and pace, light temperature | intensity = how deep the dream is |
| | spectral centroid (slow) | melt (dark sound) against blocks (bright sound), magenta against chartreuse, Gray-Scott feed and kill | timbre = the kind of corruption |
| **Macro** (song) | `dose` macro: the bounded integral of energy above a threshold (ADR-1161) | the stage, through scene states on `visual.depth` | sustained intensity = how corrupted the world has become |

**The arc.** `depth = 0.7 × dose + 0.3 × energy` drives a hysteretic state ladder. This is the PHONOTAXIS
pattern, plus a memory:

```text
Dream --depth>0.12--> Uncanny --0.28--> Infection --0.45--> Corruption --0.65--> Nightmare --0.86--> Collapse
  ^                                                                                                   |
  |                                         (energy falls through 0.22 from Corruption and above)     |
  +-------- Recovery <--------------------------------------------------------------------------------+
            (the keyframe: dose is masked, the dream palette returns; one block stays wrong)
```

- Intro, verse, build, drop, breakdown and climax are not detected. They *emerge*:
  - an intro is quiet, so the dose stays low: Dream;
  - a verse accumulates slowly: Uncanny;
  - a build raises the energy: Infection;
  - a drop is high energy on top of an accumulated dose: Corruption, then Nightmare;
  - a breakdown makes the energy fall: Recovery (temporary, because the dose is still high and it re-infects
    fast);
  - only the final climax, long into the song, reaches the dose that Collapse needs.
- The Collapse is a chain of sub-states advanced on the bar grid. Each one exposes one stratum of the
  renderer: geometry, fragments, particles, temporal fragments, pixels, colour, light (G10, brief §15). Then
  comes an instant cut to Recovery (brief: "everything suddenly becomes calm").
- Live input runs the same machine on the input's bus. The only per-track trim is the `sensitivity` macro, as
  in PHONOTAXIS.

## Camera (brief §13)

See "The travelling camera" above. It replaced the first build's orbit, which the owner found static.

## Quality tiers (brief §11)

One scene and one artistic system. The fidelity levers:

| Lever | Offline (render) | Live (LIVE AUTO) |
|---|---|---|
| Resolution | 1080p to 4K, supersample | the live ladder's render scale |
| Haze | 24-32 steps, full-resolution volume | the ladder's quarter-resolution volume and half steps |
| SDF march | `maxSteps` 128 | the tier's step caps |
| Contagion grid | 128², 60 Hz steps (exact seek) | the same (it costs well under 1 ms) |
| Particles | full spawn rates | `particleSpawnScale` |
| Temporal ring | full resolution (Offline tier) | half resolution |
| Image-space glitch | full taps | half taps (Preview) |

The live project adds a `live` block (`effects_first` priority, so the ladder drops resolution after the
effects that carry no meaning). Heroes (tree, stone) are never degraded.

## Engine additions (ADR-1160 to ADR-1164)

Each one is small, tested, and reachable from scene JSON, the CLI and the editor:

- **ADR-1160:** a raymarched SDF casts its shadow from the light's view. A defect fix: SDF shadows were marched
  from the camera's eye, over the camera's rect, and dropped when the caster was off screen. The scene's first
  frame had no tree shadow.
- **ADR-1161:** an integrating route can be bounded (`integrateMin` / `integrateMax`), which gives the dose.
- **ADR-1162:** a material-program `quantize` op: cell centres and a per-cell random, for squares and macroblock
  masks.
- **ADR-1163:** a scalar grid can saturate (`ceiling`): injection stops at a level, so the contagion spreads rather than
  piling up at the source.
- **ADR-1164:** state triggers can `hold` (fire while a condition holds), be `elapsed` (fire after a state has lasted N
  seconds) and be `idle` (wait for the running transition to land). These give the hold ladder, the timed collapse
  strata and camera moves that never interrupt a stage's own morph.

**Considered and not built:**

- **An SDF `quantize` node.** A correct distance bound for "this cell is blocks, its neighbour is not" needs the
  neighbours' distances, which the stack interpreter cannot give without seven evaluations per sample. The
  cheaper bounds draw phantom walls on the cell faces.
- **An SDF `flow` warp.**

The blocks are real instances instead. Python voxelises the tree's and the stone's own skeletons into a shell of
cubes (a `points` distribution); field effectors reveal, displace, tumble and float them; and the SDF surface
recedes beneath them through `displaceField`. One system serves as the blocks, the fragments and the pixels.
Melting is `bend` on each limb, rooted at its base, plus displacement.
