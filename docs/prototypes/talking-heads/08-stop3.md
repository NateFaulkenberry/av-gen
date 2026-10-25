# Talking Heads: stop 3, the multi-head scene on channels (for the owner's review)

Built from `07-owner-notes-stop2.md` on `scene/talking-heads`. Still **not registered**: registration waits for the
owner's approval (the name will be "Talking Heads", see below). Media is in
`~/Desktop/av-gen-review/talking-heads/stop3/`, made by `tools/talking_heads/stop3.sh <project>`.

## Structure: one scene, N heads

`tools/sonic_vfx/scenes/talking_heads.py` builds one head per entry in `HEADS`. Each entry gives:
- the head's role and the band its gate reads;
- the mix fallback;
- thresholds;
- placement, heading, scan timing and ray colours.

Everything a head owns is named by its prefix (`th1-...`). The shipped project has three heads on the Feline stems:

| Head | Source | Notes |
|---|---|---|
| th1 | drums, the stem's low band | Opens on the kick, shuts between kicks |
| th2 | bass, `role.bass.bass` | |
| th3 | synth, `role.synth.mid` | |

**Why one scene and not a sibling.** One code path, one control map and one set of fixes. Heads are found by **role**
(`role.<role>.*`, ADR-1630), so live input channels with roles replace the stems unchanged. A one-source project is
the same scene with one spec; `build(heads=[...])` frames a single head in the stop-2 view.

## What changed

**Gate, stems.** The approved feel is unchanged: a Schmitt trigger with hold, a one-frame chomp on a re-attack, and
the snap.

**Gate, mix (07, "limit the amount of times").** When no source of the head's role is active, the head reads the
mix's band. That path has a threshold 0.15 higher (0.45 for the drums head, on the kick stand-in) and opens for a
fixed 200 ms at most once every 450 ms. Measured in `checks-rays-gates-scans.txt`:

| Track | Openings per second, by head | Shortest interval |
|---|---|---|
| Feline mix | 1.51, 1.36, 0.95 | 0.50 s |
| Night Shift | 1.59, 0.83, 0.60 | |
| Trench | 1.55, 1.36, 0.77 | |
| Feline stems | 1.74, 2.11, 2.36 | |

**Rays only, and they follow the mouth (D5 as revised).**
- The hero cone is gone. Six uneven rays per head, in the head's own colours, short enough to stay in frame.
- Ray brightness **is** the jaw's opening: one pose signal through one route chain drives both. Closed = off, open =
  full, proportional through the settle.
- `tools/talking_heads/ray_check.py` checks every frame. Worst difference over the whole song, on every track: about
  1e-5.

**The pose signal.** The snap's spring now runs in a signal, and each route cuts values under 0.002 to exact 0. At a
curvature of 1e-5, the neck's Bend deformer lost its normals to float precision, which darkened the neck while a
spring's tail settled.

**Bust.**
- **Neck:** 39% shorter than the probe, and bent over its whole length (75 mm, from the shoulders to just under the
  jaw), on finer rings, so the curve is gentler and smoother.
- **Adam's apple:** moved up, about 3 cm under the jaw's underside, as one clean bump. It is on the bending neck, so
  it moves with the neck and the chin swings up and away from it.
- **Jaw (softened):** a 10° drop instead of 14°. The head keeps its skin over the jaw's sides and lower edge, and the
  jaw's hidden edges sit 0.6 mm under it, so the sides slide under the cheeks as it opens: no dark wedges. At rest a
  fine seam line shows at the sides and under the chin.
- **Eyes:** eyeballs (`th-eye.glb`) in the recesses.

**Darting eyes (realistic, per the owner's later note).**
- The eyes are conjugate (one route set drives both) and run through a slightly underdamped spring, so each saccade
  accelerates and settles in about 40-70 ms.
- Fixations come from two stepped noises at incommensurate rates (holds of about 0.3-1.2 s). Moves are mostly small,
  with occasional larger ones.
- They LEAD the head's turn: target minus current heading.
- Downward glances are biased (pitch -4..+16°), with a sub-degree micro-drift during holds.
- Range is kept within ±27° yaw so the iris never sinks into the socket. No lids, so no blinks.

**Scan (made readable).**
- Each head's heading is a stepped target (a new heading every 1.8-2.1 s, per head) through a critically damped pan
  of about 0.6-1.2 s, plus an occasional ±12° snap.
- The range is ±45° about each head's resting heading (+28°, 0, -28°), so the heads sometimes look toward each other.
- The rays sweep with the head.
- Measured: yaw spans of about 100° per head; a median of 8.6-9.7° of heading change per second; 62-67% of all
  seconds turning more than 5°. At stop 2, the same check gave 2.6-6.2° per second.

**Cost control.** Ghost copies rest at a scale of 0.001 and grow only while their head's gate is open (they still
draw, at negligible fill). The frame echo is off.

**Name (D9).** The index entry is `"name": "Talking Heads"`. The Live panel's `liveSceneLabel` strips
`"<category> - "` only when present, so it shows "Talking Heads" in the Sonic Abstract family. At registration,
`abstract.py build`'s generic category entry must be skipped for scenes that write their own `INDEX_ENTRY`;
otherwise the generic name would duplicate it.

## Performance, headless 1080p

Release, M2 Max, AC, 3 reps under the GPU lock. The CPU frame is 12.8-13.9 ms.

| Source | GPU p50 ms | GPU p95 |
|---|---|---|
| Feline stems, from 56 s | 12.33 / 12.51 / 12.51 | 16.3-16.6 |
| Night Shift, from 60 s | 11.85 / 11.70 / 11.46 | 15.0-15.7 |

There are 52 draws and 338 k triangles. That is 42% of the 29.3 ms budget for three heads. `scene` dominates at about
11 ms.

**Levers, if the live run needs them:**
- a lighter body program;
- a coarser neck ring count (the neck is now 3 mm rings);
- ghost nodes hidden rather than scaled.

## Known shortcomings

- **Jaw.** In close-up the softened jaw still shows a fine seam at its sides and under the chin, and a small step at
  the chin in profile at 32°. A truly seamless jaw needs a deformer-driven drop rather than a rigid piece.
- **Nape.** A slight crease remains at the back of the neck where the bend starts. It is softer than stop 2's notch;
  the Bend deformer's curvature starts abruptly.
- **Profile reads tall.** The bust's chest is shallow front to back, so in profile it still reads as a tall column.
  A deeper chest would read more as a bust.
- **Eyes clip.** The eyes close-up is a fixed camera while the head scans, so the face turns in and out of view.
- **Beam-origin check.** It reports MISS on some rays at 24°: the origin is inside the mouth and the lip hides the
  first pixels. Visually the rays leave the mouth (`beam-origins/`).
