# Hero assets, Stop H1: the audit, the research, the fur proof and the plan

The hero-asset lane of brief 49 (Phases 1.1, 1.2, 2, 3.1-3.2, 4.1-4.2 and 6). Branch `scene/sa-hero`, worktree
`../av-gen-sa-hero`. Written 2026-11-03. This document is the H1 stop: nothing gets built until the owner reviews it.

The media is in `~/Desktop/av-gen-review/101-strange-attractor/pass2-hero/`:
- `01-audit-turntables/`: Blender turntables of every current asset, with topology wires and texture sheets;
- `02-fur-proof/`: the fur proof rendered in AV Gen;
- `03-candidates/`: the thumbnails of the researched alternatives.

The tools used, all committed:

| Tool | What it does |
|---|---|
| `examples/strange-attractor/hero/audit_glb.py` | the numbers below, without Blender |
| `turntable.py` | 4 views, a close-up and a topology wire |
| `sheet.py` | contact sheets |
| `fur_proof.py` | the fur variants |
| `proof_stage.py` | the test ground |
| `proof_scene.py` + `render_proof.sh` | isolated AV Gen test compositions under `docs/prototypes/strange-attractor/hero/`, rendered through `tools/gpu-lock.sh`; never the real scene |

## 0. Findings the coordinator should know now

1. **`ph-teepee.glb` is not a teepee.**
   - Cause: `pass2/build_teepee.py` sets `rotation_euler` on logs imported from glTF, whose `rotation_mode` is
     QUATERNION, so the rotations are silently ignored.
   - Result: the five logs lie **flat and parallel**, 0.35-0.47 m above the pit's floor, in a 1.25 × 0.34 m bundle
     (`01-audit-turntables/sheet-teepee-firepit-current.png`).
   - Cost: the "teepee" is 509k triangles (100k per log).
   - This is part of why the reference shot's fire reads as "a particle fountain on a dark object": there are no
     logs under the flames.
   - My campfire asset replaces it. Until then, the art agent may want to know.
2. **`sasquatch-b.glb` ships its grey fur as `BLEND`.** `build_sasquatch.py`'s `mask_fur()` patches only the material
   named `sasquatch_fur`, so the silverback's `sasquatch_fur_grey` cards are alpha-blended, unsorted and outside the
   depth prepass.
3. **The flames are additive particles with no texture.** AV Gen particles have no flipbook or texture slot
   (`particle_io.cpp` keys), so they draw soft discs: the "glowing fountain" in the brief. A convincing flame
   therefore has to be geometry (see §5).
4. **The Gray is 45% metallic.** Its glTF has `metallicFactor` 0.445 and a faint green `emissiveFactor`, which is
   why it reads as chrome or plastic under the moon (`01-audit-turntables/avgen-gray-current-close.png`).
5. **AV Gen shades the back face of a `doubleSided` alpha card with its normal flipped.** With body-matched normals
   (the right normals for fur), every card seen from behind goes black. In the fur proof this alone turned the cards
   into dark "flakes" (§3). The asset-side fix is explicit back faces; see §6 for the renderer side.

## 1. Phase 1.1: the audit (numbers from `audit_glb.py`; pictures in `01-audit-turntables/`)

### The Gray: `assets/strange-attractor/sketchfab/gray/grey-alien-idle/grey_alien_idle.glb`

Source: ShaxerTakkuY on Sketchfab, CC BY 4.0. The scene uses it at scale 0.68, so 1.96 m tall, with clip
`mixamo.com` at speed 0.8.

| Item | Value |
|---|---|
| Geometry | 55,016 tris, 28,391 verts, one mesh. It is a **decimated sculpt**: a uniform triangle soup with no edge flow (`gray-current-wire.png`). |
| UVs | one 0-1 set, a single "pelt" unwrap (`sheet-textures-current.png`); low texel density on the face (about 15% of the map) |
| Textures | one 2048² PNG base colour, painterly and blotchy pink-beige. **No normal, roughness, metallic or AO map.** |
| Factors | metallic 0.445, roughness 0.77, emissive (0.032, 0.040, 0.006) |
| Eyes, teeth, nails | none as separate meshes: the eyes are painted into the base colour over shallow sockets |
| Rig | Mixamo, 26 joints: spine ×3, neck, head, arms and legs. **No fingers, no jaw, no eyes, no face.** The model *has* long modelled fingers, but they are skinned to the hand bone. |
| Clips | one 4.0 s clip, `mixamo.com`: a crouched, creeping, cross-legged idle. That is the opposite of the owner's "neutral stance" (47- §10). |
| Silhouette | good: the classic Gray (huge cranium, thin neck, long arms, four-digit hands); the posture is the problem |
| Skinning | Mixamo auto weights; acceptable on the body; the hands are rigid |
| In Blender | imports cleanly; it can be remeshed or multiresed and projected; its weights can be reused |

