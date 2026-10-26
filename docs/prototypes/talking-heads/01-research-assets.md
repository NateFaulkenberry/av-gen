# Talking Heads: asset and art-direction research (Part A)

Prep only, 2026-10-24, branch `prep/talking-heads`. Nothing was downloaded. Every licence below was read from the live
listing or API that day. The spec is `00-brief.md`.

**Verdict.** Build the bust ourselves, procedurally in Blender, from the probe in `probe/make_bust.py`. It already reads
as a mannequin head from the front, at three-quarter and in profile. It is 29,758 triangles, clean and manifold, and it
has an exact mouth region and known emitter anchors. It is our own work, so it can be committed to the public
repository. No third-party candidate beats it on character, licence or how well it fits the emitters. The best licensed
fallback is the Innerscene bust (CC0). All three of the spec's leads fall short in some way (§1).

## 1. The spec's three candidates

| | scbenoit "Mannequin head" | Innerscene "Modern abstract bust" | switchandlever "Mannequin Head #3, Untextured" |
|---|---|---|---|
| URL | sketchfab.com/3d-models/mannequin-head-d465a1f697b54136b65288ce9c23b16f | innerscene.com/tools/library/3d-parts/modern-abstract-bust-91e3ae04 | sketchfab.com/3d-models/mannequin-head-3-untextured-77910a3722124bf398a3c3030bc0eac4 |
| Licence (live) | **CC BY 4.0**: commercial use allowed, credit required | **CC0** ("public domain ... no credit required"). The library terms allow copying, modifying and redistributing for any purpose | **CC BY-NC 4.0**: **no commercial use** |
| In the public repo? | Legally yes, with credit in CREDITS.md. In practice no: the house rule keeps third-party binaries gitignored behind a manifest (`assets/rainbow-world.manifest.json` is the pattern) | Yes, legally. Same house rule applies | **No.** Not for AV Gen distribution, and not for a commercial deliverable without the author's written permission |
| Source | Photogrammetry (43 photos, PhotoCatch), "cleaned up a little in Blender" | Adapted from MakeHuman / MPFB CC0 body assets | Photogrammetry (50 photos, 3DF Zephyr Free) |
| Formats | Sketchfab download: the original upload, plus auto-converted glTF/GLB and USDZ | GLB (737 KB); more from the online viewer | As scbenoit |
| Geometry (API) | 12,336 faces, 6,152 vertices, 1 material, **4 textures** | Not published. A 737 KB GLB with materials suggests roughly 10-25 k triangles (unverified) | **605,140 faces**, 302,572 vertices, 1 material |
| Size | Real-world foam head | 0.390 x 0.237 x 0.633 m | Real-world |
| Character | A black, paint-spattered **foam wig head wearing safety glasses**. The glasses are part of the scan. Features are soft and faint | An **egg-shaped head on an egg-shaped torso** on a white pedestal, dark brown, faint features. Abstract, but there is no neck, and the silhouette reads as a stack of ovoids more than a head | The best face of the three: a male display mannequin with sculpted eyes, nose and lips, a long neck and a cut bust. Close to the brief's silhouette |
| Eyes / mouth read? | Barely. The glasses cover the eye region | Barely. The features are shallow | Yes, clearly |
| Defects | Photo texture full of dirt and paint flecks. Glasses fused into the mesh. Typical scan noise. All of it violates "no photogrammetry artefacts" | Unknown until inspected. Probably clean (generated from a base mesh) | Scan noise at 605 k faces needs decimation or a remesh. The author calls it "damn near perfect" |
| Rework needed | Remove the glasses, remesh, discard the textures, rebuild the face: effectively a new model | Lengthen the neck, fix the torso, deepen the features | Decimate to about 30 k. The licence blocks it regardless |
| **Verdict** | **Reject**: wrong character, and the cleanup would cost more than a custom build | **Fallback**: licence-perfect, but it needs neck and feature work before it can carry emitters | **Reject**: licence (non-commercial) |

