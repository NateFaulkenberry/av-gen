# Art Pass 2, M3e: progress

The notes are `47-pass2-m3e-owner-notes.md` (§1-15, plus the Evaluation rule). M3e builds on 64c1fd94. It is an art
checkpoint: clips and stills; the suites and the full render come after approval.

Media is in `~/Desktop/av-gen-review/101-strange-attractor/pass2-m3e/`.

| § | Note | State |
|---|---|---|
| 1 | Sea exit portal confined to the bay | done: the base, bay strips and seafloor are targets; field radius 45 -> 150 m, opening 22x11 -> 36x21 m (beyond the bay's 30x15 half-size); the frame's light strips are their own glb (`bay-strips`) and die 138.6-140.8 s; corruption light 1500 -> 500 (the white-out before the crossing) |
| 2 | Pyramid long shard projectiles | done (ADR-1904): slivers skipped, shard size capped |
| 3 | Mars mound hides the pyramid | done: a corridor of pads (34 m apart, radius 58 m) eases the fly-over to the plain; a sightline check finds no terrain over any line to the pyramids |
| 4 | Visible tiling | engine done (ADR-1905, tests); scenes to apply |
| 5 | Mars arrival clips a bulge | done: the exit is out of the butte's steep face at 95 m (it was in the talus apron); the first knot is 30 m over the ground |
| 6 | Earth aura, glitch and datamosh | to do |
| 7 | Dusk arrival across the river, one path past the camp | to do |
| 8 | Night camp: more light (optional after §11) | to do |
| 9 | Night camp: shadows and lighting quality | to do |
| 10 | The Gray talks across the fire | to do |
| 11 | §9 first, torches only if still dark | (rule) |
| 12 | Night arrival lower, one pass by the camp | to do |
| 13 | Reef geometry and texture, the black streaks | to do |
| 14 | Sea portal sooner | to do |
| 15 | Pyramid portal later (after the camera passes the rover) | to do |

Tools: the ground sampler is a temporary `[.sample]` test that is never committed (`sample_ground.py`); the Mars checks
are in the session scratchpad (`marscheck.py`).
