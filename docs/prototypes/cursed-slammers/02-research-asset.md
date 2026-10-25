# Cursed Slammers: character and asset research (Part B)

Prep, 2026-10-25, branch `scene/cursed-slammers`. **Nothing has been downloaded.** Every listing below was read
live through Sketchfab's public API (`api.sketchfab.com/v3/models/<uid>`) and its thumbnail on 2026-10-25. Licence
fields are quoted from the API. The repo is public: no downloaded or commercial asset is committed, whatever its
licence. Downloads go to a gitignored `assets/cursed-slammers/` with a manifest, as Talking Heads did
(`assets/talking-heads.manifest.json`).

**Headline.**
- **Head: a hybrid.** A CC-BY photogrammetry scan of a sculpted gorilla face (Gorilla Face, jtuhtan) has exactly the
  art-directed, deep-set, heavy-browed face the brief asks for, and an 8K texture. It is a front mask with carved
  eyes. We would complete it in Blender:
  - our own eyeballs and lids in cut sockets;
  - a jaw split off as its own hinged node;
  - a dark back-of-head mass, never lit.
- **Hands, bars, eyes, lids: our own Blender work.** These are procedural, committed, and made for AV Gen. No asset
  found has hands that grip a bar, and a gripping hand is mostly fingers wrapped round a cylinder, which is easy to
  author. The probe hand already shows the construction.
- **Fallback if the download is refused:** a fully procedural head in Blender (our own, committable), in the
  Talking Heads `make_bust.py` style but sculpted as an ape: crest, brow bar, muzzle, sockets. Weaker in surface
  detail, so it leans harder on lighting and the optical surface.
- **Animation without new engine code:** blink (the lid shells rotate about the eye centre), gaze (the eyeballs
  rotate), finger grip (per-finger nodes squeeze toward the bar axis), jaw (a split jaw node rotating at the hinge).
  The Hinge deformer (ADR-1672) is **on `scene/talking-heads`, not on main**. If it merges first, the jaw can open on
  the unsplit scan instead; the plan does not depend on it.

## 1. Candidate assets (live listings, 2026-10-25)

