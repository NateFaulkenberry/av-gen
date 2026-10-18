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

**Choice: B, vertical blades** (0.68 × 1.5 m on a 0.75 × 1.6 m pitch, about 8,100 in the hall). P2 showed the two
nearly alike in a still, so the deciding argument is the brief's: blades read as an architectural louvre screen;
squares read as a pixel display, which is the look the review called flat.

**The proofs changed the material** (P5, P6):
- **The face carries a tone from the phase.** A colour effector writes saturate(sin θ) per flap, and the program
  mixes `trough` to `metal`. Without it, a wall the key does not rake is black, and the islands are illegible
  (P5, top row).
- **The metal is soft** (roughness about 0.45, with the reflection bands 0.12 wide). Sharp metal under bands strobes.

The physical reading is a two-tone flap (a Rozin tile lit from above, or a BREAKFAST disc): the field shows as value
and the bands add the moving light.

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

**Setup.** Generator `tools/phase_wall/proof.py` (throwaway). Renders went through the GPU lock at 960×540 (one at
1080p), autonomous, on the R&D's 96 s coupling breath. Stills are in
`~/Desktop/av-gen-review/66-phase-wall-scene/00-proofs/`.

**The flicker metric.** The mean absolute grey-level change between consecutive frames, at 960 px wide:
- the R&D A3 clip the owner liked: p50 5.4, p95 7.4;
- the gate below is set against it.

| proof | question | result |
|---|---|---|
| **P1** hall, axis and corner (`P1-*`) | Does one wrapped network across four walls hold together, and does the hall fill the frame? | **Yes.** No void from any of the five camera positions tried (axis, oblique, frontal, corner looking up, low looking up). Islands cross the corners: the reader frames are numerically exact. Faults: the floor's lower 40% was a flat dark plane with a hard key hotspot, fixed in round 2 by brass inlay lines and a rougher stone (P4 still). |
| **P2** square against blade (`P2-*`) | Which flap? | Nearly alike in a still. Blades read as a louvre facade; squares as a pixel display. **Blades.** |
| **P3** unison (`P3-*`) | Is unison dead? | Unpainted, unison reads as **brickwork**: every flap at one angle and one value. Two fixes, both proven in P6. (1) Local coupling never reaches perfect unison (frozen vortices survive at K = 14; the order stalls near 0.7, ADR-1450), so islands persist. (2) The periodic tilt plus the reflection bands make whole bays brighten and darken in turn: the slow wave rolls round the room (`P6-chosen-unison-4s-sheet.png`). |
| **P4** aliasing at 1080p (`P4-*`) | Does the grid moiré? | At 1 m pitch, with the lit backplane behind the gaps, the axis view shows **no moiré** down to the end wall at 128 m (flaps about 9 px there). The far side walls at the grazing angle compress, but the gaps are low contrast because the backplane is lit. The real problem was **temporal**: see P5. |
| **P5** flicker (`P5-*`) | Does the per-flap shading strobe? | Sharp metal (roughness 0.2) under narrow bands strobes **badly**: p50 14.6-30.3, p95 up to 65. A flap passes through a 0.04 rad highlight in one frame. Without bands it does not flicker (p50 0.8), but the walls the key does not reach go **black**. Roughness 0.5 with 0.15-wide bands: p50 5.1, at the R&D level, but the field turns illegible (a uniform sheen). |
| **P6** chosen (`P6-*`) | Painted face plus soft bands | Roughness 0.45, bands 0.12 wide, the phase tone on the face, swing ±1.1 rad. **Islands legible; flicker p50 2.9 / p95 4.2 (oblique, islands) and p50 5.4 / p95 10.9 (frontal, unison at K = 14).** |
| **P7** live (`avgen --live-profile --mode live --audio assets/audio/feline-footwear.wav`, 1080p Auto) | Budget | The P6 hall (9,744 flaps, four walls, 293 × 25 network, shadows) at **full 1920×1080: GPU median 13.4 ms, p95 15.0, 0 misses.** That leaves about 15 ms for the environment pass at 30 fps. |

**Also found:**
- **At a 1.2 m cell** (the network one cell per flap, so islands span 8-15 flaps instead of 3-5) the defect glow
  vanished. The strain per cell is lower on a coarser lattice, so the activity thresholds (0.8-3.0) must come down.
  This is Stage 1's first tuning job.
- **The post `atmospheric` term at 0.5 with a light tint washes the hall grey** (P1 round 2, not kept). The haze
  must be dark and in the palette: about 0.1-0.15 with a tint near `environment`.
- **The key cannot rake all four walls** of a closed hall, by geometry. So the bands and the painted face carry the
  walls, and the key is for the floor, the piers and the shadows.

## 8. The plan: stages, gates and fallbacks

**Where it goes:**
- the scene: `examples/experiments/phase-wall/` (one project and its scene);
- the generator: `tools/phase_wall/build.py`, which supersedes `proof.py` (deleted at the end);
- the index category: "Experiments" unless the coordinator takes "Generative" (§6).

