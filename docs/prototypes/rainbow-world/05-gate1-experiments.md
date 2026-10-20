# Rainbow World 05: Gate 1 experiments (throwaway)

These are small Blender experiments to inform the dossier. None of them is an asset. Each was rendered headless in
Blender 5.2.1 LTS with Cycles on Metal, at 640x360 or 960x540 and 16-48 samples, in about 20 s each.

## E1: lens study (`blender/exp_fisheye.py`, `scripts/remap_fisheye.py`)

Image: `~/Desktop/av-gen-review/74-rainbow-world/gate1/08-E1-lens-study.png`.

**Setup.** One camera, rolled 25 degrees, over a blockout of hills and trees. It is rendered four ways:
- rectilinear at 120 and 140 degrees horizontal, with 1.5x overscan;
- those two frames remapped on the CPU to stereographic or equisolid fisheye, fitted to the frame diagonal;
- true equisolid fisheye from Cycles at 150, 180 and 220 degrees diagonal.

**Results.**
- Rectilinear 140 degrees (frame B) stretches near trees at the edges into smears. That is not a usable look.
- The remap of B, stereographic and fitted to the diagonal (frame D), is visually the same picture as the true
  150-degree fisheye (frame F).
- The centre magnification of the remap is 1.15 (from 120 degrees) to 1.50 (from 140 degrees, stereographic) and
  1.83 (equisolid). That factor is the render scale needed to keep the centre as sharp as a native frame.
- Only 180 degrees and above (G, H) gives the curved, bowl-shaped horizon seen in the clip. A single rectilinear
  render cannot supply that much angle.

**Decision.** A post remap at about 150 degrees is the lens. The rest of the clip's curvature has to come from the
world's own shape. That is uncertainty U1.

## E2: closed twisting band (`blender/exp_knot.py`)

Image: `09-E2-knot-world-greybox.png`.

**Setup.** A trefoil torus knot (S = 160 m per unit, about 1.5 km across) carrying a 90 x 18 m superellipse cross
section with one extra full twist. A camera hugs the surface (up = band normal) at six stations, with a 180-degree
equisolid lens. There are about 70 blob clouds.

**Results.**
- Land hangs overhead (s=0.09).
- Land rises into walls ahead as the band twists (s=0.33 and s=0.41).
- Gravity changes with no deformation and no spin.

**Faults.**
- The band is too narrow and the knot too open, so the sky takes 40-60% of the frame.
- The grey rims read as the edge of a road.
- Station s=0.02 rendered black, because the camera was inside a cloud blob.

**Decision.** Approach A is adopted with these Stage 4 requirements:
- the band 3-4x wider relative to the gaps;
- the rims designed as landforms;
- cloud keep-out zones round the path.

## E3: rainbow representation (`blender/exp_rainbow.py`)

Image: `10-E3-rainbow-study.png`. AgX Punchy, -1.3 EV.

**Results.**
- **(a) A flat ribbon with six hard stripes** reads as a sticker.
- **(b) A soft emissive volume tube with a spectral ramp** reads as light. Over the pale lower sky it turns pastel.
- **(c) b with an inner skirt, supernumerary, dark gap and secondary** reads as a real bow. The skirt as built is too
  wide and milky.
- **(d) c with coloured spill on the ground** ties the bow to the land.

**Decisions.**
1. The hero rainbows are soft volumes carrying the physical cues.
2. The inner skirt is narrowed and dimmed.
3. Rainbows are placed against darker backdrops: chroma comes from contrast.
4. The spill light is kept.
5. Emission stays below AgX's desaturation knee. This is checked again in AV Gen (U6).