**Verdict:** the silhouette is adequate and the surface is primitive. That is a materials and rig problem, not a
geometry problem.

### The Sasquatches: `assets/strange-attractor/build/sasquatch-{a,b,c}.glb`

Built by `pass2/build_sasquatch.py` from Mat Rex's "Bigfoot" (CC BY 4.0).

| Item | Value |
|---|---|
| Source body | 50,000 tris exactly, 31.5k verts: an **AI-generated or decimated mesh** (the `Mesh_0_Material_0_0` naming, the xatlas-style fragmented UV atlas, lighting baked into the albedo). Anatomy and proportions are good: a gorilla-man body, a strong brow, a decent face (`sheet-bigfoot-source.png`). It is hairless. |
| Body texture | one 2048² JPG with baked light and a pale, peachy skin. **No normal, roughness or AO map.** The UVs are hundreds of small islands, so the map cannot be painted by hand; it can only be re-baked. |
| Fur | 24,000 alpha-tested cards = 96k tris, 192k verts. One 256² strand texture of 48 strands, flat colour, dark roots. Roughness 0.95. No normals of their own. |
| Totals | 146k tris, 224k verts per creature; 14.4 MB |
| Rig | 19 joints, built by script (hips, spine, chest, neck, head, shoulders, arms, hands, legs, feet). **No fingers, no jaw, no toes.** Skin weights come from distance to bone segments (two bones, d⁻⁴), not from heat, because heat failed on the scan. |
| Clips | one 24 s seamless loop, `idle`, with a different base pose per creature: A stands, B crouches, C sits. It has breathing, two head turns, weight shifts over IK-planted feet (the drift is measured and printed), and a gesture each (scratch, point, look). |
| Variation | three heights, fur colours and builds |

**What reads as "rocks", from the turntables and the AV Gen proof:**
1. High-frequency, high-contrast speckle: sparse dark cards over pale skin.
2. Wide, wedge-shaped cards that read as scales.
3. The back faces of the cards shading black.
4. Crouched and sitting poses that close up the silhouette.

The body underneath is a clearly readable creature (`avgen-fur-p4-baked*.png`).

**Verdict:** retain the body geometry, the rig and the idle. Rebuild the surface and the fur. Add finger and jaw
bones only if the owner wants the "gesture back" option.

### The campfire: the scene's `campfire-logs` (= `ph-stone_fire_pit.glb`), `campfire-teepee` (= `ph-teepee.glb`) and four particle nodes

| Item | Value |
|---|---|
| Stone pit | Poly Haven `stone_fire_pit`, CC0, a photoscan: 3,887 tris; 1k diffuse, normal and ARM. Good. It is ash-floored, and the flames sit on that floor. |
| Teepee | broken (§0.1): 5 × 101,802 tris of Poly Haven `dead_tree_trunk` (CC0, 1k maps), flat, with no char, embers or glow |
| `campfire.glb` | the M1 procedural fire, **unused**: 1,976 tris, flat colours |
| Flames, core, embers, smoke | particles: additive soft discs, no texture, 2,400 + 900 + 600 + 700 capacity |

**Verdict:** replace the logs and the flame representation. Keep the stone pit, and its scan quality.

### The UFO: `assets/strange-attractor/build/ufo-craft.glb`

From BrunoLatour's "UFO" (CC BY 4.0). The scene uses it at scale 0.018, which makes it about 9.5 m across.

| Item | Value |
|---|---|
| Geometry | 139,164 tris, 7 meshes, clean quad-derived topology with real panel geometry: a window band, ribs on the underside, a ring (`ufo-current-wire.png`) |
| Materials | 7 Substance-painted sets, **all 2048²**: base colour, ORM (AO/rough/metal), tangent-space normal and emissive; tangents are included. 75 MB, because everything is PNG. |
| Prior changes | `prepare_ufo.py` forced every material OPAQUE and cut the emission to 12% |
| Emissives | the authored ones are **green**: small window and port dots, and underside lights (`sheet-textures-current.png`). They are dimmed to near-invisible. |
| Look | dark, weathered, near-black metal, with no visible brushed or panel response under night light (`sheet-ufo-fire-current.png`) |

