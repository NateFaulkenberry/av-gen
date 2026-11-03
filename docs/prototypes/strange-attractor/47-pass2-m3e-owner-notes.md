# Art Pass 2, M3e: the owner's notes on the merged M3d

M3d is merged to main (64c1fd94). The owner is reviewing the full render
(`pass2-m3d/07-full/strange-attractor-m3d-full.mp4`) and sends notes as they go ("notes so far"); more may
be appended here. Build M3e on scene/strange-attractor at 64c1fd94.

Owner screenshots (read-only; never commit them) are in
`~/Desktop/av-gen-review/101-strange-attractor/pass2-m3e/00-owner-refs/`.

## 1. Sea exit portal (launch bay to dawn canyon): not confined to the bay

`sea-exit-portal-confined-to-bay.png`: the glyphs and the tear exist only inside the bay window's
rectangle, inside a glowing white frame, while the base round it stays untouched.

- Remove that constraint. Like the other portals, the corruption must spread out over all the
  surrounding geometry: the base's walls, the rock and the seafloor. The tear and the shard burst should
  break out past the bay frame.
- Match the Moon reference portal's look (owner, M3d).

## 2. Pyramid exit portal: long shard projectiles

`pyramid-portal-long-shard-projectiles.png`: during the pyramid portal's burst, several very long, thin,
straight dark rods or slabs shoot out of the opening across the frame, far longer than any other shard.
They are probably the "sand triangles that read as long strips" from the M3d not-done list: degenerate,
stretched shard triangles.

- Fix it at the cause: no shard may stretch into a long projectile. Clamp or rebuild degenerate source
  triangles and cap the shard size and aspect ratio, so the burst matches the Moon reference.
- Check every portal for the same kind of shard.

## Rules

- Same branch and worktree (`scene/strange-attractor`, `../av-gen-attractor`), on 64c1fd94.
- ADRs continue from 1904 within 1885-1929.
- Media goes in `~/Desktop/av-gen-review/101-strange-attractor/pass2-m3e/`, in numbered folders, with exact
  file names. Show each fixed portal beside the Moon reference, with frame-by-frame stills through its
  burst.
- Art checkpoint first; suites and the full render after the owner approves (as for M3d).
- All standing rules in `31-art-pass-2-coordinator-notes.md` apply.

## 3. Mars: the land mass blocking the pyramid (owner addition)

`mars-landmass-blocking-pyramid.png`: during the fly-over past the rover, a big smooth sand mound or
drift (boxed in red) sits between the rover and the pyramids. It hides most of the pyramid, and it has
creased, faceted geometry on its face.

- Remove it, so the pyramid is clearly visible over the rover along the whole fly-over.
- Check the whole Mars camera path for any other terrain hiding the pyramids.

## 4. Tiled texture look, canyon and Mars (owner addition)

`mars-sand-visible-tiling.png`: the Mars sand close to the camera shows a regular grid of repeating
light and dark patches. The photoscanned texture's tile repeats are plainly visible.

- Fix the tiled look in BOTH the canyon and Mars scenes, on every surface that uses the photoscanned
  sets or a repeating procedural layer.
- Use a proper anti-tiling technique, for example:
  - stochastic or hex-tile texturing (Heitz-Neyret / Mikkelsen style blending of randomly offset and
    rotated tiles);
  - layering the texture at two or three incommensurate scales;
  - large-scale macro variation driven by world-space noise.
- No visible period anywhere at any camera distance.
- If the fix belongs in the engine's material or shader code, make it a generic feature with an ADR and
  tests.
- Show before/after stills at the same frames: Mars near the rover and the fly-over, and the canyon
  close-ups (the camp ground, the dusk banks).

## 5. Mars entrance portal: clipping through a bulge (owner addition)

`mars-entrance-clipping-bulge.png`: as the camera comes through the canyon-cliff portal into Mars, it
partly clips through a dark rust-brown bulge of terrain or rock right in front of the camera. The bulge's
cut-open, faceted section fills the middle of the frame while the portal's gold shards still frame the
edges.

- Fix where the Mars arrival comes out of the geometry, so the camera passes through the portal and
  into open air without clipping any terrain, rock or bulge. Move the exit or the camera path, or clear
  the terrain round the exit (pad it, ADR-1897 style).
- Check every portal arrival frame by frame for the same kind of near-plane clipping, and show it.

## 6. The Moon: Earth's glow becomes an aura (owner addition)

`earth-pixel-corona-to-replace.png`: the M3d pixel corona is a ring of white blocks round the Earth.

- Remove the block ring and replace it with a glowing aura: a soft, luminous, atmospheric glow hugging
  the Earth's limb and falling off smoothly into space, stylised and vibrant rather than a plain halo.
- Keep the beat pulse; the owner says it looks good.
- Add glitching and datamoshing: on the aura and the Earth, for example RGB channel splits, block
  displacement or smear, scanline tears, and macroblock mosh trails, beat-driven and deliberate.
- No shimmer or aliasing between beats; report the Moon flicker metric.
- Show stills and a clip with sound.

## 7. The dusk canyon after the Moon: a new arrival and path (owner addition)

After the Moon exit portal, in the evening (dusk) canyon with the Sasquatches:

- **Move the arrival portal** (where the camera comes out of the canyon wall) somewhere else: further
  down the river, on the opposite side of the river from where the Sasquatches stand.
- **New camera path:** from that arrival, pass by the Sasquatches, then go on up into the next portal
  above (the cliff portal to Mars).
  - No circling round them, and no lingering or heavy focus on them. They are a moment passed on the
    way, not the subject.
  - The move should be one continuous, smooth path.
