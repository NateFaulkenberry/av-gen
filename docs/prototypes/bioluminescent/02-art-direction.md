# THE RIFT: art direction

The first Environment. A new identity, owing nothing to Glowmere: no fungi forest, no fireflies over a valley, no
Glowmere species, palette or art direction.

## The world

An abyssal trench that has drained. Deep-sea fauna stand as a land ecosystem in a canyon about 150 m deep and
40-200 m across, meandering for kilometres. A shallow dark river runs along its floor, the last of the sea. Giant
sea lilies (crinoids) rise from the banks and arch their feathered crowns over the water. Siphonophore chains hang
from the crowns. Sea-pen meadows cover the floor, polyp mats encrust the walls, sea fans spread across the ledges,
and comb jellies drift in the air above the river.

Why this world:
- **The track is Trench.**
- **The deep sea is where bioluminescence is richest.** About three quarters of deep-sea animals produce light, so the
  vocabulary is real, varied and specific.
- **The marine palette (470-490 nm) keeps it out of "green neon forest" by physics, not by taste.**
- **The canyon is a composition machine.** Walls frame every shot, the reaches give depth in both directions, the river
  is a leading line, and the canopy closes overhead. The camera lives inside, so there is no world edge and no
  dead space (the owner's standing objections).

## Species (generated geometry, `examples/bioluminescent/organisms.py`)

| Species | Size | Body | Emission | Behaviour class | Hears |
|---|---|---|---|---|---|
| Crinoid (sea lily) | 18-45 m | ribbed stalk with cirri whorls, a calyx, ten arms forking once, feathered with pinnules | photophore rows along the arms, cyan to near-white at peak | slow breather; carries the canopy wave | low-mid, with a lag |
| Siphonophore chains | 4-12 m drops from crinoid arms | beads and small bells | pale cyan beads, pulses travelling down the chain | disturbance | highs |
| Sea pen | 0.6-2.5 m | rachis with paired polyp leaves | blue-cyan, waves running up the rachis | disturbance, refractory | bass (the front) |
| Polyp mat | 0.7-2 m cushions | dark lumpy cushion | 90 polyps per mat, dinoflagellate blue | disturbance (the carpet the wave runs across) | bass |
| Sea fan (gorgonian) | 1-4 m on walls | cupped planar lattice | polyps dim violet at rest; **fluoresce magenta under the passing blue wave** | slow, changes colour | low-mid |
| Whip tufts | 1-4 m | 7-15 filaments | luminous tips that flicker | disturbance, fast | highs |
| Lanterns | 0.3-1 m | translucent pods on curved stalks | warm amber cores; rare, the brightest per unit | slow breather | energy |
| Comb jellies | 0.2-0.5 m, airborne | clear body, 8 comb rows | diffraction: a moving spectrum along the rows; faint | drift | treble |
| Embers | cm, in wall crevices | — | deep red, barely visible | — | — |

## The arc on Trench (207.6 s)

Measured from the audio (0.5 s windows):
- 1 s: the bass enters at once;
- 45-47 s: a two-second break;
- 47-86 s: the full first body;
- 86-94 s: a bass-less build (mids and highs rising);
- **95 s: the drop**;
- 95-198 s: the long body;
- 198-206 s: the outro.

| Time | Ecosystem | Camera |
|---|---|---|
| 0-12 | Darkness. Faint plankton in the river, a few crinoid photophores breathing, a lantern. The ambient is barely there. True to life | long lens, slow drift low over the river |
| 12-45 | Kicks become disturbances: fronts travel down the canyon floor through the mats and pens. Each species answers at its own threshold. Fans begin to fluoresce where fronts pass | forward motion grows; low pass under the first crowns |
| 45-47 | The break: everything holds its breath, refractory | the camera hangs |
| 47-86 | A second region wakes (the next reach of the canyon). Walls begin to carry light, chains pulse | lateral tracking along a wall of fans; a rising move |
| 86-94 | The build: the bass leaves, so the fronts stop. Tiny life (spores, plankton, whip tips) awakens with the highs. Spores rise into the canopy. The world gathers | the camera climbs into the crowns; anticipation |
| **95** | **THE DROP.** Multiple seeds fire at once along the canyon. One wave front runs kilometres down the trench. Thousands of organisms ignite. The fans flash magenta, the chains cascade, the river catches everything. **Collective light floods the canyon for the first time:** the walls, the haze and the water light up | a dive from the crowns down to the river, flying along the front |
| 95-198 | The awakened ecosystem: phrases wake new reaches; swarms of comb jellies stream along the river; the canopy answers late | fast flights through dense space, orbits around a crown during a wave, macro pens |
| 198-207 | The aftermath: the light drains out slowly in the order it came. Embers and a few lanterns remain. Spores still falling | slow drift, pulling back |

## Rules (from the research, `01-research.md`)

1. Darkness is the default. At rest at least 70% of the frame is within about two stops of black.
2. At rest and in the build, emitters do not light the world. At the drop they do (the regime change).
3. No flat-filled emission. Every glow has organs, rows or polyps.
4. Light follows features: banks, ledges, gullies, arms.
5. Near-white only for flashes and the drop's cores.
6. Magenta and violet come only from fluorescence and iridescence.
7. Three planes in every frame: framing foreground, event midground, hazed background.
8. Movement is cinematic and energy-led, not beat-synced.
