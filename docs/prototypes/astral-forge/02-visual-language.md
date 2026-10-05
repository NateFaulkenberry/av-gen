# The Astral Forge: Visual Language

Brief §2, written after the research (`01-research.md`) and before implementation. It is the grammar the
prototype must obey, and each test in `05-tests.md` is judged against it.

## The one-sentence idea

**A forge in the void: metal dust that is hammered, by music, into a god, and then breaks.** The entity
is made of the same matter at every scale. What changes is how strongly that matter is held to a latent
anatomy (*coherence*), and how hot it is (*temper*).

The title gives the colour logic. Metal in a forge changes colour by **temper**: a thin oxide film grows
with heat, and its interference colour runs straw → bronze → violet → blue. The entity's colour is
therefore its *thermal and energetic history*, not a palette. It is gunmetal when cold, it blooms through
gold into violet and deep blue when charged, and it throws molten sparks when it breaks.

## The four scales

| Scale | What it is made of | How it is rendered | What moves it |
|---|---|---|---|
| **Micro** | metal flakes, shards, tiny plates; a few hot cores; engraved micro-grooves on surfaces | oriented flakes splatted in compute, **dark until a flake's normal catches a light band** (glints); groove normals and diffraction on surfaces | high band: flake tumble rate, glint density, groove crawl |
| **Meso** | filaments migrating along engraving lines; plates where matter is dense; incomplete surface patches; tendons of bound flakes streaming between features | the density iso-surface where matter is crowded; streaked flakes where it moves | mid / high-mid: filament flow speed, breakup |
| **Macro** | eyes, mouths, plates, horns, wing blades: anatomy | the iso-surface, sharpened toward the latent anatomy as coherence rises | coherence, archetype, fold |
| **Meta** | the entity as one condensation in a much larger dust field; a second, larger face sometimes implied by the dust itself | unbound "drifter" dust out to many times the entity's size, and a dim haze integrated from low density | sub: global mass, drift |

**The entity has no outline.** Unbound dust is everywhere, and the surface exists only where matter is
dense. The viewer can never point at the edge.

## Form rules

1. **Faces are masks, not heads.** A curved engraved plate with sockets, eyes and a mouth slit. There is
   no skull behind it, no jaw hinge, no nose cavity and no hair.
2. **The eyes come first.** Eye matter binds at the lowest coherence, then the mouth line, then the brow
   and plate, and the appendages last. A face is read through its eyes long before it is complete (the
   pareidolia ordering).
3. **Every face is wrong in one way.** Three concentric pupils; a mouth holding a smaller face; one half
   smooth and nearly human, the other faceted; an eye that sits too deep; teeth that continue into the
   cheek as engraving.
4. **No architecture.** Every structure must be read as anatomy, organism or instrument: blades,
   plates, rings, horns, tendrils. Forbidden: vertical stacks, columns, arches, grids of openings,
   symmetric towers, anything with a floor. Rings are allowed as eye rings and gyroscopic organs, never as
   arches or portals. **Check each still:** could this be a building, a gate or a monument? If yes, it is
   rejected.
5. **Never static.** At coherence 1, matter still streams along the engraving, flakes still glint, some
   still escape and get re-absorbed, the anatomy breathes, and the engraving crawls.

## Material rules (brief §9)

| Layer | Implementation | Restraint |
|---|---|---|
| Base | dark conductor: reflectivity 0.35-0.55 (gunmetal), edge tint toward silver | never black plastic: the Fresnel rim must always be visible |
| Microstructure | guilloché: rose lines around eyes and mouth, contour lines wrapping the form, a second rotated family for moiré; V-groove normals; multi-octave by pixel footprint | lines fade with distance rather than alias |
| Reflection | the reflection-only light bands (black-sweep studio strips) | the bands are never seen directly; the background stays black |
| Fresnel | conductor Fresnel with edge tint | the main source of "edge light" |
| Iridescence | thin-film temper colour from energy (oxide thickness 0 → ~420 nm); grating colour on grooves at specific angles only | colour appears **at angles**, never everywhere at once |
| Emission | molten heat only: in cracks of a collapsing surface and in released sparks | under 5% of the screen except during collapse |
| Particle core | ~1% of flakes are hot cores, brighter than the surface | they mark the field's "nervous system" |
| Edge energy | thin spectral rims on important anatomy (eye rings, mouth edge), from the grazing thin-film term | only on the features the viewer should read |

## Colour rules (brief §10)

- The base palette is near-black, charcoal, gunmetal and silver, which is what the metal does with
  neutral light.
- **Hue only comes from three physical mechanisms:** thin film (temper), diffraction (grooves) and
  incandescence (heat). No mechanism produces a constant rainbow.
- The archetypes differ by **temper range and light-band temperature**, not by tint:
  - **Seraph:** low temper (silver, white, faint cyan from cool bands) rising to violet at climax.
  - **Abyss:** thick temper (violet, deep blue), deep-red heat in the mouth.
  - **Machine God:** untempered silver-gunmetal, with diffraction on very regular grooves (the most
    "holographic").
  - **Choir:** straw-gold to bronze temper (the warmest), which turns violet as the faces merge.
  - **Chimera:** mixed temper per face.
- **Greyscale test.** Every still is also judged desaturated. If the form disappears in grey, the still
  fails.

## Light rules (brief §11)

- **No visible light sources, no studio key, no fill.** The only lights are:
  1. the reflection-only bands (thin, very bright strips at a few great circles, slowly orbiting);
  2. the entity's own heat;
  3. glints.
- The bands are what the music moves: a snare sweeps a band across the entity, the highs flicker band
  intensity, and phrases rotate the whole rig.
- Depth comes from **self-shadowing through the density field** (matter shadows matter), from density
  ambient occlusion, from dust occlusion (dark flakes in front of the surface), and from haze falloff.

## Camera grammar (brief §13)

The behaviours are OBSERVER, DESCENT, COLLISION, INTERNAL, MICRO, REVEAL and IMPOSSIBLE. In the prototype
they are scripted per test, and in TEST 06 they are chosen by musical state:

- a phrase that builds coherence → **OBSERVER** (slow approach);
- high coherence → **DESCENT** onto the face;
- collapse → **COLLISION** (matter explodes toward the lens);
- a quiet section → **MICRO**;
- a section change → **REVEAL** (pull back);
- a fold → **IMPOSSIBLE** (the camera passes through the plate while it is folded).

The lens is long (24-35° vertical FOV) for scale. A wide lens makes things look small and near.

## Motion grammar

| Event | Duration | Look |
|---|---|---|
| Emergence | a phrase (4-16 s) | matter streams in along curved paths, eyes first; the surface grows as patches that merge |
| Face flash | 150-400 ms | a transient briefly over-binds eye and mouth matter. The face snaps into focus, then drifts back |
| Collapse | 60-200 ms of release, 1-2 s of flight | binding released with an impulse along the surface normal, streaked flakes, sparks, the bands strobe once, surface patches tear (the threshold rises), and the dust slows into chaos |
| Fold | 2-6 s | the domain warps (twist, depth swap, tunnel, inversion) while coherence holds, so the *form* bends while staying formed |
| Breath | low mid | the plate swells and the wing blades open, by a few percent |

## Rejected looks (to check every still against)

Particle sphere; neon tunnel; galaxy swirl; glowing dots; purple smoke; fire ball; liquid-metal blob;
kaleidoscope; generic fractal; skull; horned demon head; any architecture; any translucent "energy body";
any sacred-geometry overlay.
