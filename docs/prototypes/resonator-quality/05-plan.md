# Resonant Visual Worlds: the debate, the proofs and the plan (steps B–D)

The brief is [03-brief-resonant-visual-worlds.md](03-brief-resonant-visual-worlds.md) and the research is
[04-research.md](04-research.md). The proofs are in `~/Desktop/av-gen-review/64-resonator-quality/overhaul/proofs/`.

The scoring used in each debate follows the brief:
- **Priority order:** geometry, then composition and depth, then colour, then material and light, then animation,
  then polish.
- **§14's anti-patterns** are a veto.
- **Engine budget:** 48 ops, 16 fields, 4 fields a program, vertices that are dear where deformers run, and 30 fps
  live at 1080p.

## 0. What already exists

The quality pass gave all three scenes:
- OKLab gradients (ADR-1470) between palette roles;
- brighter bodies, real fills and drifting colour phases;
- Cymatic World's offset-lattice ground;
- Waveform's clean cuts;
- Silk's fuller frame.

The overhaul builds on that; it does not restart.

## 1. The debate

### 1.1 Cymatic World

**Geometry and pattern system (the deciding axis).** Why the pattern is always radial, from the code: every
large and medium source is a `point` source (`R_m(kr)·cos mθ`, the circular family) about centres within 10 m of
each other. The candidates:

