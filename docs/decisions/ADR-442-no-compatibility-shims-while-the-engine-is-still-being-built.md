# ADR-442: No compatibility shims while the engine is still being built

- Status: Accepted (2026-09-20).
- Generalises ADR-441 (the Cosmic Ocean is removed) into a standing rule.
- Constrains ADR-500 (an effect is one file and four lines) from the other end: the registry makes
  a kind cheap to add, and this makes one cheap to take back out.
- Does NOT modify ADR-360 (the determinism contract). See "What this does not license".

## The rule

**While AV Gen is in active heavy development, removing an effect, a kind or an authored block does
not require a migration path.** Strip it from the tracked content that names it, in the same commit
that removes the code. Do not write a load-time alias, a deprecated-key reader, a defaulted shim or
a version bump to keep files working that nobody is going to open again.

The owner's instruction, which is the whole of the reasoning: *"You don't need backwards compat for
vortex just take it out of scenes if it exists there don't make shims to support it."* And the
condition that makes it correct: **we are still in active heavy development.** Every file that names
a cut effect is in this repository, authored by us, and reachable by `git grep`.

## Why a shim is worse than a strip, specifically

A compatibility layer is not free and it does not sit still.

**It outlives the thing it supports.** A load-time alias for a kind that no longer exists is code
with no caller, and this repository has an ADR family (ADR-350, and four subsystems shipped
unreachable in one session) about exactly what happens to code nobody reaches: it rots, the tests
that cover it pass forever, and nothing distinguishes "working" from "never called".

**It constrains the design of the replacement, which is the expensive part.** The concrete case:
the `Vortex` retirement was first planned as retire-with-alias, and the alias meant the
`/vortex/...` JSON block had to keep its name — which meant `world::PlacedMedium` in the new
volumetric foundation would have carried a struct named after a deleted kind, permanently, to serve
files we were about to edit anyway. Waiving compatibility did not just save writing the alias; it
freed the foundation to name the thing for what it is. **The cost of a shim is mostly paid by the
code that comes after it.**

**The migration is almost always smaller than the shim.** The Vortex appears in eight tracked files
(`constellation`, `infinite`, `library`, `machine`, `reassembly`, `stress`, and both halves of
`tree-of-life-floating-island`). The Cosmic Ocean appeared in **none** — the only files naming it
were its own A/B diagnostic arms. Checked, not assumed: `git grep` answers this in a second, and
ADR-441's removal found a kind that shipped, conformed and drew, that no project anybody renders had
adopted. An alias for that would have been a compatibility layer for zero files.

## What this does not license

**Not a licence to break renders silently.** ADR-360 is untouched: a render must still be
reproducible, and two renders of the same range may not differ. Stripping an effect out of a project
*changes what that project renders*, and that is a visible edit to tracked content, made on purpose,
in a commit that says so. It is not the same act as a frame quietly moving.

**Not a licence to delete without recording.** ADR-441's own argument stands: the next person to
read a careful design document for a subsystem that is not in the tree deserves to find out why from
an ADR and not from a git log. Record the removal and what it cost.

**Not a licence to skip the grep.** "Nothing uses it" is a measurement, not an assumption — ADR-385.
Strip from tracked content *before* claiming nothing references it, and say how many files it was.

**Not a rule about data the owner produced.** This covers effects, kinds and the authored blocks
that configure them — engine features and our own example content. A user's project files, renders
and recorded work are not ours to break on this reasoning.

## When this expires

This rule holds while the only author of AV Gen content is the team building AV Gen. It inverts the
day somebody outside this repository has projects worth preserving: at that point a cut effect needs
a migration, because the files naming it are no longer reachable by `git grep` and no longer ours to
edit. Whoever notices that has happened should supersede this ADR rather than quietly stop following
it.
