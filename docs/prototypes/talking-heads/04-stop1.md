# Talking Heads: stop 1, the vertical slice for the owner's review

The slice runs end to end on `scene/talking-heads`. It is **not yet registered** in `ABSTRACT` or
`examples/index.json`: registration (task 12) follows the owner's approval. Media is in
`~/Desktop/av-gen-review/talking-heads/stop1/`, made by `tools/talking_heads/stop1.sh <project>` (every render through
`tools/gpu-lock.sh`).

## What is built

| Part | Where | Notes |
|---|---|---|
| ADR-1670: a Light Beam follows its owner | `src/world/effects/kinds/light_beam_effect.cpp`, `kinds/stored_rows.hpp` (`b()`), `[adr1670]` in `tests/unit/test_shell2.cpp`, `docs/decisions/ADR-1670-*.md` | Adds `local` (Bool) and `pitch`, for a full-sphere aim in the node's frame. Off is byte-identical |
| The bust | `tools/talking_heads/make_bust.py`, writing `examples/sonic-abstract/meshes/th-head.glb`, `th-body.glb`, `th-head-ghost.glb`, `th-body-ghost.glb` and `th-bust.anchors.json` | See the next section |
| The scene | `tools/sonic_vfx/scenes/talking_heads.py` (`abstract.py build talking_heads`) | 7 nodes, 15 effects, 123 routes, 21 sources, 9 interpret mappings |
| Emitter helper | `kit.Scene.emitter_fan` | N local-space beams on one entity |
| Review tools | `tools/talking_heads/review.py` (still, clip, silent, bench; `--stems`, `--only <features>`, `--wav none`) and `tools/talking_heads/stop1.sh` | |

**The bust** is a custom procedural generalised cylinder. It has:
- a T-shaped brow and nose plane, and almond eye recesses 17 mm deep;
- sharp cheekbones over a hollow, a squared jaw, lips and a 22 mm mouth opening;
- a cut classical bust for the body.

**The neck joint (the owner, 2026-10-25: the head tilts back as it talks).** The bust is split at the upper neck,
just below the jaw (z 0.300 m):
- **`th-head`** (32,008 triangles; porcelain plus `mouth_interior`) has its origin at the joint, and is closed below
  by a dome of the neck's own cross-section.
- **`th-body`** (10,198 triangles) is closed above by a dome rising into the head, with a 3.5 mm collar ring over the
  seam. The ring reads as a mannequin's neck joint.
- At a 32 degree tilt, the body's dome fills the throat as it opens, so no gap shows (`10-` and `11-` stills).
- The open rims inside the domes are intentional, so each piece reports 152 non-manifold edges.
- Orientation: +Y up, face +Z, metres; the body's origin is at the base's bottom centre. A negative turn about X
  tilts the head back.
- Every emitter, mask and head ghost is in the head's frame (anchors file, `head`), so all of them ride the tilt.

**The look.**
- Sky: an indigo-to-violet gradient with no horizon band. Light: a warm key, a violet rim and a faint fill, none
  casting. About 40 slow motes. A 50 mm camera on a 78 s drift.
- **Mouth:** a pale hero cone (10 degrees) plus 6 uneven coloured rays, all riding the head (ADR-1670). The mouth
  interior glows with the voice.
- **Eyes:** 2 narrow blue rays per eye, diverging slightly. They burst on hats, one eye or the other, with a quiet
  shimmer on the highs.
- **Vibration:** a Shake on the bust plus 4 blended ghost copies, magenta and blue, of head and body. The bass parts
  them and they snap back.
- **Chromatic possession:** an OKLab palette wash concentrated by object-space mouth and eye masks, plus a Glow
  flash on onsets.
- **Tearing (D4):** full-screen glitch tear and RGB split in bursts of 110-150 ms on the snare, plus colour bands in
  the material and a Sine shear deformer on both pieces.
- **Life:** a Float, slow noise on the whole bust's yaw and pitch, the head's **tilt back** (`signal.tilt`, below)
  and a small sprung nod on low onsets.
- **States** are continuous:
  - `signal.awake` is presence: `response.level`, 1.2 s attack, 4 s release;
  - `signal.overload` is `response.intensity` above 0.7, self-limiting;
  - pads 36 (hush), 37 (speak), 38 (overload) and 39 (burst) force them.
- **Head tilt.** `signal.tilt` is the voice above its middle (1.4 s attack, 2.6 s release) plus 0.55 x overload
  (0.9 s / 3 s). It gives up to 20 degrees back while speaking and 32 in overload, with an easy return as the music
  falls silent. CC 9 sets the amount, resting at the default; fully up doubles it.

**Audio.** Every route is same-frame, and no `macro.*` is read. With stems or inputs that have roles, the vocal or
synth source drives the voice and the drums source the hits. On a mix, the voice falls back to `audio.mid`, the kick to
the beat stand-in (ADR-1620) and the snare and hat to the mix's detectors.

**MIDI map.** CC 1 master, CC 2 mouth, CC 3 eyes, CC 4 vibration, CC 5 tearing, CC 6 colour, CC 7 gain, CC 8 palette,
CC 9 head tilt. Each is a control channel resting at 0.5 (palette at 0) with pickup. Pads 36-39 as above.