| # | Listing | Author | Licence (API) | Geometry | Texture | Rig / anim | Fit |
|---|---|---|---|---|---|---|---|
| **H1** | [Gorilla Face](https://sketchfab.com/3d-models/gorilla-face-808b2c0c11bf44c983e8b8c787ada844) (uid 808b2c0c…) | jtuhtan | **CC Attribution 4.0** | 559,884 tri, 281 k verts; photogrammetry (114 photos, PhotoScan) of a gorilla-face **sculpture** at Wilhelma Zoo, Stuttgart. A front mask: brow, sockets with carved eyes, nose, muzzle top; no back, no jaw below the nose | One 8192² colour (bronze-brown patina) | None | **Best face found.** Heavy wrinkled brow, deep sockets, a strong nose. The art-directed sculptural quality the brief prefers to "AI realistic" |
| **H2** | [Gorilla Head & Neck Reconstruction](https://sketchfab.com/3d-models/gorilla-head-neck-reconstruction-61faa99ff4af4acf8e07a72efedb9b3d) (61faa99f…) | Visible Ape Project | **CC Attribution 4.0** | 4,274,304 tri; CT-based anatomical reconstruction: one half skin, the other half musculature | None (one material) | None | **Anatomical reference**, and the complete head silhouette (crest, occiput, jaw, neck) to finish H1's back and jaw. The skin half can be mirrored |
| H3 | [Gorillakopf](https://sketchfab.com/3d-models/gorillakopf-d42767244cbf4fa1894cd4f4f8fe4b01) (d4276724…) | holzkunst (the carver's own work, [artist page](https://www.holzkunst.sh/kunstwerke/skulpturen/gorilla-kopf-2aeb724ac6bb6c59485be7c2106a65e5)) | CC Attribution 4.0 | 28,502 tri; photogrammetry of a wood carving, mouth wide open with fangs | 1 | None | **Optional**: a snarl for a later "second expression" flip layer. Not needed for stop 1 |
| H4 | [SAC ZOO gorilla bust](https://sketchfab.com/3d-models/sac-zoo-gorilla-bust-0ded6b1e9ec04fa6b7b1a40a99abd31e) | pixeldog4ever | CC-BY | 10,123 tri phone scan | 1 | None | Too coarse |
| H5 | [Silverback Gorilla Figurine](https://sketchfab.com/3d-models/silverback-gorilla-figurine-926e2a5d9b7a4ff883f8957b3d5b3dc4) | tyjajack | CC-BY | 200 k, full body; a 6-inch figurine | 8192² | None | Head too small; hands planted on the ground. Reject |
| H6 | [Gorilla inspired creature](https://sketchfab.com/3d-models/gorilla-inspred-creature-935fc026f1ff44d0b61b46bdedacc6f7) | MatStar | CC-BY | 347 k bust sculpt | 1 | None | Humanoid creature, not an ape. Reject |
| H7 | [Gorilla Realistic… Beast](https://sketchfab.com/3d-models/gorilla-realistic-gorilla-3d-character-beast-89c139c978e34f64a10c7e710a4a7c94) | nd-animation | CC-BY | 1.5 M, full body | 4 | None | Published 2026-09-01. Marketing copy ("before purchasing"). Provenance and quality unverified, possibly generated. Reject |
| H8 | "Cursed Gorilla", "Brutal Primal Ape Warrior" | s8819296 | CC-BY | Exactly 500,000 / 964,711 tri | 4 | None | The signature of a generated mesh, and generic. Reject (the brief: no "generic AI-realistic horror") |
| H9 | King Kong 2005 / Skull Island / "Realistic King Kong Rig" (several) | various | CC-BY on the page | 100-700 k | yes | Some rigged (18-23 clips) | **Reject**: fan recreations of a film-studio character. The brief wants a Kong-*inspired* ape, "not an exact recreation", and the IP risk is not the uploader's to licence |
| H10 | Chimpanzee Hand (scifri), Gorilla Upper Limb (VAP) | | CC-BY | 8.7 M / 1.35 M | none | none | A skeleton and musculature, not a skin hand. Reject |
| — | Smithsonian 3D gorilla crania, mandibles, hand bones ([3d.si.edu](https://3d.si.edu/object/3d/gorilla-gorilla-gorilla-cranium%3A40b67fa6-41de-40d5-894e-db46223832f7)) | | CC0 | bone | | | Skull reference only |

No CC0 or CC-BY *skin* gorilla hand exists in these listings. Fan-game "gorilla hands" are cartoon low-poly.

Visual check: `~/Desktop/av-gen-review/cursed-slammers/prep/candidates-*.jpg` (the listings' own thumbnails, for
review only; never in the repo).

### 1.1 Risks on H1 (for the owner)

- **It scans someone else's sculpture.** The uploader licensed the *scan* CC-BY. The sculpture at Wilhelma is by an
  unnamed artist, and German freedom of panorama covers photographs of public works, not 3D replicas. For a live
  visual in a private performance the practical risk is low, but it is the owner's call. The asset is never
  redistributed (gitignored), and CREDITS.md would carry the CC-BY attribution if it is used.
- **It is a mask.** There is nothing behind the face, and no jaw below the upper lip. In a full-screen, near-frontal
  composition with ±1 unit of lateral drift, the edges fall into darkness. The missing jaw and back come from H2's
  silhouette, or from our own sculpted volume, kept unlit.
- **Carved eyes.** The sockets hold carved stone eyes. We cut them out and seat real eyeballs (a wet cornea and an
  iris), so we can have a catchlight, a gaze and a blink.
- **Downloading needs a signed-in Sketchfab account** (an API token or the browser), as Talking Heads found. The owner
  downloads, or gives an agent a token.

## 2. Download list (needs the owner's approval)

| # | What | Why | Size (est.) | Where it would live |
|---|---|---|---|---|
| D1 | **H1 Gorilla Face** (jtuhtan), glTF | The hero face: brow, sockets, nose, muzzle. Decimated to about 60-80 k tri with a baked normal map; 8K colour downsampled to 4K | about 150-250 MB source | `assets/cursed-slammers/source/` (gitignored); processed `assets/cursed-slammers/cs-face.glb` |
| D2 | **H2 Gorilla Head & Neck Reconstruction** (Visible Ape Project), glTF | Completes the head: the crest, the occiput and the jaw silhouette behind and below H1's mask. Also the anatomical reference for our jaw and lids | about 150 MB | same, processed into `cs-skull-mass.glb` (unlit back volume) |
| D3 (optional) | H3 Gorillakopf (holzkunst) | A snarl for a later expression-flip layer. Not needed for stop 1 | about 10 MB | same |

If D1 is refused, the build uses the procedural fallback (§3.2), and D2 alone still helps as a silhouette reference.

## 3. The Blender route

The Blender MCP is configured but **not connected** (`localhost:9876` refused on 2026-10-25). Use headless Blender
5.2.1 (`/Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup -P <script> -- --out <dir>`), as
`tools/talking_heads/make_bust.py` does on `scene/talking-heads`. Conventions to copy from it:
- Blender Z-up, face looking -Y, so the GLB is +Y up with the face toward +Z;
- `export_yup`, `export_apply`, normals on;
- deterministic;
- an anchors JSON with pivots and statistics.

### 3.1 With D1/D2 (recommended): `tools/cursed_slammers/make_ape.py`

1. **Import and clean H1.** Decimate to about 70 k tri (planar plus collapse). Bake the full-resolution detail to a
   2-4K tangent-space normal map. Downsample the colour to 4K and grade it toward near-black brown in the bake.
   That gives `cs-face.glb` (gitignored output; the scene references it).
2. **Sockets and eyes.** Boolean out the carved eyes. Add the eyeballs (`cs-eye.glb`, committed, our own):
   - a sphere of about 0.07 face-widths;
   - an iris disc and pupil as material regions, plus a separate cornea shell (glossy) for the catchlight;
   - the pivot at the eye centre.
3. **Lids** (`cs-lid-upper.glb`, `cs-lid-lower.glb`, committed): spherical-cap shells 4-6% larger than the eyeball,
   pivoting at the eye centre. A blink is a rotation of the upper lid about the eye's x axis, 0 to about 35 degrees.
   This needs no deformer.
4. **Jaw** (from H1's lower edge plus H2's mandible silhouette): separate the muzzle below the upper lip into
   `cs-jaw.glb` (gitignored, since it derives from D1), its pivot at the temporomandibular hinge. A jaw movement is
   a 0-6 degree rotation of that node. The seam sits in the lip line's shadow.
5. **Back mass** (`cs-skull-mass.glb`): H2's skin half, mirrored, decimated to about 15 k, fitted behind H1. Drawn
   near-black with only the rim light, so it gives the silhouette and the crest.
6. **Hands** (committed, our own): `cs-hand-l.glb`, `cs-hand-r.glb`. Per finger, its own GLB or mesh part so each can
   be a node:
   - four fingers of three phalanges each, wrapped round the bar's front, with big knuckle pads;
   - a thumb round the other side, a palm and a wrist going back into darkness;
   - fused by voxel remesh, then sculpted wrinkle and knuckle detail by displacement (a Voronoi crease texture plus
     noise), and decimated to about 25 k per hand;
   - a fingernail region as a material slot (glossy).
   The probe hand (`probe/make_probe_subject.py`) is the starting construction. Its fingers read as sausages, so the
   production one needs tapering, flattened dorsal planes, and creases at each joint.
7. **Bars** are the engine's procedural `cylinder` (one draw, `linear` distribution), not a GLB:
   - radius about 0.05;
   - 4 bars, about 0.9 apart, running out of frame top and bottom;
   - no crossbars, no cell (the owner's note);
   - a lathe profile with two faint collars could be added if plain rods read too CG.
8. **Anchors:** `cs-ape.anchors.json` holds the eye centres, the jaw hinge, the lid pivots, the finger pivots (on the
   bar axis) and the mesh statistics.

### 3.2 Without D1 (fallback): procedural head

- A generalised-cylinder head like `make_bust.py`, re-profiled as a gorilla:
  - a sagittal crest;
  - a massive brow bar overhanging deep sockets;
  - a flat broad nose with flared nostrils;
  - a protruding muzzle with a lip line;
  - a heavy jaw.
- Gaussian offsets give the features. A Voronoi crease and noise displacement in Blender gives the brow and muzzle
  wrinkles, and a fur-clump displacement over the crown and cheeks.
- The probe head (voxel-fused ellipsoids) proved the volumes and the socket cuts, but it reads as a toy. The fallback
  needs the `make_bust.py` precision. Expect a full day of sculpting iterations, against about half a day for the
  hybrid.

### 3.3 Fur

- No hair or strand renderer suits a live face: fibres exist (ADR-1180, Chorus Field), but 50k+ strands on a face is
  the wrong spend.
- Use **sculpted fur clumps** (displacement) on the crown, cheeks and back mass, plus the skin program's anisotropic
  sheen (`anisotropy` along the clump flow). Dark, coarse, with selected wet skin (nose, lips, brow pads, knuckles)
  as a lower-roughness region driven by a curvature or cavity mask (`curvatureMask`, `cavity`).

## 4. Depth separation (with 01 §4)

| Layer | Depth (z) | Contents | Scale |
|---|---|---|---|
| Near | 0.0 | Bars; both hands gripping the inner bars, outboard of the face silhouette, at muzzle height | Hands about 0.45 tall (23% of frame height) |
| Key | -2.2 | Face (H1), eyes, lids, jaw | Face fills about 70-80% of frame height when revealed |
| Back | -2.6 to -3.2 | The back mass (silhouette, crest) | |
| Substrate | -4.5 | Ridged near-black "print" | Fills the frame at any camera position |
| Haze | -7, -10 | Two flat glint and haze cards | |

The hands are never joined to the head on screen: the arms dissolve into darkness behind the bars (wrist emission 0,
the key light cut off by the spot cone). That is what keeps them "in another optical layer".

## 5. Small animations: what is feasible now

| Motion | Method | Engine change | Cost |
|---|---|---|---|
| **Blink** | Upper-lid node rotation about the eye centre (`nodes/cs-lidU-L/rotation` x), keyed in the cycle (2-3 slow blinks in Presence), plus an occasional `response.low` accent | None | 2 tiny nodes per eye |
| **Gaze** | Eyeball node rotation (yaw ±8, pitch ±4 degrees) from `noise` sources at 0.03-0.06 Hz. In Disturbance the gaze **locks on the camera** (yaw tracks the camera's x: a timeline route mirroring the camera track); the "eyes follow you" card | None | None |
| **Finger grip** | Each finger a node pivoting on the bar axis. Grip = scale x/z 1.0 to 0.97 (a squeeze onto the bar) plus 2-3 degrees of rotation round the bar, staggered per finger by 40-80 ms; the knuckle program's wet highlight brightens. Driven by `response.low` (HIT envelope) times the presence gate, plus keyed tightenings | None | 8-10 nodes |
| **Jaw** | Split jaw node rotation 0-6 degrees: a slow breath, a small part on Disturbance | None (or Hinge, ADR-1672, if it reaches main first) | 1 node |
| **Head** | `float` effect (tiny), a 1-2 degree slip out of alignment with the bars in Disturbance (node position x +0.04) | None | None |
| **Brow** | A Hinge region on the brow would be ideal | Needs ADR-1672 on main | Later |

## 6. Bars

- Procedural `cylinder` (radius 0.05, height 6, 32 sides, no caps), `linear` distribution of 4. One draw.
- Program `cs_bar`: silver metal with a muted foil F0 that turns with the view, and anisotropic roughness about 0.12
  along the bar. That gives a hair-thin vertical highlight (verified in P2).
- The highlight is made *occasional* by routing the sweep light's intensity from the timeline and the highs (03 §4).
