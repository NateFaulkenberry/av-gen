# Generative pass: audit, debate and plan

- **Brief:** [00-brief.md](00-brief.md) (the owner's, verbatim).
- **Branch:** `art/generative-pass` (from main `a55b49f9`). Media: `~/Desktop/av-gen-review/70-generative-pass/`
  (`baseline/`, `proofs/`, then a folder per stage). ADRs: 1500-1509.
- **Scenes:** RD Pins (`tools/rd_pins/build.py` → `examples/rd-pins/`) and Phase Hall
  (`tools/phase_wall/build.py` → `examples/experiments/phase-wall/`).

## 1. Audit (brief Part 1)

### 1.1 How each scene is built

| | RD Pins | Phase Hall (before) |
|---|---|---|
| Geometry | 190 × 190 = 36,100 box pins (0.46 m pitch) on a bowl (y = 18 (r/44)^2.4), lifted by a baked bowl-height grid (effector 1) and scaled in y by the RD's B (effector 2) | 8,064 box blades on four walls of a 128 × 48 × 30 m hall, each swung ±1 rad about +Y by its oscillator; piers, bays, plinths, coffered ceiling, lacquer floor, backplanes |
| Simulation | Gray-Scott plane 320² (ADR-1451), 12 iterations a sub-step, regime map baked 128² from four fields (ADR-1407), pre-rolled (ADR-1453) | Kuramoto-Sakaguchi strip 220 × 19 (ADR-1450), read by four pairs of linear readers |
| Material | One 17-op program: head colour by B, a single isoline ring at B = 0.2 (×2.4), shafts dark | Four 37-op blade programs (tone ramp of sin θ, shading in the blade frame, strain glow), a 41-op floor program |
| Light | A warm key and a cool rim, no shadows, sky ambient 0.28 | Four diffuse wall washes, a key, a bounce, four reflection bands (ADR-1151) |
| Post | Bloom 0.28 (default threshold), vignette 0.4, grain 0.01 | Bloom 0.28, vignette 0.3, atmospheric 0.12 |
| Fields (of 16) | 13 | 14 |

### 1.2 How audio and MIDI reach them

- **Signals.** Both use the response model (`response.*`, ADR-1361) through routes whose depth is Audio Influence
  (`visual.influence`, CC 4), and the audio history through Onset fields (kick and snare rings).
- **Measured on Feline Footwear** (`avgen --sonic-trace`, every frame, `build/gp/feline-trace.csv`):
  - `response.bass`, `response.level` and `response.sustain` sit at 0.94-0.99 from 10 s to 185 s. Every route from
    them is a constant offset on this track: it never moves. **This is the main reason the scenes "progress through
    colours" without responding**: the routes that were meant to carry the bass are pinned.
  - The hit envelopes (`response.kickEnv` 0.2-0.3, `snareEnv` 0.1-0.2, `hatEnv` 0.05-0.3) and the Onset fields do
    move with the music. So does the audio history's spectrogram (what a Spectrum field reads).
  - The track is dense from 10 s to 183 s; its quiet passages are the intro (0-5 s) and the outro (195-205 s). The
    drums stop at 184 s with the bass held to about 192 s: the one sustained-bass-without-drums passage. Trench's
    quietest passage is about 45 s (rms 0.07, no bass).
- **What actually moved before:**
  - RD Pins: kicks flip the regime toward spots (slowly: the RD takes seconds to respond); the snare pulses the brush;
    the bass route on pin height and the sustain route on the spot pool are pinned on Feline; hats multiply the ring
    by up to ×2.5 on every pin at once.
  - Phase Hall: kicks and snares are phase kicks in the network (fronts), the sustain route on the coupling is pinned
    (so Feline holds the hall near unison), hats add phase noise.
- **MIDI:** both bind CC 1-6, 9, 10 and notes through `control.midi.bindings` → `control.*` signals → routes. Nothing
  in either is exposed but dead (the route audit lists the MIDI routes as live-only, as it should).
- **Feature availability:** kick (`response.kick`, `audio.onsetLow`), snare (`response.snare`, `audio.onsetMid`), hats
  (`response.hat`, `audio.onsetHigh`), band levels (`audio.bass`..`audio.treble`, auto-gained; `audio.*Level`, not),
  the spectrogram history for Spectrum fields, and the onset history for Onset fields. No new analysis is needed.

### 1.3 What gives each its identity

- **RD Pins:** the labyrinth's coherent, nested, rounded contours drawn in light on dark; the pins (an LED-like
  texture); the bowl that fills the frame. Its weakness: one isoline at one brightness everywhere, in every frame;
  between passages the frames are indistinguishable (`baseline/rp-sheet.png`).
