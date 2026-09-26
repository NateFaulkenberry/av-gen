# Phase 3: the revision plan

This is the plan for the scene revision. It turns each item in the owner's brief into a concrete
change, with the check that says whether the change worked. It is a starting position: the
Director's planners (wave 2) propose durations and reactivity, and the evaluator judges every
iteration. Where they disagree with this page, the evidence wins, and the disagreement is recorded
in the iteration log.

## 1. What stays
The first pass's concept stays: **the valley's light is the protagonist.** It wakes layer by layer
with the track, dims when a visitor passes over, is drained while the saucer takes an animal, and
comes back rebuilt on the drop. These are kept:
- the kick heartbeat in the elder's gold (the evaluator reads it as "configured and observed", about +9%);
- the horse glowing gold as it is lifted, and fading into the saucer on the last beat of the roll;
- the flyby in the first pull-back, as the promise the drop keeps;
- the s14/s39 rhyme between the first grand wide and the last;
- cuts on the 130 BPM grid, compressing with the roll into the drop;
- the arc's darkest points: the pull-back and the submerged break.

## 2. The drop: a new musical state, not a new style (brief §7)
The first pass stepped the whole grade on the drop, from the riser to the drop:

| | ev | saturation | temperature | light | aurora |
|---|---|---|---|---|---|
| riser | −0.3 | 0.9 | −0.10 | 0.6 | 1.2 |
| drop | +0.5 | 1.3 | +0.12 | 2.1 | 3.2 |

That is the "stylized shift". The revision holds the grade inside the range the film has already
established; the plateau is at ev +0.15, saturation 1.10 and light 1.3. The drop is expressed by
the world's behaviour instead:
- **Reactivity depth** rises with the section's energy, through the routes stream's `depthSource`. The same routes respond harder; there are no new looks.
- **Travelling light:** waves run out from the elder through the mushrooms on every downbeat, and now reach the whole valley rather than its middle.
- **Activity:** more fireflies and spores, stronger gusts, water tears catching the wind, and the aliens on the move.
- **Camera language** (kept from the first pass): the downbeat cut on the crash, wide lateral moves, a crane, and bar cuts through the first phrase.
- **The flash and the shake** on the crash stay. They are events, not a style.

The check: the evaluator's `visual_coherence`, and colour-histogram distance between the plateau
and the drop, measured against the first pass. Colour must stay within the plateau's gamut; the
drop may differ in brightness and activity, not in hue.

## 3. The film, section by section
Times are film seconds on the grid (bar *n* starts at 0.480 + (n − 1) × 1.846 s). The durations
here are targets. The song stream's planner sets the actual cuts, and the evaluator's novelty
measure ("last new information") trims any shot that has already said its piece.

