# CHRONOS, The Devourer: Sketchfab download shortlist for the owner

The coordinator asked for this after Gate D0. The MPFB look-dev read as a screaming human (a bald shiny dome, wig-like
hair, salmon-pink CG skin), not as an ancient, inhuman Goya entity. These are high-detail bases to build the creature
from, or to transfer detail from. Sketchfab downloads need an account, so the owner downloads them.

**How each was checked (2026-11-01).** Every licence was read from Sketchfab's own model record (the public API,
`api.sketchfab.com/v3/models/<uid>`: `license.slug`), the same data the model page shows.
- **Allowed licences:** CC BY 4.0 (`by`) or CC0 (`cc0`) only. No NC, no ND, no "Free Standard".
- **Excluded:**
  - game rips and fan art of IP;
  - scans of identifiable private people (a real person's face must not become a monster);
  - uploads that look mass-produced by AI generators (several accounts post dozens of near-identical "horror creature"
    models at about 2M faces);
  - gore.

**Format.** Choose **glTF Binary (.glb)** in Sketchfab's download dialog. Sketchfab offers an auto-converted glb for
every downloadable model.

**Textures and polycount.** The face counts below are Sketchfab's. Texture sets are as each page describes them. The
public record does not list archive contents, so please glance at the download dialog's texture list. Photogrammetry
scans carry one colour map, from the scan.

**Drop folders.** Put each download under `assets/chronos/sketchfab/<folder>/`. That path is gitignored, so it is
never committed.

## Tier 1: the head (pick these first)

| # | Model | Author | Licence | Faces | Textures | Why it fits | Drop folder |
|---|---|---|---|---|---|---|---|
| 1 | **Old man head (Miquel Blay)**: https://sketchfab.com/3d-models/old-man-head-miquel-blay-8fe5023517b74eddb564130cbf2e4869 | albert_sierra (albertsierra) | CC BY 4.0 | 220k | scan colour (plaster) | **The best base.** A 19th-century sculptor's study of a very old man (Museu de la Garrotxa, Olot): deep-set eyes, hollow temples and cheeks, a furrowed brow, a heavy beard and hair mass. It is already an artist's exaggeration of age, not a photo of a person, so pushing it toward Goya's Saturn is a short step: open the jaw, widen the stare, carve the cheeks. | `blay-old-man/` |
| 2 | **1917.722 Heroic Head of Pierre de Wissant (Rodin)**: https://sketchfab.com/3d-models/1917722-heroic-head-of-pierre-de-wissant-6af79e37f2bd4ee0b6c16342b2b06bad | clevelandart (Cleveland Museum of Art) | **CC0** | 623k | scan colour (bronze/plaster) | Rodin's anguished, gaunt head from *The Burghers of Calais*. Its surface is modelled clay with thumb marks: exactly the painterly, "Goya's paint" skin quality, in a real sculpture. CC0, so it needs no attribution. Use it whole, or bake its surface detail onto the creature. | `rodin-wissant/` |
| 3 | **Desiccated Head**: https://sketchfab.com/3d-models/desiccated-head-2ddd941467744e7a8ae82b0ffe0774a3 | Kade.Wolfe | CC BY 4.0 | 904k | as described on the page (sculpted and textured) | The "ancient, inhuman" end: skin dried tight over bone, a collapsed nose, sunken sockets. It is tagged "gore" for decay, but it is dry and mummified, with no blood. Use it as the target for how far the flesh recedes, or for baking that leathery tightness onto parts of the face. | `desiccated-head/` |
| 4 | **Character Study Strong Smell (Messerschmidt)**: https://sketchfab.com/3d-models/character-study-strong-smell-messerschmidt-74d72c71baf24c3dac1345f700c75647 | OVERHEAD (Overhead4D) | CC BY 4.0 | 189k | scan colour | Franz Xaver Messerschmidt's 1770s "character heads" are the 18th century's own expressionist faces, made in Goya's lifetime. This one is a violent grimace with the mouth stretched open. It is the reference for the strained mouth, the cords and the creases. Tagged "noai" by its author. | `messerschmidt-strong-smell/` |
| 5 | **Vexed Man (Messerschmidt)**: https://sketchfab.com/3d-models/vexed-man-by-franz-xaver-messerschmidt-c6f8ae3c27fa4e53a7b53bc2d7130b84 | jtressle | CC BY 4.0 | 159k | scan colour | A second Messerschmidt head, a scowl with deeply etched folds: a good wrinkle-detail donor for a normal-map bake. | `messerschmidt-vexed/` |
| 6 | **Old man Face sculpt**: https://sketchfab.com/3d-models/old-man-face-sculpt-c1d3569eb9c54f8f8fc4c1a897c37041 | bjornaraarset (Bjørnar Aarset) | CC BY 4.0 | 722k | none (sculpt) | A ZBrush study of an aged face: heavy lids, eye bags and furrows, at high resolution. It is a face mask, not a full head, so it is a **detail source** for baking wrinkles onto the base, not the base itself. | `aarset-old-face/` |

## Tier 2: the mouth, the teeth, the skull

| # | Model | Author | Licence | Faces | Textures | Why it fits | Drop folder |
|---|---|---|---|---|---|---|---|
| 7 | **Blind Creature Head**: https://sketchfab.com/3d-models/blind-creature-head-ab5ed6138d274c879646104a5442e94f | gunsesfercho (fercho gunses) | CC BY 4.0 | 200k | as on the page | A screaming maw packed with long, irregular, layered teeth. It is the closest free model to refs r10 and r14. Kitbash its teeth and gum ridge into Chronos's mouth (we keep our own face; this is not the creature). | `blind-creature-head/` |
| 8 | **Human teeth**: https://sketchfab.com/3d-models/human-teeth-c4c569f0e08948e2a572007a7a5726f2 | Dessen (Alexander Antipov) | CC BY 4.0 | 15.6k | colour, specular and normal, 2048² | Realistic teeth, gums and tongue with real texture maps. We would age, yellow, lengthen and misalign them in Blender. It is a better start than MPFB's neat set. | `human-teeth/` |
| 9 | **Realistic Skull**: https://sketchfab.com/3d-models/realistic-skull-16a0dd9855834986bdb853017ef72b04 | walrusgoz | CC BY 4.0 | 207k | as on the page | An anatomical skull to sculpt the gaunt face over: so the brow, cheekbones and jaw read as bone under thin skin, and so the jaw's 60°+ dislocation stays anatomically plausible. | `skull-walrusgoz/` |
| 10 | **Realistic Human Skull study** (light alternative to 9): https://sketchfab.com/3d-models/realistic-human-skull-study-2a1aa0be81454dc2a9a59110ac2edc17 | azurehusky (Meri L.) | CC BY 4.0 | 42k | as on the page | The same purpose, lighter. | `skull-azurehusky/` |

## Tier 3: optional references (download only if you like them)

| Model | Author | Licence | Faces | Use |
|---|---|---|---|---|
| Laocoön and His Sons: https://sketchfab.com/3d-models/laocoon-and-his-sons-649111a9a7b74ddab3937292be5545fc | rigsters | CC BY 4.0 | 50k | The anguish in Laocoön's face. Reference only. |
| Grotesque Face by Da Vinci: https://sketchfab.com/3d-models/grotesque-face-by-da-vinci-a05e0dbd27ce43c8870a954a3a879224 | Tatein | CC BY 4.0 | 70k | Leonardo's grotesque proportions (the jaw and nose). Reference only. |
| Study of Honoré de Balzac (Rodin): https://sketchfab.com/3d-models/1972277-study-of-honore-de-balzac-093a9b8ec24440afb1e0cea48ac6d7ce | clevelandart | CC0 | 195k | More Rodin clay surface, if the Wissant head is not enough. |

**What I would download first:** 1, 2, 7 and 8. With those I can build a head that is a sculptor's old man pushed
into Goya: Blay's head carrying Rodin's surface, Blind Creature's teeth reworked with Dessen's maps, and our own jaw
rig. 3, 4, 5 and 9 refine it.

## Considered and excluded

- **Scans of identifiable private people:** "My bald father bust", "Bruno Hardwick", "3d scan man 1". A real person's
  face should not become the monster.
- **"Pigcraft" (s8819296) and "Purple.Point" uploads:** apparent mass AI generation, at about 2M faces each.
- **Nosferatu Bust:** a stylized, generic vampire.
- **Zombie heads:** gore, and generic.
- **Undead Horror Creature:** a game-asset store upload.
- **Anything NC or ND:** none was shortlisted.

## Attribution (CC BY 4.0), to carry wherever the work is shown

- "Old man head. Miquel Blay" (https://sketchfab.com/3d-models/old-man-head-miquel-blay-8fe5023517b74eddb564130cbf2e4869) by albert_sierra, CC-BY-4.0.
- "Desiccated Head" (https://sketchfab.com/3d-models/desiccated-head-2ddd941467744e7a8ae82b0ffe0774a3) by Kade.Wolfe, CC-BY-4.0.
- "Character Study Strong Smell | Messerschmidt" (https://sketchfab.com/3d-models/character-study-strong-smell-messerschmidt-74d72c71baf24c3dac1345f700c75647) by OVERHEAD, CC-BY-4.0.
- "Vexed Man by Franz Xaver Messerschmidt" (https://sketchfab.com/3d-models/vexed-man-by-franz-xaver-messerschmidt-c6f8ae3c27fa4e53a7b53bc2d7130b84) by jtressle, CC-BY-4.0.
- "Old man Face sculpt" (https://sketchfab.com/3d-models/old-man-face-sculpt-c1d3569eb9c54f8f8fc4c1a897c37041) by Bjørnar Aarset, CC-BY-4.0.
- "Blind Creature Head" (https://sketchfab.com/3d-models/blind-creature-head-ab5ed6138d274c879646104a5442e94f) by fercho gunses, CC-BY-4.0.
- "Human teeth" (https://sketchfab.com/3d-models/human-teeth-c4c569f0e08948e2a572007a7a5726f2) by Alexander Antipov (Dessen), CC-BY-4.0.
- "Realistic Skull" (https://sketchfab.com/3d-models/realistic-skull-16a0dd9855834986bdb853017ef72b04) by walrusgoz, CC-BY-4.0.
- "Realistic Human Skull study" (https://sketchfab.com/3d-models/realistic-human-skull-study-2a1aa0be81454dc2a9a59110ac2edc17) by Meri L. (azurehusky), CC-BY-4.0.
- The Rodin heads (Cleveland Museum of Art) are CC0 and need no attribution, though crediting the museum is courteous.

Only the models actually used get logged in `assets.md` once downloaded.