**Engine:** **no engine change planned**; the hall needs none. If a stage needs one, I stop and ask first. ADRs
1460-1469 are reserved for anything that arises; for example, a field-space warp for curved walls is out of scope
and only noted.

**Media:** `66-phase-wall-scene/<stage>/`.

### Stage 1: foundation (autonomous)

**Build:**
- `build.py`;
- the hall at 128 × 48 × 30 m with blades at about 8k;
- the wrapped network (cell 1.2 m) and the four reader pairs;
- the painted face plus the soft bands;
- the lit backplane;
- the corner and bay piers;
- the defect glow re-tuned to the coarser lattice.

**Gate:**
- stills from 4 cameras × 3 states (disorder t 8, islands t 28, unison t 50);
- defects glow in the islands state;
- no void;
- flicker p50 ≤ 6 and p95 ≤ 12 on a 4 s clip of every state;
- corners continuous.

**Fallbacks:**
- glow unreadable at 1.2 m → a 0.9 m cell;
- islands too blocky → a 0.9 m pitch.

### Stage 2: environment and light (the art pass)

**Build:**
- the coffered ceiling with a soft clerestory glow;
- polished stone with brass inlay;
- pier strips in the `trim` role;
- the vertical bands justified by the strips;
- a dark tinted haze (post atmospheric, no volumetric march);
- a key that rakes the floor and the piers;
- per-flap hinge play and value jitter.

**Gate: the "no dead space" sweep.** A 360° yaw at 3 pitches from 5 positions, as a contact sheet of 45 frames.
- No frame has a region of 5% or more of the frame that is near-black (below 2% luminance) and featureless (local
  standard deviation under 1%).
- The darks carry hue.
- 1080p stills at the grazing angle show no moiré.

**Fallbacks:**
- the ceiling reads empty → a second, finer coffer level or light slots;
- the floor reads empty → a central reflecting runner (glossier stone) on the axis.

### Stage 3: camera

**Build:** the promenade: a loop of about 120 s (§4), with flaps at 8 px or more everywhere on it.

**Gate:**
- a 30 s clip;
- the flicker gate along the path;
- no frame without the field in view.

**Fallback:** if the frontal pass strobes, keep it further out.

### Stage 4: audio and MIDI

**Build:**
- the routes of §5;
- the kick front from the end wall;
- the snare front from the east wall;
- the MIDI map, with note 36 as the pacemaker;
- Audio Influence;
- `sonic.live`, `response.*` (ADR-1361);
- no audio file;
- no `live.targetFps` (ADR-1360).

**Gate:** clips of Feline Footwear 40-60 s, Trench 60-80 s and Hyperborea 20-40 s against autonomous, from the same
camera:
- the kick fronts visibly cross a corner;
- sustain visibly unifies the room;
- the field-slot count stays at 16 or fewer.

**Fallback:** if the slots run out, drop the snare's compound (route it as a second injection) or the pacemaker.

### Stage 5: palettes

**Build:**
- the 8 role-based states (§6) with the ADR-1420 selector and the ADR-1421 change fallback;
- CC 9 select and CC 10 lock;
- the journey order;
- every colour constant bound to a role (none hard-coded).

**Gate:**
- a palette contact sheet (8 states × 2 cameras);
- each state reads in the darks (no neutral black).

### Stage 6: performance, integration and tests

**Build:**
- the index entry (live);
- the project-integrity pass.

**Gate:** live `avgen --live-profile --mode live --audio assets/audio/feline-footwear.wav` holds 30 fps at 1080p on
Auto, with 0-2 misses in about 150 frames, on Feline Footwear (plus a check on Trench and Hyperborea).

**Fallbacks:**
- the shadow atlas at 1k;
- the pitch at 0.9 → 1.0;
- the coffers simplified.

### Stage 7: final

**Build:**
- the final clip (about 30 s, Feline Footwear) and stills;
- `03-report.md`;
- `proof.py` removed.

**Checks, once and all exit 0:**
- `tools/ci/run-suite.sh cpu-assets`;
- `gpu`;
- `tools/review_examples.py --only "Phase Wall"`;
- `tools/check_project_integrity.py`.

**Name:** "Phase Wall" is the R&D's example (`osc-flaps`). The new scene **replaces** it in the index, rather than
adding a second "Phase Wall". The R&D's `osc-flaps` entry goes hidden as a harness, unless the coordinator prefers it
removed. Coordinate at merge, since `proto/generative-rd` owns that entry.

## Status

Stages 0-7 complete (2026-10-18). The outcome and its weaknesses are in `03-report.md`. Departures from this plan, made during the build:
- **cell:** 1.6 m with radius 3, spread 0.17 and tilt 0.006, after the coordinator's "mottled" note;
- **bevel:** replaced by a program edge highlight, for performance;
- **floor:** unlit, carrying the walls' colour;
- **index name:** "Phase Hall".
