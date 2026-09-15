# ADR-201: There is no primary hero, only importance

**Status:** Accepted
**Date:** 2026-09-14

> "can we remove the concept of there being a primary hero? if I want to make a primary hero I can
> designate that with importance sliders"
> "let's also cut this hero dropdown, it is redundant"

## The marker

Heroes are kept ranked by importance, so the "primary hero" was never a separate thing to set — it
was already "whichever has the highest importance". The `*` beside the top of the list, and the `*`
on its viewport label, named a category that does not exist, and invited an author to look for a
control that was never there. Both are gone. The importance number is on the label already and says
the same thing without implying a switch.

## The list

The Heroes panel listed every hero with a star beside it, immediately above an object list where
every one of those heroes already had a star on its own row. For a hero standing on an object that is
the same information twice.

What the rows cannot do is the case the original comment was written for: **a hero can outlive its
object** — a scene file written by hand, a node renamed or deleted. That hero has no row, so with no
list at all it is a subject the director keeps travelling to and nothing in the application can take
back.

So the list stays for exactly that case and is titled "Heroes with no object". In the ordinary
scene — every hero on a node — it does not appear at all.

## What has *not* changed, and is worth being plain about

The director still treats the top-ranked hero differently in one way: `heroOwns` gives it every build
and every drop, and the supporting cast gets the sections in between. That is a categorical split,
not a weighting, and removing the marker does not remove it. ADR-190 softened it — after four hero
shots in a row the next section goes to the cast whatever its kind — but a genuinely continuous
"cast by importance" would be a different director.

**Recorded rather than quietly done**, because it is a change to how films come out and the request
was about the interface. It is a small change if it is wanted: replace the hero/supporting split in
`directFromStructure` with an importance-weighted draw.