| | Approach | For | Against | Verdict |
|---|---|---|---|---|
| G1 | More orders and centres on the point bank | No engine work | §14 veto ("more radial rings"). Proof 1 shows the family is circular whatever the orders | **Rejected** |
| G2 | Lattice sources (n = 2, 3) at the large scale, plus the point bank, cross-faded | No engine change; hexagonal and grid families exist (proof `cw-A`, panels 4 and 8) | A lattice is a wallpaper of plane waves (proof `cw-C`'s medium "waffle"): regular, not Chladni. No curved nodal networks | **Kept for one family (hexagonal)** |
| G3 | **A `plate` mode geometry (ADR-1471):** a square-plate mode `cos(nπu)cos(mπv) − cos(mπu)cos(nπv)` as four signed plane waves of one wavenumber | The true Chladni vocabulary of curved crossing networks, loops and lobes (proof `cw-A` panels 2–3, `cw-C`). A source lists its (n, m) pairs, so each mode keeps its identity and phase. Cross-fading near-degenerate pairs gives Grinsted's compound figures | A shared engine change (`resonance.cpp`, `fields.wgsl`, a GPU parity test): about 60 lines and one ADR. Needs the coordinator's yes | **Chosen** |
| G4 | A continuous "Chladni angle" morph (one routed angle) | One knob morphs everything | Proof `cw-B`: non-rational angles give quasi-periodic mush, not organised figures | **Rejected** |

**Family director.** The scene keeps one resonator (ADR-1400) and gets four families as sources:
- **P, plate** (G3, the hero): (3,2), (5,1), (4,3), (6,1) and so on.
- **H, hexagonal lattice** (n = 3).
- **R, radial** (the current point source, now one family among four).
- **A, arcs:** a low-order point source far off-frame, which draws sweeping arcs, not rings.

Each family's gain is a slow LFO (ADR sources: pure functions of the clock, so seek-safe), with incommensurate
periods (97, 131 and 173 s) and the music on top:
- bass feeds the large family in the lead;
- the phrase (ADR-1420's pulse, ADR-1421 change) pushes the LFO phase on.

At any moment one or two families lead the large scale, so the topology changes over a phrase, not just the
amplitude. Proof `cw-C` shows the six-step journey A → compound → B → compound → hex → arcs. The medium scale
listens to a different family than the large scale, at under a fifth of its relief. Proof `cw-C`'s first pass, with
medium mixed into the same sand, buried the large figure.

**Composition and camera.**

| | Approach | For | Against | Verdict |
|---|---|---|---|---|
| C1 | Higher and further, near top-down | The whole figure reads | A map, with no depth or horizon; §5.6 "arbitrary top-down" | Rejected |
| C2 | **Oblique about 30–35° down, the plate in the lower two thirds, the terrain and sky gradient in the top third** | Overall figure, medium structure and horizon all read; depth from the basin's rim and the ranges already in the WorldMap; the brief's "slightly elevated oblique" | The far plate cells compress, and the bake grid must cover further | **Chosen, tested in stage B** |
| C3 | Low grazing, horizon at the middle | Dramatic | The nodal pattern is unreadable at grazing | Rejected |

**Environment.** The resonating plate is the basin floor inside a real landscape:
- the terraces, rim and mountain rings are already in the WorldMap but out of frame today;
- a sky gradient bound to the palette;
- palette-coloured height fog for aerial perspective, so the far plate and walls dissolve into a coloured air;
- the resonance continues faintly onto the terraces (the bake grid's edge fades rather than clamps).

**Rejected:** a flat backdrop (§14), and a second resonance field for the background (the 16-field cap).

**Material and light.** The ground stays unlit with its light painted (the lit path cost about 7 ms in the original
build). What changes:
- a wrap term and a hemisphere colour from the palette;
- roughness-like sheen only on the ridges' lips;
- nodal lines darker and differently hued (§5.7), rather than bright outlines;
- fog-coloured shadows.

**Rejected:** switching to the lit styled path (cost) and a bloom crutch.

### 1.2 Resonant Silk

**Geometry and composition (the deciding axis).**

| | Approach | For | Against | Verdict |
|---|---|---|---|---|
| S1 | One sheet, bigger | Cheap | §6.8 "more than one or two dominant sheets": fails | Rejected |
| S2 | **Layered: the gather (middle ground) + 2–3 far curtains seen through the negative-space wedges + 1 soft foreground fold crossing a frame corner** | Real foreground, middle ground and background; depth through the wedges (proof `silk-A`, top); the far layers are cheap (about 2k vertices each, drift only, no music deformers) | Placement is delicate: the first proof's foreground missed the frame | **Chosen** |
| S3 | A confluence: 2 tributaries flow into the gather | Leading lines to the focal point | Proof `silk-A` (bottom): they read as stray ribbons and fill the deliberate wedges ("spaghetti", §6.1) | Rejected; perhaps one tributary in stage A if the frame wants it |
| S4 | Inside the folds (a tunnel) | Immersive | A funnel/vortex hero (the owner's standing rule) | Rejected |

**Environment.**
- A palette sky gradient with a coloured glow behind the gather (the "sun" of the sky model, as a soft
  back-light).
- `fogSky` and palette fog, so the far curtains are atmospheric.
- The far curtains share the near sheet's drift and colour field at a slower rate (§4.3).

**Material.**

| | Approach | For | Against | Verdict |
|---|---|---|---|---|
| M1 | PBR plus **reflection bands** (ADR-1151): a coloured softbox and 1–2 strips seen only in reflections, replacing most of the white key's specular; a painted **sheen** (Fresnel × fold colour) | Satin's soft banded highlights; coloured, not white; no new engine code | A metallic lobe is still there; the band colours need palette bindings | **Chosen; proof in stage C** |
| M2 | The styled path (a hemisphere colour ambient, F0 0.04, no IBL) | Cheapest chromatic fill | No reflection bands on that path, so satin turns to matte plastic | Fallback for the far curtains only |

### 1.3 Waveform Dimension

**Geometry (the deciding axis).**

| | Approach | For | Against | Verdict |
|---|---|---|---|---|
| W1 | **Keep the nested level-set sculpture, and make travelling fronts its primary event:** the bass spectrum history laid out as spherical swells from an off-frame source (ADR-1116 `spectrum` × `wave`), crossing the kick shells, through all four sheets, with visible crests where they reinforce | The identity stays; the waves visibly travel (§7.1); cheap (fields, not vertices) | The sheets' outer bound still ends somewhere | **Chosen** |
| W2 | Replace it with an open interfering wave landscape to the horizon | Expansive | Loses the scene's identity and reads like Cymatic World's ground (§10) | Rejected |
| W3 | Extend every sheet to 700 m | Continuity | Proof `wd-A`: the shells enclose the camera, the walls fill the frame, and it costs 624k vertices | Rejected |

**Environment.**

| | Approach | For | Against | Verdict |
|---|---|---|---|---|
| E1 | **A far dome painted by the scene's own wave fields at a lower frequency**, with palette fog | Cheap (one draw, a few ops); the background is the same wave logic at another scale (§7.4); animated | Without parallax it is a dome; it must stay dark and low-contrast | **Chosen** |
| E2 | One or two sparse far partial shells in an annulus beyond the camera (coarse, no music) | Real parallax | Vertex and pixel cost; clutter risk (W3) | **Proof in stage B; keep only if it adds depth** |
| E3 | Volumetric fog coloured by a wave field (`volumeColorField`) | Light in the air | The volume march costs several ms on a scene already at about 32 ms | Rejected unless measured cheap |

**Material.**
- PBR stays.
- **Crest/trough contrast from the wave phase.**
- **Roughness from the field** (glossier crests, matte troughs).
- Coloured reflection bands as in Silk, but cooler and narrower so the two scenes do not read alike.
- The glow only at constructive crossings.

## 2. Shared pieces (kept small, §11.3)

- **ADR-1470 `oklabRamp`**, already in place.
- **ADR-1471, the `plate` mode geometry:** Cymatic World needs it, and Waveform may use it for its fine ripples.
- **A tiny kit helper:** an environment block for "palette sky + palette fog", and bindings that bind the sky,
  fog and bands to palette roles (generator code, not engine code).
- **No new material framework.**

## 3. The staged build (brief §12) and what each stage must show to pass

| Stage | Cymatic World | Resonant Silk | Waveform Dimension | To pass |
|---|---|---|---|---|
| A geometry (neutral grey) | ADR-1471; four families, director LFOs; stills of 4+ configurations; Feline quiet and loud | The layered sheets placed; stills at 3 drift phases | Travelling swells and crossings; a 4-frame strip showing propagation | Distinct configurations / layers / visible propagation in grey; my own check against §13 "Geometry" |
| B composition and environment | The oblique camera; terrain, sky, fog; bake extent | Sky glow, fog, far-curtain placement | Dome, fog; E2 proof | No unintentional black; a clear focal point; FG, MG and BG legible |
| C material and colour | Painted wrap, nodal hue, lip sheen; palettes | Bands, sheen, specular-reduced test | Phase colour, roughness field, bands | Colour holds with specular reduced; 3 palettes each |
| D temporal | Family journey over Feline 0–200 s (thumbnails), a transient, quiet | Flow continuity | Fronts on kicks; swell on bass | No twitching; families change on the phrase scale |
| E polish and perf | Paired live profiles; trims | | | Auto at 30 fps, no regression below the quality pass's level |

Each stage gets a few stills at 960×540 into `overhaul/<scene>/stage-X/`. One short clip per scene comes only in
stage D.

## 4. Risks and fallbacks

| Risk | Fallback |
|---|---|
| ADR-1471 is not approved | G2 only: hexagonal and grid lattices plus arcs, with near-degenerate lattice pairs for asymmetry. A weaker Chladni read, documented |
| Plate figures tile like wallpaper | The source's `reach` envelope (a finite plate), two plate sources with different orientations and scales, and the arcs family on top |
| The bake grid can't cover the oblique view's far plate | A second, coarser grid on the freed field slot (the far terraces need none) or a fade |
| CW's field budget (16): four families mean more readers | Families are *sources* of one resonator (no new fields). The readers stay per scale |
| Silk's far curtains steal frame time | The layered curtains have no music deformers and few vertices. If they still cost too much, cut to two |
| E1's dome reads as a flat backdrop | E2 shells, or drop the dome's contrast to fog-only |
| Live performance | Profile per stage with paired runs; the machine is shared and noisy |

## 5. The request to the coordinator

- **Approve ADR-1471** (the plate mode geometry: shared `resonance.cpp` / `fields.wgsl`, additive, with GPU parity).
- Review the directions above before stage A starts.
