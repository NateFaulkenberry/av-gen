# Rainbow World 01: the reference clip, analysed

Gate 1, §2.1 and §15.1 of the brief. Everything here comes from decoding `~/Desktop/rainbow world/clip.mov` frame by
frame (FFmpeg 7, Python). Observations are marked **Observed**. Conclusions drawn from them are marked
**Inferred**. Things the footage cannot settle are marked **Unknown**.

Media (not committed; the clip's source and licence are unknown):
`~/Desktop/av-gen-review/74-rainbow-world/gate1/`
- `03-clip-every-0.25s.png`: 34 frames, one every 0.25 s.
- `04-clip-keyframes-full.png`: 8 key frames at the full 640x480.
- `05..07-clip-dense-*.png`: sampling at 0.1-0.12 s through the three orientation changes.

Tools: `scripts/clip_sheet.py` picks the frame with the nearest pts and labels it, because the clip is VFR.
`scripts/clip_motion.py` gives the change metric and the roll estimator.

## 1. The file

| Property | Observed |
|---|---|
| Container | QuickTime `.mov`, 3.7 MB |
| Video | H.264, 640x480 (4:3), 328 frames, 8.32 s |
| Frame rate | **Variable**: nominal 120 tbr, average 39.4 fps. The frame intervals are a mix of 1/60 s (118 frames), 1/24 s (72), 1/40 s (55), 1/30 s (36) and 1/120 s (36). |
| Audio | AAC, about 2 kb/s, effectively silent. Not used. |
| Provenance | **A screen recording of a video player.** For the first 2.3 s a YouTube-style control bar (`0:52 / 2:47`, `Screenshot`, CC and settings icons) and a mouse cursor sit over the picture. The clip is about 8 s cut from 0:52 of a 2:47 video. |
| Image quality | Soft, with compression blocking in the dark areas and motion smear on fast frames. Usable for shape and motion, not for texture detail. |

**Inferred:** the variable frame rate and the duplicate frames come from the capture, not from the animation. Speeds
are therefore measured against the pts, never against the frame count.

## 2. What happens, beat by beat

| Time (s) | Observed | Reading |
|---|---|---|
| 0.0-1.3 | Drifting round a sandstone tower. A figure sits on a stair at lower left, another stands on a balcony above. A spiral stair is on the left, curved walls with windows are on the right. A tiled floor and pale sky show at the frame edges. | Establishing: dense, legible, slow |
| 1.3-2.2 | The camera turns right. A doorway with a figure seen from above (head toward the bottom of frame) comes in from the right edge and moves toward the centre. | The first landmark is acquired |
| 2.2-3.1 | The doorway figure is held near frame centre and grows, while the surroundings roll by about 30-40 degrees. The doorway's long axis goes from tilted right to tilted left. | Approach plus roll round the anchor |
| 3.1-4.0 | Close on the doorway figure. Above it a second stair and a seated figure appear. The doorway slides toward lower left. | The next landmark is visible before the turn |
| 4.0-4.9 | **Disorientation peak.** The camera pitches over the doorway's edge, and the first figure leaves at lower left. The view looks down a spiral stair with the second figure on it. The rails sweep round the frame, and the dense sample shows the frame turning about 90 degrees over 0.8 s. | Gravity flips. The new "down" is the stair. |
| 4.9-6.1 | The camera descends toward the stair figure as the roll unwinds. The figure moves from tilted about 30 degrees to upright and grows to fill a third of the frame. | Resolution of orientation |
| 6.1-6.8 | Closest approach. The figure is upright, seen from slightly below, with stairs leading up to it. | Moment of clarity |
| 6.8-8.3 | A steady pull-back down the stair, upright and slower, through a sequence of nested arches. These form a dark ring round the frame by 8.26 s. | Resolution and framing. The arch ring hands over to the next section. |

**Observed:** there are no cuts. Every frame shares geometry with its neighbours at 0.1 s spacing (dense sheets
A-C).

## 3. Camera behaviour

**Lens. Observed:**
- Straight architectural lines bow strongly toward the edges.
- The horizon of the tiled floor curves into a bowl at the frame edges in every orientation.
- The corners carry image; there is no black circle.

**Inferred:**
- This is a full-frame fisheye, roughly 160-190 degrees across the diagonal, judged against the lens study (E1, §6): frames G (180 degrees) and H (220 degrees) have the same bowl-shaped horizon.
- It is not a rectilinear lens. Even 140 degrees rectilinear smears the near objects at the edges into streaks (E1 frame B), and the clip shows nothing like that.

**Unknown:** the exact model (equisolid, equidistant or stereographic) and whether the source was cropped from a
circular fisheye.

**Speed. Observed:** `clip_motion.py` gives the mean absolute luminance change between samples 0.2 s apart. This is a
proxy for apparent speed:

| Window (s) | 0-1.8 | 1.8-2.6 | 2.6-3.6 | 3.6-4.6 | 4.6-6.0 | 6.0-7.0 | 7.0-8.3 |
|---|---|---|---|---|---|---|---|
| Change (0-255) | 28-34 | **36-38** | 22-31 | 28-31 | 24-29 | 25-31 | **15-18** |

**Inferred:**
- Speed is near constant through the middle, with no stops.
- The fastest image change comes during the roll into the first landmark, not during translation.
- The ending decelerates to about half the speed.
- Accelerations are smooth. Nothing reads as a jerk.

**Roll. Not measured.** I tried a Fourier-Mellin (log-polar phase correlation) roll estimator. It recovers synthetic
rotations of 5-30 degrees exactly, at peak 0.21-0.30. On the clip its peaks are 0.02-0.05, so its readings there are
noise: parallax, the fisheye and translation dominate. The roll figures in §2 are therefore read by eye from the
dense sheets, by following one long edge (the doorway, the stair rail, the figure's axis). Treat them as ±15 degrees.

**Translation versus orientation.** Observed: the geometry is rigid. Across consecutive samples no wall, stair or
figure changes shape apart from the lens's distortion as it moves across the frame. Inferred, as best the footage
allows:

- **Camera motion produces:** the roll, the pitch-over at 4.0-4.9 s, the approach to and pull-back from each figure,
  and all the bending, which comes from the lens.
- **World geometry produces:** the changing direction of gravity. Stairs and figures are built at 90 and 180
  degrees to one another, as in Escher's *Relativity*. The camera never needs the world to deform; it needs a world
  in which several "downs" coexist, and then it moves from one to the next.
- **Transitions are concealed** by three devices:
  - the next landmark is already in frame before the turn (3.1-4.0 s);
  - the turn happens while the camera passes close over an edge, so near geometry sweeps the frame;
  - the ring of arches at the end both frames the image and acts as a natural wipe.

## 4. What fills the frame

**Observed:** at every sample, 85-100% of the frame is structure: walls, stairs, arches and figures. The remainder is
a pale sky band and a tiled ground plane seen at the frame edges, always curved by the lens. No frame shows empty
space larger than about a sixth of the image.

## 5. Clarity, disorientation and resolution

| Phase | Time (s) | What makes it so |
|---|---|---|
| Clarity | 0-1.3, 6.1-6.8, 7.5-8.3 | An upright figure near the centre, slow change, symmetric framing |
| Building tension | 1.3-4.0 | One landmark held at the centre while everything round it rolls |
| Disorientation | 4.0-4.9 | Pitch over an edge; no stable horizon; a second gravity takes over |
| Resolution | 4.9-6.8 | The roll unwinds as the new landmark is approached, until it is upright |
| Release | 6.8-8.3 | Pull-back, deceleration, nested arches |

## 6. Translating this to Rainbow World

What I take from the clip. Each item feeds the plan in [04-production-plan.md](04-production-plan.md).

1. **A full-frame fisheye of about 150-180 degrees, at all times.** E1 shows that a remapped rectilinear frame is
   indistinguishable from a true fisheye up to about 150 degrees on the diagonal (frame D against frame F). Beyond
   that, the clip's bowl-shaped horizon needs more angle than a single rectilinear render holds. That is
   uncertainty U1 in the plan.
2. **Gravity changes come from the world, not from spinning the camera.** The world must hold several coherent
   "downs", and the camera follows the surface it is near. This is the core argument for the closed, twisting land
   form in [03-art-direction.md](03-art-direction.md) §3.
3. **Landmarks carry the eye through a roll:**
   - one hero element (a hero tree or a rainbow arch) is held near the centre while the world turns;
   - the next is visible before the turn;
   - every passage resolves to upright at a landmark.
4. **Transitions are concealed** by close passes over edges and by framing rings. Rainbow arches and arching tree
   canopies are our arches.
5. **Rhythm:** about 2-3 s per beat, steady speed, deceleration into each resolution, no stops. The clip runs at
   roughly one orientation change every 2.5-3 s. A live scene plays for minutes, so the journey needs calm
   stretches between these sequences (see the plan).
6. **Not taken:** sandstone, mannequins, the tiled floor, indoor architecture.