## The clips (spec §10)

| # | File | Source | What to look for |
|---|---|---|---|
| 1 | `01-dormant.mp4` | No audio, no routes | Pale porcelain, almost still; the ether and motes |
| 2 | `02-mouth-emission.mp4` | Feline 68-80 s, mouth only | Hero cone plus uneven rays, wavering with the voice |
| 3 | `03-eye-emission.mp4` | Feline 120-132 s, eyes only | Narrow rays flicking on hats, left or right |
| 4 | `04-spectral-vibration.mp4` | Feline with stems, 72-84 s, vibration only | Copies part on bass and kicks, then snap back |
| 5 | `05-chromatic-possession.mp4` | Feline 100-112 s, colour only | Washes at mouth and eyes; onset flashes |
| 6 | `06-signal-tearing.mp4` | Feline with stems, 128-140 s, tearing only | Snare tears, split, bands, shear |
| 7 | `07-combined-feline-stems.mp4` | Feline with stems, 60-80 s | Everything |
| 7b | `07b-combined-trench-mix.mp4` | Trench mix, 48-63 s | Everything, on a different track and on a mix |
| 8 | `08-quiet-recovery.mp4` | Feline 190-210 s (the outro into silence) | Beams fade, colour recovers, the head settles |
| 9 | `09-head-tilts-back-while-talking.mp4` | Feline 40-64 s, mouth and eyes | The head lifts back as the voice rises; the rays ride it |

Also in the folder:
- `00-contact-sheet.jpg`: one frame from each clip;
- `10-neck-joint-at-32deg-profile.png` and `11-neck-joint-at-32deg-throat.png`: the seam at full tilt, in Blender;
- `bench-*.json`: the numbers below.

## Performance, headless 1080p

Release; M2 Max; AC power; LG at 60 Hz. `--bench-json`, 360 frames, default tier, 3 repetitions, under the GPU lock.

| Source | GPU p50 ms (3 reps) | GPU p95 | CPU frame p50 | Draws | Triangles |
|---|---|---|---|---|---|
| Feline with stems, from 60 s | 12.34 / 12.22 / 12.28 | 16.1-16.4 | 13.5-13.6 | 11 | 126,620 |
| Trench mix, from 48 s | 12.27 / 12.33 / 12.29 | 15.8-16.2 | 13.6-13.7 | 11 | 126,620 |

The pass medians are `scene` 11.3, `depth` 0.15, `ao` 0.14, bloom 0.14, echo 0.12 and glitch 0.06. **That is 42% of
the 29.3 ms live GPU budget,** so the slice is within budget but over the 7 ms target in 03 §5.

**Where it goes.** Ablation, one variant each, Feline with stems from 60 s, GPU p50:

| Variant | GPU p50 ms |
|---|---|
| Full | 12.6 |
| Without the beams | 13.3 (the beams cost nothing measurable) |
| Without deformers | 12.1 |
| Without motes | 12.5 |
| Without material programs | 9.6 (**programs about 3 ms**) |
| Without ghosts | 10.3 (**ghosts about 2.3 ms**) |
| Without programs or ghosts | 7.2 |
| Plus without effects | 5.8 |

**Levers if the live run needs them:**
1. Ghost copies on the head only (two draws instead of four), about -1 ms.
2. Drop the band branch from the body's program, or give the body a short porcelain-only program (the body has no
   face to mask), about -1 ms.
3. Lower the ghost programs' fresnel cost by making them unlit.

**Not yet measured:** the live, paced, UI-on run and the live baseline row. Both come after approval (task 5 of the
coordinator's list).

## Known shortcomings (next tuning pass)

- The onset Glow flash turns the whole bust pink for a moment. Its self-glow (0.5) should drop to about 0.2 so the
  colour stays concentrated at the mouth and eyes.
- The hero cone is still the brightest thing in the frame and runs off the right edge. Decide in D5.
- The eye rays read as two long parallel lines. Shorter lengths or more divergence would separate the eyes.
- The head tilt is modest on dense mixes, because the voice falls back to the mix's mid band. A vocal stem drives it
  harder.
- The head reads as a tall block from some angles, which is the T-plane style. The owner likes the bust; I left it
  unchanged.

## Found on the way

- **`castLight: false` has never applied** (Bool rows were read as floats). It is left alone, awaiting the owner (it
  would change 8-Bit Ocean).
- The Dessen "Male Head" was not downloaded: Sketchfab needs a signed-in API token.

## Decisions for this review (D5-D9)

| # | Decision | Recommendation |
|---|---|---|
| D5 | The mouth fan's look: a hero cone plus thinner rays (as built), or an even fan; and how dominant the cone is | Keep the hero cone, about 30% dimmer |
| D6 | Composition: three-quarter from the bust's right with the fan pouring right (as built), or near-frontal | As built |
| D7 | Validation sources: Feline (mix and stems) and Trench, plus which quiet or ambient track | Add Night Shift or Hyperborea if they are quiet |
| D8 | The MIDI map: CC 1-9 and pads 36-39 (above) | As built |
| D9 | The name and set position: "Sonic Abstract - Talking Heads", #10 after 8-Bit Ocean | As proposed |
