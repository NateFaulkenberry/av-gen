# Rainbow World 02: research dossier

Gate 1, §3 of the brief. Each entry says what the source shows and what I take from it.

**Limits on this research.** I had web search but no browser. I read search results, abstracts, talk descriptions and
article text where the search returned it. I could not watch the GDC/Vimeo videos or look at the images in the
articles, so no web image is reproduced here. Claims are attributed to the source as I could read it, and anything I
could not confirm is marked *(unverified)*.

The owner's reference images were looked at directly. They are annotated on the board
(`~/Desktop/av-gen-review/74-rainbow-world/gate1/01-reference-board.png`) and in
[03-art-direction.md](03-art-direction.md) §1. All four appear to be AI-generated illustrations. I read them as mood
and colour targets, not as models of construction.

## A. Painterly environment art

| Source | What it shows | What I take |
|---|---|---|
| Steve Stark, *Creating a Ghibli-inspired painterly environment in Blender*, 80.lv: https://80.lv/articles/creating-ghibli-inspired-painterly-environment-in-blender | A full Blender painterly scene. Trees follow aVersionOfReality's tutorial, grass and flowers come from purchased packs, and the Custom Normals add-on gives clean stylised shading. | Painterly trees are a **normals** problem before they are a texture problem. |
| *Guide on creating Ghibli trees in 3D using Blender* (Lightning Boy Studio / David Forest), 80.lv: https://80.lv/articles/guide-on-creating-ghibli-trees-in-3d-using-blender/; BlenderNation: https://www.blendernation.com/2020/08/20/creating-ghibli-trees-in-3d/ | Bubble-silhouette canopies, built from leaf-card particle instances on a few lumpy volumes, with edited normals, non-destructively. | The canopy is designed as **3-7 readable masses**; the leaf cards only break their outline. |
| Habrador, *How to make stylized Witness trees*: https://blog.habrador.com/2018/05/how-to-make-stylized-witness-trees-in.html | Leaf normals pointed outward and upward from the canopy centre, so the canopy shades as one soft volume. | The normal transfer we use (sphere or hull proxy, then Data Transfer). |
| *Anime foliage pipeline*: https://trungduyng.substack.com/p/tutorial-blender-anime-foliage-pipeline | Camera-facing leaf planes, particles, a custom-normal transfer, then an "inflate" step. | Confirms the method. For AV Gen we keep the cards in world space, not camera-facing: a rolling camera makes billboards swim. |
| Polycount thread on *Breath of the Wild*-style foliage: https://polycount.com/discussion/comment/2696460 *(thread content partly unverified)* | Edited vertex normals smooth card shading; without them the cards' rotations show as sudden shading changes. | The same lesson for grass clumps. |
| *Change and Constant*, BotW GDC 2017 (Fujibayashi, Takizawa, Dohta): https://gdcvault.com/play/1024562/Change-and-Constant-Breaking-Conventions | Art director Takizawa, as reported by Tom's Guide: the right style makes it "easy to lie", balancing playability against realism. | Licence to exaggerate scale and curvature, provided the language stays consistent. |

**Production method, synthesised.**
1. Block the big forms first: terrain masses and canopy masses.
2. Set the shading by authored normals and broad gradients, before any texture.
3. Add a small number of breakup layers, sized to the camera distance.
4. Add accents last: flowers and highlights.

The finished look is about 70% shape and value design, 20% gradient colour and 10% texture.

## B. Environment composition

| Source | What I take |
|---|---|
| *The Level Design Book*, environment art: https://book.leveldesignbook.com/process/env-art *(URL inferred from a proxied search result)* | Readability, clustering and hero props/landmarks are named, distinct jobs. |
| 80.lv, *Approaching organic environment building*: https://80.lv/articles/approaching-organic-environment-building | Overusing leading lines clutters the scene until it cannot be read. Restraint. |
| Dan Cox, GDC, *What modern interior design teaches us about environment art*: https://www.gdcvault.com/play/1020421/ *(description only)* | Spaces tell the viewer their function. We use this for open "rest" meadows against dense groves. |

**For a fisheye and a rolling camera**, my own reasoning, to be checked in the Stage 3 and 4 views:
- **The centre is the only stable place.** Compose for a focal element near the centre (the clip does the same) and
  treat the outer third as a sweeping frame. Fine detail at the edge is wasted, because the lens compresses it.
- **Silhouettes against sky beat silhouettes against land.** When the image is rotated, a canopy against sky still
  reads; a canopy against a hillside turns to mush.
- **Up must stay legible without a horizon.** That needs a value and hue gradient that means "up":
  - the sky deepens to cobalt overhead;
  - a sea of cloud and warm light lies below;
  - every tree points away from its ground.