What I saw: the listing thumbnails (Sketchfab's API `thumbnails`, and Innerscene's `/api/library/<id>/preview`), plus
Sketchfab's API metadata. I did not open the meshes. Downloading needs approval.

## 2. Further CC0 / CC-BY candidates

Searched Sketchfab's API with `license=cc0|by` and `downloadable=true` for mannequin, bust, faceless, plaster, base mesh
and statue head. Searched Poly Haven's full model list too.

| Candidate | Licence | Geometry | Character | Verdict |
|---|---|---|---|---|
| Poly Haven `marble_bust_01` (Rico Cilliers), polyhaven.com/a/marble_bust_01 | CC0 | 17,456 polygons; glTF/FBX/USD/blend; 1k-8k textures; 0.27 x 0.30 x 0.51 m | A realistic Renaissance male bust with curled hair and a round plinth | Reject: a conventional portrait, the opposite of the brief. It can still serve as a lighting test object, since it is licence-perfect and already handled by `tools/fetch_polyhaven.py` |
| Sketchfab "Wig Mannequin" (yacobverse) 63b3a3b35e9242d2b914064af26bf7fe | CC BY | 11,200 faces, 1 material, 0 textures | A glossy, **completely featureless** egg head on a neck and bust | Reject: no eyes or mouth to emit from. Its proportions are a good reference for the neck and base |
| Sketchfab "Male Head" base mesh (Dessen) 0247a25a04ba46b99629130277fe39b7 | CC BY | 3,718 faces, quad topology | A clean, realistic sculpting base: head and neck, no bust | Possible sculpt base if the owner wants more anatomical structure than the procedural bust. Needs stylising and a bust added |
| Sketchfab "Head Of Michelangelo's David, Optimised" (nebulousflynn) d29af50360624e5e9b1855666475380d | Listed CC BY; the source is SMK's CC0 scan | 100,000 faces | Hair, realism, an icon | Reject on character |
| Sketchfab "LOOMIS planes head bust" (cgmonkey) ccf0ca94205e4809a1620587d2fbf1d0 | CC BY | 476 faces | Faceted drawing-aid planes | Reject. A faceted variant is an interesting later look, though |

No CC0 library I found has a smooth, stylised mannequin bust with readable features. That gap is why the custom route
wins.

## 3. Download list (for the coordinator to ask the owner; none of it is required)

The recommendation does not depend on any download. These are optional side-by-side checks:

1. **Innerscene "Modern abstract bust" GLB** (CC0, 737 KB, `/api/library/modern-abstract-bust-91e3ae04/download`).
   Why: measure its real triangle count and check its feature depth against the procedural bust, in Blender and in
   AV Gen. Where it would go: gitignored, `assets/talking-heads/` with a manifest row (CC0, so committing is legal, but
   the house rule keeps third-party binaries out). Approval: the download only.
2. **Sketchfab "Male Head" base mesh** (Dessen, CC BY, about 0.3 MB). Only if the owner wants a sculpted anatomical
   base under the stylisation. Approval: the download, plus a CREDITS.md line ("Male Head" by Dessen, CC BY 4.0) if
   anything derived from it ships.

Not recommended for download: scbenoit (wrong character), #3 (non-commercial), Poly Haven marble bust (wrong character).

## 4. The Blender route: probe result

`probe/make_bust.py` (headless Blender 5.2.1 LTS, `--factory-startup`, about 27 s including Cycles previews; the
Blender MCP was not connected) builds the bust as **one generalised cylinder**: superellipse cross-sections with
separate front and back depths, swept up a monotone-cubic profile, closed by a pole at the crown and a flat base.
Sculptural features are smooth Gaussian offsets in (angle, height): a restrained nose, high cheekbones, a brow, a
simplified chin, shallow eye sockets and a recessed mouth slot. Every number is in the script, and the build is
deterministic.

