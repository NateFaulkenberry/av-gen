# Talking Heads: stop 2, the gated head (for the owner's review)

Built from the owner's stop-1 notes (`05-owner-notes-stop1.md`) on `scene/talking-heads`. **Not yet registered** in
`ABSTRACT` or the index. Media is in `~/Desktop/av-gen-review/talking-heads/stop2/`, made by
`tools/talking_heads/stop2.sh <project>`.

## What changed

**The bust is four articulated pieces** (`tools/talking_heads/make_bust.py`). Each GLB is in
`examples/sonic-abstract/meshes/`, with its frames listed in `th-bust.anchors.json`.

| Piece | Triangles | How it moves |
|---|---|---|
| `th-body` | 5,302 | Rigid. Carries the Adam's apple |
| `th-neck` | 2,808 | **Bent** by the engine's Bend deformer (local, toward the back, over 48 mm), so it stays joined to the body at its foot and to the head at its top |
| `th-head` | 30,348 | Turned and moved onto the bent neck's top. The offset is exact at the open pose and within 0.4 mm in between |
| `th-jaw` | 4,020 | Hinged below the mouth line (a straight-cut chin block); a child of the head |

- The mouth's interior is two separate near-black meshes, `th-mouth-cavity` and `th-jaw-floor`. A procedural node's
  single material program would have painted them porcelain.
- The head and jaw have ghost copies.
- **Neck notes:**
  - no collar ring and no ridge: the neck is one continuous surface that bends;
  - the pieces share the whole mesh's smooth normals at every cut;
  - the neck is 30% shorter (shoulders to jaw).
- **The Adam's apple** is a restrained bump on the body, just below the bend's foot. It does not move when the head
  snaps back, and the chin swings away from it.

**The noise gate.** One head, one source: `HEADS` in `tools/sonic_vfx/scenes/talking_heads.py`, so a multi-head
scene adds one spec per channel. The source is the first active of `role.bass.bass` (a bass-role stem or input),
falling back to `audio.bass` (the mix's band).

| Signal | What it is |
|---|---|
| `signal.thLevel` | The source |
| `signal.thLevelS` | The source shifted by CC 3 (sensitivity) |
| Schmitt trigger | Opens at 0.55, stays open while above 0.42, held 33 ms; a distinct 0 or 1 |
| `signal.thChomp` | A one-frame shut on a re-attack while open, so notes that run together still snap one by one |
| `signal.thOpen` | A one-shot on each opening |
| `visual.thGateLive` | Gate x (1 - chomp) x (1 - the shut pad): what the head does |

On Feline Footwear's Bass stem, the whole song gives 444 openings, a median of 0.20 s open, and the gate open 41% of
the time. In the 60-63 s window the gate follows each bass note.

**What the gate drives:**
- Gate ON: the neck snaps back 24° (bend, head rotation and placement), the jaw drops 14°, and the mouth beams step
  on. Their reach follows the level.
- Gate OFF: everything snaps back and the beams cut.
- The transition is a step through a short mechanical settle: an 11 Hz spring at damping 0.62, settling in about
  60-80 ms.
- Each opening flashes the colour wash, the spectral copies, a shake and a restrained tear.

**Scanning the room.** A mechanical yaw about the vertical within ±35°: 1.5-4 s pauses, 1.2-2.6 s linear pans, and
one move in four is a one-frame snap. It is a deterministic 96 s loop on the whole bust. The head does not pitch to
scan; the pitch alternative was dropped by the owner.

**Removed:** the eye beams and their routes, the Float, the noise wobble and the nod. Eyes are next: the head keeps
the eye-recess anchors (`head.eyeLeft` / `eyeRight`) for darting eyeballs.

**Beam misalignment: the cause, and the fix.**
- The head was tilted by routes on `procedural/<node>/transform/rotation`. That turns the *drawn mesh* (the
  procedural's own transform) but not the *node*, and the beams ride the node (ADR-1670). So as the head tilted back,
  its mouth rose and the beams stayed put, apparently "below the eyes and mouth".
- Every pose now routes `nodes/<name>/rotation|position`.
- `tools/talking_heads/anchor_check.py` renders every beam on and off from a fixed close camera, projects each
  origin, and checks the beam starts there. At poses 0:0, 0:14 and 24:14, three-quarter and front: "every beam starts
  at its anchor".
- It also refuses any route that moves a beam owner's drawn mesh without its node.

**MIDI.**
- CC 1 master, CC 2 beams, CC 3 gate sensitivity, CC 4 copies, CC 5 tearing, CC 6 colour, CC 7 gain, CC 8 palette,
  CC 9 head tilt (the snap's angle). Each rests at its middle (palette at 0) with pickup.
- Pad 36 holds the gate shut, 37 holds it open, 39 bursts.

## Clips and stills

| File | What |
|---|---|
| `01-gate-on-the-bass-stem-feline.mp4` | Feline with stems, 56-72 s: the gate on the bass, neck snap, jaw, beams |
| `02-jaw-and-neck-close-up.mp4` | The same, a static close three-quarter camera, beams only |
| `03-scanning-the-room.mp4` | No audio, 0-30 s: the gate shut, the yaw scan (pans, pauses, snaps) |
| `04-full-feline-stems.mp4` | Feline with stems, 40-90 s, everything |
| `05-trench-mix.mp4` | Trench, the mix only (the gate falls back to `audio.bass`), 48-68 s |
| `10-` / `11-adams-apple-and-neck-profile-{0,32}deg.png` | The Adam's apple and the bent neck in profile (Blender) |
| `12-14-neck-front-close-{0,20,32}deg.png`, `15-neck-profile-close-32deg.png` | The neck close up |
| `16-jaw-open-blender.png` | The jaw open, close up |
| `beam-origins/anchor-check-*.png` | Every beam's projected origin, a green ring = ok |
| `00-contact-sheet.jpg` | Two frames from each clip |

## Performance (headless 1080p, Release, M2 Max, AC, 3 reps under the GPU lock)

| Source | GPU p50 ms | GPU p95 | CPU frame p50 | Draws | Triangles |
|---|---|---|---|---|---|
| Feline with stems, from 56 s | 8.74 / 8.75 / 8.66 | 9.2-9.3 | 9.8-9.9 | 16 | 104,256 |
| Trench mix, from 48 s | 8.75 / 8.66 / 8.71 | 9.5-9.6 | 9.8-10.0 | 16 | 104,256 |

Pass medians: `scene` 7.8, depth 0.2, ao 0.15, bloom 0.14, echo 0.12. That is 30% of the 29.3 ms live budget, down
from stop 1's 12.3 ms: two ghost draws fewer, the body's program has no masks in reach, and there are no eye beams.

## Known shortcomings

- **The jaw is a nutcracker hinge.** Opening shows dark wedges at the sides of the chin block. That reads as
  "mechanical", but it is blunt. Softer options: a smaller angle, a hinge further back, or side cheeks that overlap.
- **The tear flash on each opening reads like a seam.** The torn horizontal rows across the neck look like a
  geometry gap; I traced the dark bands in the stills to this. It is kept low; turn it off with CC 5 if it confuses
  the read.
- **The Adam's apple is subtle at this framing.** Easy to raise.
- **On the mix (Trench) the gate follows the mix's whole bass band.** It is busier than a stem.

## Decisions for this review

- Is the gate's feel right? Snap angle 24°, jaw 14°, settle about 70 ms, a chomp on re-attacks.
- The jaw's look: nutcracker block, or softer?
- Proceed to the darting-eyes pass (sideways and downward glances, per the owner)?
- The carried decisions D5-D9 from stop 1.
