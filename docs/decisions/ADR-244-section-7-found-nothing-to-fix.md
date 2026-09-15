# ADR-244: §7 found nothing to fix, and that moved the bottleneck

**Status:** Accepted
**Date:** 2026-09-15
**Context:** Level 3 §7, the cinematic lighting evaluation, reviewed by a human
**Follows:** ADR-243 (§4 reviewed; the detector and the eye disagree)

## Context

§7 — depth, separation, focal hierarchy — has sat open for the same reason §4 did: it requires
somebody to look. Three stills per shot at 25/50/75% through the piece and an eight-second clip from
the midpoint were rendered for all five shots, and the reviewer was given six diagnostic yes/no
questions and the six-criterion rubric from `docs/visual-quality.md`.

Each question was chosen to map onto a knob rather than to produce a score: *can you point to where
the light is coming from* is a key-to-fill ratio; *does anything look pasted on* is contact shadows
or ambient occlusion missing where an object meets the world; *squint — can you separate foreground
from midground from background* is atmospheric perspective.

## What came back

**The scope was narrowed by the owner, and legitimately.** Only Glowmere was reviewed: the other four
are the cinematic-phase master scenes and *"were just tests and will likely be discarded"*. A review
of scenes nobody intends to ship would have produced work nobody wants done.

| question | what a bad answer would have meant | Glowmere |
|---|---|---|
| squint: three layers separable? | atmospheric perspective too weak | **yes** |
| where does the eye land first? | — | mushrooms / aliens |
| is that the subject? | the rig is keyed on the wrong thing | **yes** |
| can you point to the light? | fill too high; nothing describes form | **yes** |
| anything pasted on? | contact shadows or AO missing | **no** |
| does the air have presence? | volumetrics off, thin, or smothering | **some** |

Six for six. And on the rubric:

> *"I would put those all around 7 or 8 with their limiting factors being artistic, ie: more time on
> my end manually tweaking the scene. I don't think what you're doing is causing scores lower than
> 10."*

## Decision

**§7 closes with no renderer defect found, and the finding is recorded so it is not re-opened
speculatively.** Every diagnostic that was designed to expose a specific, fixable lighting failure
came back clean on the one scene that is actually being made. There is no key/fill correction, no
contact-shadow gap, no atmospheric-perspective deficit and no hierarchy inversion to chase.

The rubric's own rule is that a score of 5 or below is a defect rather than a taste question. Nothing
scored at or below 5. A 7–8 attributed by the author to their own remaining authoring time is not a
renderer defect, and treating it as one would be inventing work.

**The four master scenes stay in `docs/visual-quality.md`.** They are the spec's own statement of the
bar and removing them is not this ADR's call; what is recorded is that the owner has deprioritised
them, so a future reader does not mistake four stale test scenes for the current quality target, and
does not score the engine against them.

## Consequences

**The bottleneck is now authoring, not rendering.** This is the useful half of a clean result. Item
12 of what Phase 1 still owes — *"Scene authoring ergonomics (§17, §G)"* — has been a one-line
placeholder with no detail behind it. It is now the highest-value item in that list, promoted by
evidence rather than by preference: the person making the scenes says the thing standing between a 7
and a 10 is time spent tweaking, and tools are what buy that back.

**Two of the mandate's items that "genuinely need a human" are now done** (§4 in ADR-243, §7 here).
The third, the performance dashboard at §13, still does — an agent cannot honestly certify an ImGui
surface by reading its source, and this session has an ADR about exactly that failure mode.

**What this does not say.** One scene, at three moments, in stills plus one clip. It is not a
statement that the lighting system is complete, and it is explicitly not a statement about the four
master scenes, which were not reviewed. A different world — a daylight exterior, an interior, a scene
with no emissive content at all — is a different experiment, and Glowmere is an emissive night world
whose lighting is the easiest case this engine has.
