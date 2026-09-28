# Phase 3 integration, gv3-int: the merged film, re-validated, previewed and evaluated

The integration stream's log (briefs.md, "Phase 3 integration: gv3-int"). Each round records what
changed, what was measured and what was decided. Worktree `av-gen-gv3-int`, branch `gv3/integrate`,
from `gv3/production` `b74c3429` (all four Phase 3 streams merged: cast, cut, look, world). Engine:
the shared build `av-gen-engine-3` (main `876a11e2`).

Evidence is in `~/Desktop/av-gen-review/18-glowmere-valley-3/revision/integrate/`. The Critic
session is `gv3-revision`, track `film` for whole films; clips go on per-category tracks.

## How a round is made

Nothing generated is committed. Each round is a snapshot under `build/gv3/int/<round>/`:

1. `python3 tools/make_glowmere_valley_3.py && python3 tools/gv3/ufo.py --no-trace` in the worktree;
2. `build/gv3/int/tools/snap.py <round>` copies the project, scene, intent tables and cut into
   `build/gv3/int/<round>/examples/world/`, a mirror of the repository's layout (the scene names
   `../materials/...` and `../../assets/...`, so `examples/<dir>` and `assets` are links), makes the
   song's path absolute, and restores the tracked first-pass copies (`git checkout --`);
3. every check and render reads that snapshot: `avgen --audit-routes`, `avgen_cast_trace --camera`
   (226 s, 60 fps, 20 Hz), `avgen_character_quality`, `tools/camera_stability.py`,
   `tools/gv3/framing.py --scene`, `tools/gv3/world.py --project --trace`, and the render
   (`build/gv3/int/tools/render.sh`, through `tools/gpu-lock.sh`, logging any other test binary or
   render that runs beside it).

`world.py` gained `--project` and `framing.py` `--scene` for this; both default to `examples/world`
as before.

## Round 1: the merged film as the four streams left it

**Generated** (`gv3/production` `b74c3429` on engine-3), log `build/gv3/int/gen-r1.log`:
- 73 shots, 225.50 s; the recorded Director cut (`song_cut.json`). The live Director now cuts 8
  spans differently (53.1, 55.1, 65.1, 66.1, 68.1, 89.1, 90.1, 107.1: gv3-cut's seven plus 107.1),
  from Song Mode's subject term (gv3-cut's finding: the cast moved, the music did not). The recorded
  cut stays, as gv3-cut decided.
- The reactivity: 59 of 96 proposed routes installed, 1 added; the generator's audit 93/93 routes,
  42/42 tracks live.
- The world closed (the north head and falls, the south sill, the river's flow pinned at 0.4116 m/s).
- `ufo.py --no-trace`: the plan compiles with nothing blocked; nominal moments E1 beam 13.865,
  E2 cross 26.636, E3 lift 66.951, E4 lift 103.871, E5 beam 170.339, lift 172.756, depart 177.699.

**The route audit on the project that renders** (`avgen --audit-routes`, `build/gv3/int/r1/audit.json`):
93/93 routes, 128/128 tracks (the camera tracks included), 7/7 effect defaults and 2/2 effects live.
**0 unknown parameters.** The load's warnings are the known two: `lodCount` (a setting the source's
procedural carries, 3 lines) and `groundGlow` having no effect under the authored ground program (an
engine defect on the open list). The nav grid is trusted: "971 sampled walks agreed with the world
exactly"; 5 pieces, 48% of walkable cells unreachable from the largest (gv3-world W6's figures).

**Re-validated across streams** (the r1 trace: `avgen_cast_trace --camera`, 226 s at 60 fps, 20 Hz, 31 min on a
machine at load 100-160; `build/gv3/int/tools/checks.sh r1`). The cast is gv3-world's closed-world trace of the
merged cast exactly (`cast-e3final.json`: every framing and on-screen moment identical): cameras do not steer it,
and gv3-look's changes do not either.