**Verdict:** this is the best of the four assets. Retain the geometry. Enhance the materials (it is far too dark and
too uniform), add restrained emissive elements, and add glass for the dome band.

## 2. Phase 1.2: free-model research (`03-candidates/`)

Method:
- I used Sketchfab's public search and model API (no login), filtered to downloadable files, for bigfoot, sasquatch,
  yeti, grey/gray alien, ufo and flying saucer.
- For the strongest candidates I fetched the face counts, animation counts, licences and the **actual texture lists
  and resolutions** (`/i/models/<uid>/textures`).
- A web search covered Blend Swap, itch.io, TurboSquid, Superhive and Fab.
- Rejected outright: game and film rips (the toro_ardido_mo yetis and bigfoots, game-ripper's Bigfoot, Primal Rage
  gorillas, Kong 2021, Ben 10, Roger Smith, the Asgard), Marvel's Sasquatch (3dworldz, tagged marvel) and NC
  licences.

| Asset | Candidate | Licence | Evidence | Compared with ours |
|---|---|---|---|---|
| Alien | **Realistic Alien (Grey) Mid Poli**, Graphetta, https://sketchfab.com/3d-models/realistic-alien-grey-mid-poli-74c15d8bbebf4235838f7ef135ad00f8 | CC BY 4.0 | 79k tris; **4096² base colour, normal, roughness and displacement**; "good topology"; not rigged; "Nazca mummy" pink skin | real normal and roughness detail that ours lacks. The design is closer to a mummy than a classic Gray, and it would need a rig and a retarget. The only real contender. |
| Alien | Grey Alien, blackscrip, https://sketchfab.com/3d-models/grey-alien-d6abc6a842414150b93688974477d760 | CC BY 4.0 | 18k tris; 4096² base colour, normal, height, AO and metallic (ZBrush + Substance); posed hunched; not rigged | good maps, but the posed hunch means a rig fights the pose |
| Alien | Alien grey, ludviglarson (60k, 1 clip); Allan the Grey, Rasmus82 (11k, walk) | CC BY | no texture list (none public) or low poly | no better than ours |
| Sasquatch | Bigfoot, jtressle, https://sketchfab.com/3d-models/bigfoot-a6ad392247524ee7803380d7d52791cb | CC BY 4.0 | a LiDAR scan of a **statue** in the Patterson stride: 218k tris, **3 × 8192²** textures; fur sculpted into the geometry; not rigged | a superb fur-clump reference and a possible bake source; useless as a deforming character |
| Sasquatch | Low Poly Bigfoot, DonTo (60k, stylised, waving) | CC BY | a stylised Christmas scene | worse |
| Sasquatch | itch.io "Retro Rigged Bigfoot" and "PSX Big Foot" | pay-what-you-want or paid, licence unstated | low-poly PSX style | worse, and the licence is unclear |
| UFO | UFO (real!), Schmoldt5000, https://sketchfab.com/3d-models/ufo-real-a75df2231baf4470a9dd21abebd80339 | CC BY 4.0 | 394k tris (including a photoscanned landscape); 2048² diffuse, roughness, metallic and normal, plus a "rust" set | a 50s riveted saucer; maps comparable to ours; a different design. Not an upgrade. |
| UFO | UFO, aung (786k); UFO, duz_vr (43k, stylised); UFO | Animated, tadenstar | CC BY | no maps listed, or stylised | worse |
| UFO | UFO, noe-3d (776k) | **CC BY-NC** | | rejected for the licence |

Conclusion:
- **There is no substantially better free alternative for the Sasquatch or the UFO.** The free ecosystem has no
  rigged, realistic, groomed Bigfoot, and our UFO is a better-built model than every free saucer I found.
- **For the alien, Graphetta's model is the only candidate with the detail ours lacks** (4K normal and roughness).
  It is a different design, though: pink and mummy-like.
- Recommendation: improve ours (§4). I would download Graphetta's only as a fallback, and to borrow its skin
  normal/roughness *as a detail texture* (CC BY permits this, with attribution).

