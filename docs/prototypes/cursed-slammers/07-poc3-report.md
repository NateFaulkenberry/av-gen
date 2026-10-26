# Cursed Slammers POC 3: opposing layers (report)

2026-10-25, branch `scene/cursed-slammers`.
- The owner found POC 2 "kind of an interesting look" and approved one more pass on the same Gorilla Face (CC BY 4.0,
  credit in `assets/cursed-slammers.manifest.json`). No replacement.
- Same isolated harness and cached offline view pipeline: nothing registered, no engine change.
- Everything derived from the scan stays in the gitignored `assets/cursed-slammers/`.
- Media: `~/Desktop/av-gen-review/93-cursed-slammers/poc3/`.

## Verdict

**Closer, and now a clearly lenticular *behaviour*, but still not the full 3D-lenticular *look*.**

What improved:
- With a near layer and a far layer moving in opposite directions around the face, and twice the view span, the
  sweep reads as a layered card being tilted, not a head turning.
- The muzzle reaches forward and the eyes sit in their sockets.

What is still missing:
- The *static* pop: a real card's depth even when you hold still. That depth is binocular. The red/cyan diagnostic
  (`03-…`, `14-…`) shows how much of the look it carries: through glasses, the muzzle and bars stand in front of the
  face, which stands in front of the backdrop.
- A flat monitor cannot show this. Only motion parallax survives, so on a monitor the effect lives in the motion.

**Caveat:** I judged from sampled frames and strips, not by watching the clips in real time. The owner's viewing is the
real test.

## What changed from POC 2

| Brief item | Implementation | Note |
|---|---|---|
| 1 Opposing layers | **Near:** two thin polished bars at z +0.40. **Key plane:** the face at z 0. **Far:** the mottled backdrop at z −3.2. Every view converges on the key plane, so the bars move against the viewer and the backdrop with it | The bars were first at +0.95. At 24 views a near layer that far out jumps about 29 px per view, and the crosstalk doubled the bars visibly. That is the real lenticular depth budget (designers keep per-view disparity to a few pixels). At +0.40 a jump is about 7 px |
| 2 Wider sweep | 24 views over ±11° (POC 2: 16 over ±6°) | The viewer sweep stays inside the lens's zone, so no pseudoscopic wrap mid-sweep |
| 3 Muzzle depth | `tools/cursed_slammers/poc3_assets.py`: B1's relative relief ×2.2, rising to ×2.8 inside a soft ellipse round the muzzle (its centre is the centroid of the frontmost 3% of the scan) | ×3.6 made a beak and was rejected. ×2.8 reaches forward but **lengthens the face toward a snout**; the owner should judge whether that is too far |
| 4a Back volume | A near-black ellipsoid behind the mask (`cs-back.glb`) | The cut edges no longer show at ±11°. At the extremes its own rim shows faintly as a dark disc edge against the backdrop |
| 4b Eyes | Two glossy eyeballs (`cs-eye.glb`), radius 0.036, a dark iris cap, set one radius behind the carved eye's surface. A small light above the lens gives each a catchlight | The automatic socket search failed (it found the mask's receding sides), so the frontal centres were read off a frontal render and passed as `--eyes`. Simple marbles, no wet cornea; they read as eyes at review size |
| 5 Lens glints | `lenticular.py`: the glint is supersampled 16× per pixel. Before, a glint narrower than a pixel fell between samples, which is why POC 2's was nearly invisible. The light sits beside the viewer, so its reflection window moves at half the viewer's speed and crosses the face. The window is narrow (0.07 × 0.10 print widths), the glint width 0.03 rad, peak gain 4 | At full resolution: a comb of thin vertical lines on the brow, only inside the moving window. Elsewhere nothing (`12-lens-glint-crop-full-res.png`). At half size it reads as a soft sheen |
| 6 Stereo check | A diagnostic red/cyan anaglyph: each eye looks through the lens from its own position (X ∓ 0.1 print widths, about 3 views apart), red = left eye, cyan = right. **Labelled DIAGNOSTIC** | The binocular depth is strong: muzzle and bars forward, backdrop deep. At the sweep's extremes one eye can leave the lens zone (a brief wrap) |

**Also changed (simulator): interlacing corrected for the viewing distance** (`--curtain 0.25`).
- With the print interlaced for a viewer at infinity, a viewer at 4 print widths saw up to 15 different views across
  the print at once. The face split into mismatched regions.
- Real printers correct the interlace pitch for the design viewing distance (the pitch test). The simulator now models
  that: the whole print shows nearly one view, and only a residual curtain drifts across.

## Performance

- **The live 3D parallax-only stage** (one view per frame): headless 1080p GPU p50 **11.96 ms** (3 reps: 11.94,
  11.96, 11.97), scene pass 11.0 ms, CPU 13.5 ms (`06-performance.txt`). POC 2's single view was 14.9 ms.
- **The lenticular stage** stays a cached offline study: 24 views rendered once, the print and the viewer in numpy at
  about 240 ms a frame on the CPU.
- **A live version** would cost one view plus about 0.06 ms for the lookup, measured in POC 2. It needs **ADR-1690**
  (image inputs for shader layers, so a post layer can read the cached views) and 200 MB for 24 cached 1080p views, or
  50 MB at half resolution.

## What is still missing for the 3D-lenticular look

1. **Binocular disparity.** Not available on a flat monitor. The anaglyph shows it is a large part of the look.
   - If the venue allows, a stereo or autostereoscopic output (or a real lenticular monitor, which the same 24-view
     interlace would drive almost directly) is the only way to get the static pop.
   - Otherwise accept that the effect lives in the motion.
2. **More near-layer separation within the depth budget.** The near layer must stay close to the key plane, or it
   doubles at 24 views. More views (48+) would allow a deeper near layer, at offline cost only, since views are cached.
3. **The face's proportions.** The muzzle exaggeration that sells depth also lengthens the face. A layered
   representation (a muzzle layer, a brow layer and an eye layer as separate depth cards, each keeping the scan's own
   relief) may give the stacked-card read without reshaping the face. That is the next representation worth testing.
4. **Horizontal resolution.** 480 lenses at a 4 px pitch soften fine detail horizontally: a true property of
   lenticulars, but visible on the fur and wrinkles. A 3 px pitch (640 lenses) is the next step if wanted.
5. **Asset.** The eyes are simple marbles, and the back volume's rim shows faintly at the extremes. Both are cheap to
   refine. The scan itself is otherwise untouched and adequate.