- **Foreground, middle ground and background become near, mid and far along the surface.** On a closed world the
  "background" is often other land overhead. Atmospheric perspective must therefore work by distance, not height.

## C. Surreal and impossible space

| Source | What it shows | What I take |
|---|---|---|
| William Chyr, *Manifold Garden*, GDC 2020 *Designing impossible geometry to be more approachable* and *Level design in impossible geometry*: https://gdcvault.com/play/1023553/Level-Design-Workshop-Level-Design, https://en.wikipedia.org/wiki/Manifold_Garden | Escher-*Relativity* gravity changes in Euclidean space. **World wrapping**: the level repeats in 3D, so every direction is filled and no edge is ever seen. The cost is rendering the geometry hundreds of times. Playtesters got lost when gravity shifted. | (1) Repetition is a tested answer to "no visible edge". (2) Gravity changes must be readable: each has a strong orientation cue. |
| *Super Mario Galaxy* planetoids (reviews and analysis, e.g. https://purenintendo.com/super-mario-galaxy-demystified/) | Gravity is the surface normal of whatever planetoid Mario is on; sections turn 90 or 180 degrees; levels loop back on themselves. | The camera's up follows the local surface normal. This is exactly the clip's behaviour, and it is cheap and deterministic. |
| *Inception* Paris fold, DNEG (Paul Franklin): https://www.artofvfx.com/inception-paul-franklin-superviseur-vfx-double-negative/, https://digitalmediaworld.tv/in-depth/inception-architecture-of-dreams | Real scanned geometry was folded in CG. The effect works because the folded city stays photographically coherent. | Fold the land, never the lighting logic. A fold is a designed form, not a shader trick. |
| M. C. Escher, *Relativity* (1953) | Three gravities in one coherent building; stairs serve two of them at once. | The land's twist should give two or three "downs" visible from one point. |

## D. Rainbows and luminous material

| Source | What it shows | What I take |
|---|---|---|
| Sadeghi, Muñoz, Laven, Jarosz, Seron, Gutierrez, Jensen, *Physically-based simulation of rainbows*, ACM TOG 2012: https://cs.dartmouth.edu/wjarosz/publications/sadeghi11physically.html | Full wave-optics rainbows: dispersion, interference and supernumeraries, which Lorenz-Mie theory confirms. It is offline. | We do not simulate. We borrow its **observable features** as design rules. |
| Atmospheric Optics (Les Cowley): https://www.atoptics.co.uk/fz533.htm, supernumeraries https://atoptics.co.uk/fza109.htm; University of Washington notes https://faculty.washington.edu/seattle/physics541/rainbow.pdf | The sky inside the primary bow is brighter. **Alexander's dark band** lies between the bows. The secondary is about 1.8x as wide, less than half as bright, and reversed. Supernumerary fringes sit inside the violet edge. | These are the cues that make a band read as *light in air* rather than *paint*. I tested them in E3. |
| Blenkhorn (UMBC), GPU glories/rainbows: https://mdsoar.org/handle/11603/20856 | Interactive Mie by sampling wavelength and angle over several frames. | Confirms that a true spectral rainbow is not a live-budget item. We use an authored ramp. |

**Three representations, distinguished:**
1. **Physical bow.** A view-dependent cone 40-42 degrees round the anti-solar point. It moves with the viewer and
   has no position. It can be done in the sky shader. On a rolling, closed world it would ride the sky and never
   frame anything.
2. **Stylised atmospheric bow.** World-space, soft, emissive and translucent, with the physical cues (inner
   brightening, dark band, secondary). It reads as light.
3. **Designed luminous arch.** A world-space object with thickness, which can be passed under, framed through and
   lit from. Reference R2.

Recommendation: the hero rainbows are **(3) built with the cues of (2)**. They are soft emissive volumes with a
spectral ramp, a brighter inner skirt, a dark gap and a faint secondary, and they spill coloured light onto land.
(1) is optional extra sky dressing. E3 shows that a flat striped ribbon reads as a sticker (see
[05-gate1-experiments.md](05-gate1-experiments.md)).

## E. Clouds, atmosphere and light

| Source | What it shows | What I take |
|---|---|---|
| Schneider and Vos, *The real-time volumetric cloudscapes of Horizon Zero Dawn*, SIGGRAPH 2015: https://www.guerrilla-games.com/read/the-real-time-volumetric-cloudscapes-of-horizon-zero-dawn | Art-directable raymarched cumulus within a 2 ms budget: powder effect, silver lining, density erosion with noise. | The lighting terms. AV Gen's ADR-1289 cloud renderer already has the silver lining, the powder term and the erosion. |
| Ang et al., *The technical art of Sea of Thieves*, SIGGRAPH 2018: https://history.siggraph.org/?p=84316; Kozin, GDC 2019: https://vimeo.com/326413164 | Art-directed 3D stylised cloudscapes "without using expensive raymarching techniques". | The precedent for stylised hero clouds rendered cheaply. Our fallback if raymarched lobes cost too much live. |
| Lightning Boy Studio, Ghibli clouds (BlenderNation): https://www.blendernation.com/2020/09/25/creating-ghibli-clouds-in-3d/; 80.lv anime clouds: https://80.lv/articles/anime-style-clouds-in-blender-step-by-step-tutorial | Stacked planes with normal maps and gradients for faked volume. Per the 80.lv article, the trick is "not just in the shader but in the geometry". | Cloud **silhouette design** happens in Blender as lobe clusters, whatever the renderer. |
| AV Gen ADR-1289 (`docs/decisions/ADR-1289-sculptural-clouds-from-authored-lobes.md`) | Clouds authored in Blender as lobe skeletons, marched against scene depth and lit by key, two-colour ambient and warm horizon. Static. **Live cost not measured.** | The first-choice path: the authoring pipeline exists. |

## F. Terrain and vegetation

| Source | What I take |
|---|---|
| Wohllaib, *Procedural grass in Ghost of Tsushima*, GDC 2021: https://gdcvault.com/play/1027214/Advanced-Graphics-Summit-Procedural-Grass | Per-blade GPU grass with procedural variation; blades widen when edge-on to the camera. |
| Rockenbeck, *Blowing from the West*, GDC: https://gdcvault.com/play/1027124/Blowing-from-the-West-Simulating | One wind field drives grass, trees and particles together, so the world moves as one. |
| hexaquo, *Grass rendering series*: https://hexaquo.at/pages/grass-rendering-series-part-1-theory/ | The trade-offs between blades, cards and billboards. Stylised art can use tall, plainly coloured blades. |

**Terrain method.** Large forms are authored as curves and profiles: sweeps, lofted cross-sections and a few sculpted
swells. A sparse set of designed bumps follows; there is no fBm. A **painted colour gradient** comes before any
noise. Noise is only a breakup layer at the distance where it shows.

**Grass on a closed, twisting surface.** AV Gen's ecology (ADR-048) scatters onto a heightfield world map, and a
band that goes upside down is not a heightfield. Grass and props are therefore placed in Blender and exported as
`distribution.points` transforms, which AV Gen already reads as full position, rotation and scale.

## G. Real-time rendering

| Source | What I take |
|---|---|
| Paul Bourke, fisheye for domes: https://paulbourke.net/papers/cgat09b/paper.pdf, slides https://paulbourke.net/papers/ecu2013/slides.pdf | Two routes. **Vertex-shader fisheye**: one pass, but every primitive must be tessellated, with hard-to-tune levels. **Multi-view**: four views cover a hemisphere, then resample. |
| Trapp and Döllner 2008, *A generalization approach for 3D viewing deformations*: https://www.scitepress.org/Papers/2008/10982 | Single-pass cubemap-based fisheye and panorama. |
| Vintage Story *Renderer Mangler* mod: https://mods.vintagestory.at/renderermangler | A cubemap fisheye costs up to about 3x vanilla at 360 degrees: a field data point for the multi-view cost. |
| AV Gen's own measurements: `docs/prototypes/generative-pass/02-report.md` §4 and `resonator-quality/10-report-live-performance.md` | The live budget is 29.3 ms GPU at 1080p30, with the Auto quality ladder and `avgen --live-profile` as the measuring tool. |

## Licensing research (summary; the full plan is in 04 §3)

| Source | Licence as found | Notes |
|---|---|---|
| Poly Haven | CC0. The FAQ allows any use; you may not claim authorship or relicense; please credit. https://polyhaven.com/license, https://docs.polyhaven.com/en/faq | The repo already has `tools/fetch_polyhaven.py` and CC0 nature assets in `assets/nature/` |
| ambientCG | CC0 *(confirmed only via third parties; check https://ambientcg.com before use)* | Textures only |
| Fab / Megascans | The Fab Standard License allows use in any engine. Megascans were free to end-2024; most are paid from 2025, with a free subset; the legacy Quixel library is under the older Epic Content License. https://www.unrealengine.com/en-US/blog/fab-epics-new-unified-content-marketplace-launches-today, https://forums.unrealengine.com/t/access-legacy-megascans-on-bridge-and-quixel-com/2142664 | **Not redistributable in a public repo.** Avoid unless an asset is irreplaceable; never commit. |
| Sketchfab | Per model: CC0, CC-BY, CC-BY-NC and so on. The store moved to Fab. | Accept only CC0 or CC-BY, verified on the model page, recorded with attribution |
