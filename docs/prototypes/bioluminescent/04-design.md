# THE RIFT: how the music becomes the world

The scene source is `examples/bioluminescent/build.py`. The generated `rift.scene.json` and `meshes/` are committed
(regenerate them with `python3 examples/bioluminescent/build.py`). Brief §8-§12.

## 1. The principle

The music never sets an organism's brightness directly. It **disturbs a medium**, and the organisms answer the
medium:
- each species answers in its own way, on its own channel and at its own threshold;
- the medium itself remembers (refractory, energy, wake).

That is the difference between an audio-reactive world and an audio-reactive collection of objects (§2: "not bass →
emission"):
- a kick does not light the meadow; it ignites a few patches of it;
- the light then travels outward at a speed, through the organisms that conduct it;
- it stops at barren rock;
- it cannot relight what it just lit.

## 2. The medium (ADR-1201)

`prop` is a 256 × 1 × 1024 excitable grid laid along the canyon (x -200..200 m, z -1400..1400 m: 1.6 × 2.7 m
cells), stepped at 30 Hz.

| Input | Field | Meaning |
|---|---|---|
| ignition | `ignite` = `kickFront` × `seeds` (one compound) | where a kick lands. `kickFront` is the low-band onset as a **planar front** launched from just behind the camera (its origin rides with the flight: routed, integrated), racing down the canyon at 38 m/s and fading over a few seconds. `seeds` (noise) breaks the front into arcs. **The music radiates out from the listener into the world** |
| conductivity | `conduct` (noise) | the medium is patchy, so fronts branch round barren ground and stall in it |
| gain | `grid/prop/injectRate` | the stage's sensitivity (0 asleep, 1.0 stirring, 3.0 at the drop), plus the bass level routed on top. Bass is large-scale energy (§8) |
| speed | `grid/prop/waveSpeed` | how fast light travels through the ecosystem, per stage: 10-16 m/s before the drop, 34 at the drop, 24-26 in the body (MIDI CC 2 live) |

**Revision after the first continuous run (v1).** v1 woke the whole canyon ahead at the drop: a wake front jumped
1.8 km, and every kick ignited all of it. The medium was lit everywhere at once, and the rock's response painted
every wall a flat pale cyan, so the canyon read as snow in daylight. That is the "washing out" the owner rejects,
and it has no travelling wave. v2 replaces the wake front with the camera-borne planar kick front:
- the drop is the biggest of those fronts, not a flood;
- the walls answer only `u` (the passing front), in a saturated deep blue, in patches.

Its four channels are read as fields:

| Channel | Field | Who answers |
|---|---|---|
| `u` excitation | `propU` | **Disturbance responders:** polyp mats, sea pens, wall crust, river plankton, all flashing as the front passes. **Sea fans fluoresce:** violet at rest, magenta while excited (`excitedColor`). The rock and the haze catch it (§4) |
| `e` energy (3 s low-pass) | `propE` | **The canopy:** crinoid photophores answer late and long. "The canopy responds slightly later." The rock and the haze catch it too |
| `w` wake (30 s low-pass) | `propW` | **Every layer's rest light** (`wakeGain`): a reach that has been active keeps glowing between events. "The music should awaken and organise that life" |

## 3. Small life hears the treble directly

`highs` is a spectrum field in which each element hears its own band in the top third of the spectrum
(`audioBand: element`, 0.62-0.98). The whip tips and the siphonophore chains read it: tiny organisms flickering with
the hats, each to its own frequency (§8, "spectral relationships"). The treble level also drives the spore spawn
rate, and the snare throws bursts of the swarm, scaled by the music's energy.

## 4. Light on the world: realism at rest, spectacle by consent

- **At rest the organisms light nothing around them**, which is true to life (`01-research.md`). There are no point
  lights except a dim moon.
- **At the drop the collective light reaches the world.** The rock's material program emits
  `WALL_GLOW × u × patch × mottle × emissionIntensity`, so walls and floor light up in pools where the front is
  passing, in saturated deep blue. The haze self-emits with `propGlow` (energy, a lagging glow) as its colour field.
  Both are 0 until the arc raises them:
  - `material/rock/emissionIntensity`: 0 → 1.4 at the drop → 0.6-0.8 in the body;
  - `scene/volumeEmission`: 0 → 0.05 → 0.02-0.03.

  This is the "regime change" the drop needs: from darkness that only glows, to an ecosystem that floods its canyon.
  It is a field the existing material and volume systems already read, so it costs no lights.

## 5. Autonomous life (§10)

Every layer has a life of its own, a pure function of time and identity, so it is seekable:
- slow breathing: crinoids at 0.04 Hz, chains at 0.09 Hz, the crust at 0.03 Hz;
- twinkle on whip tips and plankton;
- spontaneous flashes at a per-point phase (plankton 4 per minute, the crust 2, mats 1.5);
- sparsity, so most points rest dark;
- spores drifting up along the whole flight, and embers breathing in the wall's gullies.

Before the first kick the canyon is already alive, and the music awakens and organises that life rather than
switching it on.

## 6. The arc (scene states, ADR-1164)

Two slow listeners:
- `macro.bassSlow` follows `audio.bass` (attack 150 ms, decay 500 ms). It is the bass band's presence;
  `audio.bassLevel` is loudness-adaptive and hides a breakdown, staying around 0.8 through Trench's bass-less build;
- `macro.energy` follows `audio.energy` (1.5 s / 3 s).

| State | Entered when | Camera | Medium | Light |
|---|---|---|---|---|
| Dark | start | drift: 4 m up, long lens (36°), 3 m/s | asleep | rest only |
| Stirring | 6 s into the Dark | river: 2.6 m over the water, 5 m/s | ignition 1.1, wake 90 m ahead | |
| Awake / Awake 2 | 2 phrases after Stirring; alternating on phrases | canopy (27 m, through the crowns) / wall (14 m up, 24 m across) | 1.4-1.5, 260-300 m ahead | rock 0.3-0.4 |
| Breath | bass presence < 0.2 | river, 1.5 m/s: the camera hangs | no ignition | |
| Build | 2 s of Breath (a lull that lasts is a breakdown) | climb to 34 m, 4 m/s | 0.6 | spores 3,500/s |
| **Drop** | bass presence > 0.42 out of a Build (a 1.2 s ease-out: most of it lands at once) | **dive** to 5 m over the water at 24 m/s, 66°, banking | **3.2, the whole canyon ahead (1.8 km)** | **rock 5, haze 0.10, swarm 9,000/s** |
| Body 1-4 | 14 s after the Drop, then on phrases | canopy / dive / wall / reveal (70 m); 14 / 17 / 12 / 9.5 m/s | 2.2-2.6 | rock 1.6-2.2, haze 0.04-0.06 |
| Aftermath | energy < 0.62 from the body | after: 9 m, 2.5 m/s, pulling up | no ignition | the wake drains over its 30 s |

On Trench, through the engine (`--sonic-trace` on a probe copy, no GPU):

| Time (s) | Transition |
|---|---|
| 0 | Dark |
| 12 | Stirring |
| 36 | Awake |
| 46.7 | Breath (the 45 s break) |
| 53 | Awake |
| 69 | Awake 2 |
| 84 | Awake |
| 87.7 | Breath |
| 93.7 | Build |
| **94.8** | **Drop fires** (committed 96.0) |
| 116 | Body |
| 116-199 | Body cycles on phrases |
| 204.9 | Aftermath |

Nothing in the arc names Trench: any track with a breakdown builds and drops, and live input runs the same machine.

## 7. The camera (§11)

The camera rides one spline along the river (2.69 km). The camera system itself is a vocabulary of behaviours, each
an offset from the river, a look-ahead, a lens and a bank (`CAM` in build.py). The states choose among them and
blend between them over their transitions:
- **river** flight;
- **canopy** flight;
- **wall** pass;
- **climb** (anticipation);
- **dive** (the drop);
- **reveal** (the canyon's length);
- **drift** (long lens);
- **after**.

Pace:
- **The music sets the pace:** the path position integrates the stage's pace plus the energy.
- **Quiet passages** drift at 3-5 m/s on long lenses.
- **The build** slows and climbs.
- **The drop** dives at 24 m/s.
- **The body** flies at 9.5-17 m/s, plus the energy. The paces are sized so that the flight fits its 2.69 km
  path over the whole track (it ends at 95%); a test plays the song and checks it.

Moves land on phrases (with `quantize: beat`), never on kicks. Two incommensurate LFOs float the camera off its path,
more with energy.

## 8. Live (§12)

`rift-live.json` is the same world and arc on live input (`sonic.live`) at LIVE AUTO. A performer has these
controls:

| Control | Parameter | Range |
|---|---|---|
| CC 1 | `macros/sensitivity` | trims the listeners to the room, as a gain does |
| CC 2 | `grid/prop/waveSpeed` | 4-40 m/s |
| CC 3 | `grid/prop/threshold` | 0.15-1.2: how easily the medium fires |
| CC 4 | `material/rock/emissionIntensity` | 0-6: how much the world catches the light |
| pads 36-42 | states | Dark, Stirring, Awake, Build, Drop, Body, Aftermath: force a stage, and the arc carries on from it |

Every ecosystem layer's behaviour is also a parameter (`ecosystem/<layer>/...`), so any of them can be mapped.
