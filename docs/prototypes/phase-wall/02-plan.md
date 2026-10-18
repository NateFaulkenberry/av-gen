# Phase Wall: debate, proofs and plan

2026-10-17, branch `proto/phase-wall`. Research: `01-research.md`. Proof stills: `~/Desktop/av-gen-review/66-phase-wall-scene/00-proofs/`.

The owner's brief, in one line: the A3 flaps as a full live scene, with **no dead space**. The world fills every
frame from every camera position, in every state, with designed depth and colour in the darks.

## 0. Two engine facts that shape every option

1. **16 GPU field slots per scene** (`spatial::kMaxGpuFields`). Each grid reader is a slot, and so is each onset
   front, each pacemaker and each compound. A wall that shows the network needs two (sin θ for the swing, and
   activity for the defect light).
2. **A reader's frame is affine.** A planar network can be read by any number of *flat* walls, each through its own
   frame, but it cannot be wrapped onto a curved surface without a new engine feature (a field-space warp). A wall
   that curves in plan *can* read it by projection, with a stretch of 1/cos of the wall's slope. That is acceptable
   to about ±45°.

**What the proof generator verified** (`tools/phase_wall/proof.py`). Four flat walls of a closed hall each read
**their own stretch of one wrapped network**. Each reader's frame takes the wall's unrolled coordinate u to the
grid's x (`rotation = (-90, ψ, 0)`, `position = start − d (u0 − P/2) + (0, H/2, 0)`). The mapping was checked
numerically at every corner: it is exact. The waves cross the corners and circle the room. Four walls use 8 slots.

## 1. World and composition (what the wall is part of; how the frame is filled)

| option | for | against |
|---|---|---|
| **A. The Hall.** A closed rectangular hall about 128 × 48 × 30 m. All four walls are flaps reading one wrapped network, with piers at the corners and bays, a coffered ceiling, a polished floor and tinted haze. | Fills every frame from **every** camera position inside it, by construction. The field is continuous round the room: a kick front launched on the end wall runs down both side walls. It reads as an installation the size of a turbine hall (LAb[au]'s ring, UVA's Curve). 8 field slots. | A box can feel static. Bays, piers and the haze have to give scale and depth. The ceiling and floor must be designed, not left. |
| B. The Canyon. Two facing flap walls with sky above and water between. | Parallax and a vanishing point; outdoor scale. | Both open ends and the sky slot are exactly the dead space the owner banned. Closing them turns it into A. |
| C. The Wings. Parallel screens staggered in depth like theatre flats, all reading one reader by projection (3 slots). | Very cheap. Layered depth: one field seen through many screens. | It works from the front only. From any side the screens go edge-on and the gaps show what is behind them, so it fails "every camera position". It reads as a stage set. |
| D. The Arc. One wall curved in plan round the viewer (±60°), over a reflecting lake, with distant walls in the same system. | A single reader. A sweeping, cinematic image. | Projection stretch of 2× at the arc's ends; a sky above and the lake's far edge to fill; no planar reflection, so the lake cannot mirror the wall. |
| E. A city facade among other flap facades. | The "kinetic facade" made literal. | It is an asset job, and it leaves a sky. The frame is filled by buildings, not by the work. |

**Choice: A, the Hall.** It is the only option that satisfies "every frame, every camera position, every state" by
construction rather than by framing. It also turns the R&D's weakness (a flat rectangle) into the structure: the
rectangle becomes the room. B, D and E all leave a sky or an edge to manage. C fails from the side.

**Composition rules inside the hall:**
- **Bays.** The 128 m walls are divided into bays by piers every ~16 m. The piers are proud of the flaps, dark
  stone, with a narrow vertical light strip in the palette's trim colour. They give scale (a pier is a known size), a
  rhythm that recedes in perspective, and a physical reason for the reflection bands (the flaps reflect the opposite
  piers' strips).
- **The darks have hue.** The gaps between the flaps show a luminous backplane in the palette's `back` role, and the
  haze carries the `environment` role. The far wall sinks into tinted air, never into black.
- **The ceiling** is a coffered grid catching a little of the haze and the key. **The floor** is polished dark stone:
  the bands and the key give it a sheen (there is no planar reflection; see the research §5).
- **The proportions** (to be settled by proof P1): long enough for a deep one-point view down the axis, and high
  enough that a level camera sees wall above the horizon line.

## 2. Flap geometry and material

| option | for | against |
|---|---|---|
| A. Square plates (R&D) | Reads as a pixel or flip-display grid; the most flaps per area | Reads as a display, not architecture |
| B. Tall vertical blades (1:1.6), hinged on the long axis | Reads as an architectural louvre facade. A long blade shows its angle clearly. Fewer, larger units on screen | Fewer cells of the field per metre, so the islands read coarser |
| C. Two-faced flip-discs (gold and black) | The strongest value change per degree | Discs need a second proc layer per wall, and the program has no front/back input. The backplane gives the same reveal more cheaply |
| D. Diamonds (Kahn scales) | Breaks the axis-aligned grid (less moiré) | A rotated-square hinge reads as a diamond spinning about a vertical axis: a fussy silhouette |

**Material (all options):** metal (metallic 0.85, roughness about 0.3, with the `roughnessFilter` op), albedo from
the palette's `metal` role, a hashed per-flap value jitter (±10%) and hinge play (±3°). Light comes from the key and
the **vertical** reflection bands. Emission is strain only (the defects and fronts), via the instance emissive.

**Choice:** decided by proof P2 (square against blade, same frame). See §8.

## 3. Environment and light

| option | for | against |
|---|---|---|
| **A. Enclosed hall:** coffered ceiling, polished floor, tinted haze; light from a raking key (a clerestory), reflection bands and a lit backplane | Every pixel is designed and in the palette. Bands make the flaps' swing visible as moving light (Kahn) | Needs care so the haze does not flatten the walls |
| B. An open courtyard under a twilight sky | Outdoor, with sky colour | The sky is a large flat area and changes nothing with the field; it is what the owner called dead space |
| C. A hall flooded with black water (the water renderer) | Doubling, mystery | No planar reflection: the water would mirror the sky cube, not the walls. Cost |
| D. Darkness and fog only (the R&D) | Cheap | This is the failure being fixed |

**Choice: A.** The light design:
- **Key:** a directional from high and to one side, with shadows, so a flap's angle reads as value (Rozin).
- **Vertical reflection bands:** two or three great circles about horizontal axes, in direction space. They are the
  flash. A flap turning about y sweeps its reflection horizontally across them. In unison the whole room flashes
  in a wave that rolls round the hall.
- **The backplane:** emissive in the palette's `back` role, revealed as the flaps turn edge-on (the louvre, as in
  Nouvel's diaphragms).
- **The haze:** the environment fog colour, the palette's `environment` role.

No volumetric march: it cost 7 ms in the R&D. Proof P4 settles whether analytic fog is enough.

## 4. Camera

| option | for | against |
|---|---|---|
| **A. The promenade.** A slow walk down the nave and back at 5-9 m height (about 120 s a loop), turning toward a side wall at mid-hall and to the end wall at the far end | A visitor's path (UVA's Momentum). Varied framings (axis, oblique, frontal) and parallax from the piers | Must never come so close that the flaps fill the frame at the grazing angle (aliasing) |
| B. Fixed framings with a slow drift, cut on phrases | The Resonator scenes' grammar | A cut in a live scene with a stateful network shows nothing new about the field; the drift camera alone is static |
| C. A pivot in the centre (a panoramic sweep) | Shows the room's continuity | Constant yaw reads as a turntable, and looking up while turning drifts toward the owner's "vortex" rule |
| D. Truck along one wall, close | Detail of the flaps | Grazing-angle moiré; the rest of the room is never seen |

