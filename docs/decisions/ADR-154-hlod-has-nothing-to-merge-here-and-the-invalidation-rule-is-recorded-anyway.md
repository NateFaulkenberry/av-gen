# ADR-154: HLOD has nothing left to merge on instanced content, and the invalidation rule is recorded anyway

**Status:** Accepted
**Date:** 2026-09-13
**Decides:** C6 (HLOD proxies) and C7 (HLOD invalidation)
**Supports:** ADR-151

## Why C6 is not merely unjustified but structurally redundant here

ADR-151 refuses C6 on measurement. This records the reason the measurement came out that way, because
"the number was small" and "there was nothing for the mechanism to do" are different findings and
only the second one generalises.

An HLOD proxy earns its keep two ways: it **collapses draw calls**, and it **enlarges triangles**.
Target architecture §5.4 leans on the first — "both collapse *instances*, which is what Karis says
Epic's own artists hit first". Neither is available on this content.

**Draw calls are already collapsed, further than a proxy could.** `procedural_renderer.cpp` issues
**one indirect draw per (layer, rung)**, with the instance count written by the cull pass on the GPU,
and it skips a rung that has been empty for several frames. A layer of 97,161 grass instances is at
most four draws whatever the camera does. Merging a stand of them into a proxy replaces some number
of draws with one draw — but that number is already at most four, and one merged proxy *per group*
would be more draws, not fewer. The mechanism runs backwards here.

**Triangles are already the ladder's job.** Enlarging triangles is what `makeLodMesh` and the ADR-029
ladder do, per instance, on the GPU, with hysteresis and spread (ADR-082, ADR-132). A merged proxy
enlarges them once, offline, for a group — the same job, done with a longer-lived derived copy and a
coarser granularity. ADR-151 measures what is left after the ladder has done it, and then measures
that **most of even that remainder is reachable by choosing different rungs of the same ladder**
(−4.72% of the weighted cost, half the frame's triangles, from `RepresentationSelector` as built).

HLOD is the right answer where each instance is its own draw and its own object — a city of
individually placed buildings, Epic's case. Glowmere's ecology is not that, and Glowmere's *authored*
entities, which are that, were measured by ADR-126 at **0.0% sub-pixel triangles** after LOD0.

## C7: the invalidation rule, recorded rather than implemented

Risk register row 3 rates a stale proxy **High**, and the brief calls it the highest-risk task in the
plan for a good reason: a stale proxy renders a *wrong image* rather than a slow one, and this
engine's forensics found nine of eleven defects at exactly this kind of ownership boundary.

Nothing is built, so there is nothing to invalidate and no test to write. What is written down is the
contract the next person inherits, so that the risk is carried forward with the item rather than
being rediscovered:

> **An `HlodGroup` is invalidated by any change to its member set, to any member's transform, to any
> member's mesh, or to the scene identity or mesh version it was built against. It is rebuilt at
> scene load and on edit. It is never consulted while invalid — an invalid group falls back to
> drawing its members, never to drawing a stale proxy.**

Three things about that rule are load-bearing and are not in §5.5's version of it:

- **The fallback is members, not "the last good proxy".** A proxy that is merely out of date is the
  failure mode that is hard to see and therefore ships. Falling back to correct-and-slow makes the
  failure a frame-rate symptom instead of an image symptom.
- **Validity is keyed the way `MeshMetricsCache` already keys it** — on `(Scene::identity,
  Scene::meshVersion)` plus the member set — because that pattern is established in this codebase
  (ADR-122) and a second, differently-shaped invalidation rule is itself the boundary defect.
- **Offline forces the top representation** (ADR-125, §5.9), so an offline render must never consult
  a proxy at all, valid or not. That is the property to assert first, because it is the one whose
  violation ships in a deliverable.

The tests C7 would have written, for whoever writes it: a proxy consulted after a member moves fails;
after a member is added or removed fails; after a member's mesh version moves fails; in offline mode
is never consulted; and an invalidated group renders **identically** to the same scene with proxies
disabled — which is the assertion that actually catches staleness, because it compares against
ground truth rather than against a flag.

## Rejected alternatives

- **Build the `HlodSystem` module anyway**, since §5.6 names it. Rejected: "adding a module boundary
  with nothing behind it is how a renderer acquires abstraction without capability" is §5.6's own
  sentence, and it applies to itself.
- **Build C7's invalidation tests now, against a stub**, so the hard part is done first. Tempting,
  and the brief asks for tests first. Rejected because a test against a stub asserts the stub: it
  cannot fail in the way a real stale proxy fails, so it would give the next person confidence that
  the risk was retired when it was only postponed. The contract above is more honest than a green test.
- **HLOD for the authored entities instead of the ecology**, where the draw-call argument does hold.
  Rejected on ADR-126's measurement: 74 drawables, 0.0% of them below the quad threshold.

## Revisit when

A scene exists whose cost is in individually-drawn objects rather than instanced scatter — §45's City
stress scene is exactly that, and is on the board. The draw-call argument above reverses there, and
this ADR should be re-read rather than cited.

## Verified vs assumed

**Verified:** the one-indirect-draw-per-(layer, rung) structure and the empty-rung skip, read in
`procedural_renderer.cpp`. ADR-126's 0.0% figure for authored entities, reproduced by ADR-150's walk.

**Assumed:** that no proxy scheme exists that beats the ladder on this content by some route neither
of the two mechanisms above describes. That is an argument from the mechanisms, not a measurement,
which is why ADR-151's deletion ceiling is the load-bearing evidence and this ADR is the explanation.
