# Talking Heads, stop 4

Media: `~/Desktop/av-gen-review/talking-heads/stop4/`, made by `tools/talking_heads/stop4.sh <project>`. The owner's
stop-4 notes, in the order the coordinator set: the mouth (with lids), palettes and per-source effects, the neck's
swivel, then the atmosphere.

## 1. The mouth

The owner: "the mouth should completely close and the way it looks when completely open is unacceptable ... we want
the mouth to actually look like its opening and closing, not this cut out thing".

The separate jaw piece is gone. The head is one surface with a slit cut along the lips between the mouth's corners
(`make_bust.py` `cut_mouth`). The lips sit 0.6 mm apart, a fine dark line, and each lip's edge rolls 12 mm into the
head as a near-black wall.

A new engine deformer opens it: the **Hinge** (ADR-1672). It turns the lower lip and chin about a pivot behind the
mouth.
- Just below the slit, the turn tapers to nothing at the corners, so the lips part in a lens and the corners stay
  attached.
- A quarter of the mask's radius down, the whole width turns, so the cheeks stretch.
- It is rigid at the chin and eases out at the mask's edge.
- The dark cavity shows only through the parted lips.
- The rays' brightness and the hinge's angle come from the same signal through the same route chain
  (`ray_check.py`: the worst difference on any frame is 1e-4, on every track).

The first form folded the chin past the corners, where the surface is not cut, and opened as a notch. The `lip` term
replaced it before anything shipped (ADR-1672, "The first version").

Stills (`mouth/`):
- `mouth-{front,three-quarter}-tilt{00,32}-open{000,050,100}.png`: closed, half and fully open, from the front and
  three-quarter, upright and tilted back 32 degrees. The rays are off here, because a close camera sits inside their
  cones.
- `mouth-with-rays-three-quarter-tilt{00,32}-open100.png`: fully open, with the rays.
- `01-mouth-slow-open-close-rays.mp4`: the middle head's mouth on a slow 0.2 Hz sine, with its look held still, its
  rays tracking the opening.

The cheap fixes:
- **The eyes.** A round iris whose edge is one of the eyeball's own rings (it was stair-stepped), 27 degrees across.
- **The chest.** Deeper.
- **The mouth.** Wider: the cut runs 16 degrees either side of the front.

## 1b. The lids

The owner: "add eyelids to busts too".

- **The lids.** Each is a porcelain shell round the eye's centre, upper and lower, so turning it can never cut into the
  eyeball. Their ends lie on the hinge axis, so shut lids meet at the corners. The upper shell rides 0.7 mm outside
  the lower, so where they overlap it lies in front, and a fine crease reads as the lash line.
- **The blinks.** Both eyes blink together, on two smooth noises, at least 2 s apart, each head its own. Measured
  on Feline Footwear, the intervals run 2.0-8.8 s (median 3.4), and each blink lasts 133 ms: it shuts in 35 ms,
  holds 25 ms and opens in about 90 ms.
- **The lid follow.** The upper lid follows a downward glance, clamped so it never passes shut.
- **Media.** `lids-half` and `lids-shut` stills; `03-eyes-blinks-close-gate-shut.mp4`.

## 2. Palettes and per-source effects

The owner: "lets vary color palettes for multiple heads and the secondary effects on the entities should all be
unique to the sound source".

**Palettes.** Each head's rays, possession wash (four OKLab ramp stops), glow, two spectral copies and its light on
the mist share one palette:
- the drums are ICE (blue to cyan to white);
- the bass is ORCHID (violet, magenta, pink);
- the synth is EMBER (red-orange to gold to cream).

**Per-source effects.** Every secondary effect is driven by its own head's gate and nothing else.
- **What was removed.** The screen-wide post glitch, tear and RGB split routes. The burst pad now shears each head
  instead.
- **The per-entity forms, and which instrument leans on each:**
  - shake (drums 1.0, others 0.25-0.3);
  - tear: each piece's own shear and signal bands (drums 1.0);
  - colour split and ghosts: the two copies pulled apart, how far and how long being the instrument's (bass 1.0);
  - wash: the possession colour, its spread on the head's own level (synth 1.0).
- **The flash.** It now rises from what the head does (its Live), so a head held shut or silent flashes nothing. Before
  this, the shut pad left the drums head tearing.

**The isolation trace.** `tools/talking_heads/isolation_check.py` runs the sonic trace on the Feline stems, then again
with each stem in turn replaced by silence, and compares every column a head owns: its signals, and every route target
under its prefix.
- The muted head changes (59 of 111 columns).
- The other two are bit-identical on all 6317 frames, for each of the three stems (`checks/isolation.txt`).

## 3. The neck's swivel

The owner: "lets also try making their necks swivel so they can also look up down left right at the neck too".

**The neck's deformers.** It TWISTS (yaw, about the straight neck: its base stays put on the body), then BENDS:
- back, for the gate's snap and looking up;
- forward, for looking down.

