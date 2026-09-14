# ADR-171: "Glowmere Valley" names two scenes, and the successor is built on the one that is not called that

**Status:** Accepted
**Date:** 2026-09-14
**Scope:** which scene Glowmere Valley 2 succeeds

## The problem

`examples/index.json` carries seven Glowmere entries over two scene files, and **both scene files
declare `"name": "Glowmere Valley"`**:

| index entry | file | content |
|---|---|---|
| Glowmere Valley **- Painterly** | `world/glowmere-stylized.scene.json` | 16 nodes, 5 heroes, the wanderer, the UFO, water dressing, 23 audio routes |
| **Glowmere Valley** | `world/terrain.scene.json` | 6 nodes, **no heroes block**, no wanderer, no UFO, no water dressing |

A brief that says "Glowmere Valley 2" therefore does not, on its own, say which parent.

There is also no scene *identifier* to disambiguate with. `src/app/examples.cpp:48-54` reads
`project`, `scene` and `recipe` into one field — the distinction is documentation, not routing — and
`--example` selects by display-name match.

## The decision

**Glowmere Valley 2 succeeds `examples/world/glowmere-stylized.scene.json`.**

Three reasons, in order of weight.

1. **The prior audit already ruled on it.** `docs/glowmere-world-builder-audit.md:8-12` scopes itself
   to the painterly scene and says the older one "is out of scope and is not to be treated as the
   reference look." That is a decision this repo made, in writing, and re-deciding it silently would
   be worse than either answer.
2. **Every recent ADR measures it.** ADR-126, 138, 150, 151, 152, 153, 155 and 158 all load
   `glowmere-stylized.scene.json`. It is what "Glowmere" means in this repo's evidence base, and a
   successor measured against a different parent would not be comparable to any of it.
3. **The brief's own content only exists in that file.** §3.3 asks to preserve the Wanderer and the
   Visitor UFO; §3.4 asks to remove heroes that are merely primitive shapes. `terrain.scene.json`
   has no wanderer, no UFO and no heroes block at all. A brief describing objects that exist in only
   one of two candidates has named that candidate.

## What this decision is not

It is **not** a decision to deprecate, rename or touch `terrain.json`. It stays listed, stays named
"Glowmere Valley", and stays working — `docs/glowmere-valley-2/03-baseline.md` §6 asserts the whole
index still loads.

It is also **not** a claim that the naming is fine. Two scenes sharing a display name is a defect and
the successor will make it a three-way collision. Renaming `terrain.scene.json`'s entry — to
"Glowmere Valley - Generated", say, which is what it is — is the obvious fix and is deliberately
**not** taken here, because it changes a shipped example's name on the strength of an inference
about what a brief meant.

## The open question stays open

This is recorded as a decision because Phase 1 had to proceed on one of the two and the audit and
reuse matrix are written against this choice. **It is the first question put back to the user**
(`docs/glowmere-valley-2/04-plan.md` Q1), and the cost of being wrong is bounded and known: the
reuse matrix's hero, wanderer, UFO and audio-route rows would change; the terrain, water, vegetation
and camera analysis would not, because those parts are shared between the two files by construction —
both get their terrain from `world::defaultWorld()` in C++, neither carries a heightfield.

## Revisit when

- The user answers Q1 differently. Re-read `01-audit.md` §§1.9–1.12 and §2; nothing else moves.
- Somebody renames either entry, at which point this ADR is the record of why they collided.
