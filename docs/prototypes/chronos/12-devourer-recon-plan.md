# CHRONOS, The Devourer: Gate D0 recon and plan

The brief is `10-devourer-brief.md` and the working rules are in `11-devourer-coordinator-notes.md`. Media is in
`~/Desktop/av-gen-review/104-chronos/devourer-d0/`. ADRs continue from 1934.

## 0. Where the branch stands

- **The canyon is gone.** c83ed2c1 removes the `chronosCanyon` block, its generator, its tests and `examples/chronos`
  (ADR-1933 superseded; ADR-441, no shims). `composition.{hpp,cpp}` are back to main's text.
- **What stays, and is the "existing working slit-scan" the brief means:**
  - ADR-1931: the ring (fixed capacity of up to 256 layers, a routable span, a stride, per-layer metadata) and the
    composed sampler, `temporal/sample` (direction, radial or luminance map; strips; edge; blend; moving scan; two
    fetches at any span; 0.09 ms at 1080p);
  - ADR-1932: the render pre-roll;
  - the debug-capture fix;
  - ADR-1930: the shared RTIN mesher, on main.
- **Both suites:** results are in `PROGRESS.md` (run on the post-removal head in `../av-gen-chronos-ci`).

## 1. The references

`~/Desktop/chronos/` holds **14** images, not 15. They are the owner's and read-only; none is copied into the repo.
They are numbered here in filename order, r01-r14.

| # | What it is | Class | What to take from it | What to leave |
|---|---|---|---|---|
| r02 | Goya, *Saturn Devouring His Son* (detail) | **creature, primary** | The whole emotional core: the bulging white stare with tiny pupils, the face lit from one side and lost in black, the mouth as a black hole in a gaunt face, the matted grey hair as a mass rather than strands, ochre and umber flesh, the hands gripping. The horror is in the eyes and the strain, not the gore. | The blood and the body being eaten (brief §2.1: no gore). |
| r05 | Painted gaunt ghoul, fanged maw, long arms, blue-grey | creature | The impasto, smeared, unfinished paint quality; the jaw packed with long irregular teeth; the hunched mass dissolving into dark. | The cold blue palette, the full body, and the generic-monster fangs (the brief says no video-game boss). |
| r10 | Close-up of a fleshy maw with dozens of sheet-like teeth, backlit red-orange | creature (mouth) and effect | **The mouth interior:** teeth as layered translucent slats lit from behind, a hot glow deep in the throat, and wet, swollen, crimson lips. It is the best reference for "the interior transitions into something impossible". | The cartoon lip blob; the mouth must stay on a human-ish face. |
| r14 | Fleshy vortex of teeth ringed by clawing figures, all crimson | creature (mouth) and atmosphere | The ring of yellowed, irregular teeth around a black centre; the figures being drawn in; a near-monochrome crimson that still reads. | A literal circular toothed portal (the brief rules out a circular portal) and the clutter. |
| r04 | Painted file of naked figures marching in line under a smoky sky | **souls, primary** | **The assembly line in one image:** identical, anonymous, repeated figures in single file receding into the distance, read as a process. Smoky, desaturated ground. | Upright walking (ours lie horizontal) and the hooded heads. |
| r06 | A pale, white, smooth figure lying horizontal in a starfield, edges dissolving upward | souls | **The soul look:** a pale, simplified, almost porcelain body lying flat, with its edges shedding particles. This is the Stage A/B silhouette. | Navy blue (off palette). |
| r03 | Pencil drawing of elongated, smooth figures, horizontal, joined by tendrils | souls and effect | Bodies stretched along their length while staying anatomical, and soft smooth forms. It is the Stage C "stretching" target, drawn. | The tendril umbilicals. |
| r07 | Painted figures drifting in a murky green shaft of light | souls and atmosphere | Silhouettes at many depths in haze; scattered flecks; light through murk giving depth with no environment. | The green. |
| r11 | Expressionist painting of figures falling and tumbling through a dark room | souls | Varied poses, foreshortening and loose painterly flesh: the souls must not be clones. | The domestic interior. |
| r13 | Black silhouettes floating against a starfield, seen from below | souls and atmosphere | Silhouette legibility: the figures read purely by shape, splayed limbs and varied orientation. | The stars, too pretty for this scene. |
| r12 | **Slit-scan video art:** a person scanned into stair-stepped, wave-folded bands against black | **temporal effect, primary** | **The look of Stage C-D:** the body swept into horizontal ripples and folds, isolated on black, still recognisable as clothes and skin. That is what a time map on a moving figure produces, and it is exactly the engine's sampler applied to an isolated subject. | Nothing. This is the bar for the devouring effect. |
| r01 | A red tunnel of concentric rings with light at the end | temporal effect (mouth) and atmosphere | Depth recession by repeated rings: the "receding into the throat" idea. | A literal tunnel or portal. Used only inside the mouth, and only as recursion. |
| r08 | A black and red painted spiral, brush-stroked | atmosphere / effect | The brush-stroke texture and the near-black-and-crimson palette in a single gesture. | **The spiral and vortex.** The owner's standing rule is no vortex or funnel heroes; the background must not swirl. |
| r09 | A black hole with red and white fibrous radial streaks | atmosphere / effect | Radial fibres pulling inward, as fragment trails converging on the mouth during Stage E; the red and white contrast. | A radial burst framing the face (it would compete with the face). |

