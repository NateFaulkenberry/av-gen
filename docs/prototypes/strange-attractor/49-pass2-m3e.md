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
| 6 | Earth aura, glitch and datamosh | to do |
| 7 | Dusk arrival across the river, one path past the camp | to do |
| 8 | Night camp: more light (optional after §11) | to do |
| 9 | Night camp: shadows and lighting quality | to do |
| 10 | The Gray talks across the fire | to do |
| 11 | §9 first, torches only if still dark | (rule) |
| 12 | Night arrival lower, one pass by the camp | to do |
| 13 | Reef geometry and texture, the black streaks | done (to verify on render): the streaks were the canyon program's world-height strata bands on steep faces; the reef has its own program (photoscanned rock + sand, hex-tiled, algae, coral, caustics); softer relief and a finer mesh |
| 14 | Sea portal sooner | done: field 260 -> 770 m (~139 -> ~130 s), tear 130 -> 360 m (~141.6 -> ~137.5 s, after the last sortie at 136.4 s); the strips die 133.6-135.8 s |
| 15 | Pyramid portal later (after the camera passes the rover) | done: field 1700 -> 611.1 m (the camera's distance from the focus at the knot over the rover, less 15 m), tear 520 -> 411.1 m |

Research: `48-night-lighting-research.md` (coordinator's agent); its top-5 are part of M3e (§8/§9 and the night river).

Tools: the ground sampler is a temporary `[.sample]` test that is never committed (`sample_ground.py`); the Mars checks
are in the session scratchpad (`marscheck.py`).
