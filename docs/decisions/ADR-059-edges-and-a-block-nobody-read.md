# ADR-059: Edges, and a block nobody read

Status: Accepted

## Context

Two problems, found one after the other while chasing the same complaint: that the Glowmere shot
moves badly.

**The motion is aliasing, not popping.** The wind field was the obvious suspect and it is
innocent: every species in the scene resonates between 0.06 and 0.63 Hz, and the field's own terms
have periods of 9.5 to 33 seconds. So the search moved to the culler, which makes hard binary cuts
at screen radius, view distance, every LOD threshold and a density hash — a good hypothesis, and
wrong. Removing screen-radius culling did not reduce the churn.

What settled it was rendering the same two consecutive frames at twice the linear resolution and
box-filtering them back down. Culling and LOD do not care how many samples you take; aliasing
does. With the wind switched off, so the only motion is about five centimetres of camera:

| | pixels jumping > 24/255 between consecutive frames |
|---|---|
| native 1280×800 | 5.775% |
| 2× rendered, downsampled | 3.711% |

Better than a third of the frame-to-frame churn is aliasing. This renderer has no MSAA — Apple's
TBDR pays for it in tile memory, so `multisample.count` is 1 everywhere — and no TAA, and its
worlds are full of alpha-tested foliage, which is the worst case for both: a leaf edge is a
one-pixel feature that flips fully on any sub-pixel movement.

**And the scene's own post block was dead data.** Discovered while trying to switch the new filter
on from the scene file. Nothing in the engine read a composition's `post` object. Setting the tone
map to hard clamp and tripling the bloom in `glowmere-stylized.scene.json` changed *zero of
1,024,000 pixels*. Ten scene files in this repository carry such a block. The two settings it cost
most are the two that decide whether a scene's own light reads: `chromaRetention` (ADR-052) and
`bloomEmissionWeight`, both of which default to zero and were being asked for at 0.6 and 0.75.

## Decision

**FXAA, in the HDR chain, on a tone-mapped luminance proxy.** Lottes's filter: a local contrast
test, an edge orientation from the 3×3 second differences, a bounded search along the edge, and a
sub-pixel term from the neighbourhood's lowpass. The sub-pixel term is the half that matters here,
because a leaf thinner than a pixel produces no long edge for the search to find.

It runs inside the HDR chain rather than after the tone map, which keeps it a pass that can be
skipped without a target copy — but that means the thresholds see scene radiance, which is
unbounded. So the luminance the filter reasons about is `L / (1 + L)`: monotone, so it cannot
invert an edge, and two operations cheaper than an actual tone map.

Before sharpening, because sharpening an aliased edge fixes its contrast and keeps its stair step.

**The composition's `post` block becomes parameter base values.** Not a separate settings path:
base values mean the result behaves exactly as though an author had moved those sliders, so
presets, automation and the inspector all keep working, and a round trip writes back what was read.
The project's `parameters` block is applied at the end of the project load and therefore still
overrides the scene, which is the precedence an author expects.

## Consequences

FXAA at 0.75 costs **0.07 ms** at 1280×800 and removes 15.9% of the frame-to-frame churn, against
the 35.7% that 2× supersampling removes for four times the fill. Roughly 45% of the available win
for about a two-hundredth of the price. It is off by default; a scene asks for it.

| | churn | vs off |
|---|---|---|
| FXAA off | 5.775% | — |
| FXAA 0.50 | 5.210% | −9.8% |
| FXAA 0.75 | 4.857% | −15.9% |
| 2× supersampled | 3.711% | −35.7% |

Ten scenes change, because ten scenes finally get what they asked for. In Glowmere the visible
effect is selective bloom: `highlight_frac` *fell* from 0.0018 to 0.0007, because bloom now comes
from the emission target rather than from luminance, so the glowing flora bloom and the moonlit
ground does not.

The first version of the engine hook read `postParams_` after `params_.clear()` and before
`installController` re-registered them — dangling pointers, which presented as a nearly-silent
partial no-op rather than a crash. It was caught by exporting the loaded project and reading the
parameter values back, which is worth doing for any change of this shape.

A test now loads a composition with a post block, checks each value arrived, and checks a project
that names one of the same paths still wins. Nothing caught this for the life of the feature.
