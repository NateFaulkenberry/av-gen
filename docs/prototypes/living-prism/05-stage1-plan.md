# The Living Prism 05: Stage 1, the visual language, the windows and the plan

- **Spec:** `04-scene-spec.md` §13 Stage 1. **Branch:** `scene/living-prism` from main `18b28e14`.
- **Scripts:**
  - `scripts/stem_windows.py` (the stems and the windows);
  - `scripts/boards.py` (the palette board and the reference sheet);
  - `examples/living-prism/palette.py` (the looks, OKLCH);
  - `examples/living-prism/layout.py` (the grotto).
- **Review media:** `~/Desktop/av-gen-review/82-living-prism/stage1/`:
  - `references.jpg`;
  - `palette.png`;
  - `windows.png`.

## 1. Visual direction (from the eight references)

**One sentence:** a small, dark, faceted grotto whose crystals are the only saturated things in it, gathered round a
black mirror pool and seen through a crusted rock arch, with depth that comes from blue haze in a back passage.

**What recurs across the references, and what the scene takes:**
- **Silhouette:**
  - hexagonal prisms with six-faced pointed terminations, in clusters of very different heights (the prism garden,
    the cave stream);
  - tabular blades and stepped, terraced blocks for contrast (the macro amethyst).
  - No two heroes share a silhouette:
    - the **spire** is a tall upright fan;
    - the **fan** is diagonal blades leaning over the water;
    - the **geode** is a round hollow with a lined cavity;
    - the **chandelier** hangs points-down from the roof;
    - the **ember** is a squat terraced block.
- **Facets:** flat-shaded planes, so each facet catches light as one plane. The rock uses the same planar language
  at a coarser scale (decimated, faceted), so stone and crystal read as one geology.
- **Values:**
  - the rock is near black with a trace of the look's hue;
  - crystal bodies are mid-value and saturated;
  - only tips, seams and cores are bright, and they desaturate as they brighten.
  - This comes from the painterly cave and the cave stream, and it is what keeps the frame from becoming a mass of
    glowing facets (spec §3.2).
