# Dimensional portals, Gate A: the Matrix effect, and the portal in the rock

The governing documents are the brief (`12`), the coordinator notes (`13`), and the plan with the owner's decisions
after Gate R (`15` §6). Media is in `~/Desktop/av-gen-review/101-strange-attractor/portals-a/`; the measurements are in
`05-measures.txt` there.

New engine work: ADR-1872 (mesh wire v2), ADR-1873 (corruption) and ADR-1881 (the flight's approach). The Moon and the
crossing are Gate B.

## The site (`00-sites/`)

**How the candidates were found.** I dumped the 4096² height field, the gorge centreline and the flight, then searched
in two ways:
- straight rays along the gorge at camera height, for the first rock they hit;
- a grid of points ahead of the 19 s camera, for steep faces turned toward it, with open air in front.

**The candidates:**

| Site | What it is | Verdict |
|---|---|---|
| A | A tall wall (about 150 m) on an outside bend of the river | About 1.2 km from the 19 s camera. At 80 m/s, slowing to 22 m/s puts the crossing near 38 s. |
| B | A square wall closing a reach | About 2 km away: too far |
| C | A side wall the river runs along | The camera would turn about 70° into it |
| **D (chosen)** | **The river's own east bank wall, about 60 m tall, under the lit strata escarpment.** The river runs along its foot. | 600 m from the 19 s camera, facing back up the river at 28°. Reached at 22 m/s at **32.0 s**. That is the downbeat of bar 17, the first beat of the track's second section (beat clock: 121.0 BPM, bars every 1.983 s). |

D is in shadow at that hour, so the seam's light and the code read against dark rock, with the sunlit strata cliff
behind it. `map-flight-and-sites.png` shows the new flight and the four sites.

## The approach (ADR-1881)

- The flight follows the gorge until arc 1572 m, then flies straight at the wall, slowing from 21.25 s over 6.5 s
  from 80 to 22 m/s.
- Peak deceleration is about 13 m/s², eased (smoothstep).
- The climb and the reveal are not flown. Past the wall the line continues; Gate B crosses there.
- **0–17.9 s are identical to the old flight to 1e-6 m** (camera trace).
- Around the site the mesh has no edge over 1.5 m within 140 m. The corruption needs vertices where a flat wall would
  have two triangles.

| t (s) | distance to the wall (m) | field | opening |
|---|---|---|---|
| 16 | 833 | 0 | 0 |
| 20 | 519 | 0.18 | 0 |
| 22 | 362 | 0.40 | 0 |
| 24 | 230 | 0.70 | 0.24 (seam) |
| 26 | 141 | 0.88 | 0.69 |
| 28 | 89 | 0.96 | 0.91 |
| 30 | 45 | 1.0 | 1.0 |
| 32 | 1 | 1.0 | 1.0 |

## What was built

### The scan (ADR-1872)

- The event front (`response.onsetLow`) and periodic pulses now travel **out of the portal**.
  - The pulses are gated by the corruption field, so there are none far away. They thicken as the camera closes in,
    with no route.
  - A front is born as a ring, not a disk. It dies away before reaching the camera's own distance; a front sweeping
    over the eye at 550 m/s lit the whole near wall for two frames.
- Each line carries heat: hot gold ahead of a crest, cooling copper behind it.
- Lines are brighter and a little denser near the focus.
- No lines are drawn inside the opening. Its rim is drawn on the rock's own edges.

### The geometric mosh (ADR-1873)

It works in the entity vertex stage, so it is lit, shadowed and in perspective. Every effect is a pure function of
the undisplaced position, the clock and the camera's distance:
- **Blocks** (5 m) slip along the wall, lift out of it, and slide the strata inside them. Each slip is eased on its
  own staggered epoch. The triangles to the neighbours stretch, and that stretch is the smear.
- **Strips** (4 m) slide past each other like faults.
- **Frozen cells** lag their neighbours.
- **The pull.** The wall is drawn toward the opening, but not inside it.
- **A soft fold.**
- **Two echo copies** of the rock near the portal. Each keeps only the slipped blocks, stale and trailing.

### The opening, driven by the approach

1. From about 23 s, a hairline seam of light grows along the wall.
2. From about 25 s, it splits. The rock just inside the edge folds back into a 14 m tunnel, and the lips fold toward
   the camera.
3. The edge wanders and steps on a 2.5 m grid: torn, not drawn.
4. Inside it is a black void. Gate B composites the Moon there; the pass's hook is `crCut`.

The opening is monotonic in the camera's distance, so a seek lands on the same state.

### Shards (ADR-1872)

- A share of the lip triangles break off.
- They are read in place and de-indexed.
- They fly in toward the focus and back into the tunnel, turning and shrinking.
- They are lit, with glowing edges.

### Glyphs (revised after the owner's mid-gate note)

The owner asked for the glyphs to be more prominent and to start sooner, with proof 04's dense code around the focus
combined with the opening. Their note overrides the earlier "limited glyphs" ceiling.

- Our own procedural 5×7 glyphs (mirrored strokes, cycling, stateless rain).
- They now cover every face in the field. They grow from a sparse 12% of cells at the field's edge to 72% at the
  portal, brightening as they go, with brighter bands travelling out of the focus.
- The rock under the code darkens with the field.
- The field reaches 1100 m, so code first appears on the far wall at about 19–20 s and is dense by 24 s. The canyon
  before 16 s is untouched (field 0.025 at 16 s; glyphs need 0.04).
- They are world-anchored. The cell size doubles with distance (at least about 20 px, 1.8 m at close range) and is
  crossfaded, and the glyphs are antialiased.
- The water is not a target, so it carries no code. The code on the bank below the portal reads close enough to
  proof 04.
- The before/after strip is in `06-more-glyphs/`.

### No image-space mosh

The owner's direction. The existing temporal mosh is not used.

### Audio (`build_project.py`)

The field and the opening follow the approach, not the music. The music moves what happens inside the field:
- bass → the share of slipping blocks;
- low onsets → slip distance and a front out of the portal;
- highs → glyph brightness and contours.

## The flicker spike at 15.13 s (fixed)

The first render's opening measured irregularity 0.306 at 15.13 s, against 0.061 before.

The cause: the onset front's origin jumped. While the corruption did not act, the front grew from ahead of the camera,
as in ADR-1862. On the frame the field first rose above zero, it moved to the portal, so the front on the near walls
vanished in one frame.

The fix: with `fromFocus`, the front's origin is always the corruption's focus (`mw.origin`). The opening now measures
a maximum of 0.062 at 9.90 s, the same frame and value as the accepted render (0.061).

## Measurements

All figures are from `05-measures.txt`. The flicker metric is the coordinator's; "before" is the accepted canyon
render at main ec422142.

| Window | Irregularity p90 | p99 | max |
|---|---|---|---|
| 0–16 s, after | 0.012 | 0.044 | 0.063 (9.90 s) |
| 0–16 s, before | 0.014 | 0.046 | 0.061 (9.90 s) |
| 23–30 s, the opening (centre crop) | 0.061 | 0.128 | 0.153 |
| 20–32 s, the corrupted wall (centre crop) | 0.074 | 0.252 | 0.728 (31.60 s) |

- **The opening (0–16 s) is now as steady as before.**
- **The approach is busier by design.** The dense code, the slipping blocks and the shards change much of the
  picture every frame, which is what the owner asked for. Its worst moment is 31.6 s, 10 m from the wall inside the
  shard stream, just before the crossing. Gate B's crossing replaces that last half second.
- **PSNR of the opening against the accepted render** is 40.5 dB at 1 s, 35 dB at 8 s and 26–29 dB at 12–15.5 s. The
  differences are the onset fronts, which now come from the portal, and the mesh tolerance, which follows the new
  path.

## Honest weak spots

- **The mosh is weak on the shadowed wall face.** The block slips and strips read clearly on the sunlit slope above
  (the close-ups) and on the floor. On the portal's own face, which is in shadow, they mostly show through the wire
  and the glyphs. Gate B could light the face or push the slips harder there.
- **The tunnel's lips are a radial fan.** At close range they read a little like petals. This is the stretched band
  between the edge and the tunnel's back. A thicker, blockier tear is a Gate B refinement.
- **16–21 s is quiet.** The field is under 0.2 until 20 s, by design: sparse fronts, then building. It could start
  earlier if the owner wants more there.
- **The light leaking through the seam at 25–26 s** fills the half-open slit with a flat warm glow before the void
  takes over.
- **The shards** cast no shadow.
- **The opening frames are not identical to main.**
  - The mesh tolerance follows the new path, so far triangles differ slightly.
  - The onset fronts now come from the portal.
  - PSNR against the accepted render is in `04-opening-before-after/psnr.txt`.
- **The CPU twin** of the displacement is the tests' reference. It is not held to the GPU by a parity test.