- **Phase Hall:** the blades, their swing and the islands of phase that cross them; the vertical columns; saturated
  colour. Its weakness: the room (`baseline/ph-sheet.png`), a mosaic read of the phase, and flat brightness.

### 1.4 Cost (baseline live profiles, 1080p, 30 fps target, Auto, Feline from 150 s, 20 s measured)

| Scene | Audio | Auto level | GPU p50 / p95 | Misses | Main costs (GPU, ms) |
|---|---|---|---|---|---|
| RD Pins | Feline | High (0.85) | 27.4 / 34.5 | 1 / 600 | scene 21.5, effectors + sims 2.9, depth prepass 2.3 |
| RD Pins | none | High (0.85) | 30.1 / 30.7 | 0 | scene 21.9, effectors + sims 5.5, depth 2.3 |
| Phase Hall | Feline | Ultra (1.00) | 26.6 / 27.7 | 0 | scene 24.4, depth 1.25 |
| Phase Hall | none | Ultra (1.00) | 18.0 / 18.8 | 0 | scene 16.6 (the promenade frames differ) |

`baseline/prof-*.txt` / `.json`. The scene pass (fragment programs over full-frame coverage) dominates both. MIDI was
not measured with a controller (none attached); the MIDI path is a handful of routes per frame on the CPU.

## 2. Shared audio design (brief Part 2)

Each band gets a job, a spatial scale and a time constant, and the pinned levels are kept only for slow, broad things:

| Band / feature | Signal used | RD Pins | Phase Hall | Envelope |
|---|---|---|---|---|
| Bass level | `response.bass` (pinned when loud: the presence of bass, not its shape) | the broad swell's height | the cores' warmth | 250-400 ms in, 1.2-2.5 s out |
| Bass shape | the spectrogram history (Spectrum field, 0-16% of the log axis, 0.15 s window) laid out in space | a lift and a pool of light rolling out from the heart at 14 m/s | rings of membrane displacement rolling out from low in front at 34 m/s | the music's own, smoothed |
| Kick | low onsets (Onset field: the newest eight, each a ring) | a ring of impact: lift, contour swell, a band of light, the regime toward spots | a ring of push through the membrane + a phase front through the network + a ring of light | ring decays 0.7-1.4 /s |
| Mids | `audio.mid` (auto-gained band) | the ripple's reach into the contours and its heave | (network frequency, kept) | 120 ms in, 700 ms out |
| Snare | mid onsets | a quicker ring of light from off-centre; a burst of growth | a quicker phase front | decays 1.3-1.6 /s |
| Hats | `response.hat` | beads of light the ripple runs along the contours | streaks of light running up the blades | 8 ms in, 260 ms out |
| Held sound | `response.sustain` | the spot pool widens | the membrane's broad swell, the coupling | 2.5-3 s in, 5-6 s out |
| Overall | Audio Influence (CC 4) | the depth of every audio route | the depth of every audio route | — |

Hysteresis: the onset detector's own thresholds (ADR-1067) and the response model's sensitivity floor decide what is a
hit; a held note never retriggers a ring. Geometry, light and fine detail each have their own envelope (above).

**Compact MIDI set** (existing bindings kept; three added in each scene):

| CC | RD Pins | Phase Hall |
|---|---|---|
| 1-5 | pattern, seeding, pin height, audio influence, erase (kept) | coupling, complexity, motion, audio influence, lag (kept) |
| 6 | LIGHT: the contours' core and halo | LIGHT: the cores and the reflected strips |
| 7 | WAVES: the broad swells' height | SWELL: how far the membrane moves |
| 8 | IMPACT: the kick and snare rings | IMPACT: the kick's push and network kick |
| 9 / 10 | palette select / lock (kept) | palette select / lock (kept) |
| 11 | SHIMMER: the beads of light | SHIMMER: the streaks |
| notes | 36-38 hold seeds (kept) | 36 holds a pacemaker (kept) |

## 3. RD Pins (brief Part 3)

### 3.1 Approaches debated