- Keep the arrival readable as a portal exit (the "pop out of the canyon wall" behaviour the owner asked
  for at M3c), with no clipping through rock, shrubs or characters.
- Show the new dusk section as a clip with stills, beside the M3d version.

## 8. The night scene: more light sources (owner addition)

`night-camp-too-dark.png`: a wide view of the night camp shows the whole bank in near-black. The fire
and the craft's underside glow are the only pools of light; the Sasquatches and the Gray are tiny
silhouettes; the rest is a dark brown slope.

- The owner: "the night scene lighting still looks poor". Add more light points to illuminate the area,
  for example glowing torches round the camp, lanterns, glowing stones, or light spilling from the craft.
- Placement:
  - motivated, and set on the ground or in rock (not floating);
  - warm flicker that is slow and soft, never strobing;
  - each light casts onto the ground, rocks and characters.
- The aim is a readable, atmospheric night set with several warm pools and cool moonlight between them,
  not a black frame with one fire.
- Also lift the craft's own lighting so it reads as a lit object.
- Show before/after at the same frames, including this wide angle.

## 9. The night camp: shadows and lighting "still look very off" (owner addition)

`night-camp-shadows-lighting-off.png`: a closer view of the camp (the Gray, the fire, three Sasquatches,
a boulder and the craft). The coordinator's read of what is wrong:

- **A huge soft black blob shadow** (a capsule shape) on the bank behind the fire. It matches no caster's
  silhouette; it looks like a stretched, low-resolution point-light shadow.
- **The fire is inside a Sasquatch:** the flames glow through the front Sasquatch's body, so the fire or
  that character is misplaced.
- **The smoke** is a broad pale haze cone drifting across the whole upper frame.
- **An unmotivated bright white spill** on the bank behind the craft, while the craft itself is dark.
- **The Gray** is flat white with a hard gold patch down one side, unlike its surroundings.
- **No convincing contact shadows** under the characters, rocks and fire pit. The shrubs on the right
  float and are unlit.

Fix the lighting as a whole, not just by adding lights (§8):
- every shadow must match its caster and its light (resolution, bias, softness, direction from the fire);
- add contact or AO grounding;
- no character intersects the fire;
- tame the smoke into a thin rising column;
- every bright area needs a visible source;
- the characters' shading must sit in the scene's light.

If the engine's point-light shadows can't do this (resolution or filtering), say so and fix that
generically with an ADR. Before/after at this angle and the M3d camera beats.

## 10. The Gray's animation: talking to the Sasquatches (owner addition)

- Change the Gray's animation so it reads as talking or communicating with the Sasquatches **from across
  the fire**:
  - pointing and gesturing (arm and hand gestures, head turns and nods toward them);
  - the body in a **neutral stance**: upright, relaxed, feet planted, no crouch or creep.
- Placement: put the Gray on the far side of the fire from the Sasquatches, facing them.
- Seek-safe and deterministic; no foot sliding, and no limb passing through the body.
- Optional: the Sasquatches respond subtly (heads turning toward it, a shrug or gesture back), so it
  reads as an exchange.
- Show a clip at the camp beats, with an idle sheet.

## 11. Priority between §8 and §9 (owner clarification)

The extra torches and light points in §8 are now **optional**. The owner: up close there is plenty of
light in the scene; it just looks poor. So:
- Do §9 first: fix the quality of the existing lighting (shadows, grounding, motivated sources, smoke,
  character shading).
- Add torches or other light points only if, after §9, the wide night shots still read as too dark or
  flat. Say in the hand-back which way you went and why.

## Evaluation (owner, M3e onward)

- Use the Creative Critic (daemon at http://127.0.0.1:8765; CLI `~/Documents/GitHub/creative-critic/.venv/bin/critic`,
  restart with `critic start --daemon`) in a session `strange-attractor-m3e`.
- Per changed section (portals, dusk, Mars, Moon, night camp, ocean): a short `intent.json` from the notes in `44-` and
  `47-`; `critic submit --mode preview --video <clip> --video-start <film time> --intent <intent.json>` on the M3d clip
  as the baseline, then on the M3e clip; `critic compare <baseline> <candidate>`. The fast scene critic on the scene
  JSON where it applies (grounding, floating objects, lighting, composition, clipping).
- The hand-back has a table per section: key metrics before and after (exposure and contrast, shimmer and flicker,
  stability, aesthetic score, grounding findings), and the top findings acted on and dismissed, with reasons.
- The critic is evidence, not the judge: the owner's eye wins; model scores (laion_aes etc.) are experimental, and any
  Ollama vision commentary is labelled as such.

## 12. The night canyon: a new arrival and a pass-by path (owner addition)

This is the arrival from the pyramid portal into the night canyon, and the path from it to the
river/water portal into the ocean.

- **Move the arrival portal:**
  - choose a different location, **not as high up**;
  - **slightly further away from the Sasquatches** (the camp).
- **New camera path:** fly **by** the camp on the way to the water portal entrance.
  - No focus on them: no framing on the group, no lingering and no circling.
  - We only see them as we pass by on the way to the next portal.
  - One continuous, smooth path from the arrival to the river portal.
- This is the night counterpart of §7 (the dusk pass-by). Keep both consistent in feel.
- No clipping through rock, shrubs, characters or the craft. Keep the arrival readable as a portal exit.
- The camp lighting (§8, §9, §11) and the Gray (§10) still matter: they are what we glimpse in passing.
- Show the new night section as a clip with stills, beside the M3d version, with critic before/after.