**The hierarchy, as the brief orders it:**
- r02 governs the creature.
- r12 governs the effect.
- r04 and r06 govern the procession.
- r10 and r14 inform the mouth interior.
- The rest are texture and palette.

The **palette** across the set agrees with the brief: near-black, crimson, ochre flesh and bone. The two cold outliers
(r05, r06) contribute shape, not colour.

## 2. The engine, as it bears on this

| Need | What exists | Gap |
|---|---|---|
| A skinned creature with a jaw and eyes | The glTF importer brings in rigs and clips. A 164-joint MPFB rig imports and draws (Strange Attractor Art Pass 2, proofs 06-07). | **Morph targets are ignored**, so the face moves through bones only: the MPFB default rig's `jaw`, `eye.L`/`eye.R`, `head`, `neck01-03` and tongue bones. Enough for the brief's restrained animation (§3.4). |
| Bones driven by parameters, audio and the timeline | Skinned entities, clips, and parameter routes. | How a route reaches a single bone's rotation, as opposed to a clip's time, is to be confirmed in D1. The fallback is two short clips (jaw open, eyes left and right) driven by clip time from a parameter. |
| Many posed human figures | MPFB CC0 humans (proven in Strange Attractor: 40-65k triangles, 163 bones); static meshes shared across entities. | The souls need no runtime skinning. Poses are baked in Blender and exported as static meshes: 6-8 pose variants, decimated to about 15k triangles, shared. |
| Temporal effects on some objects only | The ring captures the whole frame. The **identifier target** (ADR-035, R32Uint, every opaque surface writes its entity id) is already rendered. | Nothing is localised today. See §5. |
| Background chaos | Particles, the analytic sky, post shader layers (ISF), bloom, grain, vignette. | The **volumetric fog march cost 39.8 ms** in the canyon (Gate 1). It must be avoided here, or kept tiny and low-resolution. |
| Drum-driven behaviour | `stem.drums.*` and `role.drums.*` (level, onset, onsetStrength, onsetLow/Mid/High, centroid); `feeds.beat = stem.drums` drives the beat clock and `response.*` from the drum stem. Feline Footwear has a Drums stem. | None. Routes and envelopes cover it. |

## 3. The creature route

**Recommended: MPFB as the anatomical base, pushed far past human, then sculpted in Blender headless.** It is not a
free monster model: the brief rules out generic monsters, and the free ones are exactly that. MPFB gives three things
no free monster does:

- real human head topology that holds up close, with eyes, eyelids, teeth and a tongue as separate meshes;
- a CC0 licence on everything exported (`assets.md`);
- a rig with jaw, eye, head and neck bones that the engine already imports.

**What D0 proved (`devourer-d0-look-*.png`).** The MPFB route has real expressive range. With its own anatomical
targets at their limits:
- cheeks hollowed and cheekbones raised;
- eyes pushed deep and scaled up, under heavy bags;
- the temples sunk;
- the head lengthened;
- the chin and jaw dropped;
- asymmetry;
- the expression units for the stare, the open mouth, the inner-brow lift and the corded neck;
- the jaw bone opened to 26-34°.

The face already has Goya's wide, frightened, hungry stare and a strained black mouth. It is lit in chiaroscuro: a
hard amber key from high on one side, a faint crimson from below, and a burgundy rim.

**What D0 also shows, honestly.** Today it reads as a screaming old man, not an ancient inhuman entity. The gap is
known, and it is D1's work:

