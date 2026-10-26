# Talking Heads: the background plan (stop 5)

This answers the owner's background brief, `10-brief-background.md`. The two investigations it asks for, references
and an engine audit, were done by this agent alone, because the agent cap was full.

## What the engine already had (the audit)

| Need | Existing system used | Notes |
|---|---|---|
| Shader-displaced surfaces | Procedural mesh plus deformers: Sine (travelling waves, routable amount and phase, integrable phase), Noise (slow evolving drift), world-space Sine (a tear) | No new pass. |
| Per-pixel colour | Material programs: `gradient`, `oklabRamp`, `quantize`, `smoothstep` | Measured below: costly per op over a large sheet. |
| Audio | `audio.bass`, `audio.mid`, `audio.treble`, `response.lowEnv` (bass hits), `response.onsetEnv` (transients), `response.intensity` (sustained) | Through ordinary routes. No new analysis. |
| Halos and fragments | GPU particles, soft and depth-faded | |
| Glow | Bloom, unlit emissive materials | |
| Volumetrics | The marched air | Rejected at stop 4: 13-16 ms. |

## The art direction chosen

A dark "signal dimension" with two huge irregular membranes hanging at angles behind the heads. Each is a skin of
colour crossed by fine luminous contour lines that ride its waves, so the lines draw the surface's shape as it
moves.

Around them:
- a few one-off partial rings and a bent open frame at depth;
- two broad ribbons in the far distance;
- five slack filaments hanging behind the busts.

Behind a speaking head, a halo of its own colour swells. Nothing repeats and nothing is architectural. The palette is
indigo, violet, magenta and electric blue, with pink lines on the near sheet.

### The audio mapping (brief §7)

The background answers the whole mix. The heads answer their own sources.

| Signal | Response |
|---|---|
| Low energy (`audio.bass`, slow attack) | Big waves on both sheets, travelling while it lasts |
| Bass hits (`response.lowEnv`) | A ripple runs across each sheet, and its contour lines flare |
| Mids (`audio.mid`, about 1-3 s) | The colour fields slide along their ramps; the far ribbons brighten |
| Highs (`audio.treble`) | The filaments brighten and quiver |
| Strong transients (`response.onsetEnv` above 0.7) | The membranes tear sideways for about 140 ms; the rings flare |
| Sustained energy (`response.intensity`, about 3-6 s) | The fields glow brighter |
| Independent of the audio | Slow waves keep travelling, the noise drift evolves, the colour wanders on a minute-long noise, the sky tones drift |

## Implementation, highest impact first

1. **The membranes**, with their contour lines.
2. **The colour fields and halos.**
3. **The structures and filaments.**

The busts were also enlarged and staggered: the middle head forward, the others back to the sides.

## What it cost, and how it was brought down (1080p, M2 Max, GPU p50)

- **The first membranes cost 17 ms.** They used a per-pixel program of two fbm noises plus contour arithmetic, over
  a sheet that filled the frame.
- **Each program op costs about 0.2 ms over a frame-filling sheet.** Even a plain unlit procedural sheet costs about
  4.5 ms: the shared fragment path runs its effect lanes, waves and fog before the unlit branch.
- **Three changes brought it down.**
  - **Contour lines as geometry.** They are thin ribbons on the same surface, with the same deformers and routes,
    and a 4-op program.
  - **An 8-op sheet program.** Its colour field is a drifting world-space gradient, with no per-pixel noise.
  - **Less coverage.** The sheets are scaled to leave dark sky round them, which also shows their irregular shapes.
- **Result.** About 6 ms for the whole background. The numbers are in `12-stop5.md`.
- **For later.** An engine-level fast path for unlit procedural surfaces would give back most of the per-pixel base
  cost. It is not done here.