**The owner's download is needed** for any of the four CC BY candidates, because Sketchfab requires a login:
- Graphetta's alien: https://sketchfab.com/3d-models/realistic-alien-grey-mid-poli-74c15d8bbebf4235838f7ef135ad00f8
- *(optional)* jtressle's Bigfoot statue, as a fur-clump reference and bake source:
  https://sketchfab.com/3d-models/bigfoot-a6ad392247524ee7803380d7d52791cb
- *(optional, for a side-by-side only)* blackscrip's Grey Alien:
  https://sketchfab.com/3d-models/grey-alien-d6abc6a842414150b93688974477d760

I did not download any of them. The plan below does not depend on them.

## 3. The fur approach: the cheap proof, rendered in AV Gen

**The setup.** One Sasquatch (A), with the same rig and idle, on a 24 m red-sand disc, under:
- the scene's campfire colour as a shadowed point light 1.6 m in front;
- the stone pit and the scene's flame particles;
- a cool moon rim from behind;
- AgX tone mapping (48- A1).

Everything was rendered by `avgen --render` through `tools/gpu-lock.sh`, with avgen exit 0 and 0 GPU errors each
time. The stills, from `~/Desktop/av-gen-review/101-strange-attractor/pass2-hero/02-fur-proof/`:
- `00-sheet-fur-proof-avgen.png`: the sheet;
- `avgen-fur-p*.png` (wide) and `avgen-fur-p*-close.png`.

| # | Representation | Tris | In AV Gen |
|---|---|---|---|
| p0 | current: 24k wide cards on pale skin | 146k | **rock**: high-contrast speckle, the anatomy lost |
| p1 | 60k cards: a 1024² strand atlas, body-matched normals, the body re-tinted to fur, **explicit back faces** | 530k | reads as a furry creature at the wide shot; at the close-up the fur is a uniform pelt, and the anatomy is muted |
| p2 | 16 fine shells, no cards | 850k | **the shells vanish**: the fine strands do not survive minification, so it reads as a hairless suit (see §6) |
| p3 | 10 shells + 18k cards (built before the back-face fix) | 622k | dark flakes, from the back-face bug |
| p4 | **baked only**: a 3-D streak-noise fur normal and albedo baked into the body's own maps (Cycles), no geometry | 50k | **the anatomy reads best of all**, but it is a smooth "man in a suit", with no fur silhouette |
| p5 | baked + 8 clump shells + 9k wide cards | 522k | the clump shells survive and read as matted fur on the legs; the wide cards are still flaky |
| p6 | **baked + 8 clump shells + 9k soft-rooted thin "lock" cards** on the head, shoulders, back and forearms, with back faces | 522k | **the best: a recognisable, furry, anatomical creature**, with a hair-broken silhouette, coherent shading and visible pecs, arms and shoulders. Remaining defects: the face is buried in fur (needs a bare-face mask), the cards stretch between the hands and the hips (weights), and the colour is uniform. |

What the proof established:
1. The "rock" look is made of **speckle contrast plus wedge-shaped cards plus black back faces**, not of a lack of
   detail.
2. Anatomy is carried by the **body's shading** (p4), so the body must stay visible through the fur: short shells,
   not a dense card pelt.
3. **Fine shells do not survive in AV Gen**, while clump-scale shells do.
4. Cards must have soft, non-solid roots, thin strands, body-matched normals and explicit back faces.