- **Light:**
  - colour pools at the bases of formations (the painterly cave's magenta base glow);
  - one cool shaft from above;
  - a blue-lit passage at the back for depth (the cave stream, the painterly cave).
- **Water:** a dark mirror, not a coloured surface. Its colour is whatever it reflects (the arch over a mirror pool).
  The pool doubles the arch and the spire, so it carries the composition.
- **Focal structure:** a natural rock arch over the far shore, with small crystal on its underside, framing the spire
  (the arch over a mirror pool).

**What does not combine, and is left out:**
- the jungle foliage of the prism garden;
- the aurora, circuit traces and valley scale of the aurora valley;
- the green neon and space void of the neon cluster (its ring motif survives only as an idea for pool ripples);
- full-frame iridescence (the macro quartz). Iridescence is a material accent on tips and edges, not the look.

## 2. The stems, confirmed

`scripts/stem_windows.py build/prism/stage1` (this agrees with Rainbow World 14, which analysed the same files):

| | Drums | Bass | Synth | Mix |
|---|---|---|---|---|
| Format | 24-bit, 48 kHz, stereo | same | same | same |
| Length | 210.500 s | same | same | 210.5004 s (19 frames of trailing silence) |
| Peak / RMS dBFS | -0.22 / -15.2 | -1.55 / -15.3 | -2.13 / -17.7 | -0.10 / -11.1 |
| First / last sound | 0.24 / 182.9 s | 8.19 / 193.5 s | 20.1 / 209.8 s | 0.24 / 209.8 s |

**Sync:**
- The stems sum to the mix with a lag of **0 samples**, gain 1.0007 and a residual of **-40.5 dB**.
- They are sample-aligned, so every stem's `offset` is 0.

**Content** (detail in Rainbow World 14 §3):
- **Drums:** a four-on-the-floor house kit.
  - The clap enters at 28.0 s.
  - 16th hats replace the offbeat hats from 111.3 s.
  - The drums stop dead at 182.7 s.
- **Bass:** a plucky sub, entering at 8.19 s.
  - An F pedal until 39.9 s, then F-Ab-Bb-F one chord per bar, then the pedal again from 164.9 s.
- **Synth:** falling calls 20-45 s, then the drop at 47.85 s brings a pumped chord bed (it dips on every kick).
  - It is brightest at 151-171 s.
  - A riser follows at 183-190 s, then the synth plays alone and fades out by 210 s.

## 3. The performance windows (spec §12.2)

- **Unit:** 10-bar windows, ranked by a per-bar load proxy.
- **The proxy:** 0.45 x drum onsets per bar (each is an impact, a ripple and a glint), plus 0.2 x bass level, plus
  0.2 x synth level, plus 0.15 x synth centroid.
- **What it is for:** ranking candidates, not modelling cost. The Stage 6 profiling pass confirms the choice.

| Window | Bars | Time (s) | Drum onsets/bar | Bass dB | Synth dB | Synth centroid | Why |
|---|---|---|---|---|---|---|---|
| **Typical** | 34-43 | **65.70-85.53** | 11.6 | -14.6 | -18.0 | 1,161 Hz | Groove A: kick, clap, offbeat hats, the progression, the pumped bed. Load 0.671, the closest steady stretch to the groove median (0.695) |
| **Heaviest** | 63-72 | **123.22-143.05** | 16.1 | -15.2 | -16.4 | 1,407 Hz | Groove B: 16th hats + clap + kick (densest onsets), the synth at its loudest groove level. Load 0.741, the maximum |
| Heaviest, candidate 2 | 77-86 | 150.99-170.82 | 16 | -15 | -17 | 2,400-2,750 Hz | The same density, with the synth at its brightest. The more expensive of the two if the synth drives material response hardest. Stage 6 profiles both and keeps the worse |

**Camera state:**
- The route loops every 95.21 s (48 bars), so a window's camera state is its time mod 95.21.
- Typical is route 65.7-85.5 s: right bank to high right.
- Heaviest is route 28.0-47.8 s: left bank to the arch's left leg, the spire three-quarter.
- The windows are reproducible: a live project has no audio file, and the camera is a pure function of time.

**Functional tests** (not performance windows):
- **Quiet:**
  - 0.24-8.18 s (drums alone);
  - 194.6-210.5 s (synth alone, fading).
- **Abrupt transitions:**
  - 47.85 s (the drop);
  - 182.7 s (the drums stop dead).
- **Missing source:** the drum stem after 182.7 s is a natural silent source.

## 4. Palette proposal (refining spec §6.1)

The source is `examples/living-prism/palette.py`, with roles in OKLCH. The board is `stage1/palette.png`, where swatch
width is the share of the frame each role is meant to hold.

**Every look keeps the same hierarchy:**
- **Shadow rock** (about 30% of the frame) has L < 0.15 and a trace of the look's hue.
- **The dominant crystal family:** the spire and the arch's crust.
- **The secondary family:** the fan, the chandelier and the druses.
- **A tertiary family:** the geode's lining.
- **One small accent:** the ember block and sparse seams.
- **Tips and cores** are the lightest and least saturated.

| Look | Dominant / secondary / tertiary | Accent | Character |
|---|---|---|---|
| Ultraviolet | violet / fuchsia / electric pink | coral-amber | The home look; the references' centre of gravity |
| Glacier | cobalt / electric blue / icy lavender | pale lavender (no warm) | Cold, high-key tips |
| Solar Mineral | magenta / coral / amber | molten gold | Warm. The rock warms to plum and the haze to ember |
| Opaline | rose / periwinkle / pearl | iridescent violet | Lower chroma, higher lightness. The soft look for quiet passages |
| Abyssal | deep blue / violet / restrained cyan | luminous cyan | The darkest; the bass-heavy look |
| Wild Spectrum | violet spire / cyan fan / magenta geode | gold ember, green seams | One family per region, placed deliberately. Not a rainbow filter |

**How the looks are used:**
- **Binding (Stage 3):** the roles bind to material-program `oklabRamp` constants, light colours and water tints
  through the palette system (ADR-1420 looks, OKLab transitions), as Rainbow World's do.
- **MIDI:** CC 9 selects a look and CC 10 locks it.
- **Automatic changes:** none on a timer. A change has a musical reason: section-level change signals, as Rainbow
  World's selector uses, with long holds.

## 5. The scene plan

| Stage | Content | Engine dependency |
|---|---|---|
| 2 (now) | Blockout: the shell, the floor and pool (terrain + ADR-1286 water), five heroes, the arch, druses, shards, boulders. Simple materials, blockout lights, the 95 s route | none |
| 3 | Programs for rock, dense crystal, translucent crystal, seams and pool; palette bindings; lighting | **refraction (P1-4)** for the translucent heroes; until then they are opaque with emissive cores |
| 4 | Impact, ripple, bass, synth, traces, MIDI | **`signal.*` (P0-3)**; **`EnvelopeSource` fix (P0-2)**; **register attribute (P1-1)** |
| 5 | Details | none |
| 6 | Windows profiled, ladder, latency, fallbacks | **honest profiler (P0-1)**; **sound-to-light (P1-3)**; **perf baseline (P1-7)**; upscaler (P1-6); soft takeover (P1-12) |

**Musical drivers, written now so each maps one-to-one onto a future `signal.*`:**

| Driver | Stems path (primary) | Mix fallback (`firstActive`) | Chain |
|---|---|---|---|
| `impact` | `stem.drums.onset` | `response.onset` | none: an event, on its own frame |
| `impactRegister` | the onset's register (P1-1) | the same, from the mix | none |
| `bassBody` | `stem.bass.level` | `response.bass` | attack 10 ms, release 450 ms |
| `synthLight` | `stem.synth.level` | `response.sustain` | smoothing at least 400 ms, to remove the sidechain pump |
| `synthBright` | `stem.synth.centroid` | `audio.centroid` | smoothing about 2 s, about one bar |
| `energy` | a weighted sum of the three levels | `response.level` | trend over about 8 s |
| `section` | `energy` accumulated, with decay | the same | integrate, leak about 20 s |

Until P0-3 lands, these are authored as routes generated from one Python table: each driver is one entry, so
porting a driver is one line. None uses the mix kick, snare or hat classes (the owner's option A).

## 6. Engine features this scene waits on

- **P0-1, honest instruments:** needed before any per-effect cost is reported. The Stage 2 perf read is labelled
  provisional.
- **P0-2, `EnvelopeSource`:** used by traces and impacts.
- **P0-3, `signal.*`:** used by every driver in §5.
- **P1-1, onset register:** distinct impact shapes per register.
- **P1-4, refraction on procedural materials, with a ladder lever:** the translucent heroes (the spire, the fan's
  blades).
- **P1-3, sound-to-light measurement:** the §7.5 latency report.
- **P1-7, committed perf baseline:** the comparison §12.3 asks for.
- **P1-6, spatial upscaler:** helps the lower rungs.
- **P1-12, MIDI soft takeover:** the performer controls.

**Found in Stage 2:**
- The cinematic water's SSR (ADR-1286) has **no quality-ladder lever**. `LiveQualityRung` has nothing for water.
- If the pool's SSR proves costly at 1080p, it needs one. It is the same lever P1-5 will need for material SSR.
- Evidence and numbers are in the Stage 2 report.
