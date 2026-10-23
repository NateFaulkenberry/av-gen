# The Living Prism 08: Stage 4, the musical behaviour

**Media:** `~/Desktop/av-gen-review/82-living-prism/stage4/`:
- `feline-typical-window.mp4` (65.70-85.53 s) and `feline-heaviest-window.mp4` (123.22-143.05 s): with audio,
  analysed from the three stems;
- `still-1` to `still-6`: the six stations, rendered with the song playing at those seconds.

**Where it lives:**
- `examples/living-prism/music.py`: the signals and routes, the whole mapping in one file.
- `living-prism-offline.json`: the same world playing the mix, with the stems as analysis sources (ADR-1520), written
  by `build_scene.py`.
- `living-prism.json`: the live project, which has no audio file.

## The signal map (spec deliverable 5)

Every musical idea is named once as a conditioned signal (ADR-1602, on main):
- The stem input is first (gated on the stem being present) and the mix is the fallback (`firstActive`).
- Every route reads a signal.
- **No route reads the mix's kick, snare or hat classes** (option A).

**Calibration:** the drum stem's bands on Feline Footwear's grid (trace, heaviest window):
- **bass:** 0 → 0.67 on each kick;
- **high-mid:** 0.80 and 0.58 on the claps, at most 0.18 elsewhere;
- **treble:** the hats.

| Signal | Stem input (primary) | Mix fallback | Shape | Destinations |
|---|---|---|---|---|
| `hit` | `stem.drums.bass` | `response.onset` | flash, 0 attack, 160 ms decay | the spire's core (+3.2 emissive) and its glow light, the water's sparkle; a 2.5 s afterglow in the spire (memory) |
| `front` | `stem.drums.bass`, latching linear fall (one beat, refractory 300 ms, rearm 0.25) | `response.onset`, the same | 1 → 0 over 0.55 s per kick | the **front field**: radius = 13 m x (1 - front), amplitude = front. A thin spherical shell of light leaves the spire's base on each kick and crosses the shore, the rock, the arch and every crystal (all four programs read it), fading as it goes |
| `accent` | `stem.drums.highMid` (the clap) | `response.onsetEnv`, strongest onsets only | 4 ms attack, 300 ms decay | the geode (+2.6) and its light, the arch's crust (+1.4), the rock's seams (+0.9), with a 3 s afterglow in the seams |
| `glint` | `stem.drums.treble` (the hats) | `audio.treble` | 3 ms attack, 110 ms decay | the mirror facets of all the dense crystal (`sparkleGain`) |
| `bass` | `stem.bass.onset`, a latching linear fall, with the bass stem's level as depth | `response.bass` | each note a push of about 450 ms | the fan's blades, the chandelier, the passage light, the water's swell (3 cm), the seams |
| `synth` | `stem.synth.level` | `response.sustain` | 250 ms attack, 1 s decay (through the sidechain pump) | the druse clusters' and the crust's internal light, the roof wash |
| `bright` | `stem.synth.centroid` | `audio.centroid` | about 2 s | the tips pale (`tipMix`), the saturation lifts |
| `energy` | the three stems' levels, summed | `response.level` | 2 s attack, 5 s decay | feeds `section` |
| `section` | `signal.energy` | (the same) | 8 s attack, 16 s decay | the phrase scale: bloom, the spire's and the arch's light, the back wash |

**Verified on the trace** (`avgen --sonic-trace`, the offline project, the whole song):
- `front` is a clean one-beat sawtooth on every kick.
- `hit` peaks on every kick.
- `accent` fires on the claps and rests at 0.05-0.1 between them.
- Every impact signal is 0 from 182.7 s, where the drums stop dead, so nothing latches.
- `section` decays to 0.2 by the end.
- `--audit-routes`: 36 musical routes live and 0 dead. The two hazards are the CC-only palette routes, which an
  offline audit cannot move.

## Timescales (spec section 8.3)

| Scale | What happens |
|---|---|
| Immediate | the spire's flash, the geode and arch on the clap, the hat glints |
| Travelling | the front, visible for its 0.55 s |
| Material accents | the spire's 2.5 s afterglow, the seams' 3 s afterglow |
| Phrase | `section` over 8-16 s |

Every accumulation is a one-pole decay or a linear fall, so each is bounded by its input and returns to rest.

## Palette

- **Automatic changes:** a look changes only for a musical reason. On a phrase boundary where the music's character
  has moved (the selector's change detector over `sonic.energy`, `sonic.brightness` and `sonic.density`), never more
  often than every 45 s.
- **Order:** Ultraviolet, Abyssal, Glacier, Opaline, Solar Mineral, Wild Spectrum.
- **CC 9** selects a look; **CC 10** locks it, which stops the automatic changes.
- **On Feline:** the song leaves Ultraviolet for Abyssal at about 60 s, and Glacier follows later (visible in the
  window clips).

## MIDI

| CC | Control | Effect |
|---|---|---|
| 1 | `impact` | boosts hit, front, accent and glint, up to 2x |
| 2 | `world` | boosts `section`, up to 2x |
| 9 | `palette` | selects a look |
| 10 | `paletteLock` | locks it |

**Engine gap:**
- A `control.*` channel reads 0 until its controller first moves (`ControlSource` has no rest value), so a CC used as
  a depth would silence its effect at load.
- CC 1 and CC 2 are therefore boosts (at 0, the authored strength).
- A per-binding rest value, or soft takeover (P1-12), would let them be true intensity knobs that can also reduce.

**Not yet tested:** the CCs, during playback with a controller. No MIDI device or virtual port is available to this
agent tonight (no `mido` or `rtmidi`).

## Waiting on

- **P1-1 (the onset register):** the clap/kick split could read the register instead of band gates.
- **P1-3 (sound-to-light):** needed to report latency.
  - The impacts are one hop: the drum stem's band, a signal chain on the same frame, then a route to a parameter.
  - P1-3 must still measure it.
