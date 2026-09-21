# ADR-617: A document that tells you how to check cannot go stale the way one that tells you what is true can

**Status:** Accepted
**Date:** 2026-09-21
**Related:** ADR-615 (staged and dark; the tenth shape), ADR-558 (a control that does nothing),
ADR-616 (three instrument defects), `docs/testing.md` #29, #31
**Implemented by:** `src/entity/character_ai.hpp` §0 and §2

---

## The measurement that forced this

`src/entity/character_ai.hpp` declares itself **normative** and exists so that several agents
working in parallel do not each invent their own perception layer, character-state type or
navigator. It is the first file anyone reads before touching character work.

Audited over three rounds it produced **four independent stale assertions** and this:

> **Of the twelve `file:line` coordinates in its §0 inventory table, zero land on the type they
> name.** One points at a blank line, one at a closing brace, one at an unrelated `scale` member.

Not one was wrong when written. All twelve rotted. And §2's heading — *"Perception — the layer that
genuinely does not exist"* — had become **a build instruction to write a second `GridPerception`**,
in the file whose entire purpose is preventing exactly that. A document that causes the outcome it
was written to prevent is the worst failure available to it.

## Why patching lost, and deleting would have lost too

**Patching lost three rounds.** Accurate coordinates written today are stale coordinates in a
month, because of a structural property the audit named:

> **The copy that goes stale is the one furthest from the code being changed.** `"zero call sites"`
> appears three times in this repository. When ADR-274 made it false, the two copies *beside the
> code* were rewritten to past tense and the normative header was not. `character_ai.hpp` is stale
> not because someone missed it once, but because it is structurally the **last** file anyone edits.

**Deleting would also have lost**: the file has a real job and does it. The problem is not that it
exists; it is what its sections are made of.

## The decision: change what the sections assert

**Assertions about code state are the class that rots.** "X does not exist", "the type is at
`file:line`", "nothing implements this yet" — each is falsified by an ordinary edit somewhere else,
which is a thing nobody notices. So §0 and §2 were rewritten out of that class:

1. **Purpose and invariants, not inventory.** *Why* perception is centralised, and what must remain
   true of it (a query not a store; per-character budget as the constraint; a selectable source so
   a behaviour can be bisected against an omniscient one; no second reader of the global list).
   **An invariant is falsified by a design change, which someone does on purpose. Inventory is
   falsified by an edit elsewhere, which nobody notices.**
2. **The grep rather than the answer.** §2 now opens with the command that finds the
   implementations. **A line that says "run this to find them" cannot be wrong about how many there
   are.** This is not a new idea in the file — it is the idea that caught the error: the old text
   told the reader to grep for `jointMask` to confirm masks did not exist, and that grep returns
   hits. **The document supplied the evidence against itself and nobody ran it.** Leaning into that
   is the whole of this ADR.
3. **Names, never coordinates.** A name survives a move; a line number is invalidated by any
   insertion above it.
4. **The old text kept and marked as history**, because the argument is what produced the layer and
   a reader tracing why the engine has one should find it.
5. **The reason written into the file**, so the next person tempted to add an inventory section
   meets the argument against it first.

> **A document that tells you how to check is strictly harder to falsify than one that tells you
> what is true.**

### An instruction is code, and the first draft of these was wrong

**Harder to falsify is not unfalsifiable.** The first version of §0's interface grep restricted the
search to `--include='*.hpp'`. Every `IBehavior` in this engine is implemented in
`entity/behaviors.cpp`, so that instruction returns **none** — and "no implementations found" reads
exactly like "nobody has built one yet", which is the failure the section exists to prevent,
reintroduced through the instruction meant to prevent it.

Caught by running it before shipping it, which is the only check that works:

> **An instruction to run a command is code. Run it before you write it down, and confirm it finds
> a case you already know about.** A grep that returns nothing is indistinguishable from a grep
> that is wrong.

Incidentally confirming the rot it replaced: the old table put `EntityState` at `behavior.hpp:48`;
it is now at line 54.

## The companion rule, from the same day

ADR-616 records a `--root-vel` sweep that assigned the weight *after* the database had copied it:
four byte-identical arms, while faithfully printing the weight it had been handed. It was caught
only because the arms were printed side by side.

> **A sweep prints its arms. A dead knob is invisible in an aggregate and obvious in a column.**

Same shape as this ADR one layer over: a summary asserts a conclusion, a column lets the reader
check it.

## Consequences

- **`character_ai.hpp` §0 and §2 assert nothing about code state.** Anything that must name a
  symbol names it; anything that would count or locate gives the command instead.
- **This is not a licence to stop describing things.** Purpose, invariants, rationale and measured
  numbers with their conditions all belong in headers and all survive. What does not survive is
  *inventory* — and the further a file sits from the code it inventories, the faster it rots.
- **The audit that produced this is worth more than the four fixes.** 27 of 33 absence-claims
  checked out, which is what makes the six credible; and the auditor recommended automating fewer
  patterns than it was asked to, having measured that the widened ones were noise.
