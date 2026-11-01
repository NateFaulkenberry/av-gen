# CHRONOS, The Devourer: Gate D1b, the sculpt pass toward inhuman

Media is in `~/Desktop/av-gen-review/104-chronos/devourer-d1b/`. There is no engine change. The creature is still
built by `examples/chronos/devourer/build_scan_creature.py` (the Blay scan route chosen at D1). One light was added
in `build_scene.py`. No ADR.

## What changed, in the coordinator's priority order

1. **Eyes.**
   - The eyeballs are seated in the socket by ray cast, 0.3 of a radius proud of the deepest point.
   - Lids are carved from a shell at 1.07R. The front stays, and the upper lid edge curves down at the sides. The upper lid covers about a third of the ball and the lower lid rises over its bottom.
   - The painted eye has a yellowed sclera, about 40 branching veins, an amber streaked iris (0.27 of the ball) with a dark limbal ring, and an off-centre pupil.
   - Roughness is 0.08, for the wet specular.
   - A dedicated warm spot ("eyes", role fill) lights the sockets, so they read as eyes and not as holes.
2. **Teeth and mouth.**
   - Every tooth is generated separately, irregular in size, lean and spacing. 18% are gaps and 25% are broken or worn, in three shades from bone to brown.
   - The teeth sit in gum tubes.
   - There is a real inner cavity: an inward-facing, back-face-culled flesh shell with ridges that fall to black at depth, and a tongue. Its lower half rides the jaw.
   - The jaw weights widen toward the mouth corners. This fixes the D1 tearing at 66°.
3. **Skin.**
   - The Rodin *Pierre de Wissant* head (CC0, Cleveland Museum of Art) is ray-sampled into a cylindrical relief and high-passed. That relief is baked, with fine pores, into a tangent normal map: modelled clay with thumb marks.
   - The albedo is brush-broken, with posterised stroke noise in ochre and umber, grey-green in the cavities, and burgundy patches.
   - The roughness is mapped.
   - The hair and beard are masked per vertex, so they keep their own colour, a streaked normal and a rough surface.
   - The bakes go into fresh UVs. The scan's UVs overlap, and baking into them was what painted the triangular shards seen mid-pass.
4. **Proportions.**
   - The skull is narrowed above the eye line (x 0.95 to 0.88).
   - The face below the eye line is lengthened 15%, the cheeks are hollowed, and the crown is raised.
   - The sockets are widened and deepened, which reads as larger, wider-set eyes.
   - The jaw rests at 12° open, against about 0° at D1.

The chiaroscuro, the hair and beard mass, the maw and the palette are kept.

## Files

| File | What |
|---|---|
| `devourer-d1b-portrait-three-quarter.mp4` | 24 s three-quarter clip, 1920x1080 at 30 fps, Feline Footwear with the drum stem driving the beat (the devour is at 8.2-9.4 s) |
| `devourer-d1b-three-quarter-rest.png`, `devourer-d1b-three-quarter-devour.png` | stills from the clip at 4 s and 9.2 s |
| `devourer-d1b-before-after.png` | D1 on the left, D1b on the right: three-quarter rest, three-quarter devour, frontal |
| `devourer-d1b-eyes-close.png`, `devourer-d1b-teeth-close.png` | AV Gen close-ups (the teeth at 9.2 s, jaw open) |
| `devourer-d1b-frontal-rest.png` | frontal, for comparison only |
| `devourer-d1b-blender-*.png` | the same four views from Blender (Eevee), straight out of the build |

## Honest critique

- **Better:**
  - The face is longer and narrower, and less human.
  - The skin finally reads as modelled clay rather than a smooth scan.
  - The hair and beard keep their mass.
  - The teeth are irregular, broken and gapped, with a dark throat behind them.
  - The corner tearing is gone.
  - The eyes now carry a real iris, veins and a specular.
- **Not there yet:**
  - **The eyes at portrait distance.** They still read as round balls more than sunken, hooded eyes. In the three-quarter view the near eye sits proud of the brow line. The lids work in close-up but are too thin to shadow the ball from 52 m. The next step is to sink the ball deeper and thicken the upper lid into a heavy fold that casts shadow, which costs some iris visibility.
  - **The devour.** At 66° the lower face stretches into a long, hanging sheet of skin with the teeth carried down to the chin. It is unsettling, and closer to Goya's elongation than D1's sack, but it does not yet read as a gaping maw from three-quarter: the cavity is visible only in the close-up. A wider mouth slit and deeper cavity, or a camera a little lower, would fix it.
  - **The skin relief is strong in close-up.** In the eye close-up it reads as a coarse, uniform orange-peel. Scaling the pore layer down by about half would help.
- **Three-quarter vs frontal:** three-quarter is the stronger composition. The raking key models the long face, and the hair mass frames it. Frontal shows the new proportions most plainly (the length, the narrow skull), and is worth keeping as a cut-in.

## Decisions for the owner

1. Is this inhuman enough to start D2 (procession and consumption), with the eye depth and maw readability as polish items carried into D2? Or one more short pass on just those two?
2. Should three-quarter be confirmed as the master composition?
