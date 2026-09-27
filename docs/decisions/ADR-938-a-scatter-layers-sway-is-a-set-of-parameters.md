# ADR-938: A scatter layer's sway is a set of parameters, named for what the viewer sees

**Status:** Accepted
**Date:** 2026-09-27
**Follows:** ADR-055 (vegetation motion: a species' wind response as a damped oscillator), ADR-905 (a scatter
layer's emission lane as parameters, by the layer's name), ADR-387 (the panels group parameters by path;
the owner's rule that anything visible is controllable), ADR-902 (the liveness registry's phase-rate
table)
**Found by:** the GV3 revision's gv3-look stream (`phase3/look.md`: "the fan plants ... kept the source's
stiffness: by wind.cpp's oscillator a 2 m plant swung 0.9 cm in a gust"; open item 5: "The species' sway
settings are not parameters, so an artist cannot adjust them in the app")
**Implemented by:** `ScatterSwayControl`, `scatterSwayControls()` and `ScatterLayerParameters::sway`
(`src/scene/composition.hpp`); their registration, unregistration and per-frame application beside
ADR-905's lane (`src/scene/composition.cpp`); two phase-rate rows (`src/scene/route_liveness.cpp`)
**Tests:** `tests/unit/test_scatter_sway_controls.cpp` (`[adr938]`, 4 cases);
`tests/rendering/test_scatter_sway_gpu.cpp` (`[adr938]`, 1 case, 3 sections)

## Context

How a species moves in the wind is per scatter layer (`ScatterLayer::motion`, ADR-055): how much of the
wind it catches, how far its tip travels, how stiff and how heavy it is (which set its resonance), how
quickly it stops ringing, how much of a gust it takes, how far and where along its height it bends, and
how much neighbours differ. It is what separates grass leaning into every gust from a tree that only
swings slowly.

It was scene data only. On Glowmere Valley 3 the fan plants -- the big broad-leaved plants in the
foreground of four shots -- kept the source's stiffness and swung 0.9 cm in a gust. gv3-look loosened them
(tip 0.08 -> 0.20, catching 0.65 -> 0.9 of the wind, stiffness 2.4, mass 1.2, gusts 1.2) by editing the
generator; an artist in the app could see the plants barely moving and had no control that reached them.
ADR-905 had already made each layer's glow, hue and light wave parameters; its motion was left behind.

## Decision

**1. Every scatter layer registers nine sway controls** under `nodes/<terrain>/scatter/<layer>/sway/`. The
leaves are the scene file's `motion` keys, so a generator, a project parameter and the file name the same
thing; the labels say what the plant does:

| Leaf (`motion` key) | Label | Routable |
|---|---|---|
| `windSensitivity` | "catches the wind (0 = stands still)" | yes |
| `tipAmplitude` | "sway at the tip (x its height)" | yes |
| `gustResponse` | "takes the gusts (x)" | yes |
| `stiffness` | "stiffness (bends less, rings faster)" | no |
| `mass` | "weight at the tip (rings slower)" | no |
| `damping` | "settles (low keeps ringing, 1 stops at once)" | yes |
| `bendLimit` | "bends at most (x its height)" | yes |
| `bendCurve` | "bends along (1 = the whole stem, 3 = the tip)" | yes |
| `amplitudeVariance` | "difference between plants (+- of its sway)" | yes |

Every layer, including one that authors no `motion` (it gets the species defaults, under which it stands
still until "catches the wind" is raised): a plant that barely moves is exactly the one an artist needs to
reach. The defaults are the layer's authored values; the hard ranges hold every layer GV2 and GV3 author
(tip masses from grass's 0.06 to deadwood's 60).

**2. Per frame, like the emission lane.** Each frame the finals go to every part of the layer (bark and
leaves sway as one plant), preserving each part's simulation settings; the bases go back into the layer,
so the scene the composition writes keeps them. `motion` is not in the layer's structural hash (ADR-055),
so no edit replants anything. The renderer already resolves each object's motion into gains every frame,
for Tier 0 and Tier 1 alike, so nothing downstream changed.

**3. Stiffness and mass refuse routes.** Together they set the rate the plant rings at,
`sqrt(stiffness / mass)`, and the flutter's phase is that rate times the whole render time (`windBend`).
A route moving either every frame would jump the flutter by t x the change, the phase-rate hazard ADR-902
lists. So they are not modulatable (a route to one is refused at bind, by name), and the phase-rate table
gains rows for them, for a timeline track that keys one. A slider or a key still moves them.

## Consequences

- **Where an artist finds them:** the Parameters panel, group `nodes` (the Intermediate layer, where the
  editor opens), heading "<terrain>/scatter/<layer>/sway", then the labels above; the World panel's
  Inspector, where a click on a plant selects the terrain that grew it, under "scatter" as
  "<layer>/sway/<label>" -- beside ADR-905's "<layer>/glow".
- **No existing scene's look changes**: every parameter's default is the layer's authored value, so a
  frame with nothing edited is the frame it was.
- **More parameters:** nine per layer. GV3 and GV2 multicam (18 layers each) gain 162 each.
  A project saved by the app writes them, and a project's parameters override its scene (as for every
  parameter): an app-saved value wins over a regenerated scene's `motion` until it is removed from the
  project.
- **Tests** (each with its control):
  - reach in both panels, for a layer with `motion` and one without, doing the panels' own arithmetic
    (`parameterSubGroup`, `inspectorPlace`, `inspectorRowLabel`), and that a click on a fern selects the
    terrain;
  - a slider: tip 0.08 -> 0.20 multiplies every part's steady and gust gains by exactly 2.5; stiffness and
    mass move the ring rate to `sqrt(k/m)`; the other layer, untouched, is exactly as authored; a still
    layer goes active when "catches the wind" is raised;
  - a route reaches "sway at the tip" the same frame (0.08 at rest, 0.18 on a unit signal, 0.08 again); a
    route to the stiffness is refused at bind with "not modulatable" and moves nothing;
  - the save: the bases reach the scene the composition writes, untouched members stay authored, and none
    of the nine moves a layer's structural hash;
  - on the GPU, through the composition as the engine renders it (517 ferns of the library's `Fern_1` on
    a flat meadow in a breeze, 480x300, no skybox): "sway at the tip" 0.08 -> 0.40 moves **101,683 of
    144,000 pixels**; the same edit with the wind off moves **none** (the control that the difference is
    the sway, not a replant or a re-light); setting it back to 0.08 gives the authored frame back byte for
    byte; and with "catches the wind" at 0 two seconds half a second apart must be identical, where the
    authored ferns move between them. The first run of that last arm compared
    frames 180 and 210 and found the renderer's per-frame jitter, not the ferns; the two seconds now share
    one frame index, so only animated time differs.
  - Evidence: `~/Desktop/av-gen-review/18-glowmere-valley-3/revision/uireach-work/sway/`.