**Choice: A**, with D's detail as one beat of the loop: a near-frontal pass at the east wall's middle bay, framed so
each flap is at least ~10 px. The loop is deliberately not 96 s, so the camera and the coupling breath drift against
each other.

## 5. Audio and MIDI

The A-oscillators mapping is the start, re-pointed at a closed room. **The music writes into the network, not into
the image.**

| input | target | why |
|---|---|---|
| response.sustain | coupling K +6 (3 s attack, 6 s release) | Held sound pulls the room into unison; a sparse passage loosens it into islands |
| low onset (kick) | a phase-advance ring launched from the end wall's centre, in the unrolled strip | The front runs round both corners and down the side walls. The coupling carries it |
| mid onset (snare) | a thinner, faster ring from the middle of the east wall | A second source, distinguishable by place and speed |
| response.intensity | lag α +0.35 (6 s attack, 12 s release) | A build reorganises the field, rings toward spirals |
| response.bass | natural frequency +0.25 Hz | The room swings faster with the low end |
| response.hat | phase noise | Fine disorder from the top |
| response.energy | the reflection rig's `rotation`, slowly (bands sweep the room) | Even a locked room's flashes travel when the music is loud |

**MIDI** (CC 1-8 on the R&D pattern; 9 and 10 as on the Resonators):

| control | target |
|---|---|
| CC 1 | coupling |
| CC 2 | complexity (spread) |
| CC 3 | motion (swing and speed) |
| CC 4 | audio influence (0.5 at rest) |
| CC 5 | lag (rings toward spirals) |
| CC 6 | light (bands gain and backplane) |
| CC 9 | palette select |
| CC 10 | palette lock |
| Note 36 (held) | a pacemaker on the end wall: a source of target waves while held |

**Field budget:**

| use | slots |
|---|---|
| 4 walls × (sin θ, activity) | 8 |
| tilt | 1 |
| kick + snare + their compound | 3 |
| pacemaker + the frequency compound | 2 |
| **total** | **14 of 16** |

## 6. Palettes

The project palette (ADR-1043) uses the ADR-1420 selector and the ADR-1421 energy-change fallback, as in Cymatic
World and Resonant Silk.

**Roles:**
- `environment`: haze, ceiling and background;
- `back`: the backplane, the colour in the darks;
- `metal`: the flap albedo;
- `key`: the key light;
- `front`: the strain glow;
- `peak`: the hottest defects;
- `trim`: the pier strips and the bands' warmth.

**Scalars:** roughness, key intensity, back gain and emission gain.

**Eight states** (sRGB references set at build time):

| name | metal | back | front / peak |
|---|---|---|---|
| Bronze Tide (the R&D look) | bronze | teal | cyan / amber |
| Nocturne | black lacquer | violet | violet / amber |
| Verdigris | patinated copper | sea green | mint / coral |
| Ember | dark steel | deep red | orange / white-gold |
| Cobalt | brushed steel | cobalt | ice / white |
| Gilt (BREAKFAST gold) | gold | ink indigo | magenta / pale pink |
| Nacre | pale nacre | blue-grey | soft gold, low emission |
| Ash (monochrome) | graphite | warm grey | white |

The journey (advance order) goes by temperature, so neighbours share a key.

**Index category:** "Experiments" for now. Proposed: a new category, **"Generative"**, for scenes whose image is a
running system (Phase Wall, and the R&D agent's reaction-diffusion scene). The coordinator's call at merge.

## 7. Proof renders (what paper could not settle)

(Filled in below as each proof lands.)

## 8. The plan: stages, gates and fallbacks

(Written after the proofs.)
