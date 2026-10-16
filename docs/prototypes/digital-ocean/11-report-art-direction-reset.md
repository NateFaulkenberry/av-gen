# Digital Ocean, Pass 4: the art-direction reset (report)

*The art agent's report on the owner's brief `10-brief-anime-art-direction-reset.md`. Branch `art/digital-ocean-3`,
after Pass 3. The full log, with every wrong turn and the register dumps that found them, is the "Pass 4
(art-direction reset)" section of `PROGRESS.md`. Media is in `~/Desktop/av-gen-review/57-digital-ocean-reset/`:
`00-baseline.png`, `01-water-pass-A..D.png`, `02-compositions-first-round.png`, `03-contact-sheet.png`,
`04-selected-still.png`, `05-final-clip-music.mp4`, `profiles/`, and the look-development frames in `work/`.*

## What changed, and why

The brief's one line: stop making a conventional 3D ocean look colourful, and design an illustrated ocean from the
ground up. Pass 3 had bands, but they were cut from a one-sided scalar (its height gradient saturated below the mean
level), its sun was a physically reflected ray jittered into sparks, its clouds were piles of spheres each with its
own rim, and its island was a thin strip of five trees that vanished into the horizon.

| | Pass 3 | Pass 4 |
|---|---|---|
| Composition | eye 14 m, 32 mm, a 640 m strip 900 m out | eye 10 m, 36 mm, the horizon at 47 % from the top (the owner's direction: water and sky about half each, a touch off centre; measured 47.0-46.7 % through the breath); a 96 x 52 m island 270 m out on the left third, the palm ~110 px tall |
| Island | an extruded strip, five cut-paper trees, two far strips, unlit flat paint | a 10 m plateau over an eroded, half-terraced, faceted rock base (the owner's direction), ONE windswept palm reaching toward the sun, ten Kenney rocks on the shelf and in the surf, a thin surf line; the island, palm and rocks are LIT cel materials under a key light authored at the sun |
| Water: light | height (saturated at the mean) + orientation | height CENTRED on the mean + the wave backs' orientation + 7 % wind-streaked noise, times the footprint gain |
| Water: regions | four soft bands | a foreground zone, then troughs (soft cut) / body / lit backs (crisp cut) |
| Water: marks | continuous strokes under every crest; caps cut from noise | one marks scalar (noise 30 / crest 70): strokes as tapered dashes on the crests, foam only on the reinforced ones |
| The sun on the water | a reflected-ray test, jittered into sparks | a designed column under the sun, narrow at the horizon, 3.2x wider in the foreground, lit as broken horizontal marks by the same scalar: ragged edges, scattering toward the viewer |
| Horizon | Fresnel^7 everywhere | Fresnel^24 near (gain 0.45), ^10 far (0.8): the blend lives in the last degrees |
| Rays | 2.6 per turn, hard cut | 1.9 per turn, a soft cut times the noise: broad, tapered, unequal |
| Clouds | ~320 spheres in rows | 16 lofted pillows (one contour each: 3-5 lobes over a flat underside), two-tone by one soft edge |
| Palette | violet sunset (Pass 2's) | retuned to the brief's design: indigo troughs, cobalt-violet body, lavender backs, pale turquoise strokes, pink-white foam, a peach-gold path, mauve/rose sky, coral clouds |
| Boats, dolphins | kept | kept; the wake quieter (narrower arms, higher cuts) so it sits under the illustrated foam |

The renderer is unchanged: no new pass, no engine change, no ADR. The scene now authors one light (the sun, no shadow) for the island's cel materials; everything else stays unlit. The water is still one material program on the
displaced sea mesh, 48 ops near (the budget) and 38 far.

## Assets: what was reused and how

- **Kenney rocks** (`assets/kenney/rock_largeA/C/E.glb`, `rock_smallA`, `rock_smallFlatA`; CC0, tracked, 16-80
  triangles each, ~1 m): eight instances scaled 9-16x, painted flat by `doIllus` in the palette's `rock` (their
  dirt/grass materials overridden; the engine draws a multi-material GLB as one instanced draw per part). A low
  asymmetric group of three at the island's left end balances the palm on the right; one stands in the water off
  the end; four low ones sit along the near shore. The stacked `rock_tallB` was tried first and read as a stepped
  tower (the coordinator's note), so it went.
- **The Tree of Life** (`assets/treeisle/`): inspected by projecting its vertices -- a broad spreading crown on a
  gnarled trunk, 77 m tall, 116 m wide, 3.16 M triangles, flat PBR factors (the Glowmere look did not survive the
  export, ADR-338). Not reused: a derived mesh would inherit its unrecorded licence and its gitignore, this live
  example must render on CI without private assets, and at this scale a spreading crown reads as a park tree where
  the brief's classic tiny island wants a palm. The floating island is a hovering rock with a cliff face.
- **Poly Haven rocks** (`assets/terrain/rocks/`): gitignored scans with 1k textures; the same CI reason, and a
  photoscanned rock fights a flat illustration.
- The palm is designed in the generator (`palm_mesh`, ~900 triangles): a trunk that leans and curves toward the
  sun with a slight S and a root flare, nine tapered ribbon fronds that rise, arc and droop, all swept toward the
  sun, each twisted so some read broad and some thin, three dead fronds under the crown, a coconut cluster. The
  crown alone sways (0.25 m, ~9 s).
- Reused unchanged from Pass 3: the 16 m motor yacht and its masked Kelvin wake, the dolphin pods and the hero
  leap, the sea mesh and its Gerstner trains, the audio fields, the illustrated program.

## The variant choices (`03-contact-sheet.png`)

- **Compositions**: F1 low and close (eye 10 m, 36 mm), F2 the classic poster (14 m, 32 mm), F3 elevated and wide
  (21 m, 28 mm). **F1**: the only framing where the island is recognised at once; F2 keeps more wave pattern but the
  island is a sliver; F3 loses it. The first round (`02-compositions-first-round.png`) also exposed that the whole
  island painted peach: the polar grid was wound inside-out and the colour pass culled it while the depth prepass
  wrote it, so the sea had a hole showing the clear colour. Both the island and the cloud pillows are now checked
  numerically (each face's on-screen winding) before a render.
- **Water treatments**: A broad bands, B + crest strokes, D + the path + foam. **D**, built on A once A read as
  illustrated on its own (`01-water-pass-A.png`): the first two A's did not -- the horizon blend, not the cuts.
- **Clouds**: layered silhouettes vs a richer formation (a second tier of lobes, two more banks). **Layered**: the
  rich one is bumpier, not better, and the brief says not to pick the most detailed by default.
- **Selected**: F1 + water D + layered clouds (`04-selected-still.png`).

## Palettes

Every palette names the whole world. The primary, **violet sunset**: sky zenith `#2a1a66`, mid `#8a4aa8`, horizon
warm `#ffb08a` / cool `#c87ab8`, sun `#fff2dc`, glow `#ff8a6a`; water foreground `#1c1a6c`, troughs `#2d2a96`, body
`#3f3ab0`, lit backs `#7a5ed0`, strokes `#a8ecea`, foam `#ffe4f4`, path `#ffd6a8`, horizon reflection `#d898cc`;
clouds shadow `#5a3690`, lit `#ffa48e`, rim `#ffe2c4`; land `#4a3690` / underside `#2a1a5e` / lit `#b07ab0`, sand
`#f0b4b8`, rocks `#6a4494`, trunk `#2a1c5c`, fronds `#30246f`; yacht pearl `#fff0e6`, glazing `#1c1a4a`, deck
`#dccbd8`; dolphins back `#1d1448`, belly `#8a68b8`, rim `#ffc8a8`. The golden afternoon, electric blue and
bioluminescent midnight palettes keep Pass 3's values with the two new keys (`wnear`, `frond`) added.

The one axis `visual.mood` = CC 1 + 0.12 x the piece's 12 s intensity + the daybreak envelope (was 0.35 x
intensity: the routes run at gain 2, so a build carried every reactive still a third of the way to the afternoon
and the owner's sunset rarely held).

## Audio and MIDI mappings

No new analysis path, nothing sampled in a shader; the routes read the response signals the scene already used:

| Signal | Response |
|---|---|
| level | the long swell grows and quickens (integrated phase); the crest span (the gradient's y component only) and the foam cut rise, so caps stay caps; the lit cut narrows |
| intensity | the palette mood (0.12); the band edges tighten a little |
| bass | the tide field rolls the bass's history in; the troughs deepen, the backs lift |
| kick | the onset front rolls in; the backs and the foam flash |
| hats / highs | the path's marks and the crest strokes flare; mint on the backs |
| mids | the wind sea roughens |
| sustain | the halo, the sky's middle, the clouds' lit undersides |
| snare, notes | spray; sparks on the path by pitch and velocity |

MIDI, unchanged in number and meaning, retargeted to the new ops: CC 1 palette, 2 night, 3 waves, 4 wave energy
(chop, the tide, the path's marks brighten), 5 sun rays (now also the path on the water: one lighting design), 6
clouds, 7 water colour, 8 spray, 9 wave speed, 10 wave direction, 11 band contrast (both cuts tighten), 12
highlights (the strokes brighten 1.5x, their cut opens), 13 foam (the cut drops, the foam brightens), 14 surface
detail (the noise's share 0.07 -> 0.16; t stays centred because the noise's mean is the middle). Pads 36-41 as
before; the hero wave (37) carries a foam line and brighter strokes while it rolls in. Every route that addresses a
stage-dependent op is guarded, so the look-development stages A-C build without them.

## Performance (M2 Max, `tools/live_scene_profile.py` under the GPU lock, reports in `profiles/`)

| Case | Result |
|---|---|
| Headless 1080p Ultra, silent, 300 frames from 21 s | GPU 24.58 ms median (P95 24.97); Pass 3: 26.54. Scene 20.05 + depth 3.15 ms; particles 0.52; AO 0.46; post 0.26 |
| Live path 1080p, Auto (settles at High, 1632 x 918), 30 fps target | 150 frames in 5.0 s (30 fps held); GPU 25.49 ms median (P95 26.94); CPU work 13.1 ms (the editor's UI build); 3 deadline misses (2.0 %), each one lost vsync; status "at risk" -- the same finding as Pass 3: the misses are the swapchain wait and the editor's CPU, not the scene's GPU |
| Live path 1280 x 720, Auto (High, 1088 x 612), 30 fps target | 150 frames in 5.0 s (30 fps held); GPU 21.89 ms median (P95 22.81); CPU work 11.7 ms (UI build and record 8.9); 2 misses (1.3 %); status "at risk" for the same reason |

The sea program is at its budget (48 ops near); the pillows are ~27 k triangles for 16 formations against the
~138 k of Pass 3's spheres.

## Known limitations

- **A whole cloud layer in one GLB, instanced at the origin, never drew single-sided.** Not winding (checked on
  screen), not the program (a register dump showed the clear colour), not the deformers (none made no
  difference); double-sided it drew, and so does the per-formation structure now used. The engine cause is not
  found; nothing in the scene depends on it any more, and it is left here as a question for the renderer's owner
  (a 14 km bounding box round the camera is the one thing that structure had that nothing else in the scene has).
- **The foreground path** is the brightest thing on the water under loud music: the level raises the swell and the
  near marks grow with it. It stays broken by the troughs, but it is the first thing to restrain if the owner
  finds it loud.
- **A scored trigger before a render's start never fires.** Two review renders of the hero leap (pad 36) showed
  nothing because the harness started 3 s before the still and the pad sat 5.8 s before the peak. With the
  trigger inside the rendered range the leap arcs across the path (`work/leap6-leap.png`); a live pad is live.
- **Intermediate moods**: with the music's share at 0.12 the sunset holds; CC 1 between palettes still lands on a
  blend, as Pass 3 noted.
- **The near program has no headroom**: 48 of 48 ops. One more water feature means removing one.

## Checks

On the branch after merging origin/main (the project-switch work; the engine rebuilt):
- `check_project_integrity`: 0 problems.
- `review_examples --only "Digital Ocean"`: ok, exit 0.
- `cpu-assets`: exit 0, 4255 passed, 0 failed, 19 skipped, the 1 expected failure.
- `gpu`: exit 0, 661 passed, 0 failed, 1 skipped (42 m 26 s).