| | Approach | For | Against | Verdict |
|---|---|---|---|---|
| W1 | Add audio-driven B (inject rings into the RD) | Direct | The RD plan's Q4: rings of B made voids that took seconds to heal; the pattern stops being the pattern | Rejected |
| W2 | Displace the pins only (waves in geometry) | Cheap (deformers cost no field slot) | Box pins move without tilting: a heave reads, but the contours do not change spacing | Part of the answer |
| W3 | **Move the contour level itself**: draw the contour of S = B + waves, so a wave bunches and opens the contours as it passes, plus W2 for the heave | The topographic read the brief asks for (spacing, curvature, merging) without touching the simulation; the ring already is an isoline | The program must know the wave: a field (a slot) or the same sine recomputed in ops | **Chosen** |
| W4 | Many nested isolines (a topographic map of B) | Nested contours | B is nearly binary (it crosses 0→0.35 in about a metre): nested isolines of it crowd into one line | Rejected |
| L1 | Uniform brighter rings + bloom | Cheap | Exactly the brief's "every line equally bright" | Rejected |
| L2 | **A core and a halo with an intensity field**: a fine hot core (1 - d²)^12 in a highlight colour, a halo (1 - d²)² in the contour colour, × an intensity made of a floor, the interference of two wave systems, the kick's band of light, the snare's, and beads where the ripple's sharp crest crosses a contour | The brief's luminous hierarchy and travelling light, all from fields already there | 48 ops is the whole budget | **Chosen** |

### 3.2 The layers (all functions of the clock and the audio history: seek equals play)

| Layer | What you see | Mechanism |
|---|---|---|
| A broad | Two broad swells (38 m at 4.2 m/s; 27 m at 2.9 m/s, crossing) heave the field | two world-space `sine` deformers; the bass level is A's height (route), CC 7 both |
| A bass | The bass history rolls out from the heart: a lift and a pool of light | a Spectrum field (bass band, 14 m/s, 0.15 s window) as a lift effector and in the program |
| B ripples | A 9.5 m ripple at 3.6 m/s bunches and opens the contours (the mids set its reach), with a small heave | a third `sine` deformer, reproduced exactly in the program (`gradient` + `time` + `palette` = the same sine) |
| C impacts | A kick: a ring from the heart lifts the pins, swells the contours and carries a band of light, and turns the labyrinth toward spots; a snare: a quicker ring of light from off-centre | Onset fields (the newest eight onsets: bounded), in the regime bake, an effector and the program |
| D interference | Where the ripple's crest meets the bass pool, hot spots; where crests meet contours, beads | products of the wave terms in the program |
| E light | Beads along the contours sharpen on the hats; the halo brightens a little | the ripple's crest^10 × the core × a hat-gated constant |
| E depth | The bowl (kept), the heave's parallax, the shafts darkening as pins rise | geometry |