| Measure | Probe |
|---|---|
| Triangles | **29,758** (14,881 vertices). Set by the ring and segment counts; `--segments 64` gives about 20 k |
| Topology | Quads with a triangle fan at the crown pole. 0 non-manifold edges. Smooth shading, crisp flat base |
| Materials | `porcelain` (29,682 triangles) and `mouth_interior` (76 triangles, near black). There is no eye-socket material: a hard region on this grid has stepped edges at 1080p, so the sockets darken by their depth and, at runtime, by a smooth object-space mask (§6) |
| Size | 0.332 x 0.212 x 0.600 m. Head (chin to crown) 0.265 m: about 15% longer than life, the "slightly elongated" of the brief |
| Frame | Blender Z-up, face to -Y. Exported glTF: +Y up, **face to +Z**, origin at the centre of the base's bottom face |
| Anchors | `probe/bust_anchors.json`, GLB frame: mouth (0, 0.384, 0.081), left eye (+0.0326, 0.462, 0.0729), right eye (-0.0326, 0.462, 0.0729), head centre (0, 0.465, -0.004) |

Media (`~/Desktop/av-gen-review/talking-heads/prep/`): `bust-front.png`, `bust-three-quarter.png`, `bust-profile.png`,
`bust-face-close.png`, `bust-silhouette-front.png`, `bust-silhouette-34.png`, `bust-silhouette-profile.png`,
`bust-contact-sheet.png`, `talking_head_bust_probe.glb`, `bust_anchors.json`. A small copy of the sheet is committed as
`probe/bust-contact-sheet.jpg`.

Reading the previews:
- The silhouette reads as a head immediately from the front, at three-quarter and in profile: long neck, small shoulder
  base, elongated cranium.
- The face is mannequin-simple: brow, nose, cheekbones and the dark mouth slot carry it.
- The sockets read as soft shadow, which is right for an emitter: the eye rays become the eyes.

Known shortfalls, for the production pass:
1. The mouth slot's ends step slightly, because the region follows the grid. Fix by rounding the slot's ends into the
   region selection, or by adding two rings at the slot's top and bottom edges.
2. The profile is a little flat between brow and chin. Add 2-3 mm of forward lower-face volume.
3. The chin underside is steep. Add one more profile point under the jaw.
4. Optionally, open the mouth a little more (a 6-8 mm oval) so the hero beam visibly leaves a cavity (§6).
None of these changes the approach.

**Custom vs best licensed candidate.**

| | Custom (probe) | Innerscene (CC0) |
|---|---|---|
| Character | Built to the brief: elongated, simplified jaw, high cheekbones | Abstract ovoids; no neck; faint features |
| Eye and mouth regions | Exact, with anchors exported | Must be found and probably deepened |
| Licence and repo | Our own; commit the GLB and script | CC0 but third-party: gitignored plus a manifest |
| Cost to production | About half a day of tuning in the script | Inspect, then modify in Blender anyway |
| Reproducible | Fully: one script, no binary source | Depends on a download that can vanish |

**Recommendation: the custom route.**

## 5. Asset pipeline: the exact recipe

How AV Gen takes meshes:
- glTF 2.0 / GLB through fastgltf.
- No vertex colours, one UV set (`examples/rainbow-world/blender/engine_assets.py` header). Colour beyond base PBR comes
  from material programs.
- A sub-mesh per material becomes one instanced draw (`src/scene/asset_parts.hpp`, ADR-044).
- Asset paths in a scene are relative to the scene file.

Node kinds that can draw the bust:
- **`gltf` node.** Draws through the PBR entity path. It can name one material program for all its entities
  (ADR-1572), keeps alpha-BLEND materials blended, and takes the entity FX lanes and XFORM offsets. It has no
  deformers.
- **`procedural` node with `{"kind": "mesh", "asset": ...}`.** Takes the deformer stack (Sine, Noise and others,
  routable), material programs, distributions and routes. It draws blended materials opaque
  (`procedural_renderer.cpp`).