| Check | Result |
|---|---|
| E1-E5 (`ufo.py --trace-file`) | all five play: E1 beam 13.900, sweep 15.033; E2 cross 26.650; E3 lift 66.967, **bull-21** taken 71.883; E4 lift 103.883, cow-23 and cow-12 taken 111.917; E5 beam 170.350, lift 172.783, horse-11 taken 177.717; each craft held within 0.35-0.42 m |
| Watchers (`ufo-beats.json`) | E4: rook 111.20-112.60 (27 m), sage 113.05-117.30, ember 103.65-107.60 and 111.40-114.25. E5: rook 174.00-176.65, ember 171.25-176.75, **vane 172.30-180.20**, so 95.1 "Vane sees it" (174.02-174.94) holds |
| Follow stability (`tools/camera_stability.py`) | **19 of 22** pass (gv3-cut, source world: 21 of 22). Fail: s35 59.1 (eye height HF 2.41 cm), s54 95.3 (pitch 1.31, ADR-913's exception, and now yaw 0.128), s63 103.1 (travel 1.49) |
| Framing (`framing.py`) | every aimed or followed subject in frame 100% of its shot. It reports the aim node only: 59.1 aims at the scout and rides Sage, and **Sage is out of frame for the whole shot** (the search harness: head at x +0.83, chest below the frame). 38.1's Tide is in frame |
| Set-piece moments (`onscreen.py`) | every moment the cut means to show is on screen; E3's bull-21 in frame in 37.1 and 38.1 (the rigs aim live at the scout, so the 15 m move of E3's column needs no re-framing) |
| ADR-910 (`avgen_character_quality`) | aliens: longest stands rook 4.4, tide 4.3, sage 7.8, ember 7.2 s, vane 8.9 s (watching E5, allowed); reversals rook 3 (165.9, 173.0, 183.0 s at (-105..-111, -29..-40), new with the closed world), ember 4 (199-223 s on the west bank: navfix's ADR-932/936 case), sage 1, vane 1. Animals 0 reversals, 4 turns over 90°, 5.2 s on steep ground, 0 s uphill |
| World (`world.py --check --trace`) | **FAIL**: survey, filmed ground, falls, seams all hold, but gv3-cut's new cameras see the edge: 17 views with an open end (s44 77.1 7/7 views, up to 17 of 96 columns; s03, s24, s26, s65 one column each) and the river's mouth in 91 views of 12 shots (up to 18 columns in 71.1's 85 mm) |
| Moon (`build/gv3/int/tools/moon.py`) | the crisp moon in 7 shots; a second, soft disc 32.5° away in 11 (below) |

**The render and the sheets** (`build/gv3/int/r1/review/`, `tools/gv3/review.py`; copied to the review folder):
- **Two moons.** `shaders/skybox.wgsl` draws a crisp moon at the sky's sun direction; the visible procedural sky
  (`sky_background.wgsl`) is looked up through `envRotate`, i.e. `env/rotation`, -0.568 rad inherited from GV2,
  so its own soft disc sits 32.5° away. Both show in 17.1, 59.1 and 93.1 (and briefly 11.1); the soft one alone
  in 47.1, 65.1, 94.1, 95.3 (a 123 px disc behind the rising horse), 97.1, 101.1 and 109.1. An engine defect (the crisp disc ignores the
  rotation; GV2 multicam has it too), and the crisp disc's colour and size are hard-coded: not in the UI.
- **E2's moon** is the crisp disc in the top-left corner of a 24 mm lens, stretched by the lens and greyed by the
  pull-back's exposure (a fixed (1.8, 2.1, 2.4) under ev -1.85).
- **95.3** (s54, the E5 hero moment): the horse is a pale ghost seen through the beam's particle cloud; no gold,
  12.7% of the frame clipped. The beam's `audio.rms -> emissive` link (+1.5 on a set-piece base of 0.7) triples
  it in the loud riser, and the Critic sees no response from any of its four links.
- **Composition, the scatter the traces do not hold:** 19.1's push ends inside a fern (the ring-wave shot);
  25.1's lantern behind a big leaf; 29.1's canopy hides the elder (the brief's item); 31.1's spire lost among the
  lit hillside's mushrooms (the brief's item); 73.1 ends with a leaf over the lower half; tree trunks cross 104.1
  and 107.1 mid-arc; 38.1 is half a dark rock (the first pass's "s13 at 70 s" is gone: its span is now 38.1).
- **The world edge at full size:** 77.1 looks down the valley's axis from 28 m up the new north head, and the
  south end is a flat shelf with the river's mouth a rectangular, vertical-walled slot dead centre: it reads as
  the edge of the world. 106.1 shows the mouth as a slot at the left. In 81.1, 69.1 and 66.3 it reads as a
  distant pass; in 71.1 as a flat horizon behind the elder.

**The Critic's pacing, checked against the picture.** r1's novelty reads 92.6 s held after the shots' last new
information and 18 shots static from their first frame, where gv3-cut's it2 whole film (the same cut, the first
pass's look) read 34.2 s and 8. Shot by shot the mean novelty fell 2-4x even where the camera's move did not
change (s12 0.178 -> 0.069, s30 0.178 -> 0.060, s47 0.119 -> 0.074). What changed between the two is the look:
the sky no longer follows the spectrum frame by frame (gv3-look: its frame-to-frame change 6.8% -> 1.4%), and the
distance is a smooth fog. The open items already list the Critic's bias ("it rewards sky flicker"). Measured
below the sky instead (`build/gv3/int/tools/groundmotion.py`: luma change over a third of a second in the frame's
lower 65%), s04, s05, s12 and s30 move as much as most of the film; the slowest shots are the intentional stills
(the half-bar glimpses 48.1 and 48.3, E2's night wide, the riser's locked frames) and 33.1's cairn (3.7 s). So
`trim.py`'s 15 proposals on r1 are not applied: they include 81.1 cut to 4 beats (the suspension's locked-off
wide the plan keeps) and 57.1 (E4's lift, where the pair rises through the frame).

**The air.** Beside the first pass's final, r1's wides (41.1 at 76 s, 117.1 at 218 s) are veiled in a bright teal
haze from the middle distance on, where the first pass's were a navy night with vivid emissives: the render
stream's `scene/fogSky` 1.0 (the fog fades to the sky's radiance, the aurora included) that gv3-look took into
`look.BASE`. It is the likeliest cause of the Critic's visual hierarchy 0.713 -> 0.645 ("bright areas away from the
subject dominate", 15 findings, mostly wides) and of the palette drifting from GV2's. An A/B is queued.

## Round 2: fixes in data, judged on clips

**Film-wide, in the generator (`e1bc1c49`):** `env/rotation` 0 (one moon; `look.BASE`) and the saucer beam's
four audio links dropped (`reactivity.REACTION_DROPS`). The generator's audit: 89/89 routes (93 less the four),
42/42 tracks.

**Judged on scratch variants of r1** (`build/gv3/int/tools/variants.py`: a shot's span divided among candidate
rigs, so one range render shows them all; alien shots one candidate a variant, whole span):
- batch A (`vA`): 19.1, 25.1, 31.1, 77.1 and 106.1, four or three candidates each, with the film-wide fixes; the
  77.1 and 106.1 candidates were chosen with `world.py`'s own open-end test (0 columns over their whole paths:
  77.1 low on the valley floor by the river instead of 28 m up the new north head; 106.1 from south of the veil,
  looking away from the mouth);
- batch B (`vB1-4`): 29.1 (Vane, 8-11 m up, where the canopy cannot stand between her and the elder) and 59.1
  (Rook, who goes to see E4: a fixed eye up the west bank behind him, a live aim under the hovering scout);
- the post pass (`vP`, brief §14, the audit's recipe in steps: AgX, the bloom, the filmic finish) and the air
  (`vF`, `scene/fogSky` 1.0 / 0.5 / 1.0 at 600 m / 0), step-keyed inside quiet windows of nine and seven shots.
All wait on the GPU behind the coordinator's navfix suite.

### Batch A: judged (`vA`, rendered 17:38-17:44, alone on the GPU but for CPU suites; every clip exit 0, GPU errors 0)
Sheets: `build/gv3/int/cand-<shot>.png` (r1's frames above each candidate's).
- **19.1:** candidate b, a 4 m truck across the view at 1.3 m. The r1 push, the same push at 1.8 m, and a push
  from further back all meet a fern; the higher, down-looking one grazes one at its end. Applied (`a2378178`).
- **25.1:** candidate b, from the south-east at 17 m: the lantern whole, its cyan underside (where the clap reads)
  over the frame, two aliens small below it. The low close-up under the cap (c) was the most striking, and cut
  the cap at the edge. Applied.
- **31.1:** candidate d, low from the west through a 24 mm, looking up: the spire's cap against the night sky and
  the (now single) moon, where every other vantage put it against the lit hillside or a tree. Its first frames
  start beside a big leaf, so the second half of its arc is being checked whole (batch D).
- **77.1:** all four low vantages pass `world.py` only because the near bank fills the frame. From the north end
  the elder and the south end are on one bearing (1.3 and 2.8° east of south), so no view of the elder from there
  can leave the edge out. The shot is turned round (batch D): the valley's head, where the falls come down, under
  the aurora -- 675 of 675 searched views north pass, and no shot in the cut shows the head.
- **106.1:** candidate b, from the south-south-east: the veil whole under the aurora, no slot. Applied.
- **The riser** (the beam's links dropped, one moon): the haze in 95.3 thins a little and the second moon is gone,
  but the horse still fills an 85 mm frame through the beam's glow. The cause is the cast, not the beam: the tuned
  cast moved E5's station 9.5 m, and the first pass's rig, kept by the cut, now sees the horse rise 13 m away
  instead of 22. Beside the first pass's final (`cmp/riser-fp.png`) its s29 is the bounded column in the night
  that the plan keeps. 95.3's eye moved back to 22 m (applied); it is judged on r2.

**The heartbeat, looked at close** (the meaningful tier): in the tail (121.1), 65.1 and 47.1 the gold gills are a
flat peach on and off the kick (the gold pixels' luma ×0.98-1.00 at 50 ms against 300 ms after it): they sit at
the tone curve's shoulder, so the kick's +60-100% has no headroom close up. The Critic's +1% over the film comes
from the wider framings. The post A/B's 47.1 window shows whether AgX's shoulder gives it back.

### Batches B, C, D and E: judged (candidate sheets `build/gv3/int/cand-<shot>.png`)
- **29.1** (B): Vane 8-11 m up over the canopy, the elder beyond it; **59.1** (B): a fixed eye up the west bank
  behind Rook, who goes to see E4, a live aim under the hovering scout. Both applied (`60143694`).
- **31.1** (D): candidate d's whole arc, low under the spire, its cap against the sky (`9e423bbd`).
- **77.1** (D, E): turned round to the valley's head, the falls read as a flat textured panel between two
  rocks (`cand-s44-vD.png`); of four off-axis views with no open end (E, `cand-s44-vE.png`), **w6**: across the
  valley to the west slope under the aurora from 8 m up the east side, a cow below, a lit mushroom and an
  alien in the middle distance. Applied (`df64dce6`); 113.1 keeps the look down the valley.
- **38.1** (C): **c1**, 26 m behind Tide and 10 m up: the column clears the lantern's cap. **73.1**: **hi**, 2.6 m
  (the leaf gone). **104.1**: **a** at half its swing (-10 deg): the trunk stays left of the spire; **b** was
  31.1's frame over again (the same trunk, the spire, the moon: `cmp-31.1-104.1b.png`). **107.1**: **a**, the
  other way round. Applied (`df64dce6`).

### The post pass: kept as r1 (ACES)
`vP` (AgX, the audit's step 1) desaturated the Glowmere palette and made the wides milky; rejected. `vQ` kept
ACES and stepped the rest of the recipe in seven windows: the bloom at threshold 0.9 (Q1) puts a standing halo
on every emissive, the opposite of what the owner asked of the pulses (the peaks, not the rest, should cross
the bloom's 1.761); the filmic finish (Q2: grain 0.035, vignette 0.35, anamorphic 0.12) is barely visible at
540p; the look colour and light wrap (Q3) turn the hue 5-17 deg (47.1's gills an orange haze). The post stays
r1's: ACES, bloom threshold 1.761.

### The air: fogSky 0.5 (`vF`, ten windows, sheets `build/gv3/int/vF/ab-*.png`)
| | 41.1 mean luma | 71.1 | 117.1 | violet px 41.1 / 117.1 / 113.1 / 66.3 |
|---|---|---|---|---|
| F0 fogSky 1.0 (r1) | 0.292 | 0.395 | 0.253 | 17 / 25 / 737 / 170 |
| F1 0.5 | 0.233 | 0.318 | 0.239 | 83 / 31 / 877 / 209 |
| F2 1.0 at 600 m | 0.221 | 0.303 | 0.258 | 66 / 33 / 937 / 228 |
| F3 0 | 0.211 | 0.217 | 0.214 | 158 / 115 / 1134 / 389 |

gv3-world expected 1.0 to soften the ends, and it does: at 0 the end of 109.1's crane shows the river's mouth as
a slot between two flat blocks (`vF/zoom-109.1-far-end.png`) and 71.1's elder stands in front of a hard cyan
trapezoid (its light's volume, lost in the haze at 1.0). At 0.5 both stay soft, the wides are navy under the
aurora again, and the small violet mushrooms come back (41.1: 17 -> 83 pixels). Applied (`4f246a9a`). 81.1 and
85.1 (the suspension) change little either way.

### The owner's r1 items, first attempt (r2b: calibration clips on engine-4, E5 as r1)
- **Hero pulses** (`build/gv3/int/tools/pulse.py`: the elder's gills, kick lane, 50 ms against 300 ms after
  each hit): s01 mean x1.05 (r1) -> x1.11 (r2b), p90 x1.00 -> x1.01; 9.1 x1.04 -> x1.07, p90 x1.01 -> x1.03.
  Far from +40-60%. Two causes: a route's fall is a time constant, so 260 ms leaves 32% of a hit 300 ms later
  and 18% when the next kick lands (the gills never come down); and the gills sit at the tone curve's shoulder.
  The **ladder** (`vL`: every hero's gills and underside stepped through x0.05-3.2 with their routes off,
  `vL/ladder-sheet.png`) gave each hero's rest and peak: the elder's close-up gills p90 0.47 at x0.1, 0.67 at
  x0.2, 0.76 at x0.4, then only peach and clipping (15% of them at x0.8). Recalibrated (`45a87733`): each hero
  rests dim and flares to its brightest short of clipping (the elder 0.10 -> 0.60; the veil, lantern, bloom,
  umbra, spire and ridge 0.12-0.20 -> 3.2; cairn and scree 0.20 -> 1.6; ember 0.20 -> 1.0), each lane's fall
  lets the glow come down (kick 100 ms, clap and off-beat 150, once a bar 220), the quietest section at 70%;
  the elder's light rests at 1.5 and flares x5 on the kick (the spill).
- **The small mushrooms** (`tools/violet.py`: the violet fungi's pixels at a kick against 300 ms later): 5.1
  437 -> 551 pixels, luma x1.12; 9.1 426 -> 520, x1.01; the grand wides 13-53 violet pixels in the whole frame.
  The fog hid them (above), and the hit fell too slowly. Now +3.0 on the kick's 100 ms fall, the shelf fungi
  +2.4 and beacons +2.2 on 150 ms (`45a87733`). Their size is unchanged: if they still do not read in the
  wides, the next lever is a look change (size, or the light they cast) for the owner.
- **The Camera Travel Beam** fires on the 22 cuts that open a four-bar phrase (`r2b/travel-beam-sheet.png`: at
  each it starts at the lens and sweeps away into the valley in about 1.5 s). It read as a white scan (a
  near-white edge at intensity 3); its colour now steps through the valley's lights by segment (`4f246a9a`).
  The load says its kick route is dead ("the sequence has no marker with this trigger's name") while the
  frames show it firing on every marker: a false alarm in the liveness check, for the engine's list.
- **E4's two cows** rise apart for most of the lift, but at 107.0 s one body passes through the other
  (`r2b/e4-zoom-57.1.png`): the engine lays a pair out along +x/-x in the order it takes them and took the
  western cow first, so each crossed the other's path; and 57.1 looks along that axis. Named in the plan, the
  east cow first, and 4 m apart in height (`aff43de7`).

### Engine-4 (r2: the merged production, E5's approach 250 -> 18, `690c383b`)
| Check | r1 (engine-3) | r2 (engine-4) |
|---|---|---|
| Watchers of E4 | rook, sage, ember | sage 108.25-114.15 (45 m), ember 109.30-112.50 (14 m); Rook runs at it 106-111 s (facing it within 1-21 deg) and passes 59.1's lens at 108.3 s |
| Watchers of E5 | rook, ember, **vane 172.30-180.20** | **sage alone**, 170.90-177.15, 135 m off. Vane runs straight at the saucer from 173 s (facing it within 1 deg, 3.9 m/s, 99 -> 68 m); Rook too, from 174 s |
| 95.1 "Vane sees it" | Vane stands watching | Vane running at it: the close follow bobs (pitch HF 0.109, eye height 2.26 cm: fails the stability bar) |
| Follow stability | 19/22 | 19/22: 59.1 now passes; 66.3 fails travel (2.08: Sage's path changed); 95.1 fails (above); 95.3 pitch HF 1.31 -> 0.78, yaw HF 0.128 -> 0.040 |
| ADR-910 | reversals rook 3, ember 4; ember's ABA 5 | rook 1, ember 1 (ABA 1: no pacing); longest stills rook 3.3 s, ember 4.9, vane 4.2, sage 13.4 (watching E5, the only one who does); animals 0 reversals |
| World | 17 views with an open end; the mouth in 91 views | 20 and 94 (59.1's new eye sees the mouth in 7 views, up to 4 columns; 106.1's is gone) |

The trace logs no "no way across" line with which to count the errands the coordinator expected; what can be
seen is their effect: the reversals and the pacing are gone, and the aliens who went to watch E5 now go to it.

## Round 3: the whole film with round 2's fixes (`r3`, engine-4; render 19:28-19:55, 13,530 frames, GPU errors 0)
Generated from `aff43de7`: audit 90/90 routes, 130/130 tracks, 0 unknown parameters. CPU suites of other
streams ran beside the render (`r3/render-r3.log`); the coordinator's rule is that they no longer hold the lock.
- Pulses on frames (`r3/pulse/report.txt`) and the Critic (`job_1a0e55294f206d8e8`): see 04-iterations.md.
  The five heroes that did not register sit on sparse lanes; a cap ladder (`vC4`) showed the caps are not what
  reads, so no change there.
- E4 at 1080p (`r3/e4-1080p.png`): no crossing now, but 57.1 still saw the pair one behind the other (the
  engine spreads a pair east-west and 57.1 looked west). Re-framed from the south (`vK1`, `61e9dbf0`).
- The elder's light (`vS`): at the cap, x5 on the kick moved the ground 1-3%; 7 m lower it lights the stem
  and a ring of ferns (`61e9dbf0`).
- The small mushrooms' ground light (`vG`, the owner's "more light on the ground"): x1.4-10 gain with a 300 m
  range moved the wides' ground 0-6%; only the patch nearest the lens pools. The ecology lights are chosen
  nearest-first within a ~200-light budget (the trees' faint glow takes the slots) and the ground program
  ignores `groundGlow`: an engine item, sent to the coordinator.
- E5 on engine-4 at approach 18 left Vane running through 95.1; at 100 m (r4 trace) she stops facing the
  saucer and watches 172.55-180.05, with rook, sage and ember (`779875d5`).
- Close approaches under 1.4 m: ember-rook 0.76 m at 64.8 s (off screen in 35.1); on r5 cow-19-ember
  1.26 m at 181.65 s in 99.1, on screen 95 m off at 10 px.
- 71.1 at 1080p showed the river's mouth as a hard slot behind the elder; raised to 40 m so the frame's top
  edge meets the valley floor before the mouth (`5825af56`, verified on r6 clips).

## Round 5: the final candidate (`r5`; render 20:24-20:56, GPU errors 0; Critic `job_1a0e583fde98f962e`)
Against r3: musical synchronization 0.777 -> 0.885, effects 0.881 -> 0.945, technical quality 0.882 -> 0.923,
lighting 0.822 -> 0.845; issues 0/8/50/45. The elder's kick z 28-29, the lantern's clap 10-13, the bloom 10.6,
the spire 5-6 (observed now), the veil's spores 4.4; the umbra, cairn, ridge, scree and ember weak (z 2-3.7).
Novelty 29.3 s held, 2 shots static from the first frame. Trace: follow stability 21 of 22 (95.3's ADR-913
exception); E1-E5 play, E4 takes cow-12 and cow-23, E5's watchers vane, ember, rook, sage.

## Round 6: engine-6 (main ad5623d2: behave, uireach, ecolight), the candidate for the owner's review
`build/release` -> av-gen-engine-6; gv3/production (879cd647 + c26f3fd4) merged. Generated from `7419a6c1`:
audit 90/90 routes, 130/130 tracks, 0 unknown parameters. Render 00:43-01:07 (r6b), GPU errors 0.
- **Glow pools** (ADR-945, `7004044f`): range 300 m, only the violet fungi cast (faintest 6.5), reach 6 m (11 m
  washed the foreground evenly, `vR`), ecology light x3 (1.4 -> 4.2 through the arc), kick.fungi +3.0 kept. The
  ground in the wides at each kick against 300 ms later: 41.1 x1.10, 113.1 x1.17, 117.1 x1.08 (r5 1.05-1.07,
  41.1's with the travel beam in it); violet pixels 41.1 439 -> 1166, 113.1 1825 -> 2665, 117.1 710 -> 1147.
- **Aurora glints** 0.3 -> 0.85, the effect block set to the same: the sky's mean identical to r5 in 41.1,
  75.1 and 117.1, its top 0.5% within 1%, frame-to-frame change 8-11% lower (steady glints).
- **Behave:** E4 watched by sage (40 m) and vane; Rook no longer goes to it and was out of 59.1 for the
  whole shot (the Critic's critical finding on r6) -> 59.1 over Sage's shoulder (`7419a6c1`, vM1). E5 watched
  by vane 172.20-178.40, ember, sage; 95.1 stands. Stability 19/22: 80.1 (travel 1.28) and 103.1 (eye height
  2.11 cm) newly fail marginally, 95.3 the known exception. ADR-910: longest stills 2.7-8.9 s, no pacing.
  Closest approach (behave-closest.py) 1.16 m, horse-2 and vane at 186.4 s, off screen. World: 13 views with
  an open end, the mouth in 70 views of 8 shots (r5: 20 / 94 of 12).
- **Critic** r6b `job_1a0e668cdca4609c3` against r5: musical sync 0.885 -> 0.943, effects 0.945 -> 0.975,
  pacing 1.000; technical quality 0.923 -> 0.825 and lighting 0.845 -> 0.789 (clipping in pooled shots, up to
  1.6% of pixels; bright secondary masses), cinematography -4.0. Elder's kick z 39, lantern 14, bloom 9.6.

## Round 7: the Hero Pulse restored, one animal per abduction (project for review; not rendered for review)
- **Hero Pulse** (`5d9e7bb1`): GV2 multicam's `groundPulse` effects, which the first pass had
  removed (`REMOVED_EFFECT_TYPES`), carried over on the ten mushrooms and five aliens (not the saucer, the owner),
  GV2's look whole. heroFocus needs Song-mode spans, so each fires on "hero pulse <hero>" markers on the downbeats of
  shots where it is the subject or in frame within 160 m (two per shot at most): 140 rings, the elder 53, the lantern
  37. On frames (a superseded whole-film render made before the one-animal change) the ring swells from the hero across
  the ground in s01, 11.1, 25.1 and the wides 41.1 and 117.1; close up it floods the foliage near the lens at its peak.
- **One animal per abduction** (the owner): E4 lifts cow-12 alone; cow-23 re-homed beside cow-19, bull-10 (6.5 m from
  E3's beam on r6b) to bull-1's slope. `beamclear.py` on the r7b trace: the nearest other animal to any beam, beam to
  depart, is 50 m (E4: cow-3), 80 m (E5: cow-19), 145 m (E3: bull-1). 57.1 holds the single cow rising.
- r7b checks (CPU): audit 105/105 routes, 130/130 tracks, 18/18 effects; E1-E5 play; E4 watched by rook, sage, ember,
  vane; E5 by vane, tide, sage, rook; stability 20/22 (38.1 eye height 2.14 cm, 95.3); close approaches under 1.4 m
  two, off screen. At the owner's request no further renders: the project is in the review folder
  (`project-for-review/`).
