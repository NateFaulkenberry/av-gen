# Art Pass 2, M3e: progress

The notes are `47-pass2-m3e-owner-notes.md` (§1-15, plus the Evaluation rule). M3e builds on 64c1fd94. It is an art
checkpoint: clips and stills; the suites and the full render come after approval.

Media is in `~/Desktop/av-gen-review/101-strange-attractor/pass2-m3e/`.

| § | Note | State |
|---|---|---|
| 1 | Sea exit portal confined to the bay | done: the base, bay strips and seafloor are targets; field radius 45 -> 150 m, opening 22x11 -> 36x21 m (beyond the bay's 30x15 half-size); the frame's light strips are their own glb (`bay-strips`) and die 138.6-140.8 s; corruption light 1500 -> 500 (the white-out before the crossing) |
| 2 | Pyramid long shard projectiles | done (ADR-1904): slivers skipped, shard size capped |
| 3 | Mars mound hides the pyramid | done: a corridor of pads (34 m apart, radius 58 m) eases the fly-over to the plain; a sightline check finds no terrain over any line to the pyramids |
| 4 | Visible tiling | done (ADR-1905, unit + GPU parity tests): canyon ground, Mars plain sand, the pyramids' drifts (split into `<name>-sand.glb`, drawn through `marsDrift`, ADR-1572), the reef |
| 5 | Mars arrival clips a bulge | done: the exit is out of the butte's steep face at 95 m (it was in the talus apron); the first knot is 30 m over the ground |
| 6 | Earth aura, glitch and datamosh | done: the pixel corona replaced by `earth-aura` (a radial falloff card, cyan-white at the limb to violet, scaled so its 1.0 R is the limb as seen); the beat pulse kept; RGB split copies on the beat, a torn copy (scanline tears, displaced blocks) on strong transients, two mosh trails on the low onsets; the Earth's own RGB echoes kept |
| 7 | Dusk arrival across the river, one path past the camp | done: out of the far wall (d 40, lat -53.5, 12 m), across the river, past the camp on its river side, up to the cliff portal; one paced path; the camp aim a 3.6 s glance |
| 8 | Night camp: more light (optional after §11) | superseded by the brief (`49-`); torches not added yet (§11: quality first) |
| 9 | Night camp: shadows and lighting quality | first pass done (9021ea67): ADR-1906/1907 + 48- A1-A6; the blob gone, the fire casts, Sasquatch B moved out of the fire (2.0 -> 2.9 m), thin smoke, craft spill tamed; the Gray's flat white is the hero agent's (model) |
| 10 | The Gray talks across the fire | moved to the hero-asset agent (coordinator) |
| 11 | §9 first, torches only if still dark | (rule) |
| 12 | Night arrival lower, one pass by the camp | done: the exit at 70 m (was 86) and d 54 (was 46); one paced path past the camp (5-11 m from the fire) into the glide; the aim a 3 s glance |
| 13 | Reef geometry and texture, the black streaks | done (to verify on render): the streaks were the canyon program's world-height strata bands on steep faces; the reef has its own program (photoscanned rock + sand, hex-tiled, algae, coral, caustics); softer relief and a finer mesh |
| 14 | Sea portal sooner | done: field 260 -> 770 m (~139 -> ~130 s), tear 130 -> 360 m (~141.6 -> ~137.5 s, after the last sortie at 136.4 s); the strips die 133.6-135.8 s |
| 15 | Pyramid portal later (after the camera passes the rover) | done: field 1700 -> 611.1 m (the camera's distance from the focus at the knot over the rover, less 15 m), tear 520 -> 411.1 m |

Research: `48-night-lighting-research.md` (coordinator's agent); its top-5 are part of M3e (§8/§9 and the night river).

Tools: the ground sampler is a temporary `[.sample]` test that is never committed (`sample_ground.py`); the Mars checks
are in the session scratchpad (`marscheck.py`).

## Night camp (brief `49-`, this lane: Phases 1.3, 3.3, 4.3, 5, 7, 8)

- Phase 8.1 baseline: the owner's reference camera is the journey camera at about 111.1 s
  (`pass2-m3e/08-night-camp/00-baseline-m3d-111.1s.png`, beside the reference). Stills are rendered from a 2 s run-up,
  so the particles are warm (`frames.sh` in the scratchpad).
- Engine: ADR-1906 (the shadow budget and perspective bias), ADR-1907 (the key's shadow per look).
- Scene: the night look's research values; the river's water at every hour (f0 0.06, roughness 0.085, ripple 0.48,
  streak 0.8, rays 6, glints 8, sparkle 0.3, specular 2, sky mirror 0.35, transmission 0.5).
- Open: the water's local-light streaks (B7/B8), PCSS for local lights (B4), fur wrap (B9), the Purkinje shift (C1).

## The hero agent's audit (H1), fixed here

- `build_teepee.py`: the logs lay flat (an imported glTF object is QUATERNION; its Euler was ignored); decimated to 8%.
- `build_sasquatch.py`: every `sasquatch_fur*` material alpha-tested; sasquatch-b patched as a copy.
- The Gray matte (metallic 0, no emission), as a copy (`pass2/patch_glb_materials.py`).
- Renderer gaps G1-G6 are noted for Phase 7; not taken in this checkpoint.

## The checkpoint (media in `pass2-m3e/`)

- Engine: ADR-1904 (no projectile shards), 1905 (anti-tiling texture ops), 1906 (shadow budget and perspective bias),
  1907 (the key's shadow per look). Unit and GPU tests for each; the full suites were not run (art checkpoint).
- Renders: 26-150 s at 1080p30, supersample 2, in three passes after fixes (the Mars arrival, the night exit).
- Arrivals checked frame by frame: the Mars arrival clipped a bulge in the butte's talus (fixed); the first night exit
  was inside the cliff and then swung back to it (fixed: in front of the face, the first knot out along the exit at the
  arrival's speed). Moon, dusk, ocean and dawn arrivals clear.
- Critic (preview, session `strange-attractor-m3e`): `pass2-m3e/09-critic/critic-m3d-vs-m3e.md`. The fast scene critic
  does not apply: the AV Gen adapter reads 0 entities and 0 cameras from this scene's glTF nodes.

## Open

- The sea exit's last half second before the crossing is still a white-out (146.0-146.4 s) with two frames of a disc
  of the dawn canyon in it. It is not the corruption's light (80 vs 500 made no difference); it was there in M3d.
- The ocean camera picks up a wobble (critic) on the reshaped reef: the path's heights are over the new ground.
- The water's local-light streaks (B7/B8), PCSS for local lights (B4), fur wrap (B9), Purkinje shift (C1), and the hero
  agent's G1-G6.
- Torches (§8): not added; the quality pass came first (§11) and the wide night shots read now.
