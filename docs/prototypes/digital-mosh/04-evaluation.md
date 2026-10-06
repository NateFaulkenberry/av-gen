# DIGITAL MOSH: Evaluation

An honest evaluation against every criterion in brief §18. It is based on the final renders (1080p, both tracks), the
live sessions and the Creative Critic. Review media: `~/Desktop/av-gen-review/38-digital-mosh/`.

Grades: **strong** (would show it), **adequate** (works, not yet memorable), **weak** (needs another pass).

## The seven stages

| Stage | Where (Feline Footwear) | What it is | Grade |
|---|---|---|---|
| Dream | 0-50 s | A Dalí/Tanguy desert in the Bee's and Persistence's colours: the braided olive, the hovering Tanguy object, long low shadows, the camera floating across the pan on each phrase | adequate: calm and coherent, and still a little sparse; the land carries no painterly texture at distance |
| Uncanny | 50-81 s | de Chirico's green sky and orange sun; the shadows swing against the sky's sun; the object's double in the distance swells on the music's lifts | adequate: the relation errors are real, but subtle enough that a casual viewer may read only "the light changed" |
| Infection | 81-110 s | The first bad block: a quantised ink stain with a glowing magenta front spreading from the object; the land drains to Tanguy's grey-blue; the object's own blocks show where it is eaten | strong: the most original image in the piece, and it reads as a substance, not a filter |
| Corruption | 110-130 s | Ernst's rust: the olive eaten from the root, limbs melting like wax, the first blocks lifting | strong in stills; in motion the melt is slow enough to miss |
| Nightmare | 130-154 s | The Elephants' blood sky over Tanguy's night land; limbs floating free; abrupt moves on two bars | adequate: the colour turn lands; the tile field on the ground still reads partly as "voxel effect" |
| Collapse | 154-185 s | Geometry, then fragments and temporal fragments (mosh), pixels, light: four strata on the bar grid | adequate to weak: the order is legible, and the Pixels stratum is the weakest image in the piece |
| Recovery | 185-210 s | The keyframe: the dream exactly as it was, except one macroblock that did not refresh | strong as an idea (P8); the stuck block is small enough that some viewers will not find it |

## §18 criteria

| Criterion | Verdict | Evidence |
|---|---|---|
| Composition | adequate | Vantages were placed on the land per stage (a tiny tree in a vast land; low along the riverbed; the shadow line leading in). The Critic on pass 1 flagged repeated compositions; pass 2 varied them. The double and the mesas give depth, but the middle distance is still empty more often than the brief's "negative space" intends |
| Colour | strong | Every stage's sky, land, sun and haze are sampled from a named painting (`05-palettes.md`). The corruption colour is Ernst's oxblood pushed to full chroma, and its chartreuse is Dalí's Temptation green. Colour contagion is literal: one field carries the strain to the land, the tree, the haze and the object's specular |
| Psychological effect | adequate | The arc is coherent → uncanny → infected → systemic → collapse → untrustworthy calm, and it is driven by the music's own lift against its baseline. The Uncanny is too subtle, and the Collapse is more spectacle than dread |
| Visual originality | adequate | The ink stain on a macroblock grid, rot as a world field, the hovering object eaten into its own blocks, and the stuck macroblock are not stock glitch. The late stages lean on post effects (mosh, pixelate, sort) more than the early ones lean on the world |
| Audio responsiveness | strong | Four layers (micro, rhythmic, musical, macro). The Critic measured visual energy against the music at Spearman 0.72 on pass 1. Both tracks collapse at their own climax and recover in their own outro, and Trench's breakdown produces a Respite and a re-infection. Nothing names either song |
| Temporal coherence | adequate | Pass 2 removed the cell shimmer (footprint fade) and the stuck-pixel sparkle, and moved camera moves onto the beat. The mosh is still busy in Decay |
| Deformation quality | adequate | Wax melt, bark flow and the eaten surface are exact SDF deformations with exact shadows (ADR-1160). Terrain cannot deform, so the liquid land is a skin under the pan; it reads as liquid only in its centre |
| Material quality | adequate | Matte painted land, glossy ink cells, bark from Persistence's earths. Up close the land is plain: no painterly brushwork or grain |
| Lighting | strong | A low sun (about 11°) with long shadows, sky-coloured haze, and the fracture light carrying the strain into the haze. The Light stratum burns out to Tanguy's white without clipping (pass 2) |
| Particle quality | adequate | Motes are restrained early, spores later, and the tree's blocks lift and tumble. Nothing particle-heavy happens before the Corruption |
| Glitch quality | adequate | Image-space corruption is gated to transients from the Corruption on, never constant. The Collapse strata are legible but generic in places (Pixels) |
| Performance | weak (live), adequate (offline) | See below. LIVE AUTO cannot hold 60 fps on an M2 Max at 1080p; raymarched SDF shadows are the dominant cost |
| Live-mode stability | strong (stability), weak (rate) | See below: no oscillation, no audio loss, the arc runs on live input; about 30-36 fps, not 60 |
| Offline rendering quality | strong | Deterministic and complete: both songs render end to end at 1080p with no edge of the world and no unrendered black |

## Performance

### Offline, per stage, 1920x1080, M2 Max (`--live-profile --mode headless`, 10 s measured at 120 s)

