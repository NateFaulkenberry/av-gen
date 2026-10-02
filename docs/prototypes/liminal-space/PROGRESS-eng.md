# Liminal Euclidean World: engineering progress (PROGRESS-eng)

*The engineering agent's running notes, kept current so a cold successor can resume. The art agent keeps
PROGRESS-art.md. Governing documents: `00-brief.md`, `01-addendum-emotion.md` (wins where they differ).
Design and research: `ENGINEERING.md`. Decisions: ADR-1040 to 1044.*

## ART PASS 3 (2026-10-01): Resume here

*Governing: `03-art-pass-3-addendum.md` (the owner's). My items, in the coordinator's order. ADRs 1051-1059.
Shared worktree: commit only my paths with `git commit -- <paths>`; never `tools/liminal/`, `examples/liminal/all-you-got*`,
PROGRESS-art.md. The art agent renders with a pinned binary at `373a34a7`.*

| # | item | status | ADR | tests |
|---|---|---|---|---|
| 1 | room / spatial validator (`avgen --validate-space`, `tools/liminal_space.py`); t0/t1-aware since `225e70b0` | **done** | 1051 | `[adr1051]` (12 cases) |
| 2 | SDF rim / fresnel emission (`look/rim/*`), replacing "fix the head" (coordinator) | **done** | 1052 | `[adr1052]` (CPU + GPU) |
| 3 | random entity jumping (§28): cause found, `transform-step` hazard, `avgen --trace-jumps` | **done** (the data fix is the art agent's) | 1053 | `[adr1053]` |
| 4 | screen static (`surface/<k>/static`, `look/static/*`) | **done** | 1054 | `[adr1054]` (CPU + GPU) |
| 5 | spatial colour wave (§30): `post/wave/*`, a world-space band shared by every SDF object, recolouring lines and emission (the `travelBeam` recipe remains as a light-only alternative) | **done** | 1055 | `[adr1055]` (CPU + GPU) |
| 6 | geometry tearing on events (§21) | **not built** (budget); a recipe with existing ops is below | - | - |

**Resume here:** items 1-5 are committed (the last is `5dd835a4`). Since `225e70b0` and `6878d2c9` the validator is t0/t1-aware, and lyric obstacles are side-of-wall and SDF-exact. What remains is the final hand-back: run both full suites under the lock, one
after the other, then restore `temporal-*.png`. Item 6 would be a new SDF domain op (`tear`: a band-wise offset
`p.x += amount * (hash(floor(p.y * bands + seed)) - 0.5)`), which touches `src/spatial/sdf.*`, `shaders/sdf.wgsl` and the
compiled path, in the pattern of ADR-1040's `warp`.

**Suites (pass 3, code at `5dd835a4`, run at `367d39c2`):**
- `avgen_tests`: exit 0. 3,991 cases: 3,971 passed, 19 skipped, 1 failed as expected (the slope lean).
- `avgen_render_tests`: exit 42. 567 cases: 565 passed, 1 skipped, 1 failed. The failure was "Showcase projects render
  bit-identically across fresh engines and renderers" on `examples/worlds/worlds.json`, at `test_procedural_examples_gpu.cpp:95`.
  - Rerun alone under the lock, it passes (152 assertions).
  - That scene has no SDF objects (4 procedural, 1 grid, 1 particles), and nothing in pass 3 touches its render path. So the
    failure is intermittent and is not traced to pass 3, but it is unexplained. A successor should rerun the full render suite,
    and if it recurs, diff the two images.
- `temporal-*.png` was checked: clean.

**The missing head (§33), found by the validator:** in `all-you-got-pass2.scene.json`, `hillMan` has boundsMax y = 2.5 and its
head is at y = 2.896, so the head is never marched. This is a data fix in the art agent's generator (grow the bounds). The
validator now reports it (`integrity`/`clipped`). The other mannequins' "black blob" is the crease-only line look on a rounded
head, which the art agent is fixing in the kit. It is not an engine defect.

**Real defects in the pass-2 film** (generated with the kit instrumented; `S/space/rep.json`, where
`S=/private/tmp/claude-501/-Users-natefaulkenberry-Documents-GitHub-av-gen/fed9412c-8e5e-42c0-a62b-e703644796ad/scratchpad`):
- `kit.armchair()` = `couch(seats=1)`. `repeat` gets count `seats // 2 if seats % 2 else 0` = 0, which means INFINITE: a row of
  seat cushions runs through livMedia's whole march box, through the wall, the cabinet and a window. The fix is `seats // 2` for
  odd seats (count 0 for one seat), or no repeat when seats == 1.
- The cabinet and the wardrobe start at y = 0.05-0.06, so they float. The counter does too.
- About 50 lyric placements overlap windows, curtains, paintings or furniture (see the report).
- Door_01 in v2b is blocked by a plant and a box. The chairs flipped onto tables (v2b, v2d) read as "on top of the table" plus a
  180-degree tilt. If that is intended, tag them `"tilted": true`.
- Pendants hang 0.3-0.8 m below the ceiling, so they read as "not attached". Either the cord is short, or the rule wants
  `mounts: ceiling` with a drop.

### Art agent's guide, pass 3

#### 1. The spatial validator (ADR-1051)

Run it on any scene or project (headless, no GPU, about 2 s for the whole film):

```sh
./build/release/src/avgen --validate-space examples/liminal/all-you-got-pass2.json            # section 14 text report
./build/release/src/avgen --validate-space <project.json> --json report.json --text report.txt  # both
#   --rules r.json (deep-merged over the defaults)  --margin 0.25 (lyric clearance, m)  --eye 1.6  --no-camera
#   --strict (exit 1 on errors)  --dump-rules (print every category, pose and tolerance)
```

Given a project, it reads the scene, the constant `camera/journey/height` (the eye height) and the `camera/journey/distance` keys
(each chapter's path is checked only over the span the film visits).

From Python (the generator):

```python
import sys; sys.path.insert(0, "tools"); sys.path.insert(0, "tools/liminal")
import kit, liminal_space as ls
ls.instrument_kit(kit)            # BEFORE importing rooms2 etc. or building anything: every kit prop tags itself
...build...
report = ls.validate(scene_dict)  # or a scene/project path (a project path also gives the camera span and eye height)
print(ls.text(report))
ls.apply_fixes(scene_dict, report)                    # the safe ones: floor, support, lyric, bounds (one translate per entity)
ls.apply_fixes(scene_dict, report, rules={"intersection", "orientation"})   # opt-in
```

- **Naming and relationships:** every instrumented kit call takes `entity={...}`, for example
  `K.chair(entity={"id": "DeskChair_01", "anchor": "Desk_01"})` and
  `K.mannequin("sit", name="livMan", entity={"pose": "thinker", "anchor": "LivingChair_01"})`.
- **Rooms:** `K.shell(ext)` is tagged `room` with its interior. Pass `entity={"id": "livingRoom"}` to name it, and
  `{"sealed": True}` for a room that needs no door.
- **Anything else:** `ls.tag(node, "toilet", id="Toilet_01")`. For a component, use `ls.part(node, "head")`.
- **Categories:** room, door, doorway, window, stairs, couch, armchair, chair, table, coffeeTable, desk, bed, nightstand,
  cabinet, wardrobe, shelf, counter, fridge, fireplace, sink, toilet, bathtub, lamp, tableLamp, hangingLamp, ceilingFan,
  painting, mirror, clock, wallDecoration, coatHooks, curtains, television, monitor, plant, rug, prop, hangingObject,
  mannequin, person, wallText, floorText, stairText, floatingText.
- **Poses:** stand, wait, sit, thinker, headInHands, elbowsOnTable, toilet, desk (all seated: they need an `anchor` that supports
  sitting); lie, couchLying, bedLying (they lie on the anchor's top); mirror, window (standing, facing the anchor). The mannequin's
  `hip` comes from `kit.POSES[pose]["hip_y"]`. Add new poses to the rules (`{"poses": {"kneel": {"support": "floor"}}}`).
- **Lyrics:** give a `liminal_text.place_words` entry `"room": "<room id>"` (and `"category": "floorText"` etc. when it is not
  wall text). The validator then checks it against that room's walls. It reports the overlap %, the margin, the orientation and a
  clear spot (`fix.position` + `fix.normal`). Words without a room are inferred: one lying on a wall plane is wall text, anything
  else floats.
- **Reading a violation:** `severity`, `rule`, `entities`, `message`, `measured`, `expected`, `suggestion`, `fix` and `groups` (the
  chapters in which it was seen).
- **Order (section 15):** validate, apply the safe fixes, render, critic, refine, validate again.
- **Time:** entities with `t0`/`t1` are checked only against entities whose spans overlap theirs. With a project path, the camera
  is checked only against what is present when it passes.

#### 2. Rim glow on SDF objects (ADR-1052)

- **Parameters:** `sdf/<o>/look/rim/intensity` (0 = off, the default; 1-4 is a visible contour), `look/rim/color` (bind it in
  the palette), and `look/rim/power` (3 by default; 6-10 is a thin line on the silhouette).
- **Per surface:** `sdf/<o>/surface/<k>/rim` (a multiplier, default 1). To light only the head, set 0 on the body's surfaces and
  1 on the head's.
- **JSON:** `"look": {"rimIntensity": 2, "rimColor": [0.6, 0.8, 1], "rimPower": 6}` and `"surfaces": [..., {"rim": 0}]`.
- It is fogged like the edges and blooms. Route the beat into `look/rim/intensity` for a head that pulses.

#### 3. Jumps (ADR-1053)

- A bob is `grid.song.quarter.wave` (or `.half.wave`), never the bare pulse. A pulse into a position needs
  `"chain": {"springHz": 6, "springDamping": 0.6}`.
- The `transform-step` hazard (a load warning, and in `--audit-routes`) names any route that breaks this.
- **Measure:** `./build/release/src/avgen --trace-jumps <project.json> --fps 30 [--range a:b] [--json f]` (about 40 s, no GPU).
- **Pass 2's offenders:** from 38 s, livArm, livPlant and livTable /translation (gRel and gB3). From 187 s, kitKettle,
  kitChairA, kitChairB and kitPlate /translation, and nodes/b3w83..90/position. Also the grid.song.c10 routes into the bed
  room's transform/position; that one may be deliberate.

#### 4. Screen static (ADR-1054)

- **Switch it on:** `sdf/<o>/surface/<k>/static` = 1 on the screen surface (the kit's `SCREEN`, k = 3). JSON:
  `"surfaces": [..., {"color": ..., "emission": ..., "static": 1}]`.
- **Object-wide:** `look/static/cell` (m, 0.012), `look/static/rate` (re-rolls per second, 24; 0 = frozen) and `look/static/roll`
  (the rolling bar, 0.35).
- It multiplies the surface's colour and emission. A screen needs emission (or a lit colour) for the snow to show.
- Route a clap channel into `static`, or into `rate`, for interference bursts.

#### 5. The spatial colour wave (§30): a `travelBeam` effect (existing, ADR-207/702)

- **What it is:** a directional wave front that adds coloured light to every surface it crosses, SDF walls included, with a
  rainbow ramp through the band.
- **Demo:** rendered at `~/Desktop/av-gen-review/24-liminal-space/pass3/eng/colour-wave-travelBeam-15.5-19s.png`, with its
  scene `colour-wave-demo.scene.json`. Add to the scene's `"effects"`:

```json
{"id": "colourWave", "type": "travelBeam", "name": "Colour wave", "owner": {"kind": "world"}, "enabled": true, "order": 0,
 "activation": "window", "timing": {"delay": 0, "lifetime": 0, "fadeIn": 0.2, "fadeOut": 1.0, "windowStart": 208.0, "windowSeconds": 4.0, "repeatSeconds": 0},
 "parameters": {"propagation": {"kind": "directional", "direction": "explicit", "explicitDirection": [0, 0, 1], "speed": 5.0, "range": 40.0,
     "frontWidth": 1.2, "trailLength": 8.0, "falloff": 1.2, "startOffset": 0.0, "verticalExtent": 20.0, "verticalGrowth": 0.0, "ringCount": 0, "beamRadius": 0},
   "appearance": {"color": [1, 0.3, 0.8], "intensity": 1.5, "edgeColor": [1, 1, 1], "edgeIntensity": 4.0, "width": 1.0, "rainbow": true,
     "rainbowSpeed": 0.2, "rainbowScale": 0.15, "rainbowSaturation": 0.9, "rainbowBrightness": 1.2},
   "sparkle": {"enabled": false}, "response": {"ground": 1, "foliage": 1, "surface": 1, "emissive": 1},
   "source": {"kind": "world", "position": [0, 0, -3]}}}
```

- **The settings:**
  - `source` with `explicitDirection` sets where the front starts and which way it travels (world metres, in the room's
    frame). `"source": {"kind": "camera"}` with `"direction": "cameraForward"` sends it away from the eye.
  - `speed` x `windowSeconds` sets how far it travels: 5 m/s crosses a 5 m room in a bar at 111 BPM.
  - `trailLength` sets how long surfaces stay coloured after it passes.
- **The palette:** the wave adds light; it does not change base colours. To make the room come out in a new palette, key
  `palette/position` across the same window so that it lands as the front leaves the room.

#### 5b. The world wave (ADR-1055): the one for bar 90

- **The front:** `post/wave/origin`, `direction`, `progress` (metres travelled; key it) and `width` (half-width, m).
- **The band's light and colour:** `intensity`, plus `color`, or `hueSpan` > 0 for a rainbow from `hue`.
- **Recolouring:** `edgeTint` recolours lines and emission toward the band's colour inside the band. `trail` and `trailColor`
  recolour behind the front.
- Shared by every SDF object. Off while intensity, edgeTint and trail are 0.
- **Demo:** `~/Desktop/av-gen-review/24-liminal-space/pass3/eng/world-wave-post-wave-15.5-19s.png`.

#### 6. More corruption, with what exists (no new engine code)

- **Geometry displacement on an event:** wrap a world or prop in a `warp` (ADR-1040) or a `displaceNoise` node and route the
  clap channel into its `amount` (for example `{"source": "grid.song.c05", "target": "sdf/<o>/node/<warp>/amount", "amount": 0.15}`).
  Relax `stepScale` to about 0.8.
- **Positional glitches:** a pulse with NO chain into a translation (the `transform-step` hazard, deliberately). Give it
  `attackMs: 1` to mark it intended.
- **Texture corruption:** route the channel into `surface/<k>/static` on any surface, not only the screens.
- **Frame corruption:** `temporal/mosh/*` (ADR-1049) on the same channel.


## ART PASS 2 (2026-10-01): Resume here

*The governing brief is now `02-art-pass-2.md` (the owner's; it wins). My job is its section 19 Phase 4: the
smallest reusable systems the art needs. The art agent works in the same worktree in parallel; I commit only my
own paths with `git commit -- <paths>`. ADRs 1045-1059.*

| # | system | status | ADR | tests |
|---|---|---|---|---|
| 1 | beat grid: owner-numbered bars, tempo map, pulses, authored event envelopes (BIG CLAPs, words) | **done** | 1045 | `[beatgrid]` (7 cases) |
| 2 | spatial lyric typography (mesh text + `tools/liminal_text.py`) | **done** | 1046 | `[text]` (3 cases) |
| 3 | luminous line-drawn edges (pixel width, shape, per-surface colour) | **done** | 1047 | `[adr1047]` (1 CPU, 1 GPU) |
| 4 | camera breathing | **done** | 1048 | `[breath]` (2 cases) |
| 5 | object animation on beat sources (routes from the grid; no new engine code) | **done** | 1045 | `[beatgrid]` "spin, bob and pulse an SDF prop" |
| 6 | simulation corruption: data mosh + channel shift (temporal `mosh`) | **done** | 1049 | `[adr1049]` (GPU) |
| 7 | spectrum-sweep transition (`post/sweep/*`) | **done** | 1050 | `[adr1050]` (CPU and GPU) |
| - | tint on moving figures (pass 1 gap 2) | **not reproducible at this head**: still, walking, keyed, journey-anchored and palette-bound figures all take the tint (renders checked); regression test added | 1044 | `[tint]` |

**Suites after the breath fix (`373a34a7`, 2026-10-01):** `avgen_tests` exit 0 (3,963 cases: 3,943 passed,
19 skipped, 1 failed as expected, the slope lean); `avgen_render_tests` exit 0 (564 cases: 563 passed, 1 skipped).
The render suite had failed at `test_resource_lifetime_gpu.cpp:242`: camera breathing at its defaults drifted
`camera.target` by an ulp. It now touches nothing at rest (ADR-1048 amendment).

**Resume here:** all seven Phase 4 systems are built, tested, documented below and committed (ADRs 1045-1050;
the commits `bcf2516b`..`b25b5e3d` on `proto/liminal-space`). The art agent's needs list (relayed by the
coordinator) is covered: text from a data block, beat envelopes with gain and a section mask, breathing on the
journey, mosh and channel shift, the sweep, and the line look (pixel width, per-surface colour). What a
successor might do next, if the art agent asks:
- a panel row for `temporal/mosh/*` (the echo has one; the mosh is reachable by parameters only);
- mesh text with the SDF edge look (today it reads through emission and bloom);
- SDF capsule-stroke glyphs inside the march, if mesh text does not sit right in the line-drawn world;
- the sweep before bloom (today the band's own light does not bloom; key bloom with it).

Each system: code, a test, an ADR, a guide entry below, then `git commit -- <paths>`. Never commit the art
agent's paths (`tools/liminal/`, `examples/liminal/all-you-got*`, PROGRESS-art.md). **Running the temporal GPU
tests rewrites four tracked PNGs at the repo root (`temporal-*.png`): restore them with `git checkout --`.**

### The owner's numbering (AUTHORITATIVE; the analysis numbers the count-in as bar 1)

The owner's bar N = SONG-ANALYSIS bar N+1. The owner's bar 1 beat 1 is at **2.20183486 s** (= 4 x 60/109). 109 BPM
to owner bar 74; **111 BPM from the downbeat of owner bar 75 (165.1376 s)**. Section starts in the owner's global
bars (derived from the brief's bar counts and checked against the analysis's sections):

| section | owner bars | name for `sections` |
|---|---|---|
| Intro | 1-16 | `intro: 1` |
| First release ("all you got") | 17-24 | `release: 17` |
| Verse 1 | 25-40 | `verse1: 25` |
| The pause bar | 41 | `pause: 41` |
| LET IT GO (8 bars) | 42-49 | `letgo: 42` |
| Verse 2 | 50-65 | `verse2: 50` |
| Bridge transition (FEEL IT GROW starts) | 66 | `bridgein: 66` |
| Bridge 1 | 67-74 | `bridge1: 67` |
| Bridge 2 ("is that all you", 111 BPM) | 75-82 | `bridge2: 75` |
| Bridge 3 (dance) | 83-90 | `bridge3: 83` |
| Final chorus | 91-114 | `chorus: 91` |
| Ending | 115 | `ending: 115` |

### Art agent's guide, pass 2

#### 1. The beat grid (ADR-1045): pulses and BIG CLAPs as data

Add one source to the project's `sources`:

```json
{"kind": "beatgrid", "name": "song", "settings": {
  "origin": 2.20183486, "beatsPerBar": 4,
  "tempo": [{"bar": 1, "bpm": 109}, {"bar": 75, "bpm": 111}],
  "sections": {"intro": 1, "release": 17, "verse1": 25, "pause": 41, "letgo": 42, "verse2": 50,
               "bridgein": 66, "bridge1": 67, "bridge2": 75, "bridge3": 83, "chorus": 91, "ending": 115},
  "events": [
    {"at": "release:4:4", "channel": "clap1", "release": 2},
    {"at": "release:8:4", "channel": "clap2", "attack": 0, "hold": 1, "release": 0.25, "curve": "linear"},
    {"at": "letgo:1:1", "channel": "let", "release": 0.5, "repeat": {"every": 4, "count": 8}},
    {"at": "bridge2:0:4.5", "channel": "is", "attack": 0.25, "hold": 0.25, "release": 0.5},
    {"at": "intro:5:1", "until": "release:1:1", "channel": "eighthGate", "attack": 1, "release": 1}
  ]}}
```

Signals it publishes (all 0..1, pure in time, so seek equals play):

- `grid.song.quarter` / `.eighth` / `.sixteenth` / `.half` / `.bar`: a pulse, 1 on the division and 0 just
  before the next. Its sharpness is `sources/song/pulseDecay` (fraction of the division; 0.3 default; small =
  a click, 1 = a long swell). Keyable and routable, so verse 2 can sharpen it.
- `grid.song.quarter.wave` (and every division's `.wave`): a raised cosine, 1 on the beat, 0 half way. Smooth:
  use it for camera breathing, bobbing and swells. `.phase` is the 0..1 saw (for spins that complete a turn per
  bar: route `grid.song.bar.phase` to a rotation with amount 360).
- `grid.song.<channel>`: each event channel. **A different treatment per clap = a different channel per clap**
  (`clap1` drives `palette/saturation`, `clap2` drives `camera/exposure/compensation` down for a cut to black,
  `clap3` drives an SDF node's scale...). A channel can carry many events (a word that repeats).

Event fields: `at` ("section:bar:beat" or "bar:beat"; beat 1-based and fractional; bar 0 = the bar before the
section) or `time` (seconds); `channel`; `strength` (default 1; may exceed 1); `attack` (rise *into* the
instant, so the peak lands on the beat); `hold`; `until` (a position; replaces hold); `release`; `curve` (exp
default, linear, smooth); `units` ("beats" default, or "seconds"); `repeat: {"every": beats, "count": n}`.

Wiring, with existing routes (ADR-011/900/1041):

```json
{"source": "grid.song.clap1", "target": "palette/saturation", "amount": 1.5},
{"source": "grid.song.clap2", "target": "camera/exposure/compensation", "amount": -8},
{"source": "grid.song.quarter", "target": "sdf/world/look/edge/intensity", "amount": 3,
 "depthSource": "grid.song.eighthGate"}
```

- Gate a pulse to a span of the song with an event that has `until`, used as the route's `depthSource`.
- Do not put a depth on an integrating route (ADR-1041).
- The pulses are exact on the grid; `audio.*` are the music's measured energy. Use the grid for the
  beat-locked things the brief asks for, and audio where you want the music's actual dynamics.

#### 2. Lyrics in the world (ADR-1046)

A word is a procedural node with a `text` source: extruded glyph geometry, lit, fogged, depth-tested against
the SDF, perspective-correct. Use the helper; it handles ~300 entries in one call:

```python
import sys; sys.path.insert(0, "tools")
from liminal_text import place_words
out = place_words([
  {"text": "LET", "t0": 93.52, "t1": 94.6, "position": [x, y, z], "normal": [0, 0, 1], "height": 0.5,
   "style": "pop", "role": "accent", "chapter": "rooms"},
  {"text": "IS", "at": "bridge2:0:4.5", "hold": 0.4, "position": [...], "normal": [1, 0, 0], "tilt": -8,
   "style": "flash", "color": [1, 0.4, 0.8], "intensity": 4},
], grid="song")
scene["nodes"] += out["nodes"]; grid_settings["events"] += out["events"]
project["routes"] += out["routes"]; palette["bindings"] += out["bindings"]
# out["chapters"]: {"rooms": [names]}: add them to that journey chapter's "nodes" so they show only there
```

- Entry fields: `text`; `t0`/`t1` (seconds) or `at`/`until`/`hold` (grid positions); `position` (the text's
  centre, world metres in the chapter's frame); `normal` (the wall's outward normal; the text faces it,
  upright) or `rotation` (Euler degrees); `tilt` (degrees in the wall's plane, + = anticlockwise as you face it);
  `height` (cap height, m); `depth` (extrusion, em, 0.08); `font` (`{"family", "weight" -1..1, "italic"}`,
  any installed family); `role` (palette role bound to the emission colour) or `color`; `intensity`
  (emission, 3); `fill` (lit base colour, dark by default); `style` (pop, rise, flash, flicker, cut); `in`/`out`
  (seconds); `rise` (m, for rise); `flash` (emission spike); `chapter`; `name` (default `w<i>_<TEXT>`).
- Each word is addressable: `nodes/<name>/scale|position|rotation|visible|emissiveBoost`,
  `procedural/<name>/material/emissive|emissiveColor|baseColor`, and its envelope `grid.song.<name>`
  (1 while shown). Key or route anything on top (a slide: a route from `grid.song.<name>` to
  `nodes/<name>/position` component 0).
- **At most 256 procedural objects draw at once.** The helper's `visible` route hides a word whose envelope is
  0, so only the words currently shown count.
- Without the helper: `{"kind": "procedural", "name": "w", "procedural": {"source": {"kind": "text", "text":
  "GO", "textSize": 0.7 (m per em), "textDepth": 0.08, "font": {...}, "textAlign": 0|1|2, "textVAlign":
  0 middle|1 baseline}, "material": {...}}}`. A text node faces +Z with its back at z = 0, and is a single
  instance.
- Mesh text has no SDF edge lines; make it read with emission and bloom (dark `fill`, bright emission).

#### 3. The line look (ADR-1047)

On every SDF object (`sdf/<object>/...`, also in the scene file's `look` and `surfaces`):

- `look/edge/intensity` (0 = off; 3-8 for luminous lines on a dark fill), `look/edge/color` (palette-bindable).
- **`look/edge/pixels`**: the line's width in screen pixels at every distance (1.5-3 reads as drawn lines;
  0 = the old world width `look/edge/width`). Use this in small rooms.
- `look/edge/softness` (default 0.28): small (0.05-0.12) = a hard, thin line; large (0.4-1) = a soft glow
  across the crease. `look/edge/threshold` (0.02): raise it to drop shallow creases (only hard corners draw).
- **`surface/<k>/edge`**: RGB multiplier of the edge colour on surface k (1 = the object's colour; 0 = no
  lines on that surface). For several line colours in one world, set `look/edge/color` to white and colour
  each surface's `edge`, and bind them in the palette. Needs `compile: true` (the helper sets it).
- The dark fill is the surface colour (`surface/<k>/color`) or `material/baseColor`. Bloom
  (`post/bloom/*`) makes the lines glow. Route `grid.song.quarter` into `look/edge/intensity` for lines that
  pulse on the beat.
- In JSON: `"look": {"edgeIntensity": 5, "edgePixels": 2, "edgeSoftness": 0.1}` and
  `"surfaces": [{"color": [...], "emission": [...], "edge": [1, 0.2, 0.8]}]`.

#### 5. Object animation on the beat (routes; ADR-1045)

Wrap an SDF prop in named `translate` / `rotate` / `scale` nodes (`sdf_node` helpers name them), and route the
grid into them. Tested recipe (seek-exact):

```json
{"source": "grid.song.bar.phase",     "target": "sdf/props/node/lampSpin/rotation",  "component": 1, "amount": 360},
{"source": "grid.song.quarter.wave",  "target": "sdf/props/node/lampAt/translation", "component": 1, "amount": 0.1},
{"source": "grid.song.quarter",       "target": "sdf/props/node/lampPulse/scale",    "amount": 0.25},
{"source": "grid.song.clap",          "target": "sdf/props/surface/0/emission",      "amount": 4}
```

- **Spin**: a division's `.phase` x 360 (one turn per bar with `bar.phase`, per beat with `quarter.phase`); the
  wrap from 360 to 0 is invisible. A continuous spin at a varying speed: a rate route with
  `"chain": {"integrate": true}` (no depth on it).
- **Bob**: a `.wave` into a translation component; `"polarity": "bipolar"` swings both ways.
- **Scale-pulse**: a pulse into a `scale` node (op add, small amount) or `nodes/<n>/scale` for a mesh node.
- **Glow**: a pulse or an event channel into `surface/<k>/emission` (or `look/edge/intensity` for the lines,
  `surface/<k>/edge` for one surface's lines). Mesh nodes: `nodes/<n>/emissiveBoost`.
- Gate any of these to a section with `"depthSource": "grid.song.<gate>"` (an event with `until`).
- An SDF node changes with the whole world's march (no rebuild): animate as many as you like.

#### The figure's tint (pass 1 gap 2)

Rechecked: a walking (`Walk_Loop`), keyed or journey-anchored figure takes `nodes/<n>/tint`, including when the
palette binds it. The tint is **opt-in**: the node needs `"tint": [r, g, b]` in the scene, or
`nodes/<n>/tint` does not exist and a palette binding to it does nothing. If a figure still renders pale, check
that first, then tell me the scene and time.

#### 6. Corruption: data mosh and channel shift (ADR-1049)

Project parameters (keyable, routable):

- `temporal/mosh/enabled`: true for the whole film (cheap while idle; it keeps the history ring warm).
- `temporal/mosh/amount` 0..1: the share of blocks replaced by the same block from up to `frames` frames ago,
  dragged `smear` px. **Drive it from an event channel**: `{"source": "grid.song.glitch1", "target":
  "temporal/mosh/amount", "amount": 0.6}`.
- `temporal/mosh/shift`: px the red and blue channels split (twice that inside corrupted blocks). Route a
  channel into it for a colour-corruption hit.
- `temporal/mosh/block` (px at 1080 lines, 32), `smear` (px, 24), `frames` (1-32, 8), `rate` (re-picks per
  second, 12; 0 = frozen pattern), `seed` (key a new value for a different pattern).
- Byte-identical to no effect while amount and shift are 0. Scrub-safe once `frames` frames have played
  after a seek (the history ring's warm-up).

#### 7. The spectrum sweep (ADR-1050)

Project parameters `post/sweep/*`: `progress` (0 = the band waits off the leading edge, 1 = it has left the
far edge), `width` (half-width as a fraction of the frame, 0.35), `intensity` (added light, HDR), `wash` (0..1,
how far the band tints the image under it), `angle` (degrees: 0 left to right, 90 top to bottom, -15 a
slight rise), `span` (rainbows across the band, 1), `hue` (offset in turns), `trail` (0..1 wash left behind).
Off while intensity and wash are both 0.

For owner bar 90, beats 3-4 (198.651-199.732 s), key `progress` 0 -> 1 across the two beats (or route a
grid event with `"attack": 2` in beats into it with op replace), and key `wash`/`intensity` up just before and
down after. Add a bloom key if you want the band itself to glow: the sweep is applied after bloom.

#### 4. Camera breathing (ADR-1048)

Parameters `camera/breath/amount` (multiplies everything; default 1), `forward` (m, + towards the subject),
`lift` (m), `side` (m), `yaw` (deg, + left), `pitch` (deg, + up), `fov` (deg added). Applied after the journey
(or any camera), roll-free, without touching the journey's distance (so swaps do not move). Drive them from the
smooth waves and key `amount` per section:

```json
{"source": "grid.song.quarter.wave", "target": "camera/breath/forward", "amount": 0.08},
{"source": "grid.song.quarter.wave", "target": "camera/breath/fov", "amount": -1.5},
{"source": "grid.song.half.wave", "target": "camera/breath/yaw", "amount": 0.6, "polarity": "bipolar"}
```

and a timeline track on `camera/breath/amount` (0 in bridge 3 and the chorus, where the brief says no camera
pulse; 1 in the intro; 0.5 in the verses). For a breath that eases rather than ticks, add
`"chain": {"springHz": 2, "springDamping": 0.7}`. Keep `forward` small near walls: the collision guard does not
see the breath.


## Where things stood after pass 1 (2026-09-30, about 20:00)

- Branch `proto/liminal-space`, worktree `/Users/natefaulkenberry/Documents/GitHub/av-gen-liminal`. Never push or
  merge. Shared worktree: commit only engineering paths with `git commit -- <paths>`; the art agent owns
  ART-RESEARCH, SONG-ANALYSIS, DIRECTOR-PLAN, `tools/liminal/`, PROGRESS-art.md.
- Build: `cmake --preset release -DCPM_SOURCE_CACHE=/Users/natefaulkenberry/Documents/GitHub/av-gen/.cache/cpm`
  (first time only), then `cmake --build --preset release -j 10`.

| step | what | status |
|---|---|---|
| 0 | ENGINEERING.md (research + feasibility) | done |
| a | vocabulary: `stairs`, `screw`, `warp`, `shell` nodes + `tools/liminal_sdf.py` (ADR-1040) | done |
| b | `spring` + `integrate` chain stages, seek-exact (ADR-1041) | done |
| c | journey camera, wrap, guard, nodes on the journey, **chapters**, look-at blend (ADR-1042) | done |
| d | project `palette` (ADR-1043) | done |
| e | surfaces (a material id per node) and node `tint` (ADR-1044); the walking figure | done |
| f | `examples/liminal/` (48 s) via `tools/make_liminal_example.py`; capture in `~/Desktop/av-gen-review/24-liminal-space/eng/` | done (`liminal-example.mp4`, 48 s, 1280x720, rendered at 18.9 fps) |
| 3 | analyzer: `tools/liminal_critic.py` (Critic inputs for an SDF project + the section 16 temporal checks) | done (see below) |

## Suites at hand-back (fd66212d code)

- `avgen_tests`: exit 0; 3,945 cases: 3,925 passed, 19 skipped, 1 failed as expected (the slope lean).
- `avgen_render_tests`: exit 0; 561 cases: 560 passed, 1 skipped.

## Test commands

```sh
cd /Users/natefaulkenberry/Documents/GitHub/av-gen-liminal
tools/gpu-lock.sh ./build/release/tests/avgen_tests "[liminal]"            # the POC's CPU cases (23+)
tools/gpu-lock.sh ./build/release/tests/avgen_render_tests "[sdf]~[.probe]" # SDF GPU parity, compiled, surfaces
tools/gpu-lock.sh ./build/release/tests/avgen_tests                         # full CPU suite: ONE FAILED line (slope lean) expected
tools/gpu-lock.sh ./build/release/tests/avgen_render_tests                  # full GPU suite
```

`"[sdf]"` alone also selects the hidden `[.probe]` shadow-map probe, which fails by design (a known defect).

## Performance (Apple M2 Max, the example, median of 138 frames, `--headless --frames 150 --bench-json`)

| view | 1920x1080 GPU | 960x540 GPU | sdf march | volume march | steps avg/max | out of steps |
|---|---|---|---|---|---|---|
| corridor (4 s) | 60.7 ms | 19.9 ms | 38.1 / 10.4 ms | 20.9 / 8.4 ms | 37.9 / 121 | 0.0 % |
| room (16 s) | 59.4 ms | 19.9 ms | 37.2 / 10.4 ms | 20.9 / 8.5 ms | 37.0 / 192 | 0.0 % |
| stair (26 s) | 60.0 ms | 19.3 ms | 37.2 / 10.0 ms | 21.0 / 8.4 ms | 36.5 / 179 | 0.0 % |

Measured with the lamps as a second SDF object (since merged into the world as a surface, which removes one
march). Offline 1280x720 renders at 16-18 fps. The volumetric march is a third of the frame: lower
`scene/volumeMaxDistance`/steps or density if the art does not need it everywhere.

## The art agent's guide

Everything below is data: `tools/liminal_sdf.py` (the vocabulary), a generator script in the pattern of
`tools/make_liminal_example.py`, and the project/scene JSON it writes. No engine code.

### Building spaces

- Helpers (metres, +Y up; a box is given by its extents): `room(interior, wall, openings, name, floor_surface)`,
  `corridor(start, end, width, height, floor_y, open_start/end, floor_surface)`, `doorway(wall, room, along,
  width, height, sill)`, `opening(extents)` (windows, cracks), `stairway(origin, steps, run, rise, width,
  direction, thickness)`, `landing(center, sx, sz)`, `platform(..., pier)`, `slab(extents)`, `union`,
  `difference`, `translate`, `rotate`, `morph`, `shell`, `surface(node, k)`.
- Name what you will animate: `name="room"` makes `sdf/<object>/node/room/size`, and a room's centre is
  `node/<name>At/translation`. Grow a room by keying both (floor = centre.y - size.y + wall/2). Openings are
  slabs too: name one to key its width.
- **Rules that bite:**
  - Keep cell content inside its screw cell; put the seam (the plane perpendicular to T through +-T/2)
    through the middle of a corridor, never past a wall's end, and overlap the pieces across it (the example's
    `hallNext`).
  - The screw's `seam` margin must exceed `epsilon x maxDistance` (0.24 by default); below it the seams
    draw as stripes in the distance. Smaller is better for a helix (its seams cross walls at the corners).
  - Cut doorways deep (the helpers do): a CSG cut is only a distance bound near its faces.
  - Do not `morph` between two rooms whose walls are far apart (moire); key the room's box instead.
  - `breathing(..., cell=T)` inside a translation screw fades the warp at the seams; keep its amount small
    (0.05-0.3 m) and its frequency low (0.05-0.1). Relax `stepScale` (0.8) with a warp.

### Surfaces (ADR-1044)

`sdf_node(name, tree, surfaces=[{"color": ...}, {"color": ...}, {"emission": [r, g, b]}...])` and
`surface(subtree, k)`. Up to 8 per object; parameters `sdf/<object>/surface/<k>/color|emission`; bind them in
the palette. Needs `compile: true` (the helper sets it). Roughness is per object.

### The journey (ADR-1042)

- Scene `camera`: `{"mode": 3, "fov": 62, "journey": {"chapters": [ {chapter}, ... ]}}`; a chapter is `name`,
  `start` (global metres), `from` (its own path's metres at `start`), `path` (floor points in cell 0; the
  camera adds `height`), `screw` (`translation`, `count`: the world's screw), `collide` (its SDF node),
  `radius`, `offset`, `yaw` (put its SDF nodes at the same position/rotation: `sdf_node(position=, yaw=)`),
  `nodes` and `lights` (shown only while the chapter is active).
- Parameters, all keyable: `camera/journey/distance` (metres; holds are equal keys), `yaw` (+ = left),
  `pitch`, `lookAhead`, `height`, `bob` and `stride` (the step, follows distance), `sway` and `swayRate` (the
  searching gaze), `radius` (0 disables the guard), `lookAt` + `lookAtWeight` (ease the gaze to a world point,
  in the journey's unwrapped frame), and `camera/fov` (the dolly zoom).
- A chapter swap is a jump in world position at a distance. Hide it in light: key `palette/value` and
  `camera/exposure/compensation` up and down around it (the example's 36-40.5 s). **Do not route audio pace
  into the distance ahead of a swap** (it moves the swap in time).
- Nodes on the journey: `"journey": {"distance": d}` on any composition node; its `position` becomes an offset
  (x right, y up, z forward); key `nodes/<name>/journey/distance`. The figure: Quaternius UAL1 (CC0,
  local-only) with `"animation": {"state": "Walk_Loop"}`, ~1.3 m/s to match the stride; add `"tint": [r,g,b]`
  and bind `nodes/figure/tint` to a palette role.
- The wrap is automatic; anything not periodic under the screw pops at it. Place lights and props in every
  covered cell with `screw_apply(point, T, count, k)` for k in about -1..4.
- Check a path: the test `Journey: every chapter of the example is clear...` in
  `tests/integration/test_liminal_journey.cpp` samples every chapter's path against the true distance; copy
  it for a new project (or point it at your scene) and run it with `"[liminal]"`.

### Continuous change (ADR-1041)

- The director owns trajectories on the timeline (smooth keys). Autonomous drift: `lfo`/`noise` sources.
- Music modulates through routes with `"chain": {"springHz": 0.35, "springDamping": 0.55, ...}` (weight: eases
  in and out) or `"integrate": true` (a rate becomes a position: a warp phase, a pace). Never put a `depthSource`
  on an integrating route. Scene states and sample-and-hold sources are not used.
- Seek equals play (bit-exact at 60 fps). Render finals at 60 fps or from 0.

### The palette (ADR-1043)

Project `"palette": {"states": [{"name", "colors": {role: [linear r, g, b]}, "scalars": {...}}], "bindings":
[{"role", "target", "component", "gain", "mode"}]}`; key `palette/position` (a float through the states),
`palette/saturation`, `palette/value`. OKLab blending. Bindings replace their targets after the routes. Good
targets: `sdf/<o>/surface/<k>/color|emission`, `scene/fogColor`, `env/sky/zenithColor|horizonColor|groundColor|sunColor`,
`lights/<id>/color`, `sdf/<o>/look/edge/color`, `nodes/<n>/tint`.

### Sky, sun, beacon, sun patch (existing features, confirmed by reading, not yet rendered here)

- Sky: the scene's `environment.sky` (ADR-036): `enabled`, `background` (draw it where the SDF misses),
  `zenithColor`, `horizonColor`, `groundColor`, `sunColor`, `sunDirection`, `useKeyLight`, `sunIntensity`,
  `sunSize`, `sunGlow`, `intensity`; the colours are parameters (`env/sky/*`). It is withheld wherever the
  architecture is closed, so a doorway or a lifted ceiling reveals it. Directional light and the sky are safe
  under a translation wrap.
- Beacon: a point light (`lights`) with `"volumetric": <strength>` scatters in the volumetric fog
  (`scene/volumeDensity` > 0, `scene/volumeLocalLights`); a surface with `emission` is the glowing doorway.
- Sun patch: a thin floor slab tagged with an emissive surface (it glows and blooms; it does not light).

### Capturing

```sh
tools/gpu-lock.sh ./build/release/src/avgen --headless --project <project.json> --render <out.mp4> \
    --range a:b --size 1280x720 --fps 30          # audio muxed from the project
tools/gpu-lock.sh ./build/release/src/avgen --headless --project <p> --frames 150 --range t: --size 1920x1080 \
    --bench-json out.json                         # GPU timing at t
./build/release/src/avgen --project <p> --audit-routes -   # every route and track: live/dead/hazard
```

### The analyzer (section 15-16, and the addendum's criteria)

`tools/liminal_critic.py` (python3 + numpy + ffmpeg; no new dependency):

- `inputs --project <p> --sections tools/liminal/all-you-got.sections.json --video <render> [--video-start s] --out <d>`
  writes Creative Critic inputs for an SDF project, which its AV Gen adapter could not describe: segments from the
  section map (bars to seconds on the song's grid, the section's `coupling` as the intended energy, so the Critic
  stops using its high-band proxy), shots from the timing sheet, the journey (chapters, the keyed walk as speeds),
  the palette timeline, the routes, and the addendum's ten questions in the intent. Then
  `cd ../creative-critic && .venv/bin/critic submit --inputs <d>/inputs.json --mode preview --wait`.
  Checked: job `job_1a0f4beb258918319` (fast mode, the 32 s example) completed with full coverage and 10 shots.
- `temporal --video <render> --audio ~/Desktop/"All You Got.wav" --sections ... [--video-start s] --out <d>` writes
  `temporal.json`/`.md`: **aggression-unanswered** (4-bar windows where the music's aggression -- loudness,
  high-band energy and noisiness together -- rises and the picture's activity does not), **snapping** and **flicker**
  (isolated change spikes, A-B-A flips, and whether they sit on audio transients), **colour-too-fast** /
  **colour-churn** (an OKLab colour move completed in under a bar away from a section boundary, or more than two
  per phrase), **breakdown-contrast** (a breakdown section not at least 15% below its neighbours in visual
  intensity). Findings are phrased like the brief's section 16 examples, and the report lists the addendum's
  emotional questions for the reviewer. `selftest` shows each check firing on its defect and quiet on its
  absence. On the 32 s example: aggression-activity correlation 0.83, 0 snaps, 0 flicker frames.

## Open (the art agent's needs list, ranked by the coordinator)

| need | status |
|---|---|
| 1 chapters | done (ADR-1042) |
| 2 fov keyable during a journey | yes (`camera/fov`) |
| 3 look-at blend | done |
| 4 several materials in a world | done (surfaces, ADR-1044) |
| 5 room growth / separating walls without the guard fighting | authoring (keyed boxes and translations); the guard only pushes when geometry comes within `radius`; set `camera/journey/radius` 0 to disable |
| 6 sky gradient and sun, withheld | existing (environment.sky); not yet rendered in an SDF scene here |
| 7 beacon with volumetrics, sun patch | existing (light `volumetric`, emissive surface); not yet rendered here |
| 8 figure bound to the palette | done (`nodes/<n>/tint`) |
| 9 motes near the camera across the wrap | open: anchor a particle emitter to the journey at the camera's distance; world-space particles will jump at a wrap, keep lifetimes short |
| 10 near-field tremble | done (`tremble()`: a warp with count 1, centred on the camera every frame; wrap it round the whole tree) |
| 11 section map to the analyzer | done (`tools/liminal_critic.py inputs` / `temporal` read `tools/liminal/all-you-got.sections.json`) |

## Known issues

- A faint jagged edge where the corridor stub meets the seam at floor level (about 28 s in the example): the
  seam guard lets a ray step up to the margin into the next cell; smaller margins trade it against seam stripes.
- The helix chapter's corners show the same at wall joints (margin 0.16 there).
- The example's look is engineering-grey on purpose; the art direction replaces palette, light and air.
- Motes across the wrap (need 9) are not built.

## Notes for a successor

- Creative Critic is a separate repo, `/Users/natefaulkenberry/Documents/GitHub/creative-critic` (Python;
  `adapters/avgen/avgen_adapter.py` is GV3-shaped and yields 0 shots for an SDF project; analyzers in
  `src/critic/analyzers/*.py`, rules in `src/critic/synthesis.py`; see the map in this file's history).
- Seek-exactness comes from ADR-901: route chain state is replayed on the 60 Hz grid. New chain stages must keep
  all their state in `ProcessorChain::State`.
- Rebuilding a test binary while a suite runs from it can kill the run; wait for the suite.
