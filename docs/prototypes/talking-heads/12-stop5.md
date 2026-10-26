# Talking Heads, stop 5

Media: `~/Desktop/av-gen-review/talking-heads/stop5/`, made by `tools/talking_heads/stop5.sh <project>`. Four owner
notes:
- the "oh" mouth;
- the seam;
- larger busts;
- the background brief (`10-brief-background.md`; the plan is `11-background-plan.md`).

## The "oh" mouth

The owner: "reshape fully open mouth to more of an 'oh' sound / look rather than a half smile".

The Hinge deformer gained a **purse** (ADR-1672, the `tension` row). As the mouth opens, the purse:
- draws the lips in toward the middle;
- arches the upper lip;
- pushes both lips forward;
- rounds the lens toward an ellipse.

It acts in proportion to the angle, so closed is still a sealed line, and half open is half pursed and never a
smile. Purse 0 skips it entirely, which keeps a hinge without one byte-identical (tested on the CPU and the GPU).

The scene opens the mouth further (0.28 rad, from 0.20) with a purse of 2. Fully open, the opening is about 1.2 times
taller than it is wide.

The rays still track the opening on every frame (`checks/rays-gates-looks.txt`). Every ray starts at its anchor,
including at tilt 32 with the mouth fully open (`checks/anchor-check.txt`).

Media:
- `mouth/mouth-{front,three-quarter}-tilt{00,32}-open{000,050,100}.png`;
- `mouth/mouth-with-rays-*.png`;
- `01-oh-slow-open-close-rays.mp4`.

## The seam

The owner: "eliminate this seam that gets exposed by some of the secondary entity effects".

The causes, each fixed:
- **The glow was on the head entity alone.** It is now on all three pieces, with identical values and routes.
- **The wash colour and signal bands read each piece's own local position.** Each piece has its own origin, so the
  patterns broke at the join. They now read world position.
- **The ghosts copy the head alone.** They now fade toward their lower edge through an object-space gradient.
- **What was already safe:**
  - The face masks fade inside the face.
  - The shake moves its children with it.
  - The shear is world-space with identical values on every piece.
  - The rest shading is continuous, because the pieces' normals are copied from the whole bust.

**The check.** `tools/talking_heads/seam_check.py` renders each effect at full strength close on the neck, front and
three-quarter: rest, wash, glow, bands, tear and ghosts. It measures the largest colour step at the seam against the
rows beside it.
- The seam step is under 1 (0-255) for every effect, so none shows.
- The "spill" case rendered nothing new: the spill lights were cut (see Cost).
- Results: `checks/seam-check.txt`; close-ups in `seam/`.

## The composition

The owner: "make the busts take up more space in the composition".

The middle head stands forward and the other two back and to the sides. The camera is about 2 m from the middle head,
so:
- the middle bust is about 55% of the frame's height, the others about 45%;
- every mouth stays in frame through the swivels and the snap.

## The background

The brief's "surreal signal dimension". `11-background-plan.md` has the audit, the plan, the audio mapping and what it
cost to get here.

**The pieces:**
- two irregular warped membranes, whose contour lines ride their waves;
- broad colour fields that glide with the mids;
- halos in each speaking head's colour;
- a few partial rings, a bent frame and two far ribbons at depth;
- sparse filaments that brighten with the highs;
- a ripple and a contour flare on each bass hit, and a tear on strong transients;
- slow motion that never stops.

**Media:**
- `03-background-quiet-passage.mp4` (Feline 15-30 s);
- `04-background-energetic-passage.mp4` (75-90 s);
- `05-background-heads-hidden.mp4`, `background-heads-hidden-{quiet,energetic}.png`;
- `02-three-heads-feline-stems.mp4`.

## Cost (1080p, realtime tier, M2 Max, 360 frames, three runs each)

The stop-4 regression (about 3 ms) was profiled:
- **The ghosts: 2.2 ms.** Six full-resolution head copies were drawn. The ghost mesh is decimated to a fifth (its
  look is only a fresnel edge).
- **Three spill point lights: 1.0 ms.** They are cut, since the head's own mist and glow carry its colour.
- **The lids and the new deformers** cost nothing measurable.

| | GPU p50 | GPU p95 | Wall p50 |
|---|---|---|---|
| Before: the stop-5 heads, no background | 15.0 / 15.0 / 15.0 | 23.7 / 24.5 / 25.4 | 19.8-19.9 |
| After: with the background | 20.5 / 24.5 / 23.5 | 35.4 / 35.7 / 36.5 | 25.5-30.0 |

The background adds about 5.5-9.5 ms at p50. The spread between runs probably comes from the other agent sharing the
GPU; the lock serialises jobs, so this is not proven.
- The p50 sits inside 1080p30's 33 ms.
- **The p95 (about 36 ms) does not.**
- The per-pixel base cost of an unlit procedural sheet (about 4.5 ms over a full frame) is an engine-side fast path
  not taken here.
- The live ladder's levers would be the sheets' screen coverage and the busts' ghost and wash programs.

## Checks

| Check | File | Result |
|---|---|---|
| Rays track the mouth | `checks/rays-gates-looks.txt` | Feline stems and mix |
| Isolation | `checks/isolation.txt` | Each head's effects are its own source's |
| Seam | `checks/seam-check.txt` | No effect reveals the head/neck seam |
| Anchors | `checks/anchor-check.txt` | Every beam starts at its anchor |