| Stage | LIVE AUTO: level, GPU p50 / p95 ms | Ultra: GPU p50 / p95 ms | Largest passes (Ultra) |
|---|---|---|---|
| Dream | Emergency 0.5x: 35.3 / 38.5 | 83.0 / 87.6 | shadows 42.5, SDF 19.5, opaque 11.7, volumetrics 7.9 |
| Uncanny | Emergency: 29.6 / 31.9 | 65.4 / 75.4 | shadows 31.8, SDF 12.6, opaque 11.9 |
| Infection | Emergency: 22.2 / 26.0 | 51.6 / 73.8 | shadows 22.1, opaque 15.8, volumetrics 8.1 |
| Corruption | Emergency: 35.3 / 37.6 | 81.7 / 87.2 | shadows 38.7, SDF 23.6, opaque 10.2 |
| Nightmare | Emergency: 33.7 / 48.2 | 82.4 / 87.4 | shadows 31.4, SDF 23.1, opaque 18.9 |
| Collapse | Emergency: 29.8 / 32.6 | 60.3 / 64.0 | shadows 20.7, opaque 19.8, SDF 10.6 |
| Decay | Emergency: 26.2 / 27.6 | 55.4 / 58.9 | opaque 23.3, shadows 15.4 |
| Pixels | Emergency: 22.5 / 24.1 | 49.1 / 50.8 | opaque 25.8, volumetrics 7.9, shadows 7.6 |

CPU work is 3-10 ms per frame in every stage, so the scene is GPU-bound throughout.

**Why so expensive.** The shadow maps hold six raymarched SDF objects (the trunk, three limbs, the Tanguy object and its
double), each marched per shadow texel in each of three cascades. An A/B on the Dream with the SDF objects' `castShadows`
off: 35.3 → 24.6 ms (auto) and 83.0 → 56.3 ms (Ultra). Terrain shadows cost nothing measurable (`shadowDistance` 1 m:
no change). A texel-sized hit floor on the shadow march (an absolute tolerance under the relative one) saved under
1 ms and was reverted: the cost is the texels the bounds cover times the node count, not convergence. The shadow
atlas does not scale with LIVE AUTO's render scale, so the ladder's last rung still pays it in full.

**Offline render cost.** The full song renders at about 10.5 fps at 1280x720 and FINAL_FPS_1080 at 1920x1080 (FINAL_TIME per
song) on the offline tier.

### Live

**Profiled live** (`--live-profile --mode live`: the real editor loop with present and Fifo, 1920x1080 output, 15 s
measured, LIVE AUTO):

| Stage | Target 60 fps | Target 30 fps |
|---|---|---|
| Dream | Emergency 0.5x, frame p50 46.4 / p95 53.8 ms: over budget | the same (46.9 / 53.8 ms): over budget |
| Nightmare | Emergency, 27.6 / 33.5 ms: over budget | Emergency, 33.3 / 34.9 ms, 1 deadline miss in 15 s: **target achieved** |
| Collapse | Emergency, 32.6 / 37.2 ms: over budget | Emergency, 33.4 / 37.2 ms, 2 misses: **target achieved** |

**A real live session.** Trench (0-150 s) was played into BlackHole, and the editor ran `digital-mosh-live.json` on that
input at LIVE AUTO, target 60. Result:
- the controller stepped Ultra → High → Low → Emergency within 9 s of start, then held there for the rest of the session,
  with no reverts and no oscillation;
- 5,769 frames, with frame intervals p50 28.1 ms, p95 39.1 ms, p99 60.3 ms;
- no audio dropped;
- the arc advanced on the live input exactly as it does on the file. The first session reached the Nightmare by 150 s, and the
  clip `live/live-trench-capture.mp4` shows Dream → Uncanny → Infection → Corruption → Nightmare.

So the scene is **stable** live: it does not stutter or oscillate, and the music drives it. But it is **not real-time at 60**
on an M2 Max: it runs at about 30-36 fps. The Dream, the stage with the most SDF shadow on screen, is the slowest
(about 22 fps).

Measurement notes:
- `--live-capture` re-renders every second frame, so a captured session's frame rate is not evidence. The first
  session was captured, which is where the clip comes from. The second ran uncaptured for the numbers above.
- `play_live.py` first recorded the music's start on Python's `time.monotonic_ns`, whose origin is per process on
  macOS, so the join to the capture was wrong. It now records `CLOCK_UPTIME_RAW`, AV Gen's clock. The clip was joined
  by the first frame on which the live analysis heard energy.

## What I would do next

1. **Shadows.** Let a raymarched SDF cast into the near cascades only, or render its shadow at a lower resolution
   (an engine lever, `sdf.shadowCascades`). Measured headroom: about 10 ms at Emergency and 27 ms at Ultra.
2. **Pixels stratum.** Replace post pixelation with world-space quantisation (the material `quantize` op at growing
   cells over everything), so the world is made of pixels rather than filtered into them.
3. **The Uncanny.** One stronger relation error: a second sun, or the shadow of an object that is not there.
4. **Land texture.** A painterly grain on the near land (Dalí's glazed surfaces), from a material noise in the program.
5. **The liquid.** The skin reads as liquid only at its centre; a refracting, reflective surface would sell it.
