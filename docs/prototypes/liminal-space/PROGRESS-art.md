# Liminal Euclidean World: the art agent's progress notes

*Kept current after every step so a cold successor can resume. The engineering agent's notes are `PROGRESS-eng.md`;
these are the art side's. Newest state first.*

## RESUME HERE: ART PASS 3 (2026-10-01, the owner's `03-art-pass-3-addendum.md` governs)

**Status (20:32): take 2 is the final. Take 3 (C10 and C14 only, approved by the coordinator) is rendering and
replaces the final only if it measures no worse. After it: hand back. Nothing else is planned for pass 3.**
- **The final now (take 2: `8f2c5e77`, pin `5dd835a4`):**
  - `~/Desktop/av-gen-review/24-liminal-space/pass3/all-you-got-pass3.mp4`: 1920x1080, 30 fps, h264 q90, the
    song, 258 s; render exit 0, 0 GPU errors.
  - Copies: `$S/liminal3/all-you-got-pass3-take2.mp4` and `$S/liminal3/all-you-got-pass3-take1.mp4`.
  - `pass2_av` (`$S/liminal3/av-final`): equal or better than take 1 everywhere. 16 of 18 BIG CLAPs are
    unmistakable: C12 is fixed, C10 is visible, C14 is not read.
- **Gate:** `tools/gpu-lock.sh build/release/tests/avgen_tests "[sdf],[liminal]"` exited 0 after take 2 (41,918
  assertions in 102 test cases). The test binary is unchanged since 18:46.
- **The Critic, take 2** (job `job_1a0f9f83629cee479`, the same 64 shots as take 1, so like for like): 0.93 overall.
  - Composition, colour, pacing and creative intent: 1.0. Musical sync 0.99. Motion 0.68. Technical 0.77.
  - Take 2's "smooth travel" did not change the picture: the per-shot stability numbers match take 1's to three
    digits. The remaining "wobble" is not the camera. Image shifts measured in clean windows hold 20-26% of their
    shake energy at the quarter note: that is §20's geometry breathing (the walls' 0.6% swell). The 10 Hz band holds
    only 1-2%.
  - The two "high" findings are clipped highlights, both by design: the door's light fill at 198.7 and the dawn
    white-out at 248.