| # | Segment | Bars | What it does now | UFO event | Pacing target |
|---|---|---|---|---|---|
| 1 | cold open | 1–4 | Alive on the first kick: low through the ferns to the elder; ferns in the wind; the nearest mushrooms flicker on the hats | — | one 4-bar move (contrast with what follows) |
| 2 | riff groove | 5–14 | Meet the valley. The heroes begin to answer, each on its own layer (§5). One alien, crossing, not led | **E1**: far up the valley, a thin beam sweeps a field and lifts nothing. It is seen tiny in the long-lens wide | 2-bar cuts |
| 3 | first pull-back | 15–16 | The light drops; something crosses the sky | **E2**: the flyby (kept) | one held shot |
| 4 | groove 2 | 17–32 | The light returns brighter; the first waves run through the mushrooms on the downbeats; an alien investigates a flickering cluster | — | 2-bar cuts, one 4-bar travel |
| 5 | lift | 33–40 | The body arrives; the first crane | **E3**: the crane rises to reveal a column of light far up the valley, and a cow lifted into it | 2-bar cuts; the crane runs 4 bars |
| 6 | arrival | 41–48 | The shimmer arrives: sparkle on the 16ths, the ecology light steps up, the mushrooms turn from violet toward cyan | — | the grand wide 4 bars, then 1–2 bar cuts |
| 7 | melodic plateau | 49–72 | No longer three 8-bar takes (the first pass's weakest stretch). The valley notices its visitors: an orbit of the elder, then the event, then the valley listening in close-ups | **E4**: two animals lifted together near the river, at middle distance; two aliens walk toward it and watch | 2–4 bar takes |
| 8 | lead forward | 73–80 | The aurora carries the lead; one alien looks up | — | 2–4 bars |
| 9 | suspension | 81–88 | The saucer comes over the rim; the light drains (kept) | **E5**, the centrepiece, begins | locked-off 4-bar wides (kept) |
| 10 | submerged break | 89–92 | Underwater (kept) | E5 | 2 bars |
| 11 | riser | 93–96 | The beam, the horse's lift and the compressing cuts (kept). The roll's 8ths → 16ths → 32nds drive the mushrooms' flicker rate | E5: the lift | 1 bar → ½ → ¼ (kept) |
| 12 | drop | 97–120 | The new state (§2). The saucer leaves; E3's craft leaves another way | departures | 1–2 bar cuts in the first phrase, then 4-bar wides |
| 13 | tail | 121–122 | The elder alone, then black on the last hit (kept) | — | one shot |

**Escalation:** far and nothing taken (E1), overhead (E2), far with one animal (E3), near with two
(E4), and the elder's own horse, in the centrepiece (E5). Each differs in:
- place, distance and framing;
- the number of animals;
- the camera: long-lens static, sky-held, crane reveal, walking with the aliens, then the compressed riser;
- relation to the music: E1 on the riff's phrase end, E3 at the top of the lift's crane, E4 on the plateau's sub-phrase lines (bars 57 and 65), E5 across the suspension, break and riser.

**Crafts.** E1 and E3 are flown by a smaller second craft (the same model at about 0.6 scale,
registered as its own actor), so that scale varies and the hero saucer's story stays continuous.
Nothing is committed from the licensed assets; only the scene data names them.

## 4. Characters (brief §9–12)
- **Screen time.** In the first pass, 12 shots (86 s, 38% of the film) are led by an alien. The target is 20% or less. The aliens appear more often as part of the world: in wides, reacting to E3 and E4, and crossing frames.
- **Behaviour.** No alien stands still for more than a few seconds unless it is watching an event, and there are no walk → stop → 180° → back reversals. The characters stream's metrics are the gate: longest still stretch, reversals, pivot yaw and turn radius, measured on the cast trace of the whole film.
- **Animals** are re-homed onto flat valley ground, with slope under every stationary body ≤ 12°, and walk through curved turns (the characters stream's `turnRadius`). Checked on the cast trace and by the evaluator's grounding check.

## 5. Audio reactivity: the map (brief §3–6, §16)
The reactivity planner proposes routes from the reactive catalogue. This is the intent it is
checked against. The rule: each musical layer has one visual owner, nothing important shares a
source, and phases are staggered.

| Level | Layer | Owner | Response |
|---|---|---|---|
| micro | hats (off-beats, 16th shakers in bars 41–80 and 97–120) | shelf fungi, in groups by region; spores and fireflies | small flickers, staggered 0–120 ms across groups; sparkle only where the shimmer layer plays |
| micro | the roll (bars 93–96) | the small fungi | flicker rate follows the roll's subdivision |
| meso | kick | the elder's gills (kept) | +60%, up to +100% in the drop via section depth |
| meso | clap (beats 2 and 4) | the lantern | a short emissive flare, its own colour |
| meso | bar downbeat | the spire; and a wave from the elder through the fungi (a field timed from the event) | a slow swell; a ring of light spreading at walking pace |
| meso | bass glide (every 2nd bar) | the caps' breath (kept); bloom's spores; the wind's gusts | a slow swell; a burst of spores; a gust |
| meso | phrase turnarounds (bars 24, 32, 40 …) | umbra | a hue shift that settles back |
| macro | section energy | ecology light, fog, emission gain (depth) | follows the arc |
| macro | sections | the mushrooms' hue | violet, then cyan from the arrival, then gold-white in the drop ("the light rebuilt") |
| macro | the lead (mid band, bars 73–96) | the aurora | intensity and a slow hue drift |
| macro | wind (always) | grass, ferns, leaves, spores, water tears | a steady direction; gusts every two bars; strength follows section energy |

**The three tiers of evidence, per route:**
1. **Configured:** the routes stream's `--audit-routes` finds every route live.
2. **Behavioural:** the evaluator's route-locked check reads "configured and observed" (z > 3) in the shots where the target is on screen.
3. **Meaningful:** judged per shot from frame sheets and the evaluator's visibility at the delivered size. The response must organise the frame (a wave read as a wave, a hero answering its layer); it must not be noise, and not everything at once.

## 6. The other brief items
| Item | Plan | Check |
|---|---|---|
| Water tears (§1) | The water stream's `tears` family, wind-coupled, in selected shots only: s02-like pool shots and the river travels, never the whole surface | Evaluator stills at the delivered size; the owner's reference side by side |
| Camera (§2) | The camera stream's filtered follow reference on every follow shot | Stability: pitch and yaw high-frequency ≤ 0.1°, eye vertical high-frequency ≤ 2 cm |
| Wind (§6) | Direction from the valley's axis; gust and turbulence set so ferns and grass visibly move; strength on section energy; no storm | Evaluator motion measures, and sheets |
| 4K offline (§13) | render-post.md's configuration, as changed by the render stream: offline tier, 3840×2160 ×2, 4 cascades, shadow range 160 (300 on wides), terrain LOD off and view distance 1000, fog march on, rigs at every frame | The render log's three lines; evaluator stills at 4K |
| World edge | The valley's ends closed with ridge features in scene data, with aerial perspective from the sky's radiance (render stream) | No edge visible in any wide |
| Post (§14) | AgX, bloom threshold 0.9 and knee 0.7, depth layers, grain 0.035, vignette 0.35, anamorphic 0.12; resolution-stable radii (render stream) | Evaluator before/after on the same shots; the palette checked against GV2 |

## 7. The loop
For each category, in the order audio reactivity, characters, UFO events, pacing, camera, water,
wind, post:
1. Change the generator (`tools/gv3/*`).
2. Render the affected shots as clips.
3. Submit them with `--video-start` on a session track per category.
4. Read the findings and the comparison against the previous iteration.
5. Repeat until the check in the tables above passes. Record each iteration in `04-iterations.md`.

A full 960×540 preview and a full-film evaluation close each round. The baseline is the first
pass's delivered 1080p final, evaluated whole.