The plan in 03 uses a **procedural mesh node for the main bust** (for the deformers) and **gltf nodes for the ghosts**
(for blending).

**Recipe:**
1. Source: `tools/talking_heads/make_bust.py`, the probe promoted, and nothing else. The `.blend` is not the source. Do
   not commit one.
2. Run it with `Blender -b --factory-startup -P tools/talking_heads/make_bust.py -- --out examples/talking-heads/meshes`.
3. Export (as in the probe and `rw_common.export_glb`): `export_format="GLB"`, `export_yup=True`,
   `export_apply=True`, `export_normals=True`, `export_texcoords=False` (no textures are needed; drop the flag if a
   UV-reading program is added later), `export_materials="EXPORT"`.
4. Write three outputs:
   - `th-bust.glb`: shell plus mouth, two materials;
   - `th-bust-ghost.glb`: the same geometry with one material, `alphaMode` BLEND, base colour black and emission off.
     The ghost's colour comes from its program;
   - `th-bust.anchors.json`: the anchors in the GLB frame, plus triangle counts.
   Expected sizes are about 0.5 MB each, which is normal for committed generated meshes (examples/*/meshes holds files
   of 2-8 MB).
5. Coordinates: metres, +Y up. The face looks down **+Z** in the GLB. Blender's (x, y, z) maps to AV Gen's
   (x, z, -y), as `rw_common.to_avgen` documents. The origin is the base's bottom centre. Scale is real (0.6 m), and
   the scene scales the node (about x3 for a 1.8 m bust in a sparse space; the camera distance matters more than the
   scale). The node's -Z is the back of the head. An engine feature that assumes a node faces -Z (`FxAnchor.forward`)
   would see the face as backward. Either rotate the node 180 degrees about Y in the scene, or keep +Z and say so in
   the anchors file. The plan keeps +Z and places emitters from the anchors explicitly.
6. Materials in the GLB:
   - `porcelain`: base (0.80, 0.74, 0.64), roughness 0.38;
   - `mouth_interior`: base (0.012, 0.010, 0.012), roughness 0.85.
   The runtime programs override colour. The materials just have to separate the regions.
7. Where things live:
   - script in `tools/talking_heads/`;
   - runtime GLBs in `examples/talking-heads/meshes/`, committed, our own work (CC0-equivalent; say so in CREDITS.md
     under "made for AV Gen");
   - previews in `~/Desktop/av-gen-review/talking-heads/`.
   No new `.gitignore` rule is needed. If any third-party candidate is ever downloaded, it goes to
   `assets/talking-heads/` with a gitignore line and a manifest row, following `assets/rainbow-world.manifest.json`.
8. Validate in the engine:
   - a still from `avgen --headless --project ... --render` at 1920x1080 with the bust at its intended size;
   - check the face is toward the camera, +Y is up, and the scale is right;
   - check the 2 draws (two parts) in `--bench-json`.

## 6. Art direction (one page)

**The mood reference.** Owner-supplied: *De-Loused in the Comatorium*, at
`~/Desktop/av-gen-review/talking-heads/reference/de-loused-in-the-comatorium.jpeg`. It is copyrighted album art: refer
to it only by path, never copy it. It shows a bald, golden, sculptural head lying back on a plate, eyes closed. A single
broad, pale, cone-shaped beam pours from its open mouth up and to the left through a dark clinical room.

The spec says mood only, so do not reproduce the composition: no plate, no lying head, no clinical room, no upper-left
diagonal. Take three things from it:
1. **One dominant mouth beam.** The cover's power is one soft, wide cone with a bright core, not many equal lines. So
   the mouth fan is **a hero cone plus a few thinner rays**:
   - **The hero cone:** wide, about 10-14 degree half-angle. Soft edge (`edgeSoftness` about 0.7). A bright core
     (`coreSharpness` 3-4) that is pale near the lips and takes the palette colour further out. Low dust. It is always
     the brightest element.
   - **5-8 secondary rays:** 0.8-2.5 degree half-angle, fanned ±35 degrees around the hero. Uneven brightness (a
     per-ray weight from 0.25 to 0.8 of the hero's). They waver on the mids and flare on transients.
   - **Eye rays:** 2 per eye, 0.5-1.2 degrees, longer and harder (`coreSharpness` about 6). Mostly near-invisible
     slivers when quiet; they burst on the highs.
   This also solves "avoid making every beam equally bright".
2. **A smooth, featureless sculptural head.** The procedural bust already is one.
3. **Warm light on the head against a dark ground.** One warm key catches the porcelain from high on one side, with a
   cool violet rim on the other. The beams light the air in front of the face, not the face. A faint mouth glow onto
   the lips and chin is allowed (a program term, not a light).

**Material: tuned porcelain, no SSS.** A standard PBR material through a material program (ops in
`docs/procedural-materials.md`):
- **Base:** warm ivory (0.80, 0.74, 0.64) to cool bone (0.78, 0.78, 0.80) as one routed mix. Roughness 0.32-0.40,
  metallic 0.
- **Rim:** `fresnel` (power about 3) into a faint cool emission (0.02-0.05), so the silhouette separates from the dark
  without a rim light. The entity FX `rimLight` or `fresnel` lanes can do this instead.
- **Fake translucency:** a tiny warm `fresnel`-inverse term in emission (≤ 0.03) gives the wrapped porcelain glow
  without SSS.
- **Socket darkening:** distance from the eye anchors in `localPosition`, through a `smoothstep` mask, multiplied into
  base colour (to 0.55-0.65). The mouth interior is the second GLB material, near-black, with its own program for the
  inner glow.
- **Chromatic possession** sits on top of this in the same program:
  - an `oklabRamp` of the palette roles, indexed by a slow noise in `localPosition` that scrolls with time;
  - mixed over the porcelain by a routed amount;
  - concentrated around the mouth and eyes by the same anchor masks;
  - returning to porcelain as the amount decays.
  Constants are parameters (`material/<program>/op/<n>/...`), so routes and the palette drive it.

**Palette** (linear RGB starting values; tune in the look pass):

| Role | Colour |
|---|---|
| Environment | Near-black indigo #07051A, to deep violet #160A2E at the top of the gradient |
| Secondary atmosphere | Restrained blue-purple #2A2458 (haze, 5-10% of frame luminance at most) |
| Primary emission | Magenta #FF2BD6 and violet #8A3BFF (hero cone core pale #FFE9F7) |
| Secondary emission | Electric blue #2B7BFF and pink #FF6FB5 (eye rays default blue) |
| Accent | Acidic red #FF2A1A (overload only) and pale white #F4F0FF (bursts) |

Colours change on musical structure, never on a free-running rainbow cycle. Use ADR-1420's palette selector, which
blends and advances on the music, or a routed `oklabRamp` position. The default stays magenta and violet.

**Composition:**
- The bust fills 35-45% of frame height, in three-quarter (yaw 20-30 degrees) or near-frontal view.
- Head slightly above centre. The mouth sits on the frame's horizontal centre line, so the hero cone has room toward
  the open side.
- 50 mm lens, camera about 2.1 m from a 0.6 m bust (or proportionally if scaled).
- No floor, no horizon.
- Background: the sky gradient (indigo to violet) plus a little height fog or haze. Sparse slow particles (dust motes,
  20-60 on screen) for depth.
- **Camera drift:** `kit.Scene.drift_camera` with a 60-90 s period, amplitude about 3% of camera distance, target
  amplitude about 1 cm. Never on the music.
- **Life:** the head's own slow, irregular rotation (2-4 degrees yaw, 1-2 degrees pitch from 2-3 incommensurate LFOs)
  and a ±1 cm float give the "faintly conscious" quality. Sound adds short accents (a 1-2 degree nod on strong
  transients, springing back).