- **Take 3** (`3c8cd9eb`; `$S/liminal3/final3.sh`, log `$S/liminal3/final3.log`):
  - C14: a cold flash light (`stuFlash`) on the beat, and the ceiling's lines flare. The camera is looking across
    the black side of the study there, so the old dropout read as nothing.
  - C10: the corruption carries across the cut. The bathroom arrives misregistered and with a flash, and the
    pixels smear.
  - The 112-140 s preview measures both unmistakable (C10: colour z 3.2; C14: lines z 6.1). The script renders,
    measures into `$S/liminal3/av-take3`, and runs `$S/liminal3/compare_av.py` (no BIG CLAP verdict worse, no
    section's lock more than 1 dB lower). Only then does it move take 3 into place. The log ends with "replaced the
    final with take 3" or "kept take 2".

**Generate, then check (CPU, about 80 s):**
```sh
S=/private/tmp/claude-501/-Users-natefaulkenberry-Documents-GitHub-av-gen/fed9412c-8e5e-42c0-a62b-e703644796ad/scratchpad
export AVGEN=$S/liminal3/bin-validate/avgen        # a copy of the pinned binary (5dd835a4: the validator, rim, static, world wave)
python3 tools/liminal/make_all_you_got_pass3.py    # writes examples/liminal/all-you-got-pass3{,.scene,.rig,.shots}.json
                                                   # and all-you-got-pass3.validation.txt
./build/release/src/avgen --project examples/liminal/all-you-got-pass3.json --audit-routes /tmp/a.json 2>&1 | grep -i "warn\|error"
$AVGEN --trace-jumps examples/liminal/all-you-got-pass3.json --json $S/liminal3/jumps3.json   # positional jumps
```
- **Must print:** "words not seen when they appear: 0", an empty "words placed without a clear wall" list, and 8
  "swap ... (both off screen)" lines.
- **Validator:** 0 errors, 12 warnings, 2 notes (`examples/liminal/all-you-got-pass3.validation.txt`). The
  warnings are accepted. Five are on four words: a few cm inside the 0.15 m edge margin, and one tree word 0.11 m off
  its wall. Seven are camera paths passing a shell at 3-12 cm where they go through a door, a window or the stairwell
  on purpose. The two notes are the tree's
  mirrored rooms, which it does not check.
- **Route audit:** 0 warnings.
- **Jump trace:** only authored moves remain (the riser's sideways tremor, the falling houses, the split ceiling).

**Render (GPU, through the lock, with the pinned engine):**
```sh
tools/gpu-lock.sh $S/liminal3/avgen.sh --headless --project examples/liminal/all-you-got-pass3.json \
    --render ~/Desktop/av-gen-review/24-liminal-space/pass3/all-you-got-pass3-preview.mp4 --range 0:258 --size 960x540 \
    --fps 30 --codec h264 --quality 80 --particle-warmup 30
```

**The pass 3 modules** (`tools/liminal/`; pass 2's modules are untouched):
- `figure3.py`: the posed mannequin (faceted head, sharp blocks, IK). Its parts give its march bounds.
- `tableaux3.py`: 14 poses, each checked against its anchor: `python3 tools/liminal/tableaux3.py` exits 0.
- `props3.py`: new props. `rooms3.py`: the house (ground floor, basement, upstairs), every prop tagged.
- `intro3.py`: the neighbourhood.
- `film3.py`: Builder3, which handles:
  - glides at a smooth speed;
  - gazes interpolated by angle;
  - figure swaps only off screen (walls occlude);
  - clear-wall masks for words.
- `film3_intro.py` (bars 0-16), `film3_house.py` (17-41), `film3_upper.py` (42-66), `film3_late.py` (67-end).
- `make_all_you_got_pass3.py`: the entry point. `space-rules3.json` holds the pass 3 validator rules.
- `preview3.py tableaux|rooms`: stills projects.

**The engine (all delivered; the art side is pinned to `5dd835a4`):**
- the spatial validator (ADR-1051), used through `tools/liminal_space.py` (`instrument_kit`, `tag`, `validate`);
- the rim (ADR-1052): `sdf/<o>/look/rim/intensity|color|power`, and `surface/<k>/rim` (the mannequin's head);
- the screen static (ADR-1054): `surface/<k>/static`, `look/static/cell|rate|roll` (every SCREEN surface);
- the world wave (ADR-1055): `post/wave/*`, driven by `film3_late.colour_wave()` at 198.3-199.9.

To re-pin to a newer engine: `B=$S/liminal2/bin-<sha>`, copy `build/release/src/avgen` into it, run
`git archive <sha> shaders | tar -x -C $B`, then point `$S/liminal3/avgen.sh` at it (or set `AVGEN_PIN=$B`).

### The pass 3 film (owner bars; `t()` from `pass2_grid.py`)

| bars | seconds | place | what |
|---|---|---|---|
| 0 | 0-2.20 | black | the count-in: one road dash stamps down per beat (construction begins) |
| 1-4 | 2.20-11.01 | the street | 1.1 the ground and the road burst out; houses slam down on the quarter notes |
| 5-8 | 11.01-19.82 | the street | the bass: lamps, fences and trees pop in on the eighths; a skyline rises with the sustained note |
| 9-16 | 19.82-37.43 | the street, then the hero house | the riser: the camera accelerates at the house; parts mutate and fly; 13-16 the windows flash with the chop; 16.4 the world freezes and dims, the camera keeps going |
| 17.1 | 37.431 | the front window | the camera crosses the wall plane ON the downbeat: the first interior frame is frame 1123 (37.433 s at 30 fps). The audio's attack measures 37.41-37.45 s |
| 17-32 | 37.43-72.66 | the living room | release and verse 1, a+b: tableaux Thinker (armchair), lying on the couch, head in hands, at the window; the camera sweeps the room |
| 33-36 | 72.66-81.47 | the kitchen (through the living room's back door) | at the kitchen table |
| 37-40 | 81.47-90.28 | the hall and the stair | the camera CLIMBS the stair with IT'S STEPS IN A PROCESS, LET IT GO on the risers; the top is a cliff edge into black |
| 41 | 90.28-92.48 | the void | over the edge, falling; LET IT GO falls with us |
| 42-49 | 92.48-110.09 | the basement | LET IT GO on its surfaces on the eighths; 49.4 a whip up |
| 50-65 | 110.09-145.32 | upstairs | verse 2 tableaux: in bed, at the bathroom mirror, the toilet, at the desk, at the window; C09-C16 corruptions |
| 66-end | 145.32- | as pass 2 | the roof rips off, the tree of rooms, the gallery (no head turn), the dance (ending at the front door), the colour wave through the house at 90.3, the open, the crash, the dawn (head fixed), the collapse back to the road dashes |

### Pass 3 checklist
- [x] M1: the kit: the faceted IK mannequin, the new props, entity tags, the pose checker, and stills (`324d1144`).
- [x] M2: the house `rooms3.py`, which validates with 0 errors (`817b282c`, `7cb584db`).
- [x] M3: `make_all_you_got_pass3.py`, with no breathing and no hops, and pass 2 adapted from bar 67 (`8c82d5b6`).
- [x] M4: the intro written (`3e1adc00`); it has not been seen rendered yet.
- [x] M5: the living room sweep with 4 tableaux, and the kitchen (written, not yet seen).
- [x] M6: the climb (one word a riser), the fall, and the basement (written, not yet seen).
- [x] M7: verse 2 upstairs, with 6 tableaux and 8 corruptions (written, not yet seen).
- [x] M8: corruption at transitions, claps, lyrics and peaks; static screens; the world wave; the coda on the
  road's dashes.
- [x] M9: the validator (0 errors), fixes, previews v1-v3, `pass2_av.py`, the Critic on v3 (0.93), refinement.
- [x] M10: the final 1080p render with the song (take 1; take 2 queued); `[sdf],[liminal]` exits 0; the report goes
  in the hand-back.

## RESUME HERE: ART PASS 2 (2026-10-01, the owner's `02-art-pass-2.md` governs)

**Where pass 2 stands (13:05): DONE.**
- **The final film** is `~/Desktop/av-gen-review/24-liminal-space/pass2/all-you-got-pass2.mp4`.
  - 1920x1080, 30 fps, h264 quality 90, with the song, 258 s.
  - Rendered from `82f8edba`, with a contact sheet beside it.
  - The results are in `PASS2-PLAN.md` §12: 17 of 18 BIG CLAPs unmistakable, quarter lock +10.7 to +23.6 dB,
    a Critic reactivity z of 7.8.
- **The `[sdf],[liminal]` gate** exits 0.
- **The coordinator was told** the final render finished at 12:59, so the engineer's render suite could run.
- **Next, if anyone continues** (small, not yet done):
  1. The sun rises a little late. Only its halo shows before the white wash, so raise its rise a second
     earlier: `sdf/sun/node/sunAt` in `film_build2.py`.
  2. Make C07 unmistakable (it measures z 2.2) with a light burst, as C02 got.
  3. If wanted, re-render with `tools/liminal/render_pass2.sh final`, using `AVGEN=` the pinned engine.

- **Owner numbering everywhere:** owner bar N = analysis bar N+1. `tools/liminal/pass2_grid.py` is the one grid.
  It writes `tools/liminal/all-you-got.pass2.json` (sections, 18 BIG CLAPs in seconds, events, 274 lyric
  entries); `python3 tools/liminal/pass2_grid.py --md` prints the tables.
- **Measure a render** ("does the visual response read?"):
  `python3 tools/liminal/pass2_av.py --video <mp4> [--video-start s] --out <dir>`: the quarter and eighth beat
  lock per section (pure noise reads about +4 dB with a share under 0.1; the target is above +12 dB with a
  share above 0.3), the BIG CLAP z-scores (the target is z >= 3), and the boundary changes. Pass 1:
  `$S/liminal2/av-pass1/`.
- **The Critic with the pass 2 criteria:**
  `python3 tools/liminal/critic_pass2.py --project <p.json> --video <mp4> --out <dir> --submit --label <l>
  --track film`. It fixes the modulation shape, which un-breaks the reactivity analyzer. The pass 1 run is job
  `job_1a0f80d8694ad5bee`: complete, "no measurable response" (z 0.2 over 268 events).
- **Scratch:** `S=/private/tmp/claude-501/-Users-natefaulkenberry-Documents-GitHub-av-gen/fed9412c-8e5e-42c0-a62b-e703644796ad/scratchpad`,
  then `$S/liminal2/`.
  - The Python venv with numpy, scipy, matplotlib and faster-whisper is `$S/venv/bin/python`; the system
    python3 has numpy only.
  - The clap check (`claps2.py`) and the hook-word timing (`words.py`, `vocenv.py`) are there.
- **Review media:** `~/Desktop/av-gen-review/24-liminal-space/pass2/`.

### Pass 2: how the film is built (read this first)

- **The generator:**
  - `python3 tools/liminal/make_all_you_got_pass2.py` writes `examples/liminal/all-you-got-pass2{,.scene,.rig}.json`;
  - `--kit` writes a stills project of every world.
- **Modules,** all in `tools/liminal/`:
  - `kit.py`: about 40 primitive props, the faceted mannequin, room shells;
  - `rooms2.py`: the living room, bedroom, kitchen, hallway, study, bathroom and gallery;
  - `outdoor2.py`: the terrain, trees, mountains, lanterns, stair, summit, sun, the tree of rooms, the sky
    furniture;
  - `intro2.py`: the seed and the house;
  - `film2.py`: shots as journey chapters with exact cuts, on a replica of the engine's spline, plus tracks,
    routes, events and the palette;
  - `film_build.py`: bars 0-40;
  - `film_build2.py`: bars 41-115 and the tail.
- **Track order matters.** A `replace` track overwrites everything before it in the list, and it holds its
  first and last values outside its keys.
  - So the constant base tracks are written first, then the builder's `add` tracks.
  - Never add a second replace track on a target that is already keyed; merge them. A late single-key track
    once froze the seed's core for the whole intro.
- **The pinned engine.** The engineer edits shaders in the shared tree, and the binary loads shaders from the
  source tree.
  - So render with `$S/liminal2/avgen.sh`: a copied binary plus `git archive <commit> shaders`, exported via
    `AVGEN_SHADER_DIR`.
  - It is currently `bin-d7c5e75b`, which has all seven systems.
  - To re-pin, run `B=$S/liminal2/bin-<sha>`, copy `build/release/src/avgen` into it, run
    `git archive <sha> shaders | tar -x -C $B`, and edit `avgen.sh`. Only do it when no source file is newer
    than the binary.
- **Validate:** `./build/release/src/avgen --project examples/liminal/all-you-got-pass2.json --audit-routes /tmp/a.json
  2>&1 | grep -i "warn\|error"` must print nothing but the "0 warning(s)" line.
- **Preview:** `tools/gpu-lock.sh $S/liminal2/avgen.sh --headless --project examples/liminal/all-you-got-pass2.json
  --render <out.mp4> --range 0:258 --size 960x540 --fps 30 --codec h264 --quality 80`.
  - It renders at about 63 fps, so about 2 minutes plus compiles.
  - **Stills** need motion blur off: at a low fps each frame's "previous frame" is seconds earlier, and the blur
    smears it.
- **Measure:** `python3 tools/liminal/pass2_av.py --video <mp4> --out <dir>`.
  - v1 (`$S/liminal2/v1-full.mp4`) measured 17 of 18 BIG CLAPs unmistakable (C02 visible), and quarter lock of
    +11 to +26 dB in most sections.
  - The weak sections were the stair (bars 99-106), bridge 2 and bridge 1, now being fixed.

### Pass 2 checklist

- [x] Phase 1 analysis and Phase 2 critique of pass 1 (`PASS2-PLAN.md` §1-2).
- [x] The timeline re-mapped to owner numbering, with every BIG CLAP in seconds and the lyric times
  (`pass2_grid.py`, `PASS2-PLAN.md` §4-6).
- [x] The art needs sent to the coordinator. The engineer delivered all seven systems:
  - the beat grid (ADR-1045);
  - text (1046);
  - the line look (1047);
  - breathing (1048);
  - mosh (1049);
  - the sweep (1050);
  - object animation (a route recipe).
- [x] The world kit and every world, as data (commits `c471a4cb`, `5b740615`).
- [x] The whole film assembled: 36 shots, 18 BIG CLAPs each with its own treatment, 256 words. v1 preview at
  `$S/liminal2/v1-full.mp4`.
- [x] Fixes after v1, all in code and committed (`a0aa13ee` to `26a24f59`):
  - the track order;
  - the cursor reset;
  - the stair path;
  - the summit;
  - the terrain ribs;
  - the sky, stars and sky furniture;
  - fireworks;
  - the riser's escalation;
  - a typeface per section;
  - the mannequin's head turn on GOT?;
  - the gallery orbit inside its room;
  - the growing trunk and canopy;
  - verse 2's clutter.
- [x] **Every word placed where the camera looks.**
  - `Builder.word_at` casts a view ray at the word's moment onto the room box, the terrain or open air.
  - The generator prints "words not seen when they appear", which must be 0 (it is 0 of 254).
  - `--clearance` checks every shot's eye against its SDF objects on the CPU (`tools/liminal/sdf_eval.py`).
    About 0.09 m in a doorway is the CSG bound, not a collision.
- [ ] **Next, when the GPU is free.** (The engineer's full suites held it from 12:08 into the afternoon.)
  - A flipbook: `$S/liminal2/flipbook.sh <tag> 1 0.5:257.5` waits for a free GPU, then writes 1 fps stills with
    motion blur off, and sheets in `$S/liminal2/fb-<tag>/`.
  - Then a preview, `tools/liminal/render_pass2.sh preview` with `AVGEN=$S/liminal2/avgen.sh`.
  - Then `pass2_av.py` and the Critic, then iterate.
- [x] The full render: `~/Desktop/av-gen-review/24-liminal-space/pass2/all-you-got-pass2.mp4`, 1920x1080 at 30
  fps, with the song.
- [x] `tools/gpu-lock.sh build/release/tests/avgen_tests "[sdf],[liminal]"` exits 0 (41,767 assertions in 85
  cases). The report's text goes to the coordinator in the hand-back.

## RESUME HERE (Phase B: building and rendering the video, 2026-09-30 night)

The art agent owns the worktree alone in Phase B (the engineer handed back at `2083bab4`). Deadline: a full,
art-directed render by the morning of 2026-10-01; safety net at 05:30 (render the best committed state as
`all-you-got-WIP-<sha>.mp4` with a README).

- **The generator:** `tools/liminal/make_all_you_got.py` writes `examples/liminal/all-you-got{,.scene,.rig}.json`
  from the section map. `python3 tools/liminal/make_all_you_got.py` prints the chapter table (swap times, local
  distance ranges against each path's length) and a check list of where the camera is at the plan's moments.
- **Validate without the GPU** (catches scene load errors, unknown targets, dead tracks):
  `./build/release/src/avgen --project examples/liminal/all-you-got.json --audit-routes /tmp/audit.json 2>&1 | grep -i "warn\|error\|loaded"`
  must end with `0 warning(s)`. A scene that fails to load silently renders the default orb scene.
- **SDF limits that bite:** 96 nodes per tree, at most 8 children per union (the generator's `union` nests),
  8 nested point ops. `count_nodes()` asserts them per world.
- **Stills:** `tools/gpu-lock.sh ./build/release/src/avgen --headless --project examples/liminal/all-you-got.json
  --render <dir> --format png --range 1:253 --size 640x360 --fps 0.25` (64 frames, one every 4 s).
- **The GPU is shared** with the Sonic agent, whose renders hold the lock for 5-15 minutes at a time. Queue GPU
  work in the background and author meanwhile.
- **Scratch:** `/private/tmp/claude-501/-Users-natefaulkenberry-Documents-GitHub-av-gen/fed9412c-8e5e-42c0-a62b-e703644796ad/scratchpad/liminal/`
  (the Sonic agent uses the same scratchpad root; keep to the `liminal/` folder).

### Phase B status

- [x] Generator v1 (commit `88b15d6d`): seven worlds, nine chapters, the fitted walk, K0-K12, thresholds of
  light, sparse routes scaled by a timeline `coupling` source.
- [x] Stills across the song (`scratchpad/liminal/s2`, one every 4 s): the structure works end to end.
  Calibrated (`15709f1d`): lamp panels plaster when dark, hall air, quieter teal, a small sun, denser void.
- [x] Short test renders in `~/Desktop/av-gen-review/24-liminal-space/tests/`: `test-a-intro-hall.mp4`
  (0-62 s) and `test-b-letgo-verse2.mp4` (86-118 s), 1280x720.
  - `liminal_critic.py temporal`: 0 snaps, 0 flicker; aggression-activity correlation 0.90 (A); one minor
    colour-too-fast at the riser's white-out; "let it go" 36 % below its neighbours (good contrast).
  - Creative Critic (job `job_1a0f54d3da394134a`, preview, test A): clipped highlights at the thresholds (47 %
    of pixels), camera wobble (medium), shimmer in the hall (the motes read as snow), flat low-contrast intro;
    audio reactivity not evaluated (an error in the Critic's analyzer).
  - Revised (`cc43f1b3`): sun patches in the hall (SDF soft shadow), finer motes, windows glow, the riser's
    light across four bars, the stairhead looks out at the figure and the void, thresholds as luminous haze
    (fog x3, +1.7 EV) instead of clipped white, contrast 1.12, less bob.
- [x] Full render v1 (`cc43f1b3`, 22:33-22:45, 12 min at 10.6 fps):
  `~/Desktop/av-gen-review/24-liminal-space/all-you-got-v1.mp4`, 1920x1080, 30 fps, h264 q90, the song muxed.
  - temporal: 7 snaps (the chapter swaps; one major at the grow room), aggression-unanswered at bars 5-8 (the bass
    pulse entry), "let it go" only 8 % below its neighbours, 21 colour moves (minor findings at thresholds).
  - Critic (job `job_1a0f55a9841d09e0f`): camera wobble in 41 % of shots (the guard pushing at the enfilade's
    screw seams, plus the bob), clipped highlights at the thresholds and the sun, flat low-contrast intro.
  - My viewing: a giant half-disc sun (sunSize is radians), the mannequin seen close twice (stairhead, top), grain
    on the hall's door jambs (the SDF soft shadow), a lavender hall, the look-down at bar 108 hitting the landing.
  - **Lesson:** interior light is the procedural sky's IBL, so the palette's zenith/horizon/ground roles are the
    ambient colour of each place; the rig's ambient does nothing without an ambient-role light.
- [x] Final pass (`ed1dce5d`): thresholds as thick bright air (fog x7, +1.1 EV), guard radius 0.14, bob 3-7 mm, a
  0.7-degree sun inside the final doorway, daylight from behind, the figure far down the stair and 4 m away at the
  top, warm hall light, sun patches on the hall floor, a dimmer and emptier let-it-go, the first corridor visible
  at bar 108, a look round at the release.
- [x] v2 (`ed1dce5d`, `all-you-got-v2.mp4`): snaps all minor (6), "let it go" 15 % below its neighbours (the major
  finding gone), clipping 11 -> 6 high, technical quality 0.55 -> 0.70. Still: the hall's dust read as snow, the
  walking figure rendered pale (its tint does not apply to a keyed or journey-anchored GLTF node; a static figure's
  does), the floor sun patches read as rugs.
- [x] v3 (`8184a73d`, `all-you-got-v3.mp4`): no hall dust, a standing figure in a doorway two rooms on, the
  stairhead figure on a ledge, the tremble only in the rough bars, the corridor breathing from bar 6. The clearance
  test passes. The Critic's wobble rose to 36 findings: the swaps, plus 10 Hz velocity steps from the linear 0.1 s
  distance keys during accelerations. The grow room's motes were drawing in the hall (they belonged to no chapter);
  now gated.
- [x] v4 (`80491f03`, `all-you-got-v4.mp4`): the aggression-unanswered finding at bars 5-8 is gone (the walk is
  carried by the bass pulse); snaps 4, all minor; the Critic's wobble unchanged (38).
- [x] **The wobble was real, and it was the world offsets.** With every timeline track frozen and no routes, the
  image still jumped horizontally by up to 20 px at 1080p at the end of the open chapter (the sun disc tracked
  frame by frame). Moving that world from z = 6000 m to the origin made it rock steady. The jitter grew with the
  offset (the corridor at 0 m had little, the open at 6000 m the most). All seven worlds now sit at the origin
  (`95d23ae0`); inactive chapters' nodes and lights are hidden, so overlapping is free. **Engine finding for the
  coordinator:** something in the camera or reprojection path loses precision with a camera thousands of metres out.
- [x] Also on the way: every chapter path carried straight on past its end (the gaze no longer reaches the closing
  segment), "let it go" darker (value 0.74) with a slower look back, the sun centred in the final doorway, the grow
  room's gold no longer clipping. The clearance test walks each chapter only as far as the walk goes; it passes.
- [x] Final render v5 (`95d23ae0`, kept as `all-you-got-v5.mp4`): the Critic's wobble 38 -> 23 (the rest at the chapter
  swaps; jerk elsewhere 28-125 against 200-550 before), motion 0.52 -> 0.67, lighting 0.83, technical quality 0.69;
  "let it go" 15.1 % below its neighbours (passes). One new major snap at 198.8 s: the fill's light ramped in one beat.
- [x] **FINAL** (`60594dfa`): the fill's light over two beats. `~/Desktop/av-gen-review/24-liminal-space/all-you-got-final.mp4`,
  1920x1080, 30 fps, h264 q90 + AAC, 253.8 s (12.5 min to render at 10.1 fps).
  - temporal: 4 snaps, all minor (the chapter swaps); 0 flicker; colour 3 minor; "let it go" 15.1 %, the break 25.8 %;
    one major left, `breakdown-contrast` on Verse 1, a false positive: the rule matches "let it go" in the section's
    name (a lyric), and verse 1 is not a breakdown.
  - `tools/gpu-lock.sh build/release/tests/avgen_tests "[sdf],[liminal]"`: exit 0, 41,725 assertions in 82 cases.
    No engine code changed, so the full suites were not required.
  - Analyzer outputs and contact sheets: `~/Desktop/av-gen-review/24-liminal-space/analysis/renders/`.
- [ ] The report: its full text goes to the coordinator in the hand-back (the harness refuses report files here).

### How to analyse a render

```sh
S=/private/tmp/claude-501/-Users-natefaulkenberry-Documents-GitHub-av-gen/fed9412c-8e5e-42c0-a62b-e703644796ad/scratchpad/liminal
V=~/Desktop/av-gen-review/24-liminal-space/all-you-got-v1.mp4
python3 tools/liminal_critic.py temporal --video $V --audio ~/Desktop/"All You Got.wav" \
    --sections tools/liminal/all-you-got.sections.json --video-start 0 --out $S/temporal-v1
python3 tools/liminal_critic.py inputs --project examples/liminal/all-you-got.json \
    --sections tools/liminal/all-you-got.sections.json --video $V --video-start 0 --out $S/critic-v1
cd ../creative-critic && .venv/bin/critic submit --inputs $S/critic-v1/inputs.json --mode preview --session liminal --track v1 --wait
$S/sheet.sh $V $S/v1-sheet.png 5 384 1 10 20 ...   # contact sheets of chosen seconds (no labels: ffmpeg has no drawtext)
```

## Status (Phase A complete, 19:20 on 2026-09-30; handed back to the coordinator)

- [x] Brief and addendum read in full.
- [x] `ART-RESEARCH.md`: ten rules, sources with tags ([P]/[S]/[SR]/[M]), what is adopted and rejected, and what
  carries over from the Procedural Space POC's look.
- [x] `SONG-ANALYSIS.md`, with `tools/liminal/all-you-got.sections.json` and plots in
  `~/Desktop/av-gen-review/24-liminal-space/analysis/` (01-05 and `bars.json`).
  - 116 bars; 109 BPM to bar 75, 111 BPM from bar 76 (165.14 s), a step.
  - C Dorian (Cm-Cm-Gm-F), lifting to F at bar 67; the final loop is a descending Ab-G-F-F.
  - Three texture families: sparse (intro, break, "is that all you", outro), the C groove, and the F world
    ("feel it grow" previews the ending).
  - The "let it go" drop is one bar (42), then a stripped groove to bar 48; the only drums-out passage is bars
    74-75. Vocal lines are placed, the hook and verse 2 are confirmed on short windows.
- [x] `DIRECTOR-PLAN.md` covers:
  - the idea and the film at a glance;
  - the world, the four motifs (the beacon, the stair, the figure, the sun patch) and the wandering camera;
  - the colour script K0-K12 (the sky withheld until bar 92);
  - light, objects and effects;
  - the film section by section, and the timing sheet (64 timed camera events);
  - the sound-to-behaviour vocabulary and the transitions (light thresholds);
  - the climax strategy and the shot tests;
  - the cell sheet (six designs with dimensions);
  - **the ranked needs from engineering (17)**, the risks and fallbacks, and a LIVE note.
- [x] The colour script as data (`palette_keys`, `palette_timeline`, `intent`, `shots`, `synch_points` in the
  sections file) and as a picture: `tools/liminal/colour_script.py` →
  `~/Desktop/av-gen-review/24-liminal-space/plan/colour-script.png`.

- [x] Calibration notes against the engineer's first example (`DIRECTOR-PLAN.md` §12b), and the needs statuses
  checked against ADR-1040 to 1043 as landed (§13).

Phase A commits, oldest first:
- `fb6fd9e7`
- `10705f89`
- `ab58d5d0`
- `0796ebf0`
- `1842620e`
- `eecaaf9c`
- `68540563`
- `12a8e3ec`
- `6fc4359b`
- `c9c63462`
- `22f3ce1a`
- `a9b688fd`
- and this notes update.

## Next (Phase B, a new handoff: implement the video on the engineer's infrastructure)

1. Read `ENGINEERING.md` and `PROGRESS-eng.md` for what landed. The engineer's vocabulary is `tools/liminal_sdf.py`
   and its example generator is `tools/make_liminal_example.py`; `examples/liminal/` is theirs.
2. Check the ranked needs (DIRECTOR-PLAN §13) against what landed, especially:
   - chapters (several worlds along one journey);
   - more than one material per world;
   - a look-at blend;
   - the sky;
   - the figure.

   Take the §14 fallbacks where a need is missing.
3. Write a generator `tools/liminal/make_all_you_got.py` that emits the music-video project from the sections
   file: cells A, B, C, D, P and E; journey keys from `shots`; palette states from `palette_keys` (converted from
   sRGB hex to LINEAR RGB); palette position from `palette_timeline`; routes with `spring` and `integrate` per §8.
4. Do short test renders of the four moments that carry the film: bar 18 (the hall), bar 42 (the stop), bars
   67-76 (the growth and the loop's start), and bar 92 (the release). Run them through the GPU lock and put them
   in `~/Desktop/av-gen-review/24-liminal-space/`. Then do the full render and the analyzer passes.