**Recommendation: the hybrid (the brief's option 5), as in p6, done properly:**
1. Re-UV the body to clean islands (Smart UV or a seam-guided unwrap) at 4096², so it can carry a real groom bake.
2. Bake a real groom into the body's maps:
   - build a Blender particle or curves groom (combed flow, clumping, a bare face, palms and soles);
   - render or bake its albedo, normal and AO onto the body.
   - The jtressle statue scan is the clump reference, if the owner downloads it.
3. Add 6-8 clump shells driven by a groom-length mask: short on the face and hands, long on the shoulders and back.
4. Add sparse long lock cards along the flow on the head, crown, shoulders, back, forearm "skirts" and the backs of
   the calves.
5. Fix the weights: cards take the body's weights by nearest *face on the same limb*, never spanning a hand and a
   hip.

Budget per creature: about 450-550k tris. That is fine offline (all six proof stills rendered in 0.2-0.3 s a frame).

## 4. Recommendations per asset

| Asset | Recommendation | Why |
|---|---|---|
| **Gray** | **Retain the geometry; substantially upgrade the materials; modify the geometry lightly; extend the rig and replace the clip.** | The silhouette is right. Everything else is primitive: no normal map, metallic 0.45, painted eyes, no fingers, a creeping clip. |
| **Sasquatches ×3** | **Retain the body, rig and idle; rebuild the surface and the fur** (the §3 hybrid); open the silhouettes | The anatomy is good underneath; the fur and texture make it a rock |
| **Campfire** | **Replace the logs and the flame representation; retain the stone pit** | The teepee is broken; the particles cannot carry a flame |
| **UFO** | **Retain the geometry; upgrade the materials and emissives; add glass** | Free alternatives are not better; it is a well-built model shown too dark and too uniform |

## 5. The plan (after the owner's direction)

Outputs go to `assets/strange-attractor/hero/`. Each item gets a `.blend` beside its `.glb`, a Blender preview and an
AV Gen test still, then goes to the coordinator for integration.

### H2: the Gray, `alien-v1.glb`

1. **Surface.**
   - Multires-subdivide the decimated mesh (or voxel-remesh, then project) and sculpt in skin detail: wrinkles at the
     joints, a sternum and ribs, tendon lines in the neck and hands, knuckles, pores.
   - Bake a 4096² **tangent-space normal map** back onto the original 55k mesh, so the rig is untouched.
   - The detail can be procedural (Blender noise and Voronoi displacement, masked by curvature), optionally with
     Graphetta's skin normal as a detail layer if the owner downloads it.
2. **Material.**
   - metallic 0, so the chrome goes;
   - a new albedo: a desaturated grey-olive with subtle mottling, darker in the creases, lighter on the cranium;
   - a baked roughness map: oily around the eyes and lips, drier elsewhere;
   - a baked AO map.
   - There is no SSS (§6), so keep the albedo slightly warm at the thin parts (ears, fingers) as a cue.
3. **Eyes.** Separate glossy black eye-lens meshes (roughness 0.05, a dark, deep blue-black) set into the sockets,
   so they catch the fire and moon as real specular highlights. They are skinned to the head bone.
4. **Rig and animation.**
   - Add finger bones (three per digit) and a jaw or mouth bone; weight them on the existing mesh.
   - Author a new seek-safe **"talking" idle**: a neutral upright stance with planted, IK-locked feet; small breathing;
     head nods and tilts toward the Sasquatches; one arm gesturing and pointing across the fire on held beats; the
     other hand relaxed.
   - Keep `mixamo.com` as a fallback clip.
   - The animation is a sum of whole-cycle sines plus held beats, like the Sasquatch idle, with a printed foot-drift
     check and a hand-to-body clearance check.

### H3: the Sasquatches, `sasquatch-{a,b,c}-v1.glb`

1. The §3 hybrid on the existing body, rig and idle (re-UV, groom bake, clump shells, lock cards, back faces, fixed
   weights).
2. Per creature:
   - colour (dark brown; a silverback with real grey roots and tips; auburn);
   - groom length;
   - fur noise.
3. **Silhouettes.**
   - Open the crouch and sit poses enough that the head, shoulders and arms separate from the torso.
   - Add the brief's subtle extras on top of the existing idle: stronger breathing in the chest and shoulders; head
     turns toward the Gray's gestures; independent phase per creature (already the case).
4. Optional: finger bones, so a hand can gesture back (47- §10's "exchange").

### H4: the campfire, `campfire-v1.glb` (one asset, sitting in the existing stone pit)

1. **Logs.**
   - 5-7 split logs, properly leaned into a collapsed teepee, from Poly Haven `dead_tree_trunk` at **10-20k tris
     each** (decimated from 100k).
   - Burned ends: charred alligator-crack albedo and normal from a Voronoi-crack bake, blended by distance to the
     fire's centre.
   - **Glowing cracks**: an emissive mask from the same crack pattern, orange to yellow by depth.
   - A grey-white ash ring and an **ember bed**: about 200 small coals with an emissive mask; static, so they are
     seek-safe.
2. **Flames: layered emissive geometry, not particles.**
   - 6-10 flame "tongue" sheets in two or three height tiers, crossed in X/Y, each a BLEND emissive flame-shaped card
     with a vertical colour ramp (deep orange at the edges and tips, yellow, a white-hot base).
   - Animated by a small skinned chain per tongue: whole-cycle sines, a 4-6 s seamless loop, deterministic and
     seek-safe.
   - AV Gen has no flipbook or UV scrolling, so the motion has to come from bones.
   - A hot core glow sheet at the base.
3. **Kept and retuned:** the existing particle embers and smoke for sparks and smoke.
   - The scene's particle nodes belong to the art agent; I will hand over proposed values.
   - The fire's light stays theirs.

### H5: the UFO, `ufo-v1.glb`

1. **Materials.** Keep the UVs and ORM maps, and rework them in Blender (bake-based edits):
   - lift the albedo to a dark gunmetal with **roughness variation**: a brushed-direction streak noise in the
     roughness map, since anisotropy is unsupported;
   - darken the recesses and seams using the existing AO;
   - add restrained grime at the panel lines.
2. **Glass.** Turn the window band into real glass: `KHR_materials_transmission` and ior, which AV Gen supports
   (ADR-1819), with a dim cool-white interior emissive behind it.
3. **Emissives.**
   - Replace the green with restrained cool white and pale cyan;
   - small navigation points;
   - a segmented rim strip of individual light cells;
   - a soft underbody ring glow.
   - Emissive strength is authored to read before bloom.
   - Selective geometry only: the underside ring and the cell recesses, no greebles.
4. Size: convert the PNGs to JPG and halve the unused maps (75 MB to about 25 MB).

### Order and stops

The order is H2 (Gray), H3 (Sasquatches), H4 (campfire), H5 (UFO). Each is handed back with:
- Blender previews;
- an AV Gen test still and a short clip, for animated items;
- a feet and clipping report.

## 6. Renderer and material gaps for the art agent (via the coordinator)

| # | Gap | Evidence | Impact | Suggestion |
|---|---|---|---|---|
| G1 | **The back face of a `doubleSided` material shades with its normal flipped.** With custom (body-matched) normals on an alpha card, every card seen from behind goes black. | proof p3 and p6 before the fix vs after (`avgen-fur-p6-locks-close.png`) | the fur cards, foliage cards, any thin alpha geometry | a per-material "two-sided lighting" option (keep the authored normal on back faces, or use `n·v` sign-free wrap) for MASK materials. I work around it with explicit back faces (+2× card tris). |
| G2 | **Fine alpha-tested detail vanishes with distance** (shell fur at about 300 px figure height) | p2 vs p5 | shell fur, fine grass and hair | coverage-preserving mips for MASK textures (rescale each mip's alpha to keep the mip-0 coverage at the cutoff; Castaño 2010), or alpha-to-coverage with MSAA. The existing LOD cutoff fade (`kAlphaCoverageFade`) was not enough here. |
| G3 | **No morph targets** (`gltf_loader.cpp:553`) | | facial animation: talking | bones only (my plan uses a jaw/mouth bone). Morph support would be the cinematic route. |
| G4 | **No SSS or transmission for skin; no sheen for fur** (`KHR_materials_sheen` is ignored) | the Gray's skin reads as plastic; the fur gets no rim or back-scatter | | 48-B9 (fur wrap and back-scatter), plus a cheap wrap or SSS term for skin materials (a scalar per material) |
| G5 | **No anisotropy or clearcoat** (both ignored) | | a brushed-metal UFO | I fake it with streaked roughness; a real aniso GGX would help the craft |
| G6 | **No texture transform, no UV animation, no particle flipbook or texture** | `particle_io.cpp` keys | the flames are particle discs | I will build flames as skinned geometry. A textured or flipbook particle mode would let the art agent do better sparks and smoke. |
| G7 | **`ph-teepee.glb` is broken** (§0.1), and **`sasquatch-b`'s grey fur is BLEND** (§0.2) | | the current scene | until my v1 assets land: re-run the teepee builder with `rotation_mode = 'XYZ'` set before the rotation, and patch `sasquatch_fur_grey` to MASK |
| G8 | The Gray's metallic is 0.445 | | | override it to 0 if the art agent wants an interim fix before H2 |

## 7. Licences

None of my outputs adds a new third-party source yet. The proof built only on assets already recorded in
`assets-pass2.md` (Mat Rex's Bigfoot, CC BY 4.0; Poly Haven, CC0). See `hero-assets.md`.