1. **Proportion beyond human.** A longer skull, deeper orbits, the cheekbones pushed out, the jaw opening past the
   human limit (to 60° or more, with the lower jaw lengthened so it hangs), and the lips thinned and drawn back over
   the teeth. This is scripted sculpting: region-weighted displacements on the MPFB mesh from its vertex groups and
   rig weights, plus a sculpt-brush pass by script where needed.
2. **The teeth.** MPFB's teeth are too neat. Rebuild them as irregular, long, yellowed, uneven pegs: generated in
   Blender, a few per side, each with its own length, lean and wear. r10 and r14 are the reference.
3. **The skin.** Grade the MPFB old-skin textures toward ochre, umber and dried red. Add wrinkle and pore detail as a
   baked normal map (Blender's multires bake from a displaced high-resolution copy), and a painterly dirt layer, so
   the face reads as Goya's paint and not as clean CG.
4. **The hair.** MPFB's `long01` cards sit like a wig on a bald crown (visible in the stills). Replace them with
   tangled, matted grey hair cards generated from Blender hair curves (clumped, frizzed, then converted to card
   meshes), framing the face and falling into darkness. The engine's shell-fur path (Strange Attractor's Sasquatch)
   is the fallback.
5. **Scale cues.** The face must read as enormous: soft atmospheric depth, sharp near detail, and souls 1.8 m long
   against a head about 30 m tall. The camera is long-lens, slightly low and close.

**How it holds up close.**
- MPFB's head is about 6-8k vertices before subdivision. Export one Catmull-Clark level for the face (about 30k),
  with the baked normal map carrying the wrinkles. The camera's closest framing is the mouth (D0 still 3), which
  holds.
- Budget: **≤ 120k triangles** for the creature (face, teeth, tongue, eyes, hair cards, neck and shoulders), one
  skinned mesh, rig trimmed to about 20 bones (face, neck, spine top).

