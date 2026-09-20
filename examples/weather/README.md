# The natural and weather effects

Eight effects, one shared stage, and one engine change under all of them (ADR-520).

| effect | files | what it is for |
|---|---|---|
| Rain | `rain.*` | droplet size, streak length, splash, puddle ripples, mist |
| Snow | `snow.*` | flake size, fall speed, turbulence, rotation, depth layers, near-camera flakes |
| Ashfall | `ashfall.*` | dark ash plus glowing embers, smoke, temperature, atmospheric illumination |
| Weather front | `weather-front.*` | a front that physically moves, so different regions have different weather |
| Dust motes | `dust-motes.*` | motes that become dramatically visible when lit |
| Fireflies | `fireflies.*` | clustering, wandering, pauses, synchronised flashes |
| Pollen / spores / seeds | `pollen.*` | one generic biological airborne system, wind and buoyancy |
| Season shift | `season.*` | one continuous parameter, not four presets |

`_stage.scene.json` is the backdrop the first seven include as a `scene` node, so they are judged
against the same geometry, the same distances and the same silhouettes. The season scene is
self-contained, because its macro names its own nodes' parameters and a nested composition's nodes
are registered under prefixed names.

## Rendering one

**Render a short range and take the LAST frame.**

```sh
tools/gpu-lock.sh ./build/release/src/avgen --headless \
  --project examples/weather/rain.json --size 1280x720 \
  --range 2.91:3 --particle-warmup 240 --render out --format png
# out/frame_000005.png
```

Two flags matter and both of them were defects before this work:

* `--particle-warmup 240`. Without it a render opens with empty pools. It did nothing at all on a
  `--render` until ADR-521: the flag was applied to the interactive renderer and `RenderJob` builds
  its own.
* The **short range**. `FixedStepClock::tick()` returns `deltaTime = 0` on the first tick, so on
  the first frame of any render the shutter is 0 and neither ADR-040's velocity stretch nor
  ADR-037's motion blur exists. Rain at `--range 3:3` is a field of round dots; the same rain at
  `--range 2.91:3`, frame 5, is a field of streaks. This is recorded in ADR-521 and deliberately
  not fixed there, because the honest fix moves the first-frame hash of every render in the tree.

Render through the **project**, not the scene. A `--composition` run gets default post values and
no render block, so exposure, grade, bloom and lens are not the ones the effect was tuned against.

## Turning the season knob

`season.json` declares a `macro` source with one knob and a `worldMacros` entry with 32 targets.
In the application it is `macros/season`; from a render, set it as a parameter:

```sh
--param macros/season=0.0   # high summer
--param macros/season=0.55  # the leaves are down and the snow has not started
--param macros/season=1.0   # deep winter
```

A macro target is a linear remap with a curve, so a quantity with an interior maximum — leaf fall
has one — cannot be a single target. Here it is two systems whose spawn rates cross, which is also
what happens: the leaves run out as the snow starts.

## What each one costs

Measured with the engine's own A/B, which interleaves baseline and arm blocks in one process and
reports a noise floor (ADR-170's rule -- this is a paired difference, not two separate runs):

```sh
tools/gpu-lock.sh ./build/release/src/avgen --project examples/weather/rain.json \
  --ab particles --size 1920x1080 --frames 160 --particle-warmup 240
```

Rain, 1920x1080, four systems, 39 140 pooled, 5 894 emitted per frame:

```
gpu frame median 7.41 ms; pass medians: scene=3.34 volume.march=1.90 post/dof=0.72
                                        ao=0.33 particles=0.26 depth=0.13 ...
A/B gpu : baseline 7.41 ms, arm 6.68 ms, delta +0.79 ms (+10.62%); noise floor 2.00%
          -> A RESULT: the arm is faster
A/B gpu drift: 7.34 ms first half, 7.41 ms second (+0.89%) -- the machine held still
```

So the heaviest scene here costs **0.79 ms of GPU frame time**, of which the timed `particles`
pass is 0.26 ms and the rest is the draw. That is the top of the 0.26-0.79 ms band the particle
system already occupied, for the densest weather in the directory -- nothing here adds a pass, a
binding or a second ray march, and the volumetric march (1.90 ms) is untouched.

| scene | systems | pooled particles |
|---|---|---|
| rain | 4 | 39 k |
| weather-front | 2 | 34 k |
| snow | 3 | 25 k |
| ashfall | 4 | 25 k |
| season | 3 | 20 k |
| pollen | 3 | 6 k |
| dust-motes | 2 | 4.6 k |
| fireflies | 2 | 1.6 k |

Fireflies are the point of that table: 1 600 particles, and it is the most alive of the eight.
