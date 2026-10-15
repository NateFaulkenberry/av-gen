# Digital Ocean: the scene guide (intent, controls, mappings, implementation)

**Open it:** File > Examples > Live > Sonic Abstract > Digital Ocean (`examples/sonic-abstract/digital-ocean.json`).
Live input turns on when it opens. Generated -- never hand-edit the JSON: `python3 tools/sonic_vfx/abstract.py build
digital_ocean` rewrites the project, the scene, the meshes and its own index entry.

## Artistic intent

**The Tidewriter.** A stylized ocean at electric twilight seen from a still, composed camera 12 m over the water: the
horizon on the upper 40 %, a low cream sun on the right third with its column of glints, a hazy island on the left
third, twilight stratus lit coral round the sun. Gerstner swells roll in from under the sun in three trains that never
realign. The music is written into the sea: what the bass played seconds ago arrives at your feet as swell; every kick
sends a lifted front in from the horizon; dolphins leap across the sun's path. It is beautiful with no input at all,
and a performer can raise a hero wave, fire a leap, bloom the crests, flare the sun or let night fall.

Palette: indigo and violet water, cyan light through backlit crests, mint foam, a violet-to-coral sky, a cream sun with
a pink halo. Its two related ends are on controls, read as a time of day: golden hour (CC 7) and a bioluminescent
night (CC 1: navy sea, emerald crests and foam, a pale moon).

## What to play

A full mix with a clear low end (anything with a kick and a bass), or a drum-heavy track; synth notes and chords over
it light the sun's path. Quiet, sustained music gives a calm sea with a swelling halo; dense percussion gives spray and
glints; a bass-heavy drop raises tall swell rolling in.

## MIDI (any device, any channel)

| control | does |
|---|---|
| CC 1 | NIGHT: dusk falls into a bioluminescent night |
| CC 2 | SWELL: the long and cross swells rise |
| CC 3 | CHOP: the wind sea roughens, more glints |
| CC 4 | FOAM: foam caps on more crests, brighter |
| CC 5 | SUN: the halo, glow and the sun's path |
| CC 6 | TIDE: how deep the music writes into the sea (the bass-history swell, x0.4 to x2) |
| CC 7 | GOLDEN: toward golden hour (use CC 1 or CC 7, not both) |
| CC 8 | SPRAY: how much spray and sparkle the drums and notes throw |
| pad 36 | LEAP: a dolphin breaks the surface and arcs across the sun's path (~4 s) |
| pad 37 | HERO WAVE: one big swell rises 240 m out and rolls in to your feet, foaming (~18 s) |
| pad 38 | BLOOM: bioluminescence along every crest and in the foam, fading over ~3 s |
| pad 39 | FLARE: the sun flares; halo, path and glints blaze and settle (~2 s) |
| pad 40 | SPRAY: a gust of spray off the near swell |
| notes | sparks on the sun's path: low notes near, high notes far, velocity is how many |

The Live panel's Response sliders shape every `response.*` signal below. On this machine the Live panel's saved MIDI
device can override the project's `*` filter: pick the controller there.

## Audio mappings

| music | the sea's response |
|---|---|
| bass (32-75 Hz history) | TIDEWRITING: the last ~15 s of the bass laid out as swell rolling in from 400 m at 26 m/s |
| kick (low-band onsets) | a lifted front rolls in from 70 m (~3 s); crest light and foam flash |
| level | the long swell grows (slowly) |
| intensity (12 s) | the crests sharpen as a piece builds |
| mids | the wind sea roughens |
| snare | spray off the near swell |
| hats, highs | the glints on the sun's path flare and brighten |
| sustain | the sun's halo swells, its path warms |
| brightness (slow) | the palette drifts a little warmer or cooler |
| silence | the swell rolls on, the clouds drift, pods leap every 53 and 79 s |

## Implementation

| part | how | where |
|---|---|---|
| the sea | three camera-centred fans (near/mid/far, 78k vertices): rows spaced for ~1.5 px each at 1080p | `ocean_rings`, `ocean_wedge` |
| waves | three Gerstner trains = pairs of world-space `sine` deformers (vertical + along the travel, a quarter-cycle apart; steepness sum < 1) | `gerstner`, `SWELLS` |
| the music in the sea | field deformers: `tide` = compound(spectrum low band with `audioSmooth`, planar sine); `lift` = compound(kick `onset` fronts, the pad-triggered hero `wave` pulse) | `audio_fields` |
| sea colour | unlit programs: near (48 ops) and far (flattened normals, 44 ops) | `sea_program` |
| sky | a dome with a 31-op program: gradient, glow, halo, disc, far water | `sky_program` |
| clouds | 196 squashed spheres (`points` distribution) turned round the viewer by a world-space `twist` | `cloud_points`, `cloud_program` |
| dolphins | a generated faceted mesh, a local `sine` (tail kick) and a `path` deformer on a scene spline; pods on LFO saws, the hero on an envelope | `dolphin_mesh`, `dolphins` |
| splashes, spray, sparks | fixed-pool particle systems, opened by window routes | `splash`, `spray` |
| palettes | every program colour recorded; one control routes (target - twilight) per channel | `Prog.pcolour`, `palette_routes` |
| MIDI | `control.midi.bindings` cc -> signal, noteEvent -> signal | `control_map`, `midi` |
| camera | still; a seamless 26 s / 71 s breath | `camera` |
| review | `REVIEW_MIDI` scores the MIDI offline as timeline sources | `score_midi` |

Engine change: ADR-1370 (a spectrum field can read a smoothed history). Tool changes: the probe's `controller`
scenario (`tools/sonic_live_probe.cpp`), `kit.Scene.sources` and `kit.upsert_index_entry`.