**The fallback route** (only if D1's sculpt stalls): a hand-built expressionist head in Blender from a sculpted base,
with no MPFB. It costs more time and has no proven rig. Not recommended.

## 4. Rig and animation (D1)

- **Bones.**
  - `jaw` opens and closes (slow breathing, and wide at consumption).
  - `eye.L`/`eye.R` aim at the next soul: a point in the procession, smoothed.
  - `head`/`neck` give a slow drift of a degree or two.
  - The tongue bones curl inside the mouth (subtle).
- **No morph targets** (the importer ignores them). Expression changes are therefore bone poses plus the jaw. The
  brief asks for restraint, which suits this.
- **The rest pose is baked with the MPFB expression units applied,** so the stare and the strain are the mesh itself.
  The rig moves it.
- **To confirm in D1:** whether a scene parameter can set a bone's rotation directly (an effector or a pose route).
  If not, the jaw and eyes are clips whose time is the parameter. Either way, audio and the timeline drive them
  through ordinary routes.

## 5. Localising the temporal effects (the central technical question)

The ring holds the whole frame, and the sampler today applies one age map to every pixel. Three ways to keep the
effect on the souls (and the mouth) and off everything else:

| Approach | How | Cost (1080p, M2 Max) | Verdict |
|---|---|---|---|
| **A. Identifier-gated history** (recommended) | Capture the identifier target into a second ring channel: R16Uint, point-sampled, half resolution. When the sampler fetches a past texel, it checks that texel's id. A soul id keeps the past colour; anything else falls back to the current frame. Per-soul parameters (stage A-E, span, direction toward the mouth) come from a small uniform table indexed by soul id. | **No extra scene pass.** The id ring at 64 layers is 960×540×2 B × 64 = **66 MB** (33 MB with R8Uint, ≤ 255 ids). One extra point fetch per tap, about +0.05 ms. ADR-410 already proposed id-masked reads (its §12 "object-level datamosh"), so this is the design it named. | **Recommended.** The background and the face can never be smeared, because their past pixels are rejected. A soul's trail lands where the soul was, over whatever is behind, which is the look of r12. |
| B. Isolated soul layer | Draw the souls in their own pass into an RGBA target with alpha, keep a ring of that layer only, run the sampler on it, and composite over the scene with depth. | One extra pass for the souls' geometry (small). An RGBA16F ring at half resolution, 64 layers: **265 MB**. A composite pass, about +0.3 ms. Engine work: an entity flag and a second target. | The fallback, if A's trails show artefacts where souls overlap each other or the face (a past soul texel hidden behind something else is lost to A, and B keeps it). |
| C. Depth-aware shared history | Separate the souls from the creature by depth, using a depth ring. | +33-66 MB depth ring, with heuristic thresholds. | Rejected: fragile where souls pass in front of the face, which is the whole composition. |

**How the stages come out of A.** All of these are the existing sampler, gated and parameterised:
- **Each soul's progress p** (0 far, 1 swallowed) is a pure function of its position along the procession. A soul's
  per-pixel age is `span(p) × q(d)`, with d taken along the soul's travel direction toward the mouth.
- **Stage A** (p < 0.3): span 0, so the figure is intact.
- **Stage B:** a small span plus strips, giving echoes and horizontal bands.
- **Stage C:** a long, continuous span, so the body stretches along its path: r12's folds, and r03's elongation.
- **Stage D:** strips with a per-strip random age offset, so limbs, torso and head show different moments. This is
  the brief's fracture, as a sampler term (planned ADR, Gate 2 of the old plan).
- **Stage E:** the soul's spatial footprint is squeezed toward the mouth's centre (an address warp in the same pass),
  narrowing to a ribbon, and its alpha falls to zero inside the mouth mask.
- **The gradualness is built in:** p moves continuously, so no switch ever flips.

**The mouth.** The mouth interior is its own id (a dark throat mesh inside the jaw). Inside that mask:
- the sampler shows progressively older soul imagery, zoomed toward the throat's centre;
- the planned accumulate-feedback mode (approved at Gate 0, a bounded IIR) is restricted to the mask, so swallowed
  images recede and decay into the dark, suggesting the throat consumes their whole history;
- a localised red and amber light pulses with consumption.

It stays anatomical: lips, teeth and tongue are ordinary geometry in front of the masked interior. It is never a
glowing sphere or a ring portal.

**What to build (ADR candidates).** Only what this scene needs:
- the id channel and id gating, with a per-id parameter table (one ADR);
- the fracture and squeeze terms in the sampler (one ADR);
- feedback-accumulate, restricted by mask (one ADR).

No new subsystem.

## 6. The souls

- **Source.** MPFB CC0 adults (the Strange Attractor route), 6-8 base variants: varied sex, build and height.
  - Neutral minimal clothing: plain pale wraps or shorts and a simple top, from MPFB's CC0 garments recoloured to
    grey-white. Never sexualised.
  - Skin desaturated toward pale grey-ivory (r06).
- **Poses.** Lying flat, limbs varied: arms at sides, arms up, one knee bent, curled, reaching. These are baked to
  static meshes in Blender, so there is no runtime skinning. About 15k triangles each.
- **Variety without clones:** pose × variant × a small per-instance roll, a scale of 0.95-1.05, and a slight tint
  shift. A few with a slow drifting limb, as a later polish if cheap.
- **The procession.** A spline the souls ride at a constant spacing, entering from frame right, low and far. It
  sweeps toward camera, then curves up into the mouth. Positions are a pure function of time (scrub-safe), and the
  souls are recycled at the start.
  - Visible count: about 16-24.
  - Speed from a parameter (drum sustained energy can nudge it later).
  - The direction must read at a glance; r04's single file is the model.

## 7. The background (Phase 4)

**No environment.** The layers, back to front:
1. A near-black to burgundy gradient sky (the analytic sky with its colours, or a shader layer).
2. A shader-layer field of slow, smeared, brush-like noise in dried red and umber, low contrast, drifting: r08's
   texture without its spiral.
3. Indistinct organic forms: large, soft, dark shapes that loom and recede in the haze (low-detail meshes with
   heavy fog, or the shader layer's own noise).
4. Sparse drifting particles: ash and embers.
5. Occasional red and amber light pulses (lights or shader-layer flashes on drum events).
6. Analog texture in post: grain, slight horizontal line jitter, and vignette.

The hierarchy is kept by contrast. The background never exceeds about 25% of the face's luminance and has no
saturated reds near the face.

**The fog:** the volumetric march is out at 1080p30, as Gate 1 measured. Haze comes from distance fog (cheap) and
the shader layer.

## 8. Drums (Phase 5)

The project feeds the beat from `stem.drums`. The routes, each enveloped and bounded:

| Signal | Behaviour |
|---|---|
| `stem.drums.level` | the overall intensity: the light, and the background drift |
| `stem.drums.onset` (thresholded, with an attack-release envelope) | a jaw snap of a few degrees, and a fracture kick on the soul in Stage D |
| `stem.drums.onsetStrength` above a high threshold, with a refractory period | a consumption surge: the mouth's feedback and light flare |
| sustained level (slow attack and decay) | the procession speed, within ±20% |
| silence | the jaw breathes slowly and distortion relaxes |

The scene works with audio off: every route adds to a timeline-authored rest state.

**Music:** Feline Footwear (its Drums stem), unless the owner prefers another track (§11).

## 9. Budgets (1080p30 live, High tier)

| Item | Budget |
|---|---|
| Creature | ≤ 120k triangles, 1 skinned draw, ~20 bones |
| Souls | ≤ 24 visible × 15k = 360k triangles, static, 6-8 shared meshes |
| Background | 1 shader layer, ≤ 20k particles, ≤ 3 lights |
| Temporal | ring 64-128 layers half-res colour (133-265 MB) + id ring (33-66 MB); sampler + gating ≤ 0.3 ms; mouth feedback ≤ 0.3 ms |
| Frame | ≤ 25 ms GPU (scene ≤ 15 ms, post ≤ 3 ms); no volumetric march |

Profiled per pass from the frame timeline, at each gate (Test F at D3).

## 10. Phase plan against the gates

| Gate | Work | Shows |
|---|---|---|
| **D1** | The creature: the scripted sculpt beyond human; the teeth; the skin grade, wrinkle normal map and dirt; the hair cards; the trimmed rig; export to GLB; a scene with the camera, the chiaroscuro lights and a dark background; jaw breathing and eye drift; bone-driving confirmed. | Test A: the portrait, stills and a 20 s clip. |
| **D2** | The souls (variants and poses, baked); the procession spline; the id ring and id gating (ADR); the per-soul stage table; fracture and squeeze; one soul through A-E; the mouth mask. | Tests B and C. |
| **D3** | The full procession; the mouth's accumulate-feedback (ADR); the background layers; the drum routes; the polish; the profile; a full render with sound; both suites at exit 0. | Tests D, E and F. Ready for merge. |

## 11. Risks

| Risk | Mitigation |
|---|---|
| **The face reads as a screaming old man, not Chronos** (the D0 state) | D1's sculpt beyond human, the teeth, the paint-like skin and the hair are all aimed at this. The owner judges the face before anything else is built (the coordinator's stop). |
| **No morph targets in the engine,** so expressions are limited to bones | The brief asks for restraint. Bake the expression into the mesh and animate the jaw, eyes and head. If D1 shows it is too stiff, a morph-target import is a contained engine feature, but only with the owner's approval. |
| **Id gating's edge cases:** half-resolution ids at silhouettes, and a soul's past texel hidden behind another soul | Point-sample the ids. Use the closest-soul-wins rule and accept the occlusion loss, or move to approach B. |
| **The uncanny edge:** realistic naked humans in a horror scene | Neutral garments, pale and desaturated, never sexualised; the horror stays in the face and the temporal treatment. |
| **Performance:** a skinned creature, 24 souls and the effects at 1080p30 | Static souls, a trimmed rig, no volumetric march, per-pass profiling at every gate. |
| **Owner sensitivities:** flicker, and no vortex or funnel | The fracture's randomness only re-rolls on events. No spiral background (r08 is a texture reference only). The throat recursion is a zoom inside the mask, not a funnel hero. |
| **The shared GPU** with the Strange Attractor agent | Every run goes under `tools/gpu-lock.sh` with a long timeout, and measurements are repeated. |

## 12. Decisions for the owner

1. **The creature route:** MPFB base, scripted beyond human, plus sculpt, teeth, skin and hair (recommended), against
   a from-scratch sculpt.
2. **The localisation approach:** identifier-gated history (recommended: 33-66 MB, no extra pass), against an
   isolated soul layer (265 MB, an extra pass), which is kept as the fallback.
3. **Music:** Feline Footwear's Drums stem, or another track with a drum stem.
4. **The composition:** the frontal three-quarter view (D0 still 2, more sculptural, and the mouth reads in profile
   depth), or direct frontal (D0 still 1, more iconic, and the mouth faces the camera as the procession's
   destination). Both will be tried in D1; a preference now steers the sculpt.
5. **The souls' clothing:** plain grey-white wraps (recommended), or simple undergarments.