Budgets: fields 15 of 16 (the regime's own kick field is the impact ring; `kickRegime` is gone); pin program 48 of 48
ops, 4 fields (B, kick, swell, snare); effectors 5 of 8; deformers 3.

### 3.3 Palettes

Six coordinated sets (the brief's four families kept: Copper Night, Electric Lagoon, Violet and Gold, Verdigris Reef;
plus Garnet and Indigo, Rose Quartz). Each now has a dark foundation in its own hue (environment, shadow, base), a
dominant contour colour (`ring`, the halo), a highlight that stays in the family (`hot`, the core and beads), the
pattern metal as the accent, two lights, and its own bloom intensity (`bloomI`, bound to `post/bloom/intensity`). The
journey, the selector and CC 9 / 10 are unchanged.

### 3.4 Post

Bloom threshold 1.1 in exposed units with a 0.6 knee: only the cores and beads bloom; radius 0.75 so the glow leaves
a contour without filling the channel beside it. Vignette 0.4 → 0.22, grain off.

## 4. Phase Hall (brief Parts 4-5)

### 4.1 Approaches debated

| | Approach | For | Against | Verdict |
|---|---|---|---|---|
| S1 | The hall inside a sphere | Least work | "Not simply placing the existing room inside a sphere" | Rejected |
| S2 | A smooth sphere mesh with the bands painted by a shader (cos of azimuth) | One draw | Bands of azimuth converge on the poles; no blades, no swing, no reflections sweeping: the material identity goes | Rejected |
| S3 | **Instanced blades laid on the sphere** (a `points` distribution, ADR-1118), columns of constant azimuth, each blade oriented to its meridian and swung about its own hinge | Keeps the blades, the swing, the reflections and the phase; columns are the vertical bands; one draw | The engine turned every point about one object axis, and read grids through linear frames only | **Chosen**, with two small engine features (§6) |
| P1 | Columns to the poles | — | Meridians converge: rays to a point, the brief's and the coordinator's "tunnel toward a pole" | Rejected |
| P2 | Rings of constant pitch near the poles | No convergence | Concentric rings at the zenith read as a dome's oculus or a target | Rejected |
| P3 | Columns end in a ragged hem | Simple | Proof P1: reads as an opening to a cloudy sky (an oculus) | Rejected (proof) |
| P4 | Columns dissolve into the glowing membrane | No edge, no circle | Proofs P2-P3: the bare cap reads as a dark opening, and the meridians still converge on it (the coordinator's gate-1 note) | Rejected (proof) |
| P5 | Fibonacci-scattered chips fill the cap | No rings | Proof P4: the sunflower's spiral parastichies read as a vortex | Rejected (proof) |
| P6 | **The cap as its own pattern**: from a latitude that wanders round the sphere (45-53°) the columns thin out over 10-15°, and a hexagonal lattice of chips (laid by the Lambert equal-area projection about each pole) fills in ahead of them; every chip lies along one parallel field across the cap (world Z projected), so nothing is radial, and it lights like a blade, as bright as the columns | No convergence, no rings, no hole; the chips continue the columns where the performance camera looks | The lattice reads as a woven panel overhead | **Chosen; proofs P5-P6** |

Any tangent direction field on a sphere has a singular point (hairy ball); P4 puts no structure where it would show.

### 4.2 The sphere

- Radius 150 m; 288 columns (3.3 m pitch on the equator, blades 2.5 × 5.5 m, rows 5.6 m: the rows nearly touch so a
  column reads as one tall band); blades narrow with cos(latitude) so the gaps keep their proportion. About 13,000
  blades (8,064 before).
- A membrane 2.2 m behind them (an inside-facing 160 × 80 sphere, unlit): its own deep colour with slow pools of glow,
  the same displacement as the blades.
- The network: one strip once round the sphere (144 × 54 cells, wrapped), read through the sphere projection
  (ADR-1501): islands and fronts circle the viewer with no seam.
- The performance camera: a third of the radius off-centre and a little low, looking across at the far side a few
  degrees up (18 mm); it drifts a few metres over 90 s. The default frame is all columns, curving away on both
  sides; no floor, no corners, no vanishing point.

### 4.3 Motion layers

| Layer | What you see | Mechanism |
|---|---|---|
| Broad | The membrane undulates (a 260 m swell travelling at 7 m/s; the held sound deepens it) | Wave field → every blade along its own normal (ADR-1500), the membrane by a field deformer |
| Bass | Rings of the bass history roll out across the membrane from low in front | Spectrum field (34 m/s) in the same displacement |
| Kick | A ring of push from where the kick lands, a ring of light with it, a phase front through the network | Onset fields (membrane and network) |
| Panels | The swing (kept, about each blade's own hinge), islands and fronts | the network |
| Fine | Streaks of light running up the columns at their own phases, on the hats; reflection strips sweeping across turning blades | program; ADR-1151 bands |

### 4.4 Light hierarchy

1. Narrow hot cores down every blade ((4u(1-u))^14) and streaks; 2. the faces' saturated tone under a light at the
sphere's heart (a blade turned away falls dark); 3. a soft shoulder and the membrane's pools; 4. dark gaps between
columns and a deep membrane. The cores' brightness: a low floor, the crest of the blade's own phase squared (islands in
step glow), the displacement (the swell's crests bright, its troughs dark: zones that move), the kick's ring.

### 4.5 Palettes

The brief's six: Polar Glass, Glacial Teal, Acid Cathedral, Molten Signal, Sunset Interference, Ultraviolet, each a
role set (environment, back, trough, mid, crest, core, glint, glow, front, peak), journeying round the wheel by
neighbours. CC 9 selects across the six.

## 5. Stages and gates

1. Audit, baseline, this plan (gate 1).
2. RD Pins: the layers, light and palettes; stills, a filmstrip, one short clip (gate 2).
3. Phase Hall: the sphere, the room gone; stills from the performance camera, a look up, the poles (gate 3).
4. Phase Hall motion and light finished; final profiles of both (gate 4).
5. Clean reconfigure, the suites, review and integrity checks, the report (02).

## 6. Engine changes

- **ADR-1502, a grid re-bakes what it reads at each of its sub-steps** (found while proving RD Pins: a render
  started mid-track lost most of its pattern after the first frame, on main as well): a seek now replays exactly what
  play did.

- **ADR-1500, an effector's axis can be the point's own** (`effectors[].local`): a scalar PositionOffset along the
  point's own axis, a Rotation about it. Phase Hall's blades swing about their own hinges and move along their own
  normals. CPU and GPU, tests in `[spatial][effectors]` and `[gpu][points][effectors]`.
- **ADR-1501, a grid field can read a plane round a sphere** (`projection: "sphere"`): the network's strip wraps the
  sphere through one reader per channel. CPU and GPU, test in `[gpu][simulation][fields]`.

Nothing in the live path (presentation, pacing, the quality controller, the program compiler) is touched.