**One side at a time.** The two bends are one-sided, each 0..1 and cut to exactly 0 at rest. A Bend at a curvature near
0 loses its normals to the GPU sine's precision (the stop-1 dark neck), and a bipolar pitch would have passed through
it.

**The pivot.** The head hangs from a pivot at the neck's top. The pivot takes the pitch and its place on the bent neck,
and the head the yaw, so the head turns in the same order as the neck's top ring.

**The motion.** Mechanical: stepped targets, held, through a 220 ms lag and a critically damped 1.3 Hz pan.
- Yaw is ±30 degrees.
- Pitch is -14 (down) to +10 (up), layered on the gate's 24-degree snap back (`checks/rays-gates-looks.txt`: pitch
  -15..+35, yaw ±30).
- The eyes lead both the neck's turn and its nod (target minus current), and the neck follows a beat later.
- The whole bust's scan is narrowed to ±26 degrees so the two layers together stay readable.
- The rays, eyes, lids, cavity and copies ride the head.

**Media.**
- `04-looking-around-gate-shut.mp4`, `05-looking-around-with-music.mp4`.
- `neck/`: close-ups at the extremes, front and three-quarter: looking down 14, back 34 (the snap plus looking up),
  turned ±30, and combined.
  - The twist and the head's yaw agree in sign, and the bend is seamless both ways.
  - The dark band under the chin in the face-on views is the chin's underside. The rest pose shows it too.

## 4. The atmosphere

The owner: "add more atmosphere to the talking heads background too, mist, particles, ambient tones - it should be
dynamic too".

- **The mist.** Large, soft, slow sprites, depth-faded where they cross a bust: a few dozen drifting through and behind
  the heads, violet.
- **Each head's own mist.** A small cloud round each head, in its palette. It is dark until that head's gate opens,
  then lit on the same pose as its mouth: the per-head colour on the mist round it. Its spill light, a point light
  0.45 m in front of the face, adds the colour to the porcelain.
- **Two approaches tried first, and dropped:**
  - A placed fog bank (the Effect Library's `fog`) drew nothing at this scale: every variant rendered the same sequence
    hash. No shipped project uses one, and I have not diagnosed it.
  - The environment's own marched air looked right: violet haze, each speaking head's colour glowing in it. It cost
    13.5-16.3 ms of GPU a frame at 1080p, whatever its step count, local lights or noise, and with or without the
    rays. That is not a bounded cost for a background, so it is not used.
- **The motes.** At three depths: far (fine, many), mid (the stop-3 layer) and near (few, larger, slow, in front of the
  heads).
- **The tones.** Slow colour gradients through the sky's zenith, horizon and ground, each on its own noise of a minute
  or so. The ground moves with the horizon, so no horizon line shows.
- **The swell.** The mix's energy swells the mist and the bloom a little, over seconds.
- **Media.** `06-atmosphere-gate-shut.mp4`, `07-atmosphere-with-music.mp4`.

## Cost

Headless 1080p, realtime tier, M2 Max, 360 frames, three runs each (`checks/bench-*.json`). GPU p50 / p95 in ms:

| Run | GPU p50 | GPU p95 |
|---|---|---|
| Feline stems | 15.5-15.7 | 20.8-21.6 |
| Feline stems, no atmosphere | 15.7-16.1 | 20.3-21.8 |
| Night Shift, the mix | 13.5-13.7 | 17.7-18.4 |

- **The atmosphere.**
  - Its cost is within the run-to-run noise: the particles pass goes from 0.05 to 0.23 ms.
  - The sky tones cost nothing measurable.
- **The frame.**
  - 55 draws, 374k submitted triangles.
  - Stop 3 measured 12.3 ms GPU p50 on the same take. The roughly 3 ms added is not the atmosphere, since it shows
    with the atmosphere removed.
  - It is the new geometry and deformers: the lids, the smoother eyes, the neck's twist and second bend, and the
    head's Hinge, each of which makes its piece take the finite-difference normal path.
  - I have not profiled it further. It is the first thing to look at if the scene goes to the live ladder.

## The mix fallback (origin/main daf93949)

`signal.kickEnv` is retired. The drums head's mix fallback reads `response.lowEnv`, threshold 0.7. On the Feline mix
it opens 1.25 times a second (33% open), and on Trench 1.03, beside the other heads.

## Checks

| Check | File | Result |
|---|---|---|
| Rays against the mouth, every frame | `checks/rays-gates-looks.txt` | Feline stems and mix, Night Shift, Trench: all track |
| Isolation | `checks/isolation.txt` | Bit-identical, as described in section 2 |
| Beam origins on the anchors | `checks/anchor-check.txt`, `checks/beam-origins/` | Middle head at tilt 0, 24 and 32 with the mouth open: every beam starts at its anchor |

Engine tests:
- `[procedural]`, `[deformer]`, `[ui]`, `[params]` and `[shell2]` CPU tags pass.
- The GPU `[procedural]` tag passes (`[adr1672]` included).
